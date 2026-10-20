# Fable III couch co-op

Local (couch) co-op for the PC version of Fable III: two controllers, one game, one screen. No
Nucleus Co-op and no second game instance. The Xbox 360 game had couch co-op, and the PC build
still contains most of that code; the PC port just never wires up a second controller. The mod
is an `xinput1_3.dll` proxy that patches the game at load time to turn it back on.

## What works (v1)

- Player 2 joins with START on the second controller during gameplay, after picking a hero.
- Each player controls their own hero. The camera is shared, as on the 360.
- Player 2 has their own pause menu (D-pad and buttons), and Back teleports them to player 1.
- Interacting and giving gifts work in both directions, including the Sanctuary gift step.
- Each player has their own hero save:
  - Player 1 keeps saving to the slot they loaded.
  - Player 2's hero is saved with every game save.
  - Co-op progress on a hero from a main save goes back into that hero.
- Hero pickers, drawn with the game's own menu art:
  - **Player 2:** START opens a list of every hero they can join as, plus "New hero".
  - **Player 1:** BACK on the title screen chooses which hero Continue loads.

Known issues:

- Player 2 can't move through their pause menu with the thumbsticks; use the D-pad (findings R25).
- Turning down a gift is an awkward hold-and-release prompt.

## Requirements

1. **Fable III for PC, the Games for Windows LIVE build from 2011-05-20** (`Fable3.exe` timestamp
   1305884087, title version 10000283). The mod checks this. On any other build it logs
   `unexpected exe timestamp` and stays inactive (the controllers still work as normal).
2. **xLiveLess** in place of GFWL: its `xlive.dll` in the game folder. The mod was built and
   tested with xLiveLess 2.0a4. Real GFWL is not supported.
3. **Two XInput controllers.** The first controller is player 1 and the second is player 2.
   Keyboard and mouse stay with player 1.
4. **The DirectX June 2010 runtime** (the game's `_Redist` installs it). The mod forwards every
   XInput call to the system's `xinput1_3.dll`.

### xLiveLess setup (`xlive.ini` in the game folder)

The game signs player 2 in as local user index 3, so xLiveLess needs **four profiles, numbered
1–4 with no gaps, all signed in**. With profile 3 missing, player 2's sign-in hung (xLiveLess
seems to read profiles in order and stop at the first gap). Profile 1 is player 1;
profile 4 is the one player 2 uses. Names can be anything without spaces, and each XUID is any
unique 16-digit hex number:

```ini
profile name 1 = Player1
profile xuid 1 = 1122334400000000
signin player 1 = 1
profile name 2 = Player2
profile xuid 2 = 1122334400000001
signin player 2 = 1
profile name 3 = Player3
profile xuid 3 = 1122334400000002
signin player 3 = 1
profile name 4 = Player4
profile xuid 4 = 1122334400000003
signin player 4 = 1

online profile = 1
save directory = XLiveEmu
debug log = 0
```

Saves go to `Saved Games\Lionhead Studios\Fable 3\<profile 1's XUID>\`. With the default
`SharedSaveFolder=1`, player 2's heroes are kept there too, under player 1's XUID, so the
game treats a co-op hero from a main save as a normal main save.

**Back up `Saved Games\Lionhead Studios\Fable 3` before the first run.**

## Install

1. Build the DLL (below), or take `xinput1_3.dll` from a release.
2. Copy `xinput1_3.dll` into the Fable III folder (next to `Fable3.exe` and `xlive.dll`).
3. Copy `config/fable3coop.ini` into the same folder and adjust it (see below).
4. Start the game as usual. Plug in the second controller before player 2 joins.

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

Visual Studio 2022 (C++ desktop workload) and CMake 3.20 or newer. The game is 32-bit, so
configure for Win32:

```powershell
cmake -B build -A Win32 -DFABLE3_DIR="C:/Games/Fable 3"
cmake --build build --config Release
cmake --build build --config Release --target deploy   # copies the DLL into FABLE3_DIR
```

The deploy step fails while the game is running, because the game keeps the DLL open.

## Repository

- `src/`: the mod. `addresses.h` holds every game address used, with what is known about it.
  - `custommenu.cpp` is a reusable menu with any number of items, drawn on the Options menu.
  - `picker.cpp` builds the hero pickers on top of it.
- `config/fable3coop.ini`: the settings template.
- `docs/findings.md`: the reverse-engineering ledger. Every behaviour the mod relies on is there,
  with its evidence.
- `tools/`: Ghidra query helper, Lua and GUI bank extractors, and the GUI Lua test scripts used
  with `LuaProbe`.

Game files and saves are never committed (`assets/` and `backup/` are ignored).
