#include "doa4_language.h"

namespace doa4::language {

const std::array<Choice, 8> kTextChoices = {{
    {1, "English"},
    {0, "Japanese"},
    {2, "French"},
    {3, "German"},
    {4, "Italian"},
    {5, "Spanish"},
    {6, "Traditional Chinese"},
    {7, "Korean"},
}};

std::optional<uint8_t> ValidText(int64_t v) {
  if (v < 0 || v >= kTextCount) return std::nullopt;
  return uint8_t(v);
}

std::optional<uint8_t> ValidVoice(int64_t v) {
  if (v < 0 || v >= kVoiceCount) return std::nullopt;
  return uint8_t(v);
}

Saved Resolve(int64_t text_cvar, int64_t voice_cvar, std::string* ignored_note) {
  if (ignored_note) ignored_note->clear();
  auto add = [&](const char* what, int64_t v) {
    if (!ignored_note) return;
    if (!ignored_note->empty()) *ignored_note += "; ";
    *ignored_note += std::string(what) + " " + std::to_string(v) + " is out of range";
  };
  Saved s;
  if (text_cvar != kUnset) {
    s.text = ValidText(text_cvar);
    if (!s.text) add("language", text_cvar);
  }
  if (voice_cvar != kUnset) {
    s.voice = ValidVoice(voice_cvar);
    if (!s.voice) add("voice", voice_cvar);
  }
  return s;
}

void Apply(const GuestBytes& g, const Saved& s) {
  if (s.text) {
    g.Write(kTextAddress, *s.text);
    g.Write(kTextCopyAddress, *s.text);
  }
  if (s.voice) g.Write(kVoiceAddress, *s.voice);
}

Seen Read(const GuestBytes& g) { return Seen{g.Read(kTextAddress), g.Read(kVoiceAddress)}; }

}  // namespace doa4::language
