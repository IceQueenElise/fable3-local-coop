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

// Raw-event feeder for input processes (FUN_00b6d020, feeds NUI::CGUIInput): in gameplay it
// skips pad events (event+0x24 == 1) whose user isn't the main player's.
//   MOV ECX,[ESI+0x28]; CMP ECX,[EAX+0x34]; JNZ kGuiFilterSkip
constexpr uintptr_t kGuiFilter = 0x00b6d085;
constexpr uint8_t kGuiFilterBytes[] = {0x8b, 0x4e, 0x28, 0x3b, 0x48, 0x34, 0x75, 0x1c};
constexpr uintptr_t kGuiFilterSkip = 0x00b6d0a9;

// "Couch player can enter GUI" flag, checked by the GUI-open path FUN_007d7000 for
// non-main players: byte at *(*(*(kGuiRoot) + 0x1c) + 0x58) + 0x8d. Lua: SetCanCouchPlayerEnterGUI.
constexpr uintptr_t kGuiRoot = 0x01dc2558;
constexpr size_t kGuiRootSession = 0x1c;
// Menu box (Lua DisplayMenuBox, FUN_00ebaef0) = [[*kGuiRoot + 0x18] + 0x38]. Begin with a title,
// add up to 6 entries, show; selecting or cancelling closes it and posts the choice.
constexpr size_t kGuiRootMenuHolder = 0x18;
constexpr size_t kMenuHolderMenuBox = 0x38;
constexpr uintptr_t kMenuBoxBegin = 0x00d7eb00;  // void __thiscall(menu, const string* title)
constexpr uintptr_t kMenuBoxAdd = 0x00d7f7a0;    // void __thiscall(menu, const string* text)
constexpr uintptr_t kMenuBoxShow = 0x00d7f1c0;   // bool __thiscall(menu)
constexpr size_t kMenuBoxMaxEntries = 6;
constexpr size_t kMenuBoxIndex = 0x00;       // int, highlighted entry
constexpr size_t kMenuBoxOpen = 0x0c;        // bool
constexpr size_t kMenuBoxCancelled = 0x0d;   // bool, set when closed by cancel
constexpr size_t kMenuBoxCancelable = 0x0e;  // bool
constexpr size_t kMenuBoxScreen = 0x08;       // GUI screen while shown
constexpr size_t kMenuBoxEntriesBegin = 0x1c;
constexpr size_t kMenuBoxEntriesEnd = 0x20;
constexpr size_t kMenuHolderBoxList = 0x34;     // display box list (NUI::CBoxListBase) beside the menu box
constexpr size_t kDisplayBoxesCurrent = 0x20;   // list: current box
// bool __thiscall(box list): a display box is up; the menu box show then refuses and clears.
constexpr uintptr_t kDisplayBoxBusy = 0x00bbd3f0;
constexpr size_t kGuiRootScreens = 0x34;        // GUI screen table: screen n at +8 + 4n (FUN_00d2f660)
constexpr size_t kScreensFirst = 0x08;
constexpr size_t kScreensShared = 0xa0;         // shared slot, FUN_00d30040
constexpr size_t kScreensSharedId = 0x9c;       // id of the screen in the shared slot
constexpr int kMenuBoxScreenId = 5;
constexpr int kOptionsScreenId = 0x11;       // Options.bgf
// Show/hide a screen element by dotted path from "Layer" (segments hashed h*0x1003F + c by
// FUN_00407be0): void __cdecl(const string* path, screen, char visible).
constexpr uintptr_t kSetElementVisible = 0x01015c30;
constexpr uintptr_t kFindElement = 0x01015300;      // element* __cdecl(string path (by value, destroyed), parent or 0, screen)
constexpr uintptr_t kIsElementVisible = 0x01014630; // char __cdecl(element, screen)
constexpr uintptr_t kSetElementText = 0x010142d0;
// Element opacity on a screen's presentation (negative = hidden): read FUN_01014440
// float __cdecl(element, screen, char raw); write FUN_010143b0 void __cdecl(element, screen, float).
constexpr uintptr_t kGetElementOpacity = 0x01014440;
constexpr uintptr_t kWideStringDestroy = 0x019ffa70; // void __thiscall(wstring*)
// GUI screen loader FUN_00d30040: void __thiscall(screens, int id); ids map to .bgf files
// (0 MessageBoxMulti, 4 DisplayBox, 5 MenuBox, 7 QueryBox, 0x10 BedMenuBox, 0x11 Options, ...).
constexpr uintptr_t kScreenLoad = 0x00d30040;
// Run a Gameface Lua chunk on a loaded screen (the game's own scriptlets, e.g.
// "setAttribute(getElement('Scene.Layer'), 'opacity', 0)"): void __cdecl(const string* script,
// screen). It may queue the chunk instead of running it at once (screen manager byte +0x94 clear).
// The chunk sees the Gameface element API (FUN_019fc750): getElement, getChildren, get/set/
// hasAttribute, clone(src, name[, parent]) (name hashed like native paths), goToSlide, ...
constexpr uintptr_t kRunScreenLua = 0x010146d0;
// Pointer to Gameface's log sink, void __cdecl(int level, const char* message): Lua output() lines
// and script errors (level 0 message, 1 warning, 2 error, 3 critical). Default: OutputDebugString.
constexpr uintptr_t kLuaLogCallback = 0x01dadbb4;
// Game string from text: string* __thiscall(string*, const char* text, int length or -1).
constexpr uintptr_t kStringFromText = 0x01a01cc0;
// Wide game string from text: wstring* __thiscall(wstring*, const wchar_t* text, int length or -1).
constexpr uintptr_t kWideStringAssign = 0x01a00310;
// Options menu (NUI::COptionsMenuBox, display box type 0xF on Options.bgf, GUI screen 0x11).
// Opened by FUN_00bbd860 void __thiscall(box list, CEntity* owner, char inGame), RET 8, on
// the box list [[*kGuiRoot + 0x18] + 0x34] (Lua OpenOptionsScreen). Box vtable slot 2 closes it.
// (Its selection, highlight setter and page/entry fields are in findings R36.)
constexpr uintptr_t kOpenOptionsMenu = 0x00bbd860;
constexpr size_t kGuiRootMenuLock = 0x120;  // int; the opener does nothing while > 0
constexpr uintptr_t kOptionsMenuVtable = 0x01b1e494;
constexpr size_t kBoxCloseSlot = 2;
// Input handler FUN_00bf0ab0, char __thiscall(box, const int* action, int player), RET 8: up
// 0x1e/0x26/0x2e, down 0x1f/0x27/0x2f, select 0x5d/0x5f, back 0x60, close 0xe, mouse hover 0x1d.
constexpr size_t kOptionsInputSlot = 4;
// FUN_0135d610 int __cdecl(int action, int player) turns a stick action (kActionLeftStick /
// kActionRightStick) into a direction action (0x1e up, 0x1f down, ...) using the stick's position
// and dead zone, as the Options input handler does.
constexpr uintptr_t kStickAction = 0x0135d610;
constexpr size_t kSessionCouchFlags = 0x58;
constexpr size_t kCanCouchPlayerEnterGui = 0x8d;

