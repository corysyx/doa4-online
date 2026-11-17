// Host test for src/net/np_start_rules.cpp (the netplay starting rules: the three option bytes of the profile record, in
// the hand-off's packed copy of the save and in guest memory, and the match-rules log line). Built and run by
// run_np_start_rules_test.bat; "np_start_rules_host_test.exe -v" prints each group before it runs.
//
// Every record here is SYNTHETIC: a filler that never holds a target byte (0x40 + i % 0x80, so 0x40..0xBF), never bytes
// of a game file. The expected offsets and values are typed from notes/research/sf2_match_options.md (Task 4's table),
// not taken from the module's constants: file offsets 0x797D, 0x797F, 0x7980 (record offsets 0x15, 0x17, 0x18 plus the
// record's place 0x7968 in ups.dat), targets 02, 04, 03. The filler differs between a file offset and its record offset,
// so code that writes record offsets into the file (or file offsets into memory), or a neighbouring byte, is caught by
// the every-other-byte checks.

#include "../../src/net/np_start_rules.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace doa4::np;
using Bytes = std::vector<uint8_t>;

static int g_checks = 0;
static int g_failures = 0;
static bool g_verbose = false;
static const char* g_group = "";
#define CHECK(...)                                                                        \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(__VA_ARGS__)) {                                                                 \
      std::printf("FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, g_group, #__VA_ARGS__);    \
      std::fflush(stdout);                                                                \
      ++g_failures;                                                                       \
    }                                                                                     \
  } while (0)

static void Group(const char* name) {
  g_group = name;
  if (g_verbose) std::printf("group %s\n", name);
}

// Typed from the note, independently of the module.
constexpr char kRecordName[] = "B13EBABEBABEBABE/544307D1/00000001/ups.dat/ups.dat";
constexpr size_t kFileOff[3] = {0x797D, 0x797F, 0x7980};
constexpr size_t kRecOff[3] = {0x15, 0x17, 0x18};
constexpr uint8_t kTarget[3] = {0x02, 0x04, 0x03};
constexpr size_t kShortestEdited = 0x7981;  // the last target byte, 0x7980, is the file's last byte

static Bytes Filler(size_t n, uint8_t salt = 0) {
  Bytes b(n);
  for (size_t i = 0; i < n; ++i) b[i] = uint8_t(0x40 + (i + salt) % 0x80);
  return b;
}

// The record, a thumbnail and a header, each long enough to be edited if the name were not checked.
static std::vector<handoff::File> SaveOf(size_t record_bytes) {
  return {
      {"B13EBABEBABEBABE/544307D1/00000001/ups.dat/__thumbnail.png", Filler(0x8000, 1)},
      {kRecordName, Filler(record_bytes)},
      {"B13EBABEBABEBABE/544307D1/Headers/00000001/ups.dat.header", Filler(0x8000, 2)},
  };
}

static bool IsTargetOffset(size_t i, const size_t (&offs)[3]) {
  for (size_t k = 0; k < 3; ++k)
    if (offs[k] == i) return true;
  return false;
}

static void TestApply() {
  Group("apply_sets_target_bytes");
  {
    std::vector<handoff::File> files = SaveOf(0x14000);
    CHECK(start_rules::ApplyToSaveFiles(&files).empty());
    for (size_t k = 0; k < 3; ++k) CHECK(files[1].data[kFileOff[k]] == kTarget[k]);
  }
  Group("apply_leaves_other_bytes");
  {
    const std::vector<handoff::File> before = SaveOf(0x14000);
    std::vector<handoff::File> files = before;
    CHECK(start_rules::ApplyToSaveFiles(&files).empty());
    CHECK(files.size() == before.size());
    for (size_t f = 0; f < files.size() && f < before.size(); ++f) {
      CHECK(files[f].name == before[f].name);
      CHECK(files[f].data.size() == before[f].data.size());
      if (f != 1) {
        CHECK(files[f].data == before[f].data);
        continue;
      }
      size_t differ = 0, wrong = 0;
      for (size_t i = 0; i < files[f].data.size() && i < before[f].data.size(); ++i) {
        if (files[f].data[i] == before[f].data[i]) continue;
        ++differ;
        if (!IsTargetOffset(i, kFileOff)) ++wrong;
      }
      CHECK(differ == 3);
      CHECK(wrong == 0);
      for (size_t k = 0; k < 3; ++k) CHECK(files[f].data[kRecOff[k]] == before[f].data[kRecOff[k]]);  // not the record offsets
    }
  }
  Group("apply_is_idempotent");
  {
    std::vector<handoff::File> once = SaveOf(0x14000), twice = SaveOf(0x14000);
    CHECK(start_rules::ApplyToSaveFiles(&once).empty());
    CHECK(start_rules::ApplyToSaveFiles(&twice).empty());
    CHECK(start_rules::ApplyToSaveFiles(&twice).empty());
    CHECK(once.size() == twice.size());
    for (size_t f = 0; f < once.size() && f < twice.size(); ++f) CHECK(once[f].name == twice[f].name && once[f].data == twice[f].data);
  }
  Group("apply_empty_profile_is_noop");
  {
    std::vector<handoff::File> files;
    CHECK(start_rules::ApplyToSaveFiles(&files).empty());
    CHECK(files.empty());
    // a save without the record (only other files): nothing to edit, nothing changed
    std::vector<handoff::File> others = SaveOf(0x14000);
    others.erase(others.begin() + 1);
    const std::vector<handoff::File> before = others;
    CHECK(start_rules::ApplyToSaveFiles(&others).empty());
    CHECK(others.size() == before.size());
    for (size_t f = 0; f < others.size() && f < before.size(); ++f) CHECK(others[f].data == before[f].data);
  }
  Group("apply_short_record_refused");
  {
    // the boundary: the shortest record that is edited, and one byte shorter
    std::vector<handoff::File> edge = SaveOf(kShortestEdited);
    CHECK(start_rules::ApplyToSaveFiles(&edge).empty());
    for (size_t k = 0; k < 3; ++k) CHECK(edge[1].data[kFileOff[k]] == kTarget[k]);
    const std::vector<handoff::File> before = SaveOf(kShortestEdited - 1);
    std::vector<handoff::File> files = before;
    const std::string err = start_rules::ApplyToSaveFiles(&files);
    CHECK(!err.empty());
    CHECK(err.find("ups.dat") != std::string::npos);
    CHECK(files.size() == before.size());
    for (size_t f = 0; f < files.size() && f < before.size(); ++f) CHECK(files[f].data == before[f].data);
    // far too short (an empty file)
    std::vector<handoff::File> empty = SaveOf(0);
    CHECK(!start_rules::ApplyToSaveFiles(&empty).empty());
    CHECK(empty[1].data.empty());
  }
}

static std::vector<handoff::File> HeldSave(size_t record_bytes) {
  std::vector<handoff::File> files = SaveOf(record_bytes);
  for (size_t k = 0; k < 3; ++k) files[1].data[kFileOff[k]] = kTarget[k];  // set by the test, not by ApplyToSaveFiles
  return files;
}

static void TestRulesHeld() {
  Group("rules_held");
  CHECK(start_rules::RulesHeld(HeldSave(0x14000)));
  CHECK(start_rules::RulesHeld(HeldSave(kShortestEdited)));
  CHECK(!start_rules::RulesHeld(SaveOf(0x14000)));  // the filler holds none of them
  for (size_t k = 0; k < 3; ++k) {                   // any single one of the three not at its target
    std::vector<handoff::File> files = HeldSave(0x14000);
    files[1].data[kFileOff[k]] = uint8_t(kTarget[k] + 1);
    CHECK(!start_rules::RulesHeld(files));
    files[1].data[kFileOff[k]] = uint8_t(kTarget[k] - 1);
    CHECK(!start_rules::RulesHeld(files));
  }
  {  // one byte too short (the first two bytes at their targets, the third is past the end)
    std::vector<handoff::File> files = SaveOf(kShortestEdited - 1);
    files[1].data[kFileOff[0]] = kTarget[0];
    files[1].data[kFileOff[1]] = kTarget[1];
    CHECK(!start_rules::RulesHeld(files));
  }
  CHECK(!start_rules::RulesHeld({}));  // no save at all
  {  // no record file; another name that holds the bytes does not count
    std::vector<handoff::File> files = HeldSave(0x14000);
    files[1].name = "B13EBABEBABEBABE/544307D1/00000002/ups.dat/ups.dat";
    CHECK(!start_rules::RulesHeld(files));
    files[1].name = "B13EBABEBABEBABE/544307D1/00000001/ups.dat/ups.dat.bak";
    CHECK(!start_rules::RulesHeld(files));
    files[1].name = "00000001/ups.dat/ups.dat";
    CHECK(!start_rules::RulesHeld(files));
  }
  {  // ApplyToSaveFiles makes a long-enough save hold them
    std::vector<handoff::File> files = SaveOf(0x14000);
    CHECK(start_rules::ApplyToSaveFiles(&files).empty());
    CHECK(start_rules::RulesHeld(files));
  }
}

static void TestMemoryWrite() {
  Group("memory_write");
  {
    const Bytes before = Filler(0x800);  // a record buffer in memory, P at index 0
    Bytes rec = before;
    CHECK(start_rules::WriteToRecord(rec));
    size_t differ = 0, wrong = 0;
    for (size_t i = 0; i < rec.size(); ++i) {
      if (rec[i] == before[i]) continue;
      ++differ;
      if (!IsTargetOffset(i, kRecOff)) ++wrong;
    }
    CHECK(differ == 3);
    CHECK(wrong == 0);
    for (size_t k = 0; k < 3; ++k) CHECK(rec[kRecOff[k]] == kTarget[k]);
  }
  {  // the span the hook passes: exactly up to the last option byte
    Bytes rec = Filler(0x19);
    CHECK(start_rules::WriteToRecord(rec));
    for (size_t k = 0; k < 3; ++k) CHECK(rec[kRecOff[k]] == kTarget[k]);
    const Bytes before = Filler(0x18);
    Bytes shorter = before;
    CHECK(!start_rules::WriteToRecord(shorter));
    CHECK(shorter == before);
  }
  CHECK(start_rules::kRecordMemoryBytes == 0x19);
}

static void TestGuestAddresses() {
  Group("guest_addresses");  // from the note's section 3
  CHECK(start_rules::kProfilePointer == 0x83035CC8u);
  CHECK(start_rules::kTagFlag == 0x8303683Au);
  CHECK(start_rules::kLifeValue == 0x8303691Cu);
  CHECK(start_rules::kRoundsToWin == 0x82FCC8E3u);
  CHECK(start_rules::kTimeSeconds == 0x83033A7Au);
}

static void TestLine() {
  Group("line_text");
  CHECK(start_rules::MatchRulesLine(false, 300, 3, 60) == "match rules: mode single life 300 rounds 3 time 60");
  CHECK(start_rules::MatchRulesLine(true, 300, 2, 99) == "match rules: mode tag life 300 rounds 2 time 99");
  CHECK(start_rules::MatchRulesLine(false, 240, 2, 40) == "match rules: mode single life 240 rounds 2 time 40");
  // decimal, the full range of each field (a u8 printed as a character would fail here)
  CHECK(start_rules::MatchRulesLine(true, 65535, 0, 255) == "match rules: mode tag life 65535 rounds 0 time 255");
}

static void TestReplayKey() {
  Group("replay_key");  // fix round 1: the online meta carries the armed state, a replay arms from it
  CHECK(start_rules::MetaLine(true) == "start_rules=armed\n");
  CHECK(start_rules::MetaLine(false) == "start_rules=held\n");
  CHECK(start_rules::ArmedFromMeta("armed"));
  CHECK(!start_rules::ArmedFromMeta("held"));
  // a recording without the key (np_core MetaValue gives "(none)") is NOT armed: every older recording
  CHECK(!start_rules::ArmedFromMeta("(none)"));
  CHECK(!start_rules::ArmedFromMeta(""));
  CHECK(!start_rules::ArmedFromMeta("Armed"));
  CHECK(!start_rules::ArmedFromMeta("armed "));
  CHECK(!start_rules::ArmedFromMeta("armedx"));
  // the round trip: the value after "start_rules=" in the line, without its newline
  CHECK(start_rules::ArmedFromMeta(start_rules::MetaLine(true).substr(12, 5)));
  CHECK(std::string(start_rules::kMetaKey) == "start_rules");
}

static void TestMayWrite() {
  Group("may_write");  // live or replay, and armed; never a record run, never unarmed
  CHECK(start_rules::MayWrite(true, false, true));
  CHECK(start_rules::MayWrite(false, true, true));
  CHECK(!start_rules::MayWrite(true, false, false));
  CHECK(!start_rules::MayWrite(false, true, false));
  CHECK(!start_rules::MayWrite(false, false, true));  // a harness record run (Active, neither Online nor Replaying)
  CHECK(!start_rules::MayWrite(false, false, false));
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "-v") == 0) g_verbose = true;
  TestApply();
  TestRulesHeld();
  TestMemoryWrite();
  TestGuestAddresses();
  TestLine();
  TestReplayKey();
  TestMayWrite();
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
