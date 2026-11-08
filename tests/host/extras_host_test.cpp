// Host test for src/doa4_extras.cpp (with doa4_unlocks.cpp and
// doa4_backup.cpp). No game byte is in this file: the game's tables and a new save's profile are synthetic (as in
// unlocks_host_test.cpp); with DOA4_TEST_IMAGE set (doa4_test_image.h) the whole set runs again on the image's three
// ranges. Built and run by run_extras_test.bat.

#include "../../src/doa4_extras.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

namespace fs = std::filesystem;
using namespace doa4::unlocks;

constexpr uint32_t kP = kRecordBase + kRecordToProfile;  // slot 0
constexpr uint32_t kFixtureStart = 0x3FC0;                // the profile range the unlock fields live in
constexpr uint32_t kProfileBytes = 0x8C;

// The three image tables the unlock code reads (as in unlocks_host_test.cpp).
struct Tables {
  const char* name;
  std::array<uint8_t, 23> index_to_char;  // profile index -> charId
  std::array<uint8_t, 25> max;            // maximum costume count by charId
  std::array<uint8_t, 18> roster;         // the Story roster's charIds
};

// Synthetic: index -> charId is (6 i + 14) mod 25; the maxima run 2..12; the roster holds the charIds of the 17 profile
// indices that have Story mode (all but 3, 6, 8, 12, 21 and 22) and the first one again.
static Tables SyntheticTables() {
  Tables t{"synthetic", {}, {}, {}};
  for (int i = 0; i < 23; ++i) t.index_to_char[size_t(i)] = uint8_t((6 * i + 14) % 25);
  for (int c = 0; c < 25; ++c) t.max[size_t(c)] = uint8_t(2 + (4 * c + 1) % 11);
  size_t k = 0;
  for (int i = 22; i >= 0; --i) {
    if (i == 3 || i == 6 || i == 8 || i == 12 || i == 21 || i == 22) continue;
    t.roster[k++] = t.index_to_char[size_t(i)];
  }
  t.roster[k++] = t.roster[0];
  return t;
}

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

static bool InUnlockField(uint32_t offset) {
  return offset == kFirstStoryClear || (offset >= kCostumeCount && offset < kCostumeCount + kCharacterCount) ||
         (offset >= kCharacterUnlocked && offset < kCharacterUnlocked + kCharacterCount) ||
         (offset >= kStoryClears && offset < kStoryClears + kCharacterCount) ||
         (offset >= kStoryClearedNormal && offset < kStoryClearedNormal + kCharacterCount) ||
         (offset >= kVoiceUnlocked && offset < kVoiceUnlocked + kCharacterCount);
}

// A new save's unlock fields (costume counts 2, the 7 hidden characters 3, 6, 7, 8, 12, 21, 22 locked, no voices, no
// Story clears); every other byte of the range is uint8_t(0xA5 ^ offset), so a write outside the fields shows.
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

static Tables g_t;

// A loaded fresh profile in a map, as in unlocks_host_test.cpp.
struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  int writes = 0;
  doa4::GuestBytes guest{[this](uint32_t a) { return bytes.count(a) ? bytes[a] : uint8_t(0); },
                         [this](uint32_t a, uint8_t v) {
                           bytes[a] = v;
                           ++writes;
                         }};
  FakeGuest() {
    for (size_t i = 0; i < g_t.index_to_char.size(); ++i) bytes[kProfileIndexToChar + uint32_t(i)] = g_t.index_to_char[i];
    for (size_t i = 0; i < g_t.max.size(); ++i) bytes[kMaxCostumes + uint32_t(i)] = g_t.max[i];
    for (size_t k = 0; k < g_t.roster.size(); ++k) bytes[kStoryRoster + kStoryRosterStride * uint32_t(k)] = g_t.roster[k];
    const std::vector<uint8_t> fresh = FreshProfile();
    for (size_t i = 0; i < fresh.size(); ++i) bytes[kP + kFixtureStart + uint32_t(i)] = fresh[i];
  }
  // Every byte of the profile range that is not an unlock field is still the fresh profile's filler: nothing wrote
  // outside the fields (a write there, even of the same unlock value, would change 0xA5 ^ offset).
  bool FillerIntact() const {
    for (uint32_t o = kFixtureStart; o < kFixtureStart + kProfileBytes; ++o) {
      if (InUnlockField(o)) continue;
      const auto it = bytes.find(kP + o);
      if (it == bytes.end() || it->second != uint8_t(0xA5 ^ o)) return false;
    }
    return true;
  }
};

