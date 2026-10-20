#pragma once

#include <string>
#include <vector>

// Our own menus with any number of items, drawn with the game's Options menu art (findings R37).
//
// Opening one opens the Options menu (screen 0x11) and, once it is up, turns it into a list:
// the real rows, descriptions and mouse hotspots move off screen, Gameface Lua clones the
// NewGame row into a window of up to eight rows (CoopItem1..N, beside the list) plus one
// description line, and C++ labels them. While it is open the Options box's input handler is
// taken over: up/down move our cursor (the rows scroll when there are more items than rows),
// select chooses, and back closes the menu the game's own way. On close the originals are put
// back, since the screen instance can be reused. The hero pickers (picker.cpp) are built on it.
//
// With MenuTest=1 in fable3coop.ini, F8 (MenuTestKey) opens a 12-item test menu.
//
// Game thread only.

struct CustomMenuItem {
	std::wstring label;
	std::wstring description;  // shown under the list while the item is selected; may be empty
};

enum class CustomMenuState { Closed, Open, Chosen, Cancelled };

// Hooks the Options box's input handler and close. Call after InstallInputHooks has set the
// image base.
bool InstallCustomMenu();

// Opens a menu. False if the game is not taking menus right now or another menu owns the box.
bool OpenCustomMenu(const std::wstring& title, const std::vector<CustomMenuItem>& items, int selected = 0);

bool CustomMenuIsOpen();

// Returns Chosen or Cancelled once, after the menu closes; after Chosen, CustomMenuChoice()
// holds the chosen item's index.
CustomMenuState PollCustomMenu();
int CustomMenuChoice();

// Call once per frame on the game thread (builds and refreshes the open menu, test hotkey).
void PumpCustomMenu();
