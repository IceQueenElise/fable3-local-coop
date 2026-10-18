#include "log.h"

#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdio>

namespace {

CRITICAL_SECTION g_lock;
FILE* g_file = nullptr;
wchar_t g_configPath[MAX_PATH];

}  // namespace

void LogInit(void* module)
{
	InitializeCriticalSection(&g_lock);
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(static_cast<HMODULE>(module), path, MAX_PATH);
	while (n > 0 && path[n - 1] != L'\\') {
		--n;
	}
	wcscpy_s(path + n, MAX_PATH - n, L"fable3coop.ini");
	wcscpy_s(g_configPath, path);
	if (GetPrivateProfileIntW(L"Fable3Coop", L"Log", 1, path) == 0) {
		return;
	}
	wcscpy_s(path + n, MAX_PATH - n, L"fable3coop.log");
	// Shared read so the log can be tailed while the game runs.
	g_file = _wfsopen(path, L"w", _SH_DENYWR);
}

const wchar_t* ConfigPath()
{
	return g_configPath;
}

bool LogEnabled()
{
	return g_file != nullptr;
}

void Log(const char* fmt, ...)
{
	if (!g_file) {
		return;
	}
	SYSTEMTIME t;
	GetLocalTime(&t);
	EnterCriticalSection(&g_lock);
	fprintf(g_file, "%02d:%02d:%02d.%03d [%5lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId());
	va_list args;
	va_start(args, fmt);
	vfprintf(g_file, fmt, args);
	va_end(args);
	fputc('\n', g_file);
	fflush(g_file);
	LeaveCriticalSection(&g_lock);
}
