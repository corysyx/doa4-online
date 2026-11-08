// Host test for src/doa4_unlocks.cpp. No game byte is in this file: the tables are synthetic (chosen here, a different
// permutation and different maxima from the game's, so a charId read as a profile index reads a wrong value) and the
// profiles are built by FreshProfile and FullProfile. Every check runs on the synthetic tables; with DOA4_TEST_IMAGE set
// (doa4_test_image.h) the whole set runs again on the three real ranges the unlock code reads. Built and run by
// run_unlocks_test.bat.

#include "../../src/doa4_unlocks.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "doa4_test_image.h"

static int g_failures = 0;
static int g_checks = 0;
static const char* g_tables_name = "synthetic";
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    ++g_checks;                                                                        \
    if (!(cond)) {                                                                     \
      std::printf("FAIL [%s] %s:%d: %s\n", g_tables_name, __FILE__, __LINE__, #cond); \
      ++g_failures;                                                                    \
    }                                                                                  \
  } while (0)

using namespace doa4::unlocks;

// Guest memory as a map; unset bytes read 0. Counts writes.
struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  int writes = 0;
  doa4::GuestBytes guest{[this](uint32_t a) { return Get(a); },
                         [this](uint32_t a, uint8_t v) {
                           bytes[a] = v;
                           ++writes;
                         }};
  uint8_t Get(uint32_t a) const {
    auto it = bytes.find(a);
    return it == bytes.end() ? 0 : it->second;
  }
  void SetBe32(uint32_t a, uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes[a + i] = uint8_t(v >> (24 - 8 * i));
  }
};

constexpr uint32_t kSlot = 0;
constexpr uint32_t kP = kRecordBase + kSlot * kRecordStride + kRecordToProfile;  // 0x831B0A40
constexpr uint32_t kFixtureStart = 0x3FC0;  // the profile range the unlock fields live in: P+0x3FC0..P+0x404B
constexpr size_t kProfileBytes = 0x8C;

// The three image tables the unlock code reads.
struct Tables {
  const char* name;
  std::array<uint8_t, 23> index_to_char;  // profile index -> charId
  std::array<uint8_t, 25> max;            // maximum costume count by charId
  std::array<uint8_t, 18> roster;         // the Story roster's charIds
};

// Synthetic tables. index -> charId is (6 i + 14) mod 25 (23 distinct charIds, none equal to its index; 2 and 8 have no
// profile index); the maxima are 25 distinct values 2..26 (2 + (4 c + 14) mod 25), each different from the game's at the same charId; the roster is the charIds of the 17 profile
// indices that have Story mode (all but 3, 6, 8, 12, 21 and 22), the later ones first, and the first one again.
static Tables SyntheticTables() {
  Tables t{"synthetic", {}, {}, {}};
  for (int i = 0; i < 23; ++i) t.index_to_char[size_t(i)] = uint8_t((6 * i + 14) % 25);
  for (int c = 0; c < 25; ++c) t.max[size_t(c)] = uint8_t(2 + (4 * c + 14) % 25);
  size_t k = 0;
  for (int i = 22; i >= 0; --i) {
    if (i == 3 || i == 6 || i == 8 || i == 12 || i == 21 || i == 22) continue;
    t.roster[k++] = t.index_to_char[size_t(i)];
  }
  t.roster[k++] = t.roster[0];
  return t;
}

// The real tables, when the image is there.
static std::optional<Tables> ImageTables(std::string* why) {
  const auto index_to_char = doa4_test::ReadImageRange(doa4_test::kProfileIndexToCharRange, why);
  if (!index_to_char) return std::nullopt;
  const auto max = doa4_test::ReadImageRange(doa4_test::kMaxCostumesRange, why);
  if (!max) return std::nullopt;
  const auto roster = doa4_test::ReadImageRange(doa4_test::kStoryRosterRange, why);
  if (!roster) return std::nullopt;
  Tables t{"image", {}, {}, {}};
  std::copy(index_to_char->begin(), index_to_char->end(), t.index_to_char.begin());
  std::copy(max->begin(), max->end(), t.max.begin());
  std::copy(roster->begin(), roster->end(), t.roster.begin());
  return t;
}

static bool HasStory(const Tables& t, int index) {
  for (uint8_t c : t.roster) {
    if (c == t.index_to_char[size_t(index)]) return true;
  }
  return false;
}

// SHA-1 over the unlock FIELD bytes of a 0x8C-byte profile, in the order the code visits them (the first-clear flag, then for
// each profile index its costume count, character flag, Story clears, cleared-at-Normal flag and voice flag). The two pins
// were computed from the real fresh save and the real 100% save this test used to carry; they are checksums only.
// `story_as_flag` hashes each Story-clear count as 0 or 1 (a model cannot know a real player's clear counts).
static std::string FieldsSha1(const std::vector<uint8_t>& p, bool story_as_flag) {
  std::vector<uint8_t> b;
  const auto at = [&](uint32_t offset) { return p[offset - kFixtureStart]; };
  b.push_back(at(kFirstStoryClear));
  for (uint32_t i = 0; i < uint32_t(kCharacterCount); ++i) {
    b.push_back(at(kCostumeCount + i));
    b.push_back(at(kCharacterUnlocked + i));
    const uint8_t clears = at(kStoryClears + i);
    b.push_back(story_as_flag ? uint8_t(clears ? 1 : 0) : clears);
    b.push_back(at(kStoryClearedNormal + i));
    b.push_back(at(kVoiceUnlocked + i));
  }
  return doa4_test::Sha1Hex(b.data(), b.size());
}
static const char kFreshFieldsSha1[] = "ad368af3e880eaf7d79a78e17d5562969b67bd19";
static const char kFullFieldsSha1[] = "ab5986e5f3169b8b2d33d4268d5883586dc6d936";  // Story clears as 0/1

static bool InUnlockField(uint32_t offset) {
  return offset == kFirstStoryClear || (offset >= kCostumeCount && offset < kCostumeCount + kCharacterCount) ||
         (offset >= kCharacterUnlocked && offset < kCharacterUnlocked + kCharacterCount) ||
         (offset >= kStoryClears && offset < kStoryClears + kCharacterCount) ||
         (offset >= kStoryClearedNormal && offset < kStoryClearedNormal + kCharacterCount) ||
         (offset >= kVoiceUnlocked && offset < kVoiceUnlocked + kCharacterCount);
}

