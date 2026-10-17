#include "bindings.h"

#include <windows.h>

#include <intrin.h>

#include <cstdint>

#include "addresses.h"
#include "log.h"

namespace {

// bool __thiscall CInputType*::Matches(const Event* event, int user); RET 8.
using MatchesFn = bool(__fastcall*)(void* self, void* edx, const void* event, int user);

MatchesFn g_originalButtonMatches = nullptr;
MatchesFn g_originalStickMatches = nullptr;

// Remembers which combinations were already logged, so the log stays readable.
bool FirstTime(uintptr_t caller, int eventUser, int user, int kind)
{
	static CRITICAL_SECTION lock;
	static bool init = false;
	static struct Seen {
		uintptr_t caller;
		int eventUser, user, kind;
	} seen[64];
	static int count = 0;
	if (!init) {
		InitializeCriticalSection(&lock);
		init = true;
	}
	EnterCriticalSection(&lock);
	bool first = count < 64;
	for (int i = 0; i < count && first; ++i) {
		const Seen& s = seen[i];
		if (s.caller == caller && s.eventUser == eventUser && s.user == user && s.kind == kind) {
			first = false;
		}
	}
	if (first) {
		seen[count++] = {caller, eventUser, user, kind};
	}
	LeaveCriticalSection(&lock);
	return first;
}

void Note(const char* kind, int kindId, const void* event, int user, bool result, void* returnAddress)
{
	const int eventUser = *reinterpret_cast<const int*>(static_cast<const uint8_t*>(event) + addr::kEventUser);
	const auto caller = reinterpret_cast<uintptr_t>(returnAddress) - addr::g_base + 0x00400000;
	if (FirstTime(caller, eventUser, user, kindId)) {
		Log("binding %s: event user %d, requested user %d -> %d (caller %08x)", kind, eventUser, user, result,
			static_cast<unsigned>(caller));
	}
}

bool __fastcall ButtonMatchesDetour(void* self, void* edx, const void* event, int user)
{
	bool result = g_originalButtonMatches(self, edx, event, user);
	Note("button", 0, event, user, result, _ReturnAddress());
	return result;
}

bool __fastcall StickMatchesDetour(void* self, void* edx, const void* event, int user)
{
	bool result = g_originalStickMatches(self, edx, event, user);
	Note("left stick", 1, event, user, result, _ReturnAddress());
	return result;
}

bool PatchSlot(uintptr_t slotVa, uintptr_t expectedFnVa, void* detour, MatchesFn* original)
{
	auto* slot = reinterpret_cast<uintptr_t*>(addr::Rebase(slotVa));
	if (*slot != addr::Rebase(expectedFnVa)) {
		Log("binding probe: unexpected value %p in slot %p", reinterpret_cast<void*>(*slot), slot);
		return false;
	}
	DWORD oldProtect;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		return false;
	}
	*original = reinterpret_cast<MatchesFn>(*slot);
	*slot = reinterpret_cast<uintptr_t>(detour);
	VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
	return true;
}

}  // namespace

bool InstallBindingProbe()
{
	bool ok = PatchSlot(addr::kPadButtonMatchesSlot, addr::kPadButtonMatches, &ButtonMatchesDetour,
				  &g_originalButtonMatches) &&
		PatchSlot(addr::kLeftStickMatchesSlot, addr::kLeftStickMatches, &StickMatchesDetour, &g_originalStickMatches);
	Log("binding probe %s", ok ? "installed" : "not installed");
	return ok;
}
