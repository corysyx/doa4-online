// Host test for src/doa4_suitconv_recipe.cpp (the recipe tables) and src/doa4_suitconv_inputs.cpp (the checked disc and
// game-file inputs). Built and run by run_suitconv_inputs_test.bat with the converter's compile flags.
//
// Groups: recipe_matches_picks, entry_tables_complete, image_and_folder_agree, wrong_entry_named, size_and_toc_problems,
// missing_archive, not_xbox, read_error, cancel, read_after_change_throws, eight_threads_read_equal; and three groups
// gated by environment variables (each prints "<group>: SKIPPED (<VAR> unset)" when its variable is unset):
//   real_disc    DOA4_SUITCONV_DISC      one path: the DOAX2 USA ISO, or the folder it was extracted to; 86/86 entries
//   real_game    DOA4_SUITCONV_GAME_DIR  the DOA4 game folder (chrone.afs, chrtwo.afs, chrthree.afs); 21/21 entries
//   real_oracle  DOA4_SUITCONV_ORACLE_DIR  physics_spike/imports/all34_m1: the strengths equal recipe_m1/bust_match.json
//                                          and the reference sizes and SHA-1s equal manifest.txt
// The synthetic images and folders are made from seeded bytes under %TEMP%\doa4_suitconv_inputs\a b (c), <u umlaut>\ and
// deleted at the end. The real inputs are only read. Last lines: "skipped sections: K", then
// "PASSED: N checks, 0 failures" or "FAILED: N checks, M failures".

#ifndef NOGDI
#define NOGDI  // wingdi.h's Arc() function would be ambiguous with doa4::suitconv::Arc under the using-directive
#endif
#include "../../src/doa4_suitconv_recipe.h"
#include "../../src/doa4_suitconv_inputs.h"
#include "../../src/doa4_suits_picks.h"
#include "suitconv_synth.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace doa4::suitconv;
namespace fs = std::filesystem;

static int g_checks = 0;
static int g_failures = 0;
static int g_skipped = 0;
static int g_printed = 0;
static constexpr int kPrintCap = 25;

static void Check(bool ok, const char* file, int line, const char* expr, const char* fmt = nullptr, ...) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  if (g_printed++ >= kPrintCap) return;
  std::printf("FAIL %s:%d: %s", file, line, expr);
  if (fmt) {
    std::printf("  [");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("]");
  }
  std::printf("\n");
}
#define CK(cond) Check((cond), __FILE__, __LINE__, #cond)
#define CKF(cond, ...) Check((cond), __FILE__, __LINE__, #cond, __VA_ARGS__)

static void Group(const char* name) {
  g_printed = 0;
  std::printf("-- %s\n", name);
}

static std::string Narrow(const fs::path& p) { return p.string(); }

// ---------------------------------------------------------------------------------------------------------------
// recipe_matches_picks, entry_tables_complete
// ---------------------------------------------------------------------------------------------------------------

namespace pk = doa4::suits::picks;

// The charId -> girl code mapping, typed from the plan's table (vks = Kasumi 5, vay = Ayane 13, vht = Hitomi 21,
// vco = Kokoro 10, vhe = Helena 7, vls = Lisa 15, vcr = Christie 20, vle = Leifang 12, vtn = Tina 1).
static const char* GirlOfChar(int ch) {
  switch (ch) {
    case 5: return "vks";
    case 13: return "vay";
    case 21: return "vht";
    case 10: return "vco";
    case 7: return "vhe";
    case 15: return "vls";
    case 20: return "vcr";
    case 12: return "vle";
    case 1: return "vtn";
  }
  return "?";
}

static const EntryRef* FindIn(std::span<const EntryRef> t, Arc a, uint32_t i) {
  for (const EntryRef& e : t)
    if (e.arc == a && e.index == i) return &e;
  return nullptr;
}
static bool SameEntry(const EntryRef& a, const EntryRef& b) {
  return a.arc == b.arc && a.index == b.index && a.size == b.size && std::string(a.sha1) == std::string(b.sha1);
}
static bool IsSha1(const char* s) {
  const std::string t(s);
  if (t.size() != 40) return false;
  for (char c : t)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
static bool SameBits(double a, double b) { return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b); }

struct ExpectGirl {
  const char* girl;
  int character;
  uint32_t body;
  Arc ref_arc;
  uint32_t ref_tpr;
  Arc hand_arc;
  uint32_t hand_tpr;
  uint64_t budget_tpr, budget_cat;
  bool leg_midline, carry_head, head_physics, hair_spares;
};

