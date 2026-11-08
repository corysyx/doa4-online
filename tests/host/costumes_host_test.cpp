// Host test for src/doa4_costumes.cpp on the image's real costume tables, read from your own image (DOA4_TEST_IMAGE,
// doa4_test_image.h; no game byte is in this repository). Without the image the checks that need the tables print
// SKIPPED and the rest run: the guard and model-set checks and the image reader's own checks. Built and run by
// run_costumes_test.bat.

#include "../../src/doa4_costumes.h"
#include "../../src/doa4_suits_picks.h"
#include "../../src/doa4_tweaks.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#ifdef _WIN32
#include <process.h>
#endif
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "doa4_test_image.h"

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::costumes;
using doa4::suits::kNoPart;
using doa4::suits::Suit;

// The image's three costume ranges and its maximum-costume table, loaded once in main; the tests that use FakeGuest run
// only when they are there.
static const doa4_test::CostumeImage* g_image = nullptr;
static std::vector<uint8_t> g_max_costumes;

// Guest memory from the image's ranges; unset bytes read 0. Records every written address.
struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  std::set<uint32_t> written;
  uint32_t next_block = 0x40000000;
  int allocs = 0;
  int fail_alloc_at = -1;  // the allocation (0-based) that returns 0
  doa4::GuestBytes guest{[this](uint32_t a) { return Get(a); },
                         [this](uint32_t a, uint8_t v) {
                           bytes[a] = v;
                           written.insert(a);
                         }};
  AllocFn alloc = [this](uint32_t size) -> uint32_t {
    if (allocs++ == fail_alloc_at) return 0;
    const uint32_t block = next_block;
    next_block += (size + 31) / 32 * 32;
    return block;
  };
  FakeGuest() {
    Load(doa4_test::kCostumeTablesRange.address, g_image->tables);
    Load(doa4_test::kCostumeRecordsRange.address, g_image->records);
    Load(doa4_test::kCostumeFilesRange.address, g_image->files);
  }
  void Load(uint32_t start, const std::vector<uint8_t>& data) {
    for (size_t i = 0; i < data.size(); ++i) bytes[start + uint32_t(i)] = data[i];
  }
  uint8_t Get(uint32_t a) const {
    auto it = bytes.find(a);
    return it == bytes.end() ? 0 : it->second;
  }
  uint32_t Be32(uint32_t a) const { return guest.ReadBe32(a); }
  uint16_t Be16(uint32_t a) const { return uint16_t(Get(a) << 8 | Get(a + 1)); }
};

// The game's lookup sub_82526B78 as read from its code (.19.cpp:39404-39472): signed arithmetic;
// the variant is raised to at least 0, then lowered to var[costume] - 1 (which is -1 when that
// byte is 0); index = sum of var[0..costume) + variant; model 0 when the index is outside
// [0, len - 1]. A costume at or past max over zero padding therefore gives list[len - 1].
static uint32_t GameLookup(const FakeGuest& g, uint32_t c, uint32_t costume, uint32_t variant) {
  if (c > 24) return 0;
  const uint32_t var = g.Be32(kVarTable + 4 * c), list = g.Be32(kListTable + 4 * c);
  const int last = int(g.Get(var + costume)) - 1;
  int v = int(variant);
  if (v < 0) v = 0;
  if (last < v) v = last;
  int sum = 0;
  for (uint32_t k = 0; k < costume; ++k) sum += g.Get(var + k);
  const int index = sum + v;
  if (index < 0 || index > int(g.Get(kLenTable + c)) - 1) return 0;
  return g.Get(list + uint32_t(index));
}

static Suit MakeSuit(uint8_t c, int own, uint16_t tpr, uint16_t cat, uint16_t ptpr, uint16_t pcat,
                     const char* design, uint64_t size = 0x400000) {
  Suit s;
  s.character = c;
  s.own_model = own;
  s.body_tpr = tpr;
  s.body_cat = cat;
  s.part_tpr = ptpr;
  s.part_cat = pcat;
  s.design = design;
  s.tpr = {std::string(design) + ".tpr", size, std::string(40, 'a')};
  s.cat = {std::string(design) + ".cat", 0x800, std::string(40, 'b')};
  return s;
}

static void TestFixturesAgree() {
  FakeGuest g;
  bool same = true;
  CHECK(g_max_costumes.size() == kCharCount);
  for (uint32_t c = 0; c < kCharCount && c < g_max_costumes.size(); ++c) same = same && g.Get(kMaxTable + c) == g_max_costumes[c];
  CHECK(same);  // the tables range and the maximum-costume range come from the same image
}

static void TestRecordCounts() {
  FakeGuest g;
  CHECK(RecordCount(g.guest, 13) == 6);   // Ayane: round 1d copied her 6 records
  CHECK(RecordCount(g.guest, 15) == 10);  // Lisa: round 1d copied her 10 records
  CHECK(RecordCount(g.guest, 5) == 31);   // Kasumi: 0x82C5E160 .. 0x82C5E258
  CHECK(RecordCount(g.guest, 24) == 0);   // the last block: no next block, unknown
  CHECK(RecordCount(g.guest, 25) == 0);
  const GuardState s = ReadGuardState(g.guest);
  CHECK(s.records[13] == 6 && s.records[5] == 31 && s.records[24] == 0);
}

static void TestFileLocation() {
  FakeGuest g;
  auto loc = FileLocation(g.guest, 269);
  CHECK(loc && std::string(loc->archive) == "chrone.afs" && loc->entry == 65);
  loc = FileLocation(g.guest, 349);
  CHECK(loc && std::string(loc->archive) == "chrtwo.afs" && loc->entry == 72);
  loc = FileLocation(g.guest, 481);
  CHECK(loc && std::string(loc->archive) == "chrthree.afs" && loc->entry == 76);
  CHECK(!FileLocation(g.guest, 23));    // mot.afs (archive 7)
  CHECK(!FileLocation(g.guest, 1640));  // past the table
}

static void ExpectCheck(const Suit& s, const std::string& want) {
  FakeGuest g;
  const std::string why = CheckSuit(g.guest, s);
  const bool ok = want.empty() ? why.empty() : why.find(want) != std::string::npos;
  if (!ok) std::printf("  %s: want '%s', got '%s'\n", s.design.c_str(), want.c_str(), why.c_str());
  CHECK(ok);
}

static void TestCheckSuit() {
  ExpectCheck(MakeSuit(5, 7, 269, 270, 509, 510, "ka7"), "");
  ExpectCheck(MakeSuit(13, -1, 275, 276, 543, 544, "ay"), "");
  ExpectCheck(MakeSuit(0, -1, 211, 212, kNoPart, kNoPart, "ti"), "");
  ExpectCheck(MakeSuit(10, 2, 321, 322, 569, 570, "kk2"), "");  // Kokoro m2: part already set
  ExpectCheck(MakeSuit(10, 2, 321, 322, 545, 546, "kk2b"), "already has head part 569/570");
  ExpectCheck(MakeSuit(5, 7, 271, 272, 509, 510, "kaw"), "holds files 269/270");
  ExpectCheck(MakeSuit(5, 40, 269, 270, 509, 510, "ka40"), "does not exist (she has 31)");
  ExpectCheck(MakeSuit(5, 7, 269, 270, kNoPart, kNoPart, "kanp"), "a head part is needed");
  ExpectCheck(MakeSuit(0, -1, 211, 212, 509, 510, "tip"), "she has no head part");
  ExpectCheck(MakeSuit(5, 7, 269, 270, 509, 510, "kabig", 0xA00001), "over her budget");
  ExpectCheck(MakeSuit(22, -1, 211, 212, kNoPart, kNoPart, "c22"), "no costume list");
  ExpectCheck(MakeSuit(5, -1, 23, 24, 509, 510, "mot"), "not character archive files");
}

static void TestApplyKasumi() {
  FakeGuest g;
  const uint32_t old_records = g.Be32(kModelTable + 8 * 5 + 4);
  const uint32_t old_list = g.Be32(kListTable + 4 * 5);
  std::vector<uint8_t> list_before;
  for (uint32_t i = 0; i < 28; ++i) list_before.push_back(g.Get(old_list + i));
  const std::vector<Suit> suits = {
      MakeSuit(5, 7, 269, 270, 509, 510, "vks_ch_d"), MakeSuit(5, 8, 271, 272, 509, 510, "vks_cs_a"),
      MakeSuit(5, 9, 273, 274, 509, 510, "vks_on_o"), MakeSuit(5, -1, 349, 350, 509, 510, "vks_hn_i")};
  const ApplyResult r = Apply(g.guest, g.alloc, suits);
  CHECK(r.errors.empty());
  CHECK(r.applied.size() == 4);
  const uint32_t records = g.Be32(kModelTable + 8 * 5 + 4);
  CHECK(records != old_records && records >= 0x40000000);  // moved to a heap copy
  bool copied = true;
  for (uint32_t i = 0; i < 8 * 31; ++i) {
    if (i >= 56 && i < 80 && (i % 8) >= 4) continue;  // m7..m9 parts are filled below
    copied = copied && g.Get(records + i) == g.Get(old_records + i);
  }
  CHECK(copied);
  CHECK(g.Be16(records + 8 * 31) == 349 && g.Be16(records + 8 * 31 + 2) == 350);
  CHECK(g.Be16(records + 8 * 31 + 4) == 509 && g.Be16(records + 8 * 31 + 6) == 510);
  for (uint32_t m : {7u, 8u, 9u}) {
    CHECK(g.Be16(records + 8 * m + 4) == 509 && g.Be16(records + 8 * m + 6) == 510);
    CHECK(g.Be16(old_records + 8 * m + 4) == 0xFFFF);  // the image's block is left alone
  }
  CHECK(g.Get(kMaxTable + 5) == 11);
  CHECK(g.Get(kLenTable + 5) == 32);
  const uint32_t list = g.Be32(kListTable + 4 * 5);
  bool list_kept = true;
  for (uint32_t i = 0; i < 28; ++i) list_kept = list_kept && g.Get(list + i) == list_before[i];
  CHECK(list_kept);
  CHECK(g.Get(list + 28) == 7 && g.Get(list + 29) == 8 && g.Get(list + 30) == 9 &&
        g.Get(list + 31) == 31);
  const uint32_t var = g.Be32(kVarTable + 4 * 5);
  CHECK(g.Get(var + 7) == 1 && g.Get(var + 10) == 1);
  CHECK(GameLookup(g, 5, 7, 0) == 7);
  CHECK(GameLookup(g, 5, 10, 0) == 31);
  CHECK(GameLookup(g, 5, 10, 3) == 31);  // the variant clamps to the costume's only one
  bool padded = true;
  for (uint32_t i = 11; i < 256; ++i) padded = padded && g.written.count(var + i) && g.Get(var + i) == 0;
  for (uint32_t i = 32; i < 256; ++i) padded = padded && g.written.count(list + i) && g.Get(list + i) == 0;
  CHECK(padded);  // costume numbers past max read written zeros inside the block (Task 1 limit 3)
  CHECK(GameLookup(g, 5, 200, 0) == g.Get(list + 31));  // past max: the last listed model (31)
  CHECK(GameLookup(g, 5, 200, 0) == 31);
  CHECK(r.applied.size() == 4 && r.applied[3].costume == 10 && r.applied[3].model == 31 &&
        r.applied[3].label == "vks_hn_i c0");
  CHECK(r.guard.records[5] == 32);
  CHECK(g.Be32(kModelTable + 8 * 23 + 4) == old_records);  // charId 23 shares the old block
}

