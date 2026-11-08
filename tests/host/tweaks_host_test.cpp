// Host test for src/doa4_tweaks.cpp, against the Xenia patch file's bytes (doa4_tweak_fixtures.h) and the bytes the game
// holds at the patch sites ("originals"): the test's own, each different from its patch byte, always; and with
// DOA4_TEST_IMAGE set (doa4_test_image.h) the real ones too, read from your own image and checked by one SHA-1. The costume tweaks are checked on the game's real costume tables, read
// from your own image (DOA4_TEST_IMAGE, doa4_test_image.h); without it that section prints SKIPPED. Built and run by
// run_tweaks_test.bat.

#include "../../src/doa4_costumes.h"
#include "../../src/doa4_tweaks.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "doa4_test_image.h"
#include "doa4_tweak_fixtures.h"

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

using namespace doa4::tweaks;
namespace fx = doa4_fixtures;

const std::vector<fx::FixturePatch>& FixtureFor(size_t tweak) {
  switch (tweak) {
    case kTagAllStages:
      return fx::kTagAllStages;
    case kMpHiddenCharacters:
      return fx::kMpHiddenCharacters;
    default: {
      static const std::vector<fx::FixturePatch> kNoPatches;  // the costume tweaks: tested through their own tables below
      return kNoPatches;
    }
  }
}

// What the tests that predate the costume tweaks pass as guest memory for their blocks: none is needed by them.
static const AllocFn kNoAlloc = [](uint32_t) { return uint32_t(0); };

// The bytes the game holds at every patch address before any patch: set by UseOriginals.
static std::map<uint32_t, uint8_t> g_original;
static const char* g_originals_name = "synthetic";
static const char kPatchSitesSha1[] = "14f9cc36cf7a8e270c1a7eeeafc3cb5afca4c334";  // the 24 image bytes at the sites below, by address

static std::vector<doa4_test::ImageSite> PatchSites() {
  std::vector<doa4_test::ImageSite> sites;
  for (const auto* patches : {&fx::kTagAllStages, &fx::kMpHiddenCharacters}) {
    for (const auto& p : *patches) sites.push_back({p.address, uint32_t(p.on.size())});
  }
  std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
  return sites;
}
// The test's own originals: every byte differs from the patch byte at its address.
static void UseSyntheticOriginals() {
  g_original.clear();
  g_originals_name = "synthetic";
  for (const auto* patches : {&fx::kTagAllStages, &fx::kMpHiddenCharacters}) {
    for (const auto& p : *patches) {
      for (size_t i = 0; i < p.on.size(); ++i) g_original[p.address + uint32_t(i)] = uint8_t(p.on[i] ^ 0x5A);
    }
  }
}
static void UseImageOriginals(const std::vector<uint8_t>& bytes) {
  g_original.clear();
  g_originals_name = "image";
  size_t at = 0;
  for (const auto& site : PatchSites()) {
    for (uint32_t i = 0; i < site.length; ++i) g_original[site.address + i] = bytes[at++];
  }
}

// Guest memory holding the originals at every patch address.
// Writes to read-only addresses fault (as on the host) unless made writable.
struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  std::set<uint32_t> writable_read_only;  // read-only addresses made writable
  int writes = 0;
  int faults = 0;
  std::vector<std::pair<uint32_t, bool>> protect_calls;
  bool protect_fails = false;

  doa4::GuestBytes guest{[this](uint32_t a) { return bytes[a]; },
                         [this](uint32_t a, uint8_t v) {
                           if (IsReadOnly(a) && !writable_read_only.count(a)) {
                             ++faults;
                             return;
                           }
                           bytes[a] = v;
                           ++writes;
                         }};
  ProtectFn protect = [this](uint32_t address, uint32_t size, bool writable) {
    protect_calls.emplace_back(address, writable);
    if (protect_fails) return false;
    for (uint32_t a = address; a < address + size; ++a) {
      if (writable) {
        writable_read_only.insert(a);
      } else {
        writable_read_only.erase(a);
      }
    }
    return true;
  };

  FakeGuest() {
    for (size_t t = 0; t < AllTweaks().size(); ++t) {
      for (const auto& p : FixtureFor(t)) {
        for (size_t i = 0; i < p.on.size(); ++i) bytes[p.address + uint32_t(i)] = g_original.at(p.address + uint32_t(i));
      }
    }
  }
  bool Holds(const std::vector<fx::FixturePatch>& patches, bool on) {
    for (const auto& p : patches) {
      for (size_t i = 0; i < p.on.size(); ++i) {
        const uint8_t want = on ? p.on[i] : g_original.at(p.address + uint32_t(i));
        if (bytes[p.address + uint32_t(i)] != want) return false;
      }
    }
    return true;
  }
};