static void TestRecipeMatchesPicks() {
  Group("recipe_matches_picks");
  const auto suits = Suits();
  CK(suits.size() == 34);
  CK(std::size(pk::kRows) == 34);
  CK(suits.size() == std::size(pk::kRows));
  for (size_t i = 0; i < suits.size() && i < std::size(pk::kRows); ++i) {
    const SuitRecipe& s = suits[i];
    const pk::Row& r = pk::kRows[i];
    CKF(std::string(s.design) == r.design, "row %zu: %s vs %s", i, s.design, r.design);
    CKF(s.colour == r.colour, "row %zu colour %d vs %d", i, s.colour, r.colour);
    // The girl is the picks table's character, not the design's prefix.
    CKF(std::string(s.girl) == GirlOfChar(r.character), "row %zu girl %s vs char %d", i, s.girl, r.character);
  }
  // Prefix is not the girl: three suits are worn by another girl than their design's prefix says.
  CK(std::string(suits[10].design) == "vks_wi_b" && std::string(suits[10].girl) == "vht");
  CK(std::string(suits[13].design) == "vhe_on_c" && std::string(suits[13].girl) == "vco");
  CK(std::string(suits[33].design) == "vls_on_b" && std::string(suits[33].girl) == "vtn");
  CK(std::string(suits[0].design) == "vks_ch_d" && suits[0].arc == Arc::kWertwo && suits[0].tpr_index == 26);
  CK(std::string(suits[33].design) == "vls_on_b" && suits[33].arc == Arc::kWertwo && suits[33].tpr_index == 242);
  CK(suits[20].colour == 1 && suits[27].colour == 2 && suits[33].colour == 1);

  // The girls: nine, with the per-girl table's budgets and options.
  static const ExpectGirl kGirls[] = {
      {"vks", 5, 2, Arc::kChrone, 51, Arc::kChrone, 51, 0xA00000, 0x200000, false, false, false, true},
      {"vay", 13, 10, Arc::kChrtwo, 116, Arc::kChrtwo, 116, 0x900000, 0x200000, false, false, false, false},
      {"vht", 21, 16, Arc::kChrthree, 72, Arc::kChrthree, 72, 0xC00000, 0x200000, false, false, false, false},
      {"vco", 10, 6, Arc::kChrtwo, 40, Arc::kChrtwo, 40, 0xCB0000, 0x300000, false, false, false, true},
      {"vhe", 7, 4, Arc::kChrtwo, 20, Arc::kChrtwo, 20, 0xFA0000, 0x200000, false, false, false, false},
      {"vls", 15, 12, Arc::kChrthree, 10, Arc::kChrthree, 10, 0xC00000, 0x200000, false, false, false, false},
      {"vcr", 20, 14, Arc::kChrthree, 54, Arc::kChrthree, 54, 0xC40000, 0x200000, false, false, false, false},
      {"vle", 12, 8, Arc::kChrtwo, 102, Arc::kChrtwo, 102, 0xC00000, 0x200000, true, false, false, true},
      {"vtn", 1, 0, Arc::kChrone, 11, Arc::kChrone, 13, 0x1400000, 0x400000, false, true, true, false},
  };
  const auto girls = Girls();
  CK(girls.size() == 9);
  for (size_t i = 0; i < girls.size() && i < std::size(kGirls); ++i) {
    const GirlRecipe& g = girls[i];
    const ExpectGirl& e = kGirls[i];
    CKF(std::string(g.girl) == e.girl, "girl %zu", i);
    CKF(g.character == e.character, "%s character %d", g.girl, g.character);
    CKF(std::string(GirlOfChar(g.character)) == g.girl, "%s charId maps back", g.girl);
    CKF(g.body_index == e.body, "%s body %u", g.girl, g.body_index);
    CKF(g.ref_tpr.arc == e.ref_arc && g.ref_tpr.index == e.ref_tpr && g.ref_cat.arc == e.ref_arc &&
            g.ref_cat.index == e.ref_tpr + 1,
        "%s reference", g.girl);
    CKF(g.hand_tpr.arc == e.hand_arc && g.hand_tpr.index == e.hand_tpr && g.hand_cat.arc == e.hand_arc &&
            g.hand_cat.index == e.hand_tpr + 1,
        "%s hands", g.girl);
    CKF(g.budget_tpr == e.budget_tpr && g.budget_cat == e.budget_cat, "%s budgets", g.girl);
    CKF(g.leg_midline == e.leg_midline && g.carry_head == e.carry_head && g.head_physics == e.head_physics &&
            g.hair_spares == e.hair_spares,
        "%s options", g.girl);
  }
  // Only Tina's hands differ from her reference.
  for (const GirlRecipe& g : girls) {
    const bool same = SameEntry(g.ref_tpr, g.hand_tpr) && SameEntry(g.ref_cat, g.hand_cat);
    CKF(same == (std::string(g.girl) != "vtn"), "%s hand donor", g.girl);
  }
  // Every suit's girl is one of the nine, and her charId is the picks table's.
  for (size_t i = 0; i < suits.size() && i < std::size(pk::kRows); ++i) {
    const GirlRecipe* g = nullptr;
    for (const GirlRecipe& x : girls)
      if (std::string(x.girl) == suits[i].girl) g = &x;
    CKF(g && g->character == pk::kRows[i].character, "row %zu girl's charId", i);
  }

  // Per-design options (the plan's paragraph), each suit checked against its design's rule.
  const std::set<std::string> port = {"vhe_sk_a", "vco_sk_c", "vle_on_i", "vcr_cm_c", "vtn_sh_c"};
  const std::set<std::string> gen = {"vks_on_o", "vle_sk_h"};
  const std::set<std::string> unstick = {"vle_on_i", "vle_sk_h", "vcr_cm_c"};
  const std::set<std::string> fit = {"vco_sk_c", "vle_on_i", "vcr_cm_c"};
  int n_port = 0, n_gen = 0, n_unstick = 0, n_fit = 0;
  for (const SuitRecipe& s : suits) {
    const std::string d = s.design;
    CKF(s.port_cloth == (port.count(d) != 0), "%s port_cloth", s.design);
    CKF(s.gen_cloth == (gen.count(d) != 0), "%s gen_cloth", s.design);
    CKF(s.unstick_cloth == (unstick.count(d) != 0), "%s unstick_cloth", s.design);
    CKF(s.fit_leg_colliders == (fit.count(d) != 0), "%s fit_leg_colliders", s.design);
    CKF(SameBits(s.back_margin, d == "vle_on_i" ? 0.012 : 0.0), "%s back_margin", s.design);
    CKF(SameBits(s.sink_hamstrings, d == "vle_on_i" ? 0.008 : 0.0), "%s sink_hamstrings", s.design);
    CKF(SameBits(s.strap_fade, d == "vcr_cm_c" ? 0.035 : 0.0), "%s strap_fade", s.design);
    n_port += s.port_cloth;
    n_gen += s.gen_cloth;
    n_unstick += s.unstick_cloth;
    n_fit += s.fit_leg_colliders;
    CKF(s.bust_down > 0 && s.bust_up > 0, "%s bust", s.design);
    CKF(s.out_tpr_size > 0 && s.out_cat_size > 0 && IsSha1(s.out_tpr_sha1) && IsSha1(s.out_cat_sha1), "%s outputs",
        s.design);
  }
  CK(n_port == 5 && n_gen == 2 && n_unstick == 3 && n_fit == 3);  // the sets above are all present among the 34
  CK(kConverterWord == "proto-1d");
}

