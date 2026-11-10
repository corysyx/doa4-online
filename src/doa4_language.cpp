#include "doa4_language.h"

#include <atomic>
#include <charconv>
#include <system_error>

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

std::optional<uint8_t> ValidSubtitles(int64_t v) {
  if (v < 0 || v >= kSubtitlesCount) return std::nullopt;
  return uint8_t(v);
}

Saved Resolve(int64_t text_cvar, int64_t subtitles_cvar, std::string* ignored_note) {
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
  if (subtitles_cvar != kUnset) {
    s.subtitles = ValidSubtitles(subtitles_cvar);
    if (!s.subtitles) add("subtitles", subtitles_cvar);
  }
  return s;
}

void Apply(const GuestBytes& g, const Saved& s) {
  if (s.text) {
    g.Write(kTextAddress, *s.text);
    g.Write(kTextCopyAddress, *s.text);
  }
  if (s.subtitles) g.Write(kSubtitlesAddress, *s.subtitles);
}

Seen Read(const GuestBytes& g) { return Seen{g.Read(kTextAddress), g.Read(kSubtitlesAddress)}; }

Saved ResolveText(const std::optional<std::string>& text, const std::optional<std::string>& subtitles, std::string* ignored_note) {
  std::string not_numbers;
  // kUnset for no line; the number for a whole decimal integer; kUnset, named, for anything else (never an atoi-style 0)
  auto number = [&](const char* what, const std::optional<std::string>& v) -> int64_t {
    if (!v) return kUnset;
    int64_t n = 0;
    const char* end = v->data() + v->size();
    const auto [ptr, ec] = std::from_chars(v->data(), end, n);
    if (!v->empty() && ec == std::errc() && ptr == end) return n;
    if (!not_numbers.empty()) not_numbers += "; ";
    not_numbers += std::string(what) + " " + *v + " is not a number";
    return kUnset;
  };
  const int64_t t = number("language", text), su = number("subtitles", subtitles);
  Saved s = Resolve(t, su, ignored_note);
  if (ignored_note && !not_numbers.empty()) {
    if (!ignored_note->empty()) *ignored_note += "; ";
    *ignored_note += not_numbers;
  }
  return s;
}

namespace {
// The choice record. g_last: what the bytes held at the last baseline or note, (text << 8) | subtitles, or kNone before the
// first; the game thread alone reads and writes it. g_pending: kPending | (text << 8) | subtitles, or 0: the one word the game
// thread (NoteGameChoice) and the UI thread (TakePendingSave) share. A choice's two values travel in that one word, so a
// reader never pairs one choice's text with another's subtitles; no other memory is published through it, so the
// release/acquire pair below is more than the word needs (relaxed would do), at no cost ten times a second.
constexpr uint32_t kNone = 0xFFFFFFFFu, kPending = 0x10000u;
std::atomic<uint32_t> g_last{kNone};
std::atomic<uint32_t> g_pending{0};
uint32_t Pack(Seen s) { return (uint32_t(s.text) << 8) | s.subtitles; }
}  // namespace

void SetBaseline(Seen s) { g_last.store(Pack(s), std::memory_order_relaxed); }

void NoteGameChoice(Seen s) {
  const uint32_t now = Pack(s);
  const uint32_t last = g_last.exchange(now, std::memory_order_relaxed);
  if (last == kNone || last == now) return;  // no baseline yet (the boot hook sets one first), or nothing changed
  g_pending.store(kPending | now, std::memory_order_release);
}

std::optional<Seen> TakePendingSave() {
  const uint32_t v = g_pending.exchange(0, std::memory_order_acquire);
  if (!(v & kPending)) return std::nullopt;
  return Seen{uint8_t(v >> 8), uint8_t(v)};
}

bool SavesChoices(bool session_process, bool host_settings_applied) {
  return !session_process && !host_settings_applied;
}

namespace {
constexpr int kNoLiveRequest = -1;
std::atomic<int> g_live_text{kNoLiveRequest};  // the UI thread stores, the game thread exchanges
}  // namespace

void RequestLiveText(uint8_t id) {
  if (!ValidText(id)) return;
  g_live_text.store(id, std::memory_order_release);
}

bool LiveRequestPending() { return g_live_text.load(std::memory_order_relaxed) != kNoLiveRequest; }

std::optional<uint8_t> ApplyLiveRequest(const GuestBytes& g, bool session_active) {
  if (!LiveRequestPending()) return std::nullopt;
  const int id = g_live_text.exchange(kNoLiveRequest, std::memory_order_acquire);
  if (id == kNoLiveRequest || session_active) return std::nullopt;
  Saved s;
  s.text = uint8_t(id);
  Apply(g, s);
  SetBaseline(Read(g));  // what the bytes hold now is no choice made on the game's options screen
  return uint8_t(id);
}

}  // namespace doa4::language