static void TestApplyAyane() {
  FakeGuest g;
  const ApplyResult r = Apply(g.guest, g.alloc, {MakeSuit(13, -1, 275, 276, 543, 544, "vay_cs_b")});
  CHECK(r.errors.empty() && r.applied.size() == 1);
  CHECK(r.applied.size() == 1 && r.applied[0].model == 6 && r.applied[0].costume == 6);
  CHECK(GameLookup(g, 13, 6, 0) == 6);
  const uint32_t records = g.Be32(kModelTable + 8 * 13 + 4);
  CHECK(g.Be16(records + 48) == 275 && g.Be16(records + 52) == 543);
  CHECK(g.Get(kMaxTable + 13) == 7 && g.Get(kLenTable + 13) == 7);
}

static void TestAllOrNothing() {
  FakeGuest g;
  g.fail_alloc_at = 1;  // Kasumi's second block (list) fails
  const std::vector<Suit> suits = {MakeSuit(5, -1, 349, 350, 509, 510, "vks_hn_i"),
                                   MakeSuit(13, -1, 275, 276, 543, 544, "vay_cs_b")};
  const ApplyResult r = Apply(g.guest, g.alloc, suits);
  CHECK(r.errors.size() == 1 && r.errors[0].find("no guest memory") != std::string::npos);
  bool kasumi_untouched = true;
  for (uint32_t a : {kMaxTable + 5, kLenTable + 5}) kasumi_untouched = kasumi_untouched && !g.written.count(a);
  for (uint32_t i = 0; i < 4; ++i) {
    kasumi_untouched = kasumi_untouched && !g.written.count(kListTable + 20 + i) &&
                       !g.written.count(kVarTable + 20 + i) &&
                       !g.written.count(kModelTable + 8 * 5 + 4 + i);
  }
  CHECK(kasumi_untouched);
  CHECK(r.applied.size() == 1 && r.applied[0].character == 13);  // Ayane still applied
}

// Task 1 limit 2: at most 17 costumes per character (Hitomi has 8).
static void TestMaxLimit() {
  std::vector<Suit> suits;
  for (int j = 0; j < 10; ++j) {
    suits.push_back(MakeSuit(21, -1, uint16_t(345 + 2 * j), uint16_t(346 + 2 * j), 559, 560, "vht_x"));
  }
  FakeGuest g;
  const ApplyResult r = Apply(g.guest, g.alloc, suits);  // 8 + 10 = 18: the tenth suit is past the limit
  CHECK(r.applied.size() == 9 && r.errors.size() == 1 && r.errors[0].find("at most 17") != std::string::npos);
  CHECK(g.Get(kMaxTable + 21) == 17);
  suits.pop_back();  // 8 + 9 = 17: allowed
  FakeGuest g2;
  CHECK(Apply(g2.guest, g2.alloc, suits).applied.size() == 9);
  CHECK(g2.Get(kMaxTable + 21) == 17);
}

// ---- The Extras boxes and the suits on the same tables (v1.0.9's doa4_tweaks, applied first) ----

static bool AnyAddress(uint32_t, uint32_t, bool) { return true; }

// Every box on, through the real patcher, as SetUpExtras does before SetUpSuits.
static void AllBoxesOn(FakeGuest& g, doa4::tweaks::TweakPatcher& patcher) {
  namespace tw = doa4::tweaks;
  patcher.CaptureOriginals(g.guest, g.alloc);
  for (size_t i = 0; i < tw::AllTweaks().size(); ++i) {
    CHECK(patcher.Apply(g.guest, i, true, AnyAddress).empty());
    CHECK(patcher.IsOn(g.guest, i));
  }
}

// Kokoro's and Kasumi's suits as the import names them (doax2_work/physics_spike/imports/all34_r14/manifest.txt).
static std::vector<Suit> KokoroSuits() {
  return {MakeSuit(10, 2, 321, 322, 569, 570, "vco_cs_i"), MakeSuit(10, -1, 319, 320, 569, 570, "vco_sk_c"),
          MakeSuit(10, 9, 335, 336, 569, 570, "vhe_on_c"), MakeSuit(10, 13, 343, 344, 569, 570, "vco_ch_f")};
}
static std::vector<Suit> KasumiSuits() {
  return {MakeSuit(5, 7, 269, 270, 509, 510, "vks_ch_d"), MakeSuit(5, 8, 271, 272, 509, 510, "vks_cs_a"),
          MakeSuit(5, -1, 273, 274, 509, 510, "vks_on_o"), MakeSuit(5, -1, 349, 350, 509, 510, "vks_hn_i")};
}

static uint32_t AppliedCount(const ApplyResult& r, uint32_t character) {
  uint32_t n = 0;
  for (const Applied& a : r.applied) n += a.character == character ? 1u : 0u;
  return n;
}

static void TweaksThenSuitsStayWithinLimit() {
  namespace tw = doa4::tweaks;
  // The brief's Kokoro and Kasumi: only Kokoro has a box (Kasumi's tables are the "no box" half of the fixture).
  {
    FakeGuest g;
    tw::TweakPatcher patcher;
    AllBoxesOn(g, patcher);
    const uint32_t tweak_list = g.Be32(kListTable + 4 * 10);
    CHECK(g.Get(kMaxTable + 10) == 9 && g.Get(kLenTable + 10) == 9);  // precondition: 7 of hers + the 2 kimonos
    CHECK(tweak_list >= 0x40000000);                                   // precondition: the box moved her list
    std::vector<Suit> suits = KokoroSuits();
    for (const Suit& s : KasumiSuits()) suits.push_back(s);
    for (const Suit& s : suits) {
      const std::string why = CheckSuit(g.guest, s);
      if (!why.empty()) std::printf("  %s: %s\n", s.design.c_str(), why.c_str());
      CHECK(why.empty());  // precondition: the fixture's suits are valid on the boxed tables
    }
    const ApplyResult r = Apply(g.guest, g.alloc, suits);
    CHECK(r.errors.empty());
    CHECK(r.applied.size() == 8);
    CHECK(g.Get(kMaxTable + 10) == 13 && g.Get(kLenTable + 10) == 13);  // the tweaks' 9 plus 4, NOT 7 + 4 = 11
    CHECK(g.Get(kMaxTable + 5) == 11 && g.Get(kLenTable + 5) == 32);    // her own 7 (28 list entries) plus 4
    static const int vanilla[7] = {0, 4, 6, 5, 7, 10, 11};
    for (uint32_t i = 0; i < 7; ++i) CHECK(GameLookup(g, 10, i, 0) == uint32_t(vanilla[i]));
    // The kimonos keep their costume numbers; an Apply that read the image's table would put the first suit at #7.
    CHECK(GameLookup(g, 10, 7, 0) == 8);
    CHECK(GameLookup(g, 10, 8, 0) == 3);
    CHECK(GameLookup(g, 10, 9, 0) == 2);    // own:2
    CHECK(GameLookup(g, 10, 10, 0) == 20);  // appended after her 20 records
    CHECK(GameLookup(g, 10, 11, 0) == 9);   // own:9
    CHECK(GameLookup(g, 10, 12, 0) == 13);  // own:13
    // Each suit's place in the list is past the tweaks' (own: models can be numerically below the kimonos' 8 and 3).
    for (const Applied& a : r.applied) {
      if (a.character == 10) CHECK(a.costume >= 9 && a.costume <= 12);
      if (a.character == 5) CHECK(a.costume >= 7 && a.costume <= 10);
    }
    CHECK(GameLookup(g, 5, 9, 0) == 31 && GameLookup(g, 5, 10, 0) == 32);
    // The other two boxes are untouched by suits on other girls.
    CHECK(GameLookup(g, 7, 5, 0) == 3);   // Helena's opera dress
    CHECK(GameLookup(g, 20, 4, 0) == 5);  // Christie's cutscene costume
  }
  // Exactly at the limit: 7 + 2 + 8 = 17, nothing refused.
  std::vector<Suit> many = KokoroSuits();
  for (int j = 0; j < 5; ++j) {
    many.push_back(MakeSuit(10, -1, uint16_t(211 + 2 * j), uint16_t(212 + 2 * j), 569, 570,
                            ("extra" + std::to_string(j + 1)).c_str()));
  }
  {
    FakeGuest g;
    tw::TweakPatcher patcher;
    AllBoxesOn(g, patcher);
    std::vector<Suit> fits(many.begin(), many.end() - 1);
    CHECK(9 + fits.size() == kMaxCostumesLimit);  // precondition: exactly at the limit
    const ApplyResult r = Apply(g.guest, g.alloc, fits);
    CHECK(r.errors.empty() && AppliedCount(r, 10) == 8);
    CHECK(g.Get(kMaxTable + 10) == 17 && g.Get(kLenTable + 10) == 17);
  }
  // One past it: 7 + 2 + 9 = 18. The ninth suit is refused by name, her first 17 stand, and another girl is not affected.
  {
    FakeGuest g;
    tw::TweakPatcher patcher;
    AllBoxesOn(g, patcher);
    std::vector<Suit> suits = many;
    CHECK(9 + suits.size() == kMaxCostumesLimit + 1);  // precondition: past the limit by one
    for (const Suit& s : KasumiSuits()) suits.push_back(s);
    const ApplyResult r = Apply(g.guest, g.alloc, suits);
    CHECK(AppliedCount(r, 10) == 8);
    CHECK(AppliedCount(r, 5) == 4);
    CHECK(r.errors.size() == 1);
    const std::string error = r.errors.empty() ? std::string() : r.errors[0];
    CHECK(error.find("extra5 c0") != std::string::npos);  // the suit, by the label the import's list shows
    CHECK(error.find("at most 17") != std::string::npos);  // and why
    CHECK(error.find("extra4") == std::string::npos);      // only that one
    CHECK(g.Get(kMaxTable + 10) == 17 && g.Get(kLenTable + 10) == 17);  // NOT 18, NOT left at the tweaks' 9
    static const int first17[17] = {0, 4, 6, 5, 7, 10, 11, 8, 3, 2, 20, 9, 13, 21, 22, 23, 24};
    for (uint32_t i = 0; i < 17; ++i) CHECK(GameLookup(g, 10, i, 0) == uint32_t(first17[i]));
    const uint32_t list = g.Be32(kListTable + 4 * 10), var = g.Be32(kVarTable + 4 * 10);
    CHECK(g.Get(var + 16) == 1 && g.Get(var + 17) == 0);  // no eighteenth costume
    CHECK(g.Get(list + 17) == 0);
    CHECK(r.guard.records[10] == 25);  // 20 + vco_sk_c + extra1..4: the refused append made no record
    CHECK(g.Get(kMaxTable + 5) == 11);
  }
}

// What SetUpSuits does after Apply so a box toggled later keeps her suits.
static void CarrySuits(FakeGuest& g, doa4::tweaks::TweakPatcher& patcher, const ApplyResult& r, std::string* error = nullptr) {
  std::map<uint32_t, std::vector<uint8_t>> models;
  for (const Applied& a : r.applied) models[a.character].push_back(uint8_t(a.model));
  for (const auto& [character, list] : models) {
    const std::string why = patcher.CarrySuits(g.guest, g.alloc, character, list);
    if (error) {
      *error += why;
    } else {
      CHECK(why.empty());
    }
  }
}

