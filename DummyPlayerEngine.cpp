// DummyPlayerEngine.asi
// Машина игрока звучит как машины NPC: без отдельных сэмплов разгона/сброса газа
// и без "игроковской" логики двигателя.
//
// Поддерживаемые игры (один и тот же .asi):
//   - GTA San Andreas 1.0 US
//   - GTA Vice City 1.0
//   - GTA III 1.0
//
// Настройки читаются из DummyPlayerEngine.ini рядом с .asi (секция [DummyPlayerEngine]):
//   Cars=1    машины (в SA также монстр-траки)
//   Bikes=1   мотоциклы (SA, VC)
//   Quads=1   квадроциклы (только SA)
// 1 = звук как у NPC, 0 = обычный звук игрока. Без файла всё включено.
//   Volume=100  громкость NPC-звука двигателя игрока в процентах (только GTA III и VC).
//               100 = как у NPC. Итог ограничен максимумом игры (127).
//   WorldVolume=100  громкость остальных звуков мира, пока игрок в транспорте, в процентах
//               (только GTA III и VC). 100 = без изменений, 0 = тишина.
//
// San Andreas:
//   Если включено всё, в начало CAEVehicleAudioEntity::ProcessPlayerVehicleEngine
//   (0x4FBB10) ставится переход на CAEVehicleAudioEntity::ProcessDummyVehicleEngine
//   (0x501480). Сигнатуры одинаковые (__thiscall, один параметр).
//   Если включено не всё, вызовы ProcessPlayerVehicleEngine из
//   CAEVehicleAudioEntity::ProcessVehicle (0x501E10..0x502280) перенаправляются на
//   обёртку, которая по типу транспорта (tVehicleParams::BaseVehicleType) вызывает
//   одну из двух функций. Адреса из gta-reversed.
//
// Vice City:
//   В cAudioManager::ProcessVehicleEngine есть ветка
//       if (playerVeh == veh && veh->m_modelIndex != MI_CADDY) {
//           ProcessPlayersVehicleEngine(params, veh); return;
//       }
//   Если включено всё, условный переход "это гольф-кар?" (je, 0x5F218A) заменяется
//   на безусловный, и машина игрока всегда идёт по ветке NPC. Если включено не всё,
//   проверка гольф-кара (0x5F2181..0x5F218B) заменяется переходом на заглушку,
//   которая дополнительно смотрит тип транспорта (cVehicleParams::m_VehicleType).
//   Состояние FPU-стека в обеих ветках одинаковое (проверено по дизассемблеру
//   gta-vc.exe 1.0). Перед патчем мод сверяет байты вокруг этого места.
//
// GTA III:
//   В cAudioManager::ProcessVehicleEngine есть ветка
//       if (FindPlayerVehicle() == veh) { ProcessPlayersVehicleEngine(params, automobile); return; }
//   Условный переход "не машина игрока" (jne, 0x56A6BF) заменяется на безусловный.
//   Мотоциклов в GTA III нет, поэтому учитывается только Cars.
//   Перед патчем мод сверяет байты вокруг этого места.
//
// Громкость (GTA III и VC):
//   В ветке NPC громкость сэмпла двигателя (emittingVol, 80..120 из 127) лежит
//   в локальной переменной и перед вызовом cAudioManager::ComputeVolume читается
//   инструкцией "mov al, [esp+N]; mov ecx, ebx". Эти 6 байт заменяются вызовом
//   заглушки, которая для машины игрока умножает emittingVol на Volume/100
//   (не больше 127), а затем делает то же, что оригинал. Дальше игра использует
//   эту же переменную и для m_nEmittingVolume сэмпла, поэтому громче становится
//   именно звук. Машины NPC не меняются.

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
    volatile uint8_t* p = (volatile uint8_t*)addr; // volatile: без memcpy/memset из CRT
    for (size_t i = 0; i < size; ++i) p[i] = bytes[i];
    VirtualProtect((void*)addr, size, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, size);
}

static void WriteJmp(uintptr_t from, uintptr_t to) {
    const int32_t rel = (int32_t)(to - (from + 5));
    const uint8_t jmp[5] = {
        0xE9,
        (uint8_t)(rel), (uint8_t)(rel >> 8), (uint8_t)(rel >> 16), (uint8_t)(rel >> 24),
    };
    WriteBytes(from, jmp, sizeof(jmp));
}

