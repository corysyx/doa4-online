// Host test for src/doa4_tweaks.cpp, against the Xenia patch file's bytes (doa4_tweak_fixtures.h) and the bytes the game
// holds at the patch sites ("originals"): the test's own, each different from its patch byte, always; and with
// DOA4_TEST_IMAGE set (doa4_test_image.h) the real ones too, read from your own image and checked by one SHA-1. Alpha-152's
// tweak is two of the patch file's three items plus three select-view values of the port's own (kAlphaViews); the file's
// third item, Random's record, is not patched where the file puts it but written into the 24th cell after the game's boot
// copy (TestAlphaRandomCell). The costume
// tweaks are checked on the game's real costume tables, read from your own image the same way. With the variable unset those
// sections print SKIPPED; set but unreadable or failing a pin, they FAIL. Built and run by run_tweaks_test.bat.

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

// Alpha-152's select view (not in the patch file; notes/research/alpha152_select_model_2026-11-12.md): her row (charId
// 0x10) of three per-character select tables takes Kasumi's values (charId 5), so her panel has Kasumi's camera and
// position. The tables: 0x82C338F0 panel offset, four be16 per character; 0x82C339B8 hover camera id and 0x82C33A50
// confirmed camera id, one be32 per character. The values are Kasumi's rows; the image tier reads them from your image.
inline constexpr uint32_t kAlphaChar = 0x10, kKasumiChar = 0x05;
inline constexpr uint32_t kPanelOffsets = 0x82C338F0, kHoverCameras = 0x82C339B8, kConfirmedCameras = 0x82C33A50;
static const std::vector<fx::FixturePatch> kAlphaViews = {
    {kPanelOffsets + 8 * kAlphaChar, {0xFF, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},  // x -112, y 0 (hover); 0, 0 (confirmed)
    {kHoverCameras + 4 * kAlphaChar, {0x00, 0x00, 0x00, 0xF8}},
    {kConfirmedCameras + 4 * kAlphaChar, {0x00, 0x00, 0x01, 0x0F}},
};
// Alpha-152 in the patch file plus the three view values, by address: the six sites the image tier reads (the pinned 97
// bytes). Five are her tweak's patches (AlphaFixture). The sixth is the patch file's item at kSelectStandIn (XeniaRandom):
// the port does not patch it there, because the select code shows that record in place of every locked character; its 40
// bytes go into the 24th cell after the game's boot copy instead (RandomCellFixture).
static const std::vector<fx::FixturePatch>& AlphaFileAndViews() {
  static const std::vector<fx::FixturePatch> all = [] {
    std::vector<fx::FixturePatch> v = fx::kAlpha152;
    v.insert(v.end(), kAlphaViews.begin(), kAlphaViews.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
    return v;
  }();
  return all;
}
// The patch file's Random record: its one item at kSelectStandIn (main checks there is exactly one).
static const fx::FixturePatch& XeniaRandom() {
  static const fx::FixturePatch item = [] {
    for (const auto& p : fx::kAlpha152) {
      if (p.address == kSelectStandIn) return p;
    }
    return fx::FixturePatch{0, {}};
  }();
  return item;
}
// The whole Alpha-152 tweak, by address: the patch file's two other items and the three view values.
static const std::vector<fx::FixturePatch>& AlphaFixture() {
  static const std::vector<fx::FixturePatch> patched = [] {
    std::vector<fx::FixturePatch> v;
    for (const auto& p : AlphaFileAndViews()) {
      if (p.address != kSelectStandIn) v.push_back(p);
    }
    return v;
  }();
  return patched;
}
// The 24th select cell with Random's record in it: what the step after the boot copy leaves when she was applied at load.
static const std::vector<fx::FixturePatch>& RandomCellFixture() {
  static const std::vector<fx::FixturePatch> cell = {{kSelectLastCell, XeniaRandom().on}};
  return cell;
}
// The stand-in's site alone: Holds(StandInSite(), false) = the bytes the game holds there, untouched.
static const std::vector<fx::FixturePatch>& StandInSite() {
  static const std::vector<fx::FixturePatch> site = {XeniaRandom()};
  return site;
}

const std::vector<fx::FixturePatch>& FixtureFor(size_t tweak) {
  switch (tweak) {
    case kTagAllStages:
      return fx::kTagAllStages;
    case kMpHiddenCharacters:
      return fx::kMpHiddenCharacters;
    case kAlpha152:
      return AlphaFixture();
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
static const char kAlphaSitesSha1[] = "2fab24e0528883efdbcf79316e43b58af8209de1";  // the 97 image bytes at Alpha-152's six sites, by address
static const char kLastCellSha1[] = "b80de5d138758541c5f05265ad144ab9fa86d1db";  // the 24th cell in the image: 40 zero bytes (filled at boot)
// Kasumi's rows of the three select-view tables (16 image bytes, by address): what Alpha-152's rows are set to.
static const char kKasumiViewsSha1[] = "c60f8beec0a72ccdc7a89c8f68e8f6ae965573fb";

static std::vector<doa4_test::ImageSite> SitesOf(std::initializer_list<const std::vector<fx::FixturePatch>*> lists) {
  std::vector<doa4_test::ImageSite> sites;
  for (const auto* patches : lists) {
    for (const auto& p : *patches) sites.push_back({p.address, uint32_t(p.on.size())});
  }
  std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
  return sites;
}
static std::vector<doa4_test::ImageSite> PatchSites() { return SitesOf({&fx::kTagAllStages, &fx::kMpHiddenCharacters}); }
static std::vector<doa4_test::ImageSite> AlphaSites() { return SitesOf({&AlphaFileAndViews()}); }
static std::vector<doa4_test::ImageSite> LastCellSites() { return SitesOf({&RandomCellFixture()}); }
// Kasumi's rows, in kAlphaViews order (which is address order).
static std::vector<doa4_test::ImageSite> KasumiViewSites() {
  return {{kPanelOffsets + 8 * kKasumiChar, 8}, {kHoverCameras + 4 * kKasumiChar, 4}, {kConfirmedCameras + 4 * kKasumiChar, 4}};
}
// The test's own originals: every byte differs from the patch byte at its address.
static void UseSyntheticOriginals() {
  g_original.clear();
  g_originals_name = "synthetic";
  for (const auto* patches : {&fx::kTagAllStages, &fx::kMpHiddenCharacters, &AlphaFileAndViews()}) {
    for (const auto& p : *patches) {
      for (size_t i = 0; i < p.on.size(); ++i) g_original[p.address + uint32_t(i)] = uint8_t(p.on[i] ^ 0x5A);
    }
  }
  // The 24th cell before the boot copy: its own bytes, unlike Random's record and unlike the stand-in's (main checks both).
  for (uint32_t i = 0; i < 40; ++i) g_original[kSelectLastCell + i] = uint8_t(0xC0 + i);
}
// `bytes`: the two table tweaks' sites (PatchSites order); `alpha`: Alpha-152's six sites (AlphaSites order); `cell`: the
// 24th select cell.
static void UseImageOriginals(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& alpha,
                              const std::vector<uint8_t>& cell) {
  g_original.clear();
  g_originals_name = "image";
  size_t at = 0;
  for (const auto& site : PatchSites()) {
    for (uint32_t i = 0; i < site.length; ++i) g_original[site.address + i] = bytes[at++];
  }
  at = 0;
  for (const auto& site : AlphaSites()) {
    for (uint32_t i = 0; i < site.length; ++i) g_original[site.address + i] = alpha[at++];
  }
  for (uint32_t i = 0; i < 40; ++i) g_original[kSelectLastCell + i] = cell[i];
}

// Guest memory holding the originals at every patch address.
// Writes to read-only addresses fault (as on the host) unless made writable.
struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  std::set<uint32_t> writable_read_only;  // read-only addresses made writable
  int writes = 0;
  int faults = 0;
  std::vector<std::pair<uint32_t, bool>> protect_calls;
  bool protect_fails = false;          // every protection change is refused
  bool protect_fails_restore = false;  // only making a page read-only again is refused

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
    if (protect_fails || (protect_fails_restore && !writable)) return false;
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
    // The two sites of Alpha-152's Random cell that no tweak patches: the stand-in record and the 24th cell.
    for (const uint32_t site : {kSelectStandIn, kSelectLastCell}) {
      for (uint32_t i = 0; i < 40; ++i) bytes[site + i] = g_original.at(site + i);
    }
  }
  // The game's boot copy, the static initialiser sub_82A3DCB0 (ten words from kSelectStandIn to kSelectLastCell): the
  // test's model of it. It runs after the load, so after the tweaks were applied and EndLoad was called.
  void BootCopy() {
    for (uint32_t i = 0; i < 40; ++i) bytes[kSelectLastCell + i] = bytes[kSelectStandIn + i];
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
  // Alpha-152 came back on 2026-11-12 as the sixth tweak (removed on 2026-10-28; the crash of that day was the relocation
  // defect fixed on 2026-10-30). The two table tweaks, the three costume tweaks and hers.
  CHECK(AllTweaks().size() == 6);
  CHECK(kAlpha152 == 5);  // appended: the five older tweaks keep their numbers (the menu's test action, the hand-off's name order)
  CHECK(std::string(AllTweaks()[kAlpha152].cvar) == "doa4_alpha152");
  CHECK(std::string(AllTweaks()[kAlpha152].label) == "Alpha-152 playable");
  CHECK(AllTweaks()[kAlpha152].additions.empty());
  CHECK(AllTweaks()[kAlpha152].patches.size() == 5);
  // Hers is the only tweak the game reads at boot: applied at load only.
  for (size_t t = 0; t < AllTweaks().size(); ++t) CHECK(AllTweaks()[t].at_load_only == (t == kAlpha152));
  // Her view rows are the tables' rows of charId 0x10 (derived above from the table addresses, not copied).
  CHECK(kAlphaViews[0].address == 0x82C33970 && kAlphaViews[1].address == 0x82C339F8 && kAlphaViews[2].address == 0x82C33A90);
  // The menu draws every tweak once: the table tweaks and Alpha-152 first, then the costume boxes.
  std::set<size_t> drawn(kMenuOrder.begin(), kMenuOrder.end());
  CHECK(kMenuOrder.size() == AllTweaks().size() && drawn.size() == AllTweaks().size());
  for (const size_t t : kMenuOrder) CHECK(t < AllTweaks().size());
  CHECK(kMenuOrder[0] == kTagAllStages && kMenuOrder[1] == kMpHiddenCharacters && kMenuOrder[2] == kAlpha152);
  CHECK(kMenuOrder[3] == kHelenaOpera && kMenuOrder[4] == kKokoroKimonos && kMenuOrder[5] == kChristieCostume);
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
  for (const size_t t : {kTagAllStages, kMpHiddenCharacters, kAlpha152}) {
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

// Every tweak but Alpha-152 patches DATA only: none of them needs a read-only page made writable. Hers has one read-only
// byte, the 24th select cell's switch at 0x821B52A7.
static void TestOnlyAlphaTouchesReadOnlyData() {
  int read_only_bytes = 0;
  for (size_t t = 0; t < AllTweaks().size(); ++t) {
    for (const auto& p : AllTweaks()[t].patches) {
      for (size_t i = 0; i < p.bytes.size(); ++i) {
        if (t != kAlpha152) CHECK(!IsReadOnly(p.address + uint32_t(i)));
        if (t == kAlpha152 && IsReadOnly(p.address + uint32_t(i))) {
          ++read_only_bytes;
          CHECK(p.address == 0x821B52A7 && p.bytes.size() == 1);
        }
      }
    }
  }
  CHECK(read_only_bytes == 1);
  for (const uint32_t table : {kCostumeList, kCostumeVariants, kCostumeLength, kCostumeMax}) CHECK(!IsReadOnly(table));
  for (size_t t = 0; t <= kMpHiddenCharacters; ++t) {
    FakeGuest g;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, kNoAlloc);
    CHECK(patcher.Apply(g.guest, t, true, g.protect).empty());
    CHECK(g.protect_calls.empty());
  }
}

// Alpha-152 on: the read-only byte is made writable, written, and made read-only again; every one of her five sites then
// holds its patch bytes, and the stand-in record and the 24th cell hold what they held. Off: the same protection round,
// and every site holds what it held before.
static void TestAlphaReadOnlyByteGoesThroughProtect() {
  FakeGuest g;
  TweakPatcher patcher;
  patcher.CaptureOriginals(g.guest, kNoAlloc);
  CHECK(g.Holds(AlphaFixture(), false));
  CHECK(g.bytes[0x821B52A7] != 0x01);  // the precondition: the switch is not on before
  CHECK(patcher.Apply(g.guest, kAlpha152, true, g.protect).empty());
  CHECK(g.protect_calls.size() == 2);
  if (g.protect_calls.size() == 2) {
    CHECK(g.protect_calls[0] == std::make_pair(uint32_t(0x821B52A7), true));
    CHECK(g.protect_calls[1] == std::make_pair(uint32_t(0x821B52A7), false));
  }
  CHECK(g.faults == 0);
  CHECK(g.writable_read_only.empty());
  CHECK(g.bytes[0x821B52A7] == 0x01);
  CHECK(g.writes == 57);  // 1 + 8 + 4 + 4 + 40: every byte of the five sites, once
  CHECK(g.Holds(AlphaFixture(), true));
  CHECK(g.Holds(kAlphaViews, true));  // the view part on its own
  CHECK(g.Holds(StandInSite(), false) && g.Holds(RandomCellFixture(), false));  // not patches: untouched by Apply
  CHECK(patcher.IsOn(g.guest, kAlpha152));
  // One byte of any site missing, and it is not "on" (IsOn compares all five sites).
  for (const auto& p : AlphaFixture()) {
    const uint32_t last = p.address + uint32_t(p.on.size()) - 1;
    const uint8_t keep = g.bytes[last];
    g.bytes[last] = uint8_t(keep ^ 0x5A);
    CHECK(!patcher.IsOn(g.guest, kAlpha152));
    g.bytes[last] = keep;
  }
  CHECK(patcher.IsOn(g.guest, kAlpha152));
  CHECK(patcher.Apply(g.guest, kAlpha152, false, g.protect).empty());
  CHECK(g.protect_calls.size() == 4);
  CHECK(g.faults == 0);
  CHECK(g.writable_read_only.empty());
  CHECK(g.Holds(AlphaFixture(), false));
  CHECK(!patcher.IsOn(g.guest, kAlpha152));
  CHECK(g.Holds(fx::kTagAllStages, false) && g.Holds(fx::kMpHiddenCharacters, false));  // the other tweaks' bytes: never touched
}

// All or nothing: when the read-only byte cannot be made writable, none of the five sites is written, on or off.
static void TestAlphaFailedProtectWritesNothing() {
  {
    FakeGuest g;
    g.protect_fails = true;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, kNoAlloc);
    const std::string error = patcher.Apply(g.guest, kAlpha152, true, g.protect);
    CHECK(error == "could not make 0x821b52a7 writable");
    CHECK(g.protect_calls.size() == 1);  // asked once, refused: nothing to undo
    CHECK(g.writes == 0);
    CHECK(g.faults == 0);
    CHECK(g.Holds(AlphaFixture(), false));
    CHECK(!patcher.IsOn(g.guest, kAlpha152));
  }
  {  // on, then the protection breaks: off writes nothing either, and she stays on in every site
    FakeGuest g;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, kNoAlloc);
    CHECK(patcher.Apply(g.guest, kAlpha152, true, g.protect).empty());
    const int writes = g.writes;
    g.protect_fails = true;
    CHECK(patcher.Apply(g.guest, kAlpha152, false, g.protect) == "could not make 0x821b52a7 writable");
    CHECK(g.writes == writes);
    CHECK(g.faults == 0);
    CHECK(g.Holds(AlphaFixture(), true));
    CHECK(patcher.IsOn(g.guest, kAlpha152));
  }
  {  // the page cannot be made read-only again: the bytes are written (she is on), and the error says so
    FakeGuest g;
    g.protect_fails_restore = true;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, kNoAlloc);
    CHECK(patcher.Apply(g.guest, kAlpha152, true, g.protect) == "wrote, but could not make 0x821b52a7 read-only again");
    CHECK(g.faults == 0);
    CHECK(g.Holds(AlphaFixture(), true));
    CHECK(patcher.IsOn(g.guest, kAlpha152));
  }
}

// The game copies the 24th cell at boot, so her tweak is applied at load or not at all: once the load is over
// (EndLoad) neither on nor off writes a byte, whichever state she was loaded in; the five live tweaks still apply.
static void TestAlphaAppliesOnlyAtLoad() {
  for (const bool loaded_on : {false, true}) {
    FakeGuest g;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, kNoAlloc);
    CHECK(!patcher.load_ended());
    if (loaded_on) CHECK(patcher.Apply(g.guest, kAlpha152, true, g.protect).empty());
    patcher.EndLoad();
    CHECK(patcher.load_ended());
    const int writes = g.writes;
    const size_t protect_calls = g.protect_calls.size();
    for (const bool on : {true, false}) {
      CHECK(patcher.Apply(g.guest, kAlpha152, on, g.protect) == "applies at the next start");
      CHECK(g.writes == writes);
      CHECK(g.protect_calls.size() == protect_calls);
      CHECK(g.Holds(AlphaFixture(), loaded_on));
      CHECK(patcher.IsOn(g.guest, kAlpha152) == loaded_on);
    }
    for (const size_t t : {kTagAllStages, kMpHiddenCharacters}) {
      CHECK(patcher.Apply(g.guest, t, true, g.protect).empty());
      CHECK(g.Holds(FixtureFor(t), true));
      CHECK(patcher.Apply(g.guest, t, false, g.protect).empty());
      CHECK(g.Holds(FixtureFor(t), false));
    }
    CHECK(g.Holds(AlphaFixture(), loaded_on));
  }
}

