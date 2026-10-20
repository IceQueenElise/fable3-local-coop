// xinput1_3.dll exports, forwarded to the system XInput.

#include "proxy.h"

#include "gui.h"
#include "input.h"
#include "log.h"

namespace {

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using SetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
using EnableFn = void(WINAPI*)(BOOL);
using GetDSoundGuidsFn = DWORD(WINAPI*)(DWORD, GUID*, GUID*);
using GetBatteryFn = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
using GetKeystrokeFn = DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE);

HMODULE g_real = nullptr;
GetStateFn g_getState = nullptr;
GetStateFn g_getStateEx = nullptr;
SetStateFn g_setState = nullptr;
GetCapabilitiesFn g_getCapabilities = nullptr;
EnableFn g_enable = nullptr;
GetDSoundGuidsFn g_getDSoundGuids = nullptr;
GetBatteryFn g_getBattery = nullptr;
GetKeystrokeFn g_getKeystroke = nullptr;

// Logs the first call for each (export, user index) pair so the log shows which pads the game polls.
void NoteCall(const char* name, int slot, DWORD user)
{
	static volatile LONG seen[3][8];
	if (user < 8 && InterlockedExchange(&seen[slot][user], 1) == 0) {
		Log("%s first called for user %lu (-> pad %lu)", name, user, RemapUserIndex(user));
	}
}

volatile bool g_pickerPadSwap = false;
volatile DWORD g_startPressedAt[XUSER_MAX_COUNT];
bool g_startDown[XUSER_MAX_COUNT];
volatile DWORD g_backPressedAt[XUSER_MAX_COUNT];
bool g_backDown[XUSER_MAX_COUNT];

