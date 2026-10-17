#pragma once

// Appends a timestamped line to fable3coop.log next to the DLL. Thread-safe.
void LogInit(void* module);
void Log(const char* fmt, ...);