static void TestListsMatchThePatchFile() {
  for (size_t t = 0; t < AllTweaks().size(); ++t) {
    std::vector<Patch> mine = AllTweaks()[t].patches;
    std::sort(mine.begin(), mine.end(),
              [](const Patch& a, const Patch& b) { return a.address < b.address; });
    const auto& file = FixtureFor(t);
    CHECK(mine.size() == file.size());
    for (size_t i = 0; i < std::min(mine.size(), file.size()); ++i) {
      CHECK(mine[i].address == file[i].address);
      CHECK(mine[i].bytes == file[i].on);
    }
  }
  CHECK(std::string(AllTweaks()[kTagAllStages].cvar) == "doa4_tag_all_stages");
  CHECK(std::string(AllTweaks()[kMpHiddenCharacters].cvar) == "doa4_mp_hidden_characters");
  // Alpha-152 was removed (2026-10-28): her character data lacks the motions
  // and script entries a player-controlled fighter needs, so selecting her
  // crashed in every mode tested. The two table tweaks and the three costume tweaks remain.
  CHECK(AllTweaks().size() == 5);
  for (const auto& tweak : AllTweaks()) CHECK(std::string(tweak.cvar) != "doa4_alpha152");
}

static void TestReadOnlyRanges() {
  CHECK(IsReadOnly(0x821B52A7));  // an address in READONLY_DATA
  CHECK(IsReadOnly(0x82000000));
  CHECK(IsReadOnly(0x822EFFFF));
  CHECK(!IsReadOnly(0x822F0000));  // CODE starts: never patched here
  CHECK(!IsReadOnly(0x82B70000));  // DATA
  CHECK(!IsReadOnly(0x82C346D0));
  CHECK(!IsReadOnly(0x82F5F2A0));
  CHECK(!IsReadOnly(0x835AFFFF));
  CHECK(IsReadOnly(0x835B0000));
  CHECK(IsReadOnly(0x835FFFFF));
  CHECK(!IsReadOnly(0x83600000));
}

static void TestOnThenOffRestoresEveryByte() {
  for (size_t t = 0; t <= kMpHiddenCharacters; ++t) {
    FakeGuest g;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, kNoAlloc);
    CHECK(!patcher.IsOn(g.guest, t));
    CHECK(patcher.Apply(g.guest, t, true, g.protect).empty());
    CHECK(g.Holds(FixtureFor(t), true));
    CHECK(patcher.IsOn(g.guest, t));
    CHECK(patcher.Apply(g.guest, t, false, g.protect).empty());
    CHECK(g.Holds(FixtureFor(t), false));
    CHECK(!patcher.IsOn(g.guest, t));
    CHECK(g.faults == 0);
    CHECK(g.writable_read_only.empty());  // protection restored every time
    // The other tweaks' bytes were never touched.
    for (size_t other = 0; other < AllTweaks().size(); ++other) {
      if (other != t) CHECK(g.Holds(FixtureFor(other), false));
    }
  }
}