static void ExpectKokoro(const FakeGuest& g, const std::vector<int>& models) {
  const uint32_t n = uint32_t(models.size());
  CHECK(g.Get(kMaxTable + 10) == n && g.Get(kLenTable + 10) == n);
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t got = GameLookup(g, 10, i, 0);
    if (got != uint32_t(models[i])) std::printf("  costume #%u: model %u, want %d\n", i, got, models[i]);
    CHECK(got == uint32_t(models[i]));
  }
  // Written zeros after her last costume, so a stale costume number reads nothing (Task 1 limit 3).
  const uint32_t var = g.Be32(kVarTable + 4 * 10);
  bool padded = true;
  for (uint32_t i = n; i < 256; ++i) padded = padded && g.Get(var + i) == 0;
  CHECK(padded);
}

// The boxes are live ("Re-enter character select to see the change"): a box toggled after the suits went in must keep them.
static void TestBoxToggledAfterSuits() {
  namespace tw = doa4::tweaks;
  const size_t box = tw::kKokoroKimonos;
  const std::vector<int> off = {0, 4, 6, 5, 7, 10, 11, 2, 20, 9, 13};
  const std::vector<int> on = {0, 4, 6, 5, 7, 10, 11, 8, 3, 2, 20, 9, 13};
  {  // On at start, then off, then on again.
    FakeGuest g;
    tw::TweakPatcher patcher;
    AllBoxesOn(g, patcher);
    const ApplyResult r = Apply(g.guest, g.alloc, KokoroSuits());
    CarrySuits(g, patcher, r);
    ExpectKokoro(g, on);
    CHECK(patcher.IsOn(g.guest, box));
    CHECK(patcher.Apply(g.guest, box, false, AnyAddress).empty());
    ExpectKokoro(g, off);  // NOT the image's 7: her suits stay
    CHECK(!patcher.IsOn(g.guest, box));
    CHECK(patcher.Apply(g.guest, box, true, AnyAddress).empty());
    ExpectKokoro(g, on);
    CHECK(patcher.IsOn(g.guest, box));
    CHECK(g.Get(kMaxTable + 5) == 7);  // nobody else moved
  }
  {  // Off at start, then on, then off again.
    FakeGuest g;
    tw::TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    const ApplyResult r = Apply(g.guest, g.alloc, KokoroSuits());
    CarrySuits(g, patcher, r);
    ExpectKokoro(g, off);
    CHECK(!patcher.IsOn(g.guest, box));
    CHECK(patcher.Apply(g.guest, box, true, AnyAddress).empty());
    ExpectKokoro(g, on);  // NOT the box's own 9
    CHECK(patcher.IsOn(g.guest, box));
    CHECK(patcher.Apply(g.guest, box, false, AnyAddress).empty());
    ExpectKokoro(g, off);
    CHECK(!patcher.IsOn(g.guest, box));
  }
  {  // Off at start, suits up to 16: the box would make 18, so it is refused by name and nothing moves; 15 + 2 fits.
    std::vector<Suit> suits = KokoroSuits();
    for (int j = 0; j < 5; ++j) {
      suits.push_back(MakeSuit(10, -1, uint16_t(211 + 2 * j), uint16_t(212 + 2 * j), 569, 570,
                               ("extra" + std::to_string(j + 1)).c_str()));
    }
    FakeGuest g;
    tw::TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    CHECK(7 + suits.size() == 16);  // precondition
    CarrySuits(g, patcher, Apply(g.guest, g.alloc, suits));
    const uint32_t list = g.Be32(kListTable + 4 * 10);
    const std::string refused = patcher.Apply(g.guest, box, true, AnyAddress);
    CHECK(refused.find("at most 17") != std::string::npos);
    CHECK(g.Get(kMaxTable + 10) == 16 && g.Get(kLenTable + 10) == 16);
    CHECK(g.Be32(kListTable + 4 * 10) == list);
    CHECK(patcher.Apply(g.guest, box, false, AnyAddress).empty());  // off is always there
    CHECK(g.Get(kMaxTable + 10) == 16);
    suits.pop_back();  // 15 + 2 = 17: allowed
    FakeGuest h;
    tw::TweakPatcher fits;
    fits.CaptureOriginals(h.guest, h.alloc);
    CarrySuits(h, fits, Apply(h.guest, h.alloc, suits));
    CHECK(fits.Apply(h.guest, box, true, AnyAddress).empty());
    CHECK(h.Get(kMaxTable + 10) == 17 && h.Get(kLenTable + 10) == 17);
    CHECK(GameLookup(h, 10, 7, 0) == 8 && GameLookup(h, 10, 16, 0) == 24);  // m20 vco_sk_c, then extra1..4 = m21..m24
  }
  {  // A girl with no box: nothing to carry, no memory taken. A box with no suits on its girl: as it was.
    FakeGuest g;
    tw::TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    const ApplyResult r = Apply(g.guest, g.alloc, KasumiSuits());
    const int allocs = g.allocs;
    CarrySuits(g, patcher, r);
    CHECK(g.allocs == allocs);
    CHECK(patcher.Apply(g.guest, tw::kHelenaOpera, true, AnyAddress).empty());
    CHECK(g.Get(kMaxTable + 7) == 6 && GameLookup(g, 7, 5, 0) == 3);
    CHECK(patcher.Apply(g.guest, tw::kHelenaOpera, false, AnyAddress).empty());
    CHECK(g.Get(kMaxTable + 7) == 5);
    CHECK(g.Be32(kListTable + 4 * 7) == 0x82C34758);  // back on the image's own list
    CHECK(g.Get(kMaxTable + 5) == 11 && g.Get(kLenTable + 5) == 32);  // Kasumi's suits, untouched by Helena's box
  }
  // The same two blocks whichever state the box is in: the heap of two players with suits differs only by their imports.
  int allocs_on = 0, allocs_off = 0;
  {
    FakeGuest g;
    tw::TweakPatcher patcher;
    AllBoxesOn(g, patcher);
    const ApplyResult r = Apply(g.guest, g.alloc, KokoroSuits());
    const int before = g.allocs;
    CarrySuits(g, patcher, r);
    allocs_on = g.allocs - before;
  }
  {
    FakeGuest g;
    tw::TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    const ApplyResult r = Apply(g.guest, g.alloc, KokoroSuits());
    const int before = g.allocs;
    CarrySuits(g, patcher, r);
    allocs_off = g.allocs - before;
  }
  CHECK(allocs_on == 2 && allocs_off == 2);
  {  // No memory for the other state: the box stays as it is with its reason, and her suits stay in the tables.
    FakeGuest g;
    tw::TweakPatcher patcher;
    AllBoxesOn(g, patcher);
    const ApplyResult r = Apply(g.guest, g.alloc, KokoroSuits());
    g.fail_alloc_at = g.allocs;
    std::string error;
    CarrySuits(g, patcher, r, &error);
    CHECK(error.find("no guest memory") != std::string::npos);
    const std::string refused = patcher.Apply(g.guest, box, false, AnyAddress);
    CHECK(refused.find("no guest memory") != std::string::npos);
    ExpectKokoro(g, on);
  }
  // With no suits the boxes take what they took in v1.0.9: two 32-byte blocks per addition, three additions, so a session
  // with no suits has the same heap and the same frame-0 tables.
  {
    FakeGuest g;
    tw::TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    CHECK(g.allocs == 6);
    CHECK(g.next_block == 0x40000000 + 6 * 32);
  }
  // Each box names its own girl: two boxes on one girl would need four states, not two (TweakPatcher::CarrySuits).
  {
    std::set<uint32_t> seen;
    size_t additions = 0;
    for (const tw::Tweak& t : tw::AllTweaks()) {
      for (const tw::CostumeAddition& a : t.additions) {
        seen.insert(a.character);
        ++additions;
      }
    }
    CHECK(additions == 3 && seen.size() == additions);
  }
}

// Task 5: a session's tables come from the picks, so two PCs build the same bytes whatever each holds. With every box on, the host's
// settings apply first on both PCs and then the picks (SetUpExtras, then SetUpSuits). Both guests go through the whole sequence;
// the comparison is over every byte of each, and the fixture's preconditions show something was written.
static std::map<uint32_t, uint8_t> SessionTables(bool boxes, ApplyResult* out) {
  namespace tw = doa4::tweaks;
  FakeGuest g;
  tw::TweakPatcher patcher;
  if (boxes) {
    AllBoxesOn(g, patcher);
  } else {
    patcher.CaptureOriginals(g.guest, g.alloc);
  }
  std::vector<std::string> refused;
  const std::vector<Suit> fit = doa4::costumes::Fitting(g.guest, doa4::suits::PickSuits(), &refused);
  CHECK(refused.empty());  // all 34 pass CheckSuit on the image's real tables, boxes or not
  CHECK(fit.size() == 34);
  *out = Apply(g.guest, g.alloc, fit);
  CarrySuits(g, patcher, *out);
  return g.bytes;
}

static constexpr uint32_t g_table_bytes_list_at = 30;  // inside Kasumi's list of 28 + 4 models

static void TestFitting() {
  FakeGuest g;
  // Ayane has a head part, so a suit that names none is refused; the next one stays, and the order holds.
  const Suit bad = MakeSuit(13, -1, 275, 276, kNoPart, kNoPart, "vay_cs_b");
  const Suit good = MakeSuit(5, 7, 269, 270, 509, 510, "vks_ch_d");
  const Suit good2 = MakeSuit(5, 8, 271, 272, 509, 510, "vks_cs_a");
  CHECK(!CheckSuit(g.guest, bad).empty() && CheckSuit(g.guest, good).empty());
  std::vector<std::string> refused;
  const std::vector<Suit> fit = doa4::costumes::Fitting(g.guest, {good, bad, good2}, &refused);
  CHECK(fit.size() == 2);
  if (fit.size() == 2) CHECK(fit[0].design == "vks_ch_d" && fit[1].design == "vks_cs_a");
  CHECK(refused.size() == 1);
  if (refused.size() == 1) CHECK(refused[0].rfind("vay_cs_b c0: ", 0) == 0);
  CHECK(doa4::costumes::Fitting(g.guest, {bad}, nullptr).empty());  // no list asked for: still refused, and no crash
}

