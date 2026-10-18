#include "picker.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "addresses.h"
#include "input.h"
#include "log.h"
#include "proxy.h"
#include "saves.h"

// The PC build ships no layout for the game's generic menu box (MenuBox.bgf, GUI screen 5), so
// the picker borrows the Options menu (NUI::COptionsMenuBox on Options.bgf): while the picker is
// open, its title, entry labels and descriptions are replaced through the text lookup, and
// selecting a main-page entry picks a hero instead of running the entry's action.

namespace {

using GetPlayerFn = void*(__fastcall*)(void* players, void* edx);
using OpenOptionsFn = void(__fastcall*)(void* boxList, void* edx, void* hero, int inGame);  // __thiscall, RET 8
using SelectFn = void(__fastcall*)(void* box, void* edx, int player);                      // __thiscall, RET 4
using CloseFn = void(__fastcall*)(void* box);
using TextLookupFn = void*(__fastcall*)(void* db, void* edx, void* out, const uint32_t* key);  // __thiscall, RET 8
using TagHashFn = uint32_t*(__fastcall*)(uint32_t* out, void* edx, void* const* tag);
using StringFromTextFn = void*(__fastcall*)(void* str, void* edx, const char* text, int length);
using WideAssignFn = void*(__fastcall*)(void* str, void* edx, const wchar_t* text, int length);
using StringDestroyFn = void(__fastcall*)(void* str);

struct Slot {
	std::string name;    // e.g. "hero5autosave"
	std::wstring label;  // entry text
	std::wstring detail; // description text
	FILETIME saved;
};

// Main page entries, in the box's own numbering ([box+0xCC] = 1..6), by the text tag each shows.
// Entry 4 (LIVE) is drawn without a text tag, so it can't be relabelled and stays unused.
const char* const kEntryTags[] = {"TEXT_GUI_SAVE_MANAGEMENT", "TEXT_GUI_GAME_SETTINGS", "TEXT_GUI_NEW_GAME",
	"TEXT_GUI_LIVE_DASHBOARD", "TEXT_GUI_LIVE_MARKETPLACE", "TEXT_GUI_EXIT_TO_WINDOWS"};
constexpr int kEntryCount = sizeof(kEntryTags) / sizeof(kEntryTags[0]);
constexpr int kHeroEntries[] = {1, 2, 3, 5};  // entries that show heroes, in order
constexpr int kMoreEntry = 5;                 // with more heroes than fit: next page
constexpr int kNewHeroEntry = 6;

struct Replacement {
	uint32_t key;
	std::wstring text;
};

bool g_active = false;          // picker owns the Options menu
bool g_menuSeen = false;        // the menu has asked for the picker's text
DWORD g_requestedAt = 0;
int g_page = 0;                 // page of heroes shown (when they don't all fit)
bool g_reopen = false;          // "More heroes" chosen: reopen on the next page
DWORD g_reopenUntil = 0;
std::vector<std::string> g_entryNames;  // per main page entry: save name, "new", or "" (unused)
std::vector<Replacement> g_texts;
PickerState g_result = PickerState::Closed;
std::string g_picked;
uint8_t* g_lookupTrampoline = nullptr;
CloseFn g_originalClose = nullptr;

uint32_t TagKey(const char* tag)
{
	void* str = nullptr;
	reinterpret_cast<StringFromTextFn>(addr::Rebase(addr::kStringFromText))(&str, nullptr, tag, -1);
	uint32_t key = 0;
	reinterpret_cast<TagHashFn>(addr::Rebase(addr::kTextTagHash))(&key, nullptr, &str);
	reinterpret_cast<StringDestroyFn>(addr::Rebase(addr::kStringDestroy))(&str);
	return key;
}

void AddText(const char* tag, const std::wstring& text)
{
	g_texts.push_back({TagKey(tag), text});
}

std::wstring Widen(const char* text)
{
	wchar_t wide[128];
	MultiByteToWideChar(CP_ACP, 0, text, -1, wide, 128);
	return wide;
}

// Player 2's save folder: <Saved Games>\Lionhead Studios\Fable 3\<XUID>\.
std::wstring PartnerSaveFolder()
{
	struct XUserSigninInfo {
		uint64_t xuid;
		DWORD infoFlags;
		DWORD state;
		DWORD guestNumber;
		DWORD sponsorUserIndex;
		char userName[16];
	};
	using SigninInfoFn = DWORD(WINAPI*)(DWORD user, DWORD flags, XUserSigninInfo* info);
	static auto signinInfo = reinterpret_cast<SigninInfoFn>(GetProcAddress(GetModuleHandleW(L"xlive.dll"), MAKEINTRESOURCEA(5267)));
	XUserSigninInfo info = {};
	const DWORD user = PartnerSavesInMainFolder() ? 0 : kExtraDeviceId;
	if (!signinInfo || signinInfo(user, 2, &info) != ERROR_SUCCESS || info.xuid == 0) {
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

// Every slot with a hero in player 2's folder, newest first.
std::vector<Slot> ListPartnerSlots()
{
	std::vector<Slot> slots;
	const std::wstring folder = PartnerSaveFolder();
	if (folder.empty()) {
		Log("picker: player 2's save folder not found");
		return slots;
	}
	// The loaded main save is player 1's hero: never offer it to player 2.
	const std::string loaded = LoadedMainSaveName();
	WIN32_FIND_DATAW found;
	HANDLE find = FindFirstFileW((folder + L"*_herosave.bin").c_str(), &found);
	if (find == INVALID_HANDLE_VALUE) {
		return slots;
	}
	do {
		const std::wstring file = found.cFileName;
		const std::wstring name = file.substr(0, file.size() - wcslen(L"_herosave.bin"));
		char nameText[64];
		WideCharToMultiByte(CP_ACP, 0, name.c_str(), -1, nameText, sizeof(nameText), nullptr, nullptr);
		if (!loaded.empty() && _stricmp(nameText, loaded.c_str()) == 0) {
			Log("picker: %s is the loaded main save; not offered", nameText);
			continue;
		}
		const bool henchman = GetFileAttributesW((folder + name + L"_mainsave.bin").c_str()) == INVALID_FILE_ATTRIBUTES;
		Slot slot;
		char narrow[64];
		WideCharToMultiByte(CP_ACP, 0, name.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
		slot.name = narrow;
		slot.saved = found.ftLastWriteTime;

		wchar_t nickname[64];
		GetPrivateProfileStringW(L"Heroes", name.c_str(), L"", nickname, 64, ConfigPath());
		if (nickname[0]) {
			slot.label = nickname;
		}
		else {
			slot.label = name;
			const size_t suffix = slot.label.find(L"autosave");
			if (suffix != std::wstring::npos) {
				slot.label.erase(suffix);
			}
		}
		SYSTEMTIME utc, local;
		FileTimeToSystemTime(&found.ftLastWriteTime, &utc);
		SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
		static const wchar_t* const kMonths[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
		wchar_t detail[128];
		swprintf_s(detail, L"%s - last played %s %d. Join as this hero.", henchman ? L"Henchman" : L"Hero from a main save",
			kMonths[(local.wMonth + 11) % 12], local.wDay);
		slot.detail = detail;
		if (henchman) {
			slot.label += L" (Henchman)";
		}
		slots.push_back(slot);
	} while (FindNextFileW(find, &found));
	FindClose(find);
	std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) { return CompareFileTime(&a.saved, &b.saved) > 0; });
	return slots;
}

void* MainHero()
{
	auto* game = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGameRoot));
	auto* inner = game ? *reinterpret_cast<uint8_t**>(game + 0x0c) : nullptr;
	void* players = inner ? *reinterpret_cast<void**>(inner + 0x40) : nullptr;
	auto* player = players ? static_cast<uint8_t*>(reinterpret_cast<GetPlayerFn>(addr::Rebase(addr::kMainPlayer))(players, nullptr)) : nullptr;
	auto** tracked = player ? *reinterpret_cast<void***>(player + addr::kPlayerHero) : nullptr;
	return tracked ? *tracked : nullptr;
}

void Finish(PickerState result)
{
	g_active = false;
	g_texts.clear();
	g_result = result;
	SetPickerPadSwap(false);
}

// Text lookup wrapper: while the picker is open, the Options menu's title and main page entries
// show the picker's text instead.
void* __fastcall TextLookupDetour(void* db, void* edx, void* out, const uint32_t* key)
{
	void* result = reinterpret_cast<TextLookupFn>(g_lookupTrampoline)(db, edx, out, key);
	if (g_active && key && out) {
		bool replaced = false;
		for (const Replacement& replacement : g_texts) {
			if (replacement.key == *key) {
				reinterpret_cast<WideAssignFn>(addr::Rebase(addr::kWideStringAssign))(out, nullptr, replacement.text.c_str(), -1);
				replaced = true;
				g_menuSeen = true;
				break;
			}
		}
		static int logged = 0;
		if (logged < 60) {
			++logged;
			Log("picker: text key %08x%s", *key, replaced ? " (replaced)" : "");
		}
	}
	return result;
}

// Main page selection of the Options menu: pick a hero instead of running the entry's action.
void __fastcall SelectDetour(void* box, void* edx, int player)
{
	auto* fields = static_cast<uint8_t*>(box);
	const int page = *reinterpret_cast<int*>(fields + addr::kOptionsPage);
	const int entry = *reinterpret_cast<int*>(fields + addr::kOptionsEntry);
	if (!g_active || (page != addr::kOptionsMainPage && page != addr::kOptionsMainPageAlt)) {
		reinterpret_cast<SelectFn>(addr::Rebase(addr::kOptionsSelect))(box, edx, player);
		return;
	}
	Log("picker: Options entry %d selected (page %d)", entry, page);
	if (entry < 1 || entry > static_cast<int>(g_entryNames.size()) || g_entryNames[entry - 1].empty()) {
		return;  // an unused entry: ignore
	}
	if (g_entryNames[entry - 1] == "more") {
		++g_page;
		g_reopen = true;
		g_reopenUntil = GetTickCount() + 1500;
		g_active = false;  // closing for the next page is not a cancel
		reinterpret_cast<CloseFn>((*reinterpret_cast<void***>(box))[addr::kBoxCloseSlot])(box);
		return;
	}
	g_picked = g_entryNames[entry - 1];
	Finish(PickerState::Chosen);
	reinterpret_cast<CloseFn>((*reinterpret_cast<void***>(box))[addr::kBoxCloseSlot])(box);
}

void __fastcall CloseDetour(void* box)
{
	if (g_active) {
		Log("picker: Options menu closed without a choice");
		Finish(PickerState::Cancelled);
	}
	g_originalClose(box);
}

bool HookTextLookup()
{
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(addr::kTextLookup));
	if (site[0] != 0x83 || site[1] != 0xec || site[2] != 0x64 || site[3] != 0xa1) {  // SUB ESP,0x64; MOV EAX,[cookie]
		Log("picker: unexpected bytes at the text lookup");
		return false;
	}
	constexpr size_t kStolen = 8;
	g_lookupTrampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (!g_lookupTrampoline) {
		return false;
	}
	memcpy(g_lookupTrampoline, site, kStolen);  // absolute operands are already relocated
	g_lookupTrampoline[kStolen] = 0xe9;
	const int32_t back = static_cast<int32_t>(reinterpret_cast<uintptr_t>(site + kStolen) - reinterpret_cast<uintptr_t>(g_lookupTrampoline + kStolen + 5));
	memcpy(g_lookupTrampoline + kStolen + 1, &back, sizeof(back));
	uint8_t patch[kStolen] = {0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&TextLookupDetour) - reinterpret_cast<uintptr_t>(site + 5));
	memcpy(patch + 1, &rel, sizeof(rel));
	DWORD oldProtect;
	if (!VirtualProtect(site, kStolen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}
	memcpy(site, patch, kStolen);
	VirtualProtect(site, kStolen, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, kStolen);
	return true;
}