// The remaining tweaks patch DATA only: none needs a read-only page made
// writable. (The read-only path stays in TweakPatcher for future tweaks; its
// only user and its tests went with Alpha-152.)
static void TestNoTweakTouchesReadOnlyData() {
  for (size_t t = 0; t < AllTweaks().size(); ++t) {
    for (const auto& p : AllTweaks()[t].patches) {
      for (size_t i = 0; i < p.bytes.size(); ++i) CHECK(!IsReadOnly(p.address + uint32_t(i)));
    }
  }
  for (const uint32_t table : {kCostumeList, kCostumeVariants, kCostumeLength, kCostumeMax}) CHECK(!IsReadOnly(table));
  for (size_t t = 0; t <= kMpHiddenCharacters; ++t) {
    FakeGuest g;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, kNoAlloc);
    CHECK(patcher.Apply(g.guest, t, true, g.protect).empty());
    CHECK(g.protect_calls.empty());
  }
}

static void TestOffNeedsOriginals() {
  FakeGuest g;
  TweakPatcher patcher;
  CHECK(!patcher.captured());
  CHECK(!patcher.Apply(g.guest, kTagAllStages, false, g.protect).empty());
  CHECK(!patcher.Apply(g.guest, kTagAllStages, true, g.protect).empty());
  CHECK(g.writes == 0);
  patcher.CaptureOriginals(g.guest, kNoAlloc);
  CHECK(patcher.captured());
  CHECK(!patcher.Apply(g.guest, AllTweaks().size(), true, g.protect).empty());  // no such tweak
}

// A second capture must not replace the originals with patched bytes.
static void TestCaptureOnlyOnce() {
  FakeGuest g;
  TweakPatcher patcher;
  patcher.CaptureOriginals(g.guest, kNoAlloc);
  CHECK(patcher.Apply(g.guest, kTagAllStages, true, g.protect).empty());
  patcher.CaptureOriginals(g.guest, kNoAlloc);
  CHECK(patcher.Apply(g.guest, kTagAllStages, false, g.protect).empty());
  CHECK(g.Holds(fx::kTagAllStages, false));
}

// ---- the costume tweaks ----

// One character's costume tables as the image holds them, by charId.
struct CostumeTables {
  uint32_t list_pointer, variants_pointer;
  uint8_t length, maximum;
  uint32_t records;  // model records up to the next character's block (0 = the last block: unknown)
  std::vector<uint8_t> list, variants;
};
static std::vector<CostumeTables> g_tables;

// The tables of all 25 characters, derived from the two image ranges the costume code reads: the table words, lengths,
// maximums, lists and variants from kCostumeTablesRange, the record blocks from kCostumeRecordsRange (the record count
// is the code the game's loader guard uses, costumes::RecordCount).
static std::vector<CostumeTables> DeriveCostumeTables(const doa4_test::CostumeImage& image) {
  std::map<uint32_t, uint8_t> bytes;
  for (size_t i = 0; i < image.tables.size(); ++i) bytes[doa4_test::kCostumeTablesRange.address + uint32_t(i)] = image.tables[i];
  for (size_t i = 0; i < image.records.size(); ++i) bytes[doa4_test::kCostumeRecordsRange.address + uint32_t(i)] = image.records[i];
  const doa4::GuestBytes guest{[&bytes](uint32_t a) {
                                 const auto it = bytes.find(a);
                                 return it == bytes.end() ? uint8_t(0) : it->second;
                               },
                               [](uint32_t, uint8_t) {}};
  std::vector<CostumeTables> out;
  for (uint32_t c = 0; c < doa4::costumes::kCharCount; ++c) {
    CostumeTables t;
    t.list_pointer = guest.ReadBe32(doa4::costumes::kListTable + 4 * c);
    t.variants_pointer = guest.ReadBe32(doa4::costumes::kVarTable + 4 * c);
    t.length = guest.Read(doa4::costumes::kLenTable + c);
    t.maximum = guest.Read(doa4::costumes::kMaxTable + c);
    t.records = doa4::costumes::RecordCount(guest, c);
    // A list pointer outside the image (the unused charIds) names no list.
    if (t.list_pointer < doa4::costumes::kImageEnd) {
      for (uint32_t i = 0; i < t.length; ++i) t.list.push_back(guest.Read(t.list_pointer + i));
    }
    for (uint32_t i = 0; i < t.maximum; ++i) t.variants.push_back(guest.Read(t.variants_pointer + i));
    out.push_back(std::move(t));
  }
  return out;
}

