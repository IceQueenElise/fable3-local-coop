#include <windows.h>

#include "bindings.h"
#include "fixes.h"
#include "gui.h"
#include "input.h"
#include "join.h"
#include "netprobe.h"
#include "picker.h"
#include "routing.h"
#include "saveio.h"
#include "saves.h"
#include "log.h"
#include "proxy.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(module);
		LogInit(module);
		Log("Fable3Coop loaded");
		if (!real::Load()) {
			return FALSE;
		}
		// The game's code hasn't run yet (we are a static import), so patching here is race-free.
		if (InstallInputHooks()) {
			InstallJoinProbe();
			InstallBindingProbe();
			InstallInputRouting();
			InstallGuiHooks();
			InstallFixes();
			InstallPicker();
			InstallSaveHooks();
			InstallSaveFileLog();
			InstallNetProbe();
		}
	}
	return TRUE;
}
