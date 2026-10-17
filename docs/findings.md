# Findings ledger

Target: `F:\Fit Games\Fable 3\Fable3.exe`. 32-bit PE, unpacked, built 2011-05-20 (timestamp 1305884087), title version `10000283`. Addresses are virtual addresses at the default image base `0x00400000`.

Ghidra project: `F:\Fable3Coop\ghidra` (Ghidra 12.1.4, full auto-analysis, 1278 s). Raw query output: `docs\ghidra\*.txt`. Re-run queries with `tools\gq.ps1 <name> <cmd>...`.

## Phase 1 (runtime)

| # | Finding | Evidence | Type |
|---|---------|----------|------|
| 1 | With a second xLiveLess profile configured and signed in, the game only ever queries user index 0, and `XSessionJoinLocal` adds only user 0 | `xlive_trace.log`, 2026-10-17 11:55–11:56 | Runtime API trace |
| 2 | Pressing buttons on pad 2 does nothing; the pause menu has no co-op entries; the pause-menu "Live" option does nothing | User test, 2026-10-17 | Runtime observation |

## Phase 2 (static, Ghidra)

### Input layer: the PC limit
| # | Finding | Evidence | Confidence |
|---|---------|----------|------------|
| 3 | `NInput::CInputManagerDX` ctor `FUN_0188de60` creates **one** XInput device, for pad 0: `XInputGetState(0,…)`, then `XOR EAX,EAX` before `CALL 0x0188c730` | q2 decompile, q5 disasm at `0188dee5`–`0188df07` | Observed (static) |
| 4 | Hot-plug `FUN_0188dcf0` also only polls pad 0 (`PUSH 0; CALL XInputGetState` at `0188dd28`); it creates the device with `XOR EAX,EAX` at `0188dd66` / `0188dd9f` | q5 | Observed |
| 5 | `NInput::CJoystickXBox360` ctor `FUN_0188c730` (size 0xA0, allocated via `FUN_01a035c0`). ABI: **ECX = this, EAX = pad index** (stored at `+4` by base ctor `FUN_005cdeb0`), stack arg = input manager (stored at `+0x44`) | q3 / q4 decompile, q5 | Observed |
| 6 | Device registration `FUN_0188ef00`: ECX = input manager, **EDI = device**. Device vector at manager `+0xC..+0x10` (8-byte entries), deduplicated by device `+4`; critical section at manager `+0x4C` | q3 | Observed |
| 7 | Every XInput call in the pad device uses `[this+4]` as the user index (`FUN_0188b9a0`, `FUN_0188ba30`, `FUN_0188bb90`). **A second device built with EAX=1 should read pad 2 without further changes** | q2, q3 | Inferred |
| 8 | Lua `IsControllerAvailable(n)` (`FUN_006b70d0`) returns `n <= count of type-1 devices` (`FUN_0188ec50(1)`) and loops over slots 1..4 (`FUN_006b6f60`): **the game layer above the input manager still handles 4 controllers** | q3, q4 | Observed |
| 9 | `CInputManagerDX` is created by `FUN_018f7d30` (call at `018f7dc6`) | q4 | Observed |

