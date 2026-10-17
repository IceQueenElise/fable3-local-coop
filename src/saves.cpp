#include "saves.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "addresses.h"
#include "log.h"

namespace {

constexpr int XUSER_MAX_COUNT_FABLE = 4;

using SigninInfoFn = DWORD(WINAPI*)(DWORD user, DWORD flags, void* info);
// __thiscall(save manager, int user, void* list, unsigned, char), RET 0x10
using EnumerateFn = void(__fastcall*)(void* self, void* edx, int user, void* list, unsigned a, char b);
// __thiscall(save manager, void* name, int playerType, int, char), RET 0x10
using SaveFn = void(__fastcall*)(void* self, void* edx, void* name, int playerType, int a, char b);
using GetPlayerFn = void*(__fastcall*)(void* players, void* edx);
// Add-local-player FUN_00bbfa10: void* __cdecl(int user, int, char, void*, void*).
using AddPlayerFn = void*(__cdecl*)(int user, int a, int b, void* c, void* d);

SigninInfoFn g_signinInfo = nullptr;
EnumerateFn g_originalEnumerate = nullptr;
SaveFn g_originalSave = nullptr;
AddPlayerFn g_originalAddPlayer = nullptr;
int g_saveUser = -1;  // game thread only

DWORD WINAPI SaveSigninInfo(DWORD user, DWORD flags, void* info)
{
	if (user == 0 && g_saveUser > 0) {
		user = static_cast<DWORD>(g_saveUser);
	}
	return g_signinInfo(user, flags, info);
}

void __fastcall EnumerateDetour(void* self, void* edx, int user, void* list, unsigned a, char b)
{
	const int previous = g_saveUser;
	g_saveUser = user;
	if (user != 0) {
		Log("saves: listing saves for user %d", user);
	}
	g_originalEnumerate(self, edx, user, list, a, b);
	g_saveUser = previous;
}

// The join lists the joining user's saves, then reads and loads the chosen one by name, all
// inside the add-player routine; keep the override for all of it.
void* __cdecl AddPlayerDetour(int user, int a, int b, void* c, void* d)
{
	const int previous = g_saveUser;
	if (user > 0 && user < XUSER_MAX_COUNT_FABLE) {
		g_saveUser = user;
		Log("saves: adding local player for user %d (saves from that user's folder)", user);
	}
	void* hero = g_originalAddPlayer(user, a, b, c, d);
	g_saveUser = previous;
	return hero;
}

int UserOfPlayerType(int playerType)
{
	if (playerType != 2) {
		return 0;
	}
	void* game = *reinterpret_cast<void**>(addr::Rebase(addr::kGameRoot));
	void* inner = game ? *reinterpret_cast<void**>(static_cast<uint8_t*>(game) + 0x0c) : nullptr;
	void* players = inner ? *reinterpret_cast<void**>(static_cast<uint8_t*>(inner) + 0x40) : nullptr;
	void* couch = players ? reinterpret_cast<GetPlayerFn>(addr::Rebase(addr::kCouchPlayer))(players, nullptr) : nullptr;
	return couch ? *reinterpret_cast<int*>(static_cast<uint8_t*>(couch) + addr::kPlayerUser) : 0;
}

void __fastcall SaveDetour(void* self, void* edx, void* name, int playerType, int a, char b)
{
	const int previous = g_saveUser;
	g_saveUser = UserOfPlayerType(playerType);
	if (g_saveUser != 0) {
		Log("saves: saving player type %d into user %d's folder", playerType, g_saveUser);
	}
	g_originalSave(self, edx, name, playerType, a, b);
	g_saveUser = previous;
}

// Repoints a "CALL rel32" at siteVa from expectedVa to target.
bool RedirectCall(uintptr_t siteVa, uintptr_t expectedVa, void* target)
{
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(siteVa));
	int32_t rel;
	memcpy(&rel, site + 1, sizeof(rel));
	if (site[0] != 0xe8 || reinterpret_cast<uintptr_t>(site) + 5 + rel != addr::Rebase(expectedVa)) {
		Log("saves: unexpected call at %p; not patched", site);
		return false;
	}
	rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
	DWORD oldProtect;
	if (!VirtualProtect(site + 1, sizeof(rel), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}
	memcpy(site + 1, &rel, sizeof(rel));
	VirtualProtect(site + 1, sizeof(rel), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	return true;
}

}  // namespace

int CurrentSaveUser()
{
	return g_saveUser > 0 ? g_saveUser : 0;
}

bool InstallSaveHooks()
{
	g_signinInfo = reinterpret_cast<SigninInfoFn>(GetProcAddress(GetModuleHandleW(L"xlive.dll"), MAKEINTRESOURCEA(5267)));
	if (!g_signinInfo) {
		Log("saves: xlive XUserGetSigninInfo not found; save hooks not installed");
		return false;
	}
	g_originalEnumerate = reinterpret_cast<EnumerateFn>(addr::Rebase(addr::kEnumerateSaves));
	g_originalSave = reinterpret_cast<SaveFn>(addr::Rebase(addr::kSaveForPlayer));
	g_originalAddPlayer = reinterpret_cast<AddPlayerFn>(addr::Rebase(addr::kAddLocalPlayer));

	int patched = 0;
	int expected = 0;
	for (uintptr_t site : addr::kSaveSigninInfoCalls) {
		patched += RedirectCall(site, addr::kXUserGetSigninInfoThunk, reinterpret_cast<void*>(&SaveSigninInfo));
		++expected;
	}
	for (uintptr_t site : addr::kEnumerateSavesCalls) {
		patched += RedirectCall(site, addr::kEnumerateSaves, reinterpret_cast<void*>(&EnumerateDetour));
		++expected;
	}
	for (uintptr_t site : addr::kSaveForPlayerCalls) {
		patched += RedirectCall(site, addr::kSaveForPlayer, reinterpret_cast<void*>(&SaveDetour));
		++expected;
	}
	for (uintptr_t site : addr::kAddLocalPlayerCalls) {
		patched += RedirectCall(site, addr::kAddLocalPlayer, reinterpret_cast<void*>(&AddPlayerDetour));
		++expected;
	}
	Log("saves: %d of %d call sites patched", patched, expected);
	return patched == expected;
}