void TrackStart(DWORD pad, const XINPUT_STATE* state)
{
	if (pad >= XUSER_MAX_COUNT) {
		return;
	}
	bool down = (state->Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
	if (down && !g_startDown[pad]) {
		g_startPressedAt[pad] = GetTickCount();
	}
	g_startDown[pad] = down;
	const bool backDown = (state->Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
	if (backDown && !g_backDown[pad]) {
		g_backPressedAt[pad] = GetTickCount();
	}
	g_backDown[pad] = backDown;
}

}  // namespace

DWORD LastStartPress(DWORD pad)
{
	return pad < XUSER_MAX_COUNT ? g_startPressedAt[pad] : 0;
}

DWORD LastBackPress(DWORD pad)
{
	return pad < XUSER_MAX_COUNT ? g_backPressedAt[pad] : 0;
}

void SetPickerPadSwap(bool on)
{
	if (g_pickerPadSwap != on) {
		Log("picker: %s", on ? "player 2's pad drives player 1's input" : "pads restored");
	}
	g_pickerPadSwap = on;
}

bool real::Load()
{
	// GetSystemDirectory resolves to SysWOW64 for this 32-bit process.
	wchar_t dir[MAX_PATH];
	GetSystemDirectoryW(dir, MAX_PATH);
	static const wchar_t* const kNames[] = {L"\\xinput1_3.dll", L"\\xinput1_4.dll", L"\\xinput9_1_0.dll"};
	for (const wchar_t* name : kNames) {
		wchar_t path[MAX_PATH];
		wcscpy_s(path, dir);
		wcscat_s(path, name);
		g_real = LoadLibraryW(path);
		if (g_real) {
			Log("real XInput: %ls", path);
			break;
		}
	}
	if (!g_real) {
		Log("ERROR: no system XInput found");
		return false;
	}
	g_getState = reinterpret_cast<GetStateFn>(GetProcAddress(g_real, "XInputGetState"));
	g_getStateEx = reinterpret_cast<GetStateFn>(GetProcAddress(g_real, MAKEINTRESOURCEA(100)));
	g_setState = reinterpret_cast<SetStateFn>(GetProcAddress(g_real, "XInputSetState"));
	g_getCapabilities = reinterpret_cast<GetCapabilitiesFn>(GetProcAddress(g_real, "XInputGetCapabilities"));
	g_enable = reinterpret_cast<EnableFn>(GetProcAddress(g_real, "XInputEnable"));
	g_getDSoundGuids = reinterpret_cast<GetDSoundGuidsFn>(GetProcAddress(g_real, "XInputGetDSoundAudioDeviceGuids"));
	g_getBattery = reinterpret_cast<GetBatteryFn>(GetProcAddress(g_real, "XInputGetBatteryInformation"));
	g_getKeystroke = reinterpret_cast<GetKeystrokeFn>(GetProcAddress(g_real, "XInputGetKeystroke"));
	return g_getState && g_setState && g_getCapabilities;
}

DWORD real::GetState(DWORD user, XINPUT_STATE* state)
{
	return g_getState ? g_getState(user, state) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI Proxy_XInputGetState(DWORD user, XINPUT_STATE* state)
{
	NoteCall("XInputGetState", 0, user);
	DWORD pad = RemapUserIndex(user);
	if (g_pickerPadSwap && pad == kExtraPad) {
		XINPUT_STATE physical;
		if (real::GetState(kExtraPad, &physical) == ERROR_SUCCESS) {
			TrackStart(kExtraPad, &physical);
		}
		ZeroMemory(state, sizeof(*state));
		return ERROR_SUCCESS;
	}
	if (g_pickerPadSwap && pad == 0) {
		pad = kExtraPad;
	}
	DWORD result = real::GetState(pad, state);
	if (result == ERROR_SUCCESS) {
		TrackStart(pad, state);
		// The menus read player 1's thumbsticks; while player 2 has a menu open, the two
		// pads trade thumbsticks so player 2 can navigate it and player 1 can't.
		if ((pad == 0 || pad == kExtraPad) && CouchPlayerHasMenu()) {
			XINPUT_STATE other;
			const bool otherConnected = real::GetState(pad == 0 ? kExtraPad : 0, &other) == ERROR_SUCCESS;
			state->Gamepad.sThumbLX = otherConnected ? other.Gamepad.sThumbLX : 0;
			state->Gamepad.sThumbLY = otherConnected ? other.Gamepad.sThumbLY : 0;
			state->Gamepad.sThumbRX = otherConnected ? other.Gamepad.sThumbRX : 0;
			state->Gamepad.sThumbRY = otherConnected ? other.Gamepad.sThumbRY : 0;
		}
	}
	return result;
}

extern "C" DWORD WINAPI Proxy_XInputSetState(DWORD user, XINPUT_VIBRATION* vibration)
{
	NoteCall("XInputSetState", 1, user);
	return g_setState ? g_setState(RemapUserIndex(user), vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI Proxy_XInputGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
{
	NoteCall("XInputGetCapabilities", 2, user);
	return g_getCapabilities ? g_getCapabilities(RemapUserIndex(user), flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" void WINAPI Proxy_XInputEnable(BOOL enable)
{
	if (g_enable) {
		g_enable(enable);
	}
}

extern "C" DWORD WINAPI Proxy_XInputGetDSoundAudioDeviceGuids(DWORD user, GUID* render, GUID* capture)
{
	return g_getDSoundGuids ? g_getDSoundGuids(RemapUserIndex(user), render, capture) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI Proxy_XInputGetBatteryInformation(DWORD user, BYTE type, XINPUT_BATTERY_INFORMATION* info)
{
	return g_getBattery ? g_getBattery(RemapUserIndex(user), type, info) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI Proxy_XInputGetKeystroke(DWORD user, DWORD reserved, PXINPUT_KEYSTROKE keystroke)
{
	return g_getKeystroke ? g_getKeystroke(RemapUserIndex(user), reserved, keystroke) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI Proxy_XInputGetStateEx(DWORD user, XINPUT_STATE* state)
{
	GetStateFn fn = g_getStateEx ? g_getStateEx : g_getState;
	return fn ? fn(RemapUserIndex(user), state) : ERROR_DEVICE_NOT_CONNECTED;
}