static void WriteFile(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

static void TestSaveDir() {
  CHECK(doa4::SaveDir("C:/u", 0xB13EBABEBABEBABEull) ==
        fs::path("C:/u") / "B13EBABEBABEBABE" / "544307D1" / "00000001" / "ups.dat");
  CHECK(doa4::SaveDir("C:/u", 0x00000000000000ABull) ==
        fs::path("C:/u") / "00000000000000AB" / "544307D1" / "00000001" / "ups.dat");
  CHECK(doa4::SaveDir("C:/u", 0).empty());
  CHECK(doa4::SaveDir("", 1).empty());
}

static void TestBackupStamp() {
  const std::string stamp = doa4::BackupStamp();  // YYYY-MM-DD-HHMMSS
  CHECK(stamp.size() == 17);
  CHECK(stamp[4] == '-' && stamp[7] == '-' && stamp[10] == '-');
}

static void TestDescribe() {
  CHECK(doa4::DescribeUnlock({}) == "Nothing to unlock: it was all unlocked already.");
  CHECK(doa4::DescribeUnlock({6, 0, 0, 1}) == "Unlocked 6 characters and 1 stage.");
  CHECK(doa4::DescribeUnlock({0, 0, 23, 0}) == "Unlocked 23 system voices.");
  CHECK(doa4::DescribeUnlock({6, 61, 23, 3}) ==
        "Unlocked 6 characters, 61 costumes, 23 system voices and 3 stages.");
  CHECK(doa4::DescribeUnlock({1, 1, 1, 0}) == "Unlocked 1 character, 1 costume and 1 system voice.");
}

// ups.dat as the game writes it: the profile record from guest memory,
// padded to the save file size.
static std::string SaveFileFrom(const FakeGuest& g) {
  std::string file(doa4::kSaveFileSize, '\0');
  const uint32_t record = kP - kRecordToProfile;
  for (uint32_t i = 0; i < kRecordStride; ++i) file[i] = char(g.guest.Read(record + i));
  return file;
}

// Moves ups.dat's modified time on, as a save would.
static void Touch(const fs::path& file) {
  fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(2));
}

