// DummyPlayerEngine.asi
// Машина игрока звучит как машины NPC: без отдельных сэмплов разгона/сброса газа
// и без "игроковской" логики двигателя.
//
// Поддерживаемые игры (один и тот же .asi):
//   - GTA San Andreas 1.0 US
//   - GTA Vice City 1.0
//   - GTA III 1.0
//
// DummyPlayerEngine.ini рядом с .asi: [III], [VC] или [SA].
//   Cars=1    машины (в SA также монстр-траки)
//   Bikes=1   мотоциклы (SA, VC)
//   Quads=1   квадроциклы (только SA)
// 1 = звук как у NPC, 0 = обычный звук игрока. Без файла всё включено.
//   Volume=100  громкость NPC-звука двигателя игрока в процентах.
//               100 = как у NPC. В III и VC итог ограничен максимумом игры (127).
//   WorldVolume=100  громкость остальных звуков мира, пока игрок в транспорте, в процентах
//               100 = без изменений, 0 = тишина.
//   В San Andreas проценты переводятся в дБ.
//   Если строки нет, значение 100, то есть как в игре.
//   ReloadKey=F7  клавиша, по которой ini перечитывается прямо в игре (F1..F12, буква,
//               цифра или их код клавиши Windows, например 118 или 0x76 = F7).
//               0 = выключено. Клавиша читается из состояния клавиатуры игры, как
//               в CLEO-модах. Сама ReloadKey читается только при запуске.
//
// Перечитывание на лету:
//   San Andreas: вызовы ProcessPlayerVehicleEngine всегда идут через обёртку. При смене
//   режима на ходу она повторяет то, что игра делает при посадке и выходе
//   (JustGotInVehicleAsDriver / JustGotOutOfVehicleAsDriver), см. раздел SA ниже.
// GTA III и VC:
//   Если ReloadKey задана, мод ставит "гибкие" патчи, которые читают настройки
//   во время игры: проверка "машина игрока" всегда идёт через заглушку, а вставки
//   громкости и приглушения мира ставятся сразу (при 100 они ничего не меняют).
//   После CPad::UpdatePads мод проверяет нажатие по NewKeyState и OldKeyState
//   и перечитывает ini в игровом потоке.
//   Родной звук двигателя игрока играет на отдельном канале (номер в
//   cAudioManager+3) и хранит своё состояние в статических переменных. Игра
//   останавливает этот канал, когда игрок выходит из машины
//   (cAudioManager::ProcessSpecial), и сбрасывает состояние, когда садится
//   (флаг bPlayerJustEnteredCar). Если Cars или Bikes поменялись на ходу, мод
//   зовёт cSampleManager::StopChannel и ставит bPlayerJustEnteredCar
//   до того, как опубликует новый режим двигателя.
//   Без этого старый канал продолжает играть поверх нового звука.
//   Если ReloadKey=0, мод ставит минимум патчей, как раньше.
//
// San Andreas:
//   Проверяются оба вызова ProcessPlayerVehicleEngine из CAEVehicleAudioEntity::ProcessVehicle.
//   Они перенаправляются на ProcessDummyVehicleEngine (0x501480) или на обёртку,
//   которая выбирает функцию по типу и подтипу транспорта (tVehicleParams).
//   Адреса из gta-reversed.
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

static size_t ReadableRegionSize(uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_GUARD)) {
        return 0;
    }
    switch (mbi.Protect & 0xFF) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return mbi.RegionSize - (addr - (uintptr_t)mbi.BaseAddress);
    default:
        return 0;
    }
}

