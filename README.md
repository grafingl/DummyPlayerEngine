# DummyPlayerEngine

Мод для **GTA San Andreas 1.0 US**: машина игрока звучит так же, как машины NPC — без переключения передач, с ровным звуком двигателя, который просто растёт вместе со скоростью.

## Установка

1. Нужен ASI Loader (или modloader).
2. Положите `DummyPlayerEngine.asi` в папку игры (рядом с `gta_sa.exe`) или в папку `modloader`.

## Как это работает

В начало функции `CAEVehicleAudioEntity::ProcessPlayerVehicleEngine` (`0x4FBB10`) ставится переход на `CAEVehicleAudioEntity::ProcessDummyVehicleEngine` (`0x501480`). У обеих функций одинаковая сигнатура (`__thiscall`, один параметр), поэтому достаточно одного `jmp`.

Адреса взяты из проекта [gta-reversed](https://github.com/gta-reversed/gta-reversed).

## Особенности

- Работает только на версии 1.0 US. На других версиях мод покажет предупреждение и ничего не изменит.
- Звук меняется у машин и мотоциклов. Лодки, самолёты и вертолёты не затронуты.

## Сборка

Любой компилятор, собирающий 32-битные DLL под Windows. Например, через Zig:

```
zig c++ -target x86-windows-gnu -shared -Os -s -fno-exceptions -fno-rtti -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdlib -Wl,--entry=DllMain@12 -o DummyPlayerEngine.dll DummyPlayerEngine.cpp -lkernel32 -luser32
```

(Флаг `-nostdlib` убирает зависимость от C-рантайма — мод весит ~3 КБ. В Visual Studio собирается как обычная Win32 DLL.)

Затем переименуйте `DummyPlayerEngine.dll` в `DummyPlayerEngine.asi`.