// Her Random cell (doa4_tweaks.h): the tweak leaves the stand-in record alone, and the step after the game's boot copy
// writes Random's record into the 24th cell when, and only when, she was applied at load. A case: what the saved setting
// asked for at load (`loaded`), whether the load could make her read-only byte writable (`protect_fails`: then nothing
// of her was applied), and whether she was turned off again before the load ended (`off_again`).
static void TestAlphaRandomCell() {
  // No patch of any tweak lies in the stand-in record or in the 24th cell.
  for (const auto& tweak : AllTweaks()) {
    for (const auto& p : tweak.patches) {
      for (const uint32_t site : {kSelectStandIn, kSelectLastCell}) {
        CHECK(p.address + p.bytes.size() <= site || p.address >= site + 40);
      }
    }
  }
  // The cell's record is the patch file's Random record, byte for byte, at the 24th cell.
  CHECK(XeniaRandom().address == kSelectStandIn && XeniaRandom().on.size() == 40);
  CHECK(Alpha152RandomCell().address == kSelectLastCell);
  CHECK(Alpha152RandomCell().address == 0x82F5F2C8 && kSelectStandIn == 0x82C346D0);
  CHECK(Alpha152RandomCell().bytes == XeniaRandom().on);
  CHECK(!IsReadOnly(kSelectLastCell) && !IsReadOnly(kSelectLastCell + 39));  // DATA: the write needs no protection change
  struct Case {
    bool loaded, protect_fails, off_again;
    bool expect_on;
  };
  for (const Case& c : {Case{true, false, false, true}, Case{false, false, false, false}, Case{true, true, false, false},
                        Case{true, false, true, false}}) {
    FakeGuest g;
    g.protect_fails = c.protect_fails;
    TweakPatcher patcher;
    patcher.CaptureOriginals(g.guest, kNoAlloc);
    if (c.loaded) CHECK(patcher.Apply(g.guest, kAlpha152, true, g.protect).empty() == !c.protect_fails);
    g.protect_fails = false;
    if (c.off_again) CHECK(patcher.Apply(g.guest, kAlpha152, false, g.protect).empty());  // still the load: off is applied
    patcher.EndLoad();
    const bool applied = patcher.IsOn(g.guest, kAlpha152);
    CHECK(applied == c.expect_on);
    CHECK(g.Holds(StandInSite(), false));  // on or off: the stand-in is the game's
    g.BootCopy();
    CHECK(!g.Holds(RandomCellFixture(), true));  // the game's copy does not leave Random's record there by itself
    std::vector<uint8_t> copied;
    for (uint32_t i = 0; i < 40; ++i) copied.push_back(g.bytes[kSelectLastCell + i]);
    const int writes = g.writes;
    const size_t protect_calls = g.protect_calls.size();
    CHECK(FillAlpha152RandomCell(g.guest, applied) == (c.expect_on ? 40u : 0u));
    CHECK(g.writes == writes + (c.expect_on ? 40 : 0));
    CHECK(g.protect_calls.size() == protect_calls && g.faults == 0);
    CHECK(g.Holds(RandomCellFixture(), true) == c.expect_on);
    std::vector<uint8_t> now;
    for (uint32_t i = 0; i < 40; ++i) now.push_back(g.bytes[kSelectLastCell + i]);
    CHECK((now == copied) == !c.expect_on);  // off: the cell is exactly what the game's copy left
    if (!c.expect_on) {
      for (uint32_t i = 0; i < 40; ++i) CHECK(g.bytes[kSelectLastCell + i] == g_original.at(kSelectStandIn + i));
    }
    CHECK(g.Holds(StandInSite(), false));
    CHECK(g.Holds(AlphaFixture(), c.expect_on));
    CHECK(patcher.IsOn(g.guest, kAlpha152) == c.expect_on);  // the cell is no part of "on"
    // The menu's box afterwards changes nothing of it: both directions are refused, the cell and the stand-in stay.
    for (const bool on : {true, false}) {
      CHECK(patcher.Apply(g.guest, kAlpha152, on, g.protect) == "applies at the next start");
      CHECK(g.Holds(RandomCellFixture(), true) == c.expect_on && g.Holds(StandInSite(), false));
    }
    CHECK(g.Holds(fx::kTagAllStages, false) && g.Holds(fx::kMpHiddenCharacters, false));
  }
}