static bool IsReadable(uintptr_t addr, size_t size) {
    if (size && size - 1 > UINTPTR_MAX - addr) return false;
    while (size) {
        const size_t available = ReadableRegionSize(addr);
        if (available >= size) return true;
        if (!available) return false;
        addr += available;
        size -= available;
    }
    return true;
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

static void LogPatchError(const char* action, uintptr_t addr);

struct PatchWrite {
    uintptr_t addr;
    const uint8_t* bytes;
    size_t size;
    DWORD oldProt;
};

static bool WritePatches(PatchWrite* patches, size_t count) {
    size_t ready = 0;
    for (; ready < count; ++ready) {
        PatchWrite& patch = patches[ready];
        // Один регион гарантирует одинаковую исходную защиту для всего диапазона.
        if (ReadableRegionSize(patch.addr) < patch.size ||
            !VirtualProtect((void*)patch.addr, patch.size, PAGE_EXECUTE_READWRITE, &patch.oldProt)) {
            LogPatchError("cannot make patch writable", patch.addr);
            break;
        }
    }

    const bool written = ready == count;
    if (written) {
        for (size_t i = 0; i < count; ++i) {
            const PatchWrite& patch = patches[i];
            volatile uint8_t* dest = (volatile uint8_t*)patch.addr; // без memcpy из CRT
            for (size_t j = 0; j < patch.size; ++j) dest[j] = patch.bytes[j];
        }
    }

    // Два патча могут быть на одной странице. Восстанавливаем в обратном порядке.
    while (ready > 0) {
        const PatchWrite& patch = patches[--ready];
        DWORD ignored;
        if (!VirtualProtect((void*)patch.addr, patch.size, patch.oldProt, &ignored)) {
            LogPatchError("cannot restore page protection", patch.addr);
        }
        if (written && !FlushInstructionCache(GetCurrentProcess(), (void*)patch.addr, patch.size)) {
            LogPatchError("cannot flush instruction cache", patch.addr);
        }
    }
    return written;
}

static bool WriteBytes(uintptr_t addr, const uint8_t* bytes, size_t size) {
    PatchWrite patch{addr, bytes, size, 0};
    return WritePatches(&patch, 1);
}

static void EncodeBranch(uint8_t* code, uint8_t opcode, uintptr_t from, uintptr_t to) {
    const uintptr_t rel = to - (from + 5);
    code[0] = opcode;
    for (size_t i = 0; i < 4; ++i) code[i + 1] = (uint8_t)(rel >> (i * 8));
}

static bool WriteJmp(uintptr_t from, uintptr_t to, size_t size) {
    uint8_t code[12]; // максимальный диапазон: проверка машины игрока в GTA III
    if (size < 5 || size > sizeof(code)) return false;
    EncodeBranch(code, 0xE9, from, to);
    for (size_t i = 5; i < size; ++i) code[i] = 0x90;
    return WriteBytes(from, code, size);
}

// ---------- Настройки ----------

static bool g_cars  = true;
static bool g_bikes = true;
static bool g_quads = true;
static bool g_isSA  = false;

extern "C" {
    uint32_t dpe_volume __asm__("dpe_volume") = 100; // проценты
}

// San Andreas считает громкость в децибелах: те же проценты, переведённые в дБ.
static float g_saVolumeDb = 0.0f; // прибавка к NPC-звуку двигателя игрока
static float g_saWorldDb  = 0.0f; // приглушение мира (<= 0)

// 20 * log10(pct / 100); 0% -> -100 дБ (в SA это тишина)
static float PercentToDb(uint32_t pct) {
    if (pct == 0) {
        return -100.0f;
    }
    float r = (float)pct / 100.0f;
    __asm__("fld1\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(r)); // log2(r) без CRT
    return r * 6.0205999f;                               // 20 * log10(2)
}

static uint32_t g_worldVolume = 100; // проценты, 0..100

static int  g_reloadKey   = VK_F7; // 0 = перечитывание выключено
static bool g_flexible    = false; // стоят патчи, которые читают настройки на лету
static bool g_worldHooked = false; // стоит вставка приглушения звуков мира
static bool g_volHooked   = false; // стоит вставка Volume (для журнала)

static void CheckReloadKey();

// Сброс родного звука двигателя игрока после смены Cars/Bikes на ходу (GTA III и VC)
static const uint8_t* g_audioManager = nullptr;
static bool g_hasBikes = false;
static uintptr_t g_sampleManager   = 0; // cSampleManager SampleManager
static uintptr_t g_stopChannel     = 0; // cSampleManager::StopChannel(int32)
static uintptr_t g_justEnteredCar  = 0; // static bool bPlayerJustEnteredCar

typedef void(__thiscall* StopChannelFn)(void* self, int32_t channel);
typedef uint8_t*(__cdecl* FindPlayerVehicleFn)();

// Флаги для asm-заглушек (имена без декорирования)
extern "C" {
    uint8_t dpe_vc_cars  __asm__("dpe_vc_cars")  = 1;
    uint8_t dpe_vc_bikes __asm__("dpe_vc_bikes") = 1;
    uint8_t dpe_iii_cars __asm__("dpe_iii_cars") = 1;
}

// ---------- Приглушение мира: общие правила ----------
//
// Чей звук, решается по владельцу, а не по расстоянию:
//   - звуки машины игрока и самого игрока не трогаются;
//   - звуки чужих машин приглушаются всегда, даже если машина проезжает вплотную
//     (иначе на границе радиуса звук резко включался и выключался);
//   - остальное (столкновения, взрывы, пешеходы, пассажиры) приглушается плавно
//     по расстоянию до машины игрока: ближе DUCK_NEAR как в игре, дальше DUCK_FAR
//     полностью, между ними постепенно. Так удары по своей машине и голоса
//     пассажиров остаются слышны, а скачков громкости нет.

static const float DUCK_NEAR = 5.0f;
static const float DUCK_FAR  = 10.0f;

// 0 = не приглушать, 1 = приглушить полностью
static float DuckByDistance(const float* sound, const float* veh) {
    const float dx = sound[0] - veh[0], dy = sound[1] - veh[1], dz = sound[2] - veh[2];
    float d = dx * dx + dy * dy + dz * dz;
    if (d <= DUCK_NEAR * DUCK_NEAR) return 0.0f;
    if (d >= DUCK_FAR * DUCK_FAR)   return 1.0f;
    __asm__("fsqrt" : "+t"(d)); // без CRT
    return (d - DUCK_NEAR) / (DUCK_FAR - DUCK_NEAR);
}

// Плавное включение и выключение приглушения при посадке и выходе.
// g_duckLevel идёт от 0 (пешком) до 1 (в машине) за DUCK_FADE_MS. Пока приглушение
// затухает после выхода, правила считаются от последней машины игрока: её указатель
// только сравнивается, а позиция берётся из копии.
static const float DUCK_FADE_MS = 1000.0f;

static float          g_duckLevel   = 0.0f;
static DWORD          g_duckTick    = 0;
static const uint8_t* g_lastVeh     = nullptr;
static float          g_lastVehPos[3];

// veh = машина игрока сейчас или nullptr; pos = её позиция. Возвращает уровень 0..1.
static float UpdateDuck(const uint8_t* veh, const float* pos) {
    const DWORD now = GetTickCount();
    DWORD dt = now - g_duckTick;
    g_duckTick = now;
    if (dt > 100) dt = 100; // после паузы или загрузки не перескакивать
    const float step = (float)dt / DUCK_FADE_MS;
    if (veh) {
        g_lastVeh = veh;
        g_lastVehPos[0] = pos[0]; g_lastVehPos[1] = pos[1]; g_lastVehPos[2] = pos[2];
        g_duckLevel = g_duckLevel + step < 1.0f ? g_duckLevel + step : 1.0f;
    } else {
        g_duckLevel = g_duckLevel - step > 0.0f ? g_duckLevel - step : 0.0f;
    }
    return g_duckLevel;
}

// ---------- Приглушение мира (GTA III и VC) ----------
//
// Почти все звуки мира ставятся в очередь через cAudioManager::AddSampleToRequestedQueue,
// которая берёт уже заполненный m_sQueueSample. В начало этой функции ставится переход
// на вставку, которая, пока игрок в транспорте, уменьшает громкость сэмпла
// (m_nEmittingVolume и m_nVolume) по правилам выше. 2D-звуки (меню) не трогаются.
// Владелец звука: cAudioManager::ProcessEntity пишет номер звукового объекта в
// m_sQueueSample.m_nEntityIndex, объекты лежат в m_asAudioEntities (шаг 0x28):
//   +0 m_nType (0 = AUDIOTYPE_PHYSICAL, тогда +4 - CEntity*), +8 m_bIsUsed.
// Тип сущности: CEntity+0x50 & 7 (2 = машина).
// Поля одинаковые в III и VC (проверено по дизассемблеру exe 1.0):
//   this+0x14  m_sQueueSample.m_nEntityIndex
//   this+0x21  m_sQueueSample.m_bIs2D
//   this+0x2C  m_sQueueSample.m_nVolume
//   this+0x40  m_sQueueSample.m_nEmittingVolume
//   this+0x50  m_sQueueSample.m_vecPos
//   veh+0x34   позиция машины (CPlaceable::m_matrix.pos)
// Разное: m_asAudioEntities в this+0x1DC8 (III, 200 шт.) и this+0x1F10 (VC, 250 шт.).

static uintptr_t g_findPlayerVehicle = 0;
static uintptr_t g_findPlayerPed     = 0;
static uint32_t  g_audioEntities     = 0; // смещение m_asAudioEntities в cAudioManager
static int32_t   g_audioEntityCount  = 0;

extern "C" __attribute__((fastcall)) void DuckWorld(uint8_t* am) __asm__("dpe_duck_world");
extern "C" __attribute__((fastcall)) void DuckWorld(uint8_t* am) {
    const uint32_t world = g_worldVolume;
    if (world >= 100) {
        return;
    }
    uint8_t* cur = ((FindPlayerVehicleFn)g_findPlayerVehicle)();
    const float level = UpdateDuck(cur, cur ? (const float*)(cur + 0x34) : nullptr);
    if (level <= 0.0f || am[0x21]) {
        return;
    }
    const uint8_t* veh = g_lastVeh;      // только для сравнения
    const float* vehPos = g_lastVehPos;
    float factor = -1.0f; // ещё не решено
    const int32_t idx = *(const int32_t*)(am + 0x14);
    if (idx >= 0 && idx < g_audioEntityCount) {
        const uint8_t* ae = am + g_audioEntities + (uint32_t)idx * 0x28;
        // занят, активен (как проверяет ProcessEntity) и AUDIOTYPE_PHYSICAL: тогда +4 -
        // живая сущность, которую игра в этом же кадре сама читает в ProcessPhysical
        if (ae[8] && ae[9] && *(const int32_t*)ae == 0) {
            const uint8_t* ent = *(uint8_t* const*)(ae + 4);
            if (ent == veh || ent == ((uint8_t*(__cdecl*)())g_findPlayerPed)()) {
                return;                           // своя машина или сам игрок
            }
            if (ent && (ent[0x50] & 7) == 2) {
                factor = 1.0f;                    // чужая машина: всегда
            }
        }
    }
    if (factor < 0.0f) {
        factor = DuckByDistance((const float*)(am + 0x50), vehPos);
    }
    factor *= level;
    // world..100 процентов: 100 рядом, world далеко и у чужих машин
    const uint32_t pct = 100 - (uint32_t)((float)(100 - world) * factor + 0.5f);
    am[0x40] = (uint8_t)(am[0x40] * pct / 100);
    am[0x2C] = (uint8_t)(am[0x2C] * pct / 100);
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
    return WriteJmp(at, (uintptr_t)stub, size);
}

static const uint8_t III_QUEUE_ORIGINAL[7] = { 0x53, 0x55, 0x89, 0xCD, 0x83, 0xEC, 0x08 };
static const uint8_t VC_QUEUE_ORIGINAL[8]  = { 0x53, 0x56, 0x89, 0xCB, 0x55, 0x83, 0xEC, 0x18 };

static wchar_t g_iniPath[MAX_PATH];
static bool    g_haveIni = false;
static const wchar_t* g_gameIniSection = nullptr;

// ...\DummyPlayerEngine.asi -> ...\DummyPlayerEngine.ini
static void FindIni(HINSTANCE hinst) {
    DWORD len = GetModuleFileNameW(hinst, g_iniPath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        g_iniPath[0] = 0;
        return;
    }
    DWORD dot = len;
    while (dot > 0 && g_iniPath[dot - 1] != L'.' && g_iniPath[dot - 1] != L'\\' && g_iniPath[dot - 1] != L'/') --dot;
    if (dot == 0 || g_iniPath[dot - 1] != L'.' || dot + 3 >= MAX_PATH) {
        g_iniPath[0] = 0;
        return;
    }
    g_iniPath[dot] = L'i'; g_iniPath[dot + 1] = L'n'; g_iniPath[dot + 2] = L'i'; g_iniPath[dot + 3] = 0;
    const DWORD attr = GetFileAttributesW(g_iniPath);
    g_haveIni = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// ---------- Журнал DummyPlayerEngine.log (рядом с .asi) ----------
//
// Какой ini найден, какие значения прочитаны, какие вставки стоят, нажатия ReloadKey.
// Перезаписывается при каждом запуске игры. Пишется в UTF-8.

static wchar_t g_logPath[MAX_PATH];
static bool    g_haveLog = false;

struct LogLine {
    char s[700];
    int  n;
};

static void LStr(LogLine& l, const char* t) {
    while (*t && l.n < (int)sizeof(l.s) - 3) l.s[l.n++] = *t++;
}

static void LInt(LogLine& l, int v) {
    char tmp[12];
    int k = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do { tmp[k++] = (char)('0' + u % 10); u /= 10; } while (u && k < 11);
    if (v < 0 && l.n < (int)sizeof(l.s) - 3) l.s[l.n++] = '-';
    while (k > 0 && l.n < (int)sizeof(l.s) - 3) l.s[l.n++] = tmp[--k];
}

static void LHex(LogLine& l, unsigned v) {
    LStr(l, "0x");
    char tmp[8];
    int k = 0;
    do { tmp[k++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v && k < 8);
    while (k > 0 && l.n < (int)sizeof(l.s) - 3) l.s[l.n++] = tmp[--k];
}

static void LPath(LogLine& l, const wchar_t* p) {
    const int room = (int)sizeof(l.s) - 3 - l.n;
    if (room <= 0) return;
    const int w = WideCharToMultiByte(CP_UTF8, 0, p, -1, l.s + l.n, room, nullptr, nullptr);
    if (w > 0) l.n += w - 1; // без завершающего нуля
}

static void LogWrite(LogLine& l) {
    if (!g_haveLog) return;
    l.s[l.n++] = '\r';
    l.s[l.n++] = '\n';
    HANDLE f = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(f, l.s, (DWORD)l.n, &written, nullptr);
    CloseHandle(f);
}

static void LogPatchError(const char* action, uintptr_t addr) {
    LogLine l; l.n = 0;
    LStr(l, "patch: "); LStr(l, action);
    LStr(l, " at "); LHex(l, (unsigned)addr);
    LogWrite(l);
}

static void LogWarning(const char* message) {
    LogLine l; l.n = 0;
    LStr(l, "warning: "); LStr(l, message);
    LogWrite(l);
}

static void OpenLog() {
    int i = 0;
    while (i < MAX_PATH - 1 && g_iniPath[i]) { g_logPath[i] = g_iniPath[i]; ++i; }
    g_logPath[i] = 0;
    if (i < 4) return;
    g_logPath[i - 3] = L'l'; g_logPath[i - 2] = L'o'; g_logPath[i - 1] = L'g';
    HANDLE f = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    static const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
    DWORD written;
    WriteFile(f, bom, 3, &written, nullptr);
    CloseHandle(f);
    g_haveLog = true;
}

static void ResetPlayerEngine() {
    // Пока звуковая система не запущена или игрок пешком, канал останавливать не нужно.
    if (!g_audioManager || !g_audioManager[0] ||
        !((FindPlayerVehicleFn)g_findPlayerVehicle)()) {
        return;
    }
    ((StopChannelFn)g_stopChannel)((void*)g_sampleManager, g_audioManager[3]); // m_nActiveSamples
    *(uint8_t*)g_justEnteredCar = 1;
}

static int ReadSetting(const wchar_t* key, int defaultValue) {
    return (int)GetPrivateProfileIntW(g_gameIniSection, key, defaultValue, g_iniPath);
}

// При старте вызывается из DllMain, далее только из игрового потока.
static void LoadSettings(bool reloading = false) {
    if (!g_haveIni || !g_gameIniSection) {
        return;
    }
    const bool cars  = ReadSetting(L"Cars", 1) != 0;
    const bool bikes = ReadSetting(L"Bikes", 1) != 0;
    const bool quads = ReadSetting(L"Quads", 1) != 0;
    int vol = ReadSetting(L"Volume", 100);
    if (vol < 0)    vol = 0;
    if (vol > 1000) vol = 1000;
    int world = ReadSetting(L"WorldVolume", 100);
    if (world < 0)   world = 0;
    if (world > 100) world = 100;

    if (reloading && !g_isSA && (cars != g_cars || (g_hasBikes && bikes != g_bikes))) {
        ResetPlayerEngine();
    }
    g_cars = cars;
    g_bikes = bikes;
    g_quads = quads;
    dpe_volume = (uint32_t)vol;
    g_worldVolume = (uint32_t)world;
    dpe_vc_cars  = g_cars  ? 1 : 0;
    dpe_vc_bikes = g_bikes ? 1 : 0;
    dpe_iii_cars = g_cars  ? 1 : 0;
    g_saVolumeDb = PercentToDb(dpe_volume);
    g_saWorldDb  = PercentToDb(g_worldVolume);
}

static wchar_t Upper(wchar_t c) {
    return (c >= L'a' && c <= L'z') ? (wchar_t)(c - L'a' + L'A') : c;
}

// "F7", "f12", "R", "5", "0x76", "118", "0" -> код клавиши Windows (0 = выключено).
// Непонятное значение -> F7.
static int ParseKey(const wchar_t* s) {
    while (*s == L' ' || *s == L'\t') ++s;
    int n = 0;
    while (s[n] && s[n] != L' ' && s[n] != L'\t' && s[n] != L';') ++n;
    if (n == 0) {
        return VK_F7;
    }
    const wchar_t c0 = Upper(s[0]);
    if (c0 == L'F' && n >= 2 && n <= 3) {
        int num = 0;
        for (int i = 1; i < n; ++i) {
            if (s[i] < L'0' || s[i] > L'9') return VK_F7;
            num = num * 10 + (s[i] - L'0');
        }
        return (num >= 1 && num <= 12) ? VK_F1 + num - 1 : VK_F7;
    }
    if (n == 1 && ((c0 >= L'A' && c0 <= L'Z') || (c0 >= L'1' && c0 <= L'9'))) {
        return c0; // коды букв и цифр совпадают с ASCII
    }
    int value = 0;
    if (n > 2 && s[0] == L'0' && Upper(s[1]) == L'X') {
        for (int i = 2; i < n; ++i) {
            const wchar_t c = Upper(s[i]);
            int d;
            if (c >= L'0' && c <= L'9')      d = c - L'0';
            else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
            else return VK_F7;
            value = value * 16 + d;
            if (value > 0xFF) return VK_F7;
        }
        return value;
    }
    for (int i = 0; i < n; ++i) {
        if (s[i] < L'0' || s[i] > L'9') return VK_F7;
        value = value * 10 + (s[i] - L'0');
        if (value > 0xFF) return VK_F7;
    }
    return value;
}

static void LogSettings(const char* prefix) {
    LogLine l; l.n = 0;
    LStr(l, prefix);
    LStr(l, "Cars=");  LInt(l, g_cars);
    LStr(l, " Bikes="); LInt(l, g_bikes);
    LStr(l, " Quads="); LInt(l, g_quads);
    LStr(l, " Volume=");      LInt(l, (int)dpe_volume);
    LStr(l, " WorldVolume="); LInt(l, (int)g_worldVolume);
    LogWrite(l);
}

static void LoadConfig(HINSTANCE hinst) {
    FindIni(hinst);
    OpenLog();
    {
        LogLine l; l.n = 0;
        LStr(l, "ini: "); LPath(l, g_iniPath);
        LStr(l, g_haveIni ? "  (found)" : "  (NOT FOUND - using defaults: no changes to volume)");
        LogWrite(l);
    }
    LoadSettings();
    if (g_haveIni && g_gameIniSection) {
        wchar_t buf[32];
        GetPrivateProfileStringW(g_gameIniSection, L"ReloadKey", L"F7", buf, 32, g_iniPath);
        g_reloadKey = ParseKey(buf);
    }
    g_flexible = g_haveIni && g_gameIniSection && g_reloadKey != 0;
    LogSettings("start: ");
}

// Заменяет 6 байт "mov al, [esp+N]; mov ecx, ebx" на "call stub; nop".
static bool WriteVolumeCall(uintptr_t at, void* stub) {
    uint8_t code[6];
    EncodeBranch(code, 0xE8, at, (uintptr_t)stub);
    code[5] = 0x90;
    return WriteBytes(at, code, sizeof(code));
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

// ---------- GTA San Andreas 1.0 US ----------

static const uintptr_t SA_PLAYER_ENGINE     = 0x4FBB10; // CAEVehicleAudioEntity::ProcessPlayerVehicleEngine
static const uintptr_t SA_DUMMY_ENGINE      = 0x501480; // CAEVehicleAudioEntity::ProcessDummyVehicleEngine
static const uintptr_t SA_PROCESS_VEHICLE   = 0x501E10; // CAEVehicleAudioEntity::ProcessVehicle
static const uintptr_t SA_PROCESS_VEHICLE_E = 0x502280; // CAEVehicleAudioEntity::Service (следующая функция)

enum : int32_t { SA_VEHICLE_TYPE_QUAD = 2, SA_VEHICLE_TYPE_BIKE = 9 };

struct SaVehicleParams {
    int32_t SpecificVehicleType; // eVehicleType (подтип)
    int32_t BaseVehicleType; // eVehicleType
};

typedef void(__thiscall* SaEngineFn)(void* self, SaVehicleParams* vp);
typedef void(__thiscall* SaEntityFn)(void* self);

// Смена режима на ходу. CAEVehicleAudioEntity::Service (0x502280) при посадке игрока
// водителем зовёт JustGotInVehicleAsDriver, при выходе JustGotOutOfVehicleAsDriver.
// Они переводят состояние двигателя (this+0xA9) между "игроковскими" (3..6) и
// "NPC" (0..2, 10) значениями и останавливают лишние звуки двигателя (0x4F55C0).
// Мод повторяет то же самое:
//   NPC -> игрок: JustGotIn, как при обычной посадке;
//   игрок -> NPC: JustGotOut + JustGotIn, как при выходе и посадке, чтобы звуки
//                 двигателя игрока остановились, а остальное (водитель, радио)
//                 осталось как у водителя.
static const uintptr_t SA_JUST_GOT_IN  = 0x4F5700; // CAEVehicleAudioEntity::JustGotInVehicleAsDriver
static const uintptr_t SA_JUST_GOT_OUT = 0x4FCF40; // CAEVehicleAudioEntity::JustGotOutOfVehicleAsDriver

static void* g_saLastSelf = nullptr; // только для сравнения, не разыменовывается
static bool  g_saLastNpc  = false;
static void* g_saPlayerDummySelf = nullptr; // машина игрока, пока для неё работает NPC-двигатель

static void __fastcall SaEngineHook(void* self, void* /*edx*/, SaVehicleParams* vp) {
    // CQuadBike наследует базовый тип автомобиля и задаёт подтип QUAD.
    const bool isQuad = vp->SpecificVehicleType == SA_VEHICLE_TYPE_QUAD ||
                        vp->BaseVehicleType == SA_VEHICLE_TYPE_QUAD;
    const bool npc = isQuad ? g_quads : vp->BaseVehicleType == SA_VEHICLE_TYPE_BIKE ? g_bikes : g_cars;
    if (g_flexible) {
        if (self == g_saLastSelf && npc != g_saLastNpc) {
            if (npc) {
                ((SaEntityFn)SA_JUST_GOT_OUT)(self);
            }
            ((SaEntityFn)SA_JUST_GOT_IN)(self);
        }
        g_saLastSelf = self;
        g_saLastNpc  = npc;
    }
    if (npc) {
        g_saPlayerDummySelf = self; // для Volume: сейчас считается NPC-звук машины игрока
        ((SaEngineFn)SA_DUMMY_ENGINE)(self, vp);
        g_saPlayerDummySelf = nullptr;
    } else {
        ((SaEngineFn)SA_PLAYER_ENGINE)(self, vp);
    }
}

// ---------- Громкость в San Andreas ----------
//
// Volume: громкость NPC-звука двигателя считают CAEVehicleAudioEntity::GetVolumeForDummyIdle
// (0x4F51F0) и GetVolumeForDummyRev (0x4F53D0), обе в дБ. Их вызывают только из перехода
// состояний NPC-двигателя (0x4FCA10..0x4FCF3C, по 5 вызовов). Вызовы перенаправляются на
// обёртки, которые прибавляют g_saVolumeDb, если сейчас считается машина игрока.
//
// WorldVolume: итоговую громкость каждого звука считает CAESound::CalculateVolume (0x4EFA10),
// её вызывает только цикл CAESoundManager::Service (0x4F041A). После неё мод, пока игрок
// в транспорте, вычитает приглушение из m_fFinalVolume (+0x60) по общим правилам.
// Фронтенд-звуки (m_nEnvironmentFlags & 1, +0x56: меню, интерфейс) не трогаются.
// Владелец звука: CAEAudioEntity (+4), его CEntity (+4 у владельца); объект звука (+8).
// Указатели только сравниваются, чужая ли это машина - проверяется по пулу машин
// (SaIsVehicle). Позиция звука: +0x24.

static const uintptr_t SA_DUMMY_IDLE_VOL      = 0x4F51F0;
static const uintptr_t SA_DUMMY_REV_VOL       = 0x4F53D0;
static const uintptr_t SA_DUMMY_TRANSITION    = 0x4FCA10;
static const uintptr_t SA_DUMMY_TRANSITION_E  = 0x4FCF3C;
static const uintptr_t SA_CALC_VOLUME         = 0x4EFA10; // CAESound::CalculateVolume
static const uintptr_t SA_CALC_VOLUME_CALL    = 0x4F041A; // в CAESoundManager::Service
static const uintptr_t SA_FIND_PLAYER_VEHICLE = 0x4D9E10; // CAEAudioUtility::FindVehicleOfPlayer
static const uintptr_t SA_FIND_PLAYER_PED     = 0x56E210; // FindPlayerPed(int32 playerId = -1)
static const uintptr_t SA_FRAME_COUNTER       = 0xB7CB4C; // CTimer::m_FrameCounter
static const uint32_t* g_saReloadFrame = nullptr;

typedef float(__thiscall* SaDummyVolFn)(void* self, float a, float b);

static float __fastcall SaDummyIdleVolHook(void* self, void* /*edx*/, float a, float b) {
    float v = ((SaDummyVolFn)SA_DUMMY_IDLE_VOL)(self, a, b);
    if (self == g_saPlayerDummySelf) v += g_saVolumeDb;
    return v;
}

static float __fastcall SaDummyRevVolHook(void* self, void* /*edx*/, float a, float b) {
    float v = ((SaDummyVolFn)SA_DUMMY_REV_VOL)(self, a, b);
    if (self == g_saPlayerDummySelf) v += g_saVolumeDb;
    return v;
}

// Это машина? Проверка только по адресу: указатель лежит внутри пула машин
// (CPools::ms_pVehiclePool, 0xB74494; элемент 0xA18 байт, как в коде игры по 0x4320C2)
// и ячейка занята. По самому указателю ничего не читается: у некоторых звуковых
// объектов SA поле "сущность" - не указатель на сущность, и чтение по нему роняло игру.
struct SaPool {
    uint8_t* objects;
    int8_t*  flags;   // старший бит = ячейка свободна
    int32_t  size;
};
static const uintptr_t SA_VEHICLE_POOL      = 0xB74494;
static const uint32_t  SA_VEHICLE_POOL_ITEM = 0xA18;

static bool SaIsVehicle(const uint8_t* p) {
    const SaPool* pool = *(SaPool* const*)SA_VEHICLE_POOL;
    if (!p || !pool || !pool->objects || !pool->flags || pool->size <= 0) {
        return false;
    }
    const uintptr_t off = (uintptr_t)p - (uintptr_t)pool->objects;
    if (off >= (uintptr_t)pool->size * SA_VEHICLE_POOL_ITEM || off % SA_VEHICLE_POOL_ITEM) {
        return false;
    }
    return pool->flags[off / SA_VEHICLE_POOL_ITEM] >= 0;
}

static void __fastcall SaCalcVolumeHook(uint8_t* sound) {
    // Запасной путь, если вызов CPad::UpdatePads уже изменён.
    // За кадр может обрабатываться много звуков, клавишу проверяем один раз.
    if (g_saReloadFrame) {
        static uint32_t lastFrame = UINT32_MAX;
        const uint32_t frame = *g_saReloadFrame;
        if (frame != lastFrame) {
            lastFrame = frame;
            CheckReloadKey();
        }
    }
    ((SaEntityFn)SA_CALC_VOLUME)(sound);
    const float duck = g_saWorldDb;
    if (duck >= 0.0f || (*(const uint16_t*)(sound + 0x56) & 1)) {
        return;
    }
    uint8_t* cur = ((uint8_t*(__cdecl*)())SA_FIND_PLAYER_VEHICLE)();
    const float* curPos = nullptr;
    if (cur) {
        const float* m = *(const float* const*)(cur + 0x14);     // CMatrix*
        curPos = m ? m + 12 : (const float*)(cur + 4);           // позиция машины
    }
    const float level = UpdateDuck(cur, curPos);
    if (level <= 0.0f) {
        return;
    }
    const uint8_t* veh = g_lastVeh;                              // только для сравнения
    uint8_t* ped = ((uint8_t*(__cdecl*)(int32_t))SA_FIND_PLAYER_PED)(-1);
    const uint8_t* owner    = *(uint8_t* const*)(sound + 4);
    const uint8_t* ownerEnt = owner ? *(uint8_t* const*)(owner + 4) : nullptr;
    const uint8_t* phys     = *(uint8_t* const*)(sound + 8);
    if (ownerEnt == veh || phys == veh || (ped && (ownerEnt == ped || phys == ped))) {
        return;                                                  // своя машина или сам игрок
    }
    float factor;
    if (SaIsVehicle(ownerEnt) || SaIsVehicle(phys)) {
        factor = 1.0f;                                           // чужая машина: всегда
    } else {
        factor = DuckByDistance((const float*)(sound + 0x24), g_lastVehPos);
    }
    *(float*)(sound + 0x60) += duck * factor * level;
}

static bool FindCallPatches(uintptr_t lo, uintptr_t hi, uintptr_t target, uintptr_t hook,
                            PatchWrite* patches, uint8_t (*code)[5], size_t expected) {
    if (!IsReadable(lo, hi - lo)) {
        LogPatchError("cannot read call sites", lo);
        return false;
    }
    size_t count = 0;
    for (uintptr_t a = lo; a + 5 <= hi; ++a) {
        const uint8_t* p = (const uint8_t*)a;
        if (p[0] != 0xE8) continue;
        const uint32_t rel = p[1] | ((uint32_t)p[2] << 8) |
                             ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
        if (rel != (uint32_t)(target - (a + 5))) continue;
        if (count == expected) {
            LogPatchError("too many matching calls", a);
            return false;
        }
        EncodeBranch(code[count], 0xE8, a, hook);
        patches[count] = {a, code[count], sizeof(code[count]), 0};
        ++count;
        a += 4;
    }
    if (count != expected) {
        LogPatchError("missing expected calls in range starting", lo);
        return false;
    }
    return true;
}

static bool IsSA10US() {
    return IsReadable(0x82457C, 4) && *(uint32_t*)0x82457C == 0x94BF;
}

static bool PatchSA() {
    const bool needWrapper = g_flexible || dpe_volume != 100; // Volume работает через обёртку
    if (needWrapper || g_cars || g_bikes || g_quads) {
        // ProcessVehicle вызывает двигатель игрока из веток AE_CAR и AE_BIKE.
        const uintptr_t hook = !needWrapper && g_cars && g_bikes && g_quads
            ? SA_DUMMY_ENGINE : (uintptr_t)&SaEngineHook;
        PatchWrite calls[2];
        uint8_t code[2][5];
        if (!FindCallPatches(SA_PROCESS_VEHICLE, SA_PROCESS_VEHICLE_E, SA_PLAYER_ENGINE,
                             hook, calls, code, 2) || !WritePatches(calls, 2)) {
            return false;
        }
    }

    if (needWrapper) {
        constexpr size_t callsPerFunction = 5;
        PatchWrite calls[2 * callsPerFunction];
        uint8_t code[2 * callsPerFunction][5];
        g_volHooked =
            FindCallPatches(SA_DUMMY_TRANSITION, SA_DUMMY_TRANSITION_E, SA_DUMMY_IDLE_VOL,
                            (uintptr_t)&SaDummyIdleVolHook, calls, code, callsPerFunction) &&
            FindCallPatches(SA_DUMMY_TRANSITION, SA_DUMMY_TRANSITION_E, SA_DUMMY_REV_VOL,
                            (uintptr_t)&SaDummyRevVolHook, calls + callsPerFunction,
                            code + callsPerFunction, callsPerFunction) &&
            WritePatches(calls, 2 * callsPerFunction);
        if (!g_volHooked) {
            LogWarning("Volume unavailable: could not install the engine volume hook");
        }
    }

    if (g_flexible || g_worldVolume != 100) {
        PatchWrite call;
        uint8_t code[1][5];
        g_worldHooked =
            FindCallPatches(SA_CALC_VOLUME_CALL, SA_CALC_VOLUME_CALL + 5, SA_CALC_VOLUME,
                            (uintptr_t)&SaCalcVolumeHook, &call, code, 1) &&
            WritePatches(&call, 1);
        if (!g_worldHooked) {
            LogWarning("WorldVolume unavailable: could not install the world volume hook");
        }
    }
    return true;
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
    g_findPlayerVehicle = 0x4BC1E0;
    g_audioManager     = (const uint8_t*)0x78D718;
    g_hasBikes         = true;
    g_sampleManager    = 0x983B90;
    g_stopChannel      = 0x5D6530;
    g_justEnteredCar   = 0x7838CD;

    if (!g_flexible && g_cars && g_bikes) {
        static const uint8_t jmpShort[1] = { 0xEB }; // je -> jmp
        if (!WriteBytes(VC_JE, jmpShort, sizeof(jmpShort))) return false;
    } else if (g_flexible || g_cars || g_bikes) {
        if (!WriteJmp(VC_CADDY_CHECK, (uintptr_t)&VcTypeStub, 11)) return false;
    }
    if (g_flexible || (dpe_volume != 100 && (g_cars || g_bikes))) {
        g_volHooked = BytesEqual(VC_VOL_SITE, VC_VOL_ORIGINAL, sizeof(VC_VOL_ORIGINAL)) &&
                      WriteVolumeCall(VC_VOL_CALL, (void*)&VcVolStub);
        if (!g_volHooked) {
            LogWarning("Volume unavailable: could not install the engine volume hook");
        }
    }
    if (g_flexible || g_worldVolume != 100) {
        g_findPlayerPed     = 0x4BC120;
        g_audioEntities     = 0x1F10;
        g_audioEntityCount  = 250;
        g_worldHooked = HookQueue(0x5F7560, VC_QUEUE_ORIGINAL, sizeof(VC_QUEUE_ORIGINAL), (void*)&VcQueueStub);
        if (!g_worldHooked) {
            LogWarning("WorldVolume unavailable: could not install the world volume hook");
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

// Гибкий вариант: "cmp eax, ebp; fldz x4; jne npc" (0x56A6B5..0x56A6C0, 12 байт)
// заменяется на "jmp dpe_iii_stub" + 7 nop. Заглушка делает то же самое, но машину
// игрока отправляет в ветку NPC, только если включено Cars. fldz флаги не меняет,
// ветка NPC (0x56A6D3) флаги не читает, внутрь заменённых байт никто не прыгает
// (проверено по дизассемблеру gta3.exe 1.0).
extern "C" {
    uintptr_t dpe_iii_npc    __asm__("dpe_iii_npc")    = 0x56A6D3; // ветка NPC
    uintptr_t dpe_iii_player __asm__("dpe_iii_player") = 0x56A6C1; // ветка игрока
}
extern "C" void IIIBranchStub() __asm__("dpe_iii_stub");
__asm__(
    ".text\n"
    ".globl dpe_iii_stub\n"
    "dpe_iii_stub:\n"
    "    fldz\n"
    "    fldz\n"
    "    fldz\n"
    "    fldz\n"
    "    cmpl   %ebp, %eax\n"            // FindPlayerVehicle() == veh ?
    "    jne    1f\n"                    // не машина игрока -> NPC
    "    cmpb   $0, dpe_iii_cars\n"
    "    jne    1f\n"                    // Cars=1 -> NPC
    "    jmp    *dpe_iii_player\n"
    "1:\n"
    "    jmp    *dpe_iii_npc\n"
);

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
    g_findPlayerVehicle = 0x4A10C0;
    g_audioManager      = (const uint8_t*)0x880FC0;
    g_sampleManager     = 0x7341E0;
    g_stopChannel       = 0x567BE0;
    g_justEnteredCar    = 0x6508C4;

    if (g_flexible) {
        if (!WriteJmp(III_SITE, (uintptr_t)&IIIBranchStub, 12)) return false;
    } else if (g_cars) {
        static const uint8_t jmpShort[1] = { 0xEB }; // jne -> jmp
        if (!WriteBytes(III_JNE, jmpShort, sizeof(jmpShort))) return false;
    }
    if (g_flexible || (g_cars && dpe_volume != 100)) {
        g_volHooked = BytesEqual(III_VOL_SITE, III_VOL_ORIGINAL, sizeof(III_VOL_ORIGINAL)) &&
                      WriteVolumeCall(III_VOL_CALL, (void*)&IIIVolStub);
        if (!g_volHooked) {
            LogWarning("Volume unavailable: could not install the engine volume hook");
        }
    }
    if (g_flexible || g_worldVolume != 100) {
        g_findPlayerPed     = 0x4A1150;
        g_audioEntities     = 0x1DC8;
        g_audioEntityCount  = 200;
        g_worldHooked = HookQueue(0x57B070, III_QUEUE_ORIGINAL, sizeof(III_QUEUE_ORIGINAL), (void*)&IIIQueueStub);
        if (!g_worldHooked) {
            LogWarning("WorldVolume unavailable: could not install the world volume hook");
        }
    }
    return true;
}

// ---------------------------------------------

// ---------- Перечитывание ini по клавише ----------
//
// После обновления клавиатуры сравниваем NewKeyState и OldKeyState,
// чтобы обработать только кадр нажатия.
// CKeyboardState одинаковый в III, VC и SA:
//   +0x00 int16 F[12]               F1..F12
//   +0x18 int16 standardKeys[256]   по коду символа: 'A'..'Z', 'a'..'z', '0'..'9'

static uintptr_t g_newKeyState = 0;
static uintptr_t g_oldKeyState = 0;
static uintptr_t g_inputUpdate = 0;

// Поддерживаются F1..F12, A..Z, 0..9 (коды клавиш Windows).
static bool IsGameKey(int vk) {
    return (vk >= VK_F1 && vk <= VK_F12) || (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9');
}

static bool GameKeyDown(const int16_t* ks, int vk) {
    if (vk >= VK_F1 && vk <= VK_F12) {
        return ks[vk - VK_F1] != 0;
    }
    const int16_t* standard = ks + 12;
    if (vk >= 'A' && vk <= 'Z') {
        return standard[vk] != 0 || standard[vk + ('a' - 'A')] != 0;
    }
    if (vk >= '0' && vk <= '9') {
        return standard[vk] != 0;
    }
    return false;
}

static void CheckReloadKey() {
    if (GameKeyDown((const int16_t*)g_newKeyState, g_reloadKey) &&
        !GameKeyDown((const int16_t*)g_oldKeyState, g_reloadKey)) {
        LoadSettings(true);
        LogSettings("ReloadKey pressed, reloaded: ");
    }
}

static void __cdecl InputUpdateHook() {
    ((void(__cdecl*)())g_inputUpdate)();
    CheckReloadKey();
}

static bool SetReloadKeyStates(uintptr_t newKeys, uintptr_t oldKeys) {
    constexpr size_t keyBytes = (12 + 256) * sizeof(int16_t);
    if (!IsReadable(newKeys, keyBytes) || !IsReadable(oldKeys, keyBytes)) {
        return false;
    }
    g_newKeyState = newKeys;
    g_oldKeyState = oldKeys;
    return true;
}

static bool HookReloadKey(uintptr_t lo, uintptr_t hi, uintptr_t target) {
    PatchWrite call;
    uint8_t code[1][5];
    if (!FindCallPatches(lo, hi, target, (uintptr_t)&InputUpdateHook, &call, code, 1)) {
        return false;
    }
    g_inputUpdate = target;
    return WritePatches(&call, 1);
}

static bool HookSaReloadKey() {
    if (!SetReloadKeyStates(0xB73190, 0xB72F20)) return false;
    // CGame::Process вызывает CPad::UpdatePads до вызова CStreaming::Update.
    if (HookReloadKey(0x53BEE0, 0x53BF0B, 0x541DD0)) return true;
    if (!g_worldHooked || !IsReadable(SA_FRAME_COUNTER, sizeof(uint32_t))) return false;
    g_saReloadFrame = (const uint32_t*)SA_FRAME_COUNTER;
    LogWarning("ReloadKey: using the San Andreas audio update fallback");
    return true;
}

static void LogPatched(const char* game, bool ok, bool reloadHooked) {
    LogLine l; l.n = 0;
    LStr(l, "game: "); LStr(l, game);
    LStr(l, ok ? ", engine patch OK" : ", engine patch FAILED");
    LStr(l, ", world hook: "); LStr(l, g_worldHooked ? "yes" : "no");
    LStr(l, ", volume hook: "); LStr(l, g_volHooked ? "yes" : "no");
    LStr(l, ", reload hook: "); LStr(l, reloadHooked ? "on" : "off");
    LogWrite(l);
}

// ---------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    DisableThreadLibraryCalls(hinst);
    g_isSA = IsSA10US();
    const bool isVC = !g_isSA && IsVC10();
    const bool isIII = !g_isSA && !isVC && IsIII10();
    if (g_isSA) g_gameIniSection = L"SA";
    else if (isVC) g_gameIniSection = L"VC";
    else if (isIII) g_gameIniSection = L"III";
    LoadConfig(hinst);

    // Клавиша читается из CPad::NewKeyState игры: там есть только F1..F12, буквы и цифры.
    if (g_reloadKey != 0 && !IsGameKey(g_reloadKey)) {
        LogLine l; l.n = 0;
        LStr(l, "ReloadKey "); LHex(l, (unsigned)g_reloadKey);
        LStr(l, " is not supported (use F1..F12, A..Z, 0..9), using F7");
        LogWrite(l);
        g_reloadKey = VK_F7;
    }
    {
        LogLine l; l.n = 0;
        LStr(l, "ReloadKey: ");
        if (g_flexible) LHex(l, (unsigned)g_reloadKey); else LStr(l, "off");
        LogWrite(l);
    }

    // Вызовы CPad::UpdatePads в CGame::Process и состояния клавиатуры версий 1.0.
    const char* game;
    bool ok;
    bool reloadHooked;
    if (g_isSA) {
        game = "San Andreas 1.0 US";
        ok = PatchSA();
        reloadHooked = ok && g_flexible && HookSaReloadKey();
    } else if (isVC) {
        game = "Vice City 1.0";
        ok = PatchVC();
        reloadHooked = ok && g_flexible && SetReloadKeyStates(0x7D15A8, 0x7DDB88) &&
                       HookReloadKey(0x4A4412, 0x4A4417, 0x4AB6C0);
    } else if (isIII) {
        game = "GTA III 1.0";
        ok = PatchIII();
        reloadHooked = ok && g_flexible && SetReloadKeyStates(0x6E60D0, 0x6F1E70) &&
                       HookReloadKey(0x48C850, 0x48C855, 0x492720);
    } else {
        LogWarning("unsupported game or version; no game patches applied");
        return TRUE;
    }
    LogPatched(game, ok, reloadHooked);
    if (!ok) {
        LogWarning("could not install the engine patch; plugin disabled");
    } else if (g_flexible && !reloadHooked) {
        LogWarning("ReloadKey unavailable: could not hook the game update; restart to apply INI changes");
    }
    return TRUE;
}
