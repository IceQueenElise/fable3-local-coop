#pragma once

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
