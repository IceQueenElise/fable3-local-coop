#include "gui.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "addresses.h"
#include "log.h"

namespace {

using GetPlayerFn = void*(__fastcall*)(void* players, void* edx);
using PtrFn = void*(__cdecl*)();
using AllocFn = void*(__cdecl*)(size_t);
using CtorFn = void*(__fastcall*)(void* self);
using ThisArgFn = void(__fastcall*)(void* self, void* edx, void* arg);  // __thiscall with one arg
using ActionFn = void(__fastcall*)(void* self, void* edx, const int* action);

ActionFn g_originalAction = nullptr;
void* g_mainGuiInput = nullptr;   // seen in the action hook
void* g_couchGuiInput = nullptr;
void* g_registeredWith = nullptr;
volatile GuiOwner g_owner = GuiOwner::None;
bool g_lastOpen = false;
int g_lastMode = -2;
volatile bool g_couchMenu = false;  // player 2 has a menu open; read by the XInput exports
DWORD g_lastMainButton = 0;   // GetTickCount() of the last button action per GUI input
DWORD g_lastCouchButton = 0;

// Logs each distinct button action id once per GUI input, to document the action ids.
void NoteActionId(bool couchInput, int id)
{
	static bool seen[2][256];
	if (id >= 0 && id < 256 && !seen[couchInput][id]) {
		seen[couchInput][id] = true;
		Log("GUI action 0x%x first seen on player %d's GUI input", id, couchInput ? 2 : 1);
	}
}

template <typename T>
T Field(const void* base, size_t offset)
{
	return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset);
}

void* Players()
{
	void* game = *reinterpret_cast<void**>(addr::Rebase(addr::kGameRoot));
	void* inner = game ? Field<void*>(game, 0x0c) : nullptr;
	return inner ? Field<void*>(inner, 0x40) : nullptr;
}

void* PlayerOfType(uintptr_t getter)
{
	void* players = Players();
	return players ? reinterpret_cast<GetPlayerFn>(addr::Rebase(getter))(players, nullptr) : nullptr;
}

void* GameInput()
{
	return reinterpret_cast<PtrFn>(addr::Rebase(addr::kGetInput))();
}

int CurrentGameMode()
{
	void* game = *reinterpret_cast<void**>(addr::Rebase(addr::kGameRoot));
	void* inner = game ? Field<void*>(game, 0x0c) : nullptr;
	void* modes = inner ? Field<void*>(inner, addr::kGameModeManager) : nullptr;
	if (!modes) {
		return -1;
	}
	void* mode = reinterpret_cast<void*(__fastcall*)(void*, void*)>(addr::Rebase(addr::kCurrentGameMode))(modes, nullptr);
	if (!mode) {
		return -1;
	}
	auto id = reinterpret_cast<int(__fastcall*)(void*, void*)>((*reinterpret_cast<void***>(mode))[1]);
	return id(mode, nullptr);
}

void __fastcall ActionDetour(void* self, void* edx, const int* action)
{
	const bool couchInput = self == g_couchGuiInput && self != nullptr;
	if (!couchInput) {
		g_mainGuiInput = self;
	}
	const int id = action[0];
	const bool stick = id == addr::kActionLeftStick || id == addr::kActionRightStick;
	if (!stick) {
		// Whoever pressed a button last owns the next menu that opens (see UpdateCouchGuiInput).
		(couchInput ? g_lastCouchButton : g_lastMainButton) = GetTickCount();
		NoteActionId(couchInput, id);
	}
	if (couchInput && id == addr::kActionOpenMap) {
		// Player 2's Back should only teleport them to player 1 (its other action); the world map
		// is shared and would open for player 1.
		return;
	}
	g_originalAction(self, edx, action);
}