// Game root and players: players manager = [[*kGameRoot + 0xc] + 0x40] (FUN_00673c50, FUN_00673f80).
// FUN_006b7310 / FUN_006b7320: CPlayer* __thiscall(players) for player type 1 (main) / 2 (couch).
constexpr uintptr_t kGameRoot = 0x01dbdd4c;
constexpr uintptr_t kMainPlayer = 0x006b7310;
constexpr uintptr_t kCouchPlayer = 0x006b7320;

// NUI::CGUIInput (the detector's this), an input process: +8 is the CPlayer whose controls it
// takes; its process slot (FUN_00b9ac30) is that player's type. The PC build creates one
// (FUN_00b9c760) and FUN_006acd70 always binds it to player type 1. Its START action opens the
// pause menu / Sanctuary; with player type 2 its Back action teleports that hero (FUN_00b9be30).
constexpr size_t kGuiInputPlayer = 0x08;
constexpr size_t kGuiInputSize = 0x48;
constexpr uintptr_t kGuiInputCtor = 0x00b9ab30;       // CGUIInput* __fastcall(void* mem)
constexpr uintptr_t kGuiInputSetPlayer = 0x00b9abb0;  // void __thiscall(CPlayer*), RET 4

// Game input object (FUN_00b6cad0: void* __cdecl()) and its process list.
constexpr uintptr_t kGetInput = 0x00b6cad0;
constexpr uintptr_t kInputAddProcess = 0x00b6d4e0;     // void __thiscall(input, process), RET 4
constexpr uintptr_t kInputRemoveProcess = 0x00b6d0d0;  // void __thiscall(input, process), RET 4

// Game mode stack: manager = [[*kGameRoot + 0xc] + 0x30] (FUN_00673f70); FUN_006e2bb0
// (void* __thiscall(manager)) returns the current mode object, whose vfunc+4 returns its id.
// Id 1 is the Sanctuary GUI (FUN_00bb0ec0); the early-game pause menu is a different mode.
constexpr size_t kGameModeManager = 0x30;
constexpr uintptr_t kCurrentGameMode = 0x006e2bb0;

// CGUIInput action handler (vtable slot 1, FUN_00b9bfd0): void __thiscall(const int* action),
// RET 4. action[0] is the action id; sticks store action[1..2] into the GUI input.
constexpr uintptr_t kGuiInputActionSlot = 0x01b1a254 + 0x04;
constexpr uintptr_t kGuiInputAction = 0x00b9bfd0;
constexpr int kActionOpenPause = 0x0e;  // START: opens the pause menu (mode 8)
constexpr int kActionLeftStick = 0x1b;
constexpr int kActionRightStick = 0x1c;

