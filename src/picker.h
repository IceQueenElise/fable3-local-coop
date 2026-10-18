#pragma once

// Join-time hero picker for player 2.
//
// Player 2's START (before joining) opens the game's Options menu relabelled as a hero picker:
// up to five of player 2's saves, newest first, and "New hero". Hero-only (henchman) slots are
// tagged; names come from the [Heroes] section of fable3coop.ini when set (e.g.
// hero5autosave=Bob). The chosen save is handed to the join (SetPartnerChoiceOverride).
//
// Call these on the game thread.

enum class PickerState { Closed, Open, Chosen, Cancelled };

// Installs the Options menu hooks (text lookup, selection, close). Call after
// InstallInputHooks has set the image base.
bool InstallPicker();

// Opens the picker. False if the menu could not be requested.
bool OpenPartnerPicker();

// Returns Chosen or Cancelled once, after the menu closes; after Chosen, PickedPartnerSave()
// holds the save name, or "new" for a new hero.
PickerState PollPartnerPicker();

const char* PickedPartnerSave();