static void TestPicksMakeTheSameTables() {
  for (const bool boxes : {false, true}) {
    ApplyResult a, b;
    const auto host = SessionTables(boxes, &a);
    const auto joiner = SessionTables(boxes, &b);
    CHECK(host == joiner);
    CHECK(a.applied.size() == b.applied.size());
    CHECK(a.errors == b.errors);
    // non-vacuous: every suit went in (no girl is over the limit with the boxes on), and the tables moved
    CHECK(a.applied.size() == 34);
    CHECK(a.errors.empty());
    FakeGuest vanilla;
    CHECK(host != vanilla.bytes);
  }
  // TableBytes: what the session's log line digests. Two peers agree; a byte in a moved block (outside .data) shows.
  {
    FakeGuest h, j;
    const std::vector<Suit> picks = doa4::suits::PickSuits();
    const ApplyResult ra = Apply(h.guest, h.alloc, doa4::costumes::Fitting(h.guest, picks, nullptr));
    const ApplyResult rb = Apply(j.guest, j.alloc, doa4::costumes::Fitting(j.guest, picks, nullptr));
    const std::vector<uint8_t> ta = doa4::costumes::TableBytes(h.guest, ra.guard);
    CHECK(ta.size() > 2000);
    CHECK(ta == doa4::costumes::TableBytes(j.guest, rb.guard));
    const uint32_t list = j.Be32(kListTable + 4 * 5), var = j.Be32(kVarTable + 4 * 5), records = j.Be32(kModelTable + 8 * 5 + 4);
    CHECK(list >= 0x40000000 && var >= 0x40000000 && records >= 0x40000000);  // Kasumi's three blocks moved
    for (const uint32_t a : {list + g_table_bytes_list_at, var + 10, records + 8 * 32 + 7}) {
      const uint8_t was = j.Get(a);
      j.bytes[a] = uint8_t(was + 1);
      CHECK(ta != doa4::costumes::TableBytes(j.guest, rb.guard));
      j.bytes[a] = was;
    }
    CHECK(ta == doa4::costumes::TableBytes(j.guest, rb.guard));
    j.bytes[kMaxTable + 5] = uint8_t(j.Get(kMaxTable + 5) + 1);  // a table in the image
    CHECK(ta != doa4::costumes::TableBytes(j.guest, rb.guard));
    FakeGuest vanilla;
    CHECK(doa4::costumes::TableBytes(vanilla.guest, ReadGuardState(vanilla.guest)) != ta);
  }
  // A control the comparison can fail on: boxes on one side only.
  ApplyResult a, b;
  const auto with = SessionTables(true, &a);
  const auto without = SessionTables(false, &b);
  CHECK(with != without);

  // Kasumi's list holds her own 7 and the four suits, in the picks' order, and an append is past her records; Kokoro with her box
  // on has her two kimonos first. Numbers from the manifest.
  namespace tw = doa4::tweaks;
  FakeGuest g;
  tw::TweakPatcher patcher;
  AllBoxesOn(g, patcher);
  const std::vector<Suit> fit = doa4::costumes::Fitting(g.guest, doa4::suits::PickSuits(), nullptr);
  const ApplyResult r = Apply(g.guest, g.alloc, fit);
  CHECK(g.Get(kMaxTable + 5) == 11);
  CHECK(GameLookup(g, 5, 7, 0) == 7);    // vks_ch_d own:7
  CHECK(GameLookup(g, 5, 8, 0) == 8);    // vks_cs_a own:8
  CHECK(GameLookup(g, 5, 9, 0) == 31);   // vks_on_o append: past her 31 records (RecordCount, pinned in TestRecordCounts)
  CHECK(GameLookup(g, 5, 10, 0) == 32);  // vks_hn_i append
  CHECK(g.Get(kMaxTable + 10) == 13);    // Kokoro: 7 + 2 kimonos + 4 suits
  CHECK(GameLookup(g, 10, 9, 0) == 2);   // vco_cs_i own:2, after the kimonos
}

static void TestGuardModel() {
  GuardState s;
  s.records[5] = 32;
  CHECK(GuardModel(s, 5, 31, 0) == 31);
  CHECK(GuardModel(s, 5, 32, 4) == 4);
  CHECK(GuardModel(s, 5, 255, 4) == 255);   // 255 = "none" to the loader (sub_823FE870): passes through
  CHECK(GuardModel(s, 5, 254, 4) == 4);     // 254 >= 32: still guarded
  CHECK(GuardModel(s, 24, 200, 4) == 200);  // unknown count: unguarded
  CHECK(GuardModel(s, 30, 200, 4) == 200);  // not a character
}

// ---- Edge checks added after the executed mutation probe (Task 5, round 1 survivors) ----
// Each names the mutant it kills (see the commit message). Expected counts and file numbers come
// from an independent read of the image, not from the code under test. Poked bytes go straight
// into the fake's memory (not into `written`), to build the inputs the real image never holds.

static void PokeBe32(FakeGuest& g, uint32_t a, uint32_t v) {
  for (int i = 0; i < 4; ++i) g.bytes[a + uint32_t(i)] = uint8_t(v >> (24 - 8 * i));
}

static void PokeBe16(FakeGuest& g, uint32_t a, uint16_t v) {
  g.bytes[a] = uint8_t(v >> 8);
  g.bytes[a + 1] = uint8_t(v);
}

static void ExpectCheckOn(const FakeGuest& g, const Suit& s, const std::string& want) {
  const std::string why = CheckSuit(g.guest, s);
  const bool ok = want.empty() ? why.empty() : why.find(want) != std::string::npos;
  if (!ok) std::printf("  %s: want '%s', got '%s'\n", s.design.c_str(), want.c_str(), why.c_str());
  CHECK(ok);
}

static Suit WithSizes(Suit s, uint64_t tpr, uint64_t cat) {
  s.tpr.size = tpr;
  s.cat.size = cat;
  return s;
}

static void TestRecordCountsAll() {
  FakeGuest g;
  static const uint32_t want[25] = {5, 6, 5, 5, 4, 31, 5, 7, 4, 4, 20, 5, 48, 6, 5, 10, 2, 3, 8, 4, 7, 10, 5, 31, 0};
  const GuardState s = ReadGuardState(g.guest);
  bool all = true;
  for (uint32_t c = 0; c < kCharCount; ++c) {
    all = all && s.records[c] == want[c] && RecordCount(g.guest, c) == want[c];
  }
  CHECK(all);  // every character, 22 and 23 included (a guard state that stops short)
  // Char 24 is the last block only because no block lies above it: raise char 0's block above it.
  FakeGuest h;
  PokeBe32(h, kModelTable + 4, 0x82C5E800);
  CHECK(RecordCount(h.guest, 24) == 28);  // (0x82C5E800 - 0x82C5E720) / 8
  CHECK(ReadGuardState(h.guest).records[24] == 28);
  // c == 25 reads the split table's first word (0 in the image): make it look like a block pointer.
  FakeGuest p;
  PokeBe32(p, kModelTable + 8 * 25 + 4, 0x82C5E000);
  CHECK(RecordCount(p.guest, 25) == 0);
}

// A patched character's record block lives on the heap; RecordCount reads blocks inside the image.
static void TestRecordCountOutsideImage() {
  FakeGuest g;  // heap below the image
  Apply(g.guest, g.alloc, {MakeSuit(5, -1, 349, 350, 509, 510, "vks_hn_i")});
  const uint32_t low = g.Be32(kModelTable + 8 * 5 + 4);
  CHECK(low >= 0x40000000 && low < kImageStart);  // precondition: the block moved below the image
  CHECK(RecordCount(g.guest, 5) == 0);            // a heap start would count up to the next image block
  FakeGuest h;
  h.next_block = 0xA0000000;  // physical heap, above the image
  Apply(h.guest, h.alloc, {MakeSuit(5, -1, 349, 350, 509, 510, "vks_hn_i")});
  CHECK(h.Be32(kModelTable + 8 * 5 + 4) == 0xA0000000);  // precondition
  CHECK(RecordCount(h.guest, 24) == 0);  // a heap block above the image must not end char 24's block
  CHECK(RecordCount(h.guest, 13) == 6);
}

static void ExpectFile(const FakeGuest& g, uint32_t file, const char* archive, uint32_t entry) {
  const auto loc = FileLocation(g.guest, file);
  const bool ok = loc && std::string(loc->archive) == archive && loc->entry == entry;
  if (!ok) std::printf("  file %u: want %s entry %u\n", file, archive, entry);
  CHECK(ok);
}

static void ExpectNoFile(const FakeGuest& g, uint32_t file) {
  const bool none = !FileLocation(g.guest, file);
  if (!none) std::printf("  file %u is not a character archive file\n", file);
  CHECK(none);
}

static void TestFileEdges() {
  FakeGuest g;
  // The character archives hold files 204-276 (archive 3), 277-404 (4) and 405-592 (5).
  ExpectFile(g, 204, "chrone.afs", 0);
  ExpectFile(g, 276, "chrone.afs", 72);
  ExpectFile(g, 277, "chrtwo.afs", 0);
  ExpectFile(g, 404, "chrtwo.afs", 127);
  ExpectFile(g, 405, "chrthree.afs", 0);
  ExpectFile(g, 592, "chrthree.afs", 187);
  ExpectNoFile(g, 203);   // archive 7
  ExpectNoFile(g, 593);   // archive 1
  ExpectNoFile(g, 0);     // archive 6 (next to 5)
  ExpectNoFile(g, 6);     // archive 6
  ExpectNoFile(g, 1614);  // archive 2 (next to 3)
  ExpectNoFile(g, 1639);  // the last file
  // The table ends at 1640, even when the bytes after it look like an entry of archive 3.
  g.bytes[kFileTable + 4 * 1640] = 3;
  g.bytes[kFileTable + 4 * 1640 + 3] = 5;
  ExpectNoFile(g, 1640);
}

static void TestCheckSuitEdges() {
  // Character bounds: 24 is the last character, 25 is none.
  ExpectCheck(MakeSuit(24, -1, 503, 504, kNoPart, kNoPart, "c24"), "");
  ExpectCheck(MakeSuit(25, -1, 211, 212, kNoPart, kNoPart, "c25"), "no such character");
  ExpectCheck(MakeSuit(255, -1, 211, 212, kNoPart, kNoPart, "c255"), "no such character");
  // The costume list needs BOTH pointers inside the image.
  {
    FakeGuest g;
    PokeBe32(g, kVarTable, 0x831A649F);  // charId 0's variant pointer, outside
    ExpectCheckOn(g, MakeSuit(0, -1, 211, 212, kNoPart, kNoPart, "tvar"), "no costume list");
  }
  struct Edge {
    uint32_t ptr;
    const char* want;
  };
  for (const Edge& e : {Edge{0x81FFFFFF, "no costume list"}, Edge{0x82000000, ""}, Edge{0x82FFFFFF, ""},
                        // a block an Extras box moved her list to, in either kind of heap; never 0, never the rest of the XEX heap
                        Edge{0x10000000, ""}, Edge{0x7FFFFFFF, ""}, Edge{0xA0000000, ""}, Edge{0, "no costume list"},
                        Edge{0x80000000, "no costume list"}, Edge{0x9FFFFFFF, "no costume list"},
                        Edge{0x83000000, "no costume list"}}) {
    FakeGuest g;
    PokeBe32(g, kListTable, e.ptr);  // charId 0's list pointer at the edges of [0x82000000, 0x83000000)
    ExpectCheckOn(g, MakeSuit(0, -1, 211, 212, kNoPart, kNoPart, "tlist"), e.want);
  }
  // Each of the two body files must be a character archive file.
  ExpectCheck(MakeSuit(0, -1, 211, 23, kNoPart, kNoPart, "bcat"), "not character archive files");
  ExpectCheck(MakeSuit(0, -1, 23, 212, kNoPart, kNoPart, "btpr"), "not character archive files");
  ExpectCheck(MakeSuit(0, -1, 211, 1640, kNoPart, kNoPart, "bend"), "not character archive files");
  // Budgets, Kasumi (0xA00000 / 0x200000): the exact budget fits, one byte more does not.
  ExpectCheck(WithSizes(MakeSuit(5, 7, 269, 270, 509, 510, "kt0"), 0xA00000, 0x200000), "");
  ExpectCheck(WithSizes(MakeSuit(5, 7, 269, 270, 509, 510, "kc1"), 0xA00000, 0x200001),
              "the .cat file is 0x200001 bytes, over her budget of 0x200000");
  // Budgets, charId 0 (no head part: the whole 0x1400000 / 0x400000).
  ExpectCheck(WithSizes(MakeSuit(0, -1, 211, 212, kNoPart, kNoPart, "tw0"), 0x1400000, 0x400000), "");
  ExpectCheck(WithSizes(MakeSuit(0, -1, 211, 212, kNoPart, kNoPart, "tw1"), 0x1400001, 0x800),
              "the model file is 0x1400001 bytes, over her budget of 0x1400000");
  ExpectCheck(WithSizes(MakeSuit(0, -1, 211, 212, kNoPart, kNoPart, "tw2"), 0x400000, 0x400001),
              "the .cat file is 0x400001 bytes, over her budget of 0x400000");
  // A split with only one zero half is not the whole budget (0/0 is).
  {
    FakeGuest g;
    PokeBe32(g, kSplitTable + 8 * 5 + 4, 0);
    ExpectCheckOn(g, MakeSuit(5, 7, 269, 270, 509, 510, "ksplit"), "the .cat file is 0x800 bytes, over her budget of 0x0");
  }
  // Model record bounds: Kasumi has m0..m30.
  ExpectCheck(MakeSuit(5, 30, 267, 268, 515, 516, "k30"), "");
  ExpectCheck(MakeSuit(5, 31, 255, 256, 509, 510, "k31"), "does not exist (she has 31)");
  // Both body files must match the record (m7 holds 269/270).
  ExpectCheck(MakeSuit(5, 7, 269, 272, 509, 510, "kbc"), "holds files 269/270");
  ExpectCheck(MakeSuit(5, 7, 271, 270, 509, 510, "kbt"), "holds files 269/270");
  // Both halves of an existing head part must match (Kokoro m2 holds 569/570).
  ExpectCheck(MakeSuit(10, 2, 321, 322, 569, 546, "kkp"), "already has head part 569/570");
  ExpectCheck(MakeSuit(10, 2, 321, 322, 545, 570, "kkc"), "already has head part 569/570");
  // A record with only one empty half is not empty.
  {
    FakeGuest g;
    const uint32_t m7 = g.Be32(kModelTable + 8 * 5 + 4) + 8 * 7;
    PokeBe16(g, m7 + 4, 0xFFFF);
    PokeBe16(g, m7 + 6, 509);
    ExpectCheckOn(g, MakeSuit(5, 7, 269, 270, 509, 510, "khalf"), "already has head part 65535/509");
  }
}

