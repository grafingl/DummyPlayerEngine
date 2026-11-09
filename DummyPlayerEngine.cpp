// DummyPlayerEngine.asi
// Машина игрока звучит как машины NPC: без отдельных сэмплов разгона/сброса газа
// и без "игроковской" логики двигателя.
//
// Поддерживаемые игры (один и тот же .asi):
//   - GTA San Andreas 1.0 US
//   - GTA Vice City 1.0
//   - GTA III 1.0
//
// San Andreas:
//   В начало CAEVehicleAudioEntity::ProcessPlayerVehicleEngine (0x4FBB10) ставится
//   переход на CAEVehicleAudioEntity::ProcessDummyVehicleEngine (0x501480).
//   Сигнатуры одинаковые (__thiscall, один параметр), поэтому хватает одного jmp.
//   Адреса из gta-reversed.
//
// Vice City:
//   В cAudioManager::ProcessVehicleEngine есть ветка
//       if (playerVeh == veh && veh->m_modelIndex != MI_CADDY) {
//           ProcessPlayersVehicleEngine(params, veh); return;
//       }
//   Условный переход "это гольф-кар?" (je по адресу 0x5F218A) заменяется на
//   безусловный, и машина игрока всегда идёт по ветке NPC. Состояние FPU-стека
//   в обеих ветках одинаковое (проверено по дизассемблеру gta-vc.exe 1.0).
//   Перед патчем мод сверяет байты вокруг этого места.
//
// GTA III:
//   В cAudioManager::ProcessVehicleEngine есть ветка
//       if (FindPlayerVehicle() == veh) { ProcessPlayersVehicleEngine(params, automobile); return; }
//   Условный переход "не машина игрока" (jne по адресу 0x56A6BF) заменяется на
//   безусловный, и машина игрока всегда идёт по ветке NPC. Состояние FPU-стека
//   в обеих ветках одинаковое (проверено по дизассемблеру gta3.exe 1.0).
//   Перед патчем мод сверяет байты вокруг этого места.

#include <windows.h>
#include <stdint.h>

static bool IsReadable(uintptr_t addr, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
        return false;
    }
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) {
        return false;
    }
    return addr + size <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static bool BytesEqual(uintptr_t addr, const uint8_t* bytes, size_t size) {
    if (!IsReadable(addr, size)) {
        return false;
    }
    const uint8_t* p = (const uint8_t*)addr;
    for (size_t i = 0; i < size; ++i) {
        if (p[i] != bytes[i]) return false;
    }
    return true;
}

static void WriteBytes(uintptr_t addr, const uint8_t* bytes, size_t size) {
    DWORD oldProt;
    VirtualProtect((void*)addr, size, PAGE_EXECUTE_READWRITE, &oldProt);
    uint8_t* p = (uint8_t*)addr;
    for (size_t i = 0; i < size; ++i) p[i] = bytes[i];
    VirtualProtect((void*)addr, size, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, size);
}

// ---------- GTA San Andreas 1.0 US ----------

static bool IsSA10US() {
    return IsReadable(0x82457C, 4) && *(uint32_t*)0x82457C == 0x94BF;
}

static void PatchSA() {
    const uintptr_t from = 0x4FBB10; // CAEVehicleAudioEntity::ProcessPlayerVehicleEngine
    const uintptr_t to   = 0x501480; // CAEVehicleAudioEntity::ProcessDummyVehicleEngine
    const int32_t rel = (int32_t)(to - (from + 5));
    const uint8_t jmp[5] = {
        0xE9,
        (uint8_t)(rel), (uint8_t)(rel >> 8), (uint8_t)(rel >> 16), (uint8_t)(rel >> 24),
    };
    WriteBytes(from, jmp, sizeof(jmp));
}

// ---------- GTA Vice City 1.0 ----------

