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

#include "custommenu.h"
#include "input.h"
#include "log.h"
#include "proxy.h"
#include "saves.h"

// The pickers are custom menus (custommenu.cpp): one list of every hero offered, plus "New hero"
// for player 2, on the Options menu's art.

namespace {

struct Slot {
	std::string name;    // e.g. "hero5autosave"
	std::wstring label;  // entry text
	std::wstring detail; // description text
	FILETIME saved;
	bool isLast = false; // player 1's last save
	std::string journey; // e.g. "hero6"
};

bool g_active = false;          // a picker menu is requested or open
bool g_forPlayerTwo = true;     // join picker (player 2) or Continue picker (player 1)
std::vector<std::string> g_itemNames;  // per menu item: save name, or "new"
PickerState g_result = PickerState::Closed;
std::string g_picked;

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

void Finish(PickerState result)
{
	g_active = false;
	g_result = result;
	SetPickerPadSwap(false);
}

bool OpenPicker()
{
	const std::vector<Slot> slots = ListPartnerSlots();
	std::vector<CustomMenuItem> items;
	g_itemNames.clear();
	for (const Slot& slot : slots) {
		items.push_back({slot.label, slot.detail});
		g_itemNames.push_back(slot.name);
	}
	if (g_forPlayerTwo) {
		items.push_back({L"New hero", L"Join with a brand new hero."});
		g_itemNames.push_back("new");
	}
	if (items.empty()) {
		Log("picker: no heroes to offer");
		return false;
	}
	for (size_t i = 0; i < g_itemNames.size(); ++i) {
		Log("picker: item %u: %s", static_cast<unsigned>(i + 1), g_itemNames[i].c_str());
	}
	if (!OpenCustomMenu(g_forPlayerTwo ? L"Player 2: choose a hero" : L"Choose your hero", items)) {
		return false;
	}
	g_active = true;
	g_result = PickerState::Open;
	// Player 2 has no GUI input of their own before joining: their pad drives player 1's.
	SetPickerPadSwap(g_forPlayerTwo);
	return true;
}

void Choose(const std::string& name)
{
	g_picked = name;
	if (!g_forPlayerTwo) {
		// Continue loads the chosen hero (ContinueDetour reads LastMainSave; the initial save name
		// covers the case where the game has already picked).
		wchar_t wide[64];
		MultiByteToWideChar(CP_ACP, 0, g_picked.c_str(), -1, wide, 64);
		WritePrivateProfileStringW(L"State", L"LastMainSave", wide, ConfigPath());
		SetInitialSaveName(g_picked.c_str());
	}
	Finish(PickerState::Chosen);
}

}  // namespace

bool OpenPartnerPicker()
{
	g_forPlayerTwo = true;
	return OpenPicker();
}

bool OpenMainPicker()
{
	g_forPlayerTwo = false;
	return OpenPicker();
}

bool PickerIsForPlayerTwo()
{
	return g_forPlayerTwo;
}

PickerState PollPartnerPicker()
{
	if (g_active) {
		switch (PollCustomMenu()) {
		case CustomMenuState::Chosen: {
			const int choice = CustomMenuChoice();
			if (choice >= 0 && choice < static_cast<int>(g_itemNames.size())) {
				Choose(g_itemNames[choice]);
			}
			else {
				Finish(PickerState::Cancelled);
			}
			break;
		}
		case CustomMenuState::Cancelled:
			Log("picker: closed without a choice");
			Finish(PickerState::Cancelled);
			break;
		default:
			break;
		}
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
