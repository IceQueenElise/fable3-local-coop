#pragma once

// Diagnostics: logs every game packet send (FUN_00686de0) in couch mode with its id and
// whether a network link exists to carry it. In couch co-op there is no link, so packets the
// host addresses to the partner's "machine" are dropped. Installed only while logging is on.
// Call after InstallInputHooks has set the image base.
bool InstallNetProbe();