### Couch-co-op path: still present
| # | Finding | Evidence | Confidence |
|---|---------|----------|------------|
| 10 | Lua bindings are registered as `push handler; push name; call register` (handler pushed **before** the name) | q1, phase2 | Observed |
| 11 | Game mode = `[netmgr + 0xB8]`, where netmgr = `*(*(DAT_01dc2550) + 0x10)`. Getter `FUN_006839e0`, setter `FUN_006839f0`, global wrapper `FUN_00673c90(mode)`. **Mode 1 = couch**: Lua `IsInCouchGame` = `FUN_006a8a90` returns `mode == 1` | q1, q6, q7 | Observed |
| 12 | Lua `SetSharedScreenModeToggle` = `FUN_0068ed30(this, bool on, idx)`; on enable it calls `FUN_00673c90(1)` (couch mode), on disable `FUN_00673c90(0)` | phase2, q8 | Observed |
| 13 | **Add-local-player routine `FUN_00bbfa10(playerIdx, …, skipSignin, …)`**: shows sign-in for that user (`FUN_007720e0` → `XShowSigninUI`), spawns `"CreatureHero"` (`FUN_008285f0`), sets couch mode, creates a player controller `FUN_00bbec20(2, playerIdx)`, and stores `DAT_01c86be8 = playerIdx` | q2, q9 | Observed (static); runtime behaviour untested |
| 14 | `DAT_01c86be8` = second local player's controller index (-1 = none). Couch-join driver `FUN_006b6ad0`: if set and no second hero, call `FUN_00bbfa10(DAT_01c86be8, …)`, then `SetSharedScreenModeToggle(1, idx)`. Reset to -1 by quit-to-frontend `FUN_00677320`, load `FUN_006775e0`, and drop-out `FUN_00678520` | q9, q10, q11 | Observed |
| 15 | **`FUN_00bc0500(idx)` = join local player**: `FUN_00bbfa10(idx, …)` (with sign-in), then `SetSharedScreenModeToggle(1, idx)`. Single cdecl argument. `FUN_009ef2d0` is a re-add path used after loading | q12 | Observed |
| 15a | **Press-Start detector `FUN_00b9b0c0(this, event)`** (an input-event handler, referenced from a vtable at `01b1a26c`) calls `FUN_00bc0500(event+0x28)` when: in gameplay (`FUN_00b9ac50`), event `+0x2C == 0xD`, event byte `+8 == 5`, event controller `+0x28` ≠ main player's (`[this+8]+0x34`), no player already owns that controller (`FUN_006b6c00`), `DAT_01c86c16 != 0`, not a Live game, and mode ≠ 1 | q13, q16 | Observed (static) |
| 15b | `DAT_01c86c16` ("couch join allowed") **defaults to 1** in `.data`. Its only writer is a tiny setter at `00616620` (`mov al,[esp+4]; mov [01c86c16],al; ret`) | q14, raw bytes | Observed |
| 15c | Input device ids (`+4`): **joystick 0, keyboard 1 (`FUN_0188caa0`), mouse 2 (`FUN_0188a470`)**. `AddDevice` rejects a duplicate id and deletes the new device. Lookups scan the device list (`FUN_0188ed10`); there is no fixed-size array | q18–q21 | Observed |
| 15d | Calling conventions: allocator `01a035c0` cdecl; joystick ctor ECX=this, EAX=id, push manager, `RET 4`; AddDevice ECX=mgr, EDI=device, `RET`; RemoveDevice ESI=mgr, push id, `RET 4`; hot-plug `0188dcf0` thiscall, no args, `RET`, reached through vtable slot `01bf91a8` | q17, q22 | Observed |
| 15e | **Fable3.exe is DYNAMICBASE** (DllCharacteristics 0x8140), so all addresses must be rebased at runtime | PE header | Observed |

### Sign-in
| # | Finding | Evidence | Confidence |
|---|---------|----------|------------|
| 16 | `XUserGetSigninState` callers: `FUN_0061cb70` (4 calls), `FUN_007720e0`, `FUN_00c26770`, `FUN_00c26630`, `FUN_00c2d2f0`, `FUN_00679310`, `FUN_00685540`, `FUN_0071a4a0`. `DAT_01c86be4` is passed as the active user index in `FUN_00685540` | q1, q2 | Observed |

### Side paths
- `data\*.bnk.dat` start with a zlib header (`78 DA`) but don't inflate at any offset: obfuscated or a modified deflate. Parked.

## Phase 3 build 1 (2026-10-17)
`xinput1_3.dll` proxy (`src/`), deployed to the game folder.
- **Hot-plug hook:** patches vtable slot `01bf91a8` (rebased) to `HotplugDetour`. The detour runs the original pad-0 check, then once per second checks physical pad 1. If pad 1 is connected and no device with id 3 exists, it creates a `CJoystickXBox360` with **id 3** and registers it; if pad 1 is gone, it removes the device.
- **Pad remapping:** id 3 is used because ids 1 and 2 are taken. The proxy's XInput exports map user index 3 to physical pad 1.
- **Safety checks:** exe timestamp, the `XOR EAX,EAX` bytes at `0188dd66`, and the vtable slot's current value. On any mismatch the DLL is a plain XInput passthrough.
- **Log:** `fable3coop.log` next to the DLL. It records the first XInput call per user index, hot-plug call counts, and device add/remove.

### Runtime results (2026-10-17)
| # | Finding | Evidence | Type |
|---|---------|----------|------|
| R1 | The hot-plug check runs every frame. Our id-3 joystick is created about 2 s after launch and polled from then on (XInput calls for user 3 → pad 1) | `fable3coop.log` run 1 | Runtime |
| R2 | Pad 2 drives the main hero exactly like pad 1, and no co-op is triggered. `XSessionJoinLocal` still adds only user 0 | User test + `xlive_trace.log` run 1 | Runtime |
| R3 | The XInput button-event builder `FUN_0188bfe0` sets `event+0x28 = device+4`, so controller 2's NInput events carry user 3. Button bit `0x10` (START) → button id 5; type `0xD` = down, `0xF` = up | q29, q32 | Static |
| R4 | **The detector (`NUI::CGUIInput` vtable slot 6) receives Start presses in gameplay. Every gate passes (gameplay=1, joinAllowed=1, live=0, mode=0, mainUser=0) except one: `event+0x28 == 0` for presses on both pads.** The user index is rewritten to 0 somewhere between the NInput event and the GUI input layer | `fable3coop.log` run 2 (8 detector events) | Runtime |

