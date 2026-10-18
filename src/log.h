#pragma once

// Appends a timestamped line to fable3coop.log next to the DLL. Thread-safe.
// Logging is on unless fable3coop.ini next to the DLL has "Log=0" under [Fable3Coop].
void LogInit(void* module);
void Log(const char* fmt, ...);

// False when logging is off; use to skip work done only to build log lines.
bool LogEnabled();

// Full path of fable3coop.ini next to the DLL (set by LogInit).
const wchar_t* ConfigPath();
