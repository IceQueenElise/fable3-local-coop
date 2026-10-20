#pragma once

// Debug aid for building our own menus on the game's GUI (findings R37). With LuaProbe=1 in
// fable3coop.ini, a file luaprobe.lua next to the DLL is run as Gameface Lua on a loaded GUI
// screen (LuaProbeScreen, default 0x11 = Options), then renamed to luaprobe.done. F9 in game
// (LuaProbeKey, a virtual-key code) runs luaprobe.done again. Lines in it
// of the form
//   --check <element path>           log whether the native lookup finds the element
//   --settext <element path>|<text>  set its text from C++
// run from C++ a moment later (the game may queue the chunk), with paths as the native lookup
// takes them ("Options.Sections...", from the screen's layer).
//
// Call PumpLuaProbe once per frame on the game thread.
void PumpLuaProbe();