| R5 | The heuristic stack scan of the detector's callers was too noisy to locate the re-stamping layer (stale values, vector helpers) | run 3 | Runtime, inconclusive |
| R6 | **Retagging pad 2's START to user 3 inside the detector makes the native join run.** The game calls `XShowSigninUI(cPanes=1)`, then the main thread hangs. Physics/cloth threads keep running | run 4 log + `xlive_trace.log` | Runtime |
| R7 | Cause of the hang: `FUN_007720e0(user)` checks sign-in cache entry `user`. If it isn't signed in and `XUserGetSigninState(user) == 0`, it calls `XShowSigninUI` and then waits in a `Sleep(1)` loop for the UI flag `[signin+0x20]` to go to 1 and then back to 0. xLiveLess raises and clears `XN_SYS_UI` within about 20 ms, so the loop never sees 1 | q37, trace | Static + runtime |
| R8 | **Sign-in cache refresh `FUN_00772190` is hard-coded to user 0** on PC (`XUserGetSigninInfo(0, …)`, writes entry 0 only). Entry layout (0x20): `+0` online, `+1` local, `+2` guest, `+8` XUID (online, else offline), `+0x10` offline XUID (flag 2), `+0x18` online XUID (flag 1). Cache object = `*(*(DAT_01dc2550)+0x28)`, entries vector at `+0x14/+0x18` | q38 | Static |
| R9 | xLiveLess logged no `XUserGetSigninState` for index 3 even though `XShowSigninUI` followed. Probable cause: profile 3 was missing (profiles 1, 2, 4), and xLiveLess reads profiles contiguously. Not yet verified | trace | Inference |

Build 5: before the join, seed sign-in cache entry 3 from `XUserGetSigninInfo(3)` (same fields as `FUN_00772190`). `xlive.ini` now has profiles 1–4.

Still open: the PC layer that re-stamps every event's `+0x28` to user 0. It has to be found for input routing anyway; the START retag is a stopgap.

| R10 | **Build 5: player 2 joins.** Sign-in cache seeded (Player2b, state 2), detector marks the event handled, couch mode set (`mode=1`), `XUserSetContext(userIndex=3)` repeats each second. A second hero appears with co-op UI | run 5 log + trace + user | Runtime |
| R11 | After the join, both pads drive the **new** hero; the camera stays on hero 1; GUI events are still user 0 | user + log | Runtime |
| R12 | `FUN_00914610` (player-control setup) looks up the input device **with id == CPlayer user index** (`FUN_0188ed10(userIdx)`) and marks it, plus the keyboard and mouse, active (`device+8 = 1`). This is the 360 device→player link, and it explains why id 3 = user 3 was wired to player 2 | q42 | Static |
| R13 | Hero control classes: `CECPlayer` (vtable `01b04eb4`), `CECPlayerControl` (`01b460d4`), `CInputProcess*` (e.g. CharacterMovement `01b644bc`). Bindings: `NControlSystem::CInputType*`. **Binding match methods check the user:** pad button `FUN_01358850(event, user)` = type + `event+0x28 == user` + button; left stick `FUN_013588e0` = type `0xB` + `event+0x28 == user` | q46–q51 | Static |

Build 6 adds a binding probe: it logs (caller, event user, requested user, result) for pad button and left-stick matches.

| R14 | Binding probe: every event reaching bindings has user 0, and every request is for user 0, before and after the join. Callers `00d899c8` (in `FUN_00d895d0`, user from the control context `[EBP+0xC]`) and `013593d0`. The user corrected R11: **the camera follows the new hero, and both pads still control hero 1** | run 6 log + user | Runtime |
| R15 | **Main input update `FUN_00b6dd20`:** ingests manager events (`FUN_00d89de0(mgr+0x2c)`), then for each input process gets its player slot (`vfunc+0x10`) → `FUN_006b6f60(slot)` → `CPlayer+0x34` user → `FUN_00d89480(user)` (per-user control states) | q55, q56 | Static |
| R16 | **The PC user flattening:** `FUN_00d89de0` does `event+0x28 = 0` for every event (`00d89e70 MOV [ECX+0x28],EBP` with EBP=0) and groups events with key 0 (`00d89e7c MOV [ESP+0x20],EBP`) | q56–q58 | Static |

