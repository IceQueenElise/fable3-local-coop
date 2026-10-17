#include <windows.h>

#include "bindings.h"
#include "gui.h"
#include "input.h"
#include "join.h"
#include "routing.h"
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
		}
	}
	return TRUE;
}