// The Extras tab's note under her box: only when the running game is known and differs from the saved setting. Before
// the image is loaded the running state is not known (the saved setting is about to be applied): no note, whatever is saved.
static void TestRestartNote() {
  using N = RestartNote;
  CHECK(RestartNoteFor(true, true) == N::kNone);
  CHECK(RestartNoteFor(false, false) == N::kNone);
  CHECK(RestartNoteFor(true, false) == N::kRunningWithout);  // the box was ticked while the game runs without her
  CHECK(RestartNoteFor(false, true) == N::kRunningWith);     // the box was cleared while the game runs with her
  CHECK(RestartNoteFor(true, std::nullopt) == N::kNone);     // saved on, the image not loaded yet: she is about to be applied
  CHECK(RestartNoteFor(false, std::nullopt) == N::kNone);
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
  TestOnlyAlphaTouchesReadOnlyData();
  TestAlphaReadOnlyByteGoesThroughProtect();
  TestAlphaFailedProtectWritesNothing();
  TestAlphaAppliesOnlyAtLoad();
  TestAlphaRandomCell();
  TestRestartNote();
  TestOffNeedsOriginals();
  TestCaptureOnlyOnce();
}

int main() {
  // Each original differs from its patch byte, or the on/off checks could not tell the two apart.
  UseSyntheticOriginals();
  for (const auto* patches : {&fx::kTagAllStages, &fx::kMpHiddenCharacters, &AlphaFileAndViews(), &RandomCellFixture()}) {
    for (const auto& p : *patches) {
      for (size_t i = 0; i < p.on.size(); ++i) CHECK(g_original.at(p.address + uint32_t(i)) != p.on[i]);
    }
  }
  // The patch file has exactly one item at the stand-in, 40 bytes; the tweak's fixture is the other five sites; and the
  // synthetic 24th cell differs from the stand-in too, so "the game's copy" and "untouched" can be told apart.
  CHECK(std::count_if(fx::kAlpha152.begin(), fx::kAlpha152.end(), [](const auto& p) { return p.address == kSelectStandIn; }) == 1);
  CHECK(AlphaFileAndViews().size() == 6 && AlphaFixture().size() == 5);
  for (uint32_t i = 0; i < 40; ++i) CHECK(g_original.at(kSelectLastCell + i) != g_original.at(kSelectStandIn + i));
  RunPatchTests();

  std::string why;
  const std::optional<std::vector<uint8_t>> originals = doa4_test::ReadImageSites(PatchSites(), kPatchSitesSha1, &why);
  const std::optional<std::vector<uint8_t>> alpha =
      originals ? doa4_test::ReadImageSites(AlphaSites(), kAlphaSitesSha1, &why) : std::nullopt;
  const std::optional<std::vector<uint8_t>> kasumi =
      alpha ? doa4_test::ReadImageSites(KasumiViewSites(), kKasumiViewsSha1, &why) : std::nullopt;
  const std::optional<std::vector<uint8_t>> cell =
      kasumi ? doa4_test::ReadImageSites(LastCellSites(), kLastCellSha1, &why) : std::nullopt;
  if (originals && alpha && kasumi && cell) {
    UseImageOriginals(*originals, *alpha, *cell);
    // The stand-in record as the image holds it: character 0xFE ("nobody"), its three flags 0, state 1. That is what a
    // locked cell must go on reading with Alpha-152 on (Random's record there reads 0xFF, flags 1, state 0).
    CHECK(g_original.at(kSelectStandIn) == 0xFE);
    for (uint32_t i = 1; i < 4; ++i) CHECK(g_original.at(kSelectStandIn + i) == 0x00);
    CHECK(g_original.at(kSelectStandIn + 36) == 0 && g_original.at(kSelectStandIn + 37) == 0 &&
          g_original.at(kSelectStandIn + 38) == 0 && g_original.at(kSelectStandIn + 39) == 1);
    // The 24th cell is empty in the image (the pin above is of 40 zero bytes): the game fills it at boot.
    for (uint32_t i = 0; i < 40; ++i) CHECK(g_original.at(kSelectLastCell + i) == 0x00);
    // The image holds none of her patch bytes where a site is a single value: the switch is off, her three view rows are
    // not Kasumi's yet (so on and off can be told apart on the real bytes too).
    CHECK(g_original.at(0x821B52A7) == 0x00);
    for (const auto& p : kAlphaViews) {
      std::vector<uint8_t> now;
      for (size_t i = 0; i < p.on.size(); ++i) now.push_back(g_original.at(p.address + uint32_t(i)));
      CHECK(now != p.on);
    }
    // Her view values are Kasumi's rows as your image holds them, byte for byte.
    std::vector<uint8_t> views;
    for (const auto& p : kAlphaViews) views.insert(views.end(), p.on.begin(), p.on.end());
    CHECK(views == *kasumi);
    std::printf("tweaks: image tier: Alpha-152's 6 sites, the 24th select cell and Kasumi's 3 view rows read from the image\n");
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
