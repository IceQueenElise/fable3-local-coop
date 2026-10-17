#include "routing.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "addresses.h"
#include "input.h"
#include "log.h"

namespace {

uintptr_t g_ingestReturn = 0;

// Inline asm can't take a constexpr as an immediate, so the stub spells out the id.
static_assert(kExtraDeviceId == 3, "update IngestUserStub");

// Replaces, at 00d89e70 (EBP == 0, ECX = event):
//   MOV [ECX+0x28],EBP    ; event user = 0
//   MOV [ESP+0x30],ECX
// EBP is also stored as the event-group key at 00d89e7c, so setting it here routes both.
// The instructions that follow don't read flags.
__declspec(naked) void IngestUserStub()
{
	__asm {
		xor ebp, ebp
		cmp dword ptr [ecx + 0x28], 3
		jne store
		mov ebp, 3
	store:
		mov dword ptr [ecx + 0x28], ebp
		mov dword ptr [esp + 0x30], ecx
		jmp dword ptr [g_ingestReturn]
	}
}

}  // namespace

bool InstallInputRouting()
{
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(addr::kIngestZeroUser));
	if (memcmp(site, addr::kIngestZeroUserBytes, sizeof(addr::kIngestZeroUserBytes)) != 0) {
		Log("routing: unexpected bytes at ingest site %p; not patched", site);
		return false;
	}
	g_ingestReturn = reinterpret_cast<uintptr_t>(site) + sizeof(addr::kIngestZeroUserBytes);

	uint8_t patch[sizeof(addr::kIngestZeroUserBytes)];
	memset(patch, 0x90, sizeof(patch));
	patch[0] = 0xe9;  // JMP rel32
	int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&IngestUserStub) - (reinterpret_cast<uintptr_t>(site) + 5));
	memcpy(patch + 1, &rel, sizeof(rel));

	DWORD oldProtect;
	if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		Log("routing: VirtualProtect failed: %lu", GetLastError());
		return false;
	}
	memcpy(site, patch, sizeof(patch));
	VirtualProtect(site, sizeof(patch), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
	Log("routing: device %lu events now stay user %lu (patched %p)", kExtraDeviceId, kExtraDeviceId, site);
	return true;
}
