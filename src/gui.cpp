#include "gui.h"

#include <windows.h>

#include <cstdint>

#include "addresses.h"
#include "log.h"

namespace {

using GetPlayerFn = void*(__fastcall*)(void* players, void* edx);
using PtrFn = void*(__cdecl*)();
using BoolFn = bool(__cdecl*)();
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

void __fastcall ActionDetour(void* self, void* edx, const int* action)
{
	const bool couchInput = self == g_couchGuiInput && self != nullptr;
	if (!couchInput) {
		g_mainGuiInput = self;
	}
	const int id = action[0];
	if (id == addr::kActionOpenGui && !IsGuiOpen()) {
		g_owner = couchInput ? GuiOwner::CouchPlayer : GuiOwner::MainPlayer;
	}
	const bool stick = id == addr::kActionLeftStick || id == addr::kActionRightStick;
	if (stick && g_owner == GuiOwner::CouchPlayer && IsGuiOpen()) {
		if (!couchInput) {
			return;  // player 1's sticks stay out of player 2's menu
		}
		if (g_mainGuiInput) {
			g_originalAction(g_mainGuiInput, edx, action);  // the menus read the main GUI input's sticks
		}
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
	return reinterpret_cast<BoolFn>(addr::Rebase(addr::kIsGuiOpen))();
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
	const bool open = IsGuiOpen();
	if (open != g_lastOpen) {
		if (!open) {
			g_owner = GuiOwner::None;
		}
		Log("GUI %s%s", open ? "opened" : "closed",
			!open ? "" : g_owner == GuiOwner::CouchPlayer ? " by player 2" : " by player 1");
		g_lastOpen = open;
	}

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
