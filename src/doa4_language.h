// The game's language settings: which bytes hold them, which values are valid, and what a saved value means.
// Pure code over GuestBytes, so host tests run it on a map. The hooks that call it live elsewhere.
//
// Text ids: 0 Japanese, 1 English, 2 French, 3 German, 4 Italian, 5 Spanish, 6 Traditional Chinese, 7 Korean.
// Subtitles ids (0x82FCC7C7, the game's Language > Subtitles row; the recon first called it the voice): 0..8, where 8
// is off/default.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "doa4_guest.h"

namespace doa4::language {

inline constexpr uint32_t kTextAddress = 0x82FCC7AF, kTextCopyAddress = 0x82FCC93F, kSubtitlesAddress = 0x82FCC7C7,
                          kBootDefaultAddress = 0x82FCC7C6;
inline constexpr int kTextCount = 8, kSubtitlesCount = 9, kSubtitlesOff = 8, kUnset = -1;

struct Choice {
  int id;
  const char* label;
};
// Every text id 0..7 once, labelled with its English name, in the order the Game tab lists them.
extern const std::array<Choice, 8> kTextChoices;

// v for 0..7, otherwise nothing. v is 64-bit so a value that would truncate into range is still refused.
std::optional<uint8_t> ValidText(int64_t v);
// v for 0..8, otherwise nothing.
std::optional<uint8_t> ValidSubtitles(int64_t v);

struct Saved {
  std::optional<uint8_t> text, subtitles;
};
// The saved cvar values as settings. kUnset gives nothing, silently. Out of range gives nothing, and *ignored_note
// (if not null) names each ignored value; it is cleared first and left empty when nothing was ignored.
Saved Resolve(int64_t text_cvar, int64_t subtitles_cvar, std::string* ignored_note);
// The same for the two values as carried text (a session's hand-off or a replay's recording, np::SessionGuestValue):
// nothing is kUnset; only a whole decimal integer is a number; anything else gives nothing and is named in
// *ignored_note ("language abc is not a number"), after Resolve's own notes.
Saved ResolveText(const std::optional<std::string>& text, const std::optional<std::string>& subtitles, std::string* ignored_note);

// The settings file's values the SDK did not take as written (spec G1: "the log names the value it ignored"). The SDK loads
// dead_or_alive_4.toml before logging starts (rex_app.cpp: LoadConfig, then InitLogging), so its own WARN for
// `doa4_language = "fr"` never reaches the log, and Resolve only sees the cvar (still kUnset). Given the file's text, one note
// per doa4_language / doa4_subtitles value, found and converted as the SDK does (cvar.cpp ApplyTomlTable: a table's keys
// joined with '_'; a bool, integer, float or string as text, then the int32 cvar's std::from_chars):
// - refused (not a number, or past int32): `doa4_language "fr" is not a number: the game's own value instead`;
// - read only in part (a float, or a string with a number in front): `doa4_subtitles 2.5 is not a whole number: read as 2`.
// Nothing for a value taken as written (an integer, or a string such as "3": Resolve names one out of range), an absent
// key, or a file that does not parse (the SDK then loads none of it).
std::vector<std::string> SettingsFileNotes(std::string_view toml_text);

// Writes text to kTextAddress and kTextCopyAddress and subtitles to kSubtitlesAddress, only for the fields that are set.
// Never writes kBootDefaultAddress.
void Apply(const GuestBytes& g, const Saved& s);

struct Seen {
  uint8_t text, subtitles;
};
// The live text byte (kTextAddress) and subtitles byte (kSubtitlesAddress).
Seen Read(const GuestBytes& g);

// A choice made on the game's own options screen, saved by the UI thread. The game thread calls SetBaseline with what
// the bytes hold after each apply (the boot hook always does, first), and NoteGameChoice with what they hold after the
// game's options handler: a value other than the last baseline or note is a choice. TakePendingSave (UI thread) returns
// the latest choice not yet taken, once. The two threads share one packed atomic word, so both values of a choice
// always come from the same note; nothing else passes between them.
void SetBaseline(Seen s);
void NoteGameChoice(Seen s);
std::optional<Seen> TakePendingSave();
// Whether this process may save such a choice (R15): never in a process started for a session. session_process: the
// hand-off was read (the app's session_); host_settings_applied: a host-imposed setting was applied, which also stays
// true after a session start was aborted (the app's guest_touched_). The session mode (np::Online) is not enough: a
// start that fails before np::Start leaves it off while the module still runs until the offline relaunch quits it.
bool SavesChoices(bool session_process, bool host_settings_applied);

// The Esc menu's Language dropdown (update G1): the UI thread calls RequestLiveText with the picked text id (an id that is
// no text language is ignored); the game thread, in the frame step before the frame runs, calls ApplyLiveRequest. That takes
// the pending id (the latest, once), and when session_active (an online session, a replay or a recording) drops it unapplied;
// otherwise writes the text only (Apply) and moves the choice baseline to what the bytes now hold, so the options screen's
// hook never mistakes the pick for a game choice. Returns the id it applied.
void RequestLiveText(uint8_t id);
bool LiveRequestPending();  // one relaxed load: the frame step asks before building anything
std::optional<uint8_t> ApplyLiveRequest(const GuestBytes& g, bool session_active);

}  // namespace doa4::language
