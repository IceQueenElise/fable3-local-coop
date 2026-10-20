#include "guiscreen.h"

#include <windows.h>

#include <cstdint>

#include "addresses.h"

namespace {

using StringFromTextFn = void*(__fastcall*)(void* str, void* edx, const char* text, int length);
using StringDestroyFn = void(__fastcall*)(void* str);
using WideAssignFn = void*(__fastcall*)(void* str, void* edx, const wchar_t* text, int length);
using RunScreenLuaFn = void(__cdecl*)(void* const* script, void* screen);
using FindElementFn = void*(__cdecl*)(void* pathByValue, void* parent, void* screen);  // destroys the path string
using SetTextFn = void(__cdecl*)(void* element, void* screen, void* const* text);

// SetElementText on an element that is not a text label (a node, a logo) faults.
bool TrySetText(void* element, void* screen, void* const* text)
{
	__try {
		reinterpret_cast<SetTextFn>(addr::Rebase(addr::kSetElementText))(element, screen, text);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

}  // namespace

// The shared slot when it holds that screen (a reopened menu gets a fresh instance there),
// otherwise the screen's own slot.
void* FindGuiScreen(int id)
{
	auto* root = *reinterpret_cast<uint8_t**>(addr::Rebase(addr::kGuiRoot));
	auto* screens = root ? *reinterpret_cast<uint8_t**>(root + addr::kGuiRootScreens) : nullptr;
	if (!screens) {
		return nullptr;
	}
	void* shared = *reinterpret_cast<void**>(screens + addr::kScreensShared);
	if (shared && *reinterpret_cast<int*>(screens + addr::kScreensSharedId) == id) {
		return shared;
	}
	return *reinterpret_cast<void**>(screens + addr::kScreensFirst + 4 * id);
}

void* FindGuiElement(void* screen, const char* path)
{
	void* str = nullptr;  // taken by value and destroyed by the lookup
	reinterpret_cast<StringFromTextFn>(addr::Rebase(addr::kStringFromText))(&str, nullptr, path, -1);
	return reinterpret_cast<FindElementFn>(addr::Rebase(addr::kFindElement))(str, nullptr, screen);
}

bool SetGuiText(void* screen, const char* path, const wchar_t* text)
{
	void* element = FindGuiElement(screen, path);
	if (!element) {
		return false;
	}
	void* wide = nullptr;
	reinterpret_cast<WideAssignFn>(addr::Rebase(addr::kWideStringAssign))(&wide, nullptr, text, -1);
	const bool set = TrySetText(element, screen, &wide);
	reinterpret_cast<StringDestroyFn>(addr::Rebase(addr::kWideStringDestroy))(&wide);
	return set;
}

void RunGuiLua(void* screen, const std::string& script)
{
	void* str = nullptr;
	reinterpret_cast<StringFromTextFn>(addr::Rebase(addr::kStringFromText))(&str, nullptr, script.c_str(), -1);
	reinterpret_cast<RunScreenLuaFn>(addr::Rebase(addr::kRunScreenLua))(&str, screen);
	reinterpret_cast<StringDestroyFn>(addr::Rebase(addr::kStringDestroy))(&str);
}