// A new save's unlock fields: costume counts 2, the 7 hidden characters (indices 3, 6, 7, 8, 12, 21, 22: Ein, Gen Fu,
// Helena, Leon, Tengu, Alpha-152, Spartan-458) locked, no voices, no Story clears. Every byte of the range that is not an
// unlock field is uint8_t(0xA5 ^ offset), so a write outside the fields shows.
static std::vector<uint8_t> FreshProfile() {
  std::vector<uint8_t> p(kProfileBytes);
  for (uint32_t o = kFixtureStart; o < kFixtureStart + kProfileBytes; ++o) p[o - kFixtureStart] = uint8_t(0xA5 ^ o);
  p[kFirstStoryClear - kFixtureStart] = 0;
  for (int i = 0; i < kCharacterCount; ++i) {
    const bool hidden = i == 3 || i == 6 || i == 7 || i == 8 || i == 12 || i == 21 || i == 22;
    const uint32_t u = uint32_t(i);
    p[kCostumeCount - kFixtureStart + u] = 2;
    p[kCharacterUnlocked - kFixtureStart + u] = hidden ? 0 : 1;
    p[kStoryClears - kFixtureStart + u] = 0;
    p[kStoryClearedNormal - kFixtureStart + u] = 0;
    p[kVoiceUnlocked - kFixtureStart + u] = 0;
  }
  return p;
}

// A save that has everything: every count at its table maximum, every character unlocked but Alpha-152 (never unlocked in
// retail), every voice, one or more Story clears for each Story character, the first-clear flag. The bytes outside the
// unlock fields (Time Attack records and the rest) are uint8_t(0x5A ^ offset): different from the fresh ones everywhere,
// so a test can tell "kept" from "reset".
static std::vector<uint8_t> FullProfile(const Tables& t) {
  std::vector<uint8_t> p(kProfileBytes);
  for (uint32_t o = kFixtureStart; o < kFixtureStart + kProfileBytes; ++o) p[o - kFixtureStart] = uint8_t(0x5A ^ o);
  p[kFirstStoryClear - kFixtureStart] = 1;
  for (int i = 0; i < kCharacterCount; ++i) {
    const uint32_t u = uint32_t(i);
    p[kCostumeCount - kFixtureStart + u] = t.max[t.index_to_char[size_t(i)]];
    p[kCharacterUnlocked - kFixtureStart + u] = i == kAlphaIndex ? 0 : 1;
    p[kStoryClears - kFixtureStart + u] = HasStory(t, i) ? uint8_t(1 + i % 5) : uint8_t(0);
    p[kStoryClearedNormal - kFixtureStart + u] = HasStory(t, i) ? 1 : 0;
    p[kVoiceUnlocked - kFixtureStart + u] = 1;
  }
  return p;
}

// The tables under test, the two profiles built for them, and the characters the tests name: A and B are two girls
// (charId 5 and 0x0A) who get imported suits, B also has a costume box; H is the character at profile index 7, hidden in a
// new save but with Story mode; C is charId 13, a girl with no suits; g_unused is a charId with no profile index.
static Tables g_t;
static std::vector<uint8_t> g_fresh, g_full;
constexpr uint8_t kCharA = 5, kCharB = 0x0A, kCharC = 13;
constexpr int kHiddenWithStory = 7;
constexpr int kBoxCostumes = 2;  // what B's costume box adds to her table maximum
constexpr int kSuitsPerGirl = 4;
static int g_index_a, g_index_b, g_index_c;
static uint8_t g_char_h, g_unused;

static int IndexOf(uint8_t char_id) {
  for (int i = 0; i < kCharacterCount; ++i) {
    if (g_t.index_to_char[size_t(i)] == char_id) return i;
  }
  return -1;
}

static void UseTables(const Tables& t) {
  g_t = t;
  g_tables_name = t.name;
  g_fresh = FreshProfile();
  g_full = FullProfile(t);
  g_index_a = IndexOf(kCharA);
  g_index_b = IndexOf(kCharB);
  g_index_c = IndexOf(kCharC);
  g_char_h = t.index_to_char[kHiddenWithStory];
  g_unused = 0;
  while (IndexOf(g_unused) >= 0) ++g_unused;
}

static int BaseMax(int index) { return g_t.max[g_t.index_to_char[size_t(index)]]; }
static int TotalExtras() {  // costumes a new save lacks: the sum of (maximum - 2) over the 23 characters
  int total = 0;
  for (int i = 0; i < kCharacterCount; ++i) total += BaseMax(i) - 2;
  return total;
}

// The game's tables, the profile pointer and slot, and a profile's bytes at the pointer.
static void Setup(FakeGuest& g, const std::vector<uint8_t>& profile, uint32_t slot = kSlot, uint32_t pointer = kP) {
  for (size_t i = 0; i < g_t.index_to_char.size(); ++i) g.bytes[kProfileIndexToChar + uint32_t(i)] = g_t.index_to_char[i];
  for (size_t i = 0; i < g_t.max.size(); ++i) g.bytes[kMaxCostumes + uint32_t(i)] = g_t.max[i];
  for (size_t k = 0; k < g_t.roster.size(); ++k) g.bytes[kStoryRoster + kStoryRosterStride * uint32_t(k)] = g_t.roster[k];
  // The charId -> profile index table (sub_82408398 reads it): the inverse, 0 for a charId with no index.
  for (uint32_t c = 0; c < 26; ++c) {
    const int index = c < 25 ? IndexOf(uint8_t(c)) : -1;
    g.bytes[kCharToProfileIndex + c] = uint8_t(index >= 0 ? index : 0);
  }
  g.SetBe32(kProfilePointer, pointer);
  g.SetBe32(kProfileSlot, slot);
  // The profile bytes go where the pointer points, so only the pointer checks
  // can reject a bad pointer.
  for (size_t i = 0; i < profile.size(); ++i) g.bytes[pointer + kFixtureStart + uint32_t(i)] = profile[i];
  // The profile name, as in a loaded save: "User" in UTF-16BE at record+8.
  const char* name = "User";
  for (uint32_t i = 0; name[i]; ++i) g.bytes[pointer - kRecordToProfile + kRecordName + 2 * i + 1] = uint8_t(name[i]);
  g.writes = 0;
}

static uint8_t At(const FakeGuest& g, uint32_t offset, int index = 0) {
  return g.Get(kP + offset + uint32_t(index));
}
static uint8_t Full(uint32_t offset, int index = 0) {
  return g_full[offset - kFixtureStart + uint32_t(index)];
}
static uint8_t Fresh(uint32_t offset, int index = 0) {
  return g_fresh[offset - kFixtureStart + uint32_t(index)];
}

