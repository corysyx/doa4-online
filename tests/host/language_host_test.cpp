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
    for (uint32_t a : {kTextAddress, kTextCopyAddress, kVoiceAddress, kBootDefaultAddress}) bytes[a] = 0xEE;
  }
};

static void TestAddresses() {
  CHECK(kTextAddress == 0x82FCC7AFu && kTextCopyAddress == 0x82FCC93Fu);
  CHECK(kVoiceAddress == 0x82FCC7C7u && kBootDefaultAddress == 0x82FCC7C6u);
  CHECK(kTextCount == 8 && kVoiceCount == 9 && kVoiceOff == 8 && kUnset == -1);
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

  CHECK(!ValidVoice(-1));
  CHECK(!ValidVoice(9));
  CHECK(!ValidVoice(-7));
  CHECK(!ValidVoice(int64_t{1} << 32));
  CHECK(ValidVoice(0) && *ValidVoice(0) == 0);
  CHECK(ValidVoice(8) && *ValidVoice(8) == 8);
}

static void TestResolve() {
  std::string note = "stale";
  Saved s = Resolve(-1, -1, &note);
  CHECK(!s.text && !s.voice && note.empty());

  note.clear();
  s = Resolve(42, 3, &note);
  CHECK(!s.text && s.voice && *s.voice == 3);
  CHECK(note.find("42") != std::string::npos);

  // Only the below-zero guard rejects -7 (not kUnset, not above the top): it is named; kUnset beside it is silent.
  note.clear();
  s = Resolve(-7, -1, &note);
  CHECK(!s.text && !s.voice);
  CHECK(note.find("-7") != std::string::npos);

  note.clear();
  s = Resolve(-1, 9, &note);
  CHECK(!s.text && !s.voice);
  CHECK(note.find("9") != std::string::npos && note.find("-1") == std::string::npos);

  note.clear();
  s = Resolve(-7, 9, &note);
  CHECK(!s.text && !s.voice);
  CHECK(note.find("-7") != std::string::npos && note.find("9") != std::string::npos);

  note.clear();
  s = Resolve(7, 8, &note);
  CHECK(s.text && *s.text == 7 && s.voice && *s.voice == 8 && note.empty());

  // A null note pointer is allowed.
  s = Resolve(99, 99, nullptr);
  CHECK(!s.text && !s.voice);
}

static void TestApply() {
  {
    FakeGuest g;
    Apply(g.guest, Saved{uint8_t{2}, std::nullopt});
    CHECK(g.bytes[kTextAddress] == 2 && g.bytes[kTextCopyAddress] == 2);
    CHECK(g.bytes[kVoiceAddress] == 0xEE && g.bytes[kBootDefaultAddress] == 0xEE);
    CHECK(g.writes == 2);
  }
  {
    FakeGuest g;
    Apply(g.guest, Saved{std::nullopt, uint8_t{8}});
    CHECK(g.bytes[kVoiceAddress] == 8);
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
    CHECK(g.bytes[kTextAddress] == 0 && g.bytes[kTextCopyAddress] == 0 && g.bytes[kVoiceAddress] == 0);
    CHECK(g.bytes[kBootDefaultAddress] == 0xEE && g.writes == 3);
  }
}

static void TestRead() {
  FakeGuest g;
  g.bytes[kTextAddress] = 5;
  g.bytes[kTextCopyAddress] = 6;  // Read reports the live text byte, not the copy
  g.bytes[kVoiceAddress] = 4;
  g.bytes[kBootDefaultAddress] = 1;
  const Seen seen = Read(g.guest);
  CHECK(seen.text == 5 && seen.voice == 4);
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

int main() {
  TestAddresses();
  TestValid();
  TestResolve();
  TestApply();
  TestRead();
  TestChoices();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