static void TestEntryTablesComplete() {
  Group("entry_tables_complete");
  const auto disc = DiscEntries();
  const auto game = GameEntries();
  CK(disc.size() == 86);
  CK(game.size() == 21);
  std::set<std::pair<int, uint32_t>> seen;
  uint64_t bytes = 0;
  for (const EntryRef& e : disc) {
    CKF(ArcIsDisc(e.arc), "%s is a disc archive", EntryLabel(e).c_str());
    CKF(seen.insert({int(e.arc), e.index}).second, "%s repeated", EntryLabel(e).c_str());
    CKF(IsSha1(e.sha1), "%s sha1", EntryLabel(e).c_str());
    bytes += e.size;
  }
  CK(seen.size() == 86);
  CKF(bytes == 241407828u, "disc bytes %llu", (unsigned long long)bytes);
  seen.clear();
  bytes = 0;
  for (const EntryRef& e : game) {
    CKF(!ArcIsDisc(e.arc), "%s is a game archive", EntryLabel(e).c_str());
    CKF(seen.insert({int(e.arc), e.index}).second, "%s repeated", EntryLabel(e).c_str());
    CKF(IsSha1(e.sha1), "%s sha1", EntryLabel(e).c_str());
    bytes += e.size;
  }
  CK(seen.size() == 21);
  CKF(bytes == 111611696u, "game bytes %llu", (unsigned long long)bytes);

  // The tables agree with each other: the 18 bodies, then the suits' .tpr and .cat in picks order.
  const auto suits = Suits();
  const auto girls = Girls();
  CK(disc.size() == 18 + 2 * suits.size());
  for (size_t i = 0; i < suits.size() && 18 + 2 * i + 1 < disc.size(); ++i) {
    const EntryRef& t = disc[18 + 2 * i];
    const EntryRef& c = disc[18 + 2 * i + 1];
    CKF(t.arc == suits[i].arc && t.index == suits[i].tpr_index && c.arc == t.arc && c.index == t.index + 1,
        "suit %s entries", suits[i].design);
  }
  for (const GirlRecipe& g : girls) {
    const EntryRef* t = FindIn(disc, Arc::kChr, g.body_index);
    const EntryRef* c = FindIn(disc, Arc::kChr, g.body_index + 1);
    CKF(t && c, "%s body entries in the disc table", g.girl);
    for (const EntryRef* x : {&g.ref_tpr, &g.ref_cat, &g.hand_tpr, &g.hand_cat}) {
      const EntryRef* in = FindIn(game, x->arc, x->index);
      CKF(in && SameEntry(*in, *x), "%s donor %s in the game table", g.girl, EntryLabel(*x).c_str());
    }
  }
  for (const EntryRef& x : {WaistTemplate(true), WaistTemplate(false), GenBend()}) {
    const EntryRef* in = FindIn(game, x.arc, x.index);
    CKF(in && SameEntry(*in, x), "%s in the game table", EntryLabel(x).c_str());
  }
  {
    const EntryRef d = GenDonor();
    const EntryRef* in = FindIn(disc, d.arc, d.index);
    CK(in && SameEntry(*in, d));
  }
  // The named anchors, typed from Appendix A.
  CK(WaistTemplate(true).arc == Arc::kChrtwo && WaistTemplate(true).index == 20);
  CK(WaistTemplate(false).arc == Arc::kChrtwo && WaistTemplate(false).index == 21);
  CK(GenDonor().arc == Arc::kWerone && GenDonor().index == 283 &&
     std::string(GenDonor().sha1) == "3e72583a2644dd21e60d2e54660f69220b1d395c");
  CK(GenBend().arc == Arc::kChrthree && GenBend().index == 91 &&
     std::string(GenBend().sha1) == "00b02a74e4c93171b681e23daad29d9c5b2ad1c8" && GenBend().size == 650768);
  CK(EntryLabel(GenDonor()) == "werone 283");
}

// ---------------------------------------------------------------------------------------------------------------
// Synthetic inputs
// ---------------------------------------------------------------------------------------------------------------

struct Synth {
  synth::Archive chr, werone, wertwo, chrone, chrtwo, chrthree;
  synth::Table disc, game;
  Synth()
      : chr(synth::MakeArchive(Arc::kChr, {100, 3000, 5000, 70000}, 1)),
        werone(synth::MakeArchive(Arc::kWerone, {10, 2048, 4097, 300000, 33}, 2)),
        wertwo(synth::MakeArchive(Arc::kWertwo, {0, 4096, 123457}, 3)),
        chrone(synth::MakeArchive(Arc::kChrone, {20, 9000}, 4)),
        chrtwo(synth::MakeArchive(Arc::kChrtwo, {5000, 1, 2049}, 5)),
        chrthree(synth::MakeArchive(Arc::kChrthree, {40000}, 6)) {
    for (const synth::Archive* a : {&chr, &werone, &wertwo})
      for (uint32_t i = 0; i < a->entries.size(); ++i) disc.Add(*a, i);
    for (const synth::Archive* a : {&chrone, &chrtwo, &chrthree})
      for (uint32_t i = 0; i < a->entries.size(); ++i) game.Add(*a, i);
  }
};

// The offset of entry i inside an archive file, read from its TOC.
static size_t EntryOffset(const std::vector<uint8_t>& file, uint32_t i) {
  uint32_t v;
  std::memcpy(&v, &file[8 + 8 * size_t(i)], 4);
  return v;
}

static std::unique_ptr<InputSet> OpenSet(const fs::path& disc_path, const fs::path& game_path, const synth::Table& disc,
                                         const synth::Table& game) {
  std::string error;
  auto set = InputSet::Open(disc_path, game_path, disc.refs, game.refs, &error);
  CKF(set != nullptr, "Open: %s", error.c_str());
  return set;
}

// Read that reports a throw as null, so a failing Read is one failed check and the rest of the group still runs.
static std::shared_ptr<const std::vector<uint8_t>> TryRead(InputSet& set, const EntryRef& e) {
  try {
    return set.Read(e);
  } catch (const std::exception&) {
    return nullptr;
  }
}

