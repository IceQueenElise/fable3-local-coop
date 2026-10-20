#include "picker.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
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
	bool isLast = false; // player 1's last save
	std::string journey; // e.g. "hero6"
};

// Main page entries, in the box's own numbering ([box+0xCC] = 1..6), by the text tag each shows.
// Entry 4 (LIVE) is drawn without a text tag, so it can't be relabelled and stays unused.
const char* const kEntryTags[] = {"TEXT_GUI_SAVE_MANAGEMENT", "TEXT_GUI_GAME_SETTINGS", "TEXT_GUI_NEW_GAME",
	"TEXT_GUI_LIVE_DASHBOARD", "TEXT_GUI_LIVE_MARKETPLACE", "TEXT_GUI_EXIT_TO_WINDOWS"};
constexpr int kEntryCount = sizeof(kEntryTags) / sizeof(kEntryTags[0]);
// Player 2: four heroes, "More" (when paging) and "New hero". Player 1: four heroes, the last
// becoming "More" when paging (new games start from the title screen).
constexpr int kPartnerHeroEntries[] = {1, 2, 3, 5};
constexpr int kMainHeroEntries[] = {2, 3, 5, 6};  // the title screen's Options menu hides entry 1 (Save Management)
constexpr int kNewHeroEntry = 6;

struct Replacement {
	uint32_t key;
	std::wstring text;
};

bool g_active = false;          // picker owns the Options menu
bool g_forPlayerTwo = true;     // join picker (player 2) or Continue picker (player 1)
bool g_menuSeen = false;        // the menu has asked for the picker's text
bool g_liveHidden = false;      // the LIVE entry (a logo, not text) has been hidden
DWORD g_restoreLiveAt = 0;      // show it again a moment after the picker closes (no flash)
int g_hidePasses = 0;           // hide passes since the menu opened (diagnostics)
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

using FindElementFn = void*(__cdecl*)(void* pathByValue, void* parent, void* screen);  // destroys the path string
using IsVisibleFn = char(__cdecl*)(void* element, void* screen);
using GetOpacityFn = float(__cdecl*)(void* element, void* screen, char raw);
using SetOpacityFn = void(__cdecl*)(void* element, void* screen, float opacity);

struct SavedOpacity {
	void* element;
	void* screen;
	float opacity;
};
std::vector<SavedOpacity> g_savedOpacity;  // LIVE elements' opacity before the picker hid them

// The Options menu's 4th entry is the Games for Windows LIVE logo, not text, so it can't be
// relabelled; hide its logo, mouse hotspot and description while the picker owns the menu.
void SetLiveEntryVisible(bool visible)
{
	// The Options screen can be in its own slot of the screen table or in the shared slot (a
	// reopened menu gets a fresh instance there): apply to both.
	auto* root = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGuiRoot));
	auto* screens = root ? *reinterpret_cast<uint8_t**>(root + addr::kGuiRootScreens) : nullptr;
	if (!screens) {
		return;
	}
	void* const candidates[] = {*reinterpret_cast<void**>(screens + addr::kScreensFirst + 4 * addr::kOptionsScreenId),
		*reinterpret_cast<void**>(screens + addr::kScreensShared)};
	// Both layouts of the menu: "Options" (title screen) and "Multiplayer" (in game). Paths that
	// don't exist on the current screen are ignored by the game.
	static const char* const kParts[] = {"Options.Sections.Options.SelectionTitle.Live_Logo",
		"Options.Sections.Options.MouseHotspots.LiveHotspot", "Options.Sections.Options.SelectionDesription.LiveDescription",
		"Options.Sections.Multiplayer.SelectionText.Live_Logo", "Options.Sections.Multiplayer.SelectionText.Live",
		"Options.Sections.Multiplayer.SelectionDesription.Live", "Options.Sections.Multiplayer.MouseHotspots.LiveHotspot"};
	// Diagnostics: on the first pass after each open and about a second later.
	const bool diagnose = LogEnabled() && (g_hidePasses == 0 || g_hidePasses == 10);
	++g_hidePasses;
	// Opacity is written directly: the menu's own animations make the logo visible again, and the
	// game's show/hide helper skips elements it believes are already hidden.
	for (void* screen : candidates) {
		if (!screen) {
			continue;
		}
		for (const char* part : kParts) {
			void* path = nullptr;  // taken by value and destroyed by the lookup
			reinterpret_cast<StringFromTextFn>(addr::Rebase(addr::kStringFromText))(&path, nullptr, part, -1);
			void* element = reinterpret_cast<FindElementFn>(addr::Rebase(addr::kFindElement))(path, nullptr, screen);
			if (!element) {
				continue;
			}
			auto saved = std::find_if(g_savedOpacity.begin(), g_savedOpacity.end(),
				[&](const SavedOpacity& entry) { return entry.element == element && entry.screen == screen; });
			const float now = reinterpret_cast<GetOpacityFn>(addr::Rebase(addr::kGetElementOpacity))(element, screen, 1);
			if (diagnose) {
				Log("picker: screen %p element %s: %p, opacity %.2f -> %s", screen, part, element, now, visible ? "restore" : "0");
			}
			if (!visible) {
				if (saved == g_savedOpacity.end()) {
					g_savedOpacity.push_back({element, screen, now > 0.01f ? now : 1.0f});
				}
				reinterpret_cast<SetOpacityFn>(addr::Rebase(addr::kSetElementOpacity))(element, screen, 0.0f);
			}
			else if (saved != g_savedOpacity.end()) {
				reinterpret_cast<SetOpacityFn>(addr::Rebase(addr::kSetElementOpacity))(element, screen, saved->opacity);
			}
		}
		if (candidates[1] == candidates[0]) {
			break;
		}
	}
	if (visible) {
		g_savedOpacity.clear();
	}
	if (g_liveHidden == visible) {
		Log("picker: LIVE entry %s", visible ? "restored" : "hidden");
	}
	g_liveHidden = !visible;
}

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