// ---------- Настройки ----------

static bool g_cars  = true;
static bool g_bikes = true;
static bool g_quads = true;

extern "C" {
    uint32_t dpe_volume __asm__("dpe_volume") = 100; // проценты
}

static uint32_t g_worldVolume = 100; // проценты, 0..100

// ---------- Приглушение мира (GTA III и VC) ----------
//
// Почти все звуки мира ставятся в очередь через cAudioManager::AddSampleToRequestedQueue,
// которая берёт уже заполненный m_sQueueSample. В начало этой функции ставится переход
// на вставку, которая, пока игрок в транспорте, уменьшает громкость сэмпла
// (m_nEmittingVolume и m_nVolume) на WorldVolume процентов. Не трогаются:
//   - 2D-звуки (меню, интерфейс);
//   - звуки ближе OWN_RADIUS метров к машине игрока: её двигатель, гудок, шины,
//     двери, удары по ней. У всех звуков машины позиция сэмпла = позиция машины.
// Поля одинаковые в III и VC (проверено по дизассемблеру exe 1.0):
//   this+0x21  m_sQueueSample.m_bIs2D
//   this+0x2C  m_sQueueSample.m_nVolume
//   this+0x40  m_sQueueSample.m_nEmittingVolume
//   this+0x50  m_sQueueSample.m_vecPos
//   veh+0x34   позиция машины (CPlaceable::m_matrix.pos)

static const float OWN_RADIUS = 5.0f;
static uintptr_t g_findPlayerVehicle = 0;

extern "C" __attribute__((fastcall)) void DuckWorld(uint8_t* am) __asm__("dpe_duck_world");
extern "C" __attribute__((fastcall)) void DuckWorld(uint8_t* am) {
    uint8_t* veh = ((uint8_t*(__cdecl*)())g_findPlayerVehicle)();
    if (!veh || am[0x21]) {
        return;
    }
    const float* s = (const float*)(am + 0x50);
    const float* v = (const float*)(veh + 0x34);
    const float dx = s[0] - v[0], dy = s[1] - v[1], dz = s[2] - v[2];
    if (dx * dx + dy * dy + dz * dz < OWN_RADIUS * OWN_RADIUS) {
        return;
    }
    am[0x40] = (uint8_t)(am[0x40] * g_worldVolume / 100);
    am[0x2C] = (uint8_t)(am[0x2C] * g_worldVolume / 100);
}

// Начало AddSampleToRequestedQueue переносится во вставку, после неё возврат назад.
extern "C" {
    uintptr_t dpe_iii_queue_back __asm__("dpe_iii_queue_back") = 0x57B077;
    uintptr_t dpe_vc_queue_back  __asm__("dpe_vc_queue_back")  = 0x5F7568;
}
extern "C" void IIIQueueStub() __asm__("dpe_iii_queue_stub");
extern "C" void VcQueueStub()  __asm__("dpe_vc_queue_stub");
__asm__(
    ".text\n"
    ".globl dpe_iii_queue_stub\n"
    "dpe_iii_queue_stub:\n"
    "    pushal\n"
    "    call   dpe_duck_world\n"        // ecx = this
    "    popal\n"
    "    pushl  %ebx\n"                  // оригинал 0x57B070:
    "    pushl  %ebp\n"
    "    movl   %ecx, %ebp\n"
    "    subl   $8, %esp\n"
    "    jmp    *dpe_iii_queue_back\n"
    ".globl dpe_vc_queue_stub\n"
    "dpe_vc_queue_stub:\n"
    "    pushal\n"
    "    call   dpe_duck_world\n"
    "    popal\n"
    "    pushl  %ebx\n"                  // оригинал 0x5F7560:
    "    pushl  %esi\n"
    "    movl   %ecx, %ebx\n"
    "    pushl  %ebp\n"
    "    subl   $0x18, %esp\n"
    "    jmp    *dpe_vc_queue_back\n"
);

static bool HookQueue(uintptr_t at, const uint8_t* orig, size_t size, void* stub) {
    if (!BytesEqual(at, orig, size)) {
        return false;
    }
    static const uint8_t nops[3] = { 0x90, 0x90, 0x90 };
    WriteJmp(at, (uintptr_t)stub);
    WriteBytes(at + 5, nops, size - 5);
    return true;
}