// Код 0x5F217F..0x5F2199 в gta-vc.exe 1.0:
//   jne  0x5F21A0                 ; машина не игрока -> ветка NPC
//   movsx eax, word [esi+0x5C]    ; m_modelIndex
//   cmp  eax, 0xBB                ; MI_CADDY
//   je   0x5F21A0                 ; гольф-кар -> ветка NPC   <-- патчим в jmp
//   fcompp
//   fcompp
//   mov  ecx, ebx
//   push esi
//   push edi
//   call 0x5F0EA0                 ; cAudioManager::ProcessPlayersVehicleEngine
static const uintptr_t VC_SITE = 0x5F217F;
static const uint8_t VC_ORIGINAL[] = {
    0x75, 0x1F,                         // jne +0x1F
    0x0F, 0xBF, 0x46, 0x5C,             // movsx eax, word ptr [esi+0x5C]
    0x3D, 0xBB, 0x00, 0x00, 0x00,       // cmp eax, 0xBB
    0x74, 0x14,                         // je +0x14
    0xDE, 0xD9, 0xDE, 0xD9,             // fcompp; fcompp
    0x89, 0xD9,                         // mov ecx, ebx
    0x56, 0x57,                         // push esi; push edi
    0xE8,                               // call ...
};
static const uintptr_t VC_JE = 0x5F218A;

static bool IsVC10() {
    // Точка входа gta-vc.exe 1.0 и байты в месте патча
    static const uint8_t entry[4] = { 0x55, 0x89, 0xE5, 0x53 };
    return BytesEqual(0x667BF0, entry, sizeof(entry));
}

static bool PatchVC() {
    if (!BytesEqual(VC_SITE, VC_ORIGINAL, sizeof(VC_ORIGINAL))) {
        return false;
    }
    static const uint8_t jmpShort[1] = { 0xEB }; // je -> jmp
    WriteBytes(VC_JE, jmpShort, sizeof(jmpShort));
    return true;
}

// ---------- GTA III 1.0 ----------

// Код 0x56A6B5..0x56A6CE в gta3.exe 1.0:
//   cmp  eax, ebp                 ; FindPlayerVehicle() == veh ?
//   fldz x4
//   jne  0x56A6D3                 ; не машина игрока -> ветка NPC   <-- патчим в jmp
//   fcompp
//   fcompp
//   mov  ecx, ebx
//   push ebp
//   push edi
//   call 0x56B0D0                 ; cAudioManager::ProcessPlayersVehicleEngine
static const uintptr_t III_SITE = 0x56A6B5;
static const uint8_t III_ORIGINAL[] = {
    0x39, 0xE8,                         // cmp eax, ebp
    0xD9, 0xEE, 0xD9, 0xEE,             // fldz; fldz
    0xD9, 0xEE, 0xD9, 0xEE,             // fldz; fldz
    0x75, 0x12,                         // jne +0x12
    0xDE, 0xD9, 0xDE, 0xD9,             // fcompp; fcompp
    0x89, 0xD9,                         // mov ecx, ebx
    0x55, 0x57,                         // push ebp; push edi
    0xE8, 0x02, 0x0A, 0x00, 0x00,       // call 0x56B0D0
};
static const uintptr_t III_JNE = 0x56A6BF;

static bool IsIII10() {
    // Точка входа gta3.exe 1.0
    static const uint8_t entry[8] = { 0x55, 0x89, 0xE5, 0x53, 0x83, 0xEC, 0x48, 0x55 };
    return BytesEqual(0x5C1E70, entry, sizeof(entry));
}

static bool PatchIII() {
    if (!BytesEqual(III_SITE, III_ORIGINAL, sizeof(III_ORIGINAL))) {
        return false;
    }
    static const uint8_t jmpShort[1] = { 0xEB }; // jne -> jmp
    WriteBytes(III_JNE, jmpShort, sizeof(jmpShort));
    return true;
}

// ---------------------------------------------

static void Warn(const wchar_t* text) {
    MessageBoxW(nullptr, text, L"DummyPlayerEngine", MB_OK | MB_ICONWARNING);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    DisableThreadLibraryCalls(hinst);

    if (IsSA10US()) {
        PatchSA();
    } else if (IsVC10()) {
        if (!PatchVC()) {
            Warn(L"DummyPlayerEngine: код Vice City не совпадает с версией 1.0.\n"
                 L"Возможно, exe изменён другим модом. Мод не будет включён.");
        }
    } else if (IsIII10()) {
        if (!PatchIII()) {
            Warn(L"DummyPlayerEngine: код GTA III не совпадает с версией 1.0.\n"
                 L"Возможно, exe изменён другим модом. Мод не будет включён.");
        }
    } else {
        Warn(L"DummyPlayerEngine: поддерживаются GTA San Andreas 1.0 US, GTA Vice City 1.0 и GTA III 1.0.\n"
             L"Мод не будет включён.");
    }
    return TRUE;
}