struct Progress {
  std::vector<uint64_t> seen;
  std::function<void(uint64_t)> fn() {
    return [this](uint64_t b) { seen.push_back(b); };
  }
  bool monotonic() const {
    for (size_t i = 1; i < seen.size(); ++i)
      if (seen[i] < seen[i - 1]) return false;
    return true;
  }
};

static InputCheck RunDisc(InputSet& set, Progress* p = nullptr) {
  CancelToken never;
  Progress local;
  Progress& pr = p ? *p : local;
  return set.CheckDisc(never, pr.fn());
}

struct Files {
  fs::path root;        // %TEMP%\doa4_suitconv_inputs\a b (c), <umlaut>
  fs::path img0, img1;  // a volume at offset 0 and one at 0xFD90000
  fs::path disc_dir;    // the same three archives, upper-case names
  fs::path game_dir;
  uint64_t img0_on_disk = 0, img1_on_disk = 0;
};

static void WriteFolder(const fs::path& dir, const std::vector<std::pair<std::string, const synth::Archive*>>& files) {
  fs::create_directories(dir);
  for (const auto& [name, a] : files) synth::WriteFileBytes(dir / name, a->file);
}

static void TestImageAndFolderAgree(const Synth& s, const Files& f) {
  Group("image_and_folder_agree");
  for (const fs::path* p : {&f.img0, &f.img1, &f.disc_dir}) {
    const std::string which = p == &f.img0 ? "image at 0" : p == &f.img1 ? "image at 0xFD90000" : "folder";
    auto set = OpenSet(*p, f.game_dir, s.disc, s.game);
    if (!set) continue;
    Progress pr;
    const InputCheck c = RunDisc(*set, &pr);
    CKF(c.status == InputStatus::kOk, "%s: status %d (%s)", which.c_str(), int(c.status), c.detail.c_str());
    CKF(c.mismatched.empty(), "%s: mismatched %zu", which.c_str(), c.mismatched.size());
    CKF(c.bytes_checked == s.disc.total(), "%s: bytes %llu vs %llu", which.c_str(), (unsigned long long)c.bytes_checked,
        (unsigned long long)s.disc.total());
    CK(!pr.seen.empty());
    CK(pr.monotonic());
    CK(!pr.seen.empty() && pr.seen.back() == s.disc.total());
    // Read gives the entry's own bytes, for every entry.
    const synth::Archive* arcs[] = {&s.chr, &s.werone, &s.wertwo};
    int read = 0;
    for (const EntryRef& e : s.disc.refs) {
      const synth::Archive& a = *arcs[int(e.arc)];
      const auto bytes = TryRead(*set, e);
      CKF(bytes && *bytes == a.entries[e.index], "%s: Read %s", which.c_str(), EntryLabel(e).c_str());
      ++read;
    }
    CK(read == 12);
    // The game folder (mixed-case names) through the same set.
    CancelToken never;
    const InputCheck g = set->CheckGame(never, {});
    CKF(g.status == InputStatus::kOk && g.bytes_checked == s.game.total(), "%s: game status %d %s", which.c_str(),
        int(g.status), g.detail.c_str());
  }
  // The two images and the folder give equal bytes for the same entry (checked above against the seeded source; this
  // pins the three to each other through a Read of one entry each).
  auto a = OpenSet(f.img0, f.game_dir, s.disc, s.game);
  auto b = OpenSet(f.img1, f.game_dir, s.disc, s.game);
  auto c = OpenSet(f.disc_dir, f.game_dir, s.disc, s.game);
  if (a && b && c) {
    const EntryRef e = s.disc.refs[8];
    const auto x = TryRead(*a, e), y = TryRead(*b, e), z = TryRead(*c, e);
    CK(x && y && z && *x == *y && *y == *z && !x->empty());
  }
}

static void TestWrongEntryNamed(const Synth& s, const Files& f) {
  Group("wrong_entry_named");
  const fs::path dir = f.root / "flip";
  // One payload byte of werone entry 3 flipped, in a folder and in an image.
  synth::Archive bad = s.werone;
  const size_t at = EntryOffset(bad.file, 3) + 17;
  bad.file[at] ^= 0x01;
  bad.entries[3][17] ^= 0x01;
  WriteFolder(dir / "folder", {{"CHR.AFS", &s.chr}, {"WERONE.AFS", &bad}, {"WERTWO.AFS", &s.wertwo}});
  synth::WriteSparseImage(dir / "flip.iso", synth::BuildVolume({{"chr.afs", &s.chr}, {"werone.afs", &bad}, {"wertwo.afs", &s.wertwo}}, 0));
  for (const fs::path& p : {dir / "folder", dir / "flip.iso"}) {
    auto set = OpenSet(p, f.game_dir, s.disc, s.game);
    if (!set) continue;
    const InputCheck c = RunDisc(*set);
    CKF(c.status != InputStatus::kOk, "the flipped byte went unnoticed: %s", Narrow(p).c_str());
    CKF(c.status == InputStatus::kMismatch, "status %d", int(c.status));
    CKF(c.mismatched == std::vector<std::string>{"werone 3"}, "mismatched: %zu entries, first '%s'", c.mismatched.size(),
        c.mismatched.empty() ? "" : c.mismatched[0].c_str());
    // Read refuses that entry and still serves the others.
    bool threw = false;
    try {
      set->Read(s.disc.refs[7]);  // werone 3 is the 8th entry: chr 0-3, werone 0-3
    } catch (const InputChanged&) {
      threw = true;
    }
    CK(threw);
    CK(TryRead(*set, s.disc.refs[6]) != nullptr);
  }
  // Two bad entries in different archives are both listed, in table order. One is the right size with a wrong hash,
  // the other the wrong size.
  synth::Table t;
  t.refs = s.disc.refs;
  t.sha = s.disc.sha;
  t.sha[1] = std::string(40, '0');  // chr 1: a hash that is not the entry's
  t.refs[10].size += 1;             // wertwo 1: the table's size is not the entry's
  for (size_t i = 0; i < t.refs.size(); ++i) t.refs[i].sha1 = t.sha[i].c_str();  // after the assignment above
  auto set = OpenSet(f.disc_dir, f.game_dir, t, s.game);
  if (set) {
    const InputCheck c = RunDisc(*set);
    CK(c.status == InputStatus::kMismatch);
    CKF((c.mismatched == std::vector<std::string>{"chr 1", "wertwo 1"}), "mismatched: %zu entries, first '%s'", c.mismatched.size(),
        c.mismatched.empty() ? "" : c.mismatched[0].c_str());
    // A size mismatch costs no hash: wertwo 1 (4096) was not hashed.
    CKF(c.bytes_checked == s.disc.total() - 4096, "bytes_checked %llu", (unsigned long long)c.bytes_checked);
  }
}

