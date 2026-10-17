#pragma once

// Per-player input routing.
//
// The game's input ingest (FUN_00d89de0) groups each frame's events by user, but the PC
// build first overwrites every event's user with 0 so keyboard and mouse (device ids 1, 2)
// drive player 1. This patch keeps that for devices 0-2 and leaves the extra pad's events
// (device id kExtraDeviceId) as that user, so the per-user control bindings route them to
// player 2. Call after InstallInputHooks has set the image base.
bool InstallInputRouting();