static void TestFreshProfileIsANewSave() {
  // A new save's unlock fields (every costume count 2; the 7 hidden characters Helena, Ein, Gen Fu, Leon, Tengu, Alpha-152
  // and Spartan-458 locked; no voices, no Story clears) are the real fresh save's: the model hashes to the pin computed
  // from the real save. The full profile is pinned the same way on the image's tables (a model with other tables has other
  // maxima).
  CHECK(FieldsSha1(g_fresh, false) == kFreshFieldsSha1);
  if (std::string(g_tables_name) == "image") CHECK(FieldsSha1(g_full, true) == kFullFieldsSha1);
  // The tables the tests lean on: the named characters exist, a charId is not its own index (so a charId read as an index
  // reads another girl's cell), and the two profiles differ outside the unlock fields.
  CHECK(g_index_a >= 0 && g_index_b >= 0 && g_index_c >= 0 && g_index_a != g_index_b);
  CHECK(g_index_b != int(kCharB) && g_index_c != int(kCharC));
  CHECK(g_unused < 25 && IndexOf(g_unused) < 0);
  for (uint32_t o = kFixtureStart; o < kFixtureStart + kProfileBytes; ++o) {
    if (!InUnlockField(o)) CHECK(Fresh(o) != Full(o));
  }
}

static void TestFindProfile() {
  FakeGuest g;
  Setup(g, g_fresh);
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(0x831B0A40));
  CHECK(g.writes == 0);
  // The slot index says 2 but the pointer still names slot 0's record.
  FakeGuest g2;
  Setup(g2, g_fresh, 2, kP);
  CHECK(!FindProfile(g2.guest));
  // Null pointer, pointer off the formula, slot index past the last record.
  FakeGuest g3;
  Setup(g3, g_fresh, kSlot, 0);
  CHECK(!FindProfile(g3.guest));
  FakeGuest g4;
  Setup(g4, g_fresh, kSlot, kP + 4);
  CHECK(!FindProfile(g4.guest));
  FakeGuest g5;
  Setup(g5, g_fresh, 4, kRecordBase + 4 * kRecordStride + kRecordToProfile);
  CHECK(!FindProfile(g5.guest));
  // A record the game has not loaded (all zero) is not a profile.
  FakeGuest g6;
  Setup(g6, std::vector<uint8_t>(kProfileBytes));
  CHECK(!FindProfile(g6.guest));
  // One costume count past its maximum, or one at 1, is not a profile either.
  FakeGuest g7;
  Setup(g7, g_fresh);
  g7.bytes[kP + kCostumeCount + kAlphaIndex] = uint8_t(BaseMax(kAlphaIndex) + 1);  // one past Alpha-152's maximum
  CHECK(!FindProfile(g7.guest));
  FakeGuest g8;
  Setup(g8, g_fresh);
  g8.bytes[kP + kCostumeCount + kSpartanIndex] = 1;
  CHECK(!FindProfile(g8.guest));
  // The game's default record before the save is read (seen at runtime from
  // 1 s after launch until Start is pressed): fresh costume counts and
  // character flags, but no name. Not a loaded profile.
  FakeGuest g10;
  Setup(g10, g_fresh);
  for (uint32_t i = 0; i < 32; ++i) g10.bytes[kP - kRecordToProfile + kRecordName + i] = 0;
  CHECK(!FindProfile(g10.guest));
  // A name is enough: one UTF-16 character.
  g10.bytes[kP - kRecordToProfile + kRecordName + 1] = 'A';
  CHECK(FindProfile(g10.guest).has_value());
  // The 100% save is a profile.
  FakeGuest g9;
  Setup(g9, g_full);
  CHECK(FindProfile(g9.guest).has_value());
}

static void TestEverythingMatchesTheFullSave() {
  FakeGuest g;
  Setup(g, g_fresh);
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kEverything);
  for (int i = 0; i < kCharacterCount; ++i) {
    CHECK(At(g, kCostumeCount, i) == Full(kCostumeCount, i));
    CHECK(At(g, kCharacterUnlocked, i) == Full(kCharacterUnlocked, i));
    CHECK(At(g, kVoiceUnlocked, i) == Full(kVoiceUnlocked, i));
    CHECK(At(g, kStoryClearedNormal, i) == Full(kStoryClearedNormal, i));
    // Story clears: 1 where the full save has any, 0 where it has none.
    CHECK(At(g, kStoryClears, i) == (Full(kStoryClears, i) ? 1 : 0));
  }
  CHECK(At(g, kFirstStoryClear) == 1);
  CHECK(r.characters == 6);
  CHECK(r.costumes == TotalExtras());  // sum of (max - 2) over the 23 characters
  CHECK(r.voices == 23);
  CHECK(r.stages == 3);
  CHECK(OpenStageCount(g.guest, kP) == 3);
  // Every other profile byte in the fixture range is untouched: Time Attack
  // records (P+0x401E), the bytes between the arrays, play time and the rest.
  for (uint32_t offset = kFixtureStart; offset < kFixtureStart + 0x8C; ++offset) {
    const bool unlock_byte = offset == kFirstStoryClear ||
                             (offset >= kCostumeCount && offset < kCostumeCount + 23) ||
                             (offset >= kCharacterUnlocked && offset < kCharacterUnlocked + 23) ||
                             (offset >= kStoryClears && offset < kStoryClears + 23) ||
                             (offset >= kStoryClearedNormal && offset < kStoryClearedNormal + 23) ||
                             (offset >= kVoiceUnlocked && offset < kVoiceUnlocked + 23);
    if (!unlock_byte) CHECK(At(g, offset) == Fresh(offset));
  }
  // Nothing at all is written outside the profile's unlock range.
  for (const auto& [address, value] : g.bytes) {
    (void)value;
    if (address >= kP + kFixtureStart && address < kP + kFixtureStart + 0x8C) continue;
    FakeGuest pristine;
    Setup(pristine, g_fresh);
    CHECK(pristine.bytes.count(address) == 1);
  }
}