// Fix round 1 (I1): the head-part pair a suit names must be a pair one of HER records already uses, and
// both part files must be character archive files; otherwise a hand-edited manifest loads another girl's
// head (or a motion file) as hers at fight time. Her pairs, from the image: Kasumi 509/510 (m0..m6),
// 511/512, 513/514, 515/516 (m24..m30); Ayane 543/544; Kokoro 569/570 among others.
static void TestHeadPartPair() {
  // Accepted: pairs her own records use.
  ExpectCheck(MakeSuit(5, -1, 349, 350, 509, 510, "hp509"), "");
  ExpectCheck(MakeSuit(5, -1, 349, 350, 515, 516, "hp515"), "");
  ExpectCheck(MakeSuit(10, 2, 321, 322, 569, 570, "hpkk"), "");  // Kokoro own:2, as before
  ExpectCheck(MakeSuit(13, -1, 275, 276, 543, 544, "hpay"), "");
  // Refused: both files are character archive files, but the pair is not one of hers.
  ExpectCheck(MakeSuit(5, -1, 349, 350, 543, 544, "hpother"), "head part 543/544 is not one of her head parts");
  ExpectCheck(MakeSuit(5, -1, 349, 350, 510, 509, "hpswap"), "head part 510/509 is not one of her head parts");
  ExpectCheck(MakeSuit(5, -1, 349, 350, 509, 512, "hphalfa"), "head part 509/512 is not one of her head parts");
  ExpectCheck(MakeSuit(5, -1, 349, 350, 511, 510, "hphalfb"), "head part 511/510 is not one of her head parts");
  ExpectCheck(MakeSuit(5, 7, 269, 270, 543, 544, "hpown"), "head part 543/544 is not one of her head parts");
  // Ayane's block ends where charId 14's begins, and charId 14's first record holds 567/568.
  ExpectCheck(MakeSuit(13, -1, 275, 276, 567, 568, "hpnext"), "head part 567/568 is not one of her head parts");
  // Refused: a part file is not a character archive file. This check runs before the pair test, so these
  // give the "head part files" message, not the "not one of her head parts" one.
  ExpectCheck(MakeSuit(5, -1, 349, 350, 23, 24, "hpmot"), "head part files 23/24 are not character archive files");
  ExpectCheck(MakeSuit(5, -1, 349, 350, 1639, 1639, "hplast"),
              "head part files 1639/1639 are not character archive files");
  ExpectCheck(MakeSuit(5, -1, 349, 350, 23, 510, "hptpr"), "head part files 23/510 are not character archive files");
  ExpectCheck(MakeSuit(5, -1, 349, 350, 509, 24, "hpcat"), "head part files 509/24 are not character archive files");
  ExpectCheck(MakeSuit(5, -1, 349, 350, 509, kNoPart, "hpnone"),
              "head part files 509/65535 are not character archive files");
  // Poked records: the scan covers her first and last record, and the file test does not lean on the pair test.
  {
    FakeGuest g;
    const uint32_t rec = g.Be32(kModelTable + 8 * 5 + 4);
    PokeBe16(g, rec + 4, 407);
    PokeBe16(g, rec + 6, 408);  // m0
    PokeBe16(g, rec + 8 * 30 + 4, 405);
    PokeBe16(g, rec + 8 * 30 + 6, 406);  // m30, her last record
    ExpectCheckOn(g, MakeSuit(5, -1, 349, 350, 407, 408, "hpfirst"), "");
    ExpectCheckOn(g, MakeSuit(5, -1, 349, 350, 405, 406, "hplastrec"), "");
  }
  {
    FakeGuest g;
    const uint32_t rec = g.Be32(kModelTable + 8 * 5 + 4);
    PokeBe16(g, rec + 4, 23);
    PokeBe16(g, rec + 6, 24);  // one of her records holds a pair that is not character files
    ExpectCheckOn(g, MakeSuit(5, -1, 349, 350, 23, 24, "hpmotrec"),
                  "head part files 23/24 are not character archive files");
  }
}

// Whichever allocation fails, nothing is written and no later block is asked for.
static void TestAllocFailures() {
  for (int fail = 0; fail < 3; ++fail) {  // Kasumi appends: record, list and variant blocks
    FakeGuest g;
    g.fail_alloc_at = fail;
    const ApplyResult r = Apply(g.guest, g.alloc, {MakeSuit(5, -1, 349, 350, 509, 510, "vks_hn_i")});
    CHECK(r.applied.empty() && r.errors.size() == 1 && r.errors[0].find("no guest memory") != std::string::npos);
    CHECK(g.written.empty());
    CHECK(g.allocs == fail + 1);
    CHECK(r.guard.records[5] == 31);
  }
  for (int fail = 0; fail < 2; ++fail) {  // Ayane fills her empty m5 in place: list and variant blocks
    FakeGuest g;
    g.fail_alloc_at = fail;
    const uint32_t m5 = g.Be32(kModelTable + 8 * 13 + 4) + 8 * 5;
    const ApplyResult r = Apply(g.guest, g.alloc, {MakeSuit(13, 5, 403, 404, 543, 544, "vay_x")});
    CHECK(r.applied.empty() && r.errors.size() == 1 && r.errors[0].find("no guest memory") != std::string::npos);
    CHECK(g.written.empty());  // the head part is not filled before the blocks exist
    CHECK(g.allocs == fail + 1);
    CHECK(g.Be16(m5 + 4) == 0xFFFF && g.Be16(m5 + 6) == 0xFFFF);
  }
}

// The blocks asked for have the sizes the tables need, and every byte of each is written (a real
// heap block is not zero). The fake allocator above rounds sizes up to 32 and leaves no gap, so
// this one hands out exact blocks with a gap after each: a write past a block lands in the gap.
static void TestAllocBlocks() {
  struct Case {
    int character;
    int own;
    uint16_t tpr, cat, ptpr, pcat;
    std::vector<uint32_t> sizes;
  };
  for (const Case& c : {Case{13, -1, 275, 276, 543, 544, {56, 256, 256}},   // 7 records of 8 bytes
                        Case{5, -1, 349, 350, 509, 510, {256, 256, 256}},   // 32 records
                        Case{13, 5, 403, 404, 543, 544, {256, 256}}}) {     // in place: no record block
    FakeGuest g;
    std::vector<std::pair<uint32_t, uint32_t>> blocks;
    uint32_t next = 0x40000000;
    const AllocFn exact = [&](uint32_t size) -> uint32_t {
      blocks.push_back({next, size});
      const uint32_t block = next;
      next += size + 64;
      return block;
    };
    const ApplyResult r =
        Apply(g.guest, exact, {MakeSuit(uint8_t(c.character), c.own, c.tpr, c.cat, c.ptpr, c.pcat, "vx")});
    CHECK(r.errors.empty() && r.applied.size() == 1);
    bool sizes = blocks.size() == c.sizes.size();
    for (size_t i = 0; sizes && i < blocks.size(); ++i) sizes = blocks[i].second == c.sizes[i];
    CHECK(sizes);
    bool inside = true, whole = true;
    for (uint32_t a : g.written) {
      if (a >= kImageStart) continue;  // the image's own tables (the heap blocks lie below it)
      bool in = false;
      for (const auto& b : blocks) in = in || (a >= b.first && a < b.first + b.second);
      inside = inside && in;
    }
    for (const auto& b : blocks) {
      for (uint32_t i = 0; i < b.second; ++i) whole = whole && g.written.count(b.first + i);
    }
    CHECK(inside);  // nothing is written past a block
    CHECK(whole);   // every byte of every block is written
  }
}

// A suit that fills one of her own empty records appends no record: the block stays in the image.
static void TestApplyInPlace() {
  FakeGuest g;
  const uint32_t records = g.Be32(kModelTable + 8 * 13 + 4);
  CHECK(g.Be16(records + 8 * 5 + 4) == 0xFFFF && g.Be16(records + 8 * 5 + 6) == 0xFFFF);  // Ayane m5 is empty
  const ApplyResult r = Apply(g.guest, g.alloc, {MakeSuit(13, 5, 403, 404, 543, 544, "vay_x")});
  CHECK(r.errors.empty() && r.applied.size() == 1);
  CHECK(r.applied.size() == 1 && r.applied[0].model == 5 && r.applied[0].costume == 6);
  CHECK(g.Be32(kModelTable + 8 * 13 + 4) == records);  // no copy: the pointer stays
  CHECK(g.Be16(records + 8 * 5 + 4) == 543 && g.Be16(records + 8 * 5 + 6) == 544);
  CHECK(r.guard.records[13] == 6);  // no record was appended
  CHECK(GameLookup(g, 13, 6, 0) == 5);
  CHECK(g.Get(kMaxTable + 13) == 7 && g.Get(kLenTable + 13) == 7);
  // A record that already has a head part keeps it, even for a suit CheckSuit would refuse.
  FakeGuest k;
  const uint32_t kk = k.Be32(kModelTable + 8 * 10 + 4);
  Apply(k.guest, k.alloc, {MakeSuit(10, 2, 321, 322, 545, 546, "kk2b")});
  CHECK(k.Be16(kk + 8 * 2 + 4) == 569 && k.Be16(kk + 8 * 2 + 6) == 570);
  // A suit without a head part (charId 0, whole budget) rewrites no byte of her record.
  FakeGuest t;
  const uint32_t tr = t.Be32(kModelTable + 4);
  Apply(t.guest, t.alloc, {MakeSuit(0, 3, 211, 212, kNoPart, kNoPart, "ti3")});
  bool untouched = true;
  for (uint32_t i = 0; i < 8 * 5; ++i) untouched = untouched && !t.written.count(tr + i);
  CHECK(untouched);
  CHECK(GameLookup(t, 0, 4, 0) == 3 && t.Get(kMaxTable) == 5);
}