// A save's journey (hero playthrough): "hero6autosave" and "hero6save1" are both journey "hero6".
std::string JourneyOf(const std::string& name)
{
	size_t end = 0;
	while (end < name.size() && !isdigit(static_cast<unsigned char>(name[end]))) {
		++end;
	}
	while (end < name.size() && isdigit(static_cast<unsigned char>(name[end]))) {
		++end;
	}
	return name.substr(0, end);
}

std::string LastMainSave()
{
	wchar_t wide[64];
	GetPrivateProfileStringW(L"State", L"LastMainSave", L"", wide, 64, ConfigPath());
	char text[64];
	WideCharToMultiByte(CP_ACP, 0, wide, -1, text, sizeof(text), nullptr, nullptr);
	return text;
}

// Heroes the picker offers, newest first: for player 2 every hero except the loaded main save;
// for player 1 the main saves the game can load, player 1's last save first.
std::vector<Slot> ListPartnerSlots()
{
	std::vector<Slot> slots;
	const std::wstring folder = g_forPlayerTwo ? PartnerSaveFolder() : PlayerOneSaveFolder();
	const std::string last = g_forPlayerTwo ? std::string() : LastMainSave();
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
		// Player 1's hero is the journey of the loaded main save: none of its saves are offered.
		if (g_forPlayerTwo && !loaded.empty() && _stricmp(JourneyOf(nameText).c_str(), JourneyOf(loaded).c_str()) == 0) {
			Log("picker: %s belongs to player 1's loaded hero (%s); not offered", nameText, loaded.c_str());
			continue;
		}
		if (!g_forPlayerTwo && !IsLoadableMainSave(nameText)) {
			continue;  // henchman slot, or owned by another profile
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
			const std::string journey = JourneyOf(narrow);
			slot.label = std::wstring(journey.begin(), journey.end());
		}
		SYSTEMTIME utc, local;
		FileTimeToSystemTime(&found.ftLastWriteTime, &utc);
		SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
		static const wchar_t* const kMonths[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
		wchar_t detail[128];
		if (g_forPlayerTwo) {
			swprintf_s(detail, L"%s - last played %s %d. Join as this hero.", henchman ? L"Henchman" : L"Hero from a main save",
				kMonths[(local.wMonth + 11) % 12], local.wDay);
		}
		else {
			swprintf_s(detail, L"Last played %s %d. Continue will load this hero.", kMonths[(local.wMonth + 11) % 12], local.wDay);
		}
		slot.detail = detail;
		if (henchman) {
			slot.label += L" (Henchman)";
		}
		slot.journey = JourneyOf(nameText);
		slot.isLast = !last.empty() && _stricmp(slot.journey.c_str(), JourneyOf(last).c_str()) == 0;
		if (slot.isLast) {
			slot.label += L" (last played)";
		}
		slots.push_back(slot);
	} while (FindNextFileW(find, &found));
	FindClose(find);
	std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) { return CompareFileTime(&a.saved, &b.saved) > 0; });
	// One entry per journey: its newest save, or for player 1 the exact save Continue would load.
	std::vector<Slot> journeys;
	for (const Slot& slot : slots) {
		auto existing = std::find_if(journeys.begin(), journeys.end(), [&](const Slot& j) { return _stricmp(j.journey.c_str(), slot.journey.c_str()) == 0; });
		if (existing == journeys.end()) {
			journeys.push_back(slot);
		}
		else if (!last.empty() && _stricmp(slot.name.c_str(), last.c_str()) == 0) {
			*existing = slot;
		}
	}
	std::stable_sort(journeys.begin(), journeys.end(), [](const Slot& a, const Slot& b) { return a.isLast && !b.isLast; });
	return journeys;
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