// Each of characters / costumes / voices changes only its own array.
static void TestEachButtonOnlyItsOwnBytes() {
  struct Case {
    Unlock what;
    uint32_t own;
  };
  const Case cases[] = {{Unlock::kCharacters, kCharacterUnlocked},
                        {Unlock::kCostumes, kCostumeCount},
                        {Unlock::kVoices, kVoiceUnlocked}};
  const uint32_t arrays[] = {kCostumeCount, kCharacterUnlocked, kStoryClears, kStoryClearedNormal,
                             kVoiceUnlocked};
  for (const Case& c : cases) {
    FakeGuest g;
    Setup(g, g_fresh);
    ApplyUnlock(g.guest, kP, c.what);
    CHECK(IsUnlocked(g.guest, kP, c.what));
    for (uint32_t array : arrays) {
      if (array == c.own) continue;
      for (int i = 0; i < kCharacterCount; ++i) CHECK(At(g, array, i) == Fresh(array, i));
    }
    CHECK(At(g, kFirstStoryClear) == Fresh(kFirstStoryClear));
  }
  // Characters leaves Alpha-152 locked; voices include her.
  FakeGuest g;
  Setup(g, g_fresh);
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kCharacters);
  CHECK(r.characters == 6 && r.costumes == 0 && r.voices == 0 && r.stages == 1);
  CHECK(At(g, kCharacterUnlocked, kAlphaIndex) == 0);
  CHECK(At(g, kCharacterUnlocked, kSpartanIndex) == 1);
  // Spartan-458 unlocked opens Nassau Station (reported in r.stages above).
  CHECK(OpenStageCount(g.guest, kP) == 1);
  r = ApplyUnlock(g.guest, kP, Unlock::kVoices);
  CHECK(r.voices == 23 && At(g, kVoiceUnlocked, kAlphaIndex) == 1);
}

static void TestStagesDoesWhatItTakes() {
  FakeGuest g;
  Setup(g, g_fresh);
  CHECK(OpenStageCount(g.guest, kP) == 0);
  CHECK(!IsUnlocked(g.guest, kP, Unlock::kStages));
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kStages);
  CHECK(r.stages == 3 && r.characters == 6 && r.costumes == TotalExtras() && r.voices == 23);
  CHECK(IsUnlocked(g.guest, kP, Unlock::kStages));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kEverything));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCharacters));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCostumes));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kVoices));
  // Story clears only for Story characters: Ein, Gen Fu, Leon, Tengu,
  // Alpha-152 and Spartan-458 have no Story mode.
  const int no_story[] = {3, 6, 8, 12, 21, 22};
  for (int i : no_story) {
    CHECK(At(g, kStoryClears, i) == 0);
    CHECK(At(g, kStoryClearedNormal, i) == 0);
  }
  CHECK(At(g, kStoryClears, 7) == 1);  // Helena has Story mode
}

static void TestExistingProgressIsKept() {
  FakeGuest g;
  Setup(g, g_fresh);
  constexpr int kFar = 14;  // a character with room for five costumes
  CHECK(BaseMax(kFar) >= 5);
  g.bytes[kP + kStoryClears + 0] = 7;    // seven Story clears with the first character
  g.bytes[kP + kCostumeCount + kFar] = 5;  // five costumes of her maximum
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kStages);
  CHECK(At(g, kStoryClears, 0) == 7);
  CHECK(At(g, kCostumeCount, kFar) == BaseMax(kFar));
  CHECK(r.costumes == TotalExtras() - 3);  // she had 3 of her extras already
  CHECK(r.stages == 2);     // Biolab Core was open (Zack's clears)
}

static void TestSecondClickChangesNothing() {
  const Unlock all[] = {Unlock::kCharacters, Unlock::kCostumes, Unlock::kVoices, Unlock::kStages,
                        Unlock::kEverything};
  for (Unlock what : all) {
    FakeGuest g;
    Setup(g, g_fresh);
    CHECK(!IsUnlocked(g.guest, kP, what));
    ApplyUnlock(g.guest, kP, what);
    CHECK(IsUnlocked(g.guest, kP, what));
    g.writes = 0;
    UnlockResult r = ApplyUnlock(g.guest, kP, what);
    CHECK(r.characters == 0 && r.costumes == 0 && r.voices == 0 && r.stages == 0);
    CHECK(g.writes == 0);
  }
  // The 100% save is fully unlocked already.
  FakeGuest g;
  Setup(g, g_full);
  for (Unlock what : all) CHECK(IsUnlocked(g.guest, kP, what));
  CHECK(OpenStageCount(g.guest, kP) == 3);
}

// Tatami: every voice but Ein's, every costume but Alpha-152's.
static void TestTatamiRule() {
  FakeGuest g;
  Setup(g, g_full);
  g.bytes[kP + kVoiceUnlocked + kEinIndex] = 0;
  CHECK(OpenStageCount(g.guest, kP) == 3);
  g.bytes[kP + kVoiceUnlocked + 0] = 0;
  CHECK(OpenStageCount(g.guest, kP) == 2);
  FakeGuest g2;
  Setup(g2, g_full);
  g2.bytes[kP + kCostumeCount + kAlphaIndex] = 1;  // Alpha's count does not matter
  CHECK(OpenStageCount(g2.guest, kP) == 3);
  g2.bytes[kP + kCostumeCount + 0] = uint8_t(BaseMax(0) - 1);  // the first character one short of her maximum
  CHECK(OpenStageCount(g2.guest, kP) == 2);
  FakeGuest g3;
  Setup(g3, g_full);
  g3.bytes[kP + kStoryClears + 7] = 0;  // Helena's Story never cleared
  CHECK(OpenStageCount(g3.guest, kP) == 2);
}

// A save written while a costume tweak raised a maximum, loaded with the tweak off: FindProfile refuses it, and only the
// clamp can bring it back.
static void TestClampCostumeCounts() {
  FakeGuest g;
  Setup(g, g_full);
  constexpr int kHelena = kHiddenWithStory;
  g.bytes[kP + kCostumeCount + kHelena] = uint8_t(BaseMax(kHelena) + 1);  // one past her maximum
  CHECK(!FindProfile(g.guest));
  CHECK(FindLoadedRecord(g.guest) == std::optional<uint32_t>(kP));
  CHECK(ClampCostumeCounts(g.guest, kP) == 1);
  CHECK(At(g, kCostumeCount, kHelena) == BaseMax(kHelena));
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(kP));
  // Nothing past a maximum: nothing written, the counts below the maximum stay below it.
  g.writes = 0;
  CHECK(ClampCostumeCounts(g.guest, kP) == 0);
  CHECK(g.writes == 0);
  FakeGuest fresh;
  Setup(fresh, g_fresh);
  CHECK(ClampCostumeCounts(fresh.guest, kP) == 0);
  CHECK(At(fresh, kCostumeCount, kHelena) == 2);
}

