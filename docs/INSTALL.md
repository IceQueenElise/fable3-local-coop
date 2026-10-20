# Fable III local co-op: install

This zip holds three files:

| File | What it is |
|------|------------|
| `xinput1_3.dll` | The mod. |
| `fable3coop.ini` | The mod's settings. |
| `INSTALL.md` | These instructions (don't copy it anywhere). |

## Before you start

- You need the Games for Windows LIVE version of Fable III for PC, running with **xLiveLess** (its `xlive.dll` and `xlive.ini` in the game folder) instead of GFWL.
- You need two XInput controllers. The first one is player 1, the second is player 2.
- **Back up your saves:** copy `Saved Games\Lionhead Studios\Fable 3` somewhere safe.

## Install

1. Open your Fable III folder (the one with `Fable3.exe` and `xlive.dll` in it).
2. Copy `xinput1_3.dll` and `fable3coop.ini` from this zip into that folder.
3. Open `xlive.ini` in that same folder and add this line (player 2 signs in as profile 4):

   ```ini
   signin player 4 = 1
   ```

   Leave your existing profile 1 lines alone; they decide which save folder the game uses.
4. Start the game as usual.

## Playing

- **Player 2 joins** by pressing START on the second controller during gameplay, then picking a hero (or "New hero").
- **Player 1 picks which hero Continue loads** by pressing BACK on the title screen.
- Player 2's BACK teleports them to player 1.

## Settings

`fable3coop.ini` works as shipped. Set `Log=1` if something goes wrong; the mod then writes `fable3coop.log` next to the DLL, which is what to include with a bug report. You can name heroes in the pickers under `[Heroes]`, e.g. `hero5autosave=Bob`.

## Uninstall

Delete `xinput1_3.dll`, `fable3coop.ini` and `fable3coop.log` from the game folder, and remove the `signin player 4 = 1` line from `xlive.ini` if you like.