void SetLiveEntryVisible(bool visible);

void Finish(PickerState result)
{
	if (g_liveHidden) {
		g_restoreLiveAt = GetTickCount() + 1500;  // after the closing animation
	}
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

void BuildPage();
void ApplyPageInPlace();

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
		BuildPage();
		ApplyPageInPlace();
		return;
	}
	g_picked = g_entryNames[entry - 1];
	if (!g_forPlayerTwo) {
		// Continue loads the chosen hero (ContinueDetour reads LastMainSave; the initial save name
		// covers the case where the game has already picked).
		wchar_t wide[64];
		MultiByteToWideChar(CP_ACP, 0, g_picked.c_str(), -1, wide, 64);
		WritePrivateProfileStringW(L"State", L"LastMainSave", wide, ConfigPath());
		SetInitialSaveName(g_picked.c_str());
	}
	Finish(PickerState::Chosen);
	reinterpret_cast<CloseFn>((*reinterpret_cast<void***>(box))[addr::kBoxCloseSlot])(box);
}

using SetEntryFn = void(__fastcall*)(void* box, void* edx, int entry);  // __thiscall, RET 4

bool EntryUsable(int entry)
{
	return entry >= 1 && entry <= static_cast<int>(g_entryNames.size()) && !g_entryNames[entry - 1].empty();
}

// Moving the highlight on the main page (down, up, mouse hover): while the picker owns the menu,
// skip entries with nothing on them (the hidden LIVE logo, blanks, entries the game hides).
void __fastcall SetEntryDetour(void* box, void* edx, int entry)
{
	auto setEntry = reinterpret_cast<SetEntryFn>(addr::Rebase(addr::kOptionsSetEntry));
	const int page = *reinterpret_cast<int*>(static_cast<uint8_t*>(box) + addr::kOptionsPage);
	if (!g_active || (page != addr::kOptionsMainPage && page != addr::kOptionsMainPageAlt) || EntryUsable(entry)) {
		setEntry(box, edx, entry);
		return;
	}
	const int current = *reinterpret_cast<int*>(static_cast<uint8_t*>(box) + addr::kOptionsEntry);
	const int step = entry > current ? 1 : entry < current ? -1 : 0;
	for (int next = entry + step; step != 0 && next >= 1 && next <= static_cast<int>(g_entryNames.size()); next += step) {
		if (EntryUsable(next)) {
			setEntry(box, edx, next);
			return;
		}
	}
	// Nothing usable that way (or a mouse hover on a blank): stay put.
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

bool HookSetEntry()
{
	bool ok = true;
	for (uintptr_t va : addr::kOptionsSetEntryCalls) {
		auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(va));
		int32_t rel;
		memcpy(&rel, site + 1, sizeof(rel));
		if (site[0] != 0xe8 || reinterpret_cast<uintptr_t>(site) + 5 + rel != addr::Rebase(addr::kOptionsSetEntry)) {
			Log("picker: unexpected call at the Options entry setter");
			ok = false;
			continue;
		}
		rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&SetEntryDetour) - (reinterpret_cast<uintptr_t>(site) + 5));
		DWORD oldProtect;
		if (!VirtualProtect(site + 1, sizeof(rel), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			ok = false;
			continue;
		}
		memcpy(site + 1, &rel, sizeof(rel));
		VirtualProtect(site + 1, sizeof(rel), oldProtect, &oldProtect);
		FlushInstructionCache(GetCurrentProcess(), site, 5);
	}
	return ok;
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
	const bool ok = HookTextLookup() && HookSelect() && HookClose() && HookSetEntry();
	Log("picker: Options menu hooks %s", ok ? "installed" : "not installed");
	return ok;
}