// The game's costume tables in a map, with exact-size heap blocks: a write outside a block or past its end counts.
struct CostumeGuest {
  std::map<uint32_t, uint8_t> bytes;
  std::map<uint32_t, uint32_t> blocks;  // start -> size
  uint32_t next_block = 0x30000000;
  int allocs = 0;
  int fail_alloc_at = -1;  // this allocation (0-based) returns 0
  int stray_writes = 0;

  static constexpr uint32_t kHeapStart = 0x30000000;
  doa4::GuestBytes guest{[this](uint32_t a) {
                           const auto it = bytes.find(a);
                           return it == bytes.end() ? uint8_t(0xEE) : it->second;  // poison: unset memory is not zero
                         },
                         [this](uint32_t a, uint8_t v) {
                           if (a >= kHeapStart && a < 0x40000000) {
                             bool inside = false;
                             for (const auto& b : blocks) inside = inside || (a >= b.first && a < b.first + b.second);
                             if (!inside) ++stray_writes;
                           }
                           bytes[a] = v;
                         }};
  AllocFn alloc = [this](uint32_t size) -> uint32_t {
    const int index = allocs++;
    if (index == fail_alloc_at) return 0;
    const uint32_t at = next_block;
    blocks[at] = size;
    next_block += size + 0x100;
    return at;
  };

  void SetBe32(uint32_t a, uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes[a + uint32_t(i)] = uint8_t(v >> (24 - 8 * i));
  }
  uint32_t Be32(uint32_t a) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | guest.Read(a + uint32_t(i));
    return v;
  }
  CostumeGuest() {
    for (uint32_t c = 0; c < g_tables.size(); ++c) {
      const CostumeTables& t = g_tables[c];
      SetBe32(kCostumeList + 4 * c, t.list_pointer);
      SetBe32(kCostumeVariants + 4 * c, t.variants_pointer);
      bytes[kCostumeLength + c] = t.length;
      bytes[kCostumeMax + c] = t.maximum;
      for (uint32_t i = 0; i < t.list.size(); ++i) bytes[t.list_pointer + i] = t.list[i];
      for (uint32_t i = 0; i < t.variants.size(); ++i) bytes[t.variants_pointer + i] = t.variants[i];
    }
  }
  // Every table word and byte of every character, as the game's readers see them.
  std::vector<uint8_t> AllTables() {
    std::vector<uint8_t> out;
    for (uint32_t a = kCostumeMax; a < kCostumeMax + 25; ++a) out.push_back(guest.Read(a));
    for (uint32_t a = kCostumeLength; a < kCostumeLength + 25; ++a) out.push_back(guest.Read(a));
    for (uint32_t a = kCostumeList; a < kCostumeList + 100; ++a) out.push_back(guest.Read(a));
    for (uint32_t a = kCostumeVariants; a < kCostumeVariants + 100; ++a) out.push_back(guest.Read(a));
    return out;
  }
};

// The game's lookup sub_82526B78 (variant 0), read from the decompiled code (.19.cpp:39404): sum the earlier costumes'
// variant counts, bounds-check against the list length, read the list; -1 past the end.
static int LookUp(CostumeGuest& g, uint32_t character, uint32_t costume) {
  const uint32_t list = g.Be32(kCostumeList + 4 * character);
  const uint32_t variants = g.Be32(kCostumeVariants + 4 * character);
  const uint32_t length = g.guest.Read(kCostumeLength + character);
  uint32_t at = 0;
  for (uint32_t k = 0; k < costume; ++k) at += g.guest.Read(variants + k);
  return at < length ? int(g.guest.Read(list + at)) : -1;
}

// The game's reverse lookup sub_82526C18: the costume number of the FIRST list position holding the model.
static int Reverse(CostumeGuest& g, uint32_t character, uint8_t model) {
  const uint32_t list = g.Be32(kCostumeList + 4 * character);
  const uint32_t variants = g.Be32(kCostumeVariants + 4 * character);
  const uint32_t maximum = g.guest.Read(kCostumeMax + character);
  uint32_t at = 0;
  for (uint32_t costume = 0; costume < maximum; ++costume) {
    for (uint32_t v = 0; v < g.guest.Read(variants + costume); ++v, ++at) {
      if (g.guest.Read(list + at) == model) return int(costume);
    }
  }
  return -1;
}