// Nine appended records on a character with several variants: distinct models, old costumes intact.
static void TestApplyManyAppends() {
  const FakeGuest before;
  FakeGuest g;
  std::vector<Suit> suits;
  for (int j = 0; j < 9; ++j) {
    suits.push_back(MakeSuit(21, -1, uint16_t(345 + 2 * j), uint16_t(346 + 2 * j), 559, 560, "vht_x"));
  }
  const ApplyResult r = Apply(g.guest, g.alloc, suits);
  CHECK(r.errors.empty() && r.applied.size() == 9);
  const uint32_t records = g.Be32(kModelTable + 8 * 21 + 4);
  bool ok = r.applied.size() == 9;
  for (uint32_t j = 0; ok && j < 9; ++j) {
    const uint32_t at = records + 8 * (10 + j);
    ok = r.applied[j].model == 10 + j && r.applied[j].costume == 8 + j && g.Be16(at) == 345 + 2 * j &&
         g.Be16(at + 2) == 346 + 2 * j && g.Be16(at + 4) == 559 && g.Be16(at + 6) == 560 &&
         GameLookup(g, 21, 8 + j, 0) == 10 + j;
  }
  CHECK(ok);
  CHECK(g.Get(kMaxTable + 21) == 17 && g.Get(kLenTable + 21) == 20);
  CHECK(r.guard.records[21] == 19);
  bool same = true, varied = false;
  for (uint32_t k = 0; k < 8; ++k) {
    for (uint32_t v = 0; v < 4; ++v) same = same && GameLookup(g, 21, k, v) == GameLookup(before, 21, k, v);
    varied = varied || GameLookup(before, 21, k, 1) != GameLookup(before, 21, k, 0);
  }
  CHECK(same);    // her eight costumes look up as before
  CHECK(varied);  // non-vacuity: some costume of hers has more than one variant
  CHECK(GameLookup(g, 21, 12, 3) == GameLookup(g, 21, 12, 0));  // the new ones have one
}

// Refusals and the edges of the two u8 limits (a model index and a list length are bytes).
static void TestApplyRefusals() {
  {  // Char 24's block is the last in the image: no count, so no record can be appended.
    FakeGuest g;
    const ApplyResult r = Apply(g.guest, g.alloc, {MakeSuit(24, -1, 503, 504, kNoPart, kNoPart, "c24")});
    CHECK(r.applied.empty() && r.errors.size() == 1 && r.errors[0].find("cannot be counted") != std::string::npos);
    CHECK(g.written.empty() && g.allocs == 0);
  }
  for (uint32_t len : {254u, 255u}) {  // list length + 1 must stay within the 255 entries a block holds
    FakeGuest g;
    g.bytes[kLenTable + 21] = uint8_t(len);
    const ApplyResult r = Apply(g.guest, g.alloc, {MakeSuit(21, -1, 345, 346, 559, 560, "vht_x")});
    if (len == 254) {
      CHECK(r.errors.empty() && r.applied.size() == 1 && g.Get(kLenTable + 21) == 255);
    } else {
      CHECK(r.applied.empty() && r.errors.size() == 1 && r.errors[0].find("too many costumes") != std::string::npos);
      CHECK(g.written.empty());
    }
  }
  // Model index 255 means "none" to the loader (sub_823FE870 returns without loading, .9.cpp:53983-53987),
  // so the highest index an append may produce is 254: records + appended <= 255.
  for (uint32_t blocks : {254u, 255u}) {
    FakeGuest g;
    PokeBe32(g, kModelTable + 4, 0x82C5E720 + 8 * blocks);  // char 0's block above char 24's
    CHECK(RecordCount(g.guest, 24) == blocks);               // precondition
    const ApplyResult r = Apply(g.guest, g.alloc, {MakeSuit(24, -1, 503, 504, kNoPart, kNoPart, "c24")});
    if (blocks == 254) {
      CHECK(r.errors.empty() && r.applied.size() == 1 && r.applied[0].model == 254);
      CHECK(GameLookup(g, 24, 3, 0) == 254);
    } else {
      CHECK(r.applied.empty() && r.errors.size() == 1 && r.errors[0].find("too many costumes") != std::string::npos);
      CHECK(g.written.empty());
    }
  }
}

// ---- Fix round 1 (F1): the empty-slot rule of the load-request guard ----

static void TestModelSet() {
  ModelSet s;
  CHECK(!s.contains(0) && !s.contains(255) && s.size() == 0);
  for (uint32_t m : {0u, 31u, 32u, 255u}) s.insert(m);
  s.insert(256);   // past the set: ignored (not wrapped onto bit 0 or 1)
  s.insert(1000);
  CHECK(s.contains(0) && s.contains(31) && s.contains(32) && s.contains(255));
  CHECK(!s.contains(1) && !s.contains(30) && !s.contains(33) && !s.contains(254));
  CHECK(!s.contains(256) && !s.contains(1000) && !s.contains(0xFFFFFFFFu));
  CHECK(s.size() == 4);
  s.insert(32);  // again: still four
  CHECK(s.size() == 4);
  ModelSet t;
  t.insert(256);
  t.insert(257);
  CHECK(!t.contains(0) && !t.contains(1) && t.size() == 0);
}

static void TestGuardEmptySlot() {
  GuardState s;
  s.records[5] = 32;
  s.empty_slot[5].insert(7);
  s.empty_slot[5].insert(31);
  CHECK(GuardModel(s, 5, 7, 4) == 4);   // an empty slot: her first listed model
  CHECK(GuardModel(s, 5, 31, 4) == 4);  // the last record, marked
  CHECK(GuardModel(s, 5, 6, 4) == 6);   // the neighbours of a marked model: unchanged
  CHECK(GuardModel(s, 5, 8, 4) == 8);
  CHECK(GuardModel(s, 5, 30, 4) == 30);
  CHECK(GuardModel(s, 5, 32, 4) == 4);  // the count rule, unchanged
  CHECK(GuardModel(s, 5, 254, 4) == 4);
  s.empty_slot[5].insert(255);
  CHECK(GuardModel(s, 5, 255, 4) == 255);  // 255 passes even when marked: "none" to the loader
  // Another character's marks change nothing for her, and hers nothing for them.
  s.records[6] = 32;
  s.empty_slot[6].insert(9);
  CHECK(GuardModel(s, 5, 9, 4) == 9);
  CHECK(GuardModel(s, 6, 9, 4) == 4);
  CHECK(GuardModel(s, 6, 7, 4) == 7);
  // An unknown record count (0) does not switch the empty-slot rule off; a model past the set is
  // read as not marked (no read past the set).
  s.empty_slot[24].insert(3);
  CHECK(GuardModel(s, 24, 3, 4) == 4);
  CHECK(GuardModel(s, 24, 200, 4) == 200);
  CHECK(GuardModel(s, 24, 300, 4) == 300);
  CHECK(GuardModel(s, 25, 7, 4) == 7);  // 25 is not a character
  // The rule that fired: first match wins, so a marked model past her records is the count rule.
  CHECK(RuleFor(s, 5, 7) == GuardRule::kEmptySlot);
  CHECK(RuleFor(s, 5, 32) == GuardRule::kPastRecords);
  CHECK(RuleFor(s, 5, 8) == GuardRule::kNone);
  CHECK(RuleFor(s, 5, 255) == GuardRule::kNone);
  s.empty_slot[5].insert(40);
  CHECK(RuleFor(s, 5, 40) == GuardRule::kPastRecords);
}

static void SetBe32(const FakeGuest& g, uint32_t a, uint32_t v) {
  for (int i = 0; i < 4; ++i) g.guest.Write(a + uint32_t(i), uint8_t(v >> (24 - 8 * i)));
}

static void TestDecideLoad() {
  FakeGuest g;
  // Kasumi's and Ayane's lists start with models 9 and 13; the image's own first models are
  // different, so a read of the wrong list, entry or character shows.
  const uint32_t kasumi_list = 0x40000000, ayane_list = 0x40000100;
  const uint8_t kasumi_bytes[] = {9, 4, 6}, ayane_bytes[] = {13, 2, 3};
  for (size_t i = 0; i < 3; ++i) {
    g.guest.Write(kasumi_list + uint32_t(i), kasumi_bytes[i]);
    g.guest.Write(ayane_list + uint32_t(i), ayane_bytes[i]);
  }
  SetBe32(g, kListTable + 4 * 5, kasumi_list);
  SetBe32(g, kListTable + 4 * 13, ayane_list);
  GuardState s;
  s.records[5] = 32;
  s.records[13] = 6;
  s.empty_slot[5].insert(7);
  s.empty_slot[13].insert(4);

  LoadDecision d = DecideLoad(&s, g.guest, 5, 7);  // an empty slot: Kasumi's first listed model
  CHECK(d.character == 5 && d.asked == 7 && d.use == 9 && d.rule == GuardRule::kEmptySlot);
  d = DecideLoad(&s, g.guest, 0x105, 0xAB07);  // r4 and r5 carry more than the byte
  CHECK(d.character == 5 && d.asked == 7 && d.use == 9 && d.rule == GuardRule::kEmptySlot);
  d = DecideLoad(&s, g.guest, 5, 40);  // past her 32 records
  CHECK(d.character == 5 && d.asked == 40 && d.use == 9 && d.rule == GuardRule::kPastRecords);
  d = DecideLoad(&s, g.guest, 0x205, 0x1028);  // the same, with garbage above the byte
  CHECK(d.character == 5 && d.asked == 40 && d.use == 9 && d.rule == GuardRule::kPastRecords);
  d = DecideLoad(&s, g.guest, 13, 4);  // her own list, not Kasumi's
  CHECK(d.character == 13 && d.asked == 4 && d.use == 13 && d.rule == GuardRule::kEmptySlot);
  d = DecideLoad(&s, g.guest, 13, 7);  // 7 is marked for Kasumi only; Ayane has 6 records
  CHECK(d.character == 13 && d.asked == 7 && d.use == 13 && d.rule == GuardRule::kPastRecords);
  d = DecideLoad(&s, g.guest, 5, 8);  // an unmarked model inside her records: nothing changes
  CHECK(d.character == 5 && d.asked == 8 && d.use == 8 && d.rule == GuardRule::kNone);
  d = DecideLoad(&s, g.guest, 5, 255);  // "none" passes
  CHECK(d.asked == 255 && d.use == 255 && d.rule == GuardRule::kNone);
  d = DecideLoad(&s, g.guest, 25, 7);  // not a character
  CHECK(d.character == 25 && d.use == 7 && d.rule == GuardRule::kNone);
  d = DecideLoad(&s, g.guest, 0x1FF, 40);  // 0xFF is not a character either
  CHECK(d.character == 0xFF && d.use == 40 && d.rule == GuardRule::kNone);
  d = DecideLoad(nullptr, g.guest, 5, 7);  // no guard installed yet
  CHECK(d.character == 5 && d.asked == 7 && d.use == 7 && d.rule == GuardRule::kNone);

  // The list table is read only when a rule fires (a list pointer the guard never needs is never
  // dereferenced), and then it is read.
  int list_reads = 0;
  const doa4::GuestBytes counting{[&](uint32_t a) {
                                    if (a >= kListTable && a < kListTable + 4 * kCharCount) ++list_reads;
                                    if (a >= kasumi_list && a < ayane_list + 3) ++list_reads;
                                    return g.Get(a);
                                  },
                                  [&](uint32_t, uint8_t) {}};
  d = DecideLoad(&s, counting, 5, 8);
  CHECK(d.use == 8 && list_reads == 0);
  d = DecideLoad(&s, counting, 5, 255);
  CHECK(d.use == 255 && list_reads == 0);
  d = DecideLoad(&s, counting, 25, 7);
  CHECK(d.use == 7 && list_reads == 0);
  d = DecideLoad(&s, counting, 5, 7);
  CHECK(d.use == 9 && list_reads > 0);
}

