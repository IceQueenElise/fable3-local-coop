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

bool InCouchGame()
{
	return reinterpret_cast<int(__cdecl*)()>(addr::Rebase(addr::kGetMode))() == 1;
}

using PromptInputFn = bool(__fastcall*)(void* self, void* edx, const int* action, int player);

// The world prompt icons that accept or reject a gift in the Sanctuary are owned by player 1's
// hero even when player 2 opened the gift, so player 2's presses match no icon and the gift room
// (which waits for player 2's press) soft-locks. For player 2's gift-icon presses, clear the
// owner of a gift icon owned by someone else for the duration of the call: an icon with no owner
// matches any hero, and the press is then posted with player 2's hero as the sender.
bool __fastcall PromptInputDetour(void* self, void* edx, const int* action, int player)
{
	const int id = action ? action[0] : -1;
	const bool giftIcon = id == addr::kActionGiftIconAccept || id == addr::kActionGiftIconReject;
	int* cleared = nullptr;
	uint32_t owner[2] = {};
	if (giftIcon && player == addr::kCouchGuiPlayer && InCouchGame()) {
		auto* head = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(self) + addr::kWorldPromptIcons);
		for (auto* node = head ? *reinterpret_cast<uint8_t**>(head) : nullptr; node && node != head;
			 node = *reinterpret_cast<uint8_t**>(node)) {
			auto* icon = *reinterpret_cast<int**>(node + 8);
			if (icon && icon[0] == id && (icon[2] != 0 || icon[3] != 0)) {
				cleared = icon;
				owner[0] = icon[2];
				owner[1] = icon[3];
				icon[2] = 0;
				icon[3] = 0;
				break;
			}
		}
	}
	const bool taken = reinterpret_cast<PromptInputFn>(addr::Rebase(addr::kWorldPromptInput))(self, edx, action, player);
	if (cleared) {
		cleared[2] = owner[0];
		cleared[3] = owner[1];
		Log("gift icon 0x%x pressed by player 2 (icon owner uid %08x)", id, owner[0]);
	}
	return taken;
}

using GetPlayerFn = void*(__fastcall*)(void* players, void* edx);
using FindMessageFn = void*(__fastcall*)(void* messages, void* edx, int type, const uint32_t* senderUid, int lastId);

// A player's hero UID (CEntity +0x18, two words), or false if there is no such hero.
bool HeroUid(uintptr_t playerGetter, uint32_t uid[2])
{
	auto* game = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGameRoot));
	auto* inner = game ? *reinterpret_cast<uint8_t**>(game + 0x0c) : nullptr;
	void* players = inner ? *reinterpret_cast<void**>(inner + 0x40) : nullptr;
	auto* player = players ? static_cast<uint8_t*>(reinterpret_cast<GetPlayerFn>(addr::Rebase(playerGetter))(players, nullptr)) : nullptr;
	auto** tracked = player ? *reinterpret_cast<uint8_t***>(player + addr::kPlayerHero) : nullptr;
	auto* hero = tracked ? *tracked : nullptr;
	if (!hero) {
		return false;
	}
	memcpy(uid, hero + addr::kEntityUid, 2 * sizeof(uint32_t));
	return true;
}

// The Sanctuary gift script (GUI_UnwrappedGiftManager:Update) posts the accept/reject press as
// a GUI-level button message "from GetLocalHero()", which in couch co-op is always player 1,
// while player 2's gift room asks for presses from player 2 (MessageEvents.IsMessageSentBy).
// When player 2's room finds no such message, also accept one from player 1.
void* __fastcall FindMessageDetour(void* messages, void* edx, int type, const uint32_t* senderUid, int lastId)
{
	auto find = reinterpret_cast<FindMessageFn>(addr::Rebase(addr::kFindMessageBySender));
	void* found = find(messages, edx, type, senderUid, lastId);
	if (found || !senderUid || type < addr::kGuiLevelMessageFirst || type > addr::kGuiLevelMessageLast || !InCouchGame()) {
		return found;
	}
	uint32_t partner[2];
	uint32_t main[2];
	if (!HeroUid(addr::kCouchPlayer, partner) || !HeroUid(addr::kMainPlayer, main)
		|| senderUid[0] != partner[0] || senderUid[1] != partner[1]) {
		return found;
	}
	found = find(messages, edx, type, main, lastId);
	if (found) {
		Log("GUI message %d for player 2 accepted from player 1's hero (Sanctuary script)", type);
	}
	return found;
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
	const bool gift = RedirectCall(addr::kWorldPromptInputCall, addr::kWorldPromptInput, reinterpret_cast<void*>(&PromptInputDetour));
	const bool messages = RedirectCall(addr::kIsMessageSentByFindCall, addr::kFindMessageBySender, reinterpret_cast<void*>(&FindMessageDetour));
	Log("fixes: player 2 gift accept/reject %s", gift && messages ? "enabled" : "not patched");
	return ok && gift && messages;
}