static const uint8_t III_QUEUE_ORIGINAL[7] = { 0x53, 0x55, 0x89, 0xCD, 0x83, 0xEC, 0x08 };
static const uint8_t VC_QUEUE_ORIGINAL[8]  = { 0x53, 0x56, 0x89, 0xCB, 0x55, 0x83, 0xEC, 0x18 };

static void LoadConfig(HINSTANCE hinst) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(hinst, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return;
    }
    // ...\DummyPlayerEngine.asi -> ...\DummyPlayerEngine.ini
    DWORD dot = len;
    while (dot > 0 && path[dot - 1] != L'.' && path[dot - 1] != L'\\' && path[dot - 1] != L'/') --dot;
    if (dot == 0 || path[dot - 1] != L'.' || dot + 3 >= MAX_PATH) {
        return;
    }
    path[dot] = L'i'; path[dot + 1] = L'n'; path[dot + 2] = L'i'; path[dot + 3] = 0;

    const wchar_t* sec = L"DummyPlayerEngine";
    g_cars  = GetPrivateProfileIntW(sec, L"Cars",  1, path) != 0;
    g_bikes = GetPrivateProfileIntW(sec, L"Bikes", 1, path) != 0;
    g_quads = GetPrivateProfileIntW(sec, L"Quads", 1, path) != 0;

    int vol = (int)GetPrivateProfileIntW(sec, L"Volume", 100, path);
    if (vol < 0)    vol = 0;
    if (vol > 1000) vol = 1000;
    dpe_volume = (uint32_t)vol;

    int world = (int)GetPrivateProfileIntW(sec, L"WorldVolume", 100, path);
    if (world < 0)   world = 0;
    if (world > 100) world = 100;
    g_worldVolume = (uint32_t)world;
}

// Заменяет 6 байт "mov al, [esp+N]; mov ecx, ebx" на "call stub; nop".
static void WriteVolumeCall(uintptr_t at, void* stub) {
    const int32_t rel = (int32_t)((uintptr_t)stub - (at + 5));
    const uint8_t code[6] = {
        0xE8,
        (uint8_t)(rel), (uint8_t)(rel >> 8), (uint8_t)(rel >> 16), (uint8_t)(rel >> 24),
        0x90,
    };
    WriteBytes(at, code, sizeof(code));
}

// Общая часть заглушек громкости: emittingVol = min(127, emittingVol * Volume / 100).
// Адрес байта громкости в ecx. Портит eax, edx.
__asm__(
    ".text\n"
    "dpe_scale_vol:\n"
    "    movzbl (%ecx), %eax\n"
    "    imull  dpe_volume, %eax\n"
    "    pushl  %ecx\n"
    "    xorl   %edx, %edx\n"
    "    movl   $100, %ecx\n"
    "    divl   %ecx\n"
    "    popl   %ecx\n"
    "    cmpl   $127, %eax\n"
    "    jbe    1f\n"
    "    movl   $127, %eax\n"
    "1:\n"
    "    movb   %al, (%ecx)\n"
    "    ret\n"
);

static void Warn(const wchar_t* text) {
    MessageBoxW(nullptr, text, L"DummyPlayerEngine", MB_OK | MB_ICONWARNING);
}

// ---------- GTA San Andreas 1.0 US ----------

static const uintptr_t SA_PLAYER_ENGINE     = 0x4FBB10; // CAEVehicleAudioEntity::ProcessPlayerVehicleEngine
static const uintptr_t SA_DUMMY_ENGINE      = 0x501480; // CAEVehicleAudioEntity::ProcessDummyVehicleEngine
static const uintptr_t SA_PROCESS_VEHICLE   = 0x501E10; // CAEVehicleAudioEntity::ProcessVehicle
static const uintptr_t SA_PROCESS_VEHICLE_E = 0x502280; // CAEVehicleAudioEntity::Service (следующая функция)

enum : int32_t { SA_VEHICLE_TYPE_QUAD = 2, SA_VEHICLE_TYPE_BIKE = 9 };

struct SaVehicleParams {
    int32_t SpecificVehicleType;
    int32_t BaseVehicleType; // eVehicleType
};

typedef void(__thiscall* SaEngineFn)(void* self, SaVehicleParams* vp);

