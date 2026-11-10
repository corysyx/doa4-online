// Host test for src/doa4_language.cpp. Built and run by run_language_test.bat.

#include "../../src/doa4_language.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::language;

// Guest memory in a map; every byte the test cares about starts at 0xEE so an unwanted write shows.
struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  int writes = 0;
  doa4::GuestBytes guest{[this](uint32_t a) { return bytes.count(a) ? bytes[a] : uint8_t(0); },
                         [this](uint32_t a, uint8_t v) {
                           bytes[a] = v;
                           ++writes;
                         }};
  FakeGuest() {
    for (uint32_t a : {kTextAddress, kTextCopyAddress, kSubtitlesAddress, kBootDefaultAddress}) bytes[a] = 0xEE;
  }
};

static void TestAddresses() {
  CHECK(kTextAddress == 0x82FCC7AFu && kTextCopyAddress == 0x82FCC93Fu);
  CHECK(kSubtitlesAddress == 0x82FCC7C7u && kBootDefaultAddress == 0x82FCC7C6u);
  CHECK(kTextCount == 8 && kSubtitlesCount == 9 && kSubtitlesOff == 8 && kUnset == -1);
}

static void TestValid() {
  // Each bound has its own literal: -1 (below), 8 and 42 (above), and a 2^32 that truncates to 0 in a 32-bit cast.
  CHECK(!ValidText(-1));
  CHECK(!ValidText(8));
  CHECK(!ValidText(42));
  CHECK(!ValidText(int64_t{1} << 32));
  CHECK(ValidText(0) && *ValidText(0) == 0);
  CHECK(ValidText(7) && *ValidText(7) == 7);
  CHECK(ValidText(1) && *ValidText(1) == 1);

  CHECK(!ValidSubtitles(-1));
  CHECK(!ValidSubtitles(9));
  CHECK(!ValidSubtitles(-7));
  CHECK(!ValidSubtitles(int64_t{1} << 32));
  CHECK(ValidSubtitles(0) && *ValidSubtitles(0) == 0);
  CHECK(ValidSubtitles(8) && *ValidSubtitles(8) == 8);
}

static void TestResolve() {
  std::string note = "stale";
  Saved s = Resolve(-1, -1, &note);
  CHECK(!s.text && !s.subtitles && note.empty());

  note.clear();
  s = Resolve(42, 3, &note);
  CHECK(!s.text && s.subtitles && *s.subtitles == 3);
  CHECK(note.find("42") != std::string::npos);

  // Only the below-zero guard rejects -7 (not kUnset, not above the top): it is named; kUnset beside it is silent.
  note.clear();
  s = Resolve(-7, -1, &note);
  CHECK(!s.text && !s.subtitles);
  CHECK(note.find("-7") != std::string::npos);

  note.clear();
  s = Resolve(-1, 9, &note);
  CHECK(!s.text && !s.subtitles);
  CHECK(note.find("9") != std::string::npos && note.find("-1") == std::string::npos);

  note.clear();
  s = Resolve(-7, 9, &note);
  CHECK(!s.text && !s.subtitles);
  CHECK(note.find("-7") != std::string::npos && note.find("9") != std::string::npos);

  note.clear();
  s = Resolve(7, 8, &note);
  CHECK(s.text && *s.text == 7 && s.subtitles && *s.subtitles == 8 && note.empty());

  // A null note pointer is allowed.
  s = Resolve(99, 99, nullptr);
  CHECK(!s.text && !s.subtitles);
}

static void TestApply() {
  {
    FakeGuest g;
    Apply(g.guest, Saved{uint8_t{2}, std::nullopt});
    CHECK(g.bytes[kTextAddress] == 2 && g.bytes[kTextCopyAddress] == 2);
    CHECK(g.bytes[kSubtitlesAddress] == 0xEE && g.bytes[kBootDefaultAddress] == 0xEE);
    CHECK(g.writes == 2);
  }
  {
    FakeGuest g;
    Apply(g.guest, Saved{std::nullopt, uint8_t{8}});
    CHECK(g.bytes[kSubtitlesAddress] == 8);
    CHECK(g.bytes[kTextAddress] == 0xEE && g.bytes[kTextCopyAddress] == 0xEE && g.bytes[kBootDefaultAddress] == 0xEE);
    CHECK(g.writes == 1);
  }
  {
    FakeGuest g;
    Apply(g.guest, Saved{});
    CHECK(g.writes == 0);
  }
  {
    FakeGuest g;
    Apply(g.guest, Saved{uint8_t{0}, uint8_t{0}});  // 0 is a value, not "unset"
    CHECK(g.bytes[kTextAddress] == 0 && g.bytes[kTextCopyAddress] == 0 && g.bytes[kSubtitlesAddress] == 0);
    CHECK(g.bytes[kBootDefaultAddress] == 0xEE && g.writes == 3);
  }
}

static void TestRead() {
  FakeGuest g;
  g.bytes[kTextAddress] = 5;
  g.bytes[kTextCopyAddress] = 6;  // Read reports the live text byte, not the copy
  g.bytes[kSubtitlesAddress] = 4;
  g.bytes[kBootDefaultAddress] = 1;
  const Seen seen = Read(g.guest);
  CHECK(seen.text == 5 && seen.subtitles == 4);
  CHECK(g.writes == 0);
}