bool HookSelect()
{
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(addr::kOptionsSelectCall));
	int32_t rel;
	memcpy(&rel, site + 1, sizeof(rel));
	if (site[0] != 0xe8 || reinterpret_cast<uintptr_t>(site) + 5 + rel != addr::Rebase(addr::kOptionsSelect)) {
		Log("picker: unexpected call at the Options selection");
		return false;
	}
	rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&SelectDetour) - (reinterpret_cast<uintptr_t>(site) + 5));
	DWORD oldProtect;
	if (!VirtualProtect(site + 1, sizeof(rel), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}
	memcpy(site + 1, &rel, sizeof(rel));
	VirtualProtect(site + 1, sizeof(rel), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	return true;
}

bool HookClose()
{
	auto* slot = reinterpret_cast<uintptr_t*>(addr::Rebase(addr::kOptionsMenuVtable)) + addr::kBoxCloseSlot;
	DWORD oldProtect;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		return false;
	}
	g_originalClose = reinterpret_cast<CloseFn>(*slot);
	*slot = reinterpret_cast<uintptr_t>(&CloseDetour);
	VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
	return true;
}

}  // namespace

bool InstallPicker()
{
	const bool ok = HookTextLookup() && HookSelect() && HookClose();
	Log("picker: Options menu hooks %s", ok ? "installed" : "not installed");
	return ok;
}