static void __fastcall SaEngineHook(void* self, void* /*edx*/, SaVehicleParams* vp) {
    bool npc;
    switch (vp->BaseVehicleType) {
    case SA_VEHICLE_TYPE_BIKE: npc = g_bikes; break;
    case SA_VEHICLE_TYPE_QUAD: npc = g_quads; break;
    default:                   npc = g_cars;  break;
    }
    ((SaEngineFn)(npc ? SA_DUMMY_ENGINE : SA_PLAYER_ENGINE))(self, vp);
}

static bool IsSA10US() {
    return IsReadable(0x82457C, 4) && *(uint32_t*)0x82457C == 0x94BF;
}

static bool PatchSA() {
    if (g_cars && g_bikes && g_quads) {
        WriteJmp(SA_PLAYER_ENGINE, SA_DUMMY_ENGINE);
        return true;
    }
    if (!g_cars && !g_bikes && !g_quads) {
        return true; // всё обычное, ничего не трогаем
    }

    // Перенаправляем вызовы ProcessPlayerVehicleEngine внутри ProcessVehicle на обёртку.
    int patched = 0;
    for (uintptr_t a = SA_PROCESS_VEHICLE; a + 5 <= SA_PROCESS_VEHICLE_E; ++a) {
        const uint8_t* p = (const uint8_t*)a;
        if (p[0] != 0xE8) continue;
        const int32_t rel = *(const int32_t*)(p + 1);
        if (a + 5 + rel != SA_PLAYER_ENGINE) continue;

        const int32_t newRel = (int32_t)((uintptr_t)&SaEngineHook - (a + 5));
        const uint8_t call[5] = {
            0xE8,
            (uint8_t)(newRel), (uint8_t)(newRel >> 8), (uint8_t)(newRel >> 16), (uint8_t)(newRel >> 24),
        };
        WriteBytes(a, call, sizeof(call));
        ++patched;
        a += 4;
    }
    return patched > 0;
}

// ---------- GTA Vice City 1.0 ----------

// Код 0x5F217F..0x5F2199 в gta-vc.exe 1.0 (esi = машина, edi = cVehicleParams*):
//   jne  0x5F21A0                 ; машина не игрока -> ветка NPC
//   movsx eax, word [esi+0x5C]    ; m_modelIndex
//   cmp  eax, 0xBB                ; MI_CADDY
//   je   0x5F21A0                 ; гольф-кар -> ветка NPC
//   fcompp                        ; <- 0x5F218C, ветка игрока
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
static const uintptr_t VC_CADDY_CHECK = 0x5F2181; // movsx..je, 11 байт
static const uintptr_t VC_JE          = 0x5F218A;

// Данные для заглушки (имена без декорирования, чтобы сослаться из asm)
extern "C" {
    uint8_t   dpe_vc_cars    __asm__("dpe_vc_cars")    = 1;
    uint8_t   dpe_vc_bikes   __asm__("dpe_vc_bikes")   = 1;
    uintptr_t dpe_vc_npc     __asm__("dpe_vc_npc")     = 0x5F21A0; // ветка NPC
    uintptr_t dpe_vc_player  __asm__("dpe_vc_player")  = 0x5F218C; // ветка игрока
}

// Заменяет "movsx eax,[esi+5C]; cmp eax,0xBB; je npc". Портит только eax и флаги,
// как и оригинал. FPU не трогает.
extern "C" void VcTypeStub() __asm__("dpe_vc_stub");
__asm__(
    ".text\n"
    ".globl dpe_vc_stub\n"
    "dpe_vc_stub:\n"
    "    movswl 0x5c(%esi), %eax\n"
    "    cmpl   $0xbb, %eax\n"
    "    je     1f\n"                    // гольф-кар -> NPC, как в оригинале
    "    cmpl   $5, (%edi)\n"            // params.m_VehicleType == VEHICLE_TYPE_BIKE
    "    je     2f\n"
    "    cmpb   $0, dpe_vc_cars\n"
    "    jne    1f\n"
    "    jmp    *dpe_vc_player\n"
    "2:\n"
    "    cmpb   $0, dpe_vc_bikes\n"
    "    jne    1f\n"
    "    jmp    *dpe_vc_player\n"
    "1:\n"
    "    jmp    *dpe_vc_npc\n"
);

