#pragma once

#include <cstdint>

// Fable3.exe, Steam build, title version 10000283. Addresses are Ghidra VAs at the
// default image base; the exe is DYNAMICBASE, so rebase them with addr::Rebase.
// Evidence for each entry: docs/findings.md.
namespace addr {

// Set at startup to the module base of Fable3.exe.
inline uintptr_t g_base = 0x00400000;

inline uintptr_t Rebase(uintptr_t va)
{
	return g_base + (va - 0x00400000);
}

constexpr uint32_t kTimestamp = 1305884087;  // IMAGE_FILE_HEADER.TimeDateStamp

// Game allocator: void* __cdecl (size_t).
constexpr uintptr_t kAlloc = 0x01a035c0;

// NInput::CJoystickXBox360 ctor: ECX = this (0xA0 bytes), EAX = device id,
// push input manager; RET 4; returns this.
constexpr uintptr_t kJoystickCtor = 0x0188c730;
constexpr size_t kJoystickSize = 0xa0;

// CInputManager::AddDevice: ECX = manager, EDI = device; RET.
constexpr uintptr_t kAddDevice = 0x0188ef00;

// CInputManager::RemoveDevice: ESI = manager, push device id; RET 4.
constexpr uintptr_t kRemoveDevice = 0x0188ee70;

// CInputManagerDX vtable slot holding the joystick hot-plug check (FUN_0188dcf0).
constexpr uintptr_t kHotplugSlot = 0x01bf91a8;
constexpr uintptr_t kHotplugFn = 0x0188dcf0;

// Instruction checked before patching: XOR EAX,EAX (pad 0 hard-coded) in the hot-plug check.
constexpr uintptr_t kHotplugXorEax = 0x0188dd66;

// CInputManager layout.
constexpr size_t kMgrDevicesBegin = 0x0c;  // std::vector of 8-byte entries, device* first
constexpr size_t kMgrDevicesEnd = 0x10;
constexpr size_t kMgrLock = 0x4c;  // CRITICAL_SECTION

// CInputDevice layout.
constexpr size_t kDeviceId = 0x04;  // joystick 0, keyboard 1, mouse 2; doubles as XInput user index

// NInput event (0x48 bytes), built in FUN_0188c110 for XInput pads.
constexpr size_t kEventSize = 0x48;
constexpr size_t kEventButton = 0x08;   // byte; XInput START -> 5
constexpr size_t kEventUser = 0x28;     // id of the device that produced the event
constexpr size_t kEventType = 0x2c;
constexpr size_t kEventHandled = 0x38;  // byte
constexpr int kEventButtonDown = 0xd;
constexpr uint8_t kButtonStart = 5;

// Press-Start couch-join detector FUN_00b9b0c0: thiscall(this, event), RET 4, via this vtable slot.
constexpr uintptr_t kDetectorSlot = 0x01b1a26c;
constexpr uintptr_t kDetectorFn = 0x00b9b0c0;
constexpr uintptr_t kDetectorGate = 0x00b9ac50;  // bool __thiscall(detector): in gameplay with a main player

// NControlSystem binding types: slot 3 is bool __thiscall Matches(event, user), RET 8.
// Both require event+0x28 == user (FUN_01358850, FUN_013588e0).
constexpr uintptr_t kPadButtonMatchesSlot = 0x01b645f8 + 0x0c;  // CInputTypeXboxPadButtonEvent
constexpr uintptr_t kPadButtonMatches = 0x01358850;
constexpr uintptr_t kLeftStickMatchesSlot = 0x01b1b4d8 + 0x0c;  // CInputTypeXboxPadLeftStickEvent
constexpr uintptr_t kLeftStickMatches = 0x013588e0;

// Input ingest FUN_00d89de0: per event, "MOV [ECX+0x28],EBP; MOV [ESP+0x30],ECX" with EBP == 0
// forces the user to 0; EBP is then also the per-user event-group key (00d89e7c).
constexpr uintptr_t kIngestZeroUser = 0x00d89e70;
constexpr uint8_t kIngestZeroUserBytes[] = {0x89, 0x69, 0x28, 0x89, 0x4c, 0x24, 0x30};

// CPlayer (FUN_00bbec20): +0x30 type, +0x34 user index.
constexpr size_t kPlayerUser = 0x34;

// Sign-in cache: *(*(kNetRoot) + 0x28) is an object whose vector [+0x14, +0x18) holds one
// 0x20-byte entry per XUser index. FUN_00772190 refreshes only entry 0 on PC.
constexpr uintptr_t kNetRoot = 0x01dc2550;
constexpr size_t kNetRootSignin = 0x28;
constexpr size_t kSigninEntriesBegin = 0x14;
constexpr size_t kSigninEntriesEnd = 0x18;
constexpr size_t kSigninEntrySize = 0x20;
constexpr size_t kSigninOnline = 0x00;      // bool: XUserSigninState == 2
constexpr size_t kSigninLocal = 0x01;       // bool: XUserSigninState == 1
constexpr size_t kSigninGuest = 0x02;       // XUSER_INFO_FLAG_GUEST
constexpr size_t kSigninXuid = 0x08;        // online XUID if any, else offline
constexpr size_t kSigninOfflineXuid = 0x10;
constexpr size_t kSigninOnlineXuid = 0x18;

constexpr uintptr_t kCouchJoinAllowed = 0x01c86c16;  // byte, defaults to 1
constexpr uintptr_t kIsInLiveGame = 0x006a87a0;      // bool __cdecl()
constexpr uintptr_t kGetMode = 0x00673cb0;           // int __cdecl(); 1 = couch

}  // namespace addr