// A mixed session: the game's load slots keep the model it asked for, on both PCs; only the loader's tick reads the files' model.
static void TestMixedSlotKey() {
  FakeGuest g;
  const uint32_t kasumi_list = 0x40000000;
  g.guest.Write(kasumi_list, 9);
  SetBe32(g, kListTable + 4 * 5, kasumi_list);
  GuardState s;
  s.records[5] = 32;
  s.empty_slot[5].insert(7);

  CHECK(kLoadSlots == 0x830ADBEC && kLoadSlotBytes == 44);  // read from sub_823FE870 and a dump: rows at base 0x830ADBE8 + 4, stride 44

  // The request: only an empty slot in a mixed session keeps what was asked.
  const LoadDecision empty = DecideLoad(&s, g.guest, 5, 7), past = DecideLoad(&s, g.guest, 5, 40), plain = DecideLoad(&s, g.guest, 5, 8);
  CHECK(RequestModel(empty, true) == 7);
  CHECK(RequestModel(empty, false) == 9);  // kNone, kSame, offline: today's rewrite
  CHECK(RequestModel(past, true) == 9);    // past her records is not a matter of who has which file: it stays rewritten
  CHECK(RequestModel(past, false) == 9);
  CHECK(RequestModel(plain, true) == 8);
  CHECK(RequestModel(plain, false) == 8);

  // A row: slot 2, loading (5, 7). Its neighbours must stay as they are.
  const uint32_t row = kLoadSlots + 2 * kLoadSlotBytes;
  const auto set_row = [&](uint8_t status, uint8_t character, uint8_t model, uint8_t c2, uint8_t m2) {
    g.guest.Write(row, status);
    g.guest.Write(row + 3, character);
    g.guest.Write(row + 4, model);
    g.guest.Write(row + 5, c2);
    g.guest.Write(row + 6, m2);
  };
  const uint32_t neighbour_a = kLoadSlots + 1 * kLoadSlotBytes + 4, neighbour_b = kLoadSlots + 3 * kLoadSlotBytes + 4;
  g.guest.Write(neighbour_a, 7);
  g.guest.Write(neighbour_b, 7);

  set_row(1, 5, 7, 255, 255);
  LoaderTick t = BeginLoaderTick(&s, g.guest, 2, true);
  CHECK(t.swapped && g.Get(row + 4) == 9);  // the tick reads the model whose files this PC has
  CHECK(g.Get(neighbour_a) == 7 && g.Get(neighbour_b) == 7);
  EndLoaderTick(t, g.guest, 2);
  CHECK(g.Get(row + 4) == 7);               // the row reads what was asked again
  CHECK(g.Get(row + 6) == 255);             // the tick did not finish: nothing to correct

  set_row(1, 5, 7, 255, 255);
  t = BeginLoaderTick(&s, g.guest, 2, true);
  g.guest.Write(row, 255);  // the tick finished: sub_823FE150 copies +3, +4 to +5, +6 and goes idle
  g.guest.Write(row + 5, g.Get(row + 3));
  g.guest.Write(row + 6, g.Get(row + 4));
  EndLoaderTick(t, g.guest, 2);
  CHECK(g.Get(row + 4) == 7 && g.Get(row + 5) == 5 && g.Get(row + 6) == 7);  // the game's matcher now finds (5, 7) here, as on the PC with the suit

  set_row(1, 5, 7, 255, 255);
  t = BeginLoaderTick(&s, g.guest, 2, true);
  g.guest.Write(row + 3, 255);  // the tick reset the row (sub_823FEB30): nothing to put back
  g.guest.Write(row + 4, 255);
  EndLoaderTick(t, g.guest, 2);
  CHECK(g.Get(row + 4) == 255);

  set_row(1, 5, 7, 4, 9);  // +5, +6 hold another fighter's (4, 9): the word 9 is not this row's to correct
  t = BeginLoaderTick(&s, g.guest, 2, true);
  EndLoaderTick(t, g.guest, 2);
  CHECK(g.Get(row + 5) == 4 && g.Get(row + 6) == 9);

  set_row(1, 5, 7, 5, 4);  // the same fighter's other model still loaded: +6 says what is there, not what this tick is after
  t = BeginLoaderTick(&s, g.guest, 2, true);
  EndLoaderTick(t, g.guest, 2);
  CHECK(g.Get(row + 5) == 5 && g.Get(row + 6) == 4);

  // Task 7c, fix 3: a cached row (5, the real file model 9), asked again for the suit 7. The swap puts 9 in +4; the tick takes the "same files"
  // shortcut (sub_823FE150, loc_823FE240: +0 becomes 11, +5 and +6 are not touched), so End rewrites +6 to 7 though the tick did not write it. That is
  // safe: the matcher sub_823FE9D0 reads +5/+6 only when +0 is 255, and the next tick (status 11, loc_823FE83C) copies +3/+4 into +5/+6 and goes idle,
  // where End puts +4 and +6 back to 7 whichever value the swap left in them, so both PCs' idle rows end (5, 7).
  set_row(0, 5, 7, 5, 9);
  t = BeginLoaderTick(&s, g.guest, 2, true);
  CHECK(t.swapped && g.Get(row + 4) == 9);
  g.guest.Write(row, 11);  // the shortcut
  EndLoaderTick(t, g.guest, 2);
  CHECK(g.Get(row) == 11 && g.Get(row + 4) == 7 && g.Get(row + 5) == 5 && g.Get(row + 6) == 7);  // +6 rewritten, +0 still not idle
  t = BeginLoaderTick(&s, g.guest, 2, true);  // the next tick: status 11 is not idle, so the swap happens again
  CHECK(t.swapped && g.Get(row + 4) == 9);
  g.guest.Write(row + 5, g.Get(row + 3));  // loc_823FE83C
  g.guest.Write(row + 6, g.Get(row + 4));
  g.guest.Write(row, 255);
  EndLoaderTick(t, g.guest, 2);
  CHECK(g.Get(row) == 255 && g.Get(row + 4) == 7 && g.Get(row + 5) == 5 && g.Get(row + 6) == 7);  // idle (5, 7), as on the PC with the suit

  // Task 7c, fix 1: the game has four rows (kLoadSlotCount). A slot past them is no row: untouched, not swapped, whatever the session says. An
  // empty-slot row is planted where slot 4 (and 5, and 255) would be, so that a missing bound shows as a swap.
  CHECK(kLoadSlotCount == 4u);  // sub_823FBE18 ticks slots 0 to 3, and the table has 4 rows: the literals below do not follow the constant
  for (const uint32_t past : {4u, 5u, 255u}) {
    const uint32_t r = kLoadSlots + past * kLoadSlotBytes;
    g.guest.Write(r, 1);
    g.guest.Write(r + 3, 5);
    g.guest.Write(r + 4, 7);
    g.guest.Write(r + 5, 5);
    g.guest.Write(r + 6, 9);
    t = BeginLoaderTick(&s, g.guest, past, true);
    CHECK(!t.swapped && g.Get(r + 4) == 7);
    EndLoaderTick(t, g.guest, past);
    CHECK(g.Get(r + 4) == 7 && g.Get(r + 6) == 9);
    // the same row asked about as slot 3 would swap: the row is what makes the difference, so the planted row is a real empty-slot row
    const uint32_t r3 = kLoadSlots + 3 * kLoadSlotBytes;
    g.guest.Write(r3, 1);
    g.guest.Write(r3 + 3, 5);
    g.guest.Write(r3 + 4, 7);
    t = BeginLoaderTick(&s, g.guest, 3, true);
    CHECK(t.swapped && g.Get(r3 + 4) == 9);
    EndLoaderTick(t, g.guest, 3);
    CHECK(g.Get(r3 + 4) == 7);
  }

  // Not swapped: every other session, an idle row, a model this PC has, a model past the records, no guard.
  const struct { uint8_t status, model; bool mixed; const GuardState* state; } quiet[] = {
      {1, 7, false, &s}, {255, 7, true, &s}, {1, 8, true, &s}, {1, 40, true, &s}, {1, 7, true, nullptr}};
  for (const auto& q : quiet) {
    set_row(q.status, 5, q.model, 255, 255);
    t = BeginLoaderTick(q.state, g.guest, 2, q.mixed);
    CHECK(!t.swapped && g.Get(row + 4) == q.model);
    g.guest.Write(row + 6, q.model);  // whatever the tick does
    EndLoaderTick(t, g.guest, 2);
    CHECK(g.Get(row + 4) == q.model && g.Get(row + 6) == q.model);
  }
}

static void TestGuardModelEdge() {
  // GuardState has no padding after `records`: the planted neighbour below sits at records[25].
  static_assert(sizeof(GuardState) == sizeof(uint32_t) * kCharCount * 9);
  struct {
    GuardState s;
    uint32_t after;  // sits right after records[24]: a read of records[25] would see this 1
  } w{{}, 1};
  w.s.records[5] = 32;
  CHECK(GuardModel(w.s, 25, 200, 4) == 200);  // 25 is not a character
  CHECK(GuardModel(w.s, 5, 31, 0) == 31);
}

// ---- Task 3: the select screen offers every suit with no change to the save ----

// VisibleCount(save_count, list_length, has_suits). The wrong versions: "always the list length" (shows 5 for a girl
// with no suits on an import that has other girls' suits) and "the save's count" (hides the suits).
static void TestVisibleCount() {
  CHECK(VisibleCount(2, 8, true) == 8);
  CHECK(VisibleCount(2, 8, false) == 2);
  CHECK(VisibleCount(8, 8, true) == 8);
  CHECK(VisibleCount(2, 5, false) == 2);  // NOT 5: a girl without suits keeps her count
  CHECK(VisibleCount(9, 8, true) == 9);   // never below the count
  CHECK(VisibleCount(0, 8, true) == 8);
  CHECK(VisibleCount(255, 8, false) == 255);
}

static const SuitCounts& NoSuits() {
  static const SuitCounts none{};
  return none;
}

// A fresh save's copy: 2 for every girl (the profile's starting counts), one row per player.
static void FillRows(FakeGuest& g, uint8_t count) {
  for (uint32_t p = 0; p < kCountCopyPlayers; ++p) {
    for (uint32_t c = 0; c < kCharCount; ++c) g.bytes[kCountCopy + p * kCountCopyRow + c] = count;
  }
  g.written.clear();
}