// Громкость. Код 0x5F273F..0x5F2752 (ветка NPC, esp+0xC = emittingVol, edi = cVehicleParams*):
//   fcompp; fcompp
//   mov  al, [esp+0xC]            ; <- 0x5F2743, заменяется на call dpe_vc_vol_stub; nop
//   mov  ecx, ebx
//   push dword [ebx+0x30]         ; дистанция
//   push dword [0x6B2668]         ; 50.0
//   push eax
//   call 0x5F7A20                 ; cAudioManager::ComputeVolume
static const uintptr_t VC_VOL_SITE = 0x5F273F;
static const uint8_t VC_VOL_ORIGINAL[] = {
    0xDE, 0xD9, 0xDE, 0xD9,             // fcompp; fcompp
    0x8A, 0x44, 0x24, 0x0C,             // mov al, [esp+0xC]
    0x89, 0xD9,                         // mov ecx, ebx
    0xFF, 0x73, 0x30,                   // push dword ptr [ebx+0x30]
};
static const uintptr_t VC_VOL_CALL = 0x5F2743;

// FindPlayerVehicle (0x4BC1E0) портит только eax и ecx, FPU-стек здесь пуст.
extern "C" void VcVolStub() __asm__("dpe_vc_vol_stub");
__asm__(
    ".text\n"
    ".globl dpe_vc_vol_stub\n"
    "dpe_vc_vol_stub:\n"
    "    pushl  %edx\n"
    "    movl   $0x4bc1e0, %eax\n"
    "    call   *%eax\n"                 // FindPlayerVehicle()
    "    testl  %eax, %eax\n"
    "    je     1f\n"
    "    cmpl   0xc(%edi), %eax\n"       // params.m_pVehicle == машина игрока?
    "    jne    1f\n"
    "    cmpw   $0xbb, 0x5c(%eax)\n"     // гольф-кар звучит как NPC и без мода, не трогаем
    "    je     1f\n"
    "    leal   0x14(%esp), %ecx\n"      // emittingVol (esp+0xC у вызывающего)
    "    call   dpe_scale_vol\n"
    "1:\n"
    "    popl   %edx\n"
    "    movzbl 0x10(%esp), %eax\n"      // mov al, [esp+0xC]
    "    movl   %ebx, %ecx\n"            // mov ecx, ebx
    "    ret\n"
);

static bool IsVC10() {
    static const uint8_t entry[4] = { 0x55, 0x89, 0xE5, 0x53 };
    return BytesEqual(0x667BF0, entry, sizeof(entry));
}