// FindLoadedRecord keeps every check but the count ranges.
static void TestFindLoadedRecord() {
  FakeGuest wrong_slot;
  Setup(wrong_slot, g_fresh, 2, kP);
  CHECK(!FindLoadedRecord(wrong_slot.guest));
  FakeGuest past_slots;
  Setup(past_slots, g_fresh, 4, kRecordBase + 4 * kRecordStride + kRecordToProfile);
  CHECK(!FindLoadedRecord(past_slots.guest));
  // The game's boot defaults, before the save is read: no name, so nothing may be written yet.
  FakeGuest boot;
  Setup(boot, g_fresh);
  for (uint32_t i = 0; i < 32; ++i) boot.bytes[kP - kRecordToProfile + kRecordName + i] = 0;
  CHECK(!FindLoadedRecord(boot.guest));
  FakeGuest bad_table;
  Setup(bad_table, g_fresh);
  bad_table.bytes[kProfileIndexToChar + 3] = kCharIdCount;
  CHECK(!FindLoadedRecord(bad_table.guest));
  FakeGuest fine;
  Setup(fine, g_fresh);
  CHECK(FindLoadedRecord(fine.guest) == std::optional<uint32_t>(kP));
}

// The charId is mapped through the game's table: girl B is charId 0x0A, at profile index g_index_b.
static void TestRaiseCostumes() {
  FakeGuest g;
  Setup(g, g_fresh);
  CHECK(RaiseCostumes(g.guest, kP, kCharB));
  CHECK(At(g, kCostumeCount, g_index_b) == BaseMax(g_index_b));
  CHECK(At(g, kCostumeCount, kCharB) == 2);  // the cell at the charId as an index is another girl's: untouched
  CHECK(g.writes == 1);
  CHECK(!RaiseCostumes(g.guest, kP, kCharB));  // already there
  CHECK(g.writes == 1);
  // A character still locked becomes selectable: the one at profile index 7 is locked in a new save, and an added
  // costume she cannot be picked in would be no costume at all.
  FakeGuest h;
  Setup(h, g_fresh);
  CHECK(At(h, kCharacterUnlocked, kHiddenWithStory) == 0);
  CHECK(RaiseCostumes(h.guest, kP, g_char_h));
  CHECK(At(h, kCostumeCount, kHiddenWithStory) == BaseMax(kHiddenWithStory));
  CHECK(At(h, kCharacterUnlocked, kHiddenWithStory) == 1);
  CHECK(At(h, kCharacterUnlocked, 3) == 0);  // Ein stays locked
  // Only the flag missing (costumes already at the maximum): it still writes it, and says so.
  FakeGuest flag_only;
  Setup(flag_only, g_fresh);
  flag_only.bytes[kP + kCostumeCount + kHiddenWithStory] = uint8_t(BaseMax(kHiddenWithStory));
  CHECK(RaiseCostumes(flag_only.guest, kP, g_char_h));
  CHECK(At(flag_only, kCharacterUnlocked, kHiddenWithStory) == 1);
  CHECK(!RaiseCostumes(flag_only.guest, kP, g_char_h));
  // A raised maximum is what it raises to.
  g.bytes[kMaxCostumes + kCharB] = uint8_t(BaseMax(g_index_b) + kBoxCostumes);
  CHECK(RaiseCostumes(g.guest, kP, kCharB));
  CHECK(At(g, kCostumeCount, g_index_b) == BaseMax(g_index_b) + kBoxCostumes);
  // A charId with no profile index: nothing.
  g.writes = 0;
  CHECK(!RaiseCostumes(g.guest, kP, g_unused));
  CHECK(g.writes == 0);
}

// ---- Task 3: the import changes no saved count and re-locks no stage ----
//
// The import raises the table's maximum by the girl's suits (costumes::Apply) and tells SetSuitCostumes how many. The
// setup: girl B (charId 0x0A) with her box on (her maximum + 2) and four suits (+ 4 more); girl A (charId 5) with four
// suits (her maximum + 4). Their own maxima are the tables' (BaseMax).
static int KasumiIndex() { return g_index_a; }
static int KokoroIndex() { return g_index_b; }

static void Import(FakeGuest& g, bool kokoro_box = true) {
  g.bytes[kMaxCostumes + kCharB] = uint8_t(BaseMax(g_index_b) + (kokoro_box ? kBoxCostumes : 0) + kSuitsPerGirl);
  g.bytes[kMaxCostumes + kCharA] = uint8_t(BaseMax(g_index_a) + kSuitsPerGirl);
  std::array<uint8_t, kCharIdCount> suits{};
  suits[kCharB] = kSuitsPerGirl;
  suits[kCharA] = kSuitsPerGirl;
  SetSuitCostumes(suits);
}
static void Removed(FakeGuest& g, bool kokoro_box = true) {
  g.bytes[kMaxCostumes + kCharB] = uint8_t(BaseMax(g_index_b) + (kokoro_box ? kBoxCostumes : 0));
  g.bytes[kMaxCostumes + kCharA] = uint8_t(BaseMax(g_index_a));
  SetSuitCostumes({});
}

// What the UI tick does to the save every 100 ms (SyncCostumeCounts in dead_or_alive_4_app.h), without the guest.
static void Tick(FakeGuest& g, bool kokoro_box_on) {
  if (const std::optional<uint32_t> loaded = FindLoadedRecord(g.guest)) {
    ClampCostumeCounts(g.guest, *loaded);
    if (FindProfile(g.guest) && kokoro_box_on) RaiseCostumes(g.guest, *loaded, kCharB);
  }
}

// Every byte of the profile's fixture range, for "the save is unchanged".
static std::vector<uint8_t> Profile(const FakeGuest& g) {
  std::vector<uint8_t> out;
  for (uint32_t i = 0; i < 140; ++i) out.push_back(g.Get(kP + kFixtureStart + i));
  return out;
}

