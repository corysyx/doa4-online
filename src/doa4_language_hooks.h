// The game's language at run time (doa4_language_hooks.cpp): the values this process applies, and the frame step.

#pragma once

#include <cstdint>

#include "doa4_language.h"

namespace doa4::language {

// The Saved this process applies at boot, taken once (the first call) and never again: in an online session from the
// hand-off's guest settings, in a replay from the recording's (np::SessionGuestValue; a missing line is kUnset), otherwise
// from the doa4_language / doa4_subtitles cvars. Later cvar writes (the SDK's F4 overlay writes any cvar by name) never
// change it.
Saved SavedForThisRun();

// Called by the frame function's hook (np_session.cpp sub_824F4498) on every frame, session or not, before the frame
// runs. One atomic load when there is nothing to do.
void OnFrame(uint8_t* base);

}  // namespace doa4::language