// Thumbstick state CGUIInput stores for the menus (FUN_00b9b4a0/FUN_00b9b240, FUN_00b9b380).
constexpr size_t kGuiInputLeftMoved = 0x04;   // byte
constexpr size_t kGuiInputRightMoved = 0x05;  // byte
constexpr size_t kGuiInputLeftStick = 0x1c;   // float x, y
constexpr size_t kGuiInputRightStick = 0x24;  // float x, y

// Game modes (CurrentGameMode): 6 gameplay, 8 pause menu, 1 Sanctuary GUI.
constexpr int kModeSanctuary = 1;
constexpr int kModePauseMenu = 8;

// Player-interaction prompt (FUN_01133be0): RB calls XShowGamerCardUI (xlive #5252,
// DWORD WINAPI(DWORD user, ULONGLONG xuid)) through its import thunk.
constexpr uintptr_t kXShowGamerCardUIThunk = 0x017d1afc;
constexpr uintptr_t kInteractionGamerCardCalls[] = {0x01135268, 0x011352c8};
// Save manager. Paths: "<Saved Games>\Lionhead Studios\Fable 3\" (FUN_0067c950) + "<XUID>\"
// (FUN_00747b20), with the XUID always from XUserGetSigninInfo(0, ...). These are every save
// manager call to XUserGetSigninInfo (import thunk below); the sign-in cache (FUN_00772190,
// FUN_00771e00) and CPlayer (FUN_00bbec20) calls are left alone.
constexpr uintptr_t kXUserGetSigninInfoThunk = 0x017d1a90;
constexpr uintptr_t kSaveSigninInfoCalls[] = {0x00747b71, 0x00747b8c, 0x0074971e, 0x007477d1, 0x0074791d};
// Save enumerator: __thiscall(mgr, int user, list, unsigned, char), RET 0x10, and its calls.
constexpr uintptr_t kEnumerateSaves = 0x007515a0;
constexpr uintptr_t kEnumerateSavesCalls[] = {0x007518cd, 0x007527e6, 0x00bbfbe0, 0x0071e04a, 0x007525bc};
constexpr size_t kSaveListBegin = 0x08;  // save name list: vector of game strings, newest first
constexpr size_t kSaveListEnd = 0x0c;
// Save info read FUN_0074b4b0 (-> FUN_0074ae10): void __thiscall(?, const string* name, int user,
// SaveInfo*), RET 0xC; the join calls it at kJoinReadSaveInfoCall and loads the first save whose
// info is usable. Only hero-only saves (no mainsave/entityuid file) are marked usable.
constexpr uintptr_t kReadSaveInfo = 0x0074b4b0;
constexpr uintptr_t kJoinReadSaveInfoCall = 0x00bbfc19;
constexpr size_t kSaveInfoUsable = 0x68;    // bool
constexpr size_t kSaveInfoType = 0x6c;      // 2 = hero only
constexpr size_t kSaveInfoHeroOnly = 0x70;  // bool
// Save writer for a player: __thiscall(mgr, name, int playerType, int, char), RET 0x10.
constexpr uintptr_t kSaveForPlayer = 0x00751dc0;
constexpr uintptr_t kSaveForPlayerCalls[] = {0x007532dc, 0x00753ce5};
// Save manager = [*kSaveRoot + 0xa8] (FUN_00619a90, FUN_0061a2d0); this of FUN_00753010 /
// FUN_00751dc0. Strings are a pointer to a refcounted rep (+4 length, +0xc refcount):
// copy-construct FUN_01a012c0, assign FUN_01a01270 (both __thiscall(dst, src*)), destroy
// FUN_01a01320 (__fastcall(str)).
constexpr uintptr_t kSaveRoot = 0x01dbd9dc;
constexpr size_t kSaveRootManager = 0xa8;
constexpr uintptr_t kStringCopy = 0x01a012c0;
constexpr uintptr_t kStringAssign = 0x01a01270;
constexpr uintptr_t kStringDestroy = 0x01a01320;
// Hero-only save (used for the couch partner by FUN_00752e80):
// __thiscall(mgr, name, int playerType, int user, int), RET 0x10, and its call sites.
constexpr uintptr_t kHeroOnlySave = 0x007521f0;
constexpr uintptr_t kHeroOnlySaveCalls[] = {0x00753301, 0x00752f05, 0x00753d0a};
constexpr size_t kSaveMgrMainName = 0x00;       // current main save name, e.g. "hero1autosave"
constexpr size_t kSaveMgrPartnerName = 0x04;    // couch partner save name; partner save needs it set
constexpr size_t kSaveMgrPartnerUser = 0x168;   // couch partner's user index, -1 if none
constexpr uintptr_t kPartnerSaveNameGlobal = 0x01dbd070;
// Initial save name (string; Lua SetInitialSaveGameName): the save Continue loads. FUN_00752510
// (__thiscall on the save manager) fills it with the newest usable save; the mode transition
// FUN_00bbccd0 calls it at kContinuePickCall.
constexpr uintptr_t kInitialSaveName = 0x01dbd06c;
constexpr uintptr_t kContinuePick = 0x00752510;
constexpr uintptr_t kContinuePickCall = 0x00bbcd28;  // string; used as the main name when FUN_006a8760
// Add-local-player FUN_00bbfa10: void* __cdecl(int user, int, char, void*, void*); it lists the
// user's saves and loads the chosen hero. Its five call sites:
constexpr uintptr_t kAddLocalPlayer = 0x00bbfa10;
constexpr uintptr_t kAddLocalPlayerCalls[] = {0x00bc0523, 0x00bc061c, 0x009ef665, 0x006b6b3e, 0x006b6ba4};

