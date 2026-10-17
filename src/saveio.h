#pragma once

// Diagnostics: logs every file operation Fable3.exe makes in its save folders
// ("...\Lionhead Studios\Fable 3\..."): opens with access, enumerations, deletes, moves,
// copies, and per-handle byte totals on close, each tagged with the current save user.
// Installed only while logging is on (it hooks ReadFile/WriteFile/CloseHandle in the exe's
// import table). Call after InstallSaveHooks.
bool InstallSaveFileLog();
