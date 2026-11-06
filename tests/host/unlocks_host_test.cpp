// Host test for src/doa4_unlocks.cpp, on the game's real tables and real save
// bytes (doa4_unlock_fixtures.h). Built and run by run_unlocks_test.bat.

#include "../../src/doa4_unlocks.h"

#include <cstdio>
#include <map>
#include <vector>

#include "doa4_unlock_fixtures.h"

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::unlocks;
namespace fx = doa4_fixtures;

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
constexpr uint32_t kFixtureStart = 0x3FC0;  // fixtures hold P+0x3FC0..P+0x404B

// The game's tables, the profile pointer and slot, and a profile's bytes at the pointer.
template <size_t N>
static void Setup(FakeGuest& g, const std::array<uint8_t, N>& profile, uint32_t slot = kSlot,
                  uint32_t pointer = kP) {
  for (size_t i = 0; i < fx::kProfileIndexToChar.size(); ++i)
    g.bytes[kProfileIndexToChar + uint32_t(i)] = fx::kProfileIndexToChar[i];
  for (size_t i = 0; i < fx::kMaxCostumes.size(); ++i)
    g.bytes[kMaxCostumes + uint32_t(i)] = fx::kMaxCostumes[i];
  for (size_t k = 0; k < fx::kStoryRosterChars.size(); ++k)
    g.bytes[kStoryRoster + kStoryRosterStride * uint32_t(k)] = fx::kStoryRosterChars[k];
  g.SetBe32(kProfilePointer, pointer);
  g.SetBe32(kProfileSlot, slot);
  // The profile bytes go where the pointer points, so only the pointer checks
  // can reject a bad pointer.
  for (size_t i = 0; i < N; ++i) g.bytes[pointer + kFixtureStart + uint32_t(i)] = profile[i];
  // The profile name, as in a loaded save: "User" in UTF-16BE at record+8.
  const char* name = "User";
  for (uint32_t i = 0; name[i]; ++i) g.bytes[pointer - kRecordToProfile + kRecordName + 2 * i + 1] = uint8_t(name[i]);
  g.writes = 0;
}

static uint8_t At(const FakeGuest& g, uint32_t offset, int index = 0) {
  return g.Get(kP + offset + uint32_t(index));
}
static uint8_t Full(uint32_t offset, int index = 0) {
  return fx::kFullProfile[offset - kFixtureStart + index];
}
static uint8_t Fresh(uint32_t offset, int index = 0) {
  return fx::kFreshProfile[offset - kFixtureStart + index];
}

static void TestFixturesAreWhatTheResearchSays() {
  // Fresh save: every costume count 2; the 7 hidden characters (Helena, Ein,
  // Gen Fu, Leon, Tengu, Alpha-152, Spartan-458) locked.
  for (int i = 0; i < kCharacterCount; ++i) {
    CHECK(Fresh(kCostumeCount, i) == 2);
    const bool hidden = i == 3 || i == 6 || i == 7 || i == 8 || i == 12 || i == 21 || i == 22;
    CHECK(Fresh(kCharacterUnlocked, i) == (hidden ? 0 : 1));
  }
}

static void TestFindProfile() {
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(0x831B0A40));
  CHECK(g.writes == 0);
  // The slot index says 2 but the pointer still names slot 0's record.
  FakeGuest g2;
  Setup(g2, fx::kFreshProfile, 2, kP);
  CHECK(!FindProfile(g2.guest));
  // Null pointer, pointer off the formula, slot index past the last record.
  FakeGuest g3;
  Setup(g3, fx::kFreshProfile, kSlot, 0);
  CHECK(!FindProfile(g3.guest));
  FakeGuest g4;
  Setup(g4, fx::kFreshProfile, kSlot, kP + 4);
  CHECK(!FindProfile(g4.guest));
  FakeGuest g5;
  Setup(g5, fx::kFreshProfile, 4, kRecordBase + 4 * kRecordStride + kRecordToProfile);
  CHECK(!FindProfile(g5.guest));
  // A record the game has not loaded (all zero) is not a profile.
  FakeGuest g6;
  Setup(g6, std::array<uint8_t, 0x8C>{});
  CHECK(!FindProfile(g6.guest));
  // One costume count past its maximum, or one at 1, is not a profile either.
  FakeGuest g7;
  Setup(g7, fx::kFreshProfile);
  g7.bytes[kP + kCostumeCount + kAlphaIndex] = 3;  // Alpha-152's maximum is 2
  CHECK(!FindProfile(g7.guest));
  FakeGuest g8;
  Setup(g8, fx::kFreshProfile);
  g8.bytes[kP + kCostumeCount + kSpartanIndex] = 1;
  CHECK(!FindProfile(g8.guest));
  // The game's default record before the save is read (seen at runtime from
  // 1 s after launch until Start is pressed): fresh costume counts and
  // character flags, but no name. Not a loaded profile.
  FakeGuest g10;
  Setup(g10, fx::kFreshProfile);
  for (uint32_t i = 0; i < 32; ++i) g10.bytes[kP - kRecordToProfile + kRecordName + i] = 0;
  CHECK(!FindProfile(g10.guest));
  // A name is enough: one UTF-16 character.
  g10.bytes[kP - kRecordToProfile + kRecordName + 1] = 'A';
  CHECK(FindProfile(g10.guest).has_value());
  // The 100% save is a profile.
  FakeGuest g9;
  Setup(g9, fx::kFullProfile);
  CHECK(FindProfile(g9.guest).has_value());
}

