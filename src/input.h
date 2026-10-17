#pragma once

#include <windows.h>

// Adds a second NInput::CJoystickXBox360 to the game's input manager.
//
// The game creates one XInput device with id 0 and only polls pad 0. Device
// ids 1 and 2 belong to the keyboard and mouse, so the extra joystick uses
// kExtraDeviceId and our XInput exports map that id to physical pad kExtraPad.
constexpr DWORD kExtraDeviceId = 3;
constexpr DWORD kExtraPad = 1;

// Verifies the exe build and installs the hot-plug hook. Call from DllMain
// before the game's code runs; returns false (and changes nothing) on mismatch.
bool InstallInputHooks();