namespace {

// Fills g_texts / g_entryNames for the current page.
void BuildPage()
{
	std::vector<Slot> slots = ListPartnerSlots();
	g_texts.clear();
	g_entryNames.assign(kEntryCount, "");
	AddText("TEXT_GUI_OPTIONS_TITLE", g_forPlayerTwo ? L"Player 2: choose a hero" : L"Choose your hero");
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
	const int* heroEntries = g_forPlayerTwo ? kPartnerHeroEntries : kMainHeroEntries;
	const size_t allEntries = g_forPlayerTwo ? sizeof(kPartnerHeroEntries) / sizeof(int) : sizeof(kMainHeroEntries) / sizeof(int);
	const bool paged = slots.size() > allEntries;
	const size_t perPage = paged ? allEntries - 1 : allEntries;  // paging gives up one entry to "More"
	const int moreEntry = heroEntries[allEntries - 1];
	const int pages = paged ? static_cast<int>((slots.size() + perPage - 1) / perPage) : 1;
	g_page %= pages;
	const size_t first = static_cast<size_t>(g_page) * perPage;
	for (size_t i = 0; i < perPage && first + i < slots.size(); ++i) {
		setEntry(heroEntries[i], slots[first + i].name, slots[first + i].label, slots[first + i].detail);
	}
	if (paged) {
		wchar_t more[64];
		swprintf_s(more, L"More heroes... (page %d of %d)", g_page + 1, pages);
		setEntry(moreEntry, "more", more, L"Show the next page of heroes.");
	}
	if (g_forPlayerTwo) {
		setEntry(kNewHeroEntry, "new", L"New hero", L"Join with a brand new hero.");
	}
	for (int i = 0; i < kEntryCount; ++i) {
		Log("picker: page %d entry %d: %s", g_page + 1, i + 1, g_entryNames[i].empty() ? "(unused)" : g_entryNames[i].c_str());
	}
}

const std::wstring* TextFor(const char* tag)
{
	const uint32_t key = TagKey(tag);
	for (const Replacement& replacement : g_texts) {
		if (replacement.key == key) {
			return &replacement.text;
		}
	}
	return nullptr;
}

using SetTextFn = void(__cdecl*)(void* element, void* screen, void* const* text);

// Rewrites the open menu's entry labels and descriptions for the current page, in both layouts
// of the Options screen (title screen "Options", in game "Multiplayer"), so paging doesn't have
// to reopen the menu.
void ApplyPageInPlace()
{
	struct ElementText {
		const char* path;
		const char* tag;
	};
	static const ElementText kElements[] = {
		{"Options.Sections.Options.SelectionTitle.SaveManagment", "TEXT_GUI_SAVE_MANAGEMENT"},
		{"Options.Sections.Options.SelectionTitle.GameSettings", "TEXT_GUI_GAME_SETTINGS"},
		{"Options.Sections.Options.SelectionTitle.NewGame", "TEXT_GUI_NEW_GAME"},
		{"Options.Sections.Options.SelectionTitle.Live_Marketplace", "TEXT_GUI_LIVE_MARKETPLACE"},
		{"Options.Sections.Options.SelectionTitle.Exit", "TEXT_GUI_EXIT_TO_WINDOWS"},
		{"Options.Sections.Options.SelectionDesription.SaveDescription", "TEXT_GUI_SAVE_MANAGEMENT_DESC"},
		{"Options.Sections.Options.SelectionDesription.SettingsDescription", "TEXT_GUI_GAME_SETTINGS_DESC"},
		{"Options.Sections.Options.SelectionDesription.NewGameDescription", "TEXT_GUI_NEW_GAME_DESC"},
		{"Options.Sections.Options.SelectionDesription.MarketplaceDescription", "TEXT_GUI_LIVE_MARKETPLACE_DESC"},
		{"Options.Sections.Options.SelectionDesription.ExitDescription", "TEXT_GUI_EXIT_TO_WINDOWS_DESC"},
		{"Options.Sections.Multiplayer.SelectionText.SaveManagement", "TEXT_GUI_SAVE_MANAGEMENT"},
		{"Options.Sections.Multiplayer.SelectionText.GameSettings", "TEXT_GUI_GAME_SETTINGS"},
		{"Options.Sections.Multiplayer.SelectionText.NewGame", "TEXT_GUI_NEW_GAME"},
		{"Options.Sections.Multiplayer.SelectionText.Live_Marketplace", "TEXT_GUI_LIVE_MARKETPLACE"},
		{"Options.Sections.Multiplayer.SelectionText.Exit", "TEXT_GUI_EXIT_TO_WINDOWS"},
		{"Options.Sections.Multiplayer.SelectionDesription.SaveAndContDescription", "TEXT_GUI_SAVE_MANAGEMENT_DESC"},
		{"Options.Sections.Multiplayer.SelectionDesription.GameSettingsDescription", "TEXT_GUI_GAME_SETTINGS_DESC"},
		{"Options.Sections.Multiplayer.SelectionDesription.NewGameDescription", "TEXT_GUI_NEW_GAME_DESC"},
		{"Options.Sections.Multiplayer.SelectionDesription.MarketplaceDescription", "TEXT_GUI_LIVE_MARKETPLACE_DESC"},
		{"Options.Sections.Multiplayer.SelectionDesription.ExitDescription", "TEXT_GUI_EXIT_TO_WINDOWS_DESC"},
	};
	auto* root = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGuiRoot));
	auto* screens = root ? *reinterpret_cast<uint8_t**>(root + addr::kGuiRootScreens) : nullptr;
	if (!screens) {
		return;
	}
	void* const candidates[] = {*reinterpret_cast<void**>(screens + addr::kScreensFirst + 4 * addr::kOptionsScreenId),
		*reinterpret_cast<void**>(screens + addr::kScreensShared)};
	int set = 0;
	for (void* screen : candidates) {
		if (!screen) {
			continue;
		}
		for (const ElementText& item : kElements) {
			const std::wstring* text = TextFor(item.tag);
			if (!text) {
				continue;
			}
			void* path = nullptr;  // taken by value and destroyed by the lookup
			reinterpret_cast<StringFromTextFn>(addr::Rebase(addr::kStringFromText))(&path, nullptr, item.path, -1);
			void* element = reinterpret_cast<FindElementFn>(addr::Rebase(addr::kFindElement))(path, nullptr, screen);
			if (!element) {
				continue;
			}
			void* wide = nullptr;
			reinterpret_cast<WideAssignFn>(addr::Rebase(addr::kWideStringAssign))(&wide, nullptr, text->c_str(), -1);
			reinterpret_cast<SetTextFn>(addr::Rebase(addr::kSetElementText))(element, screen, &wide);
			reinterpret_cast<StringDestroyFn>(addr::Rebase(addr::kWideStringDestroy))(&wide);
			++set;
		}
		if (candidates[1] == candidates[0]) {
			break;
		}
	}
	Log("picker: page %d applied in place (%d texts)", g_page + 1, set);
}

