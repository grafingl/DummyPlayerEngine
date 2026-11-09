// DummyPlayerEngine.asi
// GTA San Andreas 1.0 US: машина игрока звучит как машины NPC
// (без передач, ровный звук двигателя по скорости).
//
// Как работает: в начало CAEVehicleAudioEntity::ProcessPlayerVehicleEngine (0x4FBB10)
// ставится переход на CAEVehicleAudioEntity::ProcessDummyVehicleEngine (0x501480).
// У обеих функций одинаковая сигнатура (__thiscall, один параметр tVehicleParams&),
// поэтому прямой jmp безопасен: стек и регистр this остаются как есть.
// Адреса взяты из gta-reversed.

#include <windows.h>
#include <stdint.h>

static const uintptr_t ADDR_PLAYER_ENGINE = 0x4FBB10; // ProcessPlayerVehicleEngine
static const uintptr_t ADDR_DUMMY_ENGINE  = 0x501480; // ProcessDummyVehicleEngine

// Проверка версии игры (тот же способ, что и в plugin-sdk)
static bool IsGameV10US() {
    const void* p = reinterpret_cast<const void*>(0x82457C);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    return *reinterpret_cast<const uint32_t*>(p) == 0x94BF;
}

static void WriteJump(uintptr_t from, uintptr_t to) {
    DWORD oldProtect;
    VirtualProtect(reinterpret_cast<void*>(from), 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    *reinterpret_cast<uint8_t*>(from) = 0xE9; // jmp rel32
    *reinterpret_cast<int32_t*>(from + 1) = static_cast<int32_t>(to - (from + 5));
    VirtualProtect(reinterpret_cast<void*>(from), 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(from), 5);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        if (!IsGameV10US()) {
            MessageBoxW(nullptr,
                L"DummyPlayerEngine: нужна GTA San Andreas версии 1.0 US.\n"
                L"Мод не был включён.",
                L"DummyPlayerEngine", MB_ICONWARNING);
            return TRUE;
        }
        WriteJump(ADDR_PLAYER_ENGINE, ADDR_DUMMY_ENGINE);
    }
    return TRUE;
}
