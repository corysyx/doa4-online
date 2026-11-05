// Host test for src/doa4_tweaks.cpp, against the Xenia patch file's bytes and
// the image's originals (doa4_tweak_fixtures.h). Built and run by
// run_tweaks_test.bat.

#include "../../src/doa4_tweaks.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "doa4_costume_tweak_fixtures.h"
#include "doa4_tweak_fixtures.h"

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
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

// Guest memory holding the image's original bytes at every patch address.
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
        for (size_t i = 0; i < p.original.size(); ++i) bytes[p.address + uint32_t(i)] = p.original[i];
      }
    }
  }
  bool Holds(const std::vector<fx::FixturePatch>& patches, bool on) {
    for (const auto& p : patches) {
      const auto& want = on ? p.on : p.original;
      for (size_t i = 0; i < want.size(); ++i) {
        if (bytes[p.address + uint32_t(i)] != want[i]) return false;
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
    for (uint32_t c = 0; c < fx::kCostumeTables.size(); ++c) {
      const fx::CostumeTables& t = fx::kCostumeTables[c];
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
    const fx::CostumeTables& t = fx::kCostumeTables[c.character];
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
      CHECK(g.Be32(kCostumeList + 4 * other) == fx::kCostumeTables[other].list_pointer);
      CHECK(g.guest.Read(kCostumeLength + other) == fx::kCostumeTables[other].length);
      CHECK(g.guest.Read(kCostumeMax + other) == fx::kCostumeTables[other].maximum);
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
    const fx::CostumeTables& t = fx::kCostumeTables[c.character];
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

int main() {
  TestCostumeTweaksAddSlots();
  TestCostumeTweaksTogether();
  TestCostumeBlocksAreAllocatedOnceUpFront();
  TestCostumeBlockFailure();
  TestListsMatchThePatchFile();
  TestReadOnlyRanges();
  TestOnThenOffRestoresEveryByte();
  TestNoTweakTouchesReadOnlyData();
  TestOffNeedsOriginals();
  TestCaptureOnlyOnce();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("tweaks: all checks passed\n");
  return 0;
}
