#include "log.h"

#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

CRITICAL_SECTION g_lock;
FILE* g_file = nullptr;
wchar_t g_configPath[MAX_PATH];
uintptr_t g_imageBase = 0;

// Logs crashes (first-chance access violations and the like) with the faulting address as a
// Ghidra address and a few stack values that look like return addresses into the game.
LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* info)
{
	const DWORD code = info->ExceptionRecord->ExceptionCode;
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW
		&& code != EXCEPTION_INT_DIVIDE_BY_ZERO) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	static int logged = 0;
	if (logged++ >= 5) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	auto ghidra = [](uintptr_t va) -> uintptr_t {
		MEMORY_BASIC_INFORMATION mbi;
		if (g_imageBase && VirtualQuery(reinterpret_cast<void*>(va), &mbi, sizeof(mbi)) && reinterpret_cast<uintptr_t>(mbi.AllocationBase) == g_imageBase) {
			return va - g_imageBase + 0x00400000;
		}
		return 0;
	};
	const uintptr_t at = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
	HMODULE module = nullptr;
	char moduleName[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(at), &module)) {
		GetModuleFileNameA(module, moduleName, MAX_PATH);
	}
	const char* base = strrchr(moduleName, '\\') ? strrchr(moduleName, '\\') + 1 : moduleName;
	if (_stricmp(base, "KERNEL32.DLL") == 0 || _stricmp(base, "KERNELBASE.dll") == 0 || _stricmp(base, "ntdll.dll") == 0) {
		--logged;  // handled probes such as IsBadReadPtr
		return EXCEPTION_CONTINUE_SEARCH;
	}
	const ULONG_PTR access = info->ExceptionRecord->NumberParameters >= 2 ? info->ExceptionRecord->ExceptionInformation[1] : 0;
	Log("EXCEPTION %08lx at %p (game %08x, module %s+%x), address %p, eax %08lx ecx %08lx edx %08lx esi %08lx edi %08lx",
		code, reinterpret_cast<void*>(at), static_cast<unsigned>(ghidra(at)), strrchr(moduleName, '\\') ? strrchr(moduleName, '\\') + 1 : moduleName,
		static_cast<unsigned>(at - reinterpret_cast<uintptr_t>(module)), reinterpret_cast<void*>(access), info->ContextRecord->Eax,
		info->ContextRecord->Ecx, info->ContextRecord->Edx, info->ContextRecord->Esi, info->ContextRecord->Edi);
	auto* stack = reinterpret_cast<const uintptr_t*>(info->ContextRecord->Esp);
	char line[400];
	int len = 0;
	for (int i = 0; i < 256 && len < 360 && !IsBadReadPtr(stack + i, 4); ++i) {
		const uintptr_t value = ghidra(stack[i]);
		if (value >= 0x00401000 && value < 0x01a50000) {
			len += sprintf_s(line + len, sizeof(line) - len, " %08x", static_cast<unsigned>(value));
		}
	}
	line[len] = '\0';
	Log("  stack game addresses:%s", line);
	return EXCEPTION_CONTINUE_SEARCH;
}

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
	if (g_file) {
		g_imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		AddVectoredExceptionHandler(1, CrashLogger);
	}
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
