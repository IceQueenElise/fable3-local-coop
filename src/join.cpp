#include "join.h"

#include <windows.h>

#include <cstdint>

#include "addresses.h"
#include "input.h"
#include "log.h"
#include "proxy.h"

namespace {

using DetectorFn = void(__fastcall*)(void* self, void* edx, void* event);
using BoolThisFn = bool(__fastcall*)(void* self, void* edx);
using BoolFn = bool(__cdecl*)();
using IntFn = int(__cdecl*)();

DetectorFn g_originalDetector = nullptr;

template <typename T>
T Field(const void* base, size_t offset)
{
	return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset);
}

// Mirrors the checks in FUN_00b9b0c0 in order, so the log names the first one that fails.
void LogDetectorEvent(void* self, void* event)
{
	const int type = Field<int>(event, addr::kEventType);
	if (type != addr::kEventButtonDown) {
		return;
	}
	const int button = Field<uint8_t>(event, addr::kEventButton);
	const int user = Field<int>(event, addr::kEventUser);

	const bool gameplay = reinterpret_cast<BoolThisFn>(addr::Rebase(addr::kDetectorGate))(self, nullptr);
	const bool restarting = Field<uint8_t>(self, 0x30) != 0;
	const void* mainPlayer = Field<void*>(self, 0x08);
	const int mainUser = mainPlayer ? Field<int>(mainPlayer, addr::kPlayerUser) : -1;
	const bool joinAllowed = *reinterpret_cast<uint8_t*>(addr::Rebase(addr::kCouchJoinAllowed)) != 0;
	const bool liveGame = reinterpret_cast<BoolFn>(addr::Rebase(addr::kIsInLiveGame))();
	const int mode = reinterpret_cast<IntFn>(addr::Rebase(addr::kGetMode))();

	Log("detector: button %d user %d | gameplay=%d restarting=%d mainUser=%d joinAllowed=%d live=%d mode=%d",
		button, user, gameplay, restarting, mainUser, joinAllowed, liveGame, mode);
}

// XUSER_SIGNIN_INFO from the GFWL SDK.
struct XUserSigninInfo {
	uint64_t xuid;
	DWORD infoFlags;
	DWORD state;  // 0 none, 1 local, 2 online
	DWORD guestNumber;
	DWORD sponsorUserIndex;
	char userName[16];
};
constexpr DWORD kXuserGetSigninInfoOnlineXuidOnly = 1;
constexpr DWORD kXuserGetSigninInfoOfflineXuidOnly = 2;
constexpr DWORD kXuserInfoFlagGuest = 2;

using XUserGetSigninInfoFn = DWORD(WINAPI*)(DWORD user, DWORD flags, XUserSigninInfo* info);

