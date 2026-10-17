#include "saveio.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cwchar>

#include "log.h"
#include "saves.h"

namespace {

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using DeleteFileWFn = BOOL(WINAPI*)(LPCWSTR);
using DeleteFileAFn = BOOL(WINAPI*)(LPCSTR);
using MoveFileAFn = BOOL(WINAPI*)(LPCSTR, LPCSTR);
using CopyFileWFn = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, BOOL);
using CreateDirectoryWFn = BOOL(WINAPI*)(LPCWSTR, LPSECURITY_ATTRIBUTES);
using CreateDirectoryAFn = BOOL(WINAPI*)(LPCSTR, LPSECURITY_ATTRIBUTES);
using FindFirstFileWFn = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using WriteFileFn = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);

CreateFileWFn g_createFileW;
CreateFileAFn g_createFileA;
DeleteFileWFn g_deleteFileW;
DeleteFileAFn g_deleteFileA;
MoveFileAFn g_moveFileA;
CopyFileWFn g_copyFileW;
CreateDirectoryWFn g_createDirectoryW;
CreateDirectoryAFn g_createDirectoryA;
FindFirstFileWFn g_findFirstFileW;
ReadFileFn g_readFile;
WriteFileFn g_writeFile;
CloseHandleFn g_closeHandle;

// Open save-folder handles and their byte totals.
struct Tracked {
	HANDLE handle;
	wchar_t path[MAX_PATH];
	unsigned long long read;
	unsigned long long written;
};
constexpr int kMaxTracked = 64;
Tracked g_tracked[kMaxTracked];
SRWLOCK g_lock = SRWLOCK_INIT;

bool IsSavePath(const wchar_t* path)
{
	return path && wcsstr(path, L"Lionhead Studios\\Fable 3") != nullptr;
}

bool IsSavePathA(const char* path)
{
	return path && strstr(path, "Lionhead Studios\\Fable 3") != nullptr;
}

void Widen(const char* in, wchar_t* out, size_t count)
{
	MultiByteToWideChar(CP_ACP, 0, in, -1, out, static_cast<int>(count));
}