static void TestSizeAndTocProblems(const Synth& s, const Files& f) {
  Group("size_and_toc_problems");
  const fs::path dir = f.root / "toc";
  // An entry index past the archive's count: named, the others still checked.
  synth::Table t;
  t.refs = s.disc.refs;
  t.sha = s.disc.sha;
  for (size_t i = 0; i < t.refs.size(); ++i) t.refs[i].sha1 = t.sha[i].c_str();
  t.AddRaw(Arc::kChr, 9, 5, std::string(40, 'a'));
  {
    auto set = OpenSet(f.disc_dir, f.game_dir, t, s.game);
    if (set) {
      const InputCheck c = RunDisc(*set);
      CK(c.status == InputStatus::kMismatch);
      CKF((c.mismatched == std::vector<std::string>{"chr 9"}), "mismatched %zu", c.mismatched.size());
      CKF(c.bytes_checked == s.disc.total(), "the other entries were checked: %llu", (unsigned long long)c.bytes_checked);
      bool threw = false;
      try {
        set->Read(t.refs.back());
      } catch (const InputChanged&) {
        threw = true;
      }
      CK(threw);
    }
  }
  // An archive whose TOC does not parse: every table entry in it is named, the other archives are fine.
  synth::Archive junk{Arc::kWerone, {}, synth::Seeded(77, 9000)};
  WriteFolder(dir / "junk", {{"CHR.AFS", &s.chr}, {"WERONE.AFS", &junk}, {"WERTWO.AFS", &s.wertwo}});
  {
    auto set = OpenSet(dir / "junk", f.game_dir, s.disc, s.game);
    if (set) {
      const InputCheck c = RunDisc(*set);
      CK(c.status == InputStatus::kMismatch);
      CKF((c.mismatched == std::vector<std::string>{"werone 0", "werone 1", "werone 2", "werone 3", "werone 4"}),
          "mismatched %zu", c.mismatched.size());
      CK(!c.detail.empty());
    }
  }
  // A non-existent path.
  {
    auto set = OpenSet(f.root / "no such image.iso", f.root / "no such game dir", s.disc, s.game);
    if (set) {
      CKF(RunDisc(*set).status == InputStatus::kNotFound, "disc not found");
      CancelToken never;
      CK(set->CheckGame(never, {}).status == InputStatus::kNotFound);
    }
  }
  // Tables that do not fit: a game archive in the disc table.
  {
    std::string error;
    auto set = InputSet::Open(f.disc_dir, f.game_dir, s.game.refs, s.game.refs, &error);
    CK(set == nullptr && !error.empty());
  }
}

static void TestMissingArchive(const Synth& s, const Files& f) {
  Group("missing_archive");
  const fs::path dir = f.root / "missing";
  WriteFolder(dir / "disc", {{"CHR.AFS", &s.chr}, {"WERONE.AFS", &s.werone}});  // no wertwo
  WriteFolder(dir / "game", {{"CHRONE.AFS", &s.chrone}, {"CHRTWO.AFS", &s.chrtwo}});  // no chrthree
  fs::create_directories(dir / "empty");
  synth::WriteSparseImage(dir / "no_wertwo.iso", synth::BuildVolume({{"chr.afs", &s.chr}, {"werone.afs", &s.werone}}, 0));
  synth::WriteSparseImage(dir / "no_wertwo_far.iso",
                          synth::BuildVolume({{"chr.afs", &s.chr}, {"werone.afs", &s.werone}}, 0xFD90000));
  for (const fs::path& p : {dir / "disc", dir / "empty", dir / "no_wertwo.iso", dir / "no_wertwo_far.iso"}) {
    auto set = OpenSet(p, f.game_dir, s.disc, s.game);
    if (!set) continue;
    const InputCheck c = RunDisc(*set);
    CKF(c.status == InputStatus::kNoArchives, "%s: status %d", Narrow(p.filename()).c_str(), int(c.status));
    CKF(c.status != InputStatus::kOk && c.bytes_checked == 0, "%s", Narrow(p.filename()).c_str());
    CK(!c.detail.empty());
  }
  {
    auto set = OpenSet(f.disc_dir, dir / "game", s.disc, s.game);
    if (set) {
      CancelToken never;
      const InputCheck c = set->CheckGame(never, {});
      CKF(c.status == InputStatus::kNoArchives, "game folder: status %d", int(c.status));
      CK(RunDisc(*set).status == InputStatus::kOk);  // the disc side is independent
    }
  }
}

static void TestNotXbox(const Synth& s, const Files& f) {
  Group("not_xbox");
  const fs::path dir = f.root / "notxbox";
  fs::create_directories(dir);
  synth::WriteFileBytes(dir / "random.iso", synth::Seeded(5, 70000));
  synth::WriteFileBytes(dir / "empty.iso", {});
  synth::WriteFileBytes(dir / "afs_file.iso", s.chr.file);  // an AFS archive is not a disc image
  for (const char* name : {"random.iso", "empty.iso", "afs_file.iso"}) {
    auto set = OpenSet(dir / name, f.game_dir, s.disc, s.game);
    if (!set) continue;
    const InputCheck c = RunDisc(*set);
    CKF(c.status == InputStatus::kNotXbox, "%s: status %d", name, int(c.status));
    CK(c.status != InputStatus::kOk && c.bytes_checked == 0);
  }
}

