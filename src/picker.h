#pragma once

// Hero pickers, built as custom menus (custommenu.h) on the Options menu's art.
//
// Player 2's START (before joining) opens a list of every hero player 2 can join as, newest
// first, and "New hero". Hero-only (henchman) slots are tagged; names come from the [Heroes]
// section of fable3coop.ini when set (e.g. hero5autosave=Bob). The chosen save is handed to the
// join (SetPartnerChoiceOverride).
//
// Call these on the game thread.

enum class PickerState { Closed, Open, Chosen, Cancelled };

// Opens the picker. False if the menu could not be requested.
bool OpenPartnerPicker();

// Player 1's Continue picker (title screen): main saves player 1 can load, last played first.
// Choosing one makes Continue load it (LastMainSave and the game's initial save name).
bool OpenMainPicker();

// Whether the picker last opened is player 2's join picker.
bool PickerIsForPlayerTwo();

// Returns Chosen or Cancelled once, after the menu closes; after Chosen, PickedPartnerSave()
// holds the save name, or "new" for a new hero.
PickerState PollPartnerPicker();

const char* PickedPartnerSave();
