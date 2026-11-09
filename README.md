# DummyPlayerEngine

Машина игрока звучит как машины NPC: без отдельных сэмплов разгона и сброса газа и без «игроковской» логики двигателя. Один и тот же файл работает в нескольких играх, а через файл настроек можно выбрать, для какого транспорта менять звук.

## Поддерживаемые игры

- GTA San Andreas 1.0 US
- GTA Vice City 1.0
- GTA III 1.0

## Установка

1. Нужен ASI Loader (в SA также подойдёт modloader).
2. Положите `DummyPlayerEngine.asi` и `DummyPlayerEngine.ini` в папку игры (рядом с `gta_sa.exe`, `gta-vc.exe` или `gta3.exe`).

## Настройки

В `DummyPlayerEngine.ini` для каждого вида транспорта стоит `1` (звук как у NPC) или `0` (обычный звук игрока):

```ini
[DummyPlayerEngine]
Cars=1
Bikes=1
Quads=1
```

- `Cars` — машины (в San Andreas также монстр-траки);
- `Bikes` — мотоциклы (San Andreas, Vice City; в GTA III мотоциклов нет);
- `Quads` — квадроциклы (только San Andreas).

Например, чтобы NPC-звук был только на мотоциклах, поставьте `Cars=0`, `Bikes=1`. Без файла настроек включено всё.

## Как это работает

**San Andreas.** Если включено всё, в начало `CAEVehicleAudioEntity::ProcessPlayerVehicleEngine` (`0x4FBB10`) ставится переход на `CAEVehicleAudioEntity::ProcessDummyVehicleEngine` (`0x501480`). Если включено не всё, вызовы `ProcessPlayerVehicleEngine` из `CAEVehicleAudioEntity::ProcessVehicle` перенаправляются на обёртку, которая по типу транспорта выбирает одну из двух функций. Адреса из [gta-reversed](https://github.com/gta-reversed/gta-reversed).

**Vice City.** В `cAudioManager::ProcessVehicleEngine` машина игрока уходит в `ProcessPlayersVehicleEngine` (`0x5F0EA0`), если это не гольф-кар. Если включено всё, мод заменяет условный переход «это гольф-кар?» (`0x5F218A`) на безусловный, и машина игрока всегда идёт по ветке NPC. Если включено не всё, на месте этой проверки стоит переход на небольшую вставку, которая дополнительно смотрит, машина это или мотоцикл. Перед патчем мод сверяет байты вокруг этого места с оригинальным `gta-vc.exe` 1.0. Логика взята из reVC, адреса найдены по дизассемблеру exe.

**GTA III.** В `cAudioManager::ProcessVehicleEngine` машина игрока уходит в `ProcessPlayersVehicleEngine` (`0x56B0D0`). Если `Cars=1`, мод заменяет условный переход «это не машина игрока?» (`0x56A6BF`) на безусловный, и машина игрока всегда идёт по ветке NPC. Перед патчем мод сверяет байты вокруг этого места с оригинальным `gta3.exe` 1.0. Логика взята из re3, адреса найдены по дизассемблеру exe.

В Vice City и GTA III звук NPC и так зависит от передачи, поэтому ступеньки при разгоне останутся. Пропадут отдельные сэмплы разгона и сброса газа, которые есть только у игрока.

## Особенности

- В неподдерживаемой игре или версии мод покажет предупреждение и ничего не изменит.
- Затронуты машины и мотоциклы (в GTA III только машины). Лодки, самолёты и вертолёты не меняются.

## Сборка

Нужен Zig (например, `pip install ziglang`). Мод собирается без C-рантайма и импортирует только `kernel32` и `user32`:

```
zig c++ -target x86-windows-gnu -O2 -fno-exceptions -fno-rtti -fno-stack-protector -c DummyPlayerEngine.cpp -o d.o
zig dlltool -m i386 -k -d kernel32.def -l kernel32.lib
zig dlltool -m i386 -k -d user32.def -l user32.lib
zig lld-link /dll /machine:x86 /entry:DllMain@12 /nodefaultlib /subsystem:windows /safeseh:no /out:DummyPlayerEngine.asi d.o kernel32.lib user32.lib
```

`kernel32.def`:
```
LIBRARY KERNEL32.dll
EXPORTS
DisableThreadLibraryCalls@4
FlushInstructionCache@12
GetCurrentProcess@0
GetModuleFileNameW@12
GetPrivateProfileIntW@16
VirtualProtect@16
VirtualQuery@12
```

`user32.def`:
```
LIBRARY USER32.dll
EXPORTS
MessageBoxW@16
```