static bool PatchVC() {
    if (!BytesEqual(VC_SITE, VC_ORIGINAL, sizeof(VC_ORIGINAL))) {
        return false;
    }
    if (g_cars && g_bikes) {
        static const uint8_t jmpShort[1] = { 0xEB }; // je -> jmp
        WriteBytes(VC_JE, jmpShort, sizeof(jmpShort));
    } else if (g_cars || g_bikes) {
        dpe_vc_cars  = g_cars  ? 1 : 0;
        dpe_vc_bikes = g_bikes ? 1 : 0;
        uint8_t code[11];
        const int32_t rel = (int32_t)((uintptr_t)&VcTypeStub - (VC_CADDY_CHECK + 5));
        code[0] = 0xE9;
        code[1] = (uint8_t)rel; code[2] = (uint8_t)(rel >> 8);
        code[3] = (uint8_t)(rel >> 16); code[4] = (uint8_t)(rel >> 24);
        for (int i = 5; i < 11; ++i) code[i] = 0x90; // nop
        WriteBytes(VC_CADDY_CHECK, code, sizeof(code));
    }
    if (dpe_volume != 100 && (g_cars || g_bikes)) {
        if (BytesEqual(VC_VOL_SITE, VC_VOL_ORIGINAL, sizeof(VC_VOL_ORIGINAL))) {
            WriteVolumeCall(VC_VOL_CALL, (void*)&VcVolStub);
        } else {
            Warn(L"DummyPlayerEngine: место для громкости в Vice City не совпадает с версией 1.0.\n"
                 L"Звук двигателя работает, но настройка Volume не применена.");
        }
    }
    if (g_worldVolume != 100) {
        g_findPlayerVehicle = 0x4BC1E0;
        if (!HookQueue(0x5F7560, VC_QUEUE_ORIGINAL, sizeof(VC_QUEUE_ORIGINAL), (void*)&VcQueueStub)) {
            Warn(L"DummyPlayerEngine: очередь звуков Vice City не совпадает с версией 1.0\n"
                 L"(возможно, её уже изменил другой мод). Настройка WorldVolume не применена.");
        }
    }
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

// Громкость. Код 0x56AA66..0x56AA79 (ветка NPC, esp+8 = emittingVol, ebp = машина):
//   fcompp; fcompp
//   mov  al, [esp+8]              ; <- 0x56AA6A, заменяется на call dpe_iii_vol_stub; nop
//   mov  ecx, ebx
//   push dword [ebx+0x30]         ; дистанция
//   push dword [0x606A34]         ; 50.0
//   push eax
//   call 0x57ABB0                 ; cAudioManager::ComputeVolume
static const uintptr_t III_VOL_SITE = 0x56AA66;
static const uint8_t III_VOL_ORIGINAL[] = {
    0xDE, 0xD9, 0xDE, 0xD9,             // fcompp; fcompp
    0x8A, 0x44, 0x24, 0x08,             // mov al, [esp+8]
    0x89, 0xD9,                         // mov ecx, ebx
    0xFF, 0x73, 0x30,                   // push dword ptr [ebx+0x30]
};
static const uintptr_t III_VOL_CALL = 0x56AA6A;

// FindPlayerVehicle (0x4A10C0) портит только eax и ecx, FPU-стек здесь пуст.
extern "C" void IIIVolStub() __asm__("dpe_iii_vol_stub");
__asm__(
    ".text\n"
    ".globl dpe_iii_vol_stub\n"
    "dpe_iii_vol_stub:\n"
    "    pushl  %edx\n"
    "    movl   $0x4a10c0, %eax\n"
    "    call   *%eax\n"                 // FindPlayerVehicle()
    "    testl  %eax, %eax\n"
    "    je     1f\n"
    "    cmpl   %ebp, %eax\n"            // машина игрока?
    "    jne    1f\n"
    "    leal   0x10(%esp), %ecx\n"      // emittingVol (esp+8 у вызывающего)
    "    call   dpe_scale_vol\n"
    "1:\n"
    "    popl   %edx\n"
    "    movzbl 0xc(%esp), %eax\n"       // mov al, [esp+8]
    "    movl   %ebx, %ecx\n"            // mov ecx, ebx
    "    ret\n"
);

static bool IsIII10() {
    static const uint8_t entry[8] = { 0x55, 0x89, 0xE5, 0x53, 0x83, 0xEC, 0x48, 0x55 };
    return BytesEqual(0x5C1E70, entry, sizeof(entry));
}

static bool PatchIII() {
    if (!BytesEqual(III_SITE, III_ORIGINAL, sizeof(III_ORIGINAL))) {
        return false;
    }
    if (g_cars) {
        static const uint8_t jmpShort[1] = { 0xEB }; // jne -> jmp
        WriteBytes(III_JNE, jmpShort, sizeof(jmpShort));

        if (dpe_volume != 100) {
            if (BytesEqual(III_VOL_SITE, III_VOL_ORIGINAL, sizeof(III_VOL_ORIGINAL))) {
                WriteVolumeCall(III_VOL_CALL, (void*)&IIIVolStub);
            } else {
                Warn(L"DummyPlayerEngine: место для громкости в GTA III не совпадает с версией 1.0.\n"
                     L"Звук двигателя работает, но настройка Volume не применена.");
            }
        }
    }
    if (g_worldVolume != 100) {
        g_findPlayerVehicle = 0x4A10C0;
        if (!HookQueue(0x57B070, III_QUEUE_ORIGINAL, sizeof(III_QUEUE_ORIGINAL), (void*)&IIIQueueStub)) {
            Warn(L"DummyPlayerEngine: очередь звуков GTA III не совпадает с версией 1.0\n"
                 L"(возможно, её уже изменил другой мод). Настройка WorldVolume не применена.");
        }
    }
    return true;
}

// ---------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    DisableThreadLibraryCalls(hinst);
    LoadConfig(hinst);

    if (IsSA10US()) {
        if (!PatchSA()) {
            Warn(L"DummyPlayerEngine: не найдены нужные места в коде San Andreas.\n"
                 L"Возможно, exe изменён другим модом. Мод не будет включён.");
        }
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
