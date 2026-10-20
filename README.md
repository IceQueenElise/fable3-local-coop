# Fable III local co-op

Local (couch) co-op for the PC version of Fable III, built to restore local multiplayer functionality from the Xbox 360 version with two players on a single game instance.
The mod is an `xinput1_3.dll` proxy that patches the game at load time to turn back on the code that was left from the port.

#### !!! Fair warning this was built with the assistance of AI decompilation and coding tools, if that is not for you, do not use this mod !!!

## Features

- Player 2 can join with the START button on a second controller during gameplay, opening a save selector.
- Each player controls their own hero. The camera is shared, as on the 360.
- Back button teleporting them to player 1 restored.
- Interacting and giving gifts work in both directions, including opening gifts in the Sanctuary.
- Each player has their own hero save:
  - Player 1 keeps saving to the slot they loaded.
  - Player 2's hero is saved with every game save.
- ***New*** Hero selectors, reusing game menus:
  - **Player 1:** BACK on the title screen chooses which hero (journey) Continue loads.
  - **Player 2:** START opens a list of every hero they can join as, plus option for "New hero".

*Note: Saves are stored as "Journeys" which can within them store 5 saves (the save room), this mod lets you manage multiple journeys for more save slots, and independent progression for multiple players.*

Known issues:

- Player 2 can't move through their pause menu with the thumbsticks; use the D-pad.
- Rejecting a gift is buggy because of the press and release prompt (Might have to hit X on player 1's controller).

## Requirements

1. **Fable III for PC, the Games for Windows LIVE build from 2011-05-20** (`Fable3.exe` timestamp 1305884087, title version 10000283). The mod checks this. On any other build it logs `unexpected exe timestamp` and stays inactive.
2. **xLiveLess** in place of GFWL: its `xlive.dll` in the game folder. The mod was built and tested with xLiveLess 2.0a4.
3. **Two XInput controllers.** The first controller is player 1 and the second is player 2. Keyboard and mouse stay with player 1.
4. **The DirectX June 2010 runtime** (the game's `_Redist` installs it). The mod forwards every XInput call to the system's `xinput1_3.dll`.

### xLiveLess setup (`xlive.ini` in the game folder)

Player 2 signs in as xLiveLess profile 4, so the only line you need to add to your `xlive.ini` is `signin player 4 = 1`. Keep your existing profile 1 lines as they are, because profile 1's XUID decides which save folder the game uses.

```ini
profile name 1 = Player1
profile xuid 1 = 1122334400000000
signin player 1 = 1
signin player 4 = 1
```

Saves go to `Saved Games\Lionhead Studios\Fable 3\<profile 1's XUID>\`. With the default `SharedSaveFolder=1`, saves are all stored under player 1's XUID, so that saves can all be loaded as either main hero or henchman.

**Back up `Saved Games\Lionhead Studios\Fable 3` before the first run.**

## Install

1. Download the zip from the latest release (or build the DLL, below). Its `INSTALL.md` (also in `docs/`) has the steps.
2. Copy `xinput1_3.dll` and `fable3coop.ini` into the Fable III folder (next to `Fable3.exe` and `xlive.dll`).
3. Add `signin player 4 = 1` to `xlive.ini` (see above).
4. Start the game as usual. And join on a second controller.

To uninstall, delete `xinput1_3.dll` (and `fable3coop.ini`, `fable3coop.log`) from the game folder.

## Settings (`fable3coop.ini`)

All keys are under `[Fable3Coop]` unless noted.

| Key | Default | Meaning |
|-----|---------|---------|
| `Log` | 1 when the key is missing (0 in the shipped ini) | 1 writes `fable3coop.log` next to the DLL: diagnostics, every gameplay button press, crashes. Turn it on when reporting a problem. |
| `PartnerPicker` | 1 | 1: player 2's START opens the hero picker before joining. 0: join straight away with `PartnerSave`. |
| `PartnerSave` | blank | The hero player 2 joins with when the picker is off: blank = the game's choice (newest hero-only save), `new` = a new hero, or a save name (e.g. `hero5`). A full main save loads only its hero, and co-op progress saves back into that hero. |
| `SharedSaveFolder` | 1 | 1: player 2's heroes live in player 1's save folder (see above). 0: each player's saves go to their own profile's folder. |
| `[Heroes]` *save*`=`*name* | none | Names shown in the pickers instead of the save name, e.g. `hero5autosave=Bob`. |
| `[State] LastMainSave` | written by the mod | Player 1's last own save, which Continue loads. Leave it alone. |

Debug keys, all off by default; leave them off for normal play:

| Key | Default | Meaning |
|-----|---------|---------|
| `MenuTest` / `MenuTestKey` | 0 / F8 | 1: the key opens a 12-item test menu built on the custom menu component. |
| `LuaProbe` / `LuaProbeScreen` / `LuaProbeKey` | 0 / 17 / F9 | 1: runs `luaprobe.lua` from the game folder as Gameface GUI Lua on a screen (17 = Options); the key re-runs the last one. Lua output and errors go to the log. |

Key values are Windows virtual-key codes (F8 = 119, F9 = 120).

## Building

Visual Studio 2022 (C++ desktop workload) and CMake 3.20 or newer. The game is 32-bit, so configure for Win32:

```powershell
cmake -B build -A Win32 -DFABLE3_DIR="C:/Games/Fable 3"
cmake --build build --config Release
cmake --build build --config Release --target deploy   # copies the DLL into FABLE3_DIR
cmake --build build --config Release --target release  # build/Fable3Coop-v<version>.zip (DLL, ini, docs/INSTALL.md)
```

The deploy step fails while the game is running, because the game keeps the DLL open.
