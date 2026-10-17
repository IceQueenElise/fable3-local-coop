#include "fixes.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "addresses.h"
#include "log.h"

namespace {

DWORD WINAPI NoGamerCard(DWORD user, ULONGLONG xuid)
{
	Log("gamercard for user %lu (xuid %016llx) skipped", user, xuid);
	return ERROR_SUCCESS;
}

// Repoints a "CALL rel32" at siteVa from expectedVa to target.
bool RedirectCall(uintptr_t siteVa, uintptr_t expectedVa, void* target)
{
	auto* site = reinterpret_cast<uint8_t*>(addr::Rebase(siteVa));
	int32_t rel;
	memcpy(&rel, site + 1, sizeof(rel));
	if (site[0] != 0xe8 || reinterpret_cast<uintptr_t>(site) + 5 + rel != addr::Rebase(expectedVa)) {
		Log("fixes: unexpected call at %p; not patched", site);
		return false;
	}
	rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
	DWORD oldProtect;
	if (!VirtualProtect(site + 1, sizeof(rel), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}
	memcpy(site + 1, &rel, sizeof(rel));
	VirtualProtect(site + 1, sizeof(rel), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	return true;
}

}  // namespace

bool InstallFixes()
{
	bool ok = true;
	for (uintptr_t site : addr::kInteractionGamerCardCalls) {
		ok = RedirectCall(site, addr::kXShowGamerCardUIThunk, reinterpret_cast<void*>(&NoGamerCard)) && ok;
	}
	Log("fixes: player-interaction gamercard %s", ok ? "disabled" : "not fully patched");
	return ok;
}
