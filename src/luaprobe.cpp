#include "luaprobe.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "addresses.h"
#include "guiscreen.h"
#include "log.h"

namespace {

using GetOpacityFn = float(__cdecl*)(void* element, void* screen, char raw);
using LuaLogFn = void(__cdecl*)(int level, const char* message);

constexpr DWORD kPollMs = 500;
constexpr DWORD kChecksDelayMs = 300;  // after the chunk, for a queued run to happen

std::wstring g_scriptPath;
DWORD g_nextPoll = 0;
std::vector<std::string> g_pending;  // directive lines waiting for the chunk to have run
void* g_pendingScreen = nullptr;
DWORD g_checksAt = 0;
bool g_keyDown = false;
LuaLogFn g_gameLuaLog = nullptr;

// Gameface reports Lua output() and script errors here (level 0 message .. 3 critical); the game
// discards them. The CGUI constructor installs its own sink, so this is re-applied every pump.
void __cdecl LuaLog(int level, const char* message)
{
	Log("lua probe: [%d] %s", level, message ? message : "");
	if (g_gameLuaLog) {
		g_gameLuaLog(level, message);
	}
}

void HookLuaLog()
{
	auto* slot = reinterpret_cast<LuaLogFn*>(addr::Rebase(addr::kLuaLogCallback));
	if (*slot != &LuaLog) {
		g_gameLuaLog = *slot;
		*slot = &LuaLog;
	}
}

// The rerun key (LuaProbeKey, a virtual-key code; default F9), pressed while the game has focus.
bool RerunKeyPressed()
{
	static const int key = GetPrivateProfileIntW(L"Fable3Coop", L"LuaProbeKey", VK_F9, ConfigPath());
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	const bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState(key) & 0x8000) != 0;
	const bool pressed = down && !g_keyDown;
	g_keyDown = down;
	return pressed;
}

bool Enabled()
{
	static const bool enabled = GetPrivateProfileIntW(L"Fable3Coop", L"LuaProbe", 0, ConfigPath()) != 0;
	return enabled;
}

void RunDirective(void* screen, const std::string& line)
{
	if (line.rfind("--check ", 0) == 0) {
		const std::string path = line.substr(8);
		void* element = FindGuiElement(screen, path.c_str());
		if (!element) {
			Log("lua probe: %s not found", path.c_str());
			return;
		}
		const float opacity = reinterpret_cast<GetOpacityFn>(addr::Rebase(addr::kGetElementOpacity))(element, screen, 1);
		Log("lua probe: %s = %p, opacity %.2f", path.c_str(), element, opacity);
	}
	else if (line.rfind("--settext ", 0) == 0) {
		const std::string rest = line.substr(10);
		const size_t bar = rest.find('|');
		if (bar == std::string::npos) {
			Log("lua probe: settext needs <path>|<text>");
			return;
		}
		const std::string path = rest.substr(0, bar);
		if (!FindGuiElement(screen, path.c_str())) {
			Log("lua probe: settext: %s not found", path.c_str());
			return;
		}
		wchar_t text[256];
		MultiByteToWideChar(CP_UTF8, 0, rest.c_str() + bar + 1, -1, text, 256);
		const bool set = SetGuiText(screen, path.c_str(), text);
		Log(set ? "lua probe: text of %s set" : "lua probe: settext: %s is not a text element", path.c_str());
	}
}

bool ReadScript(const std::wstring& path, std::string& script)
{
	FILE* file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
		return false;
	}
	char buffer[4096];
	size_t n;
	while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		script.append(buffer, n);
	}
	fclose(file);
	return true;
}

// Runs luaprobe.lua (renaming it to luaprobe.done), or with rerun the last one, luaprobe.done.
void RunScriptFile(bool rerun)
{
	const std::wstring done = g_scriptPath.substr(0, g_scriptPath.size() - 3) + L"done";
	std::string script;
	if (!ReadScript(rerun ? done : g_scriptPath, script)) {
		if (rerun) {
			Log("lua probe: no luaprobe.done to run again");
		}
		return;
	}
	if (!rerun) {
		MoveFileExW(g_scriptPath.c_str(), done.c_str(), MOVEFILE_REPLACE_EXISTING);
	}

	const int id = GetPrivateProfileIntW(L"Fable3Coop", L"LuaProbeScreen", addr::kOptionsScreenId, ConfigPath());
	void* screen = FindGuiScreen(id);
	if (!screen) {
		Log("lua probe: screen %d is not loaded; open it and save luaprobe.lua again", id);
		return;
	}
	g_pending.clear();
	size_t start = 0;
	while (start < script.size()) {
		size_t end = script.find('\n', start);
		if (end == std::string::npos) {
			end = script.size();
		}
		std::string line = script.substr(start, end - start);
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.rfind("--check ", 0) == 0 || line.rfind("--settext ", 0) == 0) {
			g_pending.push_back(line);
		}
		start = end + 1;
	}
	RunGuiLua(screen, script);
	Log("lua probe: ran %u bytes on screen %d (%p), %u checks to follow", static_cast<unsigned>(script.size()), id, screen,
		static_cast<unsigned>(g_pending.size()));
	g_pendingScreen = screen;
	g_checksAt = GetTickCount() + kChecksDelayMs;
}

}  // namespace

void PumpLuaProbe()
{
	if (!Enabled()) {
		return;
	}
	HookLuaLog();
	const DWORD now = GetTickCount();
	if (g_checksAt && static_cast<int>(now - g_checksAt) >= 0) {
		g_checksAt = 0;
		const int id = GetPrivateProfileIntW(L"Fable3Coop", L"LuaProbeScreen", addr::kOptionsScreenId, ConfigPath());
		if (FindGuiScreen(id) != g_pendingScreen) {
			Log("lua probe: screen %d changed before the checks; skipped", id);
		}
		else {
			for (const std::string& line : g_pending) {
				RunDirective(g_pendingScreen, line);
			}
		}
		g_pending.clear();
	}
	if (g_scriptPath.empty()) {
		g_scriptPath = ConfigPath();
		g_scriptPath.replace(g_scriptPath.rfind(L'\\') + 1, std::wstring::npos, L"luaprobe.lua");
	}
	if (RerunKeyPressed() && !g_checksAt) {
		RunScriptFile(true);
	}
	if (static_cast<int>(now - g_nextPoll) < 0) {
		return;
	}
	g_nextPoll = now + kPollMs;
	if (!g_checksAt && GetFileAttributesW(g_scriptPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
		RunScriptFile(false);
	}
}