void Register(void* couch)
{
	void* input = GameInput();
	if (!input) {
		return;
	}
	if (!g_couchGuiInput) {
		void* mem = reinterpret_cast<AllocFn>(addr::Rebase(addr::kAlloc))(addr::kGuiInputSize);
		if (!mem) {
			return;
		}
		g_couchGuiInput = reinterpret_cast<CtorFn>(addr::Rebase(addr::kGuiInputCtor))(mem);
	}
	reinterpret_cast<ThisArgFn>(addr::Rebase(addr::kGuiInputSetPlayer))(g_couchGuiInput, nullptr, couch);
	reinterpret_cast<ThisArgFn>(addr::Rebase(addr::kInputAddProcess))(input, nullptr, g_couchGuiInput);
	g_registeredWith = input;
	Log("couch GUI input %p registered for player 2 (CPlayer %p, user %d) with input %p", g_couchGuiInput, couch,
		Field<int>(couch, addr::kPlayerUser), input);
}

void Unregister(const char* why)
{
	// Only remove it from the input object it was added to; if that object has been
	// replaced (level change), the old process list went with it.
	if (g_registeredWith && g_registeredWith == GameInput()) {
		reinterpret_cast<ThisArgFn>(addr::Rebase(addr::kInputRemoveProcess))(g_registeredWith, nullptr, g_couchGuiInput);
	}
	// Keep the object itself for reuse: the game may still hold a pointer this frame.
	*reinterpret_cast<void**>(static_cast<uint8_t*>(g_couchGuiInput) + addr::kGuiInputPlayer) = nullptr;
	g_registeredWith = nullptr;
	Log("couch GUI input unregistered (%s)", why);
}

}  // namespace

bool IsGuiOpen()
{
	const int mode = CurrentGameMode();
	return mode == addr::kModePauseMenu || mode == addr::kModeSanctuary;
}

bool CouchPlayerHasMenu()
{
	return g_couchMenu;
}

GuiOwner CurrentGuiOwner()
{
	return g_owner;
}

bool InstallGuiHooks()
{
	auto* slot = reinterpret_cast<uintptr_t*>(addr::Rebase(addr::kGuiInputActionSlot));
	if (*slot != addr::Rebase(addr::kGuiInputAction)) {
		Log("GUI hooks: unexpected CGUIInput action slot value %p", reinterpret_cast<void*>(*slot));
		return false;
	}
	DWORD oldProtect;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		return false;
	}
	g_originalAction = reinterpret_cast<ActionFn>(*slot);
	*slot = reinterpret_cast<uintptr_t>(&ActionDetour);
	VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
	Log("GUI hooks installed");
	return true;
}

void UpdateCouchGuiInput()
{
	const int mode = CurrentGameMode();
	if (mode != g_lastMode) {
		Log("game mode %d -> %d", g_lastMode, mode);
		g_lastMode = mode;
	}
	const bool open = IsGuiOpen();
	if (open != g_lastOpen) {
		if (open) {
			const DWORD now = GetTickCount();
			const bool couchRecent = g_lastCouchButton != 0 && now - g_lastCouchButton < 1000;
			const bool couchLatest = now - g_lastCouchButton < now - g_lastMainButton;
			g_owner = couchRecent && (couchLatest || g_lastMainButton == 0) ? GuiOwner::CouchPlayer : GuiOwner::MainPlayer;
		}
		else {
			g_owner = GuiOwner::None;
		}
		Log("GUI %s%s", open ? "opened" : "closed",
			!open ? "" : g_owner == GuiOwner::CouchPlayer ? " by player 2" : " by player 1");
		g_lastOpen = open;
	}
	g_couchMenu = open && g_owner == GuiOwner::CouchPlayer;

	void* couch = reinterpret_cast<int(__cdecl*)()>(addr::Rebase(addr::kGetMode))() == 1
		? PlayerOfType(addr::kCouchPlayer)
		: nullptr;
	if (g_registeredWith) {
		if (!couch) {
			Unregister("player 2 left");
		}
		else if (g_registeredWith != GameInput()) {
			Log("couch GUI input: game input object changed, re-registering");
			g_registeredWith = nullptr;
		}
		else if (Field<void*>(g_couchGuiInput, addr::kGuiInputPlayer) != couch) {
			Unregister("player 2 changed");
		}
	}
	if (couch && !g_registeredWith) {
		Register(couch);
	}
}

int GameModeId()
{
	return CurrentGameMode();
}