static void TestChoices() {
  std::set<int> ids;
  for (const Choice& c : kTextChoices) {
    ids.insert(c.id);
    CHECK(c.label && *c.label);
  }
  CHECK(ids.size() == 8 && *ids.begin() == 0 && *ids.rbegin() == 7);
  std::map<int, std::string> by_id;
  for (const Choice& c : kTextChoices) by_id[c.id] = c.label;
  CHECK(by_id[0] == "Japanese" && by_id[1] == "English" && by_id[2] == "French" && by_id[3] == "German");
  CHECK(by_id[4] == "Italian" && by_id[5] == "Spanish" && by_id[6] == "Traditional Chinese" && by_id[7] == "Korean");
}

// A carried value as text (the hand-off's or the recording's guest settings, np::SessionGuestValue). T2.2: the likeliest
// wrong version is an atoi-style parse, which reads "abc" and "" as 0 (Japanese) and "3x" as 3; only a whole decimal
// integer is a value, and anything else is named and gives the game's own.
static void TestResolveText() {
  std::string note = "stale";
  Saved s = ResolveText(std::string("3"), std::string("8"), &note);
  CHECK(s.text == std::optional<uint8_t>(3) && s.subtitles == std::optional<uint8_t>(8) && note.empty());

  s = ResolveText(std::string("0"), std::string("0"), &note);  // 0 is Japanese, a value
  CHECK(s.text == std::optional<uint8_t>(0) && s.subtitles == std::optional<uint8_t>(0) && note.empty());

  s = ResolveText(std::nullopt, std::nullopt, &note);  // no line: the game's own, silently
  CHECK(!s.text && !s.subtitles && note.empty());

  s = ResolveText(std::string("-1"), std::string("-1"), &note);  // kUnset as text: silent
  CHECK(!s.text && !s.subtitles && note.empty());

  for (const char* bad : {"abc", "", "3x", " 3", "+3", "0x2"}) {
    s = ResolveText(std::string(bad), std::nullopt, &note);
    CHECK(!s.text && !s.subtitles);  // NOT 0 or 3
    CHECK(note.find("language") != std::string::npos && note.find("not a number") != std::string::npos);
    CHECK(note.find("subtitles") == std::string::npos);
  }
  s = ResolveText(std::nullopt, std::string("x8"), &note);
  CHECK(!s.text && !s.subtitles && note.find("subtitles x8 is not a number") != std::string::npos);

  // in range as a number but not as an id; and a 64-bit value that truncates to 2 in a 32-bit cast
  s = ResolveText(std::string("42"), std::string("9"), &note);
  CHECK(!s.text && !s.subtitles && note.find("42") != std::string::npos && note.find("9") != std::string::npos);
  s = ResolveText(std::string("4294967298"), std::nullopt, &note);
  CHECK(!s.text && note.find("4294967298") != std::string::npos);
  s = ResolveText(std::string("99999999999999999999999"), std::nullopt, &note);  // past int64: not usable, named
  CHECK(!s.text && note.find("99999999999999999999999") != std::string::npos);

  s = ResolveText(std::string("abc"), std::string("2"), &note);  // one bad field leaves the other
  CHECK(!s.text && s.subtitles == std::optional<uint8_t>(2));
  s = ResolveText(std::string("7"), std::string("x"), nullptr);  // a null note pointer is allowed
  CHECK(s.text == std::optional<uint8_t>(7) && !s.subtitles);
}

static bool Same(const std::optional<Seen>& a, uint8_t text, uint8_t subtitles) {
  return a && a->text == text && a->subtitles == subtitles;
}

// NoteGameChoice (game thread) and TakePendingSave (UI thread): a value other than the last one the bytes held is a
// choice to save, taken once, both values from one note. Global state: the steps run in this order.
static void TestChoiceTracking() {
  // before any baseline the first note is only a baseline (the boot hook always sets one first)
  NoteGameChoice(Seen{3, 3});
  CHECK(!TakePendingSave());

  SetBaseline(Seen{1, 8});
  NoteGameChoice(Seen{1, 8});  // the options handler ran, nothing changed
  CHECK(!TakePendingSave());

  NoteGameChoice(Seen{2, 8});
  const std::optional<Seen> first = TakePendingSave();
  CHECK(Same(first, 2, 8));
  CHECK(!TakePendingSave());  // taken once

  NoteGameChoice(Seen{2, 8});  // the same value again: no new choice
  CHECK(!TakePendingSave());

  NoteGameChoice(Seen{2, 3});
  NoteGameChoice(Seen{4, 3});  // two choices before a tick: the latest, both values from it
  CHECK(Same(TakePendingSave(), 4, 3));

  NoteGameChoice(Seen{0, 0});  // Japanese, subtitles 0: a value, not "nothing pending"
  CHECK(Same(TakePendingSave(), 0, 0));

  // a new baseline (an apply) is no choice, and the next note compares against it
  SetBaseline(Seen{5, 1});
  CHECK(!TakePendingSave());
  NoteGameChoice(Seen{5, 1});
  CHECK(!TakePendingSave());
  NoteGameChoice(Seen{5, 2});  // only the subtitles changed
  CHECK(Same(TakePendingSave(), 5, 2));
}

int main() {
  TestAddresses();
  TestValid();
  TestResolve();
  TestResolveText();
  TestApply();
  TestRead();
  TestChoices();
  TestChoiceTracking();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
