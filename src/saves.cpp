#include "saves.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

struct XUserSigninInfoLite {  // XUSER_SIGNIN_INFO
	uint64_t xuid;
	DWORD infoFlags;
	DWORD state;
	DWORD guestNumber;
	DWORD sponsorUserIndex;
	char userName[16];
};
EnumerateFn g_originalEnumerate = nullptr;
SaveFn g_originalSave = nullptr;
AddPlayerFn g_originalAddPlayer = nullptr;
int g_saveUser = -1;  // game thread only

void* g_saveManager = nullptr;  // seen as this of the save writer

using StringCopyFn = void*(__fastcall*)(void* dst, void* edx, const void* src);  // __thiscall
using StringDestroyFn = void(__fastcall*)(void* str);
using HeroOnlySaveFn = bool(__fastcall*)(void* self, void* edx, void* name, int playerType, int user, int a);
HeroOnlySaveFn g_originalHeroOnlySave = nullptr;
// Save info read FUN_0074b4b0: void __thiscall(?, const string* name, int user, SaveInfo* info).
using SaveInfoFn = void(__fastcall*)(void* self, void* edx, const void* name, int user, uint8_t* info);

// Partner save choice for the join in progress (fable3coop.ini PartnerSave): empty keeps the
// game's choice, "new" makes a new hero, otherwise the name of a save in the partner's folder.
int g_joinUser = -1;
char g_partnerChoice[64] = "";
char g_partnerOverride[64] = "";  // from the join picker; used for the next join only

// fable3coop.ini SharedSaveFolder (default 1): player 2's heroes live in player 1's save folder
// (user 0) and are stamped with player 1's XUID, so one folder holds every hero. With 0, each
// player's saves go to their own profile's folder.
bool SharedSaveFolder()
{
	static const bool shared = GetPrivateProfileIntW(L"Fable3Coop", L"SharedSaveFolder", 1, ConfigPath()) != 0;
	return shared;
}

void* SaveManager()
{
	void* root = *reinterpret_cast<void**>(addr::Rebase(addr::kSaveRoot));
	return root ? *reinterpret_cast<void**>(static_cast<uint8_t*>(root) + addr::kSaveRootManager) : nullptr;
}

void* StringRep(const void* stringObject)
{
	return *static_cast<void* const*>(stringObject);
}

void CopyString(void* dst, const void* src)
{
	reinterpret_cast<StringCopyFn>(addr::Rebase(addr::kStringCopy))(dst, nullptr, src);
}

void AssignString(void* dst, const void* src)
{
	reinterpret_cast<StringCopyFn>(addr::Rebase(addr::kStringAssign))(dst, nullptr, src);
}

void DestroyString(void* str)
{
	reinterpret_cast<StringDestroyFn>(addr::Rebase(addr::kStringDestroy))(str);
}

// Formats a game string for the log. The character offset in the rep isn't confirmed, so
// look for a printable run of the stored length at the likely offsets.
void DescribeString(const void* stringObject, char* out, size_t size)
{
	const uint8_t* rep = stringObject ? *static_cast<const uint8_t* const*>(stringObject) : nullptr;
	if (!rep) {
		strcpy_s(out, size, "<empty>");
		return;
	}
	const int length = *reinterpret_cast<const int*>(rep + 4);
	auto printableRun = [length](const char* text) {
		if (length <= 0 || length >= 64 || IsBadReadPtr(text, static_cast<UINT_PTR>(length))) {
			return false;
		}
		for (int i = 0; i < length; ++i) {
			if (text[i] < 0x20 || text[i] >= 0x7f) {
				return false;
			}
		}
		return true;
	};
	// Inline characters, or a pointer to them, at the likely offsets.
	static const size_t kOffsets[] = {0x10, 0x14, 0x08, 0x18, 0x00};
	for (size_t offset : kOffsets) {
		const char* inlineText = reinterpret_cast<const char*>(rep + offset);
		if (printableRun(inlineText)) {
			sprintf_s(out, size, "\"%.*s\"", length, inlineText);
			return;
		}
		const char* pointed = *reinterpret_cast<const char* const*>(rep + offset);
		if (printableRun(pointed)) {
			sprintf_s(out, size, "\"%.*s\"", length, pointed);
			return;
		}
	}
	sprintf_s(out, size, "len %d, rep %p", length, rep);
}