const char* AccessName(DWORD access, DWORD disposition)
{
	const bool write = (access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
	if (!write) {
		return "read";
	}
	return disposition == CREATE_ALWAYS || disposition == CREATE_NEW || disposition == TRUNCATE_EXISTING ? "write(new)"
																										   : "write";
}

void Track(HANDLE h, const wchar_t* path)
{
	AcquireSRWLockExclusive(&g_lock);
	for (Tracked& t : g_tracked) {
		if (!t.handle) {
			t.handle = h;
			wcsncpy_s(t.path, path, _TRUNCATE);
			t.read = t.written = 0;
			break;
		}
	}
	ReleaseSRWLockExclusive(&g_lock);
}

void AddBytes(HANDLE h, DWORD bytes, bool write)
{
	AcquireSRWLockShared(&g_lock);
	for (Tracked& t : g_tracked) {
		if (t.handle == h) {
			// Totals are only approximate under concurrent I/O on one handle; fine for diagnostics.
			(write ? t.written : t.read) += bytes;
			break;
		}
	}
	ReleaseSRWLockShared(&g_lock);
}

void LogOpen(const wchar_t* path, DWORD access, DWORD disposition, HANDLE h)
{
	Log("save io: open %s user %d %ls -> %s", AccessName(access, disposition), CurrentSaveUser(), path,
		h == INVALID_HANDLE_VALUE ? "failed" : "ok");
}

HANDLE WINAPI CreateFileWDetour(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
	DWORD flags, HANDLE templ)
{
	HANDLE h = g_createFileW(path, access, share, sa, disposition, flags, templ);
	if (IsSavePath(path)) {
		LogOpen(path, access, disposition, h);
		if (h != INVALID_HANDLE_VALUE) {
			Track(h, path);
		}
	}
	return h;
}

HANDLE WINAPI CreateFileADetour(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
	DWORD flags, HANDLE templ)
{
	HANDLE h = g_createFileA(path, access, share, sa, disposition, flags, templ);
	if (IsSavePathA(path)) {
		wchar_t wide[MAX_PATH];
		Widen(path, wide, MAX_PATH);
		LogOpen(wide, access, disposition, h);
		if (h != INVALID_HANDLE_VALUE) {
			Track(h, wide);
		}
	}
	return h;
}

BOOL WINAPI DeleteFileWDetour(LPCWSTR path)
{
	BOOL ok = g_deleteFileW(path);
	if (IsSavePath(path)) {
		Log("save io: delete user %d %ls -> %d", CurrentSaveUser(), path, ok);
	}
	return ok;
}

BOOL WINAPI DeleteFileADetour(LPCSTR path)
{
	BOOL ok = g_deleteFileA(path);
	if (IsSavePathA(path)) {
		Log("save io: delete user %d %s -> %d", CurrentSaveUser(), path, ok);
	}
	return ok;
}

BOOL WINAPI MoveFileADetour(LPCSTR from, LPCSTR to)
{
	BOOL ok = g_moveFileA(from, to);
	if (IsSavePathA(from) || IsSavePathA(to)) {
		Log("save io: move user %d %s -> %s : %d", CurrentSaveUser(), from, to, ok);
	}
	return ok;
}

BOOL WINAPI CopyFileWDetour(LPCWSTR from, LPCWSTR to, BOOL failIfExists)
{
	BOOL ok = g_copyFileW(from, to, failIfExists);
	if (IsSavePath(from) || IsSavePath(to)) {
		Log("save io: copy user %d %ls -> %ls : %d", CurrentSaveUser(), from, to, ok);
	}
	return ok;
}

BOOL WINAPI CreateDirectoryWDetour(LPCWSTR path, LPSECURITY_ATTRIBUTES sa)
{
	BOOL ok = g_createDirectoryW(path, sa);
	if (IsSavePath(path)) {
		Log("save io: mkdir user %d %ls -> %d", CurrentSaveUser(), path, ok);
	}
	return ok;
}

BOOL WINAPI CreateDirectoryADetour(LPCSTR path, LPSECURITY_ATTRIBUTES sa)
{
	BOOL ok = g_createDirectoryA(path, sa);
	if (IsSavePathA(path)) {
		Log("save io: mkdir user %d %s -> %d", CurrentSaveUser(), path, ok);
	}
	return ok;
}

HANDLE WINAPI FindFirstFileWDetour(LPCWSTR pattern, LPWIN32_FIND_DATAW data)
{
	HANDLE h = g_findFirstFileW(pattern, data);
	if (IsSavePath(pattern)) {
		Log("save io: list user %d %ls -> %s", CurrentSaveUser(), pattern, h == INVALID_HANDLE_VALUE ? "none" : "found");
	}
	return h;
}

BOOL WINAPI ReadFileDetour(HANDLE h, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED ov)
{
	BOOL ok = g_readFile(h, buffer, size, read, ov);
	if (ok && read) {
		AddBytes(h, *read, false);
	}
	return ok;
}

BOOL WINAPI WriteFileDetour(HANDLE h, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED ov)
{
	BOOL ok = g_writeFile(h, buffer, size, written, ov);
	if (ok && written) {
		AddBytes(h, *written, true);
	}
	return ok;
}

BOOL WINAPI CloseHandleDetour(HANDLE h)
{
	Tracked closed = {};
	AcquireSRWLockExclusive(&g_lock);
	for (Tracked& t : g_tracked) {
		if (t.handle == h) {
			closed = t;
			t.handle = nullptr;
			break;
		}
	}
	ReleaseSRWLockExclusive(&g_lock);
	if (closed.handle) {
		Log("save io: close %ls (read %llu, wrote %llu)", closed.path, closed.read, closed.written);
	}
	return g_closeHandle(h);
}

// Replaces kernel32!name in Fable3.exe's import address table; returns the original.
void* PatchImport(const char* name, void* detour)
{
	auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
	auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
	auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
		if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), "KERNEL32.dll") != 0) {
			continue;
		}
		auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
		auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
		for (; names->u1.AddressOfData; ++names, ++iat) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
				continue;
			}
			auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
			if (strcmp(reinterpret_cast<const char*>(byName->Name), name) != 0) {
				continue;
			}
			void* original = reinterpret_cast<void*>(iat->u1.Function);
			DWORD oldProtect;
			VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &oldProtect);
			iat->u1.Function = reinterpret_cast<uintptr_t>(detour);
			VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), oldProtect, &oldProtect);
			return original;
		}
	}
	Log("save io: import %s not found", name);
	return nullptr;
}

template <typename Fn>
bool Hook(const char* name, Fn detour, Fn& original)
{
	original = reinterpret_cast<Fn>(PatchImport(name, reinterpret_cast<void*>(detour)));
	return original != nullptr;
}

}  // namespace

bool InstallSaveFileLog()
{
	if (!LogEnabled()) {
		return false;
	}
	bool ok = Hook("CreateFileW", &CreateFileWDetour, g_createFileW);
	ok &= Hook("CreateFileA", &CreateFileADetour, g_createFileA);
	ok &= Hook("DeleteFileW", &DeleteFileWDetour, g_deleteFileW);
	ok &= Hook("DeleteFileA", &DeleteFileADetour, g_deleteFileA);
	ok &= Hook("MoveFileA", &MoveFileADetour, g_moveFileA);
	ok &= Hook("CopyFileW", &CopyFileWDetour, g_copyFileW);
	ok &= Hook("CreateDirectoryW", &CreateDirectoryWDetour, g_createDirectoryW);
	ok &= Hook("CreateDirectoryA", &CreateDirectoryADetour, g_createDirectoryA);
	ok &= Hook("FindFirstFileW", &FindFirstFileWDetour, g_findFirstFileW);
	ok &= Hook("ReadFile", &ReadFileDetour, g_readFile);
	ok &= Hook("WriteFile", &WriteFileDetour, g_writeFile);
	ok &= Hook("CloseHandle", &CloseHandleDetour, g_closeHandle);
	Log("save io: file logging %s", ok ? "installed" : "partly installed");
	return ok;
}