bool OpenPage()
{
	void* hero = MainHero();
	auto* root = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGuiRoot));
	auto* holder = root ? *reinterpret_cast<uint8_t**>(root + addr::kGuiRootMenuHolder) : nullptr;
	void* boxList = holder ? *reinterpret_cast<void**>(holder + addr::kMenuHolderBoxList) : nullptr;
	if ((!hero && g_forPlayerTwo) || !boxList || !g_lookupTrampoline) {
		Log("picker: cannot open (hero %p, box list %p)", hero, boxList);
		return false;
	}
	if (*reinterpret_cast<int*>(root + addr::kGuiRootMenuLock) > 0) {
		Log("picker: the game is not accepting menus right now");
		return false;
	}
	BuildPage();
	g_result = PickerState::Open;
	g_active = true;
	g_menuSeen = false;
	g_liveHidden = false;
	g_restoreLiveAt = 0;
	g_hidePasses = 0;
	g_requestedAt = GetTickCount();
	reinterpret_cast<OpenOptionsFn>(addr::Rebase(addr::kOpenOptionsMenu))(boxList, nullptr, hero, 0);
	SetPickerPadSwap(g_forPlayerTwo);
	Log("picker: Options menu requested");
	return true;
}

}  // namespace

bool OpenPartnerPicker()
{
	g_forPlayerTwo = true;
	g_page = 0;
	g_reopen = false;
	return OpenPage();
}

bool OpenMainPicker()
{
	g_forPlayerTwo = false;
	g_page = 0;
	g_reopen = false;
	return OpenPage();
}

bool PickerIsForPlayerTwo()
{
	return g_forPlayerTwo;
}

PickerState PollPartnerPicker()
{
	// Re-apply every frame while open: the menu's animations restore the logo's opacity.
	if (g_active && g_menuSeen) {
		SetLiveEntryVisible(false);
	}
	if (!g_active && g_liveHidden && g_restoreLiveAt && static_cast<int>(GetTickCount() - g_restoreLiveAt) >= 0) {
		g_restoreLiveAt = 0;
		SetLiveEntryVisible(true);  // the normal Options menu keeps its LIVE entry
	}
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