// The import changes no count. A box on Kokoro is the game's own v1.0.9 behaviour (the tick raises her to the box's
// maximum), so with the box on the check is against the same save without the import. With the box off a tick writes
// nothing at all, on a new save (counts 2) and on one that earned everything (the fixture's maxima). The positive
// control is the same tick on a count the instrument must act on (a count above the maximum): it does write, so
// "nothing written" is not "the tick never ran".
static void TestImportLeavesTheCounts() {
  for (const bool earned : {false, true}) {
    FakeGuest g;
    Setup(g, earned ? g_full : g_fresh);
    Import(g, false);
    const std::vector<uint8_t> before = Profile(g);
    g.writes = 0;
    for (int i = 0; i < 5; ++i) Tick(g, false);
    CHECK(Profile(g) == before);
    CHECK(g.writes == 0);  // not one write, not even of the same value
  }
  for (const bool earned : {false, true}) {  // the box on: with the import = without it, byte for byte
    FakeGuest with, without;
    Setup(with, earned ? g_full : g_fresh);
    Setup(without, earned ? g_full : g_fresh);
    const uint8_t box_max = uint8_t(BaseMax(g_index_b) + kBoxCostumes);
    without.bytes[kMaxCostumes + kCharB] = box_max;
    with.bytes[kMaxCostumes + kCharB] = box_max;
    SetSuitCostumes({});
    for (int i = 0; i < 5; ++i) Tick(without, true);  // the game as it is: no suits told, only the box's maximum
    Import(with, true);
    for (int i = 0; i < 5; ++i) Tick(with, true);
    CHECK(Profile(with) == Profile(without));
    CHECK(At(with, kCostumeCount, KokoroIndex()) == box_max);  // the box's maximum, NOT the one with her suits
    CHECK(At(with, kCostumeCount, KasumiIndex()) == (earned ? BaseMax(KasumiIndex()) : 2));
    SetSuitCostumes({});
  }
  FakeGuest control;
  Setup(control, g_fresh);
  Import(control, false);
  const int kasumi_table_max = BaseMax(KasumiIndex()) + kSuitsPerGirl;
  control.bytes[kP + kCostumeCount + KasumiIndex()] = uint8_t(kasumi_table_max + 1);  // above her table maximum
  control.writes = 0;
  Tick(control, false);
  CHECK(control.writes >= 1 && At(control, kCostumeCount, KasumiIndex()) == kasumi_table_max);
  SetSuitCostumes({});
}

// Tatami: a save that earned it (every count at the game's own maximum) keeps it with the suits in the tables. The
// control: with the maximum raised and the suits not told (what the table alone does), it re-locks.
static void TestImportKeepsTatami() {
  FakeGuest g;
  Setup(g, g_full);
  CHECK(OpenStageCount(g.guest, kP) == 3);
  CHECK(TatamiOpen(g.guest, kP));
  Import(g, false);
  CHECK(TatamiOpen(g.guest, kP));
  CHECK(OpenStageCount(g.guest, kP) == 3);
  SetSuitCostumes({});  // the control: the raised maximum alone
  CHECK(!TatamiOpen(g.guest, kP));
  CHECK(OpenStageCount(g.guest, kP) == 2);
  // A count one short of the game's own maximum still keeps it locked: the suits are not a way in.
  Import(g, false);
  g.bytes[kP + kCostumeCount + KasumiIndex()] = uint8_t(BaseMax(KasumiIndex()) - 1);
  CHECK(!TatamiOpen(g.guest, kP));
  g.bytes[kP + kCostumeCount + KasumiIndex()] = uint8_t(BaseMax(KasumiIndex()));
  CHECK(TatamiOpen(g.guest, kP));
  // The rest of the rule is unchanged by the suits.
  g.bytes[kP + kVoiceUnlocked + 0] = 0;
  CHECK(!TatamiOpen(g.guest, kP));
  SetSuitCostumes({});
}

// "Unlock all costumes" is an explicit act, yet it writes each girl's own maximum and never the imported suits: they are
// offered on the select screen without the save's count, so the unlock note "Your save isn't changed" stays true. Taking the
// import away afterwards leaves a save the program accepts and a clamp with nothing to do.
static void TestUnlockAllThenImportRemoved() {
  FakeGuest g;
  Setup(g, g_fresh);
  Import(g);
  CHECK(ApplyUnlock(g.guest, kP, Unlock::kCostumes).costumes > 0);
  CHECK(At(g, kCostumeCount, KasumiIndex()) == BaseMax(KasumiIndex()) &&
        At(g, kCostumeCount, KokoroIndex()) == BaseMax(KokoroIndex()) + kBoxCostumes);  // her own, box included, no suits
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(kP));
  Removed(g);  // the import folder is gone: the tables are the game's again
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(kP));  // never refused: nothing was written past the game's maximum
  const std::vector<uint8_t> before = Profile(g);
  g.writes = 0;
  Tick(g, true);
  CHECK(g.writes == 0 && Profile(g) == before);  // the clamp has nothing to correct
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCostumes));
}

// Girl A (charId 5) has four suits: her table maximum is her own + 4. Unlock all costumes writes her own.
static void TestUnlockCostumesWritesOwnMax() {
  FakeGuest g;
  Setup(g, g_fresh);
  Import(g, false);
  const int own = BaseMax(KasumiIndex());
  CHECK(g.Get(kMaxCostumes + kCharA) == own + kSuitsPerGirl && own != own + kSuitsPerGirl);  // the table says more
  const UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kCostumes);
  CHECK(At(g, kCostumeCount, KasumiIndex()) == own);
  CHECK(At(g, kCostumeCount, KasumiIndex()) != own + kSuitsPerGirl);
  CHECK(At(g, kCostumeCount, KokoroIndex()) == BaseMax(KokoroIndex()));  // girl B, four suits too
  CHECK(At(g, kCostumeCount, g_index_c) == BaseMax(g_index_c));          // a girl with none
  CHECK(r.costumes == TotalExtras());                                    // counted against the own maxima
  // A count already between her own maximum and the table's is kept, not lowered.
  g.bytes[kP + kCostumeCount + KasumiIndex()] = uint8_t(own + 2);
  ApplyUnlock(g.guest, kP, Unlock::kCostumes);
  CHECK(At(g, kCostumeCount, KasumiIndex()) == own + 2);
  SetSuitCostumes({});
}

// Unlocked means at the own maximum: true there, false one below, with the suits in the table. A save that has everything
// else makes kEverything and kStages follow the costume rule alone.
static void TestIsUnlockedAtOwnMax() {
  FakeGuest g;
  Setup(g, g_full);
  Import(g, false);
  const int own = BaseMax(KasumiIndex());
  CHECK(At(g, kCostumeCount, KasumiIndex()) == own);  // the full save holds her own maximum
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCostumes));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kStages) && IsUnlocked(g.guest, kP, Unlock::kEverything));
  g.bytes[kP + kCostumeCount + KasumiIndex()] = uint8_t(own - 1);
  CHECK(!IsUnlocked(g.guest, kP, Unlock::kCostumes));
  CHECK(!IsUnlocked(g.guest, kP, Unlock::kStages) && !IsUnlocked(g.guest, kP, Unlock::kEverything));
  g.bytes[kP + kCostumeCount + KasumiIndex()] = uint8_t(own);
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCostumes));
  SetSuitCostumes({});
}