void LogSaveState(const char* when)
{
	if (!LogEnabled()) {
		return;
	}
	char partnerGlobal[96];
	DescribeString(reinterpret_cast<void*>(addr::Rebase(addr::kPartnerSaveNameGlobal)), partnerGlobal, sizeof(partnerGlobal));
	auto* mgr = static_cast<const uint8_t*>(SaveManager());
	if (!mgr) {
		Log("save state (%s): no save manager; partner-name global %s", when, partnerGlobal);
		return;
	}
	if (g_saveManager && g_saveManager != mgr) {
		Log("save state: save manager %p differs from the one seen at save time %p", mgr, g_saveManager);
	}
	char mainName[96];
	char partnerName[96];
	DescribeString(mgr + addr::kSaveMgrMainName, mainName, sizeof(mainName));
	DescribeString(mgr + addr::kSaveMgrPartnerName, partnerName, sizeof(partnerName));
	Log("save state (%s): main %s, partner %s, partner user %d, partner-name global %s", when, mainName, partnerName,
		*reinterpret_cast<const int*>(mgr + addr::kSaveMgrPartnerUser), partnerGlobal);
}

DWORD WINAPI SaveSigninInfo(DWORD user, DWORD flags, void* info)
{
	if (user == 0 && g_saveUser > 0 && !SharedSaveFolder()) {
		user = static_cast<DWORD>(g_saveUser);
	}
	return g_signinInfo(user, flags, info);
}

const char* SaveNameText(void* const* stringObject)
{
	auto* rep = static_cast<const char* const*>(*stringObject);
	return rep && *rep ? *rep : "";
}

// Applies the partner save choice to the join's save list (a vector of game strings at
// list+8..+0xC, newest first). The join loads the first usable save, or makes a new hero when
// the list is empty, so keep only the chosen save, or nothing for "new".
void ApplyPartnerChoice(uint8_t* list)
{
	auto** begin = *reinterpret_cast<void***>(list + addr::kSaveListBegin);
	auto** end = *reinterpret_cast<void***>(list + addr::kSaveListEnd);
	for (auto** it = begin; it != end; ++it) {
		Log("saves: partner save candidate %s", SaveNameText(it));
	}
	if (!g_partnerChoice[0]) {
		return;
	}
	const bool makeNew = _stricmp(g_partnerChoice, "new") == 0;
	void** keep = nullptr;
	for (auto** it = begin; !makeNew && it != end; ++it) {
		char withSuffix[80];
		sprintf_s(withSuffix, "%sautosave", g_partnerChoice);
		const char* name = SaveNameText(it);
		if (_stricmp(name, g_partnerChoice) == 0 || _stricmp(name, withSuffix) == 0) {
			keep = it;
			break;
		}
	}
	if (!makeNew && !keep) {
		Log("saves: PartnerSave \"%s\" not found in the partner's saves; keeping the game's choice", g_partnerChoice);
		return;
	}
	void* kept = keep ? *keep : nullptr;
	for (auto** it = begin; it != end; ++it) {
		if (it != keep) {
			DestroyString(it);
		}
	}
	if (keep) {
		*begin = kept;
	}
	*reinterpret_cast<void***>(list + addr::kSaveListEnd) = begin + (keep ? 1 : 0);
	if (makeNew) {
		Log("saves: PartnerSave=new: player 2 joins with a new hero");
	}
	else {
		Log("saves: PartnerSave: player 2 joins with %s", g_partnerChoice);
	}
}

// Player 1's save folder, or "" (the game builds it the same way from user 0's XUID).
std::wstring MainSaveFolder()
{
	XUserSigninInfoLite info = {};
	if (!g_signinInfo || g_signinInfo(0, 2, &info) != ERROR_SUCCESS || info.xuid == 0) {
		return {};
	}
	PWSTR savedGames = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &savedGames))) {
		return {};
	}
	wchar_t folder[MAX_PATH];
	swprintf_s(folder, L"%s\\Lionhead Studios\\Fable 3\\%llX\\", savedGames, static_cast<unsigned long long>(info.xuid));
	CoTaskMemFree(savedGames);
	return folder;
}