// An archive source that fails reads from one offset on, once armed.
class FlakySource final : public doa4::afs::Source {
 public:
  explicit FlakySource(std::shared_ptr<synth::MemArchive> inner) : inner_(std::move(inner)) {}
  uint64_t size() const override { return inner_->size(); }
  bool Read(uint64_t offset, void* dst, size_t n) const override {
    if (armed.load() && offset + n > fail_from) return false;
    return inner_->Read(offset, dst, n);
  }
  mutable std::atomic<bool> armed{false};
  uint64_t fail_from = 0;

 private:
  std::shared_ptr<synth::MemArchive> inner_;
};

static InputSet::ArchiveSources MemSources(const std::vector<std::pair<Arc, std::shared_ptr<const doa4::afs::Source>>>& v) {
  return v;
}

static void TestReadError(const Synth& s) {
  Group("read_error");
  auto chr = std::make_shared<synth::MemArchive>(s.chr.file);
  auto flaky = std::make_shared<FlakySource>(std::make_shared<synth::MemArchive>(s.werone.file));
  auto wertwo = std::make_shared<synth::MemArchive>(s.wertwo.file);
  std::string error;
  auto set = InputSet::OpenSources(MemSources({{Arc::kChr, chr}, {Arc::kWerone, flaky}, {Arc::kWertwo, wertwo}}),
                                   MemSources({}), s.disc.refs, {}, &error);
  CKF(set != nullptr, "OpenSources: %s", error.c_str());
  if (!set) return;
  flaky->fail_from = EntryOffset(s.werone.file, 2);
  flaky->armed = true;
  const InputCheck c = RunDisc(*set);
  CKF(c.status == InputStatus::kReadError, "status %d", int(c.status));
  CK(!c.detail.empty());
  CK(c.status != InputStatus::kOk);
  bool threw = false;
  try {
    set->Read(s.disc.refs[6]);  // werone 2
  } catch (const InputChanged&) {
    threw = true;
  }
  CK(threw);
}

static void TestCancel(const Synth& s) {
  Group("cancel");
  // A 20 MB entry: the token is checked inside the hash, not only between entries.
  synth::Archive big = synth::MakeArchive(Arc::kChr, {20u << 20, 100}, 9);
  synth::Table t;
  t.Add(big, 0);
  t.Add(big, 1);
  auto mem = std::make_shared<synth::MemArchive>(big.file);
  std::string error;
  auto set = InputSet::OpenSources(MemSources({{Arc::kChr, mem}}), MemSources({}), t.refs, {}, &error);
  CKF(set != nullptr, "OpenSources: %s", error.c_str());
  if (!set) return;
  {
    CancelToken token;
    std::vector<uint64_t> seen;
    const InputCheck c = set->CheckDisc(token, [&](uint64_t b) {
      seen.push_back(b);
      token.Cancel();
    });
    CK(c.status == InputStatus::kCancelled);
    CKF(seen.size() == 1, "callbacks after cancel: %zu", seen.size());
    CKF(c.bytes_checked <= (8u << 20) && c.bytes_checked > 0, "bytes before the cancel took: %llu",
        (unsigned long long)c.bytes_checked);
    CK(c.mismatched.empty());
  }
  {
    CancelToken token;
    token.Cancel();
    int calls = 0;
    const InputCheck c = set->CheckDisc(token, [&](uint64_t) { ++calls; });
    CK(c.status == InputStatus::kCancelled && c.bytes_checked == 0 && calls == 0);
  }
  {
    CancelToken never;
    const InputCheck c = set->CheckDisc(never, {});  // no callback is allowed
    CK(c.status == InputStatus::kOk && c.bytes_checked == t.total());
  }
  (void)s;
}