namespace {

bool OpenPage()
{
	void* hero = MainHero();
	auto* root = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGuiRoot));
	auto* holder = root ? *reinterpret_cast<uint8_t**>(root + addr::kGuiRootMenuHolder) : nullptr;
	void* boxList = holder ? *reinterpret_cast<void**>(holder + addr::kMenuHolderBoxList) : nullptr;
	if (!hero || !boxList || !g_lookupTrampoline) {
		Log("picker: cannot open (hero %p, box list %p)", hero, boxList);
		return false;
	}
	if (*reinterpret_cast<int*>(root + addr::kGuiRootMenuLock) > 0) {
		Log("picker: the game is not accepting menus right now");
		return false;
	}
	std::vector<Slot> slots = ListPartnerSlots();
	g_texts.clear();
	g_entryNames.assign(kEntryCount, "");
	AddText("TEXT_GUI_OPTIONS_TITLE", L"Player 2: choose a hero");
	for (int i = 0; i < kEntryCount; ++i) {
		const std::string desc = std::string(kEntryTags[i]) + "_DESC";
		AddText(kEntryTags[i], L"-");
		AddText(desc.c_str(), L" ");
	}
	auto setEntry = [](int entry, const std::string& name, const std::wstring& label, const std::wstring& detail) {
		g_entryNames[entry - 1] = name;
		const std::string desc = std::string(kEntryTags[entry - 1]) + "_DESC";
		for (Replacement& replacement : g_texts) {
			if (replacement.key == TagKey(kEntryTags[entry - 1])) {
				replacement.text = label;
			}
			else if (replacement.key == TagKey(desc.c_str())) {
				replacement.text = detail;
			}
		}
	};
	const size_t allEntries = sizeof(kHeroEntries) / sizeof(kHeroEntries[0]);
	const bool paged = slots.size() > allEntries;
	const size_t perPage = paged ? allEntries - 1 : allEntries;  // paging gives up one entry to "More"
	const int pages = paged ? static_cast<int>((slots.size() + perPage - 1) / perPage) : 1;
	g_page %= pages;
	const size_t first = static_cast<size_t>(g_page) * perPage;
	for (size_t i = 0; i < perPage && first + i < slots.size(); ++i) {
		setEntry(kHeroEntries[i], slots[first + i].name, slots[first + i].label, slots[first + i].detail);
	}
	if (paged) {
		wchar_t more[64];
		swprintf_s(more, L"More heroes... (page %d of %d)", g_page + 1, pages);
		setEntry(kMoreEntry, "more", more, L"Show the next page of heroes.");
	}
	setEntry(kNewHeroEntry, "new", L"New hero", L"Join with a brand new hero.");
	for (int i = 0; i < kEntryCount; ++i) {
		Log("picker: page %d entry %d: %s", g_page + 1, i + 1, g_entryNames[i].empty() ? "(unused)" : g_entryNames[i].c_str());
	}
	g_result = PickerState::Open;
	g_active = true;
	g_menuSeen = false;
	g_requestedAt = GetTickCount();
	reinterpret_cast<OpenOptionsFn>(addr::Rebase(addr::kOpenOptionsMenu))(boxList, nullptr, hero, 0);
	SetPickerPadSwap(true);
	Log("picker: Options menu requested");
	return true;
}

}  // namespace

bool OpenPartnerPicker()
{
	g_page = 0;
	g_reopen = false;
	return OpenPage();
}

PickerState PollPartnerPicker()
{
	if (g_reopen) {
		// The game may refuse menus for a moment while the last one closes.
		if (OpenPage()) {
			g_reopen = false;
		}
		else if (static_cast<int>(GetTickCount() - g_reopenUntil) > 0) {
			g_reopen = false;
			Finish(PickerState::Cancelled);
		}
	}
	if (g_active && !g_menuSeen && GetTickCount() - g_requestedAt > 3000) {
		Log("picker: the Options menu never appeared");
		Finish(PickerState::Cancelled);
	}
	const PickerState result = g_result;
	if (result == PickerState::Chosen || result == PickerState::Cancelled) {
		g_result = PickerState::Closed;
		if (result == PickerState::Chosen) {
			Log("picker: chose %s", g_picked.c_str());
		}
	}
	return result;
}

const char* PickedPartnerSave()
{
	return g_picked.c_str();
}
