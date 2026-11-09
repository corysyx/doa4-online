// The game's language settings: which bytes hold them, which values are valid, and what a saved value means.
// Pure code over GuestBytes, so host tests run it on a map. The hooks that call it live elsewhere.
//
// Text ids: 0 Japanese, 1 English, 2 French, 3 German, 4 Italian, 5 Spanish, 6 Traditional Chinese, 7 Korean.
// Voice ids: 0..8, where 8 is off/default.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "doa4_guest.h"

namespace doa4::language {

inline constexpr uint32_t kTextAddress = 0x82FCC7AF, kTextCopyAddress = 0x82FCC93F, kVoiceAddress = 0x82FCC7C7,
                          kBootDefaultAddress = 0x82FCC7C6;
inline constexpr int kTextCount = 8, kVoiceCount = 9, kVoiceOff = 8, kUnset = -1;

struct Choice {
  int id;
  const char* label;
};
// Every text id 0..7 once, labelled with its English name, in the order the Game tab lists them.
extern const std::array<Choice, 8> kTextChoices;

// v for 0..7, otherwise nothing. v is 64-bit so a value that would truncate into range is still refused.
std::optional<uint8_t> ValidText(int64_t v);
// v for 0..8, otherwise nothing.
std::optional<uint8_t> ValidVoice(int64_t v);

struct Saved {
  std::optional<uint8_t> text, voice;
};
// The saved cvar values as settings. kUnset gives nothing, silently. Out of range gives nothing, and *ignored_note
// (if not null) names each ignored value; it is cleared first and left empty when nothing was ignored.
Saved Resolve(int64_t text_cvar, int64_t voice_cvar, std::string* ignored_note);

// Writes text to kTextAddress and kTextCopyAddress and voice to kVoiceAddress, only for the fields that are set.
// Never writes kBootDefaultAddress.
void Apply(const GuestBytes& g, const Saved& s);

struct Seen {
  uint8_t text, voice;
};
// The live text byte (kTextAddress) and voice byte (kVoiceAddress).
Seen Read(const GuestBytes& g);

}  // namespace doa4::language
