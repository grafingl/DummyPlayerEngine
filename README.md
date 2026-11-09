# DummyPlayerEngine

Машина игрока звучит как машины NPC: без отдельных сэмплов разгона и сброса газа и без «игроковской» логики двигателя. Один и тот же файл работает в нескольких играх.

## Поддерживаемые игры

- GTA San Andreas 1.0 US
- GTA Vice City 1.0
- GTA III 1.0

## Установка

1. Нужен ASI Loader (в SA также подойдёт modloader).
2. Положите `DummyPlayerEngine.asi` в папку игры (рядом с `gta_sa.exe`, `gta-vc.exe` или `gta3.exe`).

## Как это работает

**San Andreas.** В начало `CAEVehicleAudioEntity::ProcessPlayerVehicleEngine` (`0x4FBB10`) ставится переход на `CAEVehicleAudioEntity::ProcessDummyVehicleEngine` (`0x501480`). У обеих функций одинаковая сигнатура, поэтому хватает одного `jmp`. Адреса из [gta-reversed](https://github.com/gta-reversed/gta-reversed).

**Vice City.** В `cAudioManager::ProcessVehicleEngine` машина игрока уходит в `ProcessPlayersVehicleEngine` (`0x5F0EA0`), если это не гольф-кар. Мод заменяет условный переход «это гольф-кар?» (`0x5F218A`) на безусловный, и машина игрока всегда идёт по ветке NPC. Перед патчем мод сверяет байты вокруг этого места с оригинальным `gta-vc.exe` 1.0. Логика взята из reVC, адреса найдены по дизассемблеру exe.

**GTA III.** В `cAudioManager::ProcessVehicleEngine` машина игрока уходит в `ProcessPlayersVehicleEngine` (`0x56B0D0`). Мод заменяет условный переход «это не машина игрока?» (`0x56A6BF`) на безусловный, и машина игрока всегда идёт по ветке NPC. Перед патчем мод сверяет байты вокруг этого места с оригинальным `gta3.exe` 1.0. Логика взята из re3, адреса найдены по дизассемблеру exe.

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
VirtualProtect@16
VirtualQuery@12
```

`user32.def`:
```
LIBRARY USER32.dll
EXPORTS
MessageBoxW@16
```