// With player 2's heroes in player 1's folder, player 1's sorted save lists (Continue, Load,
// the save room) would offer them too: hide the hero-only (henchman) slots, which can't be
// loaded as a main game, and put the last save player 1 made first so Continue picks it rather
// than player 2's save written in the same second. The unsorted listing (new save names) is left
// alone so numbers are never reused.
void TidyMainPlayerList(uint8_t* list)
{
	const std::wstring folder = MainSaveFolder();
	auto** begin = *reinterpret_cast<void***>(list + addr::kSaveListBegin);
	auto** end = *reinterpret_cast<void***>(list + addr::kSaveListEnd);
	char last[64];
	wchar_t lastWide[64];
	GetPrivateProfileStringW(L"State", L"LastMainSave", L"", lastWide, 64, ConfigPath());
	WideCharToMultiByte(CP_ACP, 0, lastWide, -1, last, sizeof(last), nullptr, nullptr);
	auto** out = begin;
	for (auto** it = begin; it != end; ++it) {
		const char* name = SaveNameText(it);
		wchar_t mainSave[MAX_PATH];
		swprintf_s(mainSave, L"%s%hs_mainsave.bin", folder.c_str(), name);
		if (!folder.empty() && GetFileAttributesW(mainSave) == INVALID_FILE_ATTRIBUTES) {
			Log("saves: hiding henchman save %s from player 1's list", name);
			DestroyString(it);
			continue;
		}
		*out++ = *it;
	}
	*reinterpret_cast<void***>(list + addr::kSaveListEnd) = out;
	for (auto** it = begin; last[0] && it != out; ++it) {
		if (_stricmp(SaveNameText(it), last) == 0) {
			void* found = *it;
			for (auto** shift = it; shift != begin; --shift) {
				*shift = *(shift - 1);
			}
			*begin = found;
			break;
		}
	}
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
	if (user == g_joinUser && list) {
		ApplyPartnerChoice(static_cast<uint8_t*>(list));
	}
	else if (user == 0 && b && list && SharedSaveFolder()) {
		TidyMainPlayerList(static_cast<uint8_t*>(list));
	}
}

// The join only accepts hero-only (henchman) saves. When player 2 explicitly picked a full main
// save, mark it hero-only so its hero is loaded as player 2's henchman.
void __fastcall JoinSaveInfoDetour(void* self, void* edx, const void* name, int user, uint8_t* info)
{
	reinterpret_cast<SaveInfoFn>(addr::Rebase(addr::kReadSaveInfo))(self, edx, name, user, info);
	Log("saves: join save info for %s: usable %d, type %d, hero-only %d", SaveNameText(static_cast<void* const*>(name)),
		info[addr::kSaveInfoUsable], *reinterpret_cast<int*>(info + addr::kSaveInfoType), info[addr::kSaveInfoHeroOnly]);
	const bool explicitChoice = g_partnerChoice[0] && _stricmp(g_partnerChoice, "new") != 0;
	if (explicitChoice && !info[addr::kSaveInfoUsable]) {
		info[addr::kSaveInfoUsable] = 1;
		info[addr::kSaveInfoHeroOnly] = 1;
		*reinterpret_cast<int*>(info + addr::kSaveInfoType) = 2;
		Log("saves: %s is a full save; loading only its hero for player 2", g_partnerChoice);
	}
}

