#pragma once

// GUI input for player 2.
//
// The PC build creates one NUI::CGUIInput, always bound to the main player, so only player
// 1's START opens the pause menu and nobody's Back teleports the couch partner. While player
// 2 is in the game, a second CGUIInput bound to player 2's CPlayer is registered with the
// game's input system, so their controls get the same GUI actions.
//
// The menus read player 1's thumbsticks, so while player 2 owns the open menu the XInput
// exports swap the two pads' thumbstick values (proxy.cpp).

// Installs the CGUIInput action hook. Call after InstallInputHooks has set the image base.
bool InstallGuiHooks();

// Call once per frame on the game thread.
void UpdateCouchGuiInput();

// Pause menu or Sanctuary is up. Calls into the game; use on the game thread.
bool IsGuiOpen();

// Id of the game mode on top of the mode stack (6 gameplay, 8 pause, 1 Sanctuary, 4 loading),
// or -1 when there is none (title screen). Game thread only.
int GameModeId();

// Player 2 has the pause menu / Sanctuary open. Cached once per frame; safe from any thread.
bool CouchPlayerHasMenu();

// Who opened the GUI that is currently up.
enum class GuiOwner { None, MainPlayer, CouchPlayer };
GuiOwner CurrentGuiOwner();
