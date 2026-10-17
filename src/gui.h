#pragma once

// GUI input for player 2.
//
// The PC build creates one NUI::CGUIInput, always bound to the main player, so only player
// 1's START opens the pause menu and nobody's Back teleports the couch partner. While player
// 2 is in the game, a second CGUIInput bound to player 2's CPlayer is registered with the
// game's input system, so their controls get the same GUI actions.
//
// The menus read stick input from the main CGUIInput only, so while player 2 has a menu open
// their stick actions are forwarded to it and player 1's are dropped.

// Installs the CGUIInput action hook. Call after InstallInputHooks has set the image base.
bool InstallGuiHooks();

// Call once per frame on the game thread.
void UpdateCouchGuiInput();

bool IsGuiOpen();

// Who opened the GUI that is currently up.
enum class GuiOwner { None, MainPlayer, CouchPlayer };
GuiOwner CurrentGuiOwner();