struct CostumeCase {
  size_t tweak;
  uint32_t character;
  std::vector<uint8_t> models;  // the Xenia Rev 1 patch file's values (Helena 0x82C34758 = 03; Kokoro 0x82C34768 = 08,
                                // 0x82C34769 = 03; Christie 0x82C347F0 = 05), as extra costumes after her own
  const char* cvar;
};
static const std::vector<CostumeCase> kCostumeCases = {
    {kHelenaOpera, 0x07, {0x03}, "doa4_helena_opera"},
    {kKokoroKimonos, 0x0A, {0x08, 0x03}, "doa4_kokoro_kimonos"},
    {kChristieCostume, 0x14, {0x05}, "doa4_christie_costume"},
};

static void TestCostumeTweaksAddSlots() {
  for (const CostumeCase& c : kCostumeCases) {
    const CostumeTables& t = g_tables[c.character];
    CHECK(c.tweak < AllTweaks().size());
    if (c.tweak >= AllTweaks().size()) continue;
    CHECK(std::string(AllTweaks()[c.tweak].cvar) == c.cvar);
    CostumeGuest g;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    const std::vector<uint8_t> before = g.AllTables();
    CHECK(!patcher.IsOn(g.guest, c.tweak));
    // The fixture is the shape the tweak exists for: not one of these models is in her list yet.
    for (uint8_t m : c.models) CHECK(std::find(t.list.begin(), t.list.end(), m) == t.list.end());

    CHECK(patcher.Apply(g.guest, c.tweak, true, [](uint32_t, uint32_t, bool) { return true; }).empty());
    CHECK(patcher.IsOn(g.guest, c.tweak));
    const uint32_t k = uint32_t(c.models.size());
    CHECK(g.guest.Read(kCostumeLength + c.character) == t.length + k);
    CHECK(g.guest.Read(kCostumeMax + c.character) == t.maximum + k);
    // Her own costumes keep their numbers and models; the new ones follow, in the order given.
    for (uint32_t n = 0; n < t.maximum; ++n) CHECK(LookUp(g, c.character, n) == int(t.list[n]));
    for (uint32_t j = 0; j < k; ++j) {
      CHECK(LookUp(g, c.character, t.maximum + j) == int(c.models[j]));
      CHECK(Reverse(g, c.character, c.models[j]) == int(t.maximum + j));
    }
    CHECK(LookUp(g, c.character, t.maximum + k) == -1);
    // The invariants the game's readers depend on: one variant each, so the variants sum to the length; every model
    // has a record; none twice.
    uint32_t sum = 0;
    const uint32_t variants = g.Be32(kCostumeVariants + 4 * c.character);
    for (uint32_t n = 0; n < t.maximum + k; ++n) sum += g.guest.Read(variants + n);
    CHECK(sum == t.length + k);
    std::set<int> seen;
    for (uint32_t n = 0; n < t.maximum + k; ++n) {
      const int m = LookUp(g, c.character, n);
      CHECK(m >= 0 && uint32_t(m) < t.records);
      CHECK(seen.insert(m).second);
    }
    // No other character's tables moved.
    for (uint32_t other = 0; other < 25; ++other) {
      if (other == c.character) continue;
      CHECK(g.Be32(kCostumeList + 4 * other) == g_tables[other].list_pointer);
      CHECK(g.guest.Read(kCostumeLength + other) == g_tables[other].length);
      CHECK(g.guest.Read(kCostumeMax + other) == g_tables[other].maximum);
    }
    CHECK(g.stray_writes == 0);

    CHECK(patcher.Apply(g.guest, c.tweak, false, [](uint32_t, uint32_t, bool) { return true; }).empty());
    CHECK(!patcher.IsOn(g.guest, c.tweak));
    CHECK(g.AllTables() == before);
    CHECK(LookUp(g, c.character, t.maximum) == -1);
    // On a second time: the same result (the blocks are kept, not allocated again).
    const int allocated = g.allocs;
    CHECK(patcher.Apply(g.guest, c.tweak, true, [](uint32_t, uint32_t, bool) { return true; }).empty());
    CHECK(g.allocs == allocated);
    CHECK(LookUp(g, c.character, t.maximum) == int(c.models[0]));
  }
}

