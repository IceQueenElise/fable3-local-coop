#include "routing.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "addresses.h"
#include "gui.h"
#include "input.h"
#include "log.h"

namespace {

uintptr_t g_ingestReturn = 0;

constexpr int kRightStick = 0xc;

// Chooses the user an event is grouped under (event+0x28 holds the device id on entry).
// Devices 0-2 (pad 1, keyboard, mouse) are player 1 and the extra pad is player 2, except
// that while player 2 is in the game and no menu is up, the extra pad's right stick turns the
// shared co-op camera, which belongs to player 1.
int __cdecl RouteEventUser(const uint8_t* event)
{
	const int device = *reinterpret_cast<const int*>(event + 0x28);
	if (device != static_cast<int>(kExtraDeviceId)) {
		return 0;
	}
	const int type = *reinterpret_cast<const int*>(event + 0x2c);
	if (type == kRightStick && reinterpret_cast<int(__cdecl*)()>(addr::Rebase(addr::kGetMode))() == 1 && !IsGuiOpen()) {
		return 0;
	}
	return static_cast<int>(kExtraDeviceId);
}

// Replaces, at 00d89e70 (EBP == 0, ECX = event):
//   MOV [ECX+0x28],EBP    ; event user = 0
//   MOV [ESP+0x30],ECX
// EBP is also stored as the event-group key at 00d89e7c, so setting it here routes both.
// EBX/ESI/EDI survive the cdecl call; EAX/ECX/EDX are saved around it. The instructions
// that follow don't read flags.
__declspec(naked) void IngestUserStub()
{
	__asm {
		push eax
		push ecx
		push edx
		push ecx
		call RouteEventUser
		add esp, 4
		mov ebp, eax
		pop edx
		pop ecx
		pop eax
		mov dword ptr [ecx + 0x28], ebp
		mov dword ptr [esp + 0x30], ecx
		jmp dword ptr [g_ingestReturn]
	}
}

uintptr_t g_guiFilterPass = 0;
uintptr_t g_guiFilterSkip = 0;

// Inline asm can't take a constexpr as an immediate, so GuiFilterStub spells out the id.
static_assert(kExtraDeviceId == 3, "update GuiFilterStub");

// Replaces, at 00b6d085 (ESI = event, EAX = main CPlayer):
//   MOV ECX,[ESI+0x28]; CMP ECX,[EAX+0x34]; JNZ skip
// so the GUI also receives the extra pad's events, letting player 2 open menus.
__declspec(naked) void GuiFilterStub()
{
	__asm {
		mov ecx, dword ptr [esi + 0x28]
		cmp ecx, dword ptr [eax + 0x34]
		je pass
		cmp ecx, 3
		je pass
		jmp dword ptr [g_guiFilterSkip]
	pass:
		jmp dword ptr [g_guiFilterPass]
	}
}

// Overwrites the instructions at site with a JMP to stub, padded with NOPs, after
// checking they match expected. Returns the address just past the patched bytes.
template <size_t N>
uintptr_t PatchJump(uintptr_t siteVa, const uint8_t (&expected)[N], void* stub, const char* name)
{
	static_assert(N >= 5, "need room for a JMP rel32");
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(siteVa));
	if (memcmp(site, expected, N) != 0) {
		Log("routing: unexpected bytes at %s site %p; not patched", name, site);
		return 0;
	}
	uint8_t patch[N];
	memset(patch, 0x90, N);
	patch[0] = 0xe9;  // JMP rel32
	int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(site) + 5));
	memcpy(patch + 1, &rel, sizeof(rel));

	DWORD oldProtect;
	if (!VirtualProtect(site, N, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		Log("routing: VirtualProtect failed at %s: %lu", name, GetLastError());
		return 0;
	}
	memcpy(site, patch, N);
	VirtualProtect(site, N, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, N);
	return reinterpret_cast<uintptr_t>(site) + N;
}

}  // namespace

bool InstallInputRouting()
{
	g_ingestReturn = PatchJump(addr::kIngestZeroUser, addr::kIngestZeroUserBytes, &IngestUserStub, "ingest");
	if (!g_ingestReturn) {
		return false;
	}
	Log("routing: device %lu events now stay user %lu", kExtraDeviceId, kExtraDeviceId);

	g_guiFilterSkip = addr::Rebase(addr::kGuiFilterSkip);
	g_guiFilterPass = PatchJump(addr::kGuiFilter, addr::kGuiFilterBytes, &GuiFilterStub, "GUI filter");
	if (g_guiFilterPass) {
		Log("routing: GUI now also receives user %lu pad events", kExtraDeviceId);
	}
	return true;
}
