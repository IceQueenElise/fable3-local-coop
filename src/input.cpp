#include "input.h"

#include <cstdint>

#include "addresses.h"
#include "join.h"
#include "log.h"
#include "proxy.h"

namespace {

using HotplugFn = void(__fastcall*)(void* manager, void* edx);

HotplugFn g_originalHotplug = nullptr;
DWORD g_lastPoll = 0;
bool g_lastConnected = false;

template <typename T>
T& Field(void* base, size_t offset)
{
	return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

bool HasDevice(void* manager, DWORD id)
{
	auto* lock = &Field<CRITICAL_SECTION>(manager, addr::kMgrLock);
	EnterCriticalSection(lock);
	bool found = false;
	for (auto** it = Field<void**>(manager, addr::kMgrDevicesBegin);
		 it != Field<void**>(manager, addr::kMgrDevicesEnd); it += 2) {
		if (*it && Field<DWORD>(*it, addr::kDeviceId) == id) {
			found = true;
			break;
		}
	}
	LeaveCriticalSection(lock);
	return found;
}

void* CreateJoystick(void* manager, DWORD id)
{
	auto alloc = reinterpret_cast<void*(__cdecl*)(size_t)>(addr::Rebase(addr::kAlloc));
	void* mem = alloc(addr::kJoystickSize);
	if (!mem) {
		return nullptr;
	}
	void* device;
	const uintptr_t ctor = addr::Rebase(addr::kJoystickCtor);
	__asm {
		mov ecx, mem
		mov eax, id
		push manager
		call ctor
		mov device, eax
	}
	return device;
}

void AddDevice(void* manager, void* device)
{
	const uintptr_t fn = addr::Rebase(addr::kAddDevice);
	__asm {
		mov ecx, manager
		mov edi, device
		call fn
	}
}

void RemoveDevice(void* manager, DWORD id)
{
	const uintptr_t fn = addr::Rebase(addr::kRemoveDevice);
	__asm {
		mov esi, manager
		push id
		call fn
	}
}

// Same job as the game's check for pad 0, done for the extra pad.
void HotplugExtraPad(void* manager)
{
	// XInputGetState on an empty slot is slow, so don't poll it every call.
	DWORD now = GetTickCount();
	if (now - g_lastPoll < 1000) {
		return;
	}
	g_lastPoll = now;

	XINPUT_STATE state;
	bool connected = real::GetState(kExtraPad, &state) == ERROR_SUCCESS;
	bool present = HasDevice(manager, kExtraDeviceId);
	if (connected != g_lastConnected) {
		Log("pad %lu %s", kExtraPad, connected ? "connected" : "disconnected");
		g_lastConnected = connected;
	}
	if (connected && !present) {
		void* device = CreateJoystick(manager, kExtraDeviceId);
		if (device) {
			AddDevice(manager, device);
		}
		Log("added joystick device %p (id %lu -> pad %lu), registered=%d", device, kExtraDeviceId, kExtraPad,
			HasDevice(manager, kExtraDeviceId));
	}
	else if (!connected && present) {
		RemoveDevice(manager, kExtraDeviceId);
		Log("removed joystick device id %lu", kExtraDeviceId);
	}
}

void __fastcall HotplugDetour(void* manager, void* edx)
{
	static volatile LONG calls = 0;
	LONG n = InterlockedIncrement(&calls);
	if (n <= 3 || n % 1000 == 0) {
		Log("hot-plug check #%ld, manager %p", n, manager);
	}
	g_originalHotplug(manager, edx);
	HotplugExtraPad(manager);
	// Runs every frame on the game thread, the same thread as the GUI input handlers.
	PumpExtraPadJoin();
}

bool IsExpectedBuild()
{
	auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
	auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(dos) + dos->e_lfanew);
	if (nt->FileHeader.TimeDateStamp != addr::kTimestamp) {
		Log("unexpected exe timestamp %lu (want %lu)", nt->FileHeader.TimeDateStamp, addr::kTimestamp);
		return false;
	}
	addr::g_base = reinterpret_cast<uintptr_t>(dos);
	Log("Fable3.exe base %p", dos);
	const auto* xorEax = reinterpret_cast<const uint8_t*>(addr::Rebase(addr::kHotplugXorEax));
	if (xorEax[0] != 0x33 || xorEax[1] != 0xc0) {
		Log("unexpected bytes at hot-plug check: %02x %02x", xorEax[0], xorEax[1]);
		return false;
	}
	// The loader has already applied relocations, so the slot holds the rebased function address.
	auto slot = *reinterpret_cast<uintptr_t*>(addr::Rebase(addr::kHotplugSlot));
	if (slot != addr::Rebase(addr::kHotplugFn)) {
		Log("unexpected vtable slot value %p", reinterpret_cast<void*>(slot));
		return false;
	}
	return true;
}

}  // namespace

DWORD RemapUserIndex(DWORD user)
{
	return user == kExtraDeviceId ? kExtraPad : user;
}

bool InstallInputHooks()
{
	if (!IsExpectedBuild()) {
		Log("Fable3.exe build not recognised; mod disabled (XInput passthrough only)");
		return false;
	}
	auto* slot = reinterpret_cast<uintptr_t*>(addr::Rebase(addr::kHotplugSlot));
	DWORD oldProtect;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		Log("VirtualProtect failed: %lu", GetLastError());
		return false;
	}
	g_originalHotplug = reinterpret_cast<HotplugFn>(*slot);
	*slot = reinterpret_cast<uintptr_t>(&HotplugDetour);
	VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
	Log("hooked CInputManagerDX hot-plug check (slot %p)", slot);
	return true;
}
