#include "custommenu.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "addresses.h"
#include "guiscreen.h"
#include "log.h"

namespace {

using GetPlayerFn = void*(__fastcall*)(void* players, void* edx);
using OpenOptionsFn = void(__fastcall*)(void* boxList, void* edx, void* hero, int inGame);  // __thiscall, RET 8
using InputFn = char(__fastcall*)(void* box, void* edx, const int* action, int player);    // __thiscall, RET 8
using CloseFn = void(__fastcall*)(void* box);
using StickActionFn = int(__cdecl*)(int action, int player);

constexpr int kMaxRows = 8;          // rows that fit between the menu's lines (27 apart)
constexpr int kNativeRows = 6;       // up to this many keep the game's own spacing (38)
// First row's y in the section, just under the top line. Fixed rather than taken from the list:
// the title screen's list sits 30 higher because its first row (Save Management) is hidden.
constexpr float kFirstRowY = 130.0f;
constexpr DWORD kOpenTimeoutMs = 3000;
constexpr DWORD kRestoreDelayMs = 1500;  // after closing, for the close animation to finish
// Move actions keep arriving while a direction is held: one step, then repeats after a delay.
constexpr DWORD kHeldGapMs = 100;        // a same-direction action this soon after the last is a hold
constexpr DWORD kRepeatDelayMs = 400;
constexpr DWORD kRepeatMs = 120;

// Options box input actions (FUN_00bf0ab0).
bool IsUp(int action) { return action == 0x1e || action == 0x26 || action == 0x2e; }
bool IsDown(int action) { return action == 0x1f || action == 0x27 || action == 0x2f; }
bool IsSelect(int action) { return action == 0x5d || action == 0x5f; }
bool IsBack(int action) { return action == 0x60 || action == 0x0e; }

enum class Phase { Closed, Waiting, Labelling, Ready };

InputFn g_originalInput = nullptr;
CloseFn g_originalClose = nullptr;

Phase g_phase = Phase::Closed;
std::wstring g_title;
std::vector<CustomMenuItem> g_items;
int g_rows = 0;
float g_spacing = 38.0f;
int g_cursor = 0;
int g_top = 0;                 // item shown on the first row
void* g_screen = nullptr;
DWORD g_requestedAt = 0;
DWORD g_builtAt = 0;
void* g_restoreScreen = nullptr;  // closed menu's screen, put back once its close animation is over
DWORD g_restoreAt = 0;
int g_heldDirection = 0;
DWORD g_lastMoveAction = 0;
DWORD g_nextRepeat = 0;
bool g_loggedAction = false;
CustomMenuState g_result = CustomMenuState::Closed;
int g_choice = -1;
bool g_testOpened = false;
bool g_testKeyDown = false;

// Lua shared by the chunks: helpers and the two layouts of the Options screen (single player and
// co-op); only one is visible, but both are kept in step so the C++ side needs no detection.
const char kLuaPrelude[] = R"(
local OFF = -10000
local function el(p) local ok, e = pcall(getElement, p) if ok then return e end return nil end
local function get(e, a) if not e then return nil end local ok, v = pcall(getAttribute, e, a) if ok then return v end return nil end
local function set(e, a, v) if e then pcall(setAttribute, e, a, v) end end
local function each(p, f) local e = el(p) if not e then return end local ok, c = pcall(getChildren, e) if ok and type(c) == 'table' then for _, x in ipairs(c) do f(x) end end end
local layouts = { { 'Scene.Layer.Options.Sections.Options', 'SelectionTitle' }, { 'Scene.Layer.Options.Sections.Multiplayer', 'SelectionText' } }
)";

// Only x is moved: the game re-applies opacity to some rows every frame, but has not been seen to
// touch x (all of these elements sit at x 0).
const char kLuaHide[] = R"(
for _, l in ipairs(layouts) do
	each(l[1] .. '.' .. l[2], function(x) set(x, 'position.x', OFF) end)
	each(l[1] .. '.SelectionDesription', function(x) set(x, 'position.x', OFF) end)
	set(el(l[1] .. '.MouseHotspots'), 'position.x', OFF)
end
)";

const char kLuaRestore[] = R"(
for _, l in ipairs(layouts) do
	each(l[1] .. '.' .. l[2], function(x) set(x, 'position.x', 0) end)
	each(l[1] .. '.SelectionDesription', function(x) set(x, 'position.x', 0) end)
	set(el(l[1] .. '.MouseHotspots'), 'position.x', 0)
	for i = 1, 8 do set(el(l[1] .. '.CoopItem' .. i), 'position.x', OFF) end
	set(el(l[1] .. '.CoopDescription'), 'position.x', OFF)
end
)";

