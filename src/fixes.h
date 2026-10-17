#pragma once

// Small fixes for GFWL features that don't work under xLiveLess.
//
// Viewing the other player's gamercard (RB on the player-interaction prompt) calls
// XShowGamerCardUI; xLiveLess answers with a burst of sign-in/profile notifications, after
// which the interaction prompts stop appearing. Those two calls now return without calling
// xlive. Call after InstallInputHooks has set the image base.
bool InstallFixes();