static void TestEverythingMatchesTheFullSave() {
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
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
  CHECK(r.costumes == 61);  // sum of (max - 2) over the 23 characters
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
    Setup(pristine, fx::kFreshProfile);
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
    Setup(g, fx::kFreshProfile);
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
  Setup(g, fx::kFreshProfile);
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
  Setup(g, fx::kFreshProfile);
  CHECK(OpenStageCount(g.guest, kP) == 0);
  CHECK(!IsUnlocked(g.guest, kP, Unlock::kStages));
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kStages);
  CHECK(r.stages == 3 && r.characters == 6 && r.costumes == 61 && r.voices == 23);
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
  Setup(g, fx::kFreshProfile);
  g.bytes[kP + kStoryClears + 0] = 7;    // seven Story clears with Zack
  g.bytes[kP + kCostumeCount + 14] = 5;  // Hitomi: 5 of 8 costumes
  UnlockResult r = ApplyUnlock(g.guest, kP, Unlock::kStages);
  CHECK(At(g, kStoryClears, 0) == 7);
  CHECK(At(g, kCostumeCount, 14) == 8);
  CHECK(r.costumes == 58);  // Hitomi had 3 of her 6 extras already
  CHECK(r.stages == 2);     // Biolab Core was open (Zack's clears)
}

static void TestSecondClickChangesNothing() {
  const Unlock all[] = {Unlock::kCharacters, Unlock::kCostumes, Unlock::kVoices, Unlock::kStages,
                        Unlock::kEverything};
  for (Unlock what : all) {
    FakeGuest g;
    Setup(g, fx::kFreshProfile);
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
  Setup(g, fx::kFullProfile);
  for (Unlock what : all) CHECK(IsUnlocked(g.guest, kP, what));
  CHECK(OpenStageCount(g.guest, kP) == 3);
}

// Tatami: every voice but Ein's, every costume but Alpha-152's.
static void TestTatamiRule() {
  FakeGuest g;
  Setup(g, fx::kFullProfile);
  g.bytes[kP + kVoiceUnlocked + kEinIndex] = 0;
  CHECK(OpenStageCount(g.guest, kP) == 3);
  g.bytes[kP + kVoiceUnlocked + 0] = 0;
  CHECK(OpenStageCount(g.guest, kP) == 2);
  FakeGuest g2;
  Setup(g2, fx::kFullProfile);
  g2.bytes[kP + kCostumeCount + kAlphaIndex] = 1;  // Alpha's count does not matter
  CHECK(OpenStageCount(g2.guest, kP) == 3);
  g2.bytes[kP + kCostumeCount + 0] = 3;  // Zack one short of 4
  CHECK(OpenStageCount(g2.guest, kP) == 2);
  FakeGuest g3;
  Setup(g3, fx::kFullProfile);
  g3.bytes[kP + kStoryClears + 7] = 0;  // Helena's Story never cleared
  CHECK(OpenStageCount(g3.guest, kP) == 2);
}

// A save written while a costume tweak raised a maximum, loaded with the tweak off: FindProfile refuses it, and only the
// clamp can bring it back.
static void TestClampCostumeCounts() {
  FakeGuest g;
  Setup(g, fx::kFullProfile);
  constexpr int kHelena = 7;
  g.bytes[kP + kCostumeCount + kHelena] = 6;  // her maximum is 5
  CHECK(!FindProfile(g.guest));
  CHECK(FindLoadedRecord(g.guest) == std::optional<uint32_t>(kP));
  CHECK(ClampCostumeCounts(g.guest, kP) == 1);
  CHECK(At(g, kCostumeCount, kHelena) == 5);
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(kP));
  // Nothing past a maximum: nothing written, the counts below the maximum stay below it.
  g.writes = 0;
  CHECK(ClampCostumeCounts(g.guest, kP) == 0);
  CHECK(g.writes == 0);
  FakeGuest fresh;
  Setup(fresh, fx::kFreshProfile);
  CHECK(ClampCostumeCounts(fresh.guest, kP) == 0);
  CHECK(At(fresh, kCostumeCount, kHelena) == 2);
}