// Rows are cloned beside the list (in the section), so hiding the list's children leaves them
// alone; a reused screen keeps them, so existing ones are taken over instead of cloned again.
const char kLuaBuild[] = R"(
for _, l in ipairs(layouts) do
	local P, L = l[1], l[2]
	local sec, list, tpl = el(P), el(P .. '.' .. L), el(P .. '.' .. L .. '.NewGame')
	if sec and list and tpl then
		local lz = get(list, 'position.z') or 0
		for i = 1, 8 do
			local row = el(P .. '.CoopItem' .. i)
			if i <= ROWS then
				if not row then
					local ok, c = pcall(clone, tpl, 'CoopItem' .. i, sec)
					if ok then row = c else output('custom menu: clone ' .. i .. ' failed: ' .. tostring(c)) end
				end
				set(row, 'position.x', 0)
				set(row, 'position.y', TOP - SPACING * (i - 1))
				set(row, 'position.z', lz)
				set(row, 'opacity', 100)
			else
				set(row, 'position.x', OFF)
			end
		end
		local dl, d = el(P .. '.SelectionDesription'), el(P .. '.SelectionDesription.NewGameDescription')
		local desc = el(P .. '.CoopDescription')
		if not desc and d then
			local ok, c = pcall(clone, d, 'CoopDescription', sec)
			if ok then desc = c end
		end
		if desc and dl and d then
			set(desc, 'position.x', 0)
			set(desc, 'position.y', (get(dl, 'position.y') or 0) + (get(d, 'position.y') or 0))
			set(desc, 'position.z', get(dl, 'position.z') or 0)
			set(desc, 'opacity', 100)
		end
	else
		output('custom menu: ' .. P .. ' has no list or NewGame row')
	end
end
)";

// The bar's centre sits at its background child's offset; rows and bar share the section's space.
const char kLuaBar[] = R"(
for _, l in ipairs(layouts) do
	local P = l[1]
	local row, bar = el(P .. '.CoopItem' .. ROW), el(P .. '.Background.Selection')
	if row and bar then
		local off = get(el(P .. '.Background.Selection.SelectionBar_Background'), 'position.y') or 0
		set(bar, 'position.y', (get(row, 'position.y') or 0) - off)
		set(el(P .. '.Background.Selection.SelectionBar_ArrowTop'), 'opacity', UP)
		set(el(P .. '.Background.Selection.SelectionBar_ArrowBottom'), 'opacity', DOWN)
	end
end
)";

const char* const kSections[] = {"Options.Sections.Options", "Options.Sections.Multiplayer"};

void* MainHero()
{
	auto* game = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGameRoot));
	auto* inner = game ? *reinterpret_cast<uint8_t**>(game + 0x0c) : nullptr;
	void* players = inner ? *reinterpret_cast<void**>(inner + 0x40) : nullptr;
	auto* player = players ? static_cast<uint8_t*>(reinterpret_cast<GetPlayerFn>(addr::Rebase(addr::kMainPlayer))(players, nullptr)) : nullptr;
	auto** tracked = player ? *reinterpret_cast<void***>(player + addr::kPlayerHero) : nullptr;
	return tracked ? *tracked : nullptr;
}

std::string BuildChunk()
{
	char values[128];
	snprintf(values, sizeof(values), "local ROWS, TOP, SPACING = %d, %g, %g\n", g_rows, kFirstRowY, g_spacing);
	return std::string(kLuaPrelude) + values + kLuaBuild + kLuaHide;
}

// Moves the bar onto the cursor's row.
std::string BarChunk()
{
	char values[128];
	snprintf(values, sizeof(values), "local ROW, UP, DOWN = %d, %d, %d\n", g_cursor - g_top + 1, g_cursor > 0 ? 100 : 0,
		g_cursor < static_cast<int>(g_items.size()) - 1 ? 100 : 0);
	return std::string(kLuaPrelude) + values + kLuaBar;
}

bool ClonesPresent()
{
	for (const char* section : kSections) {
		if (FindGuiElement(g_screen, (std::string(section) + ".CoopItem1").c_str())) {
			return true;
		}
	}
	return false;
}

void ApplyRowLabels()
{
	char path[96];
	for (const char* section : kSections) {
		snprintf(path, sizeof(path), "%s.Title", section);
		SetGuiText(g_screen, path, g_title.c_str());
		for (int row = 0; row < g_rows; ++row) {
			snprintf(path, sizeof(path), "%s.CoopItem%d", section, row + 1);
			SetGuiText(g_screen, path, g_items[g_top + row].label.c_str());
		}
	}
}

void ApplyDescription()
{
	const std::wstring& text = g_items[g_cursor].description;
	char path[96];
	for (const char* section : kSections) {
		snprintf(path, sizeof(path), "%s.CoopDescription", section);
		SetGuiText(g_screen, path, text.empty() ? L" " : text.c_str());
	}
}

