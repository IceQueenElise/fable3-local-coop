#pragma once

#include <windows.h>
#include <xinput.h>

// The system XInput, loaded from the system directory. Calls here bypass the
// device-id remapping done by our exported functions.
namespace real {

bool Load();
DWORD GetState(DWORD user, XINPUT_STATE* state);

}  // namespace real

// GetTickCount() of the last START press (released -> pressed) seen on a physical pad, or 0.
DWORD LastStartPress(DWORD pad);

// Input-manager device ids that are not XInput user indices map to a physical pad here.
// Returns the user index unchanged when it is not remapped.
DWORD RemapUserIndex(DWORD user);