| R17 | Build 8: each pad drives its own hero (user confirmed). The GUI never receives user-3 events, so pad 2 can't join through the GUI. Fixed by offering the detector a synthetic START event from the hot-plug hook each frame | runs 7–8 | Runtime |
| R18 | Build 9: pad 2's right stick (event type `0xC`) kept as user 0, so both players turn the shared camera (user confirmed) | run 9 | Runtime |
| R19 | **The GUI receives events via `FUN_00b6d020`** (detector caller `00b6d09d`), the raw-event feed for input processes whose `vfunc+0x14` is true. In gameplay it skips pad events (`+0x24 == 1`) whose user ≠ the main CPlayer's (`00b6d085`) | run 9 log, q70 | Runtime + static |
| R20 | GUI-open `FUN_007d7000(playerType, …)` opens only if `IsInLiveGame() \|\| playerType == 1 \|\| CanCouchPlayerEnterGUI`. The flag is the byte at `*(*(*(01dc2558)+0x1c)+0x58)+0x8d`; Lua `SetCanCouchPlayerEnterGUI` handler at `00eb88d0` → `FUN_00c26d90`. Also present: Lua `SetLocalHeroToEnterGUIOf` (`009ee730`), `SetIsGUIOfLocalHeroOverride` (`009edd20`), `IsFullPauseMenuDisabledForHero` (`00ebcae0`) | q63–q69 | Static |

| R21 | Build 11 (handing player 1's GUI input over to player 2) partly worked: the menu opened after two presses, then neither pad could drive it. The GUI state value `[[guiRoot+0x34]+4]` stays 33 throughout, so it isn't an open indicator. **In-GUI flag = byte `DAT_01dca550`** (set/cleared by the GUI update `FUN_008590a0`, read by `FUN_00856b10`) | run 11 log, q82–q84 | Runtime + static |
| R22 | **GUI input ownership:** `NUI::CGUIInput` (0x48 bytes, ctor `FUN_00b9ab30`) is created once by `FUN_00b9c760` (`guiRoot+0xC`) and registered by `FUN_00b9ae00` → `input->AddProcess` (`FUN_00b6d4e0`). Its player is set by `FUN_00b9abb0`, whose only caller `FUN_006acd70` always passes player type 1. Process slot = `[this+8]+0x30`. Its action handler (slot 1, `FUN_00b9bfd0`): START (action `0x5B`) → `FUN_00b9b900` (opens the GUI). Back → `FUN_00b9be30` → teleport request `FUN_00c3c5f0` (pending byte `+0x77`, done by `FUN_00c33460` with `FX_Henchman_Teleport`), **only when the slot is 2** | q90–q97, q85–q89 | Static |

Build 12: a second CGUIInput bound to player 2's CPlayer is registered with the input system while player 2 is in the game.

Build 10: GUI filter patched to pass user 3 too; `CanCouchPlayerEnterGUI` set once player 2 has joined (couch mode).

Build 7 patches `00d89e70` to jump to a stub that sets EBP = 3 for device-3 events (otherwise 0), which fixes both the event user and the group key. Sign-in seeding now also runs when a START with user 3 reaches the detector.

## Original working hypothesis for Phase 3
1. After `CInputManagerDX` is constructed (hook `FUN_018f7d30` or the ctor `FUN_0188de60`), allocate a 0xA0-byte block with the game allocator `FUN_01a035c0`. Call `FUN_0188c730` with ECX=block, EAX=1 and push the manager, then register it with `FUN_0188ef00` (ECX=manager, EDI=device). This needs naked asm thunks for the custom calling conventions.
2. Patch or extend the hot-plug `FUN_0188dcf0` so it doesn't drop or recreate the wrong device. It looks up the device with `+4 == 0`, so it may leave ours alone, but that needs checking.
3. Check whether the game's own join trigger now fires for pad 2 (`IsControllerAvailable(2)` becomes true). If not, call the couch-join path directly from a debug hotkey: set `DAT_01c86be8 = 1` and let `FUN_006b6ad0` run, or call `FUN_00bbfa10(1, …)` from the game thread.

## Open questions
- What triggers the join: `FUN_00bc0500` vs `FUN_009ef2d0`, and what input event do they wait for?
- Sign-in for player 2: `FUN_00bbfa10` calls `XShowSigninUI` for `playerIdx`. With xLiveLess profile 2 signed in, `XUserGetSigninState(1)` should already return signed-in. Verify at runtime.
- Controller → player routing: `FUN_00bbec20(2, playerIdx)` builds the second player's controller object. Check that it binds to input device `+4 == playerIdx`.
