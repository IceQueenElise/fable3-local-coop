#pragma once

#include <string>

// Access to the elements of a loaded Gameface GUI screen (findings R37). Element paths are given
// from the screen's layer, as the native lookup takes them ("Options.Sections.Options.Title");
// Lua run with RunGuiLua needs them from the scene ("Scene.Layer.Options...").
//
// Game thread only.

// The loaded instance of GUI screen id (e.g. 0x11 Options), or null.
void* FindGuiScreen(int id);

// The element at path, or null.
void* FindGuiElement(void* screen, const char* path);

// Sets the text of the text element at path. False when it is missing or not a text element.
bool SetGuiText(void* screen, const char* path, const wchar_t* text);

// Runs a Gameface Lua chunk on the screen (it sees getElement, setAttribute, clone, output, ...).
void RunGuiLua(void* screen, const std::string& script);
