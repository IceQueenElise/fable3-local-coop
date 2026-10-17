#pragma once

// Diagnostics for NControlSystem input bindings: wraps the pad button and left-stick
// "matches(event, user)" methods and logs each distinct (caller, event user, requested
// user) combination. Call after InstallInputHooks has set the image base.
bool InstallBindingProbe();
