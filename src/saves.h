#pragma once

#include <string>

// Per-player save folders.
//
// Saves live in "Saved Games\Lionhead Studios\Fable 3\<XUID>\", but the PC build always
// builds that path from user 0's XUID (FUN_00747b20 and friends call XUserGetSigninInfo(0)).
// So when player 2 joins, the game lists player 1's saves and loads player 1's hero as the
// partner, and saving the partner would write into player 1's folder.
//
// The save manager's sign-in queries go through SaveSigninInfo, which answers for a "save
// user" override instead of user 0 while one is set. The override is set around the save
// enumerator (to the user it was asked about) and the save writer (to the user of the player
// whose save it is). Call after InstallInputHooks has set the image base.
bool InstallSaveHooks();

// The user whose save folder the save manager is currently using (0 unless overridden).
int CurrentSaveUser();

// True when player 2's heroes are kept in player 1's save folder (fable3coop.ini SharedSaveFolder).
bool PartnerSavesInMainFolder();

// Player 1's save folder ("<Saved Games>\Lionhead Studios\Fable 3\<XUID>\"), or "".
std::wstring PlayerOneSaveFolder();

// The save in player 1's folder has a world and is owned by player 1's profile, so the main
// game can load it.
bool IsLoadableMainSave(const char* name);

// Makes Continue load this save (sets the game's initial save name).
void SetInitialSaveName(const char* name);

// Name of the main save currently loaded (e.g. "hero4autosave"), or "" if none.
std::string LoadedMainSaveName();

// The save player 2's next join should use (a save name, or "new"), overriding the
// PartnerSave setting for that one join. Pass nullptr to clear.
void SetPartnerChoiceOverride(const char* choice);