void Move(int delta)
{
	const int count = static_cast<int>(g_items.size());
	const int next = std::clamp(g_cursor + delta, 0, count - 1);
	if (next == g_cursor) {
		return;
	}
	g_cursor = next;
	const int top = std::clamp(g_top, g_cursor - g_rows + 1, g_cursor);
	if (top != g_top) {
		g_top = top;
		ApplyRowLabels();
	}
	ApplyDescription();
	RunGuiLua(g_screen, BarChunk());
}

void Close(void* box)
{
	reinterpret_cast<CloseFn>((*reinterpret_cast<void***>(box))[addr::kBoxCloseSlot])(box);
}

char __fastcall InputDetour(void* box, void* edx, const int* action, int player)
{
	if (g_phase == Phase::Closed) {
		return g_originalInput(box, edx, action, player);
	}
	if (!g_loggedAction) {
		g_loggedAction = true;
		Log("custom menu: first input action 0x%x from player %d", *action, player);
	}
	if (IsBack(*action)) {
		return g_originalInput(box, edx, action, player);  // the game closes the menu; CloseDetour cleans up
	}
	if (g_phase != Phase::Ready) {
		return 1;
	}
	int code = *action;
	if (code == addr::kActionLeftStick || code == addr::kActionRightStick) {
		code = reinterpret_cast<StickActionFn>(addr::Rebase(addr::kStickAction))(code, player);
	}
	if (IsUp(code) || IsDown(code)) {
		const int direction = IsUp(code) ? -1 : 1;
		const DWORD now = GetTickCount();
		const bool held = direction == g_heldDirection && now - g_lastMoveAction <= kHeldGapMs;
		g_heldDirection = direction;
		g_lastMoveAction = now;
		if (!held) {
			Move(direction);
			g_nextRepeat = now + kRepeatDelayMs;
		}
		else if (static_cast<int>(now - g_nextRepeat) >= 0) {
			Move(direction);
			g_nextRepeat = now + kRepeatMs;
		}
	}
	else if (IsSelect(code)) {
		g_choice = g_cursor;
		g_result = CustomMenuState::Chosen;
		Log("custom menu: item %d chosen", g_choice);
		Close(box);
	}
	return 1;  // everything else (mouse hover, tabs, paging) is swallowed while the menu is ours
}

void __fastcall CloseDetour(void* box)
{
	if (g_phase != Phase::Closed) {
		// Putting the originals back now would flash them during the close animation.
		g_restoreScreen = g_screen;
		g_restoreAt = GetTickCount() + kRestoreDelayMs;
		if (g_result != CustomMenuState::Chosen) {
			g_result = CustomMenuState::Cancelled;
			Log("custom menu: closed without a choice");
		}
		g_phase = Phase::Closed;
		g_screen = nullptr;
	}
	g_originalClose(box);
}

bool HookSlot(size_t slot, void* detour, uintptr_t* previous)
{
	auto* entry = reinterpret_cast<uintptr_t*>(addr::Rebase(addr::kOptionsMenuVtable)) + slot;
	DWORD oldProtect;
	if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtect)) {
		return false;
	}
	*previous = *entry;
	*entry = reinterpret_cast<uintptr_t>(detour);
	VirtualProtect(entry, sizeof(*entry), oldProtect, &oldProtect);
	return true;
}

// The test menu (MenuTest=1): F8, or MenuTestKey (a virtual-key code), while the game has focus.
void PumpTest()
{
	static const bool enabled = GetPrivateProfileIntW(L"Fable3Coop", L"MenuTest", 0, ConfigPath()) != 0;
	if (!enabled) {
		return;
	}
	if (g_testOpened) {
		const CustomMenuState result = PollCustomMenu();
		if (result == CustomMenuState::Chosen || result == CustomMenuState::Cancelled) {
			g_testOpened = false;
			Log("custom menu: test menu %s", result == CustomMenuState::Chosen ? "chose an item" : "cancelled");
		}
	}
	static const int key = GetPrivateProfileIntW(L"Fable3Coop", L"MenuTestKey", VK_F8, ConfigPath());
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	const bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState(key) & 0x8000) != 0;
	const bool pressed = down && !g_testKeyDown;
	g_testKeyDown = down;
	if (!pressed || g_testOpened) {
		return;
	}
	std::vector<CustomMenuItem> items;
	for (int i = 1; i <= 12; ++i) {
		items.push_back({L"Test item " + std::to_wstring(i), L"Description of test item " + std::to_wstring(i)});
	}
	g_testOpened = OpenCustomMenu(L"Test menu", items, 0);
}

}  // namespace

