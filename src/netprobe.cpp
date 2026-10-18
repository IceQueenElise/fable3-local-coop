#include "netprobe.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "addresses.h"
#include "log.h"

namespace {

uintptr_t g_sendResume = 0;

void __cdecl NoteSend(const uint8_t* netManager, void* packet)
{
	if (!netManager || !packet || reinterpret_cast<int(__cdecl*)()>(addr::Rebase(addr::kGetMode))() != 1) {
		return;
	}
	using IdFn = int(__fastcall*)(void* packet, void* edx);
	const int id = reinterpret_cast<IdFn>((*static_cast<void***>(packet))[1])(packet, nullptr);
	// Log the first few sends of each packet id, so frequent ones don't crowd out the rest.
	static uint8_t seen[0x200];
	if (id < 0 || id >= 0x200 || seen[id] >= 3) {
		return;
	}
	++seen[id];
	const int link = *reinterpret_cast<const int*>(netManager + addr::kNetManagerLink);
	Log("net send: packet 0x%x%s", id, link ? "" : " (no link: dropped)");
}

// Replaces "SUB ESP,0x840" at the entry of FUN_00686de0 (ECX = net manager, [ESP+4] = packet).
__declspec(naked) void SendStub()
{
	__asm {
		pushad
		mov eax, dword ptr [esp + 0x24]
		push eax
		push ecx
		call NoteSend
		add esp, 8
		popad
		sub esp, 0x840
		jmp dword ptr [g_sendResume]
	}
}

uintptr_t g_dispatchResume = 0;

using GuiCallbackFn = bool(__cdecl*)(const int* action, int player, void* owner);

// Calls one GUI input callback for the dispatcher and logs which callback takes the
// accept/reject presses (first few per callback and action).
bool __cdecl CallGuiCallback(GuiCallbackFn fn, const int* action, int player, void* owner)
{
	const bool taken = fn(action, player, owner);
	const int id = action ? action[0] : -1;
	if (taken && (id == 0x5d || id == 0x5f || id == 0x64 || id == 0x67)) {
		static struct { uintptr_t fn; int id; int count; } seen[64];
		const uintptr_t va = reinterpret_cast<uintptr_t>(fn) - addr::g_base + 0x00400000;
		for (auto& entry : seen) {
			if (entry.fn == 0) {
				entry = {va, id, 0};
			}
			if (entry.fn == va && entry.id == id) {
				if (entry.count++ < 3) {
					Log("GUI callback %08x (owner %p) took action 0x%x from player %d", static_cast<unsigned>(va), owner, id, player);
				}
				break;
			}
		}
	}
	return taken;
}

// Replaces "CALL [ESP+0x34]; ADD ESP,0xC" in the GUI action dispatcher FUN_00d78ba0, where the
// callback's three cdecl args (action, player, owner) are already pushed.
__declspec(naked) void DispatchStub()
{
	__asm {
		push dword ptr [esp + 0x34]
		call CallGuiCallback
		add esp, 0x10
		jmp dword ptr [g_dispatchResume]
	}
}

bool PatchDispatch()
{
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(addr::kGuiDispatchCall));
	static const uint8_t kExpected[] = {0xff, 0x54, 0x24, 0x34, 0x83, 0xc4, 0x0c};
	if (memcmp(site, kExpected, sizeof(kExpected)) != 0) {
		Log("net probe: unexpected bytes at GUI dispatch; not installed");
		return false;
	}
	g_dispatchResume = reinterpret_cast<uintptr_t>(site) + sizeof(kExpected);
	uint8_t patch[sizeof(kExpected)] = {0xe9, 0, 0, 0, 0, 0x90, 0x90};
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&DispatchStub) - (reinterpret_cast<uintptr_t>(site) + 5));
	memcpy(patch + 1, &rel, sizeof(rel));
	DWORD oldProtect;
	if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}
	memcpy(site, patch, sizeof(patch));
	VirtualProtect(site, sizeof(patch), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
	return true;
}

using InteractionInputFn = bool(__fastcall*)(void* self, void* edx, const int* action, int player);
InteractionInputFn g_originalInteractionInput = nullptr;

// Logs presses reaching the interaction screen (NUI::CInteractionModeGUIWorld) and whether its
// player check FUN_0119ddc0 accepts them.
bool __fastcall InteractionInputDetour(void* self, void* edx, const int* action, int player)
{
	const bool taken = g_originalInteractionInput(self, edx, action, player);
	static int logged = 0;
	const int id = action ? action[0] : -1;
	const bool stick = id == 0x01 || id == 0x03 || id == 0x1b || id == 0x1c || id == 0x53 || id == 0xa0;
	if (!stick && logged < 400) {
		++logged;
		const bool playerOk = reinterpret_cast<InteractionInputFn>(addr::Rebase(addr::kInteractionPlayerCheck))(self, edx, action, player);
		auto* info = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(self) + 0x24);
		void** tracked = info ? *reinterpret_cast<void***>(info + 0x30) : nullptr;
		void** other = info ? *reinterpret_cast<void***>(info + 0x38) : nullptr;
		auto uid = [](void** t) -> unsigned {
			auto* e = t ? static_cast<uint8_t*>(*t) : nullptr;
			return e ? *reinterpret_cast<unsigned*>(e + 0x18) : 0;
		};
		Log("interaction screen: action 0x%x player %d -> %d (player check %d, interacting %p uid %08x, with %p uid %08x)",
			id, player, taken, playerOk, tracked ? *tracked : nullptr, uid(tracked), other ? *other : nullptr, uid(other));
	}
	return taken;
}