// Game packet send: FUN_00686de0 void __thiscall(net manager, packet*, char reliable, int, int).
// Drops the packet when the net manager has no link (+0x94 == 0). Packet vtable: [0] write,
// [1] id, [6] read. Receive dispatch: FUN_00690db0 -> FUN_0068a8f0 (ids < 0xee).
constexpr uintptr_t kPacketSend = 0x00686de0;
constexpr size_t kNetManagerLink = 0x94;
// GUI action dispatcher FUN_00d78ba0 (this = [GUI root + 0x48]) calls each callback here as
// bool __cdecl(const int* action, int player, void* owner), player 1 or 2.
constexpr uintptr_t kGuiDispatchCall = 0x00d78c98;
// Script interaction screen NUI::CInteractionModeGUIWorld (vtable 0x01b61cd4, made by the GUI
// screen manager's mode 3, Lua OpenInteractionMode). Slot 9 is its input handler FUN_0119e8f0,
// bool __thiscall(screen, const int* action, int player); FUN_0119ddc0 (same signature) checks
// the press comes from the interacting hero's player. [screen+0x24]+0x30/+0x38 track the
// interacting entity and the entity it interacts with.
constexpr uintptr_t kInteractionInputSlot = 0x01b61cf8;
constexpr uintptr_t kInteractionInput = 0x0119e8f0;
constexpr uintptr_t kInteractionPlayerCheck = 0x0119ddc0;
// Script message lookup FUN_007033e0: void* __thiscall(messages, int type, const u32 senderUid[2],
// int lastId). The Lua binding IsMessageSentBy (FUN_009f7a50) calls it at kIsMessageSentByFindCall.
// Room scripts accept a button press only if MessageEvents.IsMessageSentBy(self.GUIHeroEntity).
constexpr uintptr_t kFindMessageBySender = 0x007033e0;
constexpr uintptr_t kIsMessageSentByFindCall = 0x009f7b33;
// GUI-level button message types (MESSAGE_EVENT_GUI_LEVEL_*: 101 A, 102 B, 103 X, ...).
constexpr int kGuiLevelMessageFirst = 101;
constexpr int kGuiLevelMessageLast = 112;
// Script message posts on the same object: FUN_009f8ca0 (local) and FUN_009f8ab0 (networked),
// __thiscall(messages, name, const float* pos, CEntity* entity, CEntity* other, int, int).
constexpr uintptr_t kPostMessage = 0x009f8ca0;
constexpr uintptr_t kPostMessageNet = 0x009f8ab0;
// World prompt icons NUI::CEmotionUI: GUI input callback 00c80680 (prio 5) calls FUN_00c7d430,
// bool __thiscall(ui, const int* action, int player), at kWorldPromptInputCall. It posts the
// room's button message (FUN_00c7a310) only for an icon with that action owned by the pressing
// hero (or by nobody).
constexpr uintptr_t kWorldPromptInput = 0x00c7d430;
constexpr uintptr_t kWorldPromptInputCall = 0x00c80692;
constexpr size_t kWorldPromptIcons = 0x24;   // std::list head; node+8 -> icon: [0] action, [2..3] owner UID
constexpr int kActionGiftIconAccept = 0xa6;  // Sanctuary gift icons (A / X)
constexpr int kActionGiftIconReject = 0xa9;
constexpr int kCouchGuiPlayer = 2;           // player number GUI callbacks get for player 2's input

// CPlayer (FUN_00bbec20): +0x30 type, +0x34 user index.
constexpr size_t kPlayerUser = 0x34;
constexpr size_t kPlayerHero = 0x10;   // CTrackingPointer<CEntity>* to the player's hero (FUN_00bbe910)
constexpr size_t kEntityUid = 0x18;    // CEntity UID, two words

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