// The join lists the joining user's saves, then reads and loads the chosen one by name, all
// inside the add-player routine; keep the override for all of it.
//
// The join also allocates a new save name for the partner but stores it as the main save
// name, so the host's saves move to a new slot and the couch partner save (FUN_00752e80,
// which needs the partner name) never runs. Move it to the partner name and give the host
// their name back.
void* __cdecl AddPlayerDetour(int user, int a, int b, void* c, void* d)
{
	const bool partner = user > 0 && user < XUSER_MAX_COUNT_FABLE;
	if (!partner) {
		return g_originalAddPlayer(user, a, b, c, d);
	}
	const int previous = g_saveUser;
	g_saveUser = user;
	if (g_partnerOverride[0]) {
		strcpy_s(g_partnerChoice, g_partnerOverride);
		g_partnerOverride[0] = '\0';
	}
	else {
		wchar_t choice[64];
		GetPrivateProfileStringW(L"Fable3Coop", L"PartnerSave", L"", choice, 64, ConfigPath());
		WideCharToMultiByte(CP_ACP, 0, choice, -1, g_partnerChoice, sizeof(g_partnerChoice), nullptr, nullptr);
	}
	g_joinUser = user;
	Log("saves: adding local player for user %d (saves from that user's folder), PartnerSave \"%s\"", user, g_partnerChoice);
	LogSaveState("before join");

	uint8_t* mgr = static_cast<uint8_t*>(SaveManager());
	void* hostName = nullptr;  // a game string object: one pointer
	if (mgr) {
		CopyString(&hostName, mgr + addr::kSaveMgrMainName);
	}
	void* hero = g_originalAddPlayer(user, a, b, c, d);
	g_saveUser = previous;
	g_joinUser = -1;

	if (mgr && StringRep(mgr + addr::kSaveMgrMainName) != StringRep(&hostName)) {
		AssignString(mgr + addr::kSaveMgrPartnerName, mgr + addr::kSaveMgrMainName);
		AssignString(mgr + addr::kSaveMgrMainName, &hostName);
		Log("saves: join put the partner's new save name in the main slot; moved it to the partner name");
	}
	if (mgr) {
		DestroyString(&hostName);
	}
	LogSaveState("after join");
	return hero;
}

// The couch partner's hero-only save (FUN_00752e80 -> here) gets the partner's user index but
// builds its path from user 0's XUID; point it at the partner's folder.
bool __fastcall HeroOnlySaveDetour(void* self, void* edx, void* name, int playerType, int user, int a)
{
	char saveName[96];
	DescribeString(name, saveName, sizeof(saveName));
	Log("saves: hero-only save of player type %d (user %d) as %s", playerType, user, saveName);
	const int previous = g_saveUser;
	if (user > 0 && user < XUSER_MAX_COUNT_FABLE) {
		g_saveUser = user;
	}
	const bool ok = g_originalHeroOnlySave(self, edx, name, playerType, user, a);
	g_saveUser = previous;
	Log("saves: hero-only save %s", ok ? "succeeded" : "failed");
	return ok;
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
	g_saveManager = self;
	char saveName[96];
	DescribeString(name, saveName, sizeof(saveName));
	if (playerType == 1) {
		// Remember player 1's own last save for Continue (see TidyMainPlayerList).
		const char* text = SaveNameText(static_cast<void* const*>(name));
		if (text[0]) {
			wchar_t wide[64];
			MultiByteToWideChar(CP_ACP, 0, text, -1, wide, 64);
			WritePrivateProfileStringW(L"State", L"LastMainSave", wide, ConfigPath());
		}
	}
	Log("saves: saving player type %d as %s", playerType, saveName);
	LogSaveState("at save");
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

void SetPartnerChoiceOverride(const char* choice)
{
	strcpy_s(g_partnerOverride, choice ? choice : "");
}

bool PartnerSavesInMainFolder()
{
	return SharedSaveFolder();
}

std::string LoadedMainSaveName()
{
	auto* mgr = static_cast<uint8_t*>(SaveManager());
	return mgr ? SaveNameText(reinterpret_cast<void* const*>(mgr + addr::kSaveMgrMainName)) : "";
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
	g_originalHeroOnlySave = reinterpret_cast<HeroOnlySaveFn>(addr::Rebase(addr::kHeroOnlySave));

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
	for (uintptr_t site : addr::kHeroOnlySaveCalls) {
		patched += RedirectCall(site, addr::kHeroOnlySave, reinterpret_cast<void*>(&HeroOnlySaveDetour));
		++expected;
	}
	patched += RedirectCall(addr::kJoinReadSaveInfoCall, addr::kReadSaveInfo, reinterpret_cast<void*>(&JoinSaveInfoDetour));
	++expected;
	Log("saves: %d of %d call sites patched", patched, expected);
	return patched == expected;
}