// FindLoadedRecord keeps every check but the count ranges.
static void TestFindLoadedRecord() {
  FakeGuest wrong_slot;
  Setup(wrong_slot, fx::kFreshProfile, 2, kP);
  CHECK(!FindLoadedRecord(wrong_slot.guest));
  FakeGuest past_slots;
  Setup(past_slots, fx::kFreshProfile, 4, kRecordBase + 4 * kRecordStride + kRecordToProfile);
  CHECK(!FindLoadedRecord(past_slots.guest));
  // The game's boot defaults, before the save is read: no name, so nothing may be written yet.
  FakeGuest boot;
  Setup(boot, fx::kFreshProfile);
  for (uint32_t i = 0; i < 32; ++i) boot.bytes[kP - kRecordToProfile + kRecordName + i] = 0;
  CHECK(!FindLoadedRecord(boot.guest));
  FakeGuest bad_table;
  Setup(bad_table, fx::kFreshProfile);
  bad_table.bytes[kProfileIndexToChar + 3] = kCharIdCount;
  CHECK(!FindLoadedRecord(bad_table.guest));
  FakeGuest fine;
  Setup(fine, fx::kFreshProfile);
  CHECK(FindLoadedRecord(fine.guest) == std::optional<uint32_t>(kP));
}

// The charId is mapped through the game's table: Kokoro is charId 0x0A, profile index 18.
static void TestRaiseCostumes() {
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
  CHECK(RaiseCostumes(g.guest, kP, 0x0A));
  CHECK(At(g, kCostumeCount, 18) == 7);
  CHECK(At(g, kCostumeCount, 0x0A) == 2);  // profile index 10 is Lei Fang: not the charId's index
  CHECK(g.writes == 1);
  CHECK(!RaiseCostumes(g.guest, kP, 0x0A));  // already there
  CHECK(g.writes == 1);
  // A character still locked becomes selectable: Helena (charId 7, profile index 7) is locked in a new save, and an
  // added costume she cannot be picked in would be no costume at all.
  FakeGuest h;
  Setup(h, fx::kFreshProfile);
  CHECK(At(h, kCharacterUnlocked, 7) == 0);
  CHECK(RaiseCostumes(h.guest, kP, 0x07));
  CHECK(At(h, kCostumeCount, 7) == 5);
  CHECK(At(h, kCharacterUnlocked, 7) == 1);
  CHECK(At(h, kCharacterUnlocked, 3) == 0);  // Ein stays locked
  // Only the flag missing (costumes already at the maximum): it still writes it, and says so.
  FakeGuest flag_only;
  Setup(flag_only, fx::kFreshProfile);
  flag_only.bytes[kP + kCostumeCount + 7] = 5;
  CHECK(RaiseCostumes(flag_only.guest, kP, 0x07));
  CHECK(At(flag_only, kCharacterUnlocked, 7) == 1);
  CHECK(!RaiseCostumes(flag_only.guest, kP, 0x07));
  // A raised maximum is what it raises to.
  g.bytes[kMaxCostumes + 0x0A] = 9;
  CHECK(RaiseCostumes(g.guest, kP, 0x0A));
  CHECK(At(g, kCostumeCount, 18) == 9);
  // A charId with no profile index (the unused RTM, 0x16): nothing.
  g.writes = 0;
  CHECK(!RaiseCostumes(g.guest, kP, 0x16));
  CHECK(g.writes == 0);
}

// ---- Task 3: the import changes no saved count and re-locks no stage ----
//
// The import raises the table's maximum by the girl's suits (costumes::Apply) and tells SetSuitCostumes how many. The
// fixtures: Kokoro (charId 0x0A, profile index 18) with her box on (maximum 9) and four suits (13); Kasumi (charId 5,
// profile index 5, maximum 7) with four suits (11).
constexpr int kKasumiIndex = 5, kKokoroIndex = 18;

static void Import(FakeGuest& g, bool kokoro_box = true) {
  g.bytes[kMaxCostumes + 0x0A] = kokoro_box ? 13 : 11;
  g.bytes[kMaxCostumes + 5] = 11;
  std::array<uint8_t, kCharIdCount> suits{};
  suits[0x0A] = 4;
  suits[5] = 4;
  SetSuitCostumes(suits);
}
static void Removed(FakeGuest& g, bool kokoro_box = true) {
  g.bytes[kMaxCostumes + 0x0A] = kokoro_box ? 9 : 7;
  g.bytes[kMaxCostumes + 5] = 7;
  SetSuitCostumes({});
}