static void TestReadAfterChangeThrows(const Synth& s, const Files& f) {
  Group("read_after_change_throws");
  // The files an InputSet reads are held open with writing refused, so a checked file cannot be changed through the
  // file system (that is the first line of defence; asserted here). Read re-hashes anyway: shown on a source whose
  // bytes change after the check.
  {
    auto set = OpenSet(f.disc_dir, f.game_dir, s.disc, s.game);
    if (set) {
      CK(RunDisc(*set).status == InputStatus::kOk);
      HANDLE h = CreateFileW((f.disc_dir / "WERONE.AFS").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
      CKF(h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION, "a checked archive could be opened for writing");
      if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
  }
  {
    // Control: with the set gone, the same open succeeds, so the refusal above came from the set.
    HANDLE h = CreateFileW((f.disc_dir / "WERONE.AFS").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    CK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
  auto chr = std::make_shared<synth::MemArchive>(s.chr.file);
  auto werone = std::make_shared<synth::MemArchive>(s.werone.file);
  auto wertwo = std::make_shared<synth::MemArchive>(s.wertwo.file);
  std::string error;
  auto set = InputSet::OpenSources(MemSources({{Arc::kChr, chr}, {Arc::kWerone, werone}, {Arc::kWertwo, wertwo}}),
                                   MemSources({}), s.disc.refs, {}, &error);
  CKF(set != nullptr, "OpenSources: %s", error.c_str());
  if (!set) return;
  CK(RunDisc(*set).status == InputStatus::kOk);
  const EntryRef& e = s.disc.refs[7];  // werone 3
  CK(TryRead(*set, e) != nullptr);
  werone->bytes()[EntryOffset(s.werone.file, 3) + 100] ^= 0x40;  // a byte changes after the check
  bool threw = false;
  std::string what;
  try {
    set->Read(e);
  } catch (const InputChanged& x) {
    threw = true;
    what = x.what();
  }
  CK(threw);
  CKF(what.find("werone 3") != std::string::npos, "message: %s", what.c_str());
  CK(TryRead(*set, s.disc.refs[6]) != nullptr);  // werone 2 is untouched
  // The change is undone: Read serves the entry again (no latch).
  werone->bytes()[EntryOffset(s.werone.file, 3) + 100] ^= 0x40;
  CK(TryRead(*set, e) != nullptr);
  // A change of the last byte of the entry is caught as well as one in the middle.
  werone->bytes()[EntryOffset(s.werone.file, 3) + s.werone.entries[3].size() - 1] ^= 0x01;
  threw = false;
  try {
    set->Read(e);
  } catch (const InputChanged&) {
    threw = true;
  }
  CK(threw);
}

static void TestEightThreads(const Synth& s, const Files& f) {
  Group("eight_threads_read_equal");
  for (const fs::path* p : {&f.img1, &f.disc_dir}) {
    auto set = OpenSet(*p, f.game_dir, s.disc, s.game);
    if (!set) continue;
    CK(RunDisc(*set).status == InputStatus::kOk);
    // The single-threaded reads, each shown non-empty (except the zero-length entry).
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> want;
    for (const EntryRef& e : s.disc.refs) want.push_back(TryRead(*set, e));
    for (size_t i = 0; i < want.size(); ++i) CK(want[i] && (s.disc.refs[i].size == 0 || !want[i]->empty()));
    std::atomic<int> reads{0}, bad{0}, threw{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
      threads.emplace_back([&, t] {
        for (int round = 0; round < 30; ++round) {
          for (size_t k = 0; k < s.disc.refs.size(); ++k) {
            const size_t i = (k + size_t(t) * 3 + size_t(round)) % s.disc.refs.size();
            try {
              const auto got = set->Read(s.disc.refs[i]);
              if (!got || *got != *want[i]) ++bad;
            } catch (...) {
              ++threw;
            }
            ++reads;
          }
        }
      });
    }
    for (auto& th : threads) th.join();
    CKF(reads == 8 * 30 * 12, "reads %d", reads.load());
    CKF(bad == 0, "%d reads differed", bad.load());
    CKF(threw == 0, "%d reads threw", threw.load());
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Real inputs (read only)
// ---------------------------------------------------------------------------------------------------------------

static const char* Env(const char* name) {
  static std::map<std::string, std::string> cache;  // _dupenv_s copies; keep the copy alive
  char* buf = nullptr;
  size_t len = 0;
  if (_dupenv_s(&buf, &len, name) != 0 || !buf) return nullptr;
  cache[name] = buf;
  free(buf);
  return cache[name].empty() ? nullptr : cache[name].c_str();
}

static void SkipGroup(const char* group, const char* var) {
  ++g_skipped;
  std::printf("%s: SKIPPED (%s unset)\n", group, var);
}

static double Seconds(std::chrono::steady_clock::time_point a) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
}

static void TestRealDisc() {
  const char* disc = Env("DOA4_SUITCONV_DISC");
  if (!disc) return SkipGroup("real_disc", "DOA4_SUITCONV_DISC");
  Group("real_disc");
  std::printf("disc: %s\n", disc);
  std::string error;
  auto set = InputSet::Open(fs::path(disc), fs::path(), DiscEntries(), {}, &error);
  CKF(set != nullptr, "Open: %s", error.c_str());
  if (!set) return;
  CancelToken never;
  const auto t0 = std::chrono::steady_clock::now();
  uint64_t last = 0;
  bool monotonic = true;
  const InputCheck c = set->CheckDisc(never, [&](uint64_t b) {
    monotonic = monotonic && b >= last;
    last = b;
  });
  const double secs = Seconds(t0);
  int n = int(DiscEntries().size()) - int(c.mismatched.size());
  std::printf("real_disc: %d/%zu entries ok, %llu bytes checked, %.1f s\n", n, DiscEntries().size(),
              (unsigned long long)c.bytes_checked, secs);
  for (const std::string& m : c.mismatched) std::printf("  mismatched: %s\n", m.c_str());
  CKF(c.status == InputStatus::kOk, "status %d (%s)", int(c.status), c.detail.c_str());
  CK(c.mismatched.empty());
  CK(n == 86);
  CKF(c.bytes_checked == 241407828u, "bytes %llu", (unsigned long long)c.bytes_checked);
  CK(monotonic && last == c.bytes_checked);
  // Read of one entry (re-hashed) and of the donor of the generated cloth.
  const auto d = TryRead(*set, GenDonor());
  CK(d && d->size() == GenDonor().size);
}

static void TestRealGame() {
  const char* dir = Env("DOA4_SUITCONV_GAME_DIR");
  if (!dir) return SkipGroup("real_game", "DOA4_SUITCONV_GAME_DIR");
  Group("real_game");
  std::printf("game: %s\n", dir);
  std::string error;
  auto set = InputSet::Open(fs::path(), fs::path(dir), {}, GameEntries(), &error);
  CKF(set != nullptr, "Open: %s", error.c_str());
  if (!set) return;
  CancelToken never;
  const auto t0 = std::chrono::steady_clock::now();
  const InputCheck c = set->CheckGame(never, {});
  const double secs = Seconds(t0);
  int n = int(GameEntries().size()) - int(c.mismatched.size());
  std::printf("real_game: %d/%zu entries ok, %llu bytes checked, %.1f s\n", n, GameEntries().size(),
              (unsigned long long)c.bytes_checked, secs);
  for (const std::string& m : c.mismatched) std::printf("  mismatched: %s\n", m.c_str());
  CKF(c.status == InputStatus::kOk, "status %d (%s)", int(c.status), c.detail.c_str());
  CK(c.mismatched.empty());
  CK(n == 21);
  CKF(c.bytes_checked == 111611696u, "bytes %llu", (unsigned long long)c.bytes_checked);
  const auto b = TryRead(*set, GenBend());
  CK(b && b->size() == GenBend().size);
}

static bool ReadText(const fs::path& p, std::string* out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

// Just enough JSON for bust_match.json: an object of "name": [number, number].
static bool ParseBust(const std::string& text, std::map<std::string, std::pair<double, double>>* out) {
  size_t i = 0;
  const auto skip = [&] {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t' || text[i] == ',' ||
                               text[i] == ':'))
      ++i;
  };
  const auto number = [&](double* v) {
    skip();
    const size_t start = i;
    while (i < text.size() && std::string("+-0123456789.eE").find(text[i]) != std::string::npos) ++i;
    if (i == start) return false;
    return std::from_chars(text.data() + start, text.data() + i, *v).ec == std::errc();
  };
  skip();
  if (i >= text.size() || text[i] != '{') return false;
  ++i;
  for (;;) {
    skip();
    if (i < text.size() && text[i] == '}') return true;
    if (i >= text.size() || text[i] != '"') return false;
    const size_t end = text.find('"', i + 1);
    if (end == std::string::npos) return false;
    const std::string name = text.substr(i + 1, end - i - 1);
    i = end + 1;
    skip();
    if (i >= text.size() || text[i] != '[') return false;
    ++i;
    double a, b;
    if (!number(&a) || !number(&b)) return false;
    skip();
    if (i >= text.size() || text[i] != ']') return false;
    ++i;
    (*out)[name] = {a, b};
  }
}

static void TestRealOracle() {
  const char* dir = Env("DOA4_SUITCONV_ORACLE_DIR");
  if (!dir) return SkipGroup("real_oracle", "DOA4_SUITCONV_ORACLE_DIR");
  Group("real_oracle");
  std::printf("oracle: %s\n", dir);
  const fs::path oracle(dir);
  std::string bust_text, manifest;
  const fs::path bust_path = oracle / ".." / ".." / "recipe_m1" / "bust_match.json";
  CKF(ReadText(bust_path, &bust_text), "cannot read %s", Narrow(bust_path).c_str());
  CKF(ReadText(oracle / "manifest.txt", &manifest), "cannot read manifest.txt");
  std::map<std::string, std::pair<double, double>> bust;
  CK(ParseBust(bust_text, &bust));
  CKF(bust.size() == 34, "bust_match.json holds %zu designs", bust.size());
  int matched = 0;
  for (const SuitRecipe& s : Suits()) {
    const auto it = bust.find(s.design);
    CKF(it != bust.end(), "%s missing from bust_match.json", s.design);
    if (it == bust.end()) continue;
    const bool same = SameBits(it->second.first, s.bust_down) && SameBits(it->second.second, s.bust_up);
    CKF(same, "%s strengths: json [%.17g, %.17g] vs table [%.17g, %.17g]", s.design, it->second.first, it->second.second,
        s.bust_down, s.bust_up);
    matched += same;
  }
  CK(matched == 34);
  // The manifest: header, then 34 suit lines; the reference sizes and SHA-1s of the Python converter's outputs.
  std::istringstream in(manifest);
  std::string line;
  std::getline(in, line);
  CK(line == "doax2-suits 1");
  std::getline(in, line);
  CK(line == std::string("converter ") + std::string(kConverterWord));
  size_t i = 0;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream ls(line);
    std::vector<std::string> w;
    for (std::string x; ls >> x;) w.push_back(x);
    CKF(w.size() == 15 && i < Suits().size(), "manifest line %zu has %zu words", i, w.size());
    if (w.size() != 15 || i >= Suits().size()) break;
    const SuitRecipe& s = Suits()[i];
    CKF(w[7] == s.design && std::stoi(w[8]) == s.colour, "manifest line %zu is %s %s", i, w[7].c_str(), w[8].c_str());
    CKF(w[9] == std::string(s.design) + "_" + std::to_string(s.colour) + ".tpr" &&
            w[12] == std::string(s.design) + "_" + std::to_string(s.colour) + ".cat",
        "%s file names", s.design);
    CKF(std::stoull(w[10]) == s.out_tpr_size && w[11] == s.out_tpr_sha1, "%s .tpr size/sha1", s.design);
    CKF(std::stoull(w[13]) == s.out_cat_size && w[14] == s.out_cat_sha1, "%s .cat size/sha1", s.design);
    ++i;
  }
  CKF(i == 34, "manifest has %zu suit lines", i);
}

int main() {
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  Files f;
  f.root = fs::temp_directory_path() / L"doa4_suitconv_inputs" / L"a b (c), \u00FC";
  std::error_code ec;
  fs::remove_all(f.root, ec);  // a leftover of this test's own earlier run
  bool made = false;
  try {
    TestRecipeMatchesPicks();
    TestEntryTablesComplete();
    const Synth s;
    fs::create_directories(f.root);
    made = true;
    f.img0 = f.root / "disc_at_0.iso";
    f.img1 = f.root / "disc_at_FD90000.iso";
    f.disc_dir = f.root / "disc_folder";
    f.game_dir = f.root / "game_folder";
    f.img0_on_disk = synth::WriteSparseImage(
        f.img0, synth::BuildVolume({{"chr.afs", &s.chr}, {"WerOne.afs", &s.werone}, {"WERTWO.AFS", &s.wertwo}}, 0));
    f.img1_on_disk = synth::WriteSparseImage(
        f.img1, synth::BuildVolume({{"chr.afs", &s.chr}, {"WerOne.afs", &s.werone}, {"WERTWO.AFS", &s.wertwo}}, 0xFD90000));
    WriteFolder(f.disc_dir, {{"CHR.AFS", &s.chr}, {"WERONE.AFS", &s.werone}, {"WERTWO.AFS", &s.wertwo}});
    WriteFolder(f.game_dir, {{"CHRONE.AFS", &s.chrone}, {"ChrTwo.afs", &s.chrtwo}, {"chrthree.AFS", &s.chrthree}});
    std::printf("synthetic images: volume at 0 is %llu bytes on disk, volume at 0xFD90000 is %llu bytes on disk (file size %llu)\n",
                (unsigned long long)f.img0_on_disk, (unsigned long long)f.img1_on_disk,
                (unsigned long long)fs::file_size(f.img1));
    TestImageAndFolderAgree(s, f);
    TestWrongEntryNamed(s, f);
    TestSizeAndTocProblems(s, f);
    TestMissingArchive(s, f);
    TestNotXbox(s, f);
    TestReadError(s);
    TestCancel(s);
    TestReadAfterChangeThrows(s, f);
    TestEightThreads(s, f);
    TestRealDisc();
    TestRealGame();
    TestRealOracle();
  } catch (const std::exception& e) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL threw: %s\n", e.what());
  }
  if (made) fs::remove_all(f.root, ec);
  std::error_code ec2;
  fs::remove(f.root.parent_path(), ec2);  // only if empty
  std::printf("skipped sections: %d\n", g_skipped);
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