static void TestUnlockWithBackup() {
  const fs::path root = fs::temp_directory_path() / "doa4_extras_test_data";
  fs::remove_all(root);
  const fs::path save = root / "00000001" / "ups.dat";
  const fs::path backups = root / "doa4_save_backups";
  fs::create_directories(save);
  FakeGuest g;
  const std::string fresh_file = SaveFileFrom(g);
  WriteFile(save / "ups.dat", fresh_file);

  // Backed up, then unlocked; waiting for the game's save.
  doa4::ExtrasState state;
  std::string error;
  CHECK(doa4::UnlockWithBackup(g.guest, kP, Unlock::kCharacters, save, backups, "s1", &state,
                               &error));
  CHECK(error.empty());
  CHECK(state.backup == backups / "ups.dat.backup-s1");
  CHECK(fs::exists(state.backup / "ups.dat"));
  CHECK(state.last_change == "Unlocked 6 characters and 1 stage.");
  CHECK(state.waiting_for_save && !state.saved);
  CHECK(g.writes == 6);
  CHECK(g.FillerIntact());

  // Not saved while ups.dat is unchanged.
  doa4::PollSaved(g.guest, save, &state);
  CHECK(state.waiting_for_save && !state.saved);
  // A newer ups.dat that does not hold the unlock (a save that copied the
  // record before the click, or a reload from disk) is not "saved".
  WriteFile(save / "ups.dat", fresh_file);
  Touch(save / "ups.dat");
  doa4::PollSaved(g.guest, save, &state);
  CHECK(state.waiting_for_save && !state.saved);
  // A file caught mid-write (short) is not "saved" either.
  WriteFile(save / "ups.dat", SaveFileFrom(g).substr(0, 1000));
  doa4::PollSaved(g.guest, save, &state);
  CHECK(state.waiting_for_save && !state.saved);
  // The game's save with the unlock in it.
  WriteFile(save / "ups.dat", SaveFileFrom(g));
  Touch(save / "ups.dat");
  doa4::PollSaved(g.guest, save, &state);
  CHECK(!state.waiting_for_save && state.saved);

  // A click that changes nothing leaves "saved" alone.
  CHECK(doa4::UnlockWithBackup(g.guest, kP, Unlock::kCharacters, save, backups, "s2", &state,
                               &error));
  CHECK(state.last_change == "Nothing to unlock: it was all unlocked already.");
  CHECK(state.saved && !state.waiting_for_save);
  CHECK(state.backup == backups / "ups.dat.backup-s1");  // still the first

  // Two clicks before one save: "saved" needs both in the file.
  FakeGuest g2;
  const fs::path save2 = root / "00000002" / "ups.dat";
  fs::create_directories(save2);
  WriteFile(save2 / "ups.dat", SaveFileFrom(g2));
  doa4::ExtrasState state2;
  CHECK(doa4::UnlockWithBackup(g2.guest, kP, Unlock::kCharacters, save2, root / "b2", "a",
                               &state2, &error));
  CHECK(doa4::UnlockWithBackup(g2.guest, kP, Unlock::kVoices, save2, root / "b2", "b", &state2,
                               &error));
  // A file with the second click's unlock but not the first's.
  FakeGuest voices_only;
  ApplyUnlock(voices_only.guest, kP, Unlock::kVoices);
  WriteFile(save2 / "ups.dat", SaveFileFrom(voices_only));
  Touch(save2 / "ups.dat");
  doa4::PollSaved(g2.guest, save2, &state2);
  CHECK(state2.waiting_for_save && !state2.saved);
  WriteFile(save2 / "ups.dat", SaveFileFrom(g2));
  Touch(save2 / "ups.dat");
  doa4::PollSaved(g2.guest, save2, &state2);
  CHECK(state2.saved);

  // Backup impossible (its name is taken by a file): nothing written.
  const fs::path save3 = root / "00000003" / "ups.dat";
  fs::create_directories(save3);
  WriteFile(save3 / "ups.dat", fresh_file);
  fs::create_directories(root / "b3");
  WriteFile(root / "b3" / "ups.dat.backup-s3", "a file");
  FakeGuest g3;
  doa4::ExtrasState state3;
  CHECK(!doa4::UnlockWithBackup(g3.guest, kP, Unlock::kEverything, save3, root / "b3", "s3",
                                &state3, &error));
  CHECK(error.rfind("Nothing unlocked: ", 0) == 0);
  CHECK(g3.writes == 0);
  CHECK(state3.last_change.empty() && !state3.waiting_for_save);

  // Unknown save folder: nothing written.
  FakeGuest g4;
  doa4::ExtrasState state4;
  error.clear();
  CHECK(!doa4::UnlockWithBackup(g4.guest, kP, Unlock::kVoices, fs::path(), root / "b4", "s4",
                                &state4, &error));
  CHECK(!error.empty());
  CHECK(g4.writes == 0);

  // No save yet: unlocked without a backup, with a note; saved once a
  // ups.dat holding the unlock appears.
  const fs::path save5 = root / "00000005" / "ups.dat";
  FakeGuest g5;
  doa4::ExtrasState state5;
  CHECK(doa4::UnlockWithBackup(g5.guest, kP, Unlock::kVoices, save5, root / "b5", "s5", &state5,
                               &error));
  CHECK(state5.backup.empty() && !state5.backup_note.empty());
  CHECK(g5.writes == 23);
  CHECK(g5.FillerIntact());
  doa4::PollSaved(g5.guest, save5, &state5);
  CHECK(state5.waiting_for_save);
  fs::create_directories(save5);
  WriteFile(save5 / "ups.dat", SaveFileFrom(g5));
  doa4::PollSaved(g5.guest, save5, &state5);
  CHECK(state5.saved);

  fs::remove_all(root);
}

// Restarting before the game has saved loses the unlocks: the menu warns.
static void TestUnsavedWarning() {
  doa4::ExtrasState state;
  CHECK(doa4::UnsavedUnlocksWarning(state) == nullptr);
  state.waiting_for_save = true;
  const char* warning = doa4::UnsavedUnlocksWarning(state);
  CHECK(warning != nullptr && std::string(warning).find("not saved") != std::string::npos);
  state.waiting_for_save = false;
  state.saved = true;
  CHECK(doa4::UnsavedUnlocksWarning(state) == nullptr);
}