// What the UI tick does to the save every 100 ms (SyncCostumeCounts in dead_or_alive_4_app.h), without the guest.
static void Tick(FakeGuest& g, bool kokoro_box_on) {
  if (const std::optional<uint32_t> loaded = FindLoadedRecord(g.guest)) {
    ClampCostumeCounts(g.guest, *loaded);
    if (FindProfile(g.guest) && kokoro_box_on) RaiseCostumes(g.guest, *loaded, 0x0A);
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
    Setup(g, earned ? fx::kFullProfile : fx::kFreshProfile);
    Import(g, false);
    const std::vector<uint8_t> before = Profile(g);
    g.writes = 0;
    for (int i = 0; i < 5; ++i) Tick(g, false);
    CHECK(Profile(g) == before);
    CHECK(g.writes == 0);  // not one write, not even of the same value
  }
  for (const bool earned : {false, true}) {  // the box on: with the import = without it, byte for byte
    FakeGuest with, without;
    Setup(with, earned ? fx::kFullProfile : fx::kFreshProfile);
    Setup(without, earned ? fx::kFullProfile : fx::kFreshProfile);
    without.bytes[kMaxCostumes + 0x0A] = 9;
    with.bytes[kMaxCostumes + 0x0A] = 9;
    SetSuitCostumes({});
    for (int i = 0; i < 5; ++i) Tick(without, true);  // the game as it is: no suits told, the box maximum 9
    Import(with, true);
    for (int i = 0; i < 5; ++i) Tick(with, true);
    CHECK(Profile(with) == Profile(without));
    CHECK(At(with, kCostumeCount, kKokoroIndex) == 9);  // the box's maximum, NOT the 13 with her suits
    CHECK(At(with, kCostumeCount, kKasumiIndex) == (earned ? 7 : 2));
    SetSuitCostumes({});
  }
  FakeGuest control;
  Setup(control, fx::kFreshProfile);
  Import(control, false);
  control.bytes[kP + kCostumeCount + kKasumiIndex] = 12;  // above her 11
  control.writes = 0;
  Tick(control, false);
  CHECK(control.writes >= 1 && At(control, kCostumeCount, kKasumiIndex) == 11);
  SetSuitCostumes({});
}

// Tatami: a save that earned it (every count at the game's own maximum) keeps it with the suits in the tables. The
// control: with the maximum raised and the suits not told (what the table alone does), it re-locks.
static void TestImportKeepsTatami() {
  FakeGuest g;
  Setup(g, fx::kFullProfile);
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
  g.bytes[kP + kCostumeCount + kKasumiIndex] = 6;
  CHECK(!TatamiOpen(g.guest, kP));
  g.bytes[kP + kCostumeCount + kKasumiIndex] = 7;
  CHECK(TatamiOpen(g.guest, kP));
  // The rest of the rule is unchanged by the suits.
  g.bytes[kP + kVoiceUnlocked + 0] = 0;
  CHECK(!TatamiOpen(g.guest, kP));
  SetSuitCostumes({});
}

// "Unlock all costumes" is an explicit act and writes the whole maximum, suits included. Taking the import away after it
// must not leave a save the program refuses: the clamp brings the counts to the game's own maximum, and then the
// Extras tab works again.
static void TestUnlockAllThenImportRemoved() {
  FakeGuest g;
  Setup(g, fx::kFreshProfile);
  Import(g);
  CHECK(ApplyUnlock(g.guest, kP, Unlock::kCostumes).costumes > 0);
  CHECK(At(g, kCostumeCount, kKasumiIndex) == 11 && At(g, kCostumeCount, kKokoroIndex) == 13);  // the explicit act
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(kP));
  Removed(g);  // the import folder is gone: the tables are the game's again
  CHECK(!FindProfile(g.guest));  // before the clamp: refused, as the brief's removal case describes
  CHECK(FindLoadedRecord(g.guest) == std::optional<uint32_t>(kP));
  Tick(g, true);
  CHECK(FindProfile(g.guest) == std::optional<uint32_t>(kP));
  CHECK(At(g, kCostumeCount, kKasumiIndex) == 7);   // her own maximum
  CHECK(At(g, kCostumeCount, kKokoroIndex) == 9);   // the box's
  CHECK(IsUnlocked(g.guest, kP, Unlock::kCostumes));
  g.writes = 0;
  Tick(g, true);
  CHECK(g.writes == 0);
}

// The real 100% save wiped: every unlock field equals the real fresh save's; every other byte stays the 100% save's
// (it has Time Attack clears, which the fresh save has not, so "copy the fresh fixture" would fail here).
static void TestWipe() {
  FakeGuest g;
  Setup(g, fx::kFullProfile);
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
  Setup(fresh, fx::kFreshProfile);
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
    Setup(g, fx::kFreshProfile);
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

int main() {
  TestClampCostumeCounts();
  TestFindLoadedRecord();
  TestRaiseCostumes();
  TestWipe();
  TestIsWipedSeesEveryField();
  TestFixturesAreWhatTheResearchSays();
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
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("unlocks: all checks passed\n");
  return 0;
}