// The PC build only ever refreshes sign-in cache entry 0 (FUN_00772190), so the
// add-player routine thinks any other user is signed out and opens the sign-in UI,
// which never completes under xLiveLess. Fill the entry the same way FUN_00772190 does.
void SeedSigninCache(DWORD user)
{
	static auto getSigninInfo = reinterpret_cast<XUserGetSigninInfoFn>(
		GetProcAddress(GetModuleHandleW(L"xlive.dll"), MAKEINTRESOURCEA(5267)));
	if (!getSigninInfo) {
		Log("sign-in cache: xlive XUserGetSigninInfo not found");
		return;
	}
	void* root = *reinterpret_cast<void**>(addr::Rebase(addr::kNetRoot));
	void* cache = root ? Field<void*>(root, addr::kNetRootSignin) : nullptr;
	if (!cache) {
		Log("sign-in cache: not created yet");
		return;
	}
	auto* begin = Field<uint8_t*>(cache, addr::kSigninEntriesBegin);
	auto* end = Field<uint8_t*>(cache, addr::kSigninEntriesEnd);
	if (static_cast<size_t>(end - begin) < (user + 1) * addr::kSigninEntrySize) {
		Log("sign-in cache: only %d entries", static_cast<int>((end - begin) / addr::kSigninEntrySize));
		return;
	}
	uint8_t* entry = begin + user * addr::kSigninEntrySize;

	// Same order and fields as FUN_00772190: state and flags come from the offline query.
	XUserSigninInfo offline = {};
	XUserSigninInfo online = {};
	if (getSigninInfo(user, kXuserGetSigninInfoOfflineXuidOnly, &offline) != ERROR_SUCCESS || offline.state == 0) {
		Log("sign-in cache: xlive reports user %lu signed out (state %lu); check xlive.ini profiles", user, offline.state);
		return;
	}
	if (getSigninInfo(user, kXuserGetSigninInfoOnlineXuidOnly, &online) != ERROR_SUCCESS) {
		online.xuid = 0;
	}
	entry[addr::kSigninOnline] = offline.state == 2;
	entry[addr::kSigninLocal] = offline.state == 1;
	entry[addr::kSigninGuest] = (offline.infoFlags & kXuserInfoFlagGuest) ? 1 : 0;
	*reinterpret_cast<uint64_t*>(entry + addr::kSigninOfflineXuid) = offline.xuid;
	*reinterpret_cast<uint64_t*>(entry + addr::kSigninOnlineXuid) = online.xuid;
	*reinterpret_cast<uint64_t*>(entry + addr::kSigninXuid) = online.xuid ? online.xuid : offline.xuid;
	Log("sign-in cache: seeded user %lu (%.16s, state %lu, xuid %016llx)", user, offline.userName, offline.state,
		static_cast<unsigned long long>(offline.xuid));
}

void* g_detector = nullptr;
DWORD g_handledStartPress = 0;

void __fastcall DetectorDetour(void* self, void* edx, void* event)
{
	if (!g_detector) {
		Log("detector first called, this %p", self);
	}
	g_detector = self;
	LogDetectorEvent(self, event);
	g_originalDetector(self, edx, event);
	if (Field<uint8_t>(event, addr::kEventHandled) != 0 && Field<int>(event, addr::kEventType) == addr::kEventButtonDown) {
		Log("detector: event marked handled (join requested?)");
	}
}

}  // namespace

void PumpExtraPadJoin()
{
	// The GUI only receives the primary user's events, so the extra pad's START never
	// reaches the detector. Hand it a START press from the extra pad's user instead; the
	// detector still applies every one of its own checks (in gameplay, not already joined, ...).
	const DWORD pressedAt = LastStartPress(kExtraPad);
	if (pressedAt == 0 || pressedAt == g_handledStartPress) {
		return;
	}
	g_handledStartPress = pressedAt;
	if (!g_detector) {
		Log("pad %lu START ignored: join detector not seen yet (press any button on pad 0 once)", kExtraPad);
		return;
	}

	alignas(8) uint8_t event[addr::kEventSize] = {};
	*reinterpret_cast<int*>(event + addr::kEventType) = addr::kEventButtonDown;
	event[addr::kEventButton] = addr::kButtonStart;
	*reinterpret_cast<int*>(event + addr::kEventUser) = static_cast<int>(kExtraDeviceId);

	Log("pad %lu START: offering join to the game as user %lu", kExtraPad, kExtraDeviceId);
	SeedSigninCache(kExtraDeviceId);
	LogDetectorEvent(g_detector, event);
	g_originalDetector(g_detector, nullptr, event);
	Log("join offer %s", event[addr::kEventHandled] ? "accepted" : "ignored");
}

bool InstallJoinProbe()
{
	auto* slot = reinterpret_cast<uintptr_t*>(addr::Rebase(addr::kDetectorSlot));
	if (*slot != addr::Rebase(addr::kDetectorFn)) {
		Log("join probe: unexpected detector slot value %p", reinterpret_cast<void*>(*slot));
		return false;
	}
	DWORD oldProtect;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		Log("join probe: VirtualProtect failed: %lu", GetLastError());
		return false;
	}
	g_originalDetector = reinterpret_cast<DetectorFn>(*slot);
	*slot = reinterpret_cast<uintptr_t>(&DetectorDetour);
	VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
	Log("join probe installed (slot %p)", slot);
	return true;
}