// Unlock everything on a new save with suits: every girl at her own maximum, nothing of the suits, and a second click
// changes nothing.
static void TestUnlockEverythingLeavesSuitsOut() {
  FakeGuest g;
  Setup(g, g_fresh);
  Import(g, false);
  CHECK(!IsUnlocked(g.guest, kP, Unlock::kEverything));
  ApplyUnlock(g.guest, kP, Unlock::kEverything);
  for (int i = 0; i < kCharacterCount; ++i) CHECK(At(g, kCostumeCount, i) == BaseMax(i));
  CHECK(At(g, kCostumeCount, KasumiIndex()) < g.Get(kMaxCostumes + kCharA));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kEverything));
  g.writes = 0;
  const UnlockResult again = ApplyUnlock(g.guest, kP, Unlock::kEverything);
  CHECK(g.writes == 0 && again.costumes == 0);
  SetSuitCostumes({});
}

// The online session's frame step is not the player's "Unlock all": the session's save is its own copy, and in a session the
// select screen's count copy is not raised (the hooks leave the game's rule), so the suits show only because the step writes
// the TABLE maximum. CostumeMax::kTable asks for exactly that; the default stays each girl's own maximum.
static void TestSessionUnlockKeepsTableMax() {
  FakeGuest g;
  Setup(g, g_fresh);
  Import(g, false);
  const int own = BaseMax(KasumiIndex());
  const int table = own + kSuitsPerGirl;
  CHECK(!IsUnlocked(g.guest, kP, Unlock::kEverything, CostumeMax::kTable));
  ApplyUnlock(g.guest, kP, Unlock::kEverything, CostumeMax::kTable);
  CHECK(At(g, kCostumeCount, KasumiIndex()) == table);                                  // girl A: her own + 4
  CHECK(At(g, kCostumeCount, KokoroIndex()) == BaseMax(KokoroIndex()) + kSuitsPerGirl);  // girl B too (no box here)
  CHECK(At(g, kCostumeCount, g_index_c) == BaseMax(g_index_c));                          // a girl with none
  CHECK(IsUnlocked(g.guest, kP, Unlock::kEverything, CostumeMax::kTable));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kStages, CostumeMax::kTable));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCostumes, CostumeMax::kTable));
  CHECK(IsUnlocked(g.guest, kP, Unlock::kEverything));  // at the table's, so also at the own maximum
  g.writes = 0;
  const UnlockResult again = ApplyUnlock(g.guest, kP, Unlock::kEverything, CostumeMax::kTable);
  CHECK(g.writes == 0 && again.costumes == 0);
  // The table's rule is false at the own maximum; the own rule is true there.
  FakeGuest full;
  Setup(full, g_full);
  Import(full, false);
  CHECK(At(full, kCostumeCount, KasumiIndex()) == own);
  CHECK(!IsUnlocked(full.guest, kP, Unlock::kEverything, CostumeMax::kTable));
  CHECK(!IsUnlocked(full.guest, kP, Unlock::kCostumes, CostumeMax::kTable));
  CHECK(IsUnlocked(full.guest, kP, Unlock::kEverything));
  CHECK(IsUnlocked(full.guest, kP, Unlock::kEverything, CostumeMax::kOwn));  // the default is kOwn
  // The default and kOwn write the same bytes.
  FakeGuest by_default, explicit_own;
  Setup(by_default, g_fresh);
  Setup(explicit_own, g_fresh);
  Import(by_default, false);
  Import(explicit_own, false);
  ApplyUnlock(by_default.guest, kP, Unlock::kEverything);
  ApplyUnlock(explicit_own.guest, kP, Unlock::kEverything, CostumeMax::kOwn);
  CHECK(Profile(by_default) == Profile(explicit_own));
  CHECK(At(by_default, kCostumeCount, KasumiIndex()) == own);
  SetSuitCostumes({});
}

// No suits told: the counts equal the table's maximum, as before.
static void TestNoSuitsUnchanged() {
  FakeGuest g;
  Setup(g, g_fresh);
  SetSuitCostumes({});
  const UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kCostumes);
  for (int i = 0; i < kCharacterCount; ++i) CHECK(At(g, kCostumeCount, i) == g.Get(kMaxCostumes + g_t.index_to_char[size_t(i)]));
  CHECK(r.costumes == TotalExtras());
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCostumes));
}

// The clamp still uses the table's whole maximum: a count of her own + 2 (inside the table's, past her own) is not lowered,
// and one past the table's is.
static void TestClampKeepsWholeMax() {
  FakeGuest g;
  Setup(g, g_full);
  Import(g, false);
  const int own = BaseMax(KasumiIndex());
  g.bytes[kP + kCostumeCount + KasumiIndex()] = uint8_t(own + 2);
  g.writes = 0;
  CHECK(ClampCostumeCounts(g.guest, kP) == 0 && g.writes == 0);
  CHECK(At(g, kCostumeCount, KasumiIndex()) == own + 2);
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(kP));
  g.bytes[kP + kCostumeCount + KasumiIndex()] = uint8_t(own + kSuitsPerGirl + 1);
  CHECK(!FindProfile(g.guest));
  CHECK(ClampCostumeCounts(g.guest, kP) == 1);
  CHECK(At(g, kCostumeCount, KasumiIndex()) == own + kSuitsPerGirl);
  SetSuitCostumes({});
}