// The game reads these tables on its own thread while the menu changes them. On, the list and variants move first (to
// blocks holding her old costumes too) and the length and maximum grow after; off, the reverse. So a read between two
// writes never indexes past a list. Pointer words go in one store where the guest offers one.
static void TestCostumeTablesChangeInASafeOrder() {
  for (const CostumeCase& c : kCostumeCases) {
    CostumeGuest g;
    std::vector<uint32_t> order;
    std::vector<uint32_t> words;
    doa4::GuestBytes recording = g.guest;
    recording.write = [&](uint32_t a, uint8_t v) {
      order.push_back(a);
      g.guest.Write(a, v);
    };
    recording.write_be32 = [&](uint32_t a, uint32_t v) {
      words.push_back(a);
      order.push_back(a);
      g.SetBe32(a, v);
    };
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    const uint32_t list = kCostumeList + 4 * c.character, variants = kCostumeVariants + 4 * c.character;
    const uint32_t length = kCostumeLength + c.character, maximum = kCostumeMax + c.character;
    const auto at = [&](uint32_t a) { return std::find(order.begin(), order.end(), a) - order.begin(); };
    CHECK(patcher.Apply(recording, c.tweak, true, [](uint32_t, uint32_t, bool) { return true; }).empty());
    CHECK(words.size() == 2);
    CHECK(order.size() == 4);  // two whole words and two bytes: nothing byte by byte
    CHECK(at(list) < at(length) && at(variants) < at(length) && at(list) < at(maximum) && at(variants) < at(maximum));
    order.clear();
    words.clear();
    CHECK(patcher.Apply(recording, c.tweak, false, [](uint32_t, uint32_t, bool) { return true; }).empty());
    CHECK(words.size() == 2);
    CHECK(at(length) < at(list) && at(length) < at(variants) && at(maximum) < at(list) && at(maximum) < at(variants));
    CHECK(g.Be32(list) == g_tables[c.character].list_pointer);
  }
}

// Past her entries a block holds zeros: a reader that walks on finds no more variants or models.
static void TestCostumeBlocksAreZeroPadded() {
  for (const CostumeCase& c : kCostumeCases) {
    const CostumeTables& t = g_tables[c.character];
    CostumeGuest g;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    CHECK(patcher.Apply(g.guest, c.tweak, true, [](uint32_t, uint32_t, bool) { return true; }).empty());
    const uint32_t list = g.Be32(kCostumeList + 4 * c.character);
    const uint32_t variants = g.Be32(kCostumeVariants + 4 * c.character);
    const uint32_t k = uint32_t(c.models.size());
    CHECK(g.blocks.count(list) == 1 && g.blocks.count(variants) == 1);
    for (uint32_t i = t.length + k; i < g.blocks[list]; ++i) CHECK(g.guest.Read(list + i) == 0);
    for (uint32_t i = t.maximum + k; i < g.blocks[variants]; ++i) CHECK(g.guest.Read(variants + i) == 0);
  }
}

// All on together: Kokoro's two and the others' one each, each character on her own blocks.
static void TestCostumeTweaksTogether() {
  CostumeGuest g;
  TweakPatcher patcher;
  patcher.CaptureOriginals(g.guest, g.alloc);
  const std::vector<uint8_t> before = g.AllTables();
  for (const CostumeCase& c : kCostumeCases) {
    CHECK(patcher.Apply(g.guest, c.tweak, true, [](uint32_t, uint32_t, bool) { return true; }).empty());
  }
  for (const CostumeCase& c : kCostumeCases) {
    const CostumeTables& t = g_tables[c.character];
    for (uint32_t j = 0; j < c.models.size(); ++j) CHECK(LookUp(g, c.character, t.maximum + j) == int(c.models[j]));
    for (uint32_t n = 0; n < t.maximum; ++n) CHECK(LookUp(g, c.character, n) == int(t.list[n]));
  }
  // Off in another order than on.
  for (auto it = kCostumeCases.begin(); it != kCostumeCases.end(); ++it) {
    CHECK(patcher.Apply(g.guest, it->tweak, false, [](uint32_t, uint32_t, bool) { return true; }).empty());
  }
  CHECK(g.AllTables() == before);
}

