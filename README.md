# Hide Stowed Left-Hand Item — *Dark Souls: Remastered*

While you two-hand the **right-hand** weapon, the model of whatever sits in your **left** hand — shield, bow, talisman, catalyst, staff — **is no longer drawn**. Let go and it comes straight back.

> Dark Souls: Remastered. Only the display model id in memory is touched: weight, requirements, icon, description, SpEffects and your save are untouched.

**How**: each character keeps a part-model cache, and a part's file name is built from the `equipModelId` u16 at `+0xB8` of the item's `EquipParamWeapon` row (`WP_A_%04d`). Zeroing that u16 makes the item resolve to an empty part with no mesh; the part cache is then rebuilt and the model is gone. Un-hiding writes the original value back and rebuilds once more. No equipment slot is ever written, so the HUD icon, the equip menu and every stat calculation see nothing at all — the price is a transition of about 1–3 frames, where the item is mounted on your back just before it disappears.

## Install

Place `dinput8.dll` into the game directory (next to `DarkSoulsRemastered.exe`).

## Source & build

`src/mod.cpp` is the mod itself; `src/proxy.cpp` and `src/proxy.def` are the dinput8 proxy shell that forwards all six entries the system DLL exports — `DirectInput8Create`, `GetdfDIJoystick` and the four COM/registration entry points. Needs the Desktop C++ workload of VS Build Tools — one line from an "x64 Native Tools Command Prompt for VS", run in the repo root:

```bat
cl /nologo /std:c++17 /EHsc /O2 /MT /LD /Fe:dinput8.dll src\proxy.cpp src\mod.cpp /link /SUBSYSTEM:WINDOWS /DEF:src\proxy.def /NOLOGO
```

The result is `dinput8.dll` in the repo root. Keep `/Fe`: `cl /LD` names the DLL after the first source file, so without it you get `proxy.dll`.

The checked-in `dinput8.dll` was built with MSVC 19.51 (VS 2026 Build Tools, x64). Any recent MSVC x64 toolset compiles this source, but a different one gives a different binary.

## Known limits

* Hiding works at the **param row** level: while it is hidden, that item is not drawn on any character (in PvP the other player will not see their own copy of it — hitboxes are unaffected).
* Switching takes about 1–3 frames. That is the part-cache mechanism of DSR: unlike Dark Souls III it has no per-frame hang-position table that could be re-pointed.