static void TestCountSuits() {
  FakeGuest g;
  std::vector<Suit> suits = KokoroSuits();
  for (const Suit& s : KasumiSuits()) suits.push_back(s);
  const ApplyResult r = Apply(g.guest, g.alloc, suits);
  CHECK(r.applied.size() == 8);
  const SuitCounts n = CountSuits(r.applied);
  CHECK(n[10] == 4 && n[5] == 4);
  uint32_t others = 0;
  for (uint32_t c = 0; c < kCharCount; ++c) others += (c == 10 || c == 5) ? 0u : n[c];
  CHECK(others == 0);
  CHECK(CountSuits({}) == NoSuits());
}

// The copy: a fresh save (2 each) with Kokoro's and Kasumi's suits imported. Both girls show all their costumes in every
// player's row; Ayane (charId 13, in the fixture with a normal maximum, no suit here) stays at her 2.
static void TestRaiseCountRow() {
  FakeGuest g;
  std::vector<Suit> suits = KokoroSuits();
  for (const Suit& s : KasumiSuits()) suits.push_back(s);
  const SuitCounts counts = CountSuits(Apply(g.guest, g.alloc, suits).applied);
  FillRows(g, 2);
  CHECK(g.Get(kMaxTable + 5) == 11 && g.Get(kMaxTable + 10) == 11 && g.Get(kMaxTable + 13) == 6);  // preconditions
  const uint32_t row = kCountCopy + 1 * kCountCopyRow;  // player 1
  const std::vector<Raised> raised = RaiseCountRow(g.guest, row, counts);
  CHECK(raised.size() == 2);  // exactly the two girls with suits, in charId order
  if (raised.size() == 2) {
    CHECK(raised[0].character == 5 && raised[0].save == 2 && raised[0].shows == 11 && raised[0].of == 11);
    CHECK(raised[1].character == 10 && raised[1].save == 2 && raised[1].shows == 11);
  }
  CHECK(g.Get(row + 5) == 11 && g.Get(row + 10) == 11);
  CHECK(g.Get(row + 13) == 2);  // a girl without suits: her count, NOT her 6
  CHECK(g.Get(row + 0) == 2 && g.Get(row + 24) == 2);
  // Only that row, and only those two bytes: another player's row and every byte around are as they were.
  CHECK(g.written.size() == 2 && g.written.count(row + 5) == 1 && g.written.count(row + 10) == 1);
  CHECK(g.Get(kCountCopy + 5) == 2 && g.Get(kCountCopy + 2 * kCountCopyRow + 10) == 2);
  // Nothing more to raise the second time: no write at all (the writers run every refresh).
  g.written.clear();
  CHECK(RaiseCountRow(g.guest, row, counts).empty());
  CHECK(g.written.empty());
  // A save that already earned more than the list (a count at or above the maximum) is left alone.
  g.bytes[row + 5] = 11;
  g.bytes[row + 10] = 12;
  CHECK(RaiseCountRow(g.guest, row, counts).empty());
  CHECK(g.Get(row + 10) == 12);
  // A row the game did not fill (0): not made up. A girl the writer filled with 0 stays 0 as well.
  FakeGuest unfilled;
  std::vector<Suit> some = KokoroSuits();
  const SuitCounts some_counts = CountSuits(Apply(unfilled.guest, unfilled.alloc, some).applied);
  FillRows(unfilled, 0);
  CHECK(RaiseCountRow(unfilled.guest, kCountCopy, some_counts).empty());
  CHECK(unfilled.written.empty() && unfilled.Get(kCountCopy + 10) == 0);
  // No suits anywhere: nothing is raised whatever the maxima are (the import without a girl's suits, or none).
  FakeGuest none;
  FillRows(none, 2);
  CHECK(RaiseCountRow(none.guest, kCountCopy, NoSuits()).empty());
  CHECK(none.written.empty());
}

// A box on a girl with suits: the maximum follows the box live, so the copy follows it too.
static void TestRaiseCountRowFollowsBox() {
  namespace tw = doa4::tweaks;
  FakeGuest g;
  tw::TweakPatcher patcher;
  AllBoxesOn(g, patcher);
  const ApplyResult r = Apply(g.guest, g.alloc, KokoroSuits());
  CarrySuits(g, patcher, r);
  const SuitCounts counts = CountSuits(r.applied);
  FillRows(g, 2);
  CHECK(RaiseCountRow(g.guest, kCountCopy, counts).size() == 1);
  CHECK(g.Get(kCountCopy + 10) == 13);  // the box's 9 and her 4 suits
  CHECK(patcher.Apply(g.guest, tw::kKokoroKimonos, false, AnyAddress).empty());
  FillRows(g, 2);
  CHECK(RaiseCountRow(g.guest, kCountCopy, counts).size() == 1);
  CHECK(g.Get(kCountCopy + 10) == 11);  // the box off: 7 and 4
}

// ---- doa4_test_image.h itself: always run, with no image ----

// A file of 0x300 bytes of a known pattern standing for an image: guest address 0x82000000 + n is file offset n.
static std::filesystem::path WriteFakeImage(std::vector<uint8_t>* bytes) {
  // One name per process: a second run at the same time must not delete this one's file.
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("doa4_test_image_reader_check_" + std::to_string(unsigned(_getpid())) + ".bin");
  bytes->resize(0x300);
  for (size_t i = 0; i < bytes->size(); ++i) (*bytes)[i] = uint8_t(i * 7 + 3);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes->data()), std::streamsize(bytes->size()));
  return path;
}

static void SetImageVariable(const std::filesystem::path* value) {
#ifdef _WIN32
  _wputenv_s(L"DOA4_TEST_IMAGE", value ? value->c_str() : L"");
#endif
}

static void TestImageReader() {
  using namespace doa4_test;
  // SHA-1 known answers (FIPS 180-1 examples and the empty message).
  CHECK(Sha1Hex(nullptr, 0) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  const std::string abc = "abc";
  CHECK(Sha1Hex(reinterpret_cast<const uint8_t*>(abc.data()), abc.size()) == "a9993e364706816aba3e25717850c26c9cd0d89d");
  const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK(Sha1Hex(reinterpret_cast<const uint8_t*>(two_blocks.data()), two_blocks.size()) ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  // The padding boundaries: 55 bytes fill one block with the length, 56 and 63 need a second block, 64 is a whole block.
  for (const auto& [length, want] : {std::pair<size_t, const char*>{55, "c1c8bbdc22796e28c0e15163d20899b65621d65a"},
                                      {56, "c2db330f6083854c99d4b5bfb6e8f29f201be699"},
                                      {63, "03f09f5b158a7a8cdad920bddc29b81c18a551f5"},
                                      {64, "0098ba824b5c16427bd7a1122a5a442a25ec644d"}}) {
    const std::vector<uint8_t> a(length, uint8_t('a'));
    CHECK(Sha1Hex(a.data(), a.size()) == want);
  }
  const std::vector<uint8_t> million(1000000, uint8_t('a'));
  CHECK(Sha1Hex(million.data(), million.size()) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

  std::vector<uint8_t> file;
  const std::filesystem::path path = WriteFakeImage(&file);
  const std::string name = PathText(path);
  const auto pin = [](const std::vector<uint8_t>& v) { return Sha1Hex(v.data(), v.size()); };
  const std::vector<uint8_t> plain(file.begin() + 0x40, file.begin() + 0x50);
  const std::string plain_pin = pin(plain);
  std::vector<uint8_t> strided;
  for (int k = 0; k < 5; ++k) strided.push_back(file[size_t(0x10 + 8 * k)]);
  const std::string strided_pin = pin(strided);

  std::string why;
  // The right bytes at the right offset; a stride takes every stride-th byte.
  CHECK(ReadRangeFromFile(path, ImageRange{0x82000040, 16, plain_pin.c_str()}, &why) == plain);
  CHECK(ReadRangeFromFile(path, ImageRange{0x82000010, 5, strided_pin.c_str(), 8}, &why) == strided);
  // Wrong pin: refused, and the reason names the file, the address, what was there and what was expected.
  why.clear();
  CHECK(!ReadRangeFromFile(path, ImageRange{0x82000041, 16, plain_pin.c_str()}, &why));
  CHECK(why.rfind(name + ": 0x82000041 has SHA-1 ", 0) == 0 && why.find(", expected " + plain_pin) != std::string::npos);
  why.clear();
  CHECK(!ReadRangeFromFile(path, ImageRange{0x82000010, 5, strided_pin.c_str()}, &why));  // the stride matters
  CHECK(why.find("has SHA-1 ") != std::string::npos);
  // Unreadable: no such file, and a range past the end of the file.
  why.clear();
  CHECK(!ReadRangeFromFile(path.string() + ".missing", ImageRange{0x82000040, 16, plain_pin.c_str()}, &why));
  CHECK(why == name + ".missing: cannot read");
  why.clear();
  CHECK(!ReadRangeFromFile(path, ImageRange{0x820002F8, 16, plain_pin.c_str()}, &why));
  CHECK(why == name + ": cannot read");

  // Through the environment variable (Windows: read as UTF-16).
#ifdef _WIN32
  const std::optional<std::filesystem::path> before = ImagePath();
  SetImageVariable(nullptr);
  why.clear();
  CHECK(!ReadImageRange(ImageRange{0x82000040, 16, plain_pin.c_str()}, &why) && why == "DOA4_TEST_IMAGE unset");
  SetImageVariable(&path);
  CHECK(ReadImageRange(ImageRange{0x82000040, 16, plain_pin.c_str()}, &why) == plain);
  const std::filesystem::path missing = path.wstring() + L".missing";
  SetImageVariable(&missing);
  why.clear();
  CHECK(!ReadImageRange(ImageRange{0x82000040, 16, plain_pin.c_str()}, &why) && why == name + ".missing: cannot read");
  SetImageVariable(before ? &*before : nullptr);
  CHECK(ImagePath() == before);
#endif
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

int main() {
  // The image first (before TestImageReader touches the variable and puts it back).
  std::string why;
  const std::optional<doa4_test::CostumeImage> image = doa4_test::LoadCostumeImage(&why);
  std::string why_max;
  const std::optional<std::vector<uint8_t>> max_costumes =
      image ? doa4_test::ReadImageRange(doa4_test::kMaxCostumesRange, &why_max) : std::nullopt;
  TestImageReader();
  // Checks that never touch the tables.
  TestGuardModel();
  TestGuardModelEdge();
  TestModelSet();
  TestGuardEmptySlot();
  TestVisibleCount();
  if (image && max_costumes) {
    g_image = &*image;
    g_max_costumes = *max_costumes;
    TestFixturesAgree();
    TestRecordCounts();
    TestFileLocation();
    TestCheckSuit();
    TestApplyKasumi();
    TestApplyAyane();
    TestAllOrNothing();
    TestMaxLimit();
    TestFitting();
    TestPicksMakeTheSameTables();
    TestRecordCountsAll();
    TestRecordCountOutsideImage();
    TestFileEdges();
    TestCheckSuitEdges();
    TestHeadPartPair();
    TestAllocFailures();
    TestAllocBlocks();
    TestApplyInPlace();
    TestApplyManyAppends();
    TestApplyRefusals();
    TestDecideLoad();
    TestMixedSlotKey();
    TweaksThenSuitsStayWithinLimit();
    TestBoxToggledAfterSuits();
    TestCountSuits();
    TestRaiseCountRow();
    TestRaiseCountRowFollowsBox();
  } else if (doa4_test::ReportNoImage("costumes: the checks on the image's costume tables", image ? why_max : why)) {
    ++g_checks;
    ++g_failures;
  }
  std::printf("costumes: %d checks run\n", g_checks);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("costumes: all checks passed\n");
  return 0;
}