// The blocks exist whether or not a tweak is on: both players' guest heaps look the same for any setting of the boxes.
static void TestCostumeBlocksAreAllocatedOnceUpFront() {
  CostumeGuest g;
  TweakPatcher patcher;
  patcher.CaptureOriginals(g.guest, g.alloc);
  CHECK(g.allocs == 6);  // list + variants for each of the three
  patcher.CaptureOriginals(g.guest, g.alloc);  // a second capture allocates nothing
  CHECK(g.allocs == 6);
}

// No guest memory for one tweak: that tweak refuses with a reason and writes nothing; the others still work.
static void TestCostumeBlockFailure() {
  for (int fail = 0; fail < 6; ++fail) {
    CostumeGuest g;
    g.fail_alloc_at = fail;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, g.alloc);
    CHECK(patcher.captured());
    const size_t failed = kHelenaOpera + size_t(fail) / 2;
    const std::vector<uint8_t> before = g.AllTables();
    for (const bool on : {true, false}) {
      CHECK(patcher.Apply(g.guest, failed, on, [](uint32_t, uint32_t, bool) { return true; }) ==
            "no guest memory for this costume");
    }
    CHECK(g.AllTables() == before);
    CHECK(!patcher.IsOn(g.guest, failed));
    for (const CostumeCase& c : kCostumeCases) {
      if (c.tweak == failed) continue;
      CHECK(patcher.Apply(g.guest, c.tweak, true, [](uint32_t, uint32_t, bool) { return true; }).empty());
      CHECK(patcher.IsOn(g.guest, c.tweak));
    }
  }
}

// The tests that use the patch sites' originals.
static void RunPatchTests() {
  TestListsMatchThePatchFile();
  TestReadOnlyRanges();
  TestOnThenOffRestoresEveryByte();
  TestNoTweakTouchesReadOnlyData();
  TestOffNeedsOriginals();
  TestCaptureOnlyOnce();
}

int main() {
  // Each original differs from its patch byte, or the on/off checks could not tell the two apart.
  UseSyntheticOriginals();
  for (const auto* patches : {&fx::kTagAllStages, &fx::kMpHiddenCharacters}) {
    for (const auto& p : *patches) {
      for (size_t i = 0; i < p.on.size(); ++i) CHECK(g_original.at(p.address + uint32_t(i)) != p.on[i]);
    }
  }
  RunPatchTests();

  std::string why;
  const std::optional<std::vector<uint8_t>> originals = doa4_test::ReadImageSites(PatchSites(), kPatchSitesSha1, &why);
  if (originals) {
    UseImageOriginals(*originals);
    RunPatchTests();
  } else if (doa4_test::ReportNoImage("tweaks: the patch tests on the image's original bytes", why)) {
    ++g_checks;
    ++g_failures;
  }
  why.clear();
  if (const std::optional<doa4_test::CostumeImage> image = doa4_test::LoadCostumeImage(&why)) {
    g_tables = DeriveCostumeTables(*image);
    CHECK(g_tables.size() == 25);
    TestCostumeTweaksAddSlots();
    TestCostumeTablesChangeInASafeOrder();
    TestCostumeBlocksAreZeroPadded();
    TestCostumeTweaksTogether();
    TestCostumeBlocksAreAllocatedOnceUpFront();
    TestCostumeBlockFailure();
  } else if (doa4_test::ReportNoImage("tweaks: the costume tweaks on the image's costume tables", why)) {
    ++g_checks;
    ++g_failures;
  }
  std::printf("tweaks: %d checks run\n", g_checks);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("tweaks: all checks passed\n");
  return 0;
}
