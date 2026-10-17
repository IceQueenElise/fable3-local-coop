#pragma once

// Diagnostics for the game's couch-join path: wraps the press-Start detector
// (FUN_00b9b0c0) and logs each button-down event it sees and why it does or
// doesn't join. Call after InstallInputHooks has set the image base.
bool InstallJoinProbe();

// Call once per frame on the game thread. When the extra pad's START has been pressed
// since the last call, offers the game's join detector a START event from that pad's user.
void PumpExtraPadJoin();