// Fix round 1: sub_82408398(charId) = "her count >= her maximum" (the Story-clear unlock sub_824082F0 and the award
// sub_824C3238 count++ only when it says no). With suits in the table the maximum is the raised one, so a girl at her
// own maximum would earn another count. Offline with suits it compares against her own maximum; online, and for a
// girl without suits, it is the table's. The charId -> profile index table (Setup writes the inverse of the profile
// index -> charId table; the game's is at 0x82C59FF8) is what CostumesComplete reads.
static void TestCostumesComplete() {
  FakeGuest g;
  Setup(g, g_fresh);
  Import(g, false);  // girl A (charId 5): table maximum = her own + 4 suits; girl B the same
  const auto count = [&](int index, int v) { g.bytes[kP + kCostumeCount + uint32_t(index)] = uint8_t(v); };
  const int own = BaseMax(KasumiIndex()), table = own + kSuitsPerGirl;
  // Precondition: own maximum and raised maximum differ, so the wrong rule cannot pass.
  CHECK(g.Get(kMaxCostumes + kCharA) == table && own != table);
  count(KasumiIndex(), own);
  CHECK(CostumesComplete(g.guest, kP, kCharA, true));    // at her own maximum: complete (the table's says no)
  count(KasumiIndex(), own - 1);
  CHECK(!CostumesComplete(g.guest, kP, kCharA, true));   // one below her own maximum
  count(KasumiIndex(), table);
  CHECK(CostumesComplete(g.guest, kP, kCharA, true));    // above: complete
  // Online (not offline) the game's own rule: the table's maximum.
  count(KasumiIndex(), own);
  CHECK(!CostumesComplete(g.guest, kP, kCharA, false));
  count(KasumiIndex(), table);
  CHECK(CostumesComplete(g.guest, kP, kCharA, false));
  // A girl with no suits (charId 13): the table's, offline or not.
  const int c_max = BaseMax(g_index_c);
  count(g_index_c, c_max - 1);
  CHECK(!CostumesComplete(g.guest, kP, kCharC, true) && !CostumesComplete(g.guest, kP, kCharC, false));
  count(g_index_c, c_max);
  CHECK(CostumesComplete(g.guest, kP, kCharC, true) && CostumesComplete(g.guest, kP, kCharC, false));
  // A charId past the 25: no maximum (0), so always complete, as the game's.
  CHECK(CostumesComplete(g.guest, kP, 25, true));
  // Nothing is written by asking.
  g.writes = 0;
  CostumesComplete(g.guest, kP, kCharA, true);
  CHECK(g.writes == 0);
  SetSuitCostumes({});
}

// The full save wiped: every unlock field equals the fresh save's; every other byte stays the full save's
// (its other bytes differ from the fresh save's, so "copy the fresh profile" would fail here).
static void TestWipe() {
  FakeGuest g;
  Setup(g, g_full);
  CHECK(!IsWiped(g.guest, kP));
  CHECK(WipeUnlocks(g.guest, kP) > 0);
  const auto in_unlock_field = [](uint32_t offset) {
    return offset == kFirstStoryClear ||
           (offset >= kCostumeCount && offset < kCostumeCount + kCharacterCount) ||
           (offset >= kCharacterUnlocked && offset < kCharacterUnlocked + kCharacterCount) ||
           (offset >= kStoryClears && offset < kStoryClears + kCharacterCount) ||
           (offset >= kStoryClearedNormal && offset < kStoryClearedNormal + kCharacterCount) ||
           (offset >= kVoiceUnlocked && offset < kVoiceUnlocked + kCharacterCount);
  };
  int kept_differs_from_fresh = 0;
  for (uint32_t offset = kFixtureStart; offset < kFixtureStart + 0x8C; ++offset) {
    if (in_unlock_field(offset)) {
      CHECK(At(g, offset) == Fresh(offset));
    } else {
      CHECK(At(g, offset) == Full(offset));
      if (Full(offset) != Fresh(offset)) ++kept_differs_from_fresh;
    }
  }
  CHECK(kept_differs_from_fresh > 0);  // the fixture can tell "kept" from "reset"
  CHECK(IsWiped(g.guest, kP));
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(kP));
  CHECK(OpenStageCount(g.guest, kP) == 0);
  for (const Unlock what :
       {Unlock::kCharacters, Unlock::kCostumes, Unlock::kVoices, Unlock::kStages, Unlock::kEverything}) {
    CHECK(!IsUnlocked(g.guest, kP, what));
  }
  // A second wipe, and a wipe of a really fresh save, change nothing.
  g.writes = 0;
  CHECK(WipeUnlocks(g.guest, kP) == 0);
  CHECK(g.writes == 0);
  FakeGuest fresh;
  Setup(fresh, g_fresh);
  CHECK(IsWiped(fresh.guest, kP));
  CHECK(WipeUnlocks(fresh.guest, kP) == 0);
  CHECK(fresh.writes == 0);
  // After a wipe the buttons work again.
  ApplyUnlock(g.guest, kP, Unlock::kEverything);
  CHECK(IsUnlocked(g.guest, kP, Unlock::kEverything));
}

// IsWiped looks at every field: one byte unlocked anywhere is not wiped.
static void TestIsWipedSeesEveryField() {
  const auto one_changed = [](uint32_t offset, uint8_t value) {
    FakeGuest g;
    Setup(g, g_fresh);
    g.bytes[kP + offset] = value;
    return IsWiped(g.guest, kP);
  };
  CHECK(!one_changed(kFirstStoryClear, 1));
  for (int i = 0; i < kCharacterCount; ++i) {
    CHECK(!one_changed(kCostumeCount + uint32_t(i), 3));
    CHECK(!one_changed(kCharacterUnlocked + uint32_t(i), Fresh(kCharacterUnlocked, i) ? 0 : 1));
    CHECK(!one_changed(kStoryClears + uint32_t(i), 1));
    CHECK(!one_changed(kStoryClearedNormal + uint32_t(i), 1));
    CHECK(!one_changed(kVoiceUnlocked + uint32_t(i), 1));
  }
}

// Every test, on the tables set by UseTables.
static void RunAll() {
  TestClampCostumeCounts();
  TestFindLoadedRecord();
  TestRaiseCostumes();
  TestWipe();
  TestIsWipedSeesEveryField();
  TestFreshProfileIsANewSave();
  TestFindProfile();
  TestEverythingMatchesTheFullSave();
  TestEachButtonOnlyItsOwnBytes();
  TestStagesDoesWhatItTakes();
  TestExistingProgressIsKept();
  TestSecondClickChangesNothing();
  TestTatamiRule();
  TestImportLeavesTheCounts();
  TestImportKeepsTatami();
  TestUnlockAllThenImportRemoved();
  TestUnlockCostumesWritesOwnMax();
  TestIsUnlockedAtOwnMax();
  TestUnlockEverythingLeavesSuitsOut();
  TestSessionUnlockKeepsTableMax();
  TestNoSuitsUnchanged();
  TestClampKeepsWholeMax();
  TestCostumesComplete();
}

int main() {
  UseTables(SyntheticTables());
  RunAll();
  std::string why;
  if (const std::optional<Tables> image = ImageTables(&why)) {
    UseTables(*image);
    RunAll();
  } else if (doa4_test::ReportNoImage("unlocks: the checks on the image's tables", why)) {
    ++g_checks;
    ++g_failures;
  }
  std::printf("unlocks: %d checks run\n", g_checks);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("unlocks: all checks passed\n");
  return 0;
}