static void TestWipeWithBackup() {
  const fs::path root = fs::temp_directory_path() / "doa4_extras_wipe_test_data";
  fs::remove_all(root);
  const fs::path save = root / "00000001" / "ups.dat";
  const fs::path backups = root / "doa4_save_backups";
  fs::create_directories(save);
  FakeGuest g;
  std::string error;
  doa4::ExtrasState state;

  // Unlocked and saved: the state a wipe starts from.
  WriteFile(save / "ups.dat", SaveFileFrom(g));
  CHECK(doa4::UnlockWithBackup(g.guest, kP, Unlock::kEverything, save, backups, "u1", &state, &error));
  CHECK(g.FillerIntact());  // Unlock everything wrote the unlock fields and nothing else
  WriteFile(save / "ups.dat", SaveFileFrom(g));
  const std::string unlocked_file = SaveFileFrom(g);
  Touch(save / "ups.dat");
  doa4::PollSaved(g.guest, save, &state);
  CHECK(state.saved);

  // The copy holds the save from just before the wipe, in the game's memory the wipe has happened, waiting for a save.
  CHECK(doa4::WipeWithBackup(g.guest, kP, save, backups, "w1", &state, &error));
  CHECK(error.empty());
  CHECK(state.wipe_backup == backups / "ups.dat.wipe-w1");
  {
    std::ifstream in(state.wipe_backup / "ups.dat", std::ios::binary);
    const std::string copy((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(copy == unlocked_file);
  }
  CHECK(IsWiped(g.guest, kP));
  CHECK(g.FillerIntact());  // the wipe wrote the unlock fields and nothing else
  CHECK(state.last_change == "Wiped: characters, costumes, voices, stages and Story clears are back to a new save's.");
  CHECK(state.waiting_for_save && !state.saved && state.pending_wipe);
  CHECK(state.pending_unlocks == 0);
  const char* warning = doa4::UnsavedUnlocksWarning(state);
  CHECK(warning != nullptr && std::string(warning).find("wipe") != std::string::npos);

  // A newer file that still holds the unlocks (a save from before the wipe) is not "saved"; the wiped one is.
  WriteFile(save / "ups.dat", unlocked_file);
  Touch(save / "ups.dat");
  doa4::PollSaved(g.guest, save, &state);
  CHECK(state.waiting_for_save && !state.saved);
  WriteFile(save / "ups.dat", SaveFileFrom(g));
  Touch(save / "ups.dat");
  doa4::PollSaved(g.guest, save, &state);
  CHECK(!state.waiting_for_save && state.saved && !state.pending_wipe);

  // Already wiped: no copy, nothing written, "saved" left alone.
  g.writes = 0;
  CHECK(doa4::WipeWithBackup(g.guest, kP, save, backups, "w2", &state, &error));
  CHECK(!fs::exists(backups / "ups.dat.wipe-w2"));
  CHECK(g.writes == 0);
  CHECK(state.saved && !state.waiting_for_save);
  CHECK(state.last_change == "Nothing to wipe: the unlocks are already a new save's.");

  // An unlock after an unsaved wipe replaces it: "saved" then looks for the unlock (and the other way round).
  CHECK(doa4::UnlockWithBackup(g.guest, kP, Unlock::kVoices, save, backups, "u2", &state, &error));
  CHECK(doa4::WipeWithBackup(g.guest, kP, save, backups, "w3", &state, &error));
  CHECK(state.pending_wipe && state.pending_unlocks == 0);
  CHECK(doa4::UnlockWithBackup(g.guest, kP, Unlock::kVoices, save, backups, "u3", &state, &error));
  CHECK(!state.pending_wipe && state.pending_unlocks != 0);
  CHECK(doa4::WipeWithBackup(g.guest, kP, save, backups, "w3b", &state, &error));

  // A costume box turned on after an unsaved wipe: the save can no longer become "wiped", so nothing waits for that.
  doa4::CostumeBoxOn(&state);
  CHECK(!state.pending_wipe && !state.waiting_for_save);
  CHECK(doa4::UnsavedUnlocksWarning(state) == nullptr);
  // Unsaved unlocks stay awaited: the box says nothing about them.
  CHECK(doa4::UnlockWithBackup(g.guest, kP, Unlock::kCharacters, save, backups, "u4", &state, &error));
  CHECK(state.waiting_for_save);
  doa4::CostumeBoxOn(&state);
  CHECK(state.waiting_for_save && state.pending_unlocks != 0);

  // No copy possible (its name is taken): nothing written, the state unchanged.
  FakeGuest g2;
  ApplyUnlock(g2.guest, kP, Unlock::kEverything);
  const fs::path save2 = root / "00000002" / "ups.dat";
  fs::create_directories(save2);
  WriteFile(save2 / "ups.dat", SaveFileFrom(g2));
  fs::create_directories(root / "b2" / "ups.dat.wipe-w4");
  g2.writes = 0;
  doa4::ExtrasState state2;
  error.clear();
  CHECK(!doa4::WipeWithBackup(g2.guest, kP, save2, root / "b2", "w4", &state2, &error));
  CHECK(error.rfind("Nothing wiped: ", 0) == 0);
  CHECK(g2.writes == 0);
  CHECK(state2.last_change.empty() && !state2.waiting_for_save);

  // Unknown save folder: nothing written.
  error.clear();
  CHECK(!doa4::WipeWithBackup(g2.guest, kP, fs::path(), root / "b3", "w5", &state2, &error));
  CHECK(error.rfind("Nothing wiped: ", 0) == 0);
  CHECK(g2.writes == 0);

  // No save yet: wiped without a copy, with a note.
  const fs::path save3 = root / "00000003" / "ups.dat";
  doa4::ExtrasState state3;
  CHECK(doa4::WipeWithBackup(g2.guest, kP, save3, root / "b4", "w6", &state3, &error));
  CHECK(state3.wipe_backup.empty() && !state3.backup_note.empty());
  CHECK(IsWiped(g2.guest, kP));
  fs::remove_all(root);
}

// A costume box writes to the save (the character's count), so it takes the same first backup as an unlock.
static void TestBackupBeforeCostumeBox() {
  const fs::path root = fs::temp_directory_path() / "doa4_extras_box_test_data";
  fs::remove_all(root);
  const fs::path save = root / "00000001" / "ups.dat";
  const fs::path backups = root / "doa4_save_backups";
  fs::create_directories(save);
  FakeGuest g;
  const std::string file = SaveFileFrom(g);
  WriteFile(save / "ups.dat", file);
  doa4::ExtrasState state;
  std::string error;
  CHECK(doa4::BackupBeforeCostumeBox(save, backups, "b1", &state, &error));
  CHECK(error.empty());
  CHECK(state.backup == backups / "ups.dat.backup-b1");
  CHECK(fs::exists(state.backup / "ups.dat"));
  // The first backup is kept.
  CHECK(doa4::BackupBeforeCostumeBox(save, backups, "b2", &state, &error));
  CHECK(state.backup == backups / "ups.dat.backup-b1");
  // No backup possible: refused, with the reason.
  WriteFile(save / "ups.dat", "short");
  doa4::ExtrasState state2;
  error.clear();
  CHECK(!doa4::BackupBeforeCostumeBox(save, root / "b2", "b3", &state2, &error));
  CHECK(error.rfind("Costume not added: ", 0) == 0);
  error.clear();
  CHECK(!doa4::BackupBeforeCostumeBox(fs::path(), root / "b2", "b4", &state2, &error));
  CHECK(error.rfind("Costume not added: ", 0) == 0);
  // No save yet: nothing to lose, with a note.
  doa4::ExtrasState state3;
  error.clear();
  CHECK(doa4::BackupBeforeCostumeBox(root / "none" / "ups.dat", root / "b3", "b5", &state3, &error));
  CHECK(state3.backup.empty() && !state3.backup_note.empty());
  fs::remove_all(root);
}

// Every test, on the tables in g_t.
static void RunAll() {
  try {
    TestBackupBeforeCostumeBox();
    TestWipeWithBackup();
  } catch (const std::exception& e) {
    std::printf("FAIL [%s] TestWipeWithBackup threw: %s\n", g_tables_name, e.what());
    ++g_failures;
  }
  TestSaveDir();
  TestBackupStamp();
  TestDescribe();
  try {
    TestUnlockWithBackup();
    TestUnsavedWarning();
  } catch (const std::exception& e) {
    std::printf("FAIL [%s] TestUnlockWithBackup threw: %s\n", g_tables_name, e.what());
    ++g_failures;
  }
}

int main() {
  g_t = SyntheticTables();
  RunAll();
  std::string why;
  if (const std::optional<Tables> image = ImageTables(&why)) {
    g_t = *image;
    g_tables_name = "image";
    RunAll();
  } else {
    doa4_test::PrintSkipped("extras: the checks on the image's tables", why);
  }
  std::printf("extras: %d checks run\n", g_checks);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("extras: all checks passed\n");
  return 0;
}