bool HookInteractionInput()
{
	auto* slot = reinterpret_cast<uintptr_t*>(addr::Rebase(addr::kInteractionInputSlot));
	if (*slot != addr::Rebase(addr::kInteractionInput)) {
		return false;
	}
	DWORD oldProtect;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		return false;
	}
	g_originalInteractionInput = reinterpret_cast<InteractionInputFn>(*slot);
	*slot = reinterpret_cast<uintptr_t>(&InteractionInputDetour);
	VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
	return true;
}

uintptr_t g_postResume[2] = {};

// Logs script message posts (FUN_009f8ca0 local, FUN_009f8ab0 networked): caller and the entity
// argument's UID, a few times per caller and entity.
void __cdecl NotePost(int which, uintptr_t ret, const uint8_t* entity)
{
	if (reinterpret_cast<int(__cdecl*)()>(addr::Rebase(addr::kGetMode))() != 1) {
		return;
	}
	const uintptr_t caller = ret - addr::g_base + 0x00400000;
	const uint32_t uid = entity && !IsBadReadPtr(entity, 0x20) ? *reinterpret_cast<const uint32_t*>(entity + 0x18) : 0;
	static struct { uintptr_t caller; uint32_t uid; int count; } seen[256];
	static int logged = 0;
	for (auto& entry : seen) {
		if (entry.caller == 0) {
			entry = {caller, uid, 0};
		}
		if (entry.caller == caller && entry.uid == uid) {
			if (entry.count++ < 6 && logged++ < 600) {
				Log("message post (%s) from %08x entity uid %08x", which ? "net" : "local", static_cast<unsigned>(caller), uid);
			}
			return;
		}
	}
}

// Replaces "PUSH EBP; MOV EBP,ESP; AND ESP,-16" at a post function's entry ([ESP+0xC] = entity).
#define POST_STUB(n)                                   	__declspec(naked) void PostStub##n()               	{                                                  		__asm { pushad }                               		__asm { push dword ptr [esp + 0x2c] }          		__asm { push dword ptr [esp + 0x24] }          		__asm { push n }                               		__asm { call NotePost }                        		__asm { add esp, 12 }                          		__asm { popad }                                		__asm { push ebp }                             		__asm { mov ebp, esp }                         		__asm { and esp, 0xfffffff0 }                  		__asm { jmp dword ptr [g_postResume + 4 * n] } 	}
POST_STUB(0)
POST_STUB(1)

bool HookPost(int which, uintptr_t va, void* stub)
{
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(va));
	static const uint8_t kExpected[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0};
	if (memcmp(site, kExpected, sizeof(kExpected)) != 0) {
		return false;
	}
	g_postResume[which] = reinterpret_cast<uintptr_t>(site) + sizeof(kExpected);
	uint8_t patch[sizeof(kExpected)] = {0xe9, 0, 0, 0, 0, 0x90};
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(site) + 5));
	memcpy(patch + 1, &rel, sizeof(rel));
	DWORD oldProtect;
	if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}
	memcpy(site, patch, sizeof(patch));
	VirtualProtect(site, sizeof(patch), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
	return true;
}

}  // namespace

bool InstallNetProbe()
{
	if (!LogEnabled()) {
		return false;
	}
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(addr::kPacketSend));
	static const uint8_t kExpected[] = {0x81, 0xec, 0x40, 0x08, 0x00, 0x00};  // SUB ESP,0x840
	if (memcmp(site, kExpected, sizeof(kExpected)) != 0) {
		Log("net probe: unexpected bytes at packet send; not installed");
		return false;
	}
	g_sendResume = reinterpret_cast<uintptr_t>(site) + sizeof(kExpected);
	uint8_t patch[sizeof(kExpected)] = {0xe9, 0, 0, 0, 0, 0x90};
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&SendStub) - (reinterpret_cast<uintptr_t>(site) + 5));
	memcpy(patch + 1, &rel, sizeof(rel));
	DWORD oldProtect;
	if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}
	memcpy(site, patch, sizeof(patch));
	VirtualProtect(site, sizeof(patch), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
	Log("net probe installed; GUI dispatch probe %s", PatchDispatch() ? "installed" : "not installed");
	Log("interaction screen probe %s", HookInteractionInput() ? "installed" : "not installed");
	const bool posts = HookPost(0, addr::kPostMessage, reinterpret_cast<void*>(&PostStub0))
		&& HookPost(1, addr::kPostMessageNet, reinterpret_cast<void*>(&PostStub1));
	Log("message post probe %s", posts ? "installed" : "not installed");
	return true;
}