bool InstallCustomMenu()
{
	uintptr_t input = 0;
	uintptr_t close = 0;
	const bool ok = HookSlot(addr::kOptionsInputSlot, reinterpret_cast<void*>(&InputDetour), &input) &&
		HookSlot(addr::kBoxCloseSlot, reinterpret_cast<void*>(&CloseDetour), &close);
	g_originalInput = reinterpret_cast<InputFn>(input);
	g_originalClose = reinterpret_cast<CloseFn>(close);
	Log("custom menu: Options box hooks %s", ok ? "installed" : "not installed");
	return ok;
}

bool OpenCustomMenu(const std::wstring& title, const std::vector<CustomMenuItem>& items, int selected)
{
	auto* root = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGuiRoot));
	auto* holder = root ? *reinterpret_cast<uint8_t**>(root + addr::kGuiRootMenuHolder) : nullptr;
	void* boxList = holder ? *reinterpret_cast<void**>(holder + addr::kMenuHolderBoxList) : nullptr;
	if (items.empty() || g_phase != Phase::Closed || !boxList || !g_originalInput) {
		Log("custom menu: cannot open (%u items, phase %d, box list %p)", static_cast<unsigned>(items.size()),
			static_cast<int>(g_phase), boxList);
		return false;
	}
	if (*reinterpret_cast<int*>(root + addr::kGuiRootMenuLock) > 0) {
		Log("custom menu: the game is not accepting menus right now");
		return false;
	}
	g_title = title;
	g_items = items;
	const int count = static_cast<int>(items.size());
	g_rows = std::min(count, kMaxRows);
	g_spacing = count > kNativeRows ? 27.0f : 38.0f;
	g_cursor = std::clamp(selected, 0, count - 1);
	g_top = std::clamp(g_cursor - g_rows + 1, 0, count - g_rows);
	g_screen = nullptr;
	g_restoreScreen = nullptr;  // the new menu takes the screen over as it is
	g_requestedAt = GetTickCount();
	g_heldDirection = 0;
	g_loggedAction = false;
	g_result = CustomMenuState::Open;
	g_choice = -1;
	g_phase = Phase::Waiting;
	reinterpret_cast<OpenOptionsFn>(addr::Rebase(addr::kOpenOptionsMenu))(boxList, nullptr, MainHero(), 0);
	Log("custom menu: \"%ls\" requested (%d items, %d rows)", title.c_str(), count, g_rows);
	return true;
}

bool CustomMenuIsOpen()
{
	return g_phase != Phase::Closed;
}

CustomMenuState PollCustomMenu()
{
	const CustomMenuState result = g_result;
	if (result == CustomMenuState::Chosen || result == CustomMenuState::Cancelled) {
		g_result = CustomMenuState::Closed;
	}
	return result;
}

int CustomMenuChoice()
{
	return g_choice;
}

void PumpCustomMenu()
{
	const DWORD now = GetTickCount();
	if (g_restoreScreen && static_cast<int>(now - g_restoreAt) >= 0) {
		// Only a reused screen needs it; each open has so far had a fresh instance with its own
		// rows. Never sooner: input still reaches the closing box, and its fade would show them.
		if (FindGuiScreen(addr::kOptionsScreenId) == g_restoreScreen) {
			RunGuiLua(g_restoreScreen, std::string(kLuaPrelude) + kLuaRestore);
			Log("custom menu: original rows put back on screen %p", g_restoreScreen);
		}
		g_restoreScreen = nullptr;
	}
	if (g_phase == Phase::Waiting) {
		void* screen = FindGuiScreen(addr::kOptionsScreenId);
		if (screen) {
			// Built on the first frame the screen exists, so the original rows never show.
			g_screen = screen;
			RunGuiLua(g_screen, BuildChunk());
			g_builtAt = now;
			g_phase = Phase::Labelling;
			Log("custom menu: rows built on screen %p", g_screen);
		}
		else if (now - g_requestedAt > kOpenTimeoutMs) {
			Log("custom menu: the Options menu never appeared");
			g_phase = Phase::Closed;
			g_result = CustomMenuState::Cancelled;
		}
	}
	if (g_phase == Phase::Labelling) {
		if (FindGuiScreen(addr::kOptionsScreenId) != g_screen) {
			Log("custom menu: the screen changed before the rows were labelled; building again");
			g_phase = Phase::Waiting;
		}
		else if (ClonesPresent()) {
			ApplyRowLabels();
			ApplyDescription();
			RunGuiLua(g_screen, BarChunk());
			g_phase = Phase::Ready;
			Log("custom menu: ready after %lu ms", now - g_builtAt);
		}
		else if (now - g_builtAt > kOpenTimeoutMs) {
			Log("custom menu: the cloned rows never appeared");
			g_builtAt = now;  // keep waiting; back still closes the menu
		}
	}
	PumpTest();
}
