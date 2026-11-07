// Host test for the suit converter's geometry core (src/doa4_suitconv_core.cpp, doa4_suitconv_trace.cpp,
// doa4_suitconv_catx.cpp, doa4_suitconv.cpp): convert.py's port at recipe level L0. Built and run by
// run_suitconv_convert_test.bat with the converter's compile flags.
//
// Groups: synthetic_body_converts (synthetic models from suitconv_synth.h: the output parses with Task 4's parser, every
// skinned object's palette starts with its owner's parent, two runs give equal bytes and equal traces), level_gates
// (the levels not ported yet fail with ConvertFailure{"level", ...}; a cancelled token fails with "cancelled"),
// attach_frame (physics_port.attach at L0, with an MPV, and its not-ported branches), mpv_header_marks (a synthetic chest
// object through build_mpv, read back by doa4_suits_bust.h's reader), l1_synthetic (the synthetic suit has no morph
// decl, so L1 fails the way the Python does), attach_apa_synthetic (_add_apa on a hand-built donor, offsets written out by
// hand), hands_numeric (the software fused multiply-add and np.linalg.norm of a 3-vector against pins); and the groups
// gated by environment variables
// (each prints "<group>: SKIPPED (<VAR> unset)" when a variable is unset):
//   oracle_L0_vay_on_k  DOA4_SUITCONV_DISC, DOA4_SUITCONV_GAME_DIR, DOA4_SUITCONV_LEVELS_DIR: C++ L0 equals the Python's
//                       L0 for vay_on_k, both files (with DOA4_SUITCONV_TRACE_DIR also the trace)
//   oracle_L0_all       the same three: both files, all 34 suits; prints each suit's conversion time (one thread)
//   trace_L0_all        the three and DOA4_SUITCONV_TRACE_DIR: the C++ trace equals the Python's line for line, all 34
//   oracle_L1_all, trace_L1_all   the same two at level L1 (the bust channel: prep_bust, bust_peak, build_mpv)
//   oracle_L2_all, trace_L2_all   the same two at level L2 (L1 plus --waist-fit: prep_waist_fit, H4 weights in encode_objects)
//                       the trace comparison exempts the hash of `B p<i>.h4` lines (counted; skipped without H4_DIR)
//   h4_abs_L2           also DOA4_SUITCONV_H4_DIR (the sidecar of the waist weights as doubles): C++ against Python, |diff| <= kH4MaxAbs
//   oracle_L3_all, trace_L3_all, h4_abs_L3   the same at level L3 (L2 plus --hands: prep_hands, build_hands, the donor's
//                       request table in the .cat, the hand channels in the MPV)
//   oracle_m1_without_extras   also DOA4_SUITCONV_ORACLE_DIR (the shipped m1 files, <design>_<colour>.tpr/.cat): the 16 suits
//                       with no feature after the hands convert at level m1 to the shipped bytes and to the C++ L3 bytes;
//                       the 18 suits with a spares, head or cloth feature are the groups below
//   oracle_m1_spares_head   the same directory: the 11 suits whose girl has the hair collision spares or Tina's head physics
//                       convert at level m1 to the shipped bytes (and differ from the C++ L3 bytes)
//   trace_m1, h4_abs_m1   the trace and waist-weight checks over the 34 suits kM1Ready lists
//   oracle_m1_cloth     the same directory: the 5 authored-cloth suits equal the shipped m1 files, their MPP pin-row counts
//                       equal the oracle trace's, and every relocated pin row points at its pin
//   oracle_m1_gen       the same directory: the 2 generated-cloth suits equal the shipped m1 files; fit_cloth_chunk on a generated
//                       chunk (vle_sk_h), with_spares onto the suit's own cloth and attach's cloth path with no pin rows are
//                       asserted on the real data
//   mpv_threshold_is_math_dist   build_mpv's threshold compares CPython's math.dist (vector_norm), not a plain sqrt of squares
//   gen_numeric         the generated cloth's numeric sites (Cosine, Distance, VerticalRatio, LoopKey) against interpreter pins
//   gen_prep_synthetic, gen_cloth_synthetic, walk_rejects_unterminated_writes   cloth_gen's prep (its asserts), build on a
//                       hand-made skirt against the recipe's own chunk (length and SHA-1), and walk refusing a write list
//                       without its terminator
//   argsort_is_what_matters   the same plus the traces: under a stable argsort (ScopedArgsortForTest) exactly vcr_cm_c and
//                       vtn_sh_c differ, as Python's census measured
//   cloth_numeric, unstick_rounds   the cloth's primitives (Gemv3, MathDist, PySetOrder) against interpreter pins, and the
//                       six unstick rounds on a hand-built chunk; prep_bust_synthetic also drives the cloth fade
//   attach_cloth_pins, spares_must_end_file, head_physics_synthetic   hand-built references: attach's MULTI_CLOTH branch with
//                       pin rows, with_spares (both branches, its asserts), prep_head_physics (section, face channels,
//                       asserts, CPython's set order), the {**hand, **face} merge
// On a difference the oracle groups print the first differing offset and what is there (WhereInTpr / WhereInCat:
// object, decl, vertex and field; or the .cat section and record) and, with traces, the first differing trace line,
// both lines and the last S line before it. No byte of any game file is stored: the real sections read the player's
// files at test time. Last lines: "skipped sections: K", then "PASSED: N checks, 0 failures" or "FAILED: ...".

#include "../../src/doa4_suitconv.h"
#include "../../src/doa4_suitconv_bust.h"
#include "../../src/doa4_suitconv_catx.h"
#include "../../src/doa4_suitconv_cloth.h"
#include "../../src/doa4_suitconv_core.h"
#include "../../src/doa4_suitconv_gencloth.h"
#include "../../src/doa4_suitconv_hands.h"
#include "../../src/doa4_suitconv_inputs.h"
#include "../../src/doa4_suitconv_num.h"
#include "../../src/doa4_suitconv_recipe.h"
#include "../../src/doa4_suitconv_sort.h"
#include "../../src/doa4_suitconv_tpr.h"
#include "../../src/doa4_suitconv_trace.h"
#include "../../src/doa4_suits_bust.h"
#include "suitconv_synth.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace doa4::suitconv;
namespace fs = std::filesystem;
namespace tp = doa4::suitconv::tpr;
namespace cb = doa4::suitconv::bust;
namespace rdr = doa4::suits::bust;  // the game side reader of the MPV marks (an independent consumer)

static int g_checks = 0;
static int g_failures = 0;
static int g_skipped = 0;
static int g_printed = 0;
static size_t g_h4_exempt = 0, g_h4_equal = 0;  // `B p<i>.h4` trace lines exempt (hash differs) / equal in full
static constexpr int kPrintCap = 40;

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

static const char* Env(const char* name) {
  static std::map<std::string, std::string> cache;
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

static std::span<const uint8_t> Sp(const std::vector<uint8_t>& b) { return {b.data(), b.size()}; }

// A Trace that keeps every line.
class MemTrace : public Trace {
 public:
  void Line(std::string_view s) override { lines.emplace_back(s); }
  std::vector<std::string> lines;
};

// A Trace that compares each line with the expected file's as it arrives; keeps the first difference.
class CompareTrace : public Trace {
 public:
  explicit CompareTrace(std::vector<std::string> expected) : exp_(std::move(expected)) {}
  void Line(std::string_view s) override {
    if (!diff_) {
      if (n_ < exp_.size() && exp_[n_] == s) {
        if (s.rfind("S ", 0) == 0) last_s_ = std::string(s);
        if (IsH4(s)) ++h4_equal_;
      } else if (n_ < exp_.size() && IsH4(s) && IsH4(exp_[n_]) && BeforeSha1(s) == BeforeSha1(exp_[n_])) {
        // `B p<i>.h4 <bytes> <sha1>`: the part and the byte count agree, only the hash of the doubles differs. numpy's
        // exp on this PC is the C runtime's, which the kit's Exp matches to 1 ulp but not bit for bit; the doubles are
        // checked by absolute difference in the h4_abs_L2 section instead. Exempt, and counted.
        ++h4_exempt_;
      } else {
        diff_ = true;
        at_ = n_;
        got_ = std::string(s);
      }
    }
    ++n_;
  }
  // After the run: true when every line matched and the counts agree.
  bool Equal() {
    if (!diff_ && n_ != exp_.size()) {
      diff_ = true;
      at_ = n_;
      got_ = "<end of the C++ trace>";
    }
    return !diff_;
  }
  size_t h4_exempt() const { return h4_exempt_; }  // `B p<i>.h4` lines whose hash differs (exempt)
  size_t h4_equal() const { return h4_equal_; }    // `B p<i>.h4` lines equal in full
  static bool IsH4(std::string_view l) {
    if (l.rfind("B p", 0) != 0) return false;
    size_t k = 3;
    while (k < l.size() && l[k] >= '0' && l[k] <= '9') ++k;
    return k > 3 && l.compare(k, 4, ".h4 ") == 0;
  }
  static std::string_view BeforeSha1(std::string_view l) { return l.substr(0, l.rfind(' ')); }
  void Print(const char* label) const {
    std::printf("  %s: trace differs at line %zu (C++ wrote %zu lines, Python %zu)\n", label, at_ + 1, n_, exp_.size());
    std::printf("    last S line: %s\n", last_s_.c_str());
    std::printf("    python: %.300s\n", at_ < exp_.size() ? exp_[at_].c_str() : "<end of the Python trace>");
    std::printf("    c++   : %.300s\n", got_.c_str());
  }

 private:
  std::vector<std::string> exp_;
  size_t n_ = 0, at_ = 0, h4_exempt_ = 0, h4_equal_ = 0;
  bool diff_ = false;
  std::string got_, last_s_ = "-";
};

static std::optional<std::vector<uint8_t>> ReadFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::vector<std::string> SplitLines(const std::vector<uint8_t>& b) {
  std::vector<std::string> out;
  std::string cur;
  for (uint8_t c : b) {
    if (c == '\n') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(char(c));
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Diagnostics: where a byte offset lies in a .tpr / .cat (B7)
// ---------------------------------------------------------------------------------------------------------------

static std::string WhereInTpr(const std::vector<uint8_t>& file, size_t off) {
  try {
    const tp::Xpr x = tp::Xpr::Parse(Sp(file));
    char buf[512];
    if (!x.hs) {  // the parser read no header size: say what is known
      std::snprintf(buf, sizeof buf, "(expected .tpr: header size unknown, %zu resources, offset 0x%zx)", x.res.size(), off);
      return buf;
    }
    const size_t data0 = 12 + *x.hs;
    if (off < 16 + 16 * x.res.size()) return "XPR2 header / resource table";
    if (off < data0) {
      for (size_t i = 0; i < x.res.size(); ++i) {
        const size_t s = 12 + x.orig_offs[i].first, e = s + x.res[i].blob.size();
        if (off < s || off >= e) continue;
        const size_t rel = off - s;
        std::string t(x.res[i].type.begin(), x.res[i].type.end());
        std::snprintf(buf, sizeof buf, "resource %zu (%s) +0x%zx", i, t.c_str(), rel);
        std::string out = buf;
        const auto users = x.OfType(tp::MakeFourcc("USER"));
        if ((users.size() > 2 && (i == users[1] || i == users[2]))) {
          const tp::ChunkHead h = tp::ParseChunkHead(x.res[i].blob, 0);
          for (size_t k = 0; k < h.table.size(); ++k) {
            const size_t a = h.table[k], b = k + 1 < h.table.size() ? h.table[k + 1] : h.size;
            if (rel >= a && rel < b) {
              std::snprintf(buf, sizeof buf, ", %s object %zu +0x%zx", i == users[1] ? "MdlGeo" : "MdlInfo", k, rel - a);
              out += buf;
            }
          }
        }
        return out;
      }
      return "header padding";
    }
    const size_t rel = off - data0;
    const auto users = x.OfType(tp::MakeFourcc("USER"));
    std::vector<tp::ObjGeo> geo;
    if (users.size() > 1) geo = tp::ParseMdlGeo(x.res[users[1]].blob);
    const auto vbs = x.OfType(tp::MakeFourcc("VBUF")), ibs = x.OfType(tp::MakeFourcc("IBUF")),
               txs = x.OfType(tp::MakeFourcc("TX2D"));
    for (size_t v = 0; v < vbs.size(); ++v) {
      const auto [base, size] = tp::BufSpan(x.res[vbs[v]].blob);
      if (rel < base || rel >= base + size) continue;
      std::snprintf(buf, sizeof buf, "data: vertex buffer %zu +0x%zx", v, rel - base);
      std::string out = buf;
      for (size_t o = 0; o < geo.size(); ++o)
        for (size_t d = 0; d < geo[o].decls.size(); ++d) {
          const tp::Decl& dc = geo[o].decls[d];
          if (dc.Vb() != v) continue;
          const size_t stride = dc.Stride(), r = rel - base;
          const size_t vert = stride ? r / stride : 0, byte = stride ? r % stride : 0;
          std::string field = "?";
          for (const auto& e : dc.elems)
            if (e.a == 0 && byte >= e.b) {
              const tp::DtEntry* dt = tp::DtFind(e.c);
              if (dt && byte < size_t(e.b) + dt->size)
                field = std::string(tp::UsageName(e.e)) + std::to_string(e.f) + " " + std::string(dt->name) + " +" +
                        std::to_string(byte - e.b);
            }
          std::snprintf(buf, sizeof buf, "; object %zu decl %zu vertex %zu of %u, byte %zu: %s", o, d, vert, dc.head[5],
                        byte, field.c_str());
          out += buf;
        }
      return out;
    }
    for (size_t v = 0; v < ibs.size(); ++v) {
      const auto [base, size] = tp::BufSpan(x.res[ibs[v]].blob);
      if (rel < base || rel >= base + size) continue;
      std::snprintf(buf, sizeof buf, "data: index buffer %zu, index %zu", v, (rel - base) / 2);
      std::string out = buf;
      for (size_t o = 0; o < geo.size(); ++o)
        for (size_t d = 0; d < geo[o].decls.size(); ++d)
          if (geo[o].decls[d].head[2] == v) {
            std::snprintf(buf, sizeof buf, "; object %zu decl %zu", o, d);
            out += buf;
          }
      return out;
    }
    for (size_t t = 0; t < txs.size(); ++t) {
      const auto [base, mip] = tp::TexAddrs(x.res[txs[t]].blob);
      if (rel >= base) {
        std::snprintf(buf, sizeof buf, "data: at or after texture %zu (base 0x%x) +0x%zx", t, base, rel - base);
        return buf;
      }
    }
    std::snprintf(buf, sizeof buf, "data +0x%zx (padding)", rel);
    return buf;
  } catch (const std::exception& e) {
    return std::string("(expected .tpr does not parse: ") + e.what() + ")";
  }
}

static std::string WhereInCat(const std::vector<uint8_t>& file, size_t off) {
  try {
    const auto dir = tp::CatDir(file);
    std::vector<std::pair<uint32_t, uint8_t>> starts;
    for (const auto& [t, v] : dir) starts.emplace_back(v, t);
    std::sort(starts.begin(), starts.end());
    char buf[256];
    if (off < 0x18) return "header";
    const uint32_t dir_off = (uint32_t(file[4]) << 24) | (uint32_t(file[5]) << 16) | (uint32_t(file[6]) << 8) | file[7];
    if (off >= dir_off && off < dir_off + 8 * (dir.size() + 1)) {
      std::snprintf(buf, sizeof buf, "directory record %zu", (off - dir_off) / 8);
      return buf;
    }
    for (size_t i = starts.size(); i-- > 0;) {
      if (off < starts[i].first) continue;
      const size_t rel = off - starts[i].first;
      if (starts[i].second == 4)
        std::snprintf(buf, sizeof buf, "section 4 (materials), record %zu +0x%zx", rel / 0x40, rel % 0x40);
      else
        std::snprintf(buf, sizeof buf, "section %u +0x%zx", starts[i].second, rel);
      return buf;
    }
    return "before the first section";
  } catch (const std::exception& e) {
    return std::string("(expected .cat does not parse: ") + e.what() + ")";
  }
}

// Compares one file; prints where it first differs. Returns true when equal.
static bool CompareFile(const char* label, const char* kind, const std::vector<uint8_t>& got,
                        const std::vector<uint8_t>& want) {
  if (got == want) return true;
  size_t at = 0;
  while (at < got.size() && at < want.size() && got[at] == want[at]) ++at;
  const std::string where = std::string(kind) == "tpr" ? WhereInTpr(want, at) : WhereInCat(want, at);
  std::printf("  %s .%s differs: C++ %zu bytes, Python %zu, first difference at 0x%zx: %s\n", label, kind, got.size(),
              want.size(), at, where.c_str());
  return false;
}

// ---------------------------------------------------------------------------------------------------------------
// synthetic_body_converts
// ---------------------------------------------------------------------------------------------------------------

static SuitRecipe SynthSuit() {
  return SuitRecipe{"syn_su_a", 0, "vsy", ArcId::kWerone, 7, 0.5, 0.5, false, false, false, false, 0.0, 0.0, 0.0,
                    0, "", 0, ""};
}
static GirlRecipe SynthGirl() {
  const EntryRef none{ArcId::kChrone, 0, 0, ""};
  return GirlRecipe{"vsy", 1, 3, none, none, none, none, 1 << 30, 1 << 30, false, false, false, false};
}
static SuitInputs SynthInputs(const synth::conv::Inputs& s) {
  auto P = [](const std::vector<uint8_t>& v) { return std::make_shared<const std::vector<uint8_t>>(v); };
  SuitInputs in;
  in.body_tpr = P(s.body_tpr);
  in.body_cat = P(s.body_cat);
  in.suit_tpr = P(s.suit_tpr);
  in.suit_cat = P(s.suit_cat);
  in.ref_tpr = P(s.ref_tpr);
  in.ref_cat = P(s.ref_cat);
  in.waist_tpr = P(s.waist_tpr);
  in.waist_cat = P(s.waist_cat);
  return in;
}

static void TestSyntheticBodyConverts() {
  Group("synthetic_body_converts");
  const auto s = synth::conv::MakeInputs();
  const SuitInputs in = SynthInputs(s);
  const SuitRecipe suit = SynthSuit();
  const GirlRecipe girl = SynthGirl();
  ConvertFailure f1, f2;
  MemTrace t1, t2;
  const auto a = ConvertSuit(suit, girl, in, Level::kL0, nullptr, &t1, &f1);
  CKF(a.has_value(), "first run failed at %s: %s", f1.stage.c_str(), f1.what.c_str());
  const auto b = ConvertSuit(suit, girl, in, Level::kL0, nullptr, &t2, &f2);
  CKF(b.has_value(), "second run failed at %s: %s", f2.stage.c_str(), f2.what.c_str());
  ConvertFailure f3;
  const auto c = ConvertSuit(suit, girl, in, Level::kL0, nullptr, nullptr, &f3);  // no trace
  CKF(c.has_value(), "untraced run failed at %s: %s", f3.stage.c_str(), f3.what.c_str());
  if (!a || !b || !c) return;
  CK(a->tpr == b->tpr);
  CK(a->cat == b->cat);
  CK(a->tpr == c->tpr);
  CK(a->cat == c->cat);
  CK(t1.lines == t2.lines);
  CKF(!a->tpr.empty() && !a->cat.empty(), "%zu, %zu", a->tpr.size(), a->cat.size());

  // The output parses with Task 4's parser.
  const tp::Xpr x = tp::Xpr::Parse(Sp(a->tpr));
  const auto users = x.OfType(tp::MakeFourcc("USER"));
  CK(users.size() == 4);
  if (users.size() != 4) return;
  const auto geo = tp::ParseMdlGeo(x.res[users[1]].blob);
  const auto info = tp::ParseMdlInfo(x.res[users[2]].blob);
  const auto hie = tp::ParseHie(x.res[users[3]].blob);
  const auto codes = synth::conv::RefCodes();
  CKF(hie.size() == codes.size(), "%zu nodes", hie.size());
  CKF(geo.size() == hie.size() && info.size() == hie.size(), "%zu geo, %zu info", geo.size(), info.size());
  CK(x.OfType(tp::MakeFourcc("VBUF")).size() >= geo.size());
  // Every skinned object's palette starts with its owner's parent (object index = node id).
  int skinned = 0, rigid = 0, dummies = 0;
  size_t verts = 0;
  for (size_t o = 0; o < info.size() && o < geo.size(); ++o) {
    for (const auto& d : geo[o].decls) verts += d.head[5];
    if (info[o].palette.empty()) {
      if (geo[o].decls.size() == 1 && geo[o].decls[0].head[5] == 1) ++dummies;
      else ++rigid;
      continue;
    }
    ++skinned;
    int64_t parent_id = -2;
    for (const auto& nd : hie)
      if (nd.id == int64_t(o)) parent_id = nd.parent >= 0 ? hie[size_t(nd.parent)].id : -1;
    CKF(parent_id >= 0 && info[o].palette[0] == uint16_t(parent_id), "object %zu palette[0] %u, owner's parent %lld", o,
        info[o].palette[0], (long long)parent_id);
    CKF(info[o].palette.size() >= 2 && info[o].palette.size() <= 3, "object %zu palette size %zu", o,
        info[o].palette.size());
  }
  std::printf("synthetic: %d skinned objects, %d rigid, %d dummies, %zu vertices in the decls\n", skinned, rigid,
              dummies, verts);
  CKF(skinned >= 3, "%d skinned objects", skinned);  // pelvis (S1, A1), neck (N2), shoulder skin (3 bones)
  CKF(rigid >= 2, "%d rigid objects", rigid);       // spine, hip
  CKF(verts > size_t(dummies), "%zu vertices", verts);
  // The palettes, derived by hand from build_objects' rules on this fixture (node ids are RefCodes() positions: 0 lower
  // root 0x9002, 1 upper root 0x9000, 7 0x9008, 13 0x900E, 14 0x9100, 15 0x9101, 21 0x9108):
  //  - pelvis skin (owner 14, parent 1; weights first seen 0 then 1): [1, 0]. NOT [0, 1] (the root first, or the
  //    weights in first-seen or sorted order without the parent rule).
  //  - neck (owner 15, parent 1; weights 15 and 1, rule N2 owner slot): [1, 1]. NOT [1, 15] (owner kept in the list).
  //  - shoulder skin (owner 21, parent 13; weights first seen 7 then 1): [13, 7, 1]. NOT [13, 1, 7] (weights as a
  //    sorted map), NOT [7, 1] or [0, 7, 1] (no parent, or the root as parent).
  const int64_t neck = std::find(codes.begin(), codes.end(), 0x9101) - codes.begin();
  const int64_t sh2 = std::find(codes.begin(), codes.end(), 0x9108) - codes.begin();
  const int64_t pelvis = std::find(codes.begin(), codes.end(), 0x9100) - codes.begin();
  CK(info[size_t(pelvis)].palette == (std::vector<uint16_t>{1, 0}));
  CK(info[size_t(neck)].palette == (std::vector<uint16_t>{1, 1}));
  CK(info[size_t(sh2)].palette == (std::vector<uint16_t>{13, 7, 1}));
  const auto dir = tp::CatDir(a->cat);
  std::set<int> types;
  for (const auto& [t, v] : dir) types.insert(t);
  CK(types == (std::set<int>{2, 4, 6}));
  CK(a->cat.size() % 16 == 0);

  // The paths the synthetic suit is built to reach: rule S1 snapped vertices, the N2 ring, an accessory part (A1b).
  auto has = [&](const std::string& prefix) {
    return std::any_of(t1.lines.begin(), t1.lines.end(), [&](const std::string& l) { return l.rfind(prefix, 0) == 0; });
  };
  std::vector<std::string> snapped;
  for (const auto& l : t1.lines)
    if (l.rfind("K snapped ", 0) == 0) snapped.push_back(l);
  CKF(snapped.size() == 2 && snapped[0] != "K snapped 0" && snapped[1] != "K snapped 0", "%zu K snapped lines",
      snapped.size());
  CK(has("S accessory_parts"));
  CK(has("K owner_slot " + std::to_string(neck)));
  bool shifted = false;
  for (const auto& l : t1.lines)
    if (l.find(".attach owner=") != std::string::npos &&
        l.find("shift=0000000000000000,0000000000000000,0000000000000000") == std::string::npos)
      shifted = true;
  CK(shifted);
  CKF(t1.lines.size() > 100, "%zu trace lines", t1.lines.size());
  std::printf("synthetic: %zu trace lines, %s, %s\n", t1.lines.size(), snapped.empty() ? "-" : snapped[0].c_str(),
              snapped.size() > 1 ? snapped[1].c_str() : "-");
}

// ---------------------------------------------------------------------------------------------------------------
// level_gates, attach_frame
// ---------------------------------------------------------------------------------------------------------------

// attach_accessories: when every contact distance is NaN nothing beats np.inf, so the Python keeps best = (inf, -1, -1)
// and carries on with cand[-1] and pts[-1] (it does not fail). The synthetic accessory has NaN x on every vertex.
static void TestAttachNanFallback() {
  Group("attach_nan_fallback");
  const auto s = synth::conv::MakeInputs(true);
  const SuitInputs in = SynthInputs(s);
  ConvertFailure f;
  MemTrace t;
  const auto r = ConvertSuit(SynthSuit(), SynthGirl(), in, Level::kL0, nullptr, &t, &f);
  CKF(r.has_value(), "failed at %s: %s", f.stage.c_str(), f.what.c_str());
  int attach_lines = 0, nan_shift = 0;
  for (const auto& l : t.lines)
    if (l.find(".attach owner=") != std::string::npos) {
      ++attach_lines;
      // d = inf > far, so rule A1b shifts by (contact - NaN point): NaN components (0x7ff8... when the NaN is quiet)
      if (l.find("shift=") != std::string::npos && l.find("7ff8000000000000", l.find("shift=")) != std::string::npos)
        ++nan_shift;
    }
  CKF(attach_lines >= 1 && nan_shift >= 1, "%d attach lines, %d with a NaN shift", attach_lines, nan_shift);
  std::printf("attach_nan_fallback: converted; %d attach lines, %d with a NaN shift\n", attach_lines, nan_shift);
}

// ---------------------------------------------------------------------------------------------------------------
// Synthetic groups: attach's cloth branch with pin rows, with_spares, prep_head_physics, the channel merge
// ---------------------------------------------------------------------------------------------------------------

static void PutBe(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.at(at + i) = uint8_t(v >> (8 * (3 - i)));
}
static uint32_t GetBe(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b.at(at)) << 24 | uint32_t(b.at(at + 1)) << 16 | uint32_t(b.at(at + 2)) << 8 | b.at(at + 3);
}
static std::string ErrOf(const std::function<void()>& fn) {
  try {
    fn();
  } catch (const ConvertError& e) {
    return e.what();
  }
  return "";
}

// cloth_numeric: the cloth's census-chosen primitives against pins from the recipe's interpreter (numpy 1.26.3, CPython
// 3.12.1) on seeded synthetic inputs (no game data). Gemv3 is numpy's (N, 3) @ u for N > 1 (cblas_dgemv): 24 rows where
// the plain sum and/or the fused chain from 0 (NpDotFused) differ from numpy, flagged per row; the N == 1 matmul is
// numpy's dot path, a fused chain from 0, which differs from Gemv3 on the first row. MathDist is math.dist: 16 pairs
// where the plain sqrt of the sum of squares differs. PySetOrder is list(set(l)): 6 lists, among them one past the
// 8-slot table (resizes), one with duplicates and one with colliding hashes; the four-key case must agree with
// catx::SmallSetOrder.
static void TestClothNumeric() {
  Group("cloth_numeric");
  namespace cl = doa4::suitconv::cloth;
  using V = core::Vec3;
  const V u{0x1.15cb42210b411p-2, -0x1.2344989264c58p-1, -0x1.8d82b0d1af718p-1};
  struct G {
    V a;
    double want;
  };
  const G gemv[] = {
      {{-0x1.2c8477f715712p+0, -0x1.f87d0207fb342p+1, 0x1.85a8ad625edffp+0}, 0x1.7be01472b74bbp-1},  // 1 1
      {{0x1.71842487be1c5p+2, 0x1.5cf2f9d03f260p+2, -0x1.9e216bdb1698ap+0}, -0x1.1e2d9a353d963p-2},  // 1 1
      {{0x1.312cc3ca20ac8p+1, 0x1.a5eff979878bep+2, 0x1.a36d517954a74p+1}, -0x1.69751de3a9748p+2},  // 1 1
      {{0x1.3ab4cba1cc298p-3, -0x1.b45ea8a916d94p-1, 0x1.ece23f1490832p-1}, -0x1.c45408af2b8a2p-3},  // 1 1
      {{-0x1.95e8a401578d6p+1, -0x1.9c6f43a507ae4p-2, 0x1.605d562cfb15ep+0}, -0x1.b325b1f207573p+0},  // 1 1
      {{-0x1.614af6402dd23p-12, -0x1.f2901c5b72b00p-11, 0x1.6082b3066edc9p-11}, -0x1.2fdb9578f2a1cp-14},  // 1 1
      {{0x1.f6871740878acp-12, -0x1.78960f19cf84cp-11, 0x1.933efda547706p-11}, -0x1.eada2cf76a1dcp-15},  // 1 1
      {{-0x1.f07cf2f0f1462p-13, 0x1.1caa1f6fd83dfp-13, 0x1.731fd971ef27fp-14}, -0x1.b8b2a4850cf21p-13},  // 1 1
      {{-0x1.25934ccd70a13p-5, -0x1.3b22a71c039edp-5, 0x1.eaf9745a30efap-6}, -0x1.6bd74f04a11b1p-7},  // 1 1
      {{0x1.1d219c2e9d360p-12, -0x1.2ac44e2faecbdp-14, 0x1.c009fdb0c1cb0p-12}, -0x1.c8050b8851393p-13},  // 1 1
      {{-0x1.51d6a69c70b70p-3, 0x1.78848f46b4160p-1, -0x1.3ec9df0599960p-1}, 0x1.4cb0b3d19735fp-6},  // 1 1
      {{-0x1.8c1ae5923f7c0p-3, -0x1.c756f6abca598p-1, 0x1.25f5b6e490788p-1}, 0x1.f8d2cb0774450p-8},  // 1 1
      {{-0x1.2a1efc404c4fap-1, 0x1.94a5215a7571cp+1, -0x1.85a522a0dcb06p+2}, 0x1.629ded0f49159p+1},  // 1 0
      {{-0x1.20962e0031490p-4, -0x1.84b7d4c903454p-2, 0x1.6b29dd7983ad2p-1}, -0x1.6a593cf4778b0p-2},  // 1 0
      {{-0x1.b561d35943496p-1, 0x1.59861fb40c170p-2, 0x1.7f9a8ff23c708p-2}, -0x1.6dd92c605865ep-1},  // 1 0
      {{-0x1.7084f6eb7fcb8p-4, -0x1.a2af224e472cdp-7, -0x1.796971b19c535p-4}, 0x1.bda29e24001a8p-5},  // 1 0
      {{-0x1.71d3c9989919ap-12, 0x1.01e95284cf4d5p-10, -0x1.040620f5a7af8p-10}, 0x1.e1378f1488fa3p-14},  // 1 0
      {{0x1.d40b2118fb0e8p-11, -0x1.9567d608930b4p-12, 0x1.ef0b345cb3ec1p-11}, -0x1.1c1e70cd3759fp-12},  // 1 0
      {{0x1.5851f5009dbaep+2, 0x1.4ad50b0b2b338p+2, 0x1.18fa216260768p+2}, -0x1.38f19014560f4p+2},  // 0 1
      {{-0x1.cc031099184a8p-11, 0x1.32a49b53ffe04p-14, -0x1.f0c39aeffeef4p-11}, 0x1.de2a41bae2774p-12},  // 0 1
      {{0x1.9eb7b84cd7abep-1, 0x1.430fe2e63e78cp-1, -0x1.05067164cd8e0p-5}, -0x1.d4e64c680445ep-4},  // 0 1
      {{-0x1.d08262bc5c772p+1, -0x1.e737df0e50e60p+1, 0x1.89ef12f8d8b77p+2}, -0x1.cc88cd16553e4p+1},  // 0 1
      {{0x1.1bdc049d02693p-5, 0x1.d50c2931ae1d0p-5, 0x1.2c6f4bd8ffde8p-4}, -0x1.482ace80aad76p-4},  // 0 1
      {{0x1.b794353f32880p-8, 0x1.ff19146d46fc0p-7, -0x1.1ddbffbba1d67p-8}, -0x1.e0a49f621a1c2p-9},  // 0 1
  };
  int gemv_ok = 0, plain_differs = 0, fused_differs = 0;
  for (const G& r : gemv) {
    const double got = cl::Gemv3(r.a, u);
    CKF(std::bit_cast<uint64_t>(got) == std::bit_cast<uint64_t>(r.want), "Gemv3 %a, want %a", got, r.want);
    gemv_ok += got == r.want;
    plain_differs += ((r.a[0] * u[0] + r.a[1] * u[1]) + r.a[2] * u[2]) != r.want;
    fused_differs += num::NpDotFused(r.a.data(), u.data(), 3) != r.want;
  }
  CKF(plain_differs == 18 && fused_differs == 18, "vacuity: plain differs on %d, fused chain on %d (18 each)", plain_differs,
      fused_differs);
  const double one = 0x1.7be01472b74bdp-1;  // np.array([row 0]) @ u: the scalar_out path (ddot)
  CK(num::NpDotFused(gemv[0].a.data(), u.data(), 3) == one && cl::Gemv3(gemv[0].a, u) != one);
  struct Dd {
    V p, q;
    double want;
  };
  const Dd dist[] = {
      {{0x1.269571d43fcb8p-2, -0x1.805978b89b930p-2, -0x1.77835b2b602a2p+0}, {-0x1.a896053698ffcp-1, 0x1.a1f835920c5e2p+0, 0x1.7509089b7ed88p-1}, 0x1.96c8ce6a1ae9dp+1},
      {{0x1.4f5f396601dd0p-1, -0x1.b8eac0e46e728p+0, 0x1.b033f27873de8p+0}, {0x1.d44028e2d44d6p+0, -0x1.37f861a538c6ep+0, -0x1.f967353b34764p-1}, 0x1.7b7e4657a4bc2p+1},
      {{-0x1.616e94debf068p-1, 0x1.7ddd6b60a5ec0p-1, -0x1.4c3675f8af9b4p+0}, {-0x1.61ae42d3f3238p-1, 0x1.7d9b1f9ad0365p-1, -0x1.4c19114117ce6p+0}, 0x1.b475ff1dc8189p-11},
      {{0x1.d0e13eb8c5940p-3, -0x1.67e26391c40c2p+0, 0x1.b4b798cbf5df8p-2}, {0x1.13d867b6d382cp+0, 0x1.7ce542a7bf100p-2, 0x1.12ebd246b0948p-1}, 0x1.f94dcaaec9194p+0},
      {{0x1.25128590e46f8p-1, -0x1.c61ebe0175cb2p+0, -0x1.8f087559093e0p-2}, {0x1.9bef8583fa112p+0, 0x1.c3af235092c62p+0, -0x1.06cb7b7581344p+0}, 0x1.deee4d0674c9fp+1},
      {{-0x1.4f1e80ee14066p+0, 0x1.514f4428b92d4p+0, 0x1.49b35b0bfcd46p+0}, {-0x1.af573c477fa00p-4, 0x1.9d0434fad092ap+0, -0x1.55d1062e5dd98p-2}, 0x1.05451621714f6p+1},
      {{-0x1.894348ff3b4c8p+0, -0x1.853155adadba8p-1, 0x1.9f4a73aff835ep+0}, {0x1.c036745951976p+0, 0x1.4501a3fda1000p-2, 0x1.ac04e37ba3c32p+0}, 0x1.bad041d39c57bp+1},
      {{-0x1.d15b4e3df1710p+0, -0x1.1c4fb8b9ae688p-2, 0x1.b595c228002c0p-1}, {0x1.94cecd6a4b7c0p-1, -0x1.128232bbbf858p-2, 0x1.a865779a651dcp+0}, 0x1.5d59bb853ef75p+1},
      {{0x1.2f39ce3a3dd9ap+0, 0x1.5013129dcae00p-6, 0x1.05950d23e2440p-4}, {0x1.2f60fa0ce9b58p+0, 0x1.4729690195016p-6, 0x1.0796f51e4ce75p-4}, 0x1.ef8c8b105f2b2p-11},
      {{-0x1.fc3d4c1971450p-3, -0x1.99ee9af6553dcp-1, 0x1.beeac11299c4ep+0}, {-0x1.9df44cbb2ae82p+0, -0x1.98870b86b1e3ep+0, -0x1.e236f06606c38p-2}, 0x1.5ca92b738fef1p+1},
      {{0x1.6e5fc7f6c5414p+0, 0x1.3727eca746dc0p-4, -0x1.932d57b023dbcp-1}, {0x1.a4a27fd381542p+0, -0x1.2219501944000p-13, -0x1.c5ccfc03771f4p-1}, 0x1.f7b671f50a6f8p-3},
      {{-0x1.612a52f466690p-2, -0x1.a54bc52c67a7cp+0, 0x1.c7d1e475a0b50p-1}, {-0x1.60cdd1d85e3c2p-2, -0x1.a53dbf8e80e01p+0, 0x1.c7e5ed3b3f3acp-1}, 0x1.cd729f1fb5c8fp-12},
      {{0x1.6da3ea6cbae00p-4, 0x1.8e06216852048p-2, -0x1.3673a3864f288p-2}, {0x1.94b72dbe1a782p+0, 0x1.b84bba40ea11ep+0, -0x1.d94bdc1489bc8p+0}, 0x1.43776f87c3e2fp+1},
      {{-0x1.49b46cba0ffb6p+0, 0x1.091561f713f00p-5, 0x1.23439ac25ef24p-1}, {0x1.451c877ffbb04p+0, 0x1.7d7c2d15bd3b0p-2, -0x1.948be524afdd4p+0}, 0x1.add82bc613012p+1},
      {{0x1.4b9f6cb10c930p+0, 0x1.f1f74ea4b3d88p-2, 0x1.50f0e2887e2bap+0}, {0x1.655f70d1f479ap+0, -0x1.5d46677261090p-3, -0x1.f1f36b0ef49a0p-1}, 0x1.310e3ef92f28ep+1},
      {{-0x1.72d9b36b47b64p+0, -0x1.ff39c56680a48p-2, 0x1.f975bda57545cp-1}, {-0x1.72c181810ce0ep+0, -0x1.00103996f6880p-1, 0x1.f9b6cae5dc6aep-1}, 0x1.19f7d46e9c330p-10},
  };
  int dist_ok = 0, dist_plain_differs = 0;
  for (const Dd& r : dist) {
    const double got = cl::MathDist(r.p, r.q);
    CKF(std::bit_cast<uint64_t>(got) == std::bit_cast<uint64_t>(r.want), "MathDist %a, want %a", got, r.want);
    dist_ok += got == r.want;
    const double dx = r.p[0] - r.q[0], dy = r.p[1] - r.q[1], dz = r.p[2] - r.q[2];
    dist_plain_differs += std::sqrt((dx * dx + dy * dy) + dz * dz) != r.want;
  }
  CKF(dist_plain_differs == 16, "vacuity: plain differs on %d of 16", dist_plain_differs);
  CK(cl::MathDist(V{0, 0, 0}, V{0, 0, 0}) == 0.0 && std::isnan(cl::MathDist(V{0, NAN, 0}, V{0, 0, 0})));
  CK(cl::MathDist(V{0, 0, 0}, V{3, 4, 12}) == 13.0 && std::isinf(cl::MathDist(V{0, INFINITY, 0}, V{0, 0, NAN})));
  struct Sc {
    std::vector<int64_t> in, want;
  };
  const Sc sets[] = {
      {{5, 13, 8, 1}, {8, 1, 13, 5}},
      {{60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60}},
      {{0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960, 1024, 1088, 1152, 1216}, {0, 256, 128, 384, 512, 1024, 1152, 640, 768, 896, 64, 192, 320, 448, 576, 1088, 704, 832, 960, 1216}},
      {{0, 37, 74, 111, 148, 185, 11, 48, 85, 122, 159, 196, 22, 59, 96, 133, 170, 207, 33, 70, 107, 144, 181, 7, 44, 81, 118, 155, 192, 18, 55, 92, 129, 166, 203, 29, 66, 103, 140, 177, 3, 40, 77, 114, 151, 188, 14, 51, 88, 125, 162, 199, 25, 62, 99, 136, 173, 210, 36, 73, 110, 147, 184, 10, 47, 84, 121, 158, 195, 21, 58, 95, 132, 169, 206, 32, 69, 106, 143, 180, 6, 43, 80, 117, 154, 191, 17, 54, 91, 128, 165, 202, 28, 65, 102, 139, 176, 2, 39, 76, 113, 150, 187, 13, 50, 87, 124, 161, 198, 24, 61, 98, 135, 172, 209, 35, 72, 109, 146, 183, 9, 46, 83, 120, 157, 194, 20, 57, 94, 131, 168, 205, 31, 68, 105, 142, 179, 5, 42, 79, 116, 153, 190, 16, 53, 90, 127, 164, 201, 27}, {0, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 16, 17, 18, 20, 21, 22, 24, 25, 27, 28, 29, 31, 32, 33, 35, 36, 37, 39, 40, 42, 43, 44, 46, 47, 48, 50, 51, 53, 54, 55, 57, 58, 59, 61, 62, 65, 66, 68, 69, 70, 72, 73, 74, 76, 77, 79, 80, 81, 83, 84, 85, 87, 88, 90, 91, 92, 94, 95, 96, 98, 99, 102, 103, 105, 106, 107, 109, 110, 111, 113, 114, 116, 117, 118, 120, 121, 122, 124, 125, 127, 128, 129, 131, 132, 133, 135, 136, 139, 140, 142, 143, 144, 146, 147, 148, 150, 151, 153, 154, 155, 157, 158, 159, 161, 162, 164, 165, 166, 168, 169, 170, 172, 173, 176, 177, 179, 180, 181, 183, 184, 185, 187, 188, 190, 191, 192, 194, 195, 196, 198, 199, 201, 202, 203, 205, 206, 207, 209, 210}},
      {{5, 7, 12, 18, 35, 36, 40, 45, 46, 47, 53, 55, 59, 62, 65, 68, 69, 72, 77, 78, 79, 81, 90, 99, 102, 105, 107, 110, 113, 123, 124, 130, 137, 138, 140, 141, 144, 147, 155, 156, 164, 170, 172, 175, 178, 180, 183, 185, 190, 193, 201, 203, 214, 225, 231, 232, 234, 238, 241, 242, 251, 260, 264, 267, 272, 273, 277, 281, 287, 300, 301, 302, 304, 307, 315, 318, 323, 327, 329, 332, 336, 339, 341, 350, 351, 360, 382, 388, 390, 391, 392, 393, 395, 397, 398, 402, 403, 405, 427, 430, 434, 435, 436, 438, 440, 442, 444, 448, 459, 462, 465, 469, 474, 476, 482, 484, 487, 488, 490, 499}, {5, 7, 12, 18, 35, 36, 40, 45, 46, 47, 53, 55, 59, 62, 65, 68, 69, 72, 77, 78, 79, 81, 90, 99, 102, 105, 107, 110, 113, 123, 124, 130, 137, 138, 140, 141, 144, 147, 155, 156, 164, 170, 172, 175, 178, 180, 183, 185, 190, 193, 201, 203, 214, 225, 231, 232, 234, 238, 241, 242, 251, 260, 264, 267, 272, 273, 277, 281, 287, 300, 301, 302, 304, 307, 315, 318, 323, 327, 329, 332, 336, 339, 341, 350, 351, 360, 382, 388, 390, 391, 392, 393, 395, 397, 398, 402, 403, 405, 427, 430, 434, 435, 436, 438, 440, 442, 444, 448, 459, 462, 465, 469, 474, 476, 482, 484, 487, 488, 490, 499}},
      {{3, 3, 11, 19, 3, 27, 35, 43, 51, 59, 67}, {3, 35, 67, 59, 11, 43, 19, 51, 27}},
  };
  int sets_ok = 0;
  for (const Sc& c : sets) {
    const auto got = cl::PySetOrder(c.in);
    CKF(got == c.want, "set of %zu keys: order differs", c.in.size());
    sets_ok += got == c.want;
  }
  CK(cl::PySetOrder(sets[0].in) == catx::SmallSetOrder(sets[0].in));
  std::printf("cloth_numeric: %d/24 Gemv3 rows, %d/16 MathDist pairs, %d/6 set orders equal to the interpreter's\n", gemv_ok,
              dist_ok, sets_ok);
}

// unstick_rounds: fit_cloth_chunk(unstick=True, fit=False) on a hand-built chunk (no game data). Layout (hex): cloth
// count 1 at 0x18, table at 0x20 -> C = 0x40; C+0x24 -> GRID_DATA at 0x80 (nodes at 0xC0, two), C+0x2C -> FIX_GRID at
// 0x140 (one pin, node 1), C+0x38 -> a chain of 7 spheres (type 2, bone 0, radius 0.1) at 0x1C0 + 0x70 * i, listed S6
// first and S0 last, sphere Sk centred at (0.103 * k, 0, 0). Free node 0 starts at (0.002, 0, 0), inside S0 only. A push
// moves it out along +x to the sphere's surface plus the 0.004 margin, centre + 0.104, which is 0.001 past the next
// centre: inside the next sphere only. Each of the 6 rounds walks the chain S6..S0 once, so each round advances it one
// sphere: after 6 rounds it is at S5's centre + 0.104 (0.619 in float32 arithmetic: 0x1.3ced9p-1), NOT S4's (5 rounds,
// 0x1.083126p-1). Both values from the recipe's own fit_cloth_chunk in its interpreter (5 rounds by patching range from
// outside). The pinned node 1 and every other byte stay as they were.
static void TestUnstickRounds() {
  Group("unstick_rounds");
  namespace cl = doa4::suitconv::cloth;
  std::vector<uint8_t> b(0x1C0 + 0x70 * 7, 0);
  auto f32 = [&](size_t at, float v) { PutBe(b, at, std::bit_cast<uint32_t>(v)); };
  PutBe(b, 0x18, 1);
  PutBe(b, 0x1C, 0x20);
  PutBe(b, 0x20, 0x40);
  PutBe(b, 0x40 + 0x24, 0x40);
  PutBe(b, 0x40 + 0x2C, 0x100);
  PutBe(b, 0x40 + 0x38, 0x180);
  PutBe(b, 0x80 + 0x20, 0x40);
  PutBe(b, 0x80 + 0x24, 2);
  f32(0xC0, 0.002f);
  for (int c = 0; c < 3; ++c) f32(0xC0 + 48 + 4 * c, 0.5f);
  PutBe(b, 0x140 + 0x18, 0x20);
  PutBe(b, 0x140 + 0x1C, 1);
  PutBe(b, 0x160 + 0x14, 0x40 + 48);
  for (int i = 0; i < 7; ++i) {
    const size_t at = 0x1C0 + 0x70 * size_t(i);
    PutBe(b, at, 2);
    PutBe(b, at + 8, i + 1 < 7 ? uint32_t(0x70 * (i + 1)) : 0u);
    f32(at + 0x30, float(0.103 * (6 - i)));
    f32(at + 0x3C, 0.1f);
  }
  core::PyDict<int64_t, core::Vec3> bones;
  bones.Set(0, core::Vec3{0, 0, 0});
  std::vector<uint8_t> out;
  num::FpEnvGuard env;
  try {
    out = cl::FitClothChunk(b, bones, core::Vec3{0, 0, 0}, {}, {}, true, false, 0.0);
  } catch (const ConvertError& e) {
    CKF(false, "FitClothChunk threw: %s", e.what());
    return;
  }
  CK(out.size() == b.size());
  if (out.size() != b.size()) return;
  const float x = std::bit_cast<float>(GetBe(out, 0xC0));
  CKF(x == 0x1.3ced9p-1f, "node 0 x = %a, the recipe's 6 rounds give 0x1.3ced9p-1", double(x));
  CK(x != 0x1.083126p-1f);  // NOT the 5-round position
  CK(GetBe(out, 0xC4) == 0 && GetBe(out, 0xC8) == 0);
  bool rest_same = true;
  for (size_t i = 0; i < b.size(); ++i)
    if ((i < 0xC0 || i >= 0xCC) && out[i] != b[i]) rest_same = false;
  CK(rest_same);
  std::printf("unstick_rounds: free node walked through 6 of 7 spheres to %a\n", double(x));
}

static void TestLevelGates() {
  Group("level_gates");
  const auto s = synth::conv::MakeInputs();
  const SuitInputs in = SynthInputs(s);
  SuitRecipe suit = SynthSuit();
  const GirlRecipe girl = SynthGirl();
  {  // no M1 feature is refused by a level gate any more: every flag of the recipe, one at a time and all together
    const auto no_level_failure = [&](const SuitRecipe& sr, const GirlRecipe& gr, const char* what) {
      ConvertFailure f;
      const auto r = ConvertSuit(sr, gr, in, Level::kM1, nullptr, nullptr, &f);
      CKF(r || f.stage != "level", "%s: %s / %s", what, f.stage.c_str(), f.what.c_str());
    };
    SuitRecipe a = suit;
    a.gen_cloth = true;
    no_level_failure(a, girl, "gen_cloth");  // (the synthetic suit has no skirt: it fails at gen_prep, not at a gate)
    a = suit;
    a.port_cloth = true;
    no_level_failure(a, girl, "port_cloth");
    a = suit;
    a.fit_leg_colliders = true;
    no_level_failure(a, girl, "fit_leg_colliders");
    a = suit;
    a.unstick_cloth = true;
    no_level_failure(a, girl, "unstick_cloth");
    a = suit;
    a.back_margin = 0.012;
    no_level_failure(a, girl, "back_margin");
    a = suit;
    a.sink_hamstrings = 0.008;
    no_level_failure(a, girl, "sink_hamstrings");
    GirlRecipe g = girl;
    g.hair_spares = true;
    no_level_failure(suit, g, "hair_spares");
    g = girl;
    g.head_physics = true;
    no_level_failure(suit, g, "head_physics");
    SuitRecipe all = suit;
    all.gen_cloth = all.port_cloth = all.fit_leg_colliders = all.unstick_cloth = true;
    all.back_margin = 0.012;
    all.sink_hamstrings = 0.008;
    g = girl;
    g.hair_spares = g.head_physics = true;
    no_level_failure(all, g, "all flags");
  }
  {  // the authored cloth and its options are ported: no longer refused at M1 by the gate
    SuitRecipe s2 = suit;
    s2.port_cloth = true;
    s2.fit_leg_colliders = true;
    s2.unstick_cloth = true;
    s2.back_margin = 0.012;
    s2.sink_hamstrings = 0.008;
    ConvertFailure f;
    const auto r = ConvertSuit(s2, girl, in, Level::kM1, nullptr, nullptr, &f);
    CKF(r || f.stage != "level", "%s / %s", f.stage.c_str(), f.what.c_str());
  }
  {  // the hair spares and head physics are ported: a girl with either flag is no longer refused at M1 by the gate
    GirlRecipe g2 = girl;
    g2.hair_spares = true;
    g2.head_physics = true;
    ConvertFailure f;
    const auto r = ConvertSuit(suit, g2, in, Level::kM1, nullptr, nullptr, &f);
    CKF(r || f.stage != "level", "%s / %s", f.stage.c_str(), f.what.c_str());
  }
  {
    CancelToken tok;
    tok.Cancel();
    ConvertFailure f;
    MemTrace t;
    const auto r = ConvertSuit(suit, girl, in, Level::kL0, &tok, &t, &f);
    CKF(!r && f.stage == "cancelled", "%s / %s", f.stage.c_str(), f.what.c_str());
    CK(t.lines.empty());
  }
  {
    // A broken input fails at the stage that reads it, caught at the boundary.
    SuitInputs bad = in;
    bad.ref_cat = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{'C', 'A', 'T', 0});
    ConvertFailure f;
    const auto r = ConvertSuit(suit, girl, bad, Level::kL0, nullptr, nullptr, &f);
    CKF(!r && f.stage == "main_chain", "%s / %s", f.stage.c_str(), f.what.c_str());
    SuitInputs none = in;
    none.suit_tpr = nullptr;
    const auto r2 = ConvertSuit(suit, girl, none, Level::kL0, nullptr, nullptr, &f);
    CKF(!r2 && f.stage == "load", "%s / %s", f.stage.c_str(), f.what.c_str());
  }
}

static void TestAttachFrame() {
  Group("attach_frame");
  std::vector<uint8_t> cat(40, 7);
  const auto out = catx::Attach(cat, nullptr, nullptr, nullptr, nullptr);
  CK(out.size() == 48);
  CK(std::equal(cat.begin(), cat.end(), out.begin()));
  CK(out[40] == 0 && out[47] == 0);
  const std::vector<uint8_t> empty, one{1};
  CK(catx::Attach(cat, &empty, &empty, nullptr, &empty).size() == 48);  // empty = falsy
  auto throws = [&](auto fn, const char* text) {
    try {
      fn();
    } catch (const ConvertError& e) {
      return std::string(e.what()).find(text) != std::string::npos;
    }
    return false;
  };
  {  // the MPV branch: appended at the next 16-byte boundary, +0xC points at it, the end is aligned again
    const auto with = catx::Attach(cat, &one, nullptr, nullptr, nullptr);
    CK(with.size() == 64);
    CK(with[48] == 1 && with[49] == 0 && with[63] == 0);
    CK((uint32_t(with[0xC]) << 24 | uint32_t(with[0xD]) << 16 | uint32_t(with[0xE]) << 8 | with[0xF]) == 48);
    bool same = true;
    for (size_t i = 0; i < cat.size(); ++i)
      if ((i < 0xC || i > 0xF) && with[i] != cat[i]) same = false;
    CK(same);
    // a 16-byte multiple needs no padding before the block
    std::vector<uint8_t> c32(32, 9);
    const auto w2 = catx::Attach(c32, &one, nullptr, nullptr, nullptr);
    CK(w2.size() == 48 && w2[32] == 1 && w2[0xF] == 32);
  }
  {  // the cloth branch: appended at the next 16-byte boundary (48), +0x14 points at it (no MPV: no pin rows to move)
    const auto cl = catx::Attach(cat, nullptr, &one, nullptr, nullptr);
    CK(cl.size() == 64 && cl[48] == 1 && GetBe(cl, 0x14) == 48);
    CK(throws([&] { catx::Attach(cat, &one, &one, nullptr, nullptr); }, "read past end"));  // a 1-byte MPV has no MPP
  }
  CK(throws([&] { catx::Attach(cat, nullptr, nullptr, &empty, nullptr); }, "read past end"));  // a donor with no header
  {  // the head physics branch: at 48, +8 points at it
    const auto hp1 = catx::Attach(cat, nullptr, nullptr, nullptr, &one);
    CK(hp1.size() == 64 && hp1[48] == 1 && GetBe(hp1, 8) == 48);
  }
  std::vector<uint8_t> b(17, 1);
  CK(catx::Al(b, 16) == 32);
  CK(catx::Al(b, 16) == 32);
}

// _add_apa on a hand-built donor. The donor's layout (offsets in hex): header with the directory offset 0xC0 at +4;
// rule data 0x20..0x30; map data 0x30..0x40; a rule directory at 0x40 (one rule whose offset 0x20 is at +4); entry 0 at
// 0x50 {nmap 1, mp 0x30, nrules 1, rp 0x40}; entry 1 at 0x60 {nmap 1, mp 0x38, nrules 0, rp 0}; the apa block at 0x70
// {'apa', count 2, entries 0x50 and 0x60}; the directory at 0xC0 lists a type-2 entry (0x10) and the type-5 entry (0x70).
// start = min of every offset the block names = 0x20 and end = 0x70 + 8 + 4 * 2 = 0x80, so 96 bytes are copied. The
// target .cat is 48 bytes with its directory (one type-2 entry) at 0x20, so the block lands at 48 and every non-zero
// offset field moves by 48 - 0x20 = 16. Expected values are written out by hand, not computed by the port.
static void TestAttachApa() {
  Group("attach_apa_synthetic");
  auto put = [](std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * (3 - i)));
  };
  std::vector<uint8_t> d(0xD8, 0);
  for (size_t i = 0x20; i < 0x30; ++i) d[i] = uint8_t(0xA0 + (i & 15));  // rule data
  for (size_t i = 0x30; i < 0x40; ++i) d[i] = uint8_t(0xB0 + (i & 15));  // map data
  put(d, 4, 0xC0);
  put(d, 0x40, 7);
  put(d, 0x44, 0x20);
  put(d, 0x50, 1), put(d, 0x54, 0x30), put(d, 0x58, 1), put(d, 0x5C, 0x40);
  put(d, 0x60, 1), put(d, 0x64, 0x38), put(d, 0x68, 0), put(d, 0x6C, 0);
  put(d, 0x70, 0x617061), put(d, 0x74, 2), put(d, 0x78, 0x50), put(d, 0x7C, 0x60);
  d[0xC0] = 1, d[0xC2] = 2, put(d, 0xC4, 0x10);
  d[0xC8] = 1, d[0xCA] = 5, put(d, 0xCC, 0x70);
  std::vector<uint8_t> cat(0x30, 3);
  put(cat, 4, 0x20);
  for (size_t i = 0x20; i < 0x30; ++i) cat[i] = 0;
  cat[0x20] = 1, cat[0x22] = 2, put(cat, 0x24, 0x10);
  const auto out = catx::Attach(cat, nullptr, nullptr, &d, nullptr);
  auto Be32 = [&](const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b.at(at)) << 24 | uint32_t(b.at(at + 1)) << 16 | uint32_t(b.at(at + 2)) << 8 | b.at(at + 3);
  };
  CKF(out.size() == 176, "size %zu", out.size());
  if (out.size() != 176) return;
  // the first 48 bytes are the target, except the directory offset
  bool head_same = true;
  for (size_t i = 0; i < 0x30; ++i)
    if ((i < 4 || i > 7) && out[i] != cat[i]) head_same = false;
  CK(head_same);
  CK(Be32(out, 4) == 0x90);
  // the copied block: d[0x20:0x80] at 48, with the offset fields moved by 16
  std::vector<uint8_t> want(d.begin() + 0x20, d.begin() + 0x80);
  auto setw = [&](size_t donor_at, uint32_t v) { put(want, donor_at - 0x20, v); };
  setw(0x78, 0x60);  // table entries
  setw(0x7C, 0x70);
  setw(0x54, 0x40);  // entry 0: mp, rp
  setw(0x5C, 0x50);
  setw(0x64, 0x48);  // entry 1: mp (its rp field stays 0)
  setw(0x44, 0x30);  // the rule directory's offset
  CK(std::equal(want.begin(), want.end(), out.begin() + 48));
  // the new directory at 0x90: the old entry, the type-5 entry at 0x70 + 16, the terminator; then the final alignment
  CK(out[0x90] == 1 && out[0x92] == 2 && Be32(out, 0x94) == 0x10);
  CK(out[0x98] == 1 && out[0x99] == 0 && out[0x9A] == 5 && out[0x9B] == 0 && Be32(out, 0x9C) == 0x80);
  bool tail_zero = true;
  for (size_t i = 0xA0; i < 0xB0; ++i) tail_zero = tail_zero && out[i] == 0;
  CK(tail_zero);
  // a donor with no type-5 entry is a KeyError; a block without the 'apa' magic is the assert
  std::vector<uint8_t> no5 = d;
  no5[0xCA] = 3;
  auto msg = [&](const std::vector<uint8_t>& donor) {
    try {
      catx::Attach(cat, nullptr, nullptr, &donor, nullptr);
    } catch (const ConvertError& e) {
      return std::string(e.what());
    }
    return std::string();
  };
  CKF(msg(no5) == "KeyError: 5", "%s", msg(no5).c_str());
  std::vector<uint8_t> bad = d;
  bad[0x71] = 0;
  CKF(msg(bad) == "AssertionError: ('not an apa block', '0x70')", "%s", msg(bad).c_str());
  std::printf("attach_apa_synthetic: block of %zu bytes relocated by 16, directory rewritten\n", want.size());
}

// attach's cloth branch. cat 32 bytes (already aligned); MPV 0xE0 bytes whose channel 4 (the offset table entry at
// mpv + 52) points at the MPP at mpv + 0x40; the MPP says (+0x40, +0x44) pin block at +0x50 and 2 rows, so the rows are
// at mpv + 0x40 + 0x50 + 40 * i = mpv + 0x90 and mpv + 0xB8. Worked by hand: the MPV lands at 32 (+0xC = 32) and ends at
// 256, so the cloth lands at 256 (+0x14 = 256) and mpp = 32 + 0x40 = 96. A row holds rel (signed) and becomes
// at + rel - mpp: row 0 had 256 -> 256 + 256 - 96 = 416; row 1 had -8 -> 256 - 8 - 96 = 152. Row 0 sits at file offset
// 32 + 0x90 = 176 and row 1 at 32 + 0xB8 = 216. The cloth's 20 bytes end at 276, so the file ends at 288.
static void TestAttachClothPins() {
  Group("attach_cloth_pins");
  const std::vector<uint8_t> cat(32, 3);
  std::vector<uint8_t> mpv(0xE0, 0x11);
  PutBe(mpv, 0x34, 0x40);
  PutBe(mpv, 0x80, 0x50);
  PutBe(mpv, 0x84, 2);
  PutBe(mpv, 0x90, 256);
  PutBe(mpv, 0xB8, 0xFFFFFFF8u);
  const std::vector<uint8_t> cloth(20, 0xCC);
  const auto out = catx::Attach(cat, &mpv, &cloth, nullptr, nullptr);
  CKF(out.size() == 288, "size %zu", out.size());
  if (out.size() != 288) return;
  CK(GetBe(out, 0xC) == 32);
  CK(GetBe(out, 0x14) == 256);
  CKF(GetBe(out, 176) == 416, "row 0: %u", GetBe(out, 176));
  CKF(GetBe(out, 216) == 152, "row 1: %u", GetBe(out, 216));
  // everything else is the inputs: the cat, the MPV with the two rows and +0xC / +0x14 aside, the cloth, zero padding
  std::vector<uint8_t> want(288, 0);
  std::copy(cat.begin(), cat.end(), want.begin());
  std::copy(mpv.begin(), mpv.end(), want.begin() + 32);
  std::copy(cloth.begin(), cloth.end(), want.begin() + 256);
  PutBe(want, 0xC, 32);
  PutBe(want, 0x14, 256);
  PutBe(want, 176, 416);
  PutBe(want, 216, 152);
  CK(out == want);
  // a pin block with no rows moves nothing
  std::vector<uint8_t> mpv0 = mpv;
  PutBe(mpv0, 0x84, 0);
  const auto out0 = catx::Attach(cat, &mpv0, &cloth, nullptr, nullptr);
  CK(GetBe(out0, 176) == 256 && GetBe(out0, 216) == 0xFFFFFFF8u);
  // a cloth with no MPV: +0x14 only, no relocation (cat 40 bytes -> cloth at 48 -> 5 bytes -> 64)
  const std::vector<uint8_t> cat40(40, 7), five(5, 1);
  const auto nm = catx::Attach(cat40, nullptr, &five, nullptr, nullptr);
  CK(nm.size() == 64 && GetBe(nm, 0x14) == 48 && nm[48] == 1 && nm[52] == 1 && nm[53] == 0);
  // the head physics section lands before the MPV: section at 48 (+8 = 48, 20 bytes), MPV at 80 (+0xC = 80), end 96
  const std::vector<uint8_t> sec(20, 9), one{1};
  const auto hp = catx::Attach(cat40, &one, nullptr, nullptr, &sec);
  CKF(hp.size() == 96 && GetBe(hp, 8) == 48 && GetBe(hp, 0xC) == 80 && hp[48] == 9 && hp[67] == 9 && hp[80] == 1,
      "hp %zu", hp.size());
  std::printf("attach_cloth_pins: 2 pin rows relocated (416, 152), cloth at 256, file 288 bytes\n");
}

// A reference .cat for with_spares, laid out by hand (offsets in hex). Header: +0x14 = 0x20. MULTI_CLOTH at 0x20 (its
// first 0x50 bytes are 0x20..0x6F): +0x18 (at 0x38) = 0xAABBCCDD, +0x20 (at 0x40) = 0x60, so the SPARE_CHUNK is at
// s = 0x80. The chunk: count (+0x1C, at 0x9C) = 2, table offset (+0x20, at 0xA0) = 0x30, the two block offsets at
// s + 0x30 = 0xB0 (0x40 and 0x60). Block 0 at s + 0x40 = 0xC0 has size (+0x10, at 0xD0) 0x20 and extra (+0x24, at 0xE4) 0:
// it ends at 0xE0. Block 1 at s + 0x60 = 0xE0 has size (+0x10, at 0xF0) `last_size` and extra (+0x24, at 0x104) 0: it ends
// at 0xE0 + last_size = 0x120 for 0x40, which is the file's length.
static std::vector<uint8_t> SynthSpareRef(uint32_t last_size = 0x40) {
  std::vector<uint8_t> d(0x120);
  for (size_t i = 0; i < d.size(); ++i) d[i] = uint8_t((i * 7 + 3) & 0xFF);
  PutBe(d, 0x14, 0x20);
  std::memcpy(&d[0x20], "MULTI_CLOTH", 11);
  PutBe(d, 0x38, 0xAABBCCDDu);
  PutBe(d, 0x40, 0x60);
  std::memcpy(&d[0x80], "SPARE_CHUNK", 11);
  PutBe(d, 0x9C, 2);
  PutBe(d, 0xA0, 0x30);
  PutBe(d, 0xB0, 0x40);
  PutBe(d, 0xB4, 0x60);
  PutBe(d, 0xD0, 0x20);
  PutBe(d, 0xE4, 0);
  PutBe(d, 0xF0, last_size);
  PutBe(d, 0x104, 0);
  d[0x11F] = 0x77;
  return d;
}

static void TestSparesMustEndFile() {
  Group("spares_must_end_file");
  const auto d = SynthSpareRef();
  // onto no cloth: a new MULTI_CLOTH = d[0x20:0x70] with +0x18 = 0; the chunk is appended at the next 16 boundary
  // (0x50 already is one) and +0x20 points at it; then d[0x80:] (160 bytes). 80 + 160 = 240 bytes.
  {
    const auto r = catx::WithSpares(nullptr, Sp(d));
    CK(r.has_value());
    if (r) {
      std::vector<uint8_t> want(d.begin() + 0x20, d.begin() + 0x70);
      PutBe(want, 0x18, 0);
      PutBe(want, 0x20, 0x50);  // was 0x60 in the reference: the spares moved
      want.insert(want.end(), d.begin() + 0x80, d.end());
      CKF(r->size() == 240 && *r == want, "no cloth: %zu bytes", r->size());
    }
  }
  // onto the suit's cloth: 0x26 bytes, so the chunk starts at 0x30 and +0x20 = 0x30 (not 0x26: the alignment comes first)
  {
    const std::vector<uint8_t> cloth(0x26, 0x55);
    const auto r = catx::WithSpares(&cloth, Sp(d));
    CK(r.has_value());
    if (r) {
      std::vector<uint8_t> want = cloth;
      want.resize(0x30, 0);
      PutBe(want, 0x20, 0x30);
      want.insert(want.end(), d.begin() + 0x80, d.end());
      CKF(r->size() == 0xD0 && *r == want, "onto cloth: %zu bytes", r->size());
    }
  }
  // no spares in the reference: the cloth comes back unchanged (None stays None)
  {
    auto none = d;
    PutBe(none, 0x14, 0);  // no MULTI_CLOTH at all
    CK(!catx::WithSpares(nullptr, Sp(none)).has_value());
    const std::vector<uint8_t> cloth(0x26, 0x55);
    const auto same = catx::WithSpares(&cloth, Sp(none));
    CK(same.has_value() && *same == cloth);
    auto nosp = d;
    PutBe(nosp, 0x40, 0);  // a MULTI_CLOTH with no spare offset
    CK(!catx::WithSpares(nullptr, Sp(nosp)).has_value());
  }
  // the asserts: the blocks must end the file; the tags must be there
  {
    const auto bad = SynthSpareRef(0x30);  // block 1 ends at 0xE0 + 0x30 = 0x110, the file is 0x120 long
    const std::string t = ErrOf([&] { catx::WithSpares(nullptr, Sp(bad)); });
    CKF(t == "AssertionError: ('the spare blocks do not end the file', '0x110', '0x120')", "%s", t.c_str());
    auto notag = d;
    notag[0x80] = 'X';
    const std::string t2 = ErrOf([&] { catx::WithSpares(nullptr, Sp(notag)); });
    CKF(t2 == "AssertionError: ('no SPARE_CHUNK at the offset', '0x80')", "%s", t2.c_str());
    auto nomc = d;
    nomc[0x20] = 'X';
    CK(ErrOf([&] { catx::WithSpares(nullptr, Sp(nomc)); }).rfind("AssertionError", 0) == 0);
    // a cloth shorter than the +0x20 field cannot take the offset: struct.error, not a silent skip
    const std::vector<uint8_t> tiny(0x10, 1);
    CK(!ErrOf([&] { catx::WithSpares(&tiny, Sp(d)); }).empty());
  }
  std::printf("spares_must_end_file: no-cloth 240 bytes, onto-cloth 208 bytes, none-in-reference, 3 assert texts\n");
}

// prep_head_physics on a hand-built reference. Header: +8 = base 0x20, +0xC = mpv 0x100. Head objects {3, 5}.
// The directory at base (8 bytes per entry; byte 0 the kind, +4 the record offset from base), 7 entries, the 0xFF
// terminator at 0x50, so the table region is 0x38 bytes:
//   e0 (0x20) kind 1, record at base + 0x40 = 0x60: its +0x4C (0xAC) holds 0x78, and the word at base + 0x78 = 0x98 is
//      0xFFFF0003: top half 0xFFFF, low 12 bits 3, in head -> kept.
//   e1 (0x28) kind 1, record 0x64: +0x4C (0xB0) holds 0x7C, the word at 0x9C is 0xFFFE0007: 7 is not a head object -> dropped.
//   e2 (0x30) kind 0xFA, record 0x70: the halfwords at 0x72 and 0x74 are 3 and 5 -> kept.
//   e3 (0x38) kind 0xFA, record 0x78: 3 and 9 -> dropped. e4 (0x40) kind 0xFA, record 0x80: 9 first -> dropped.
//   e5 (0x48) kind 2: not a kind that is ever kept. e6 (0x50) 0xFF.
// So the new table is e0, e2, 0xFF, 7 zero bytes = 24 bytes, padded with 32 zeros to the old 0x38; the rest of the
// section (0x58..0xFF) is copied. Face channels: MPV offset table at mpv + 36 + 4 * index: index 2 -> 0x40 (block at
// 0x140), index 3 -> 0x100 (block at 0x200). Block 2: obj 3, kind 0, nposes 2, word list at +0x40 = 0x180 with three
// words (0x80000001, 2, 0x80000003) and the terminator: records = 1 + 2 = 3; pose offsets at +28 (0x15C): 0x50, 0x70, so
// end = 0x140 + 0x70 + 3 * 12 = 0x1D4 (148 bytes). Block 3: obj 5, kind 0, nposes 1, words at +0x30 = 0x230: (1),
// terminator: records = 1; pose offset at 0x21C: 0x40; end = 0x200 + 0x40 + 12 = 0x24C (76 bytes).
static std::vector<uint8_t> SynthHeadRef() {
  std::vector<uint8_t> d(0x300);
  for (size_t i = 0; i < d.size(); ++i) d[i] = uint8_t((i * 7 + 3) & 0xFF);
  PutBe(d, 8, 0x20);
  PutBe(d, 0xC, 0x100);
  auto entry = [&](size_t at, uint8_t kind, uint32_t off) {
    std::fill(d.begin() + at, d.begin() + at + 8, uint8_t(0));
    d[at] = kind;
    PutBe(d, at + 4, off);
  };
  entry(0x20, 1, 0x40);
  entry(0x28, 1, 0x44);
  entry(0x30, 0xFA, 0x50);
  entry(0x38, 0xFA, 0x58);
  entry(0x40, 0xFA, 0x60);
  entry(0x48, 2, 0);
  entry(0x50, 0xFF, 0);
  PutBe(d, 0xAC, 0x78);
  PutBe(d, 0x98, 0xFFFF0003u);
  PutBe(d, 0xB0, 0x7C);
  PutBe(d, 0x9C, 0xFFFE0007u);
  auto half = [&](size_t at, uint16_t v) { d[at] = uint8_t(v >> 8), d[at + 1] = uint8_t(v); };
  half(0x72, 3), half(0x74, 5);
  half(0x7A, 3), half(0x7C, 9);
  half(0x82, 9), half(0x84, 5);
  PutBe(d, 0x12C, 0x40);
  PutBe(d, 0x130, 0x100);
  PutBe(d, 0x140 + 12, 3), PutBe(d, 0x140 + 16, 0), PutBe(d, 0x140 + 20, 2), PutBe(d, 0x140 + 24, 0x40);
  PutBe(d, 0x180, 0x80000001u), PutBe(d, 0x184, 2), PutBe(d, 0x188, 0x80000003u), PutBe(d, 0x18C, 0xFFFFFFFFu);
  PutBe(d, 0x15C, 0x50), PutBe(d, 0x160, 0x70);
  PutBe(d, 0x200 + 12, 5), PutBe(d, 0x200 + 16, 0), PutBe(d, 0x200 + 20, 1), PutBe(d, 0x200 + 24, 0x30);
  PutBe(d, 0x230, 1), PutBe(d, 0x234, 0xFFFFFFFFu);
  PutBe(d, 0x21C, 0x40);
  return d;
}

static void TestHeadPhysicsSynthetic() {
  Group("head_physics_synthetic");
  const auto d = SynthHeadRef();
  core::PartArena arena;
  auto make_objects = [&](bool native3, std::vector<core::Part*>* keep_parts) {
    std::vector<std::optional<core::Object>> objects(6);
    for (int oid : {1, 3, 5}) {
      objects[oid].emplace();
      const int nparts = oid == 3 ? 2 : 1;
      for (int k = 0; k < nparts; ++k) {
        core::Part* p = arena.New();
        if (oid != 1 && (native3 || oid != 3)) p->native.emplace();
        objects[oid]->ps.push_back(p);
        keep_parts->push_back(p);
      }
    }
    return objects;
  };
  std::vector<core::Part*> parts;
  auto objects = make_objects(true, &parts);
  const auto hp = catx::PrepHeadPhysics(Sp(d), {3, 5}, objects);
  std::vector<uint8_t> want(d.begin() + 0x20, d.begin() + 0x100);
  std::vector<uint8_t> table(d.begin() + 0x20, d.begin() + 0x28);  // e0
  table.insert(table.end(), d.begin() + 0x30, d.begin() + 0x38);   // e2
  table.push_back(0xFF);
  table.insert(table.end(), 7, 0);
  table.insert(table.end(), 0x38 - table.size(), 0);
  std::copy(table.begin(), table.end(), want.begin());
  CKF(hp.section == want && hp.section.size() == 0xE0, "section %zu bytes", hp.section.size());
  CK(hp.channels.size() == 2 && hp.channels.count(2) && hp.channels.count(3));
  if (hp.channels.size() == 2) {
    CKF(hp.channels.at(2) == std::vector<uint8_t>(d.begin() + 0x140, d.begin() + 0x1D4), "channel 2: %zu bytes",
        hp.channels.at(2).size());
    CKF(hp.channels.at(3) == std::vector<uint8_t>(d.begin() + 0x200, d.begin() + 0x24C), "channel 3: %zu bytes",
        hp.channels.at(3).size());
  }
  // every part of the head objects 3 and 5 is writable now; object 1 is not a head object
  int writable = 0, untouched = 0;
  for (const core::Part* p : objects[3]->ps) writable += p->writable;
  for (const core::Part* p : objects[5]->ps) writable += p->writable;
  for (const core::Part* p : objects[1]->ps) untouched += !p->writable;
  CKF(writable == 3 && untouched == 1, "%d writable, %d untouched", writable, untouched);
  // the asserts, with their texts
  {
    std::vector<core::Part*> p2;
    auto ob2 = make_objects(false, &p2);  // object 3 has parts without a carried decl
    const std::string t = ErrOf([&] { catx::PrepHeadPhysics(Sp(d), {3, 5}, ob2); });
    CKF(t == "AssertionError: ('head decl was not carried', 3)", "%s", t.c_str());
    auto nobase = d;
    PutBe(nobase, 8, 0);
    const std::string t2 = ErrOf([&] { catx::PrepHeadPhysics(Sp(nobase), {3, 5}, ob2); });
    CKF(t2 == "AssertionError: ('physics section must sit before MPV', '0x0', '0x100')", "%s", t2.c_str());
    auto noface = d;
    PutBe(noface, 0x12C, 0);
    const std::string t3 = ErrOf([&] { catx::PrepHeadPhysics(Sp(noface), {3, 5}, ob2); });
    CKF(t3 == "AssertionError: ('reference has no face channel', 2)", "%s", t3.c_str());
    const std::string t4 = ErrOf([&] { catx::PrepHeadPhysics(Sp(d), {3}, ob2); });  // channel 3's object (5) is not a head object
    CKF(t4 == "AssertionError: ('face channel', 3, 5, 0)", "%s", t4.c_str());
    auto noposes = d;
    PutBe(noposes, 0x140 + 20, 0);
    const std::string t5 = ErrOf([&] { catx::PrepHeadPhysics(Sp(noposes), {3, 5}, ob2); });
    CKF(t5 == "ValueError: max() arg is an empty sequence", "%s", t5.c_str());
  }
  // set(head_ids) iterates in CPython's table order: with two objects lacking a carried decl, the one the set yields
  // first names the assert. {8, 1} iterates 8 then 1 (slot 0 before slot 1), so object 8 is named, not the lower id.
  {
    std::vector<std::optional<core::Object>> ob(9);
    for (int oid : {1, 8}) {
      ob[oid].emplace();
      ob[oid]->ps.push_back(arena.New());
    }
    auto d2 = d;
    PutBe(d2, 0x140 + 12, 8);  // channel 2's object, and channel 3's, must be head objects: use 8 and 1
    PutBe(d2, 0x200 + 12, 1);
    for (const auto& ids : {std::vector<int64_t>{1, 8}, std::vector<int64_t>{8, 1}}) {
      const std::string t = ErrOf([&] { catx::PrepHeadPhysics(Sp(d2), ids, ob); });
      CKF(t == "AssertionError: ('head decl was not carried', 8)", "%s", t.c_str());
    }
  }
  // the {**hand, **face} merge: a face key replaces an equal hand key
  {
    std::map<int64_t, std::vector<uint8_t>> hand{{0, {1}}, {1, {2}}, {2, {3}}};
    const std::map<int64_t, std::vector<uint8_t>> face{{2, {9, 9}}, {3, {8}}};
    catx::MergeChannels(hand, face);
    CK(hand.size() == 4 && hand.at(0) == std::vector<uint8_t>{1} && hand.at(1) == std::vector<uint8_t>{2});
    CK(hand.at(2) == std::vector<uint8_t>({9, 9}) && hand.at(3) == std::vector<uint8_t>{8});
  }
  // the set order emulation against CPython 3.12 (list(set(l)) for each input, taken from the recipe's interpreter)
  {
    static const std::pair<std::vector<int64_t>, std::vector<int64_t>> kPins[] = {
      {{3, 5}, {3, 5}},
      {{8, 1}, {8, 1}},
      {{1, 8}, {8, 1}},
      {{17, 1}, {17, 1}},
      {{1, 17}, {1, 17}},
      {{9, 1, 17}, {9, 1, 17}},
      {{17, 9, 1}, {17, 9, 1}},
      {{2, 10, 18}, {18, 2, 10}},
      {{18, 10, 2}, {2, 18, 10}},
      {{4, 12, 20, 28}, {20, 28, 4, 12}},
      {{28, 20, 12, 4}, {12, 4, 28, 20}},
      {{7, 15, 23}, {15, 23, 7}},
      {{23, 15, 7}, {15, 7, 23}},
      {{5, 5, 13}, {13, 5}},
      {{0}, {0}},
      {{0, 8}, {0, 8}},
      {{8, 0}, {8, 0}},
      {{16, 8, 0}, {16, 8, 0}},
      {{39, 23, 17}, {17, 23, 39}},
      {{11, 0}, {0, 11}},
      {{32, 29, 38}, {32, 29, 38}},
      {{21, 35}, {35, 21}},
      {{2, 24, 10, 28}, {24, 2, 10, 28}},
      {{27, 10, 10, 15}, {10, 27, 15}},
      {{7, 8}, {8, 7}},
      {{37, 4, 24, 6}, {24, 4, 37, 6}},
      {{13, 14, 26}, {26, 13, 14}},
      {{17, 13}, {17, 13}},
      {{17, 21, 2}, {17, 2, 21}},
      {{0, 26}, {0, 26}},
      {{24, 31}, {24, 31}},
      {{1, 15}, {1, 15}},
      {{7, 38, 0}, {0, 38, 7}},
      {{37, 12}, {12, 37}},
      {{21, 0}, {0, 21}},
      {{8, 34}, {8, 34}},
      {{32, 5}, {32, 5}},
      {{31, 34, 12, 26}, {34, 26, 12, 31}},
      {{25, 12}, {25, 12}},
      {{5, 37, 9, 11}, {11, 9, 37, 5}},
      {{2, 3, 17, 35}, {17, 2, 3, 35}},
      {{38, 9, 17, 36}, {9, 36, 38, 17}},
      {{7, 25}, {25, 7}},
      {{10, 39}, {10, 39}},
    };
    int bad = 0;
    for (const auto& [in, want_order] : kPins)
      if (catx::SmallSetOrder(in) != want_order) ++bad;
    CKF(bad == 0, "%d of %zu set orders differ", bad, std::size(kPins));
  }
  std::printf("head_physics_synthetic: section %zu bytes, channels %zu and %zu bytes\n", hp.section.size(),
              hp.channels.at(2).size(), hp.channels.at(3).size());
}

// ---------------------------------------------------------------------------------------------------------------
// hands_numeric: the weld key's rounding
// ---------------------------------------------------------------------------------------------------------------
// (The software fma and the norm's fused dot are pinned in suitconv_num_host_test.cpp, where the numeric kit lives.)

static void TestHandsNumeric() {
  Group("hands_numeric");
  const num::FpEnvGuard env;
  // the weld key: numpy's rint rounds half to even (k * 0.0002 / 0.0002 is exactly k for these k), then int32
  {
    const double ks[] = {0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5};
    const int32_t want[] = {0, 2, 2, 4, 0, -2, -2};
    for (size_t i = 0; i < 7; ++i)
      CKF(doa4::suitconv::hands::WeldCoord(0.0002 * ks[i]) == want[i], "WeldCoord(0.0002 * %g) = %d, want %d", ks[i],
          int(doa4::suitconv::hands::WeldCoord(0.0002 * ks[i])), int(want[i]));
    CK(doa4::suitconv::hands::WeldCoord(std::numeric_limits<double>::quiet_NaN()) == INT32_MIN);
    CK(doa4::suitconv::hands::WeldCoord(1e10) == INT32_MIN);
    CK(doa4::suitconv::hands::WeldCoord(0.0) == 0);
  }
  std::printf("hands_numeric: weld key rounding pinned\n");
}

// ---------------------------------------------------------------------------------------------------------------
// mpv_header_marks, l1_synthetic
// ---------------------------------------------------------------------------------------------------------------

// Guest memory for the game side reader: big-endian words over a byte array, addresses start at kBase.
namespace {
constexpr uint32_t kBase = 0x40000000;
struct MpvMem {
  std::vector<uint8_t> bytes;
  uint32_t ReadU32(uint32_t a) const {
    const uint8_t* p = &bytes.at(a - kBase);
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  }
  void WriteU32(uint32_t a, uint32_t v) {
    uint8_t* p = &bytes.at(a - kBase);
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
  }
  float F(uint32_t a) const { return std::bit_cast<float>(ReadU32(a)); }
};
uint32_t Be32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b.at(at)) << 24 | uint32_t(b.at(at + 1)) << 16 | uint32_t(b.at(at + 2)) << 8 | b.at(at + 3);
}
}  // namespace

static void TestMpvHeaderMarks() {
  Group("mpv_header_marks");
  const auto ch = synth::conv::MakeChest();
  const double fd = 1.0 / 0.562, fu = 1.0 / 0.704;
  const auto mpv = cb::BuildMpv(ch.objects, ch.geos, ch.bind4, ch.chest, 0.0005, fd, fu);
  CK(mpv.size() > 0x40 && mpv[0] == 'M' && mpv[1] == 'P' && mpv[2] == 'V' && mpv[3] == 0);
  CK(Be32(mpv, 4) == 1 && Be32(mpv, 12) == 5 && Be32(mpv, 32) == 5);
  const uint32_t mpp_off = Be32(mpv, 36 + 4 * 4);  // channel 4's slot
  CKF(mpp_off == 0x40, "channel 4 at 0x%x", mpp_off);
  // The expected records, derived here from the fixture by plain loops (not by the code under test).
  struct Rec {
    uint32_t word;
    float rest[3], down[3], up[3];
  };
  std::vector<Rec> want;
  for (int k = 0; k < 2; ++k) {
    const auto& p = *ch.parts[k];
    for (size_t j = 0; j < p.verts.size(); ++j) {
      const auto& dn = p.bust->first[j];
      const auto& up = p.bust->second[j];
      const double nd = std::sqrt(dn[0] * dn[0] + dn[1] * dn[1] + dn[2] * dn[2]);
      const double nu = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
      if ((std::max)(nd, nu) < 0.0005) continue;
      Rec r{};
      r.word = (uint32_t(k) << 24) | uint32_t(j * ch.strides[k]) | (want.empty() ? 0u : 0x80000000u);
      for (int c = 0; c < 3; ++c) {
        const double lp = p.verts[j].pos[c] - ch.own[c];
        r.rest[c] = float(lp);
        r.down[c] = float(lp + dn[c]);
        r.up[c] = float(lp + up[c]);
      }
      want.push_back(r);
    }
  }
  CKF(int(want.size()) == ch.kept, "%zu expected records, fixture says %d", want.size(), ch.kept);
  CK(!want.empty());
  // The mark and the record count, then the four ratios, through doa4_suits_bust.h.
  MpvMem m;
  m.bytes = mpv;
  const uint32_t mpp = kBase + mpp_off;
  CK(rdr::IsMarked(m, mpp));
  CK(m.ReadU32(mpp + 0x28) == 0x44583242u);
  CKF(m.ReadU32(mpp + 0x2C) == want.size(), "records %u, expected %zu", m.ReadU32(mpp + 0x2C), want.size());
  CK(m.F(mpp + 0x30) == 1.0f && m.F(mpp + 0x38) == 1.0f);
  CK(m.F(mpp + 0x34) == float(fd) && m.F(mpp + 0x3C) == float(fu));
  CK(m.F(mpp + 0x34) != m.F(mpp + 0x3C));
  // The words: first without the high bit, the rest with it, then the terminator.
  const uint32_t words_at = mpp_off + 0x50;
  for (size_t i = 0; i < want.size(); ++i) CKF(Be32(mpv, words_at + 4 * i) == want[i].word, "word %zu", i);
  CK(Be32(mpv, words_at + 4 * want.size()) == 0xFFFFFFFFu);
  // Bind as the game does (+8 = 1, absolute pose addresses), then rescale to the full DOAX2 range with the reader.
  uint32_t pose_at[3];
  for (int i = 0; i < 3; ++i) {
    pose_at[i] = mpp + Be32(mpv, mpp_off + 28 + 4 * i);
    CK((pose_at[i] - mpp) % 16 == 0);
    m.WriteU32(mpp + 0x1C + 4 * i, pose_at[i]);
  }
  m.WriteU32(mpp + 0x08, 1);
  const std::vector<uint8_t> before = m.bytes;
  CK(rdr::Rescale(m, mpp, true));
  CK(m.F(mpp + 0x30) == float(fd) && m.F(mpp + 0x38) == float(fu));
  int scaled_down = 0, scaled_up = 0;
  for (size_t i = 0; i < want.size(); ++i)
    for (int c = 0; c < 3; ++c) {
      const float r = want[i].rest[c];
      CK(m.F(pose_at[0] + 12 * i + 4 * c) == r);  // the rest pose is not touched
      const float fdn = float(fd) / 1.0f, fup = float(fu) / 1.0f;
      const float ed = r + (want[i].down[c] - r) * fdn, eu = r + (want[i].up[c] - r) * fup;
      CKF(m.F(pose_at[1] + 12 * i + 4 * c) == ed, "down record %zu comp %d", i, c);
      CKF(m.F(pose_at[2] + 12 * i + 4 * c) == eu, "up record %zu comp %d", i, c);
      if (m.F(pose_at[1] + 12 * i + 4 * c) != want[i].down[c]) ++scaled_down;
      if (m.F(pose_at[2] + 12 * i + 4 * c) != want[i].up[c]) ++scaled_up;
    }
  CKF(scaled_down > 0 && scaled_up > 0, "%d down and %d up components moved", scaled_down, scaled_up);
  // Nothing else moved: only the two ratio words and exactly records * 12 bytes of each of the two poses.
  size_t changed_outside = 0;
  for (size_t i = 0; i < m.bytes.size(); ++i) {
    if (m.bytes[i] == before[i]) continue;
    const uint32_t a = kBase + uint32_t(i);
    const bool in_down = a >= pose_at[1] && a < pose_at[1] + 12 * want.size();
    const bool in_up = a >= pose_at[2] && a < pose_at[2] + 12 * want.size();
    const bool ratio = (a >= mpp + 0x30 && a < mpp + 0x34) || (a >= mpp + 0x38 && a < mpp + 0x3C);
    if (!in_down && !in_up && !ratio) ++changed_outside;
  }
  CKF(changed_outside == 0, "%zu bytes changed outside the two poses and their ratios", changed_outside);
  CK(rdr::Rescale(m, mpp, false));  // back to the file's strength
  CK(m.F(mpp + 0x30) == 1.0f && m.F(mpp + 0x38) == 1.0f);

  // extra channels and pins, in the same function: sorted channel slots, the pin block after the poses.
  const std::map<int64_t, std::vector<uint8_t>> extra{{1, std::vector<uint8_t>(20, 0xEE)}};
  std::vector<cb::PinRow> pins(1);
  pins[0].rel = -0x100;
  pins[0].rest = {1.0, 2.0, 3.0};
  pins[0].down = {4.0, 5.0, 6.0};
  pins[0].up = {7.0, 8.0, 9.0};
  const auto mpv2 = cb::BuildMpv(ch.objects, ch.geos, ch.bind4, ch.chest, 0.0005, fd, fu, &extra, &pins);
  CK(Be32(mpv2, 36 + 4) == 0x40 && mpv2[0x40] == 0xEE && mpv2[0x53] == 0xEE);
  const uint32_t mpp2 = Be32(mpv2, 36 + 16);
  CKF(mpp2 == 0x60, "channel 4 at 0x%x", mpp2);
  const uint32_t pin_at = Be32(mpv2, mpp2 + 0x40);
  CK(pin_at % 16 == 0 && pin_at >= 0x50 && Be32(mpv2, mpp2 + 0x44) == 1);
  CK(Be32(mpv2, mpp2 + pin_at) == uint32_t(-0x100));
  CK(std::bit_cast<float>(Be32(mpv2, mpp2 + pin_at + 4)) == 1.0f &&
     std::bit_cast<float>(Be32(mpv2, mpp2 + pin_at + 36)) == 9.0f);
  CK(mpv2.size() == mpp2 + pin_at + 40);
  MpvMem m2;
  m2.bytes = mpv2;
  CK(rdr::IsMarked(m2, kBase + mpp2));
  // an empty list of pins is Python's None
  const std::vector<cb::PinRow> none;
  CK(cb::BuildMpv(ch.objects, ch.geos, ch.bind4, ch.chest, 0.0005, fd, fu, nullptr, &none) == mpv);
  // build_mpv's own asserts
  auto throws = [&](auto fn, const char* text) {
    try {
      fn();
    } catch (const ConvertError& e) {
      return std::string(e.what()).find(text) != std::string::npos;
    }
    return false;
  };
  CK(throws([&] { cb::BuildMpv(ch.objects, ch.geos, ch.bind4, ch.chest, 1.0, fd, fu); }, "no bust vertex"));
  auto geos2 = ch.geos;
  geos2[3].decls[0].elems[0].b = 4;
  CK(throws([&] { cb::BuildMpv(ch.objects, geos2, ch.bind4, ch.chest, 0.0005, fd, fu); }, "position is not first"));
  std::printf("mpv_header_marks: %zu records, MPV %zu bytes, channel 4 at 0x%x, ratios %.6f / %.6f\n", want.size(),
              mpv.size(), mpp_off, double(float(fd)), double(float(fu)));
}

// mpv_threshold_is_math_dist: build_mpv keeps a vertex when max(math.dist(down, 0), math.dist(up, 0)) is not below the
// threshold (physics_port.py line 138). math.dist is CPython's vector_norm, not the square root of the plain sum of squares:
// for this offset (a seeded synthetic vector) CPython 3.12.1 gives 0x1.761cc3566e09ap-11 and the plain sqrt one ulp less. With
// the threshold at math.dist's own value the vertex is kept (not below); a threshold one ulp higher drops it (the control).
static void TestMpvThresholdIsMathDist() {
  Group("mpv_threshold_is_math_dist");
  const core::Vec3 v{0x1.58296810665e0p-12, 0x1.e488b6d0ca5b8p-12, -0x1.c68deb4ecfaa4p-12};
  const double thresh = 0x1.761cc3566e09ap-11;
  CK(std::sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]) < thresh);  // vacuity: the plain norm is below the threshold
  CK(num::MathDist3(v, core::Vec3{0.0, 0.0, 0.0}) == thresh);
  const double fd = 1.0 / 0.562, fu = 1.0 / 0.704;
  const auto count_records = [&](double th) {
    auto ch = synth::conv::MakeChest();
    ch.parts[0]->bust->first[2] = v;
    ch.parts[0]->bust->second[2] = core::Vec3{0.0, 0.0, 0.0};
    const auto mpv = cb::BuildMpv(ch.objects, ch.geos, ch.bind4, ch.chest, th, fd, fu);
    const uint32_t mpp_off = Be32(mpv, 36 + 4 * 4);
    size_t n = 0;
    while (Be32(mpv, mpp_off + 0x50 + 4 * n) != 0xFFFFFFFFu) ++n;
    return std::make_pair(n, size_t(ch.kept));
  };
  const auto [kept_at, base] = count_records(thresh);
  CKF(kept_at == base + 1, "threshold = math.dist: %zu records, the fixture's %zu plus the vertex", kept_at, base);
  const auto [kept_above, base2] = count_records(std::nextafter(thresh, 1.0));
  CKF(kept_above == base2, "threshold one ulp above: %zu records, the fixture's %zu", kept_above, base2);
}

// prep_bust on a hand-built model (seeded numbers, no game data): one chest part with a 7-buffer morph decl and five
// vertices, a part whose up and down buffers run backwards, a decl with one buffer only, a part with no model, a body
// part (the border grid) and a strap part (the model prep_bust compares against). Offsets are multiples of 1/64 and the
// strengths 0.5 and 0.25, so every expected number is exact; the cell size is 0.002.
//   vertex 0 (0,0,0): the strap is 0.02 away of the 0.04 fade range, t = 0.5, k_f = 0.5: both offsets halve
//   vertex 1 (10,0,0): nothing near
//   vertex 2 (20.0,0,0): the body vertex at 20.0005 is one cell away (cell 9999 and 10000 with float //): held still
//   vertex 3 (30.0,0,0): the body vertex at 30.0025 is two cells away (14999 and 15001): not held
//   vertex 4 (0.022,5,0): the body vertex at (0.025,5,0) is two cells away with float // (10 and 12) but one cell away
//                         with floor(x / cell) (11 and 12): not held
static void TestPrepBustSynthetic() {
  Group("prep_bust_synthetic");
  namespace core = doa4::suitconv::core;
  using synth::conv::PutBe32;
  using synth::conv::PutBeF;
  const int n = 5;
  core::Model chest_model, strap_model, body_model;
  std::vector<uint8_t> data;
  auto desc = [](uint32_t base, uint32_t size) {
    std::vector<uint8_t> d;
    PutBe32(d, base);
    PutBe32(d, size);
    return d;
  };
  chest_model.vbs = {desc(0, 16 * n), desc(80, 12 * n), desc(140, 12 * n)};
  for (int j = 0; j < n; ++j) {  // main buffer: position + 4 pad bytes
    PutBeF(data, 0.0625f * j);
    PutBeF(data, 0.125f);
    PutBeF(data, -0.25f);
    PutBe32(data, 0);
  }
  auto up_off = [](int j) { return std::array<float, 3>{0.0625f * (j + 1), 0.25f, 0.015625f * j}; };
  auto dn_off = [](int j) { return std::array<float, 3>{-0.0625f, -0.5f * (j + 1), 0.125f}; };
  for (int j = 0; j < n; ++j) {  // buffer 1: the up target = P + up_off
    const auto o = up_off(j);
    PutBeF(data, 0.0625f * j + o[0]);
    PutBeF(data, 0.125f + o[1]);
    PutBeF(data, -0.25f + o[2]);
  }
  for (int j = 0; j < n; ++j) {  // buffer 2: the down target
    const auto o = dn_off(j);
    PutBeF(data, 0.0625f * j + o[0]);
    PutBeF(data, 0.125f + o[1]);
    PutBeF(data, -0.25f + o[2]);
  }
  chest_model.x.data = data;
  auto tail = [](uint32_t vb, uint32_t stride) {
    std::vector<uint8_t> t;
    PutBe32(t, vb);
    PutBe32(t, stride);
    PutBe32(t, 0);
    PutBe32(t, 0);
    return t;
  };
  auto morph_decl = [&](uint32_t up_vb, uint32_t down_vb) {
    tp::Decl d;
    d.head = {0, 7, 0, 0, 2, uint32_t(n), 0, 0, 0, 0, 0, 0};
    d.elems = {tp::DeclElem{0, 0, 0x2A23B9, 0, 0, 0, 0}, tp::DeclElem{0xFF, 0, 0, 0, 0, 0, 0}};
    d.tails = {tail(0, 16), tail(0, 0), tail(0, 0), tail(0, 0), tail(0, 0), tail(up_vb, 12), tail(down_vb, 12)};
    return d;
  };
  chest_model.geo.resize(1);
  chest_model.geo[0].decls = {morph_decl(1, 2), morph_decl(2, 1)};
  tp::Decl plain;
  plain.head = {0, 1, 0, 0, 2, uint32_t(n), 0, 0, 0, 0, 0, 0};
  plain.elems = {tp::DeclElem{0, 0, 0x2A23B9, 0, 0, 0, 0}, tp::DeclElem{0xFF, 0, 0, 0, 0, 0, 0}};
  plain.tails = {tail(0, 16)};
  chest_model.geo[0].decls.push_back(plain);

  core::PartArena arena;
  const int64_t chest = 0;
  auto make = [&](const core::Model* model, int64_t decl, int64_t owner, std::vector<core::Vec3> pos) {
    core::Part* p = arena.New();
    p->model = model;
    p->obj_id = 0;
    p->decl_index = decl;
    p->owner = owner;
    for (const auto& v : pos) {
      core::Vert vv;
      vv.pos = v;
      p->verts.push_back(vv);
    }
    return p;
  };
  const std::vector<core::Vec3> cpos = {{0, 0, 0}, {10, 0, 0}, {20.0, 0, 0}, {30.0, 0, 0}, {0.022, 5.0, 0}};
  core::Part* morph = make(&chest_model, 0, chest, cpos);
  core::Part* reversed = make(&chest_model, 1, chest, cpos);
  core::Part* thin = make(&chest_model, 2, chest, cpos);
  core::Part* carried = make(nullptr, 0, chest, cpos);
  core::Part* body = make(&body_model, 0, 5, {{20.0005, 0, 0}, {30.0025, 0, 0}, {0.025, 5.0, 0}});
  core::Part* strap = make(&strap_model, 0, 7, {{0, 0.02, 0}});
  std::vector<std::optional<core::Object>> objects(1);
  objects[0] = core::Object{{morph, reversed, thin, carried}, {1}};
  core::PyDict<int64_t, int64_t> chan2id;
  chan2id.Set(0x9000, chest);
  const std::vector<core::Part*> parts = {morph, reversed, thin, carried, body, strap};
  const int64_t got = cb::PrepBust(objects, parts, chan2id, 0.25, 0.5, {}, &strap_model, 0.04);
  CK(got == chest);
  CK(morph->bust.has_value() && morph->writable && morph->border.has_value());
  CK(!reversed->bust && !reversed->writable && !reversed->border);  // up and down run backwards: stays still
  CK(!thin->bust && !thin->writable && !thin->border);              // not a 7-buffer decl
  CK(!carried->bust && !carried->writable && !carried->border);     // no model
  if (!morph->bust) return;
  CKF(morph->border == std::set<int64_t>({2}), "border has %zu entries", morph->border ? morph->border->size() : size_t(0));
  int checked = 0;
  for (int j = 0; j < n; ++j) {
    const auto u = up_off(j), d = dn_off(j);
    for (int c = 0; c < 3; ++c) {
      double eu = 0.5 * double(u[c]), ed = 0.25 * double(d[c]);
      if (j == 0) eu *= 0.5, ed *= 0.5;  // eased by the strap
      if (j == 2) eu = ed = 0.0;         // held still at the border
      CKF(morph->bust->second[j][c] == eu, "up[%d][%d] = %g, expected %g", j, c, morph->bust->second[j][c], eu);
      CKF(morph->bust->first[j][c] == ed, "down[%d][%d] = %g, expected %g", j, c, morph->bust->first[j][c], ed);
      ++checked;
    }
  }
  std::printf("prep_bust_synthetic: %d components checked, border {2}, eased vertex 0, three parts left still\n", checked);
  // without a strap fade nothing eases; without the strap model's parts the same
  morph->bust.reset();
  morph->border.reset();
  morph->writable = false;
  cb::PrepBust(objects, parts, chan2id, 0.25, 0.5, {}, &strap_model, 0.0);
  CK(morph->bust.has_value() && morph->bust->second[0][0] == 0.5 * double(up_off(0)[0]));
  // the chest object must exist
  std::vector<std::optional<core::Object>> none(1);
  bool threw = false;
  try {
    cb::PrepBust(none, parts, chan2id, 0.25, 0.5, {}, &strap_model, 0.0);
  } catch (const ConvertError& e) {
    threw = std::string(e.what()).find("no chest object") != std::string::npos;
  }
  CK(threw);

  // The cloth fade (prep_bust with cloth_objs, --port-cloth; never reached before the authored cloth was ported): the
  // chest's morphing body fades near a piece of the suit's cloth (model cloth_model, object id in cloth_objs) that sits on
  // the chest object. Worked by hand, fade range 0.0625, strap fade off: the cloth vertex (10, 1/64, 0) is 1/64 from
  // vertex 1 (10, 0, 0), so t = (1/64) / 0.0625 = 0.25 and k_f = 0.25 * 0.25 * (3 - 2 * 0.25) = 0.15625; vertex 1's
  // offsets become 0.15625 * (0.5 * up_off(1)) and 0.15625 * (0.25 * dn_off(1)). A part of the same model on the chest
  // whose object id (1) is NOT in cloth_objs sits 1/64 from vertex 0 and must not fade it (NOT 0.15625 * ...). Every other
  // vertex is more than 9 away from the cloth vertex: unfaded, and vertex 2 is still held at the border.
  core::Part* cloth = make(&strap_model, 0, chest, {{10.0, 0.015625, 0.0}});
  core::Part* not_cloth = make(&strap_model, 0, chest, {{0.0, 0.015625, 0.0}});
  not_cloth->obj_id = 1;
  morph->bust.reset();
  morph->border.reset();
  morph->writable = false;
  objects[0] = core::Object{{morph, reversed, thin, carried, cloth, not_cloth}, {1}};
  cb::PrepBust(objects, parts, chan2id, 0.25, 0.5, {0}, &strap_model, 0.0, 0.001, 0.0625);
  CK(morph->bust.has_value() && !cloth->bust && !not_cloth->bust);
  if (morph->bust) {
    int faded_checked = 0;
    for (int j = 0; j < n; ++j) {
      const auto u = up_off(j), d = dn_off(j);
      for (int c = 0; c < 3; ++c) {
        double eu = 0.5 * double(u[c]), ed = 0.25 * double(d[c]);
        if (j == 1) eu = 0.15625 * eu, ed = 0.15625 * ed;  // faded near the cloth
        if (j == 2) eu = ed = 0.0;                         // held still at the border
        CKF(morph->bust->second[j][c] == eu, "cloth fade: up[%d][%d] = %g, expected %g", j, c, morph->bust->second[j][c], eu);
        CKF(morph->bust->first[j][c] == ed, "cloth fade: down[%d][%d] = %g, expected %g", j, c, morph->bust->first[j][c], ed);
        ++faded_checked;
      }
    }
    CK(morph->bust->second[1][1] != 0.5 * double(up_off(1)[1]));  // the fade changed vertex 1
    std::printf("prep_bust_synthetic: cloth fade, %d components checked, vertex 1 at k_f 0.15625, vertex 0 not faded by "
                "a part outside cloth_objs\n",
                faded_checked);
  }
}

// bust_peak: the most forward point (largest z, first one wins a tie, in the chest's frame), skipping parts without a
// bust and parts with a band set. Offsets are multiples of 1/16 so every subtraction is exact.
static void TestBustPeakRules() {
  Group("bust_peak_rules");
  auto ch = synth::conv::MakeChest();  // own = (0.5, 1.25, -0.125)
  auto set_verts = [&](doa4::suitconv::core::Part& p, std::vector<std::array<double, 2>> yz) {
    p.verts.clear();
    for (const auto& [y, z] : yz) {
      doa4::suitconv::core::Vert v;
      v.pos = {ch.own[0], ch.own[1] + y, ch.own[2] + z};
      p.verts.push_back(v);
    }
  };
  set_verts(*ch.parts[0], {{0.3125, 0.0625}, {0.125, 0.25}, {0.5, 0.0625}});  // z 0.25 first at y 0.125
  set_verts(*ch.parts[1], {{0.75, 0.25}, {0.375, 0.1875}});                   // z 0.25 again at y 0.75: must lose
  // a part with a band set, and a part without a bust: both further forward, both skipped
  doa4::suitconv::core::Part banded, plain;
  set_verts(banded, {{0.0625, 0.5}});
  banded.bust = ch.parts[0]->bust;
  banded.band = std::set<int64_t>{0};
  set_verts(plain, {{0.0625, 0.5}});
  ch.objects[3]->ps.push_back(&banded);
  ch.objects[3]->ps.push_back(&plain);
  const auto peak = cb::BustPeak(ch.objects, ch.chest, ch.bind4);
  CK(peak.has_value());
  if (peak) CKF((*peak)[0] == 0.125 && (*peak)[1] == 0.25, "peak (%g, %g)", (*peak)[0], (*peak)[1]);
  // part order decides a tie: with the parts swapped the other y comes out
  std::swap(ch.objects[3]->ps[0], ch.objects[3]->ps[1]);
  const auto peak2 = cb::BustPeak(ch.objects, ch.chest, ch.bind4);
  CK(peak2.has_value());
  if (peak2) CKF((*peak2)[0] == 0.75 && (*peak2)[1] == 0.25, "swapped peak (%g, %g)", (*peak2)[0], (*peak2)[1]);
  // nothing morphing: None
  ch.objects[3]->ps = {&plain};
  CK(!cb::BustPeak(ch.objects, ch.chest, ch.bind4).has_value());
}

static void TestL1Synthetic() {
  Group("l1_synthetic");
  // The synthetic models have no 7-buffer morph decl, so no chest part takes a bust and the Python stops at build_mpv.
  const auto s = synth::conv::MakeInputs();
  const SuitInputs in = SynthInputs(s);
  ConvertFailure f;
  MemTrace t;
  const auto r = ConvertSuit(SynthSuit(), SynthGirl(), in, Level::kL1, nullptr, &t, &f);
  CKF(!r && f.stage == "mpv" && f.what.find("no bust vertex") != std::string::npos, "%s / %s", f.stage.c_str(),
      f.what.c_str());
  auto has = [&](const std::string& prefix) {
    return std::any_of(t.lines.begin(), t.lines.end(), [&](const std::string& l) { return l.rfind(prefix, 0) == 0; });
  };
  CK(has("S bust") && has("K chest ") && has("S peak") && has("K peak none") && has("S mpv"));
  CK(!has("D ") && !has("B mpv"));
  // and at L0 the same inputs convert, with none of those stages
  MemTrace t0;
  CK(ConvertSuit(SynthSuit(), SynthGirl(), in, Level::kL0, nullptr, &t0, &f).has_value());
  CK(std::none_of(t0.lines.begin(), t0.lines.end(), [](const std::string& l) { return l.rfind("S bust", 0) == 0; }));
  // a suit without bust strengths skips the channel at L1 (`if a.bust_down` is false)
  SuitRecipe nob = SynthSuit();
  nob.bust_down = nob.bust_up = 0.0;
  MemTrace t1;
  CK(ConvertSuit(nob, SynthGirl(), in, Level::kL1, nullptr, &t1, &f).has_value());
  CK(std::none_of(t1.lines.begin(), t1.lines.end(), [](const std::string& l) { return l.rfind("S bust", 0) == 0; }));
}

// waist_band_short_fails: the waist template's heights span 0.03 m, and the band used is never longer than the
// template's span, so prep_waist_fit's `assert band_hi - band_lo > 0.05, ('waist band too short', band_lo, band_hi)`
// fires: the conversion comes back as ConvertFailure{"waist", "AssertionError: ('waist band too short', <lo>, <hi>)"}, not as an output
// and not as a crash. The stage must be reached with a candidate object (else the fit is SKIPPED, returns None and the
// conversion goes on): the trace shows the `S waist` line and no `B waist_target` line.
static void TestWaistBandShortFails() {
  Group("waist_band_short_fails");
  const auto s = synth::conv::MakeInputs();
  const SuitInputs in = SynthInputs(s);
  ConvertFailure f;
  MemTrace t;
  const auto r = ConvertSuit(SynthSuit(), SynthGirl(), in, Level::kL2, nullptr, &t, &f);
  CKF(!r.has_value(), "converted although the band is too short");
  CKF(f.stage == "waist", "stage %s: %s", f.stage.c_str(), f.what.c_str());
  CKF(f.what.rfind("AssertionError: ('waist band too short', ", 0) == 0 && !f.what.empty() && f.what.back() == ')', "text: %s",
      f.what.c_str());
  auto has = [&](const std::string& prefix) {
    return std::any_of(t.lines.begin(), t.lines.end(), [&](const std::string& l) { return l.rfind(prefix, 0) == 0; });
  };
  CK(has("S waist"));
  CK(!has("B waist_target"));
}

// waist_synthetic: with a template tall enough (0.2 m) the same inputs convert at L2 (bust strengths 0 so the MPV is not
// asked for): the trace shows the waist stage, `B waist_target` and an h4 line, and the output differs from L0's
// (the FLOAT16_4 weights of the waist object are written by encode_objects).
static void TestWaistSynthetic() {
  Group("waist_synthetic");
  const auto s = synth::conv::MakeInputs();
  SuitInputs in = SynthInputs(s);
  in.waist_tpr = std::make_shared<const std::vector<uint8_t>>(synth::conv::MakeWaistTemplateTpr(0.2f));
  SuitRecipe nob = SynthSuit();
  nob.bust_down = nob.bust_up = 0.0;
  ConvertFailure f;
  MemTrace t2, t0;
  const auto r2 = ConvertSuit(nob, SynthGirl(), in, Level::kL2, nullptr, &t2, &f);
  CKF(r2.has_value(), "failed at %s: %s", f.stage.c_str(), f.what.c_str());
  const auto r0 = ConvertSuit(nob, SynthGirl(), in, Level::kL0, nullptr, &t0, &f);
  CK(r0.has_value());
  auto has = [](const MemTrace& t, const std::string& needle) {
    return std::any_of(t.lines.begin(), t.lines.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; });
  };
  CK(has(t2, "S waist") && has(t2, "B waist_target 9 "));
  int h4_lines = 0;
  for (const auto& l : t2.lines)
    if (l.rfind("B p", 0) == 0 && l.find(".h4 ") != std::string::npos) ++h4_lines;
  CK(h4_lines >= 1);
  CK(!has(t0, "S waist") && !has(t0, ".h4 "));
  if (r2 && r0) CK(r2->tpr != r0->tpr);
  std::printf("waist_synthetic: converted; %d h4 trace lines\n", h4_lines);
}

// ---------------------------------------------------------------------------------------------------------------
// The oracle sections
// ---------------------------------------------------------------------------------------------------------------

struct OracleEnv {
  const char* disc = nullptr;
  const char* game = nullptr;
  const char* levels = nullptr;
  const char* traces = nullptr;
};

static bool OracleEnvOrSkip(const char* group, OracleEnv* env, bool need_traces) {
  env->disc = Env("DOA4_SUITCONV_DISC");
  env->game = Env("DOA4_SUITCONV_GAME_DIR");
  env->levels = Env("DOA4_SUITCONV_LEVELS_DIR");
  env->traces = Env("DOA4_SUITCONV_TRACE_DIR");
  if (!env->disc) return SkipGroup(group, "DOA4_SUITCONV_DISC"), false;
  if (!env->game) return SkipGroup(group, "DOA4_SUITCONV_GAME_DIR"), false;
  if (!env->levels) return SkipGroup(group, "DOA4_SUITCONV_LEVELS_DIR"), false;
  if (need_traces && !env->traces) return SkipGroup(group, "DOA4_SUITCONV_TRACE_DIR"), false;
  return true;
}

static std::unique_ptr<InputSet> g_set;
static std::string g_set_key;

static InputSet* OpenSet(const OracleEnv& env) {
  const std::string key = std::string(env.disc) + "|" + env.game;
  if (g_set && g_set_key == key) return g_set.get();
  std::string error;
  g_set = InputSet::Open(fs::path(env.disc), fs::path(env.game), DiscEntries(), GameEntries(), &error);
  CKF(g_set != nullptr, "Open: %s", error.c_str());
  g_set_key = key;
  return g_set.get();
}

static std::string Stem(const SuitRecipe& s) { return std::string(s.design) + "_" + std::to_string(s.colour); }

// The suits that convert at level m1, written once. The first 16 need no feature after the hands; the next 11 add the hair
// collision spares (Kasumi, Kokoro and Lei Fang as the wearer) or Tina's carried head physics; the next 5 carry the suit's own
// authored cloth (--port-cloth, with the collider fit, unstick, back margin and hamstring sink where the recipe gives them);
// the last 2 carry a generated cloth (--gen-cloth). The group tests derive the same set from the recipe flags and assert it
// equals this list.
static const char* const kM1Ready[] = {"vay_cs_b", "vay_on_k", "vay_sk_d", "vht_cs_f", "vht_sp_h", "vht_on_e", "vks_wi_b",
                                       "vhe_cm_a", "vhe_on_d", "vhe_dl_l", "vls_cs_h", "vls_on_j", "vls_sp_a", "vcr_cs_g",
                                       "vcr_on_n", "vcr_ch_e",  // 16
                                       "vks_ch_d", "vks_cs_a", "vks_hn_i", "vco_cs_i", "vhe_on_c", "vco_ch_f", "vle_cs_c",
                                       "vle_dl_a", "vtn_cs_e", "vtn_ch_i", "vls_on_b",  // + 11
                                       "vhe_sk_a", "vtn_sh_c", "vcr_cm_c", "vco_sk_c", "vle_on_i",  // + 5 authored cloth
                                       "vks_on_o", "vle_sk_h"};  // + 2 generated cloth
static bool InM1Ready(const SuitRecipe& s) {
  for (const char* n : kM1Ready)
    if (std::string(n) == s.design) return true;
  return false;
}
// The suits a level covers: all 34 up to L3, the kM1Ready list at m1.
static bool InLevel(Level level, const SuitRecipe& s) { return level != Level::kM1 || InM1Ready(s); }
static size_t LevelCount(Level level) { return level == Level::kM1 ? std::size(kM1Ready) : size_t(34); }

static bool OnlyAllows(const SuitRecipe& s) {
  const char* only = Env("DOA4_SUITCONV_ONLY");
  if (!only) return true;
  const std::string list = std::string(",") + only + ",";
  return list.find(std::string(",") + s.design + ",") != std::string::npos;
}

// Converts one suit at L0 and compares both files (and, when compare_trace, the trace). Returns 1 when everything
// compared is equal. *secs gets the conversion time when not traced.
static const char* LevelDir(Level l) {
  return l == Level::kL0 ? "L0" : l == Level::kL1 ? "L1" : l == Level::kL2 ? "L2" : l == Level::kL3 ? "L3" : "m1";
}

static int OracleOne(InputSet& set, const OracleEnv& env, const SuitRecipe& s, Level level, bool files,
                     bool compare_trace, double* secs) {
  const char* const lv = LevelDir(level);
  const std::string stem = Stem(s);
  const GirlRecipe* girl = FindGirl(s.girl);
  CKF(girl != nullptr, "%s: no girl %s", stem.c_str(), s.girl);
  if (!girl) return 0;
  SuitInputs in;
  try {
    in = ReadSuitInputs(set, s, *girl);
  } catch (const std::exception& e) {
    CKF(false, "%s: ReadSuitInputs: %s", stem.c_str(), e.what());
    return 0;
  }
  std::unique_ptr<CompareTrace> ct;
  if (compare_trace) {
    const auto tb = ReadFile(fs::path(env.traces) / lv / (stem + ".trace"));
    CKF(tb.has_value(), "%s: no oracle trace", stem.c_str());
    if (!tb) return 0;
    ct = std::make_unique<CompareTrace>(SplitLines(*tb));
  }
  ConvertFailure f;
  const auto t0 = std::chrono::steady_clock::now();
  const auto out = ConvertSuit(s, *girl, in, level, nullptr, ct.get(), &f);
  const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (secs) *secs = dt;
  bool ok = true;
  if (!out) {
    std::printf("  %s: ConvertSuit failed at stage %s: %s\n", stem.c_str(), f.stage.c_str(), f.what.c_str());
    ok = false;
  }
  if (ct && !ct->Equal()) {
    ct->Print(stem.c_str());
    ok = false;
  }
  if (ct) {
    g_h4_exempt += ct->h4_exempt();
    g_h4_equal += ct->h4_equal();
  }
  CKF(!ct || ct->Equal(), "%s: trace", stem.c_str());
  if (files && out) {
    const auto wt = ReadFile(fs::path(env.levels) / lv / (stem + ".tpr"));
    const auto wc = ReadFile(fs::path(env.levels) / lv / (stem + ".cat"));
    CKF(wt.has_value() && wc.has_value(), "%s: no oracle files", stem.c_str());
    if (!wt || !wc) return 0;
    const bool et = CompareFile(stem.c_str(), "tpr", out->tpr, *wt);
    const bool ec = CompareFile(stem.c_str(), "cat", out->cat, *wc);
    CKF(et, "%s .tpr equal", stem.c_str());
    CKF(ec, "%s .cat equal", stem.c_str());
    ok = ok && et && ec;
  }
  CKF(out.has_value(), "%s: converted", stem.c_str());
  return ok ? 1 : 0;
}

static void TestOracleVayOnK() {
  OracleEnv env;
  if (!OracleEnvOrSkip("oracle_L0_vay_on_k", &env, false)) return;
  Group("oracle_L0_vay_on_k");
  InputSet* set = OpenSet(env);
  if (!set) return;
  const SuitRecipe* s = nullptr;
  for (const SuitRecipe& r : Suits())
    if (std::string(r.design) == "vay_on_k") s = &r;
  CK(s != nullptr);
  if (!s) return;
  double secs = 0;
  const int eq = OracleOne(*set, env, *s, Level::kL0, true, env.traces != nullptr, &secs);
  std::printf("oracle_L0_vay_on_k: %s (%s), %.2f s\n", eq ? "equal" : "DIFFERS",
              env.traces ? "files and trace" : "files", secs);
}

static void OracleAll(Level level, const char* group) {
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, false)) return;
  Group(group);
  const char* lv = LevelDir(level);
  InputSet* set = OpenSet(env);
  if (!set) return;
  int eq = 0, n = 0;
  double total = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s)) continue;
    double secs = 0;
    const int e = OracleOne(*set, env, s, level, true, false, &secs);
    eq += e;
    ++n;
    total += secs;
    std::printf("  %s %-10s %s %.2f s\n", lv, Stem(s).c_str(), e ? "equal " : "DIFFERS", secs);
  }
  std::printf("%s: %d/%d suits equal at %s (both files), %.1f s converting (one thread)\n", group, eq, n, lv, total);
  CK(n == 34 || Env("DOA4_SUITCONV_ONLY"));
  // The same suits again in reverse order, in this process: no state may carry from one conversion to the next (the
  // Python ran each suit in a fresh process; plan row 11).
  int req = 0, rn = 0;
  const auto suits = Suits();
  for (size_t i = suits.size(); i-- > 0;) {
    if (!OnlyAllows(suits[i])) continue;
    const int e = OracleOne(*set, env, suits[i], level, true, false, nullptr);
    if (!e) std::printf("  %s reverse %-10s DIFFERS\n", lv, Stem(suits[i]).c_str());
    req += e;
    ++rn;
  }
  std::printf("%s: %d/%d suits equal at %s in reverse order (same process)\n", group, req, rn, lv);
}

static void TraceAll(Level level, const char* group) {
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, true)) return;
  Group(group);
  InputSet* set = OpenSet(env);
  if (!set) return;
  int eq = 0, n = 0;
  const size_t ex0 = g_h4_exempt, eqh0 = g_h4_equal;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s) || !InLevel(level, s)) continue;
    eq += OracleOne(*set, env, s, level, false, true, nullptr);
    ++n;
  }
  std::printf("%s: %d/%d traces equal at %s\n", group, eq, n, LevelDir(level));
  CK(size_t(n) == LevelCount(level) || Env("DOA4_SUITCONV_ONLY"));
  // The exempt `B p<i>.h4` lines (hash of the waist weights differs, part and byte count agree) are checked by absolute difference in
  // h4_abs_L2; without DOA4_SUITCONV_H4_DIR they are not checked at all, so the section counts as skipped.
  const size_t ex = g_h4_exempt - ex0, eqh = g_h4_equal - eqh0;
  std::printf("%s: %zu h4 lines exempt (hash differs), %zu h4 lines equal in full\n", group, ex, eqh);
  if (ex > 0 && !Env("DOA4_SUITCONV_H4_DIR")) SkipGroup(group, "DOA4_SUITCONV_H4_DIR");
}

static void TestOracleAll() { OracleAll(Level::kL0, "oracle_L0_all"); }
static void TestTraceAll() { TraceAll(Level::kL0, "trace_L0_all"); }
static void TestOracleL1All() { OracleAll(Level::kL1, "oracle_L1_all"); }
static void TestTraceL1All() { TraceAll(Level::kL1, "trace_L1_all"); }
static void TestOracleL2All() { OracleAll(Level::kL2, "oracle_L2_all"); }
static void TestTraceL2All() { TraceAll(Level::kL2, "trace_L2_all"); }
static void TestOracleL3All() { OracleAll(Level::kL3, "oracle_L3_all"); }
static void TestTraceL3All() { TraceAll(Level::kL3, "trace_L3_all"); }

// ---------------------------------------------------------------------------------------------------------------
// h4_abs_L2: the waist weights (p.h4, doubles) against the Python's, by absolute difference
// ---------------------------------------------------------------------------------------------------------------
// The trace hashes these doubles, but numpy's exp is the C runtime's here and the kit's Exp is within 1 ulp of it, not
// bit-equal, so the trace comparer exempts the hash of `B p<i>.h4` lines and this section checks the values instead.
// Test seam: Trace::H4, called by Tracer::State beside each such line (a no-op for every other Trace).
// Sidecar (DOA4_SUITCONV_H4_DIR/L2/<design>_<colour>.h4): a file of the Python's h4 doubles, written by an out-of-repo
// wrapper around prep_waist_fit. Format: magic "SH4\1", u32 part count; per part u32 trace index, u32 vertex count,
// count*4 little-endian float64; nothing after the last part. The section fails a suit whose sidecar has fewer parts
// than its header says, a part count different from the C++ run's, or trailing bytes.

// The largest absolute difference any waist weight may have from the Python's. Measured maximum: 2.22e-16 over 391,104
// values (34 suits, L2); the bound is twice that. The weights are row-normalised, so an error of 1 ulp in exp's argument
// is an absolute error of about 1 ulp of 1.0 however small the weight: ulp distance is the wrong metric for them. A real
// waist bug (a wrong band, grid or blend) moves weights by far more.
static constexpr double kH4MaxAbs = 0x1p-51;  // 4.44e-16

class H4Trace : public Trace {
 public:
  void Line(std::string_view) override {}
  void H4(int index, const std::vector<std::array<double, 4>>& rows) override { parts.emplace(index, rows); }  // first wins
  std::map<int, std::vector<std::array<double, 4>>> parts;
};

// Distance in representable doubles; +0 and -0 are 0 apart. NaN must be excluded by the caller.
static uint64_t UlpDistance(double a, double b) {
  auto key = [](double x) {
    const int64_t k = std::bit_cast<int64_t>(x);
    return k < 0 ? (std::numeric_limits<int64_t>::min)() - k : k;
  };
  const int64_t ka = key(a), kb = key(b);
  return ka > kb ? uint64_t(ka) - uint64_t(kb) : uint64_t(kb) - uint64_t(ka);
}

static void H4Abs(Level level, const char* group) {
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, false)) return;
  const char* h4dir = Env("DOA4_SUITCONV_H4_DIR");
  if (!h4dir) return SkipGroup(group, "DOA4_SUITCONV_H4_DIR");
  Group(group);
  InputSet* set = OpenSet(env);
  if (!set) return;
  int suits = 0, parts = 0;
  uint64_t values = 0, differ = 0, max_ulp = 0;
  std::string max_at;
  uint64_t hist[12] = {};  // ulp distance: 0, 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65-128, 129-256, 257-512, more
  double max_abs = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s) || !InLevel(level, s)) continue;
    const std::string stem = Stem(s);
    const GirlRecipe* girl = FindGirl(s.girl);
    CKF(girl != nullptr, "%s: no girl", stem.c_str());
    if (!girl) continue;
    const auto sb = ReadFile(fs::path(h4dir) / LevelDir(level) / (stem + ".h4"));
    CKF(sb.has_value(), "%s: no h4 sidecar", stem.c_str());
    if (!sb) continue;
    SuitInputs in;
    try {
      in = ReadSuitInputs(*set, s, *girl);
    } catch (const std::exception& e) {
      CKF(false, "%s: ReadSuitInputs: %s", stem.c_str(), e.what());
      continue;
    }
    H4Trace tr;
    ConvertFailure f;
    const auto out = ConvertSuit(s, *girl, in, level, nullptr, &tr, &f);
    CKF(out.has_value(), "%s: converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    if (!out) continue;
    ++suits;
    // parse the sidecar
    const std::vector<uint8_t>& b = *sb;
    auto u32 = [&](size_t o) { uint32_t v = 0; std::memcpy(&v, b.data() + o, 4); return v; };
    bool shape = b.size() >= 8 && std::memcmp(b.data(), "SH4\1", 4) == 0;
    CKF(shape, "%s: sidecar magic", stem.c_str());
    if (!shape) continue;
    const uint32_t np = u32(4);
    size_t o = 8;
    bool same_parts = np == tr.parts.size();
    uint32_t read_parts = 0;  // parts actually read whole from the sidecar and matched
    for (uint32_t k = 0; k < np && o + 8 <= b.size(); ++k) {
      const uint32_t idx = u32(o), cnt = u32(o + 4);
      o += 8;
      if (o + size_t(cnt) * 32 > b.size()) {
        same_parts = false;
        break;
      }
      const auto it = tr.parts.find(int(idx));
      if (it == tr.parts.end() || it->second.size() != cnt) {
        same_parts = false;
        o += size_t(cnt) * 32;
        continue;
      }
      ++parts;
      ++read_parts;
      for (uint32_t r = 0; r < cnt; ++r)
        for (int c = 0; c < 4; ++c) {
          double want;
          std::memcpy(&want, b.data() + o + (size_t(r) * 4 + c) * 8, 8);
          const double got = it->second[r][c];
          ++values;
          if (std::isnan(got) || std::isnan(want)) {
            CKF(false, "%s p%u row %u col %d: NaN (C++ %g, Python %g)", stem.c_str(), idx, r, c, got, want);
            continue;
          }
          const uint64_t d = UlpDistance(got, want);
          if (d) ++differ;
          {
            int hb = 0;
            for (uint64_t lim = 0; hb < 11 && d > lim; ++hb) lim = lim ? lim * 2 : 1;
            ++hist[hb];
            max_abs = (std::max)(max_abs, std::fabs(got - want));
          }
          if (d > max_ulp) {
            max_ulp = d;
            max_at = stem + " p" + std::to_string(idx) + " row " + std::to_string(r) + " col " + std::to_string(c);
          }
          if (!(std::fabs(got - want) <= kH4MaxAbs))
            CKF(false, "%s p%u row %u col %d: C++ %.17g, Python %.17g, |diff| %.3g > bound %.3g", stem.c_str(), idx, r, c,
                got, want, std::fabs(got - want), kH4MaxAbs);
        }
      o += size_t(cnt) * 32;
    }
    // complete: every part the header announces was read, that is the C++ part count too, and nothing follows the last part
    same_parts = same_parts && read_parts == np && read_parts == tr.parts.size() && o == b.size();
    CKF(same_parts,
        "%s: part indices and vertex counts agree with the sidecar (C++ %zu parts, sidecar header %u, read %u, %zu trailing bytes)",
        stem.c_str(), tr.parts.size(), np, read_parts, o < b.size() ? b.size() - o : size_t(0));
  }
  std::printf("%s: %d suits, %d parts, %llu values: %llu differ, max abs diff %.3g (bound %.3g), max %llu ulp (information) at %s\n", group,
              suits, parts, (unsigned long long)values, (unsigned long long)differ, max_abs, kH4MaxAbs,
              (unsigned long long)max_ulp, max_at.empty() ? "-" : max_at.c_str());
  std::printf("%s: ulp histogram (information) 0:%llu 1:%llu 2:%llu 3-4:%llu 5-8:%llu 9-16:%llu 17-32:%llu 33-64:%llu 65-128:%llu 129-256:%llu 257-512:%llu more:%llu\n", group,
              (unsigned long long)hist[0], (unsigned long long)hist[1], (unsigned long long)hist[2], (unsigned long long)hist[3], (unsigned long long)hist[4], (unsigned long long)hist[5], (unsigned long long)hist[6], (unsigned long long)hist[7], (unsigned long long)hist[8], (unsigned long long)hist[9], (unsigned long long)hist[10], (unsigned long long)hist[11]);
  CK(size_t(suits) == LevelCount(level) || Env("DOA4_SUITCONV_ONLY"));
  CK(values > 0);
}

// ---------------------------------------------------------------------------------------------------------------
// oracle_m1_without_extras, oracle_m1_spares_head: at level m1 the suits kM1Ready lists equal the shipped m1 files
// ---------------------------------------------------------------------------------------------------------------
// DOA4_SUITCONV_ORACLE_DIR is the folder of the shipped m1 outputs (<design>_<colour>.tpr / .cat). Every suit is ready at m1.
// The 16 with no cloth feature (none of port_cloth, gen_cloth, fit_leg_colliders, unstick_cloth, back_margin,
// sink_hamstrings) whose girl has neither hair_spares nor head_physics need nothing after the hands: the recipe's m1 output
// is its L3 output, so the C++ m1 must equal both the shipped file and the C++ L3 (oracle_m1_without_extras). The 11 with no
// cloth feature that add the hair collision spares (the girls vks, vco, vle) or Tina's head physics must equal the shipped
// file while differing from the C++ L3 (oracle_m1_spares_head). The 5 with the authored cloth are oracle_m1_cloth, the 2
// with the generated cloth oracle_m1_gen.

static bool ClothGatesFree(const SuitRecipe& s) {
  return !s.port_cloth && !s.gen_cloth && !s.fit_leg_colliders && !s.unstick_cloth && s.back_margin == 0.0 &&
         s.sink_hamstrings == 0.0;
}

static std::string Names(const std::set<std::string>& v) {
  std::string names;
  for (const std::string& d : v) names += (names.empty() ? "" : " ") + d;
  return names;
}

static void TestOracleM1WithoutExtras() {
  const char* group = "oracle_m1_without_extras";
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, false)) return;
  const char* odir = Env("DOA4_SUITCONV_ORACLE_DIR");
  if (!odir) return SkipGroup(group, "DOA4_SUITCONV_ORACLE_DIR");
  Group(group);
  InputSet* set = OpenSet(env);
  if (!set) return;
  std::set<std::string> want_ready(std::begin(kM1Ready), std::end(kM1Ready)), derived;
  int equal = 0, same_as_l3 = 0, elsewhere = 0, n = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s)) continue;
    ++n;
    const std::string stem = Stem(s);
    const GirlRecipe* girl = FindGirl(s.girl);
    CKF(girl != nullptr, "%s: no girl", stem.c_str());
    if (!girl) continue;
    const bool free = ClothGatesFree(s) && !girl->hair_spares && !girl->head_physics;
    derived.insert(s.design);
    SuitInputs in;
    try {
      in = ReadSuitInputs(*set, s, *girl);
    } catch (const std::exception& e) {
      CKF(false, "%s: ReadSuitInputs: %s", stem.c_str(), e.what());
      continue;
    }
    ConvertFailure f;
    if (!free) {  // the spares and head physics suits (oracle_m1_spares_head), the cloth suits (oracle_m1_cloth, _gen)
      ++elsewhere;
      continue;
    }
    const auto m1 = ConvertSuit(s, *girl, in, Level::kM1, nullptr, nullptr, &f);
    CKF(m1.has_value(), "%s: m1 converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    if (!m1) continue;
    const auto wt = ReadFile(fs::path(odir) / (stem + ".tpr"));
    const auto wc = ReadFile(fs::path(odir) / (stem + ".cat"));
    CKF(wt.has_value() && wc.has_value(), "%s: no shipped m1 files", stem.c_str());
    if (!wt || !wc) continue;
    const bool et = CompareFile(stem.c_str(), "tpr", m1->tpr, *wt);
    const bool ec = CompareFile(stem.c_str(), "cat", m1->cat, *wc);
    CKF(et, "%s m1 .tpr equals the shipped file", stem.c_str());
    CKF(ec, "%s m1 .cat equals the shipped file", stem.c_str());
    if (et && ec) ++equal;
    const auto l3 = ConvertSuit(s, *girl, in, Level::kL3, nullptr, nullptr, &f);
    CKF(l3.has_value(), "%s: L3 converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    if (l3) {
      const bool same = l3->tpr == m1->tpr && l3->cat == m1->cat;
      CKF(same, "%s: C++ L3 equals C++ m1", stem.c_str());
      if (same) ++same_as_l3;
    }
  }
  std::printf("%s: derived list (%zu): %s\n", group, derived.size(), Names(derived).c_str());
  std::printf("%s: %d/16 equal to the shipped m1 files, %d equal to C++ L3, %d suits elsewhere ("
              "oracle_m1_spares_head, oracle_m1_cloth, oracle_m1_gen)\n",
              group, equal, same_as_l3, elsewhere);
  if (!Env("DOA4_SUITCONV_ONLY")) {
    CKF(derived == want_ready, "the derived list is kM1Ready's %zu (%zu derived)", want_ready.size(), derived.size());
    CK(n == 34 && elsewhere == 18);
    CK(equal == 16 && same_as_l3 == 16);
  }
}

static void TestOracleM1SparesHead() {
  const char* group = "oracle_m1_spares_head";
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, false)) return;
  const char* odir = Env("DOA4_SUITCONV_ORACLE_DIR");
  if (!odir) return SkipGroup(group, "DOA4_SUITCONV_ORACLE_DIR");
  Group(group);
  InputSet* set = OpenSet(env);
  if (!set) return;
  std::set<std::string> want_eleven(std::begin(kM1Ready) + 16, std::begin(kM1Ready) + 27), derived;
  int equal = 0, changed = 0, n = 0, spares_suits = 0, head_suits = 0;
  uint64_t pin_rows = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s)) continue;
    const std::string stem = Stem(s);
    const GirlRecipe* girl = FindGirl(s.girl);
    CKF(girl != nullptr, "%s: no girl", stem.c_str());
    if (!girl) continue;
    if (!ClothGatesFree(s) || !(girl->hair_spares || girl->head_physics)) continue;
    ++n;
    derived.insert(s.design);
    SuitInputs in;
    try {
      in = ReadSuitInputs(*set, s, *girl);
    } catch (const std::exception& e) {
      CKF(false, "%s: ReadSuitInputs: %s", stem.c_str(), e.what());
      continue;
    }
    ConvertFailure f;
    const auto m1 = ConvertSuit(s, *girl, in, Level::kM1, nullptr, nullptr, &f);
    CKF(m1.has_value(), "%s: m1 converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    if (!m1) continue;
    const auto wt = ReadFile(fs::path(odir) / (stem + ".tpr"));
    const auto wc = ReadFile(fs::path(odir) / (stem + ".cat"));
    CKF(wt.has_value() && wc.has_value(), "%s: no shipped m1 files", stem.c_str());
    if (!wt || !wc) continue;
    const bool et = CompareFile(stem.c_str(), "tpr", m1->tpr, *wt);
    const bool ec = CompareFile(stem.c_str(), "cat", m1->cat, *wc);
    CKF(et, "%s m1 .tpr equals the shipped file", stem.c_str());
    CKF(ec, "%s m1 .cat equals the shipped file", stem.c_str());
    if (et && ec) ++equal;
    // vacuity control: the feature wrote something, so the m1 .cat is not the L3 .cat
    const auto l3 = ConvertSuit(s, *girl, in, Level::kL3, nullptr, nullptr, &f);
    CKF(l3.has_value(), "%s: L3 converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    if (l3 && l3->cat != m1->cat) ++changed;
    CKF(l3 && l3->cat != m1->cat, "%s: m1 .cat differs from L3", stem.c_str());
    const auto& cat = m1->cat;
    if (girl->hair_spares) {  // a MULTI_CLOTH at +0x14, and the MPV's pin rows (MPP +0x44) it relocates
      ++spares_suits;
      CKF(GetBe(cat, 0x14) != 0, "%s: no MULTI_CLOTH", stem.c_str());
      const uint32_t mpv_at = GetBe(cat, 0xC);
      const uint32_t mpp = mpv_at + GetBe(cat, mpv_at + 36 + 16);
      pin_rows += GetBe(cat, mpp + 0x44);
    }
    if (girl->head_physics) {  // the carried physics section at +8
      ++head_suits;
      CKF(GetBe(cat, 8) != 0, "%s: no head physics section", stem.c_str());
    }
  }
  std::printf("%s: derived list (%zu): %s\n", group, derived.size(), Names(derived).c_str());
  std::printf("%s: %d/%d equal to the shipped m1 files, %d differ from C++ L3; %d spares suits, %d head physics suits, "
              "MPP pin rows relocated: %llu\n",
              group, equal, n, changed, spares_suits, head_suits, (unsigned long long)pin_rows);
  if (!Env("DOA4_SUITCONV_ONLY")) {
    CKF(derived == want_eleven, "the derived list is the 11 spares and head-physics suits (%zu derived)", derived.size());
    CK(n == 11 && equal == 11 && changed == 11);
    CK(spares_suits + head_suits == 11);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// oracle_m1_cloth: the 5 suits with the suit's own authored cloth (--port-cloth) at m1
// ---------------------------------------------------------------------------------------------------------------
// Each must equal the shipped m1 files and differ from the C++ L3 (the cloth is attached at +0x14). The MPP pin rows
// the suit reaches (MPP +0x44) must equal the count of the oracle's `pins` trace stage (`K nrows N`; 0 when the stage is
// absent: the cloth is not on the chest object); with DOA4_SUITCONV_TRACE_DIR unset the count check is skipped. On real
// data each relocated row must point at its pin: the row's first word w (written by attach as at + rel - mpp) puts the
// cloth's FIX_GRID pin record at file offset mpp + w, whose first 12 bytes are the pin's rest point, the same 3 floats
// as the row's rest.
static bool AuthoredCloth(const SuitRecipe& s) { return s.port_cloth && !s.gen_cloth; }

static int64_t OracleNrows(const OracleEnv& env, const std::string& stem) {
  if (!env.traces) return -1;
  const auto tb = ReadFile(fs::path(env.traces) / "m1" / (stem + ".trace"));
  if (!tb) return -2;
  for (const std::string& l : SplitLines(*tb))
    if (l.rfind("K nrows ", 0) == 0) return std::stoll(l.substr(8));
  return 0;
}

static void TestOracleM1Cloth() {
  const char* group = "oracle_m1_cloth";
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, false)) return;
  const char* odir = Env("DOA4_SUITCONV_ORACLE_DIR");
  if (!odir) return SkipGroup(group, "DOA4_SUITCONV_ORACLE_DIR");
  Group(group);
  InputSet* set = OpenSet(env);
  if (!set) return;
  const std::set<std::string> want_five(std::begin(kM1Ready) + 27, std::begin(kM1Ready) + 32);
  std::set<std::string> derived;
  int equal = 0, changed = 0, n = 0, rows_checked = 0, counts_checked = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s) || !AuthoredCloth(s)) continue;
    ++n;
    derived.insert(s.design);
    const std::string stem = Stem(s);
    const GirlRecipe* girl = FindGirl(s.girl);
    CKF(girl != nullptr, "%s: no girl", stem.c_str());
    if (!girl) continue;
    SuitInputs in;
    try {
      in = ReadSuitInputs(*set, s, *girl);
    } catch (const std::exception& e) {
      CKF(false, "%s: ReadSuitInputs: %s", stem.c_str(), e.what());
      continue;
    }
    ConvertFailure f;
    const auto m1 = ConvertSuit(s, *girl, in, Level::kM1, nullptr, nullptr, &f);
    CKF(m1.has_value(), "%s: m1 converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    if (!m1) continue;
    const auto wt = ReadFile(fs::path(odir) / (stem + ".tpr"));
    const auto wc = ReadFile(fs::path(odir) / (stem + ".cat"));
    CKF(wt.has_value() && wc.has_value(), "%s: no shipped m1 files", stem.c_str());
    if (!wt || !wc) continue;
    const bool et = CompareFile(stem.c_str(), "tpr", m1->tpr, *wt);
    const bool ec = CompareFile(stem.c_str(), "cat", m1->cat, *wc);
    CKF(et, "%s m1 .tpr equals the shipped file", stem.c_str());
    CKF(ec, "%s m1 .cat equals the shipped file", stem.c_str());
    if (et && ec) ++equal;
    const auto l3 = ConvertSuit(s, *girl, in, Level::kL3, nullptr, nullptr, &f);
    CKF(l3.has_value(), "%s: L3 converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    if (l3 && l3->cat != m1->cat) ++changed;
    CKF(l3 && l3->cat != m1->cat, "%s: m1 .cat differs from L3", stem.c_str());
    const auto& cat = m1->cat;
    CKF(cat.size() >= 0x18 && GetBe(cat, 0x14) != 0, "%s: no MULTI_CLOTH at +0x14", stem.c_str());
    if (cat.size() < 0x18 || GetBe(cat, 0xC) == 0) {
      CKF(false, "%s: no MPV", stem.c_str());
      continue;
    }
    const uint32_t mpv_at = GetBe(cat, 0xC);
    const uint32_t mpp = mpv_at + GetBe(cat, mpv_at + 36 + 16);
    const uint32_t nrows = GetBe(cat, mpp + 0x44);
    const int64_t want_rows = OracleNrows(env, stem);
    if (want_rows >= 0) {
      ++counts_checked;
      CKF(int64_t(nrows) == want_rows, "%s: MPP pin rows %u, the oracle's pins stage %lld", stem.c_str(), nrows,
          (long long)want_rows);
    }
    const uint32_t block = GetBe(cat, mpp + 0x40);
    for (uint32_t i = 0; i < nrows; ++i) {
      const size_t row = size_t(mpp) + block + 40 * size_t(i);
      const size_t pin = size_t(mpp) + GetBe(cat, row);
      const bool in_range = row + 40 <= cat.size() && pin + 12 <= cat.size() && pin >= GetBe(cat, 0x14);
      CKF(in_range && std::equal(cat.begin() + row + 4, cat.begin() + row + 16, cat.begin() + pin),
          "%s: pin row %u does not point at its pin's rest point", stem.c_str(), i);
      ++rows_checked;
    }
    std::printf("  %s: m1 %s, MPP pin rows %u (oracle pins stage %lld)\n", stem.c_str(), et && ec ? "equal " : "DIFFERS",
                nrows, (long long)want_rows);
  }
  std::printf("%s: derived list (%zu): %s\n", group, derived.size(), Names(derived).c_str());
  std::printf("%s: %d/%d equal to the shipped m1 files, %d differ from C++ L3, %d pin counts checked, %d relocated pin rows "
              "checked on real data\n",
              group, equal, n, changed, counts_checked, rows_checked);
  if (!Env("DOA4_SUITCONV_ONLY")) {
    CKF(derived == want_five, "the derived list is the 5 authored-cloth suits (%zu derived)", derived.size());
    CK(n == 5 && equal == 5 && changed == 5);
    CK(!env.traces || counts_checked == 5);
    CK(!env.traces || rows_checked > 0);  // the relocation runs on real data
  }
}

// ---------------------------------------------------------------------------------------------------------------
// oracle_m1_gen: the two suits with the generated cloth equal the shipped m1 files
// ---------------------------------------------------------------------------------------------------------------
// vks_on_o (--gen-cloth, the girl's hair spares) and vle_sk_h (--gen-cloth with --unstick-cloth, the girl's hair spares).
// Besides the files (the trace compare is trace_m1's) this asserts the paths these two suits reach for the first time, on
// real data: fit_cloth_chunk on a generated chunk (vle_sk_h only: the stage exists, the chunk changes and keeps its size),
// with_spares onto the suit's own cloth (both: the spares stage is `B spares`, longer than the cloth it started from), and
// attach's cloth path with no pin rows (the MPP pin-row count is 0 and the oracle's trace has no `pins` stage). The L3
// vacuity check: the m1 .cat differs from the C++ L3 .cat (the cloth is in it).
static std::string TraceLine(const std::vector<std::string>& lines, const std::string& prefix) {
  for (const std::string& l : lines)
    if (l.rfind(prefix, 0) == 0) return l;
  return std::string();
}
static size_t TraceLength(const std::string& b_line) {  // "B <name> <length> <sha1>"
  const size_t a = b_line.find(' ', 2);
  return a == std::string::npos ? 0 : size_t(std::stoull(b_line.substr(a + 1)));
}
static std::string TraceSha(const std::string& b_line) { return b_line.substr(b_line.rfind(' ') + 1); }

static void TestOracleM1Gen() {
  const char* group = "oracle_m1_gen";
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, false)) return;
  const char* odir = Env("DOA4_SUITCONV_ORACLE_DIR");
  if (!odir) return SkipGroup(group, "DOA4_SUITCONV_ORACLE_DIR");
  Group(group);
  InputSet* set = OpenSet(env);
  if (!set) return;
  const std::set<std::string> want_two(std::begin(kM1Ready) + 32, std::end(kM1Ready));
  std::set<std::string> derived;
  int equal = 0, changed = 0, n = 0, fit_suits = 0, spares_checked = 0, nopin_checked = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s) || !s.gen_cloth) continue;
    ++n;
    derived.insert(s.design);
    const std::string stem = Stem(s);
    const GirlRecipe* girl = FindGirl(s.girl);
    CKF(girl != nullptr, "%s: no girl", stem.c_str());
    if (!girl) continue;
    SuitInputs in;
    try {
      in = ReadSuitInputs(*set, s, *girl);
    } catch (const std::exception& e) {
      CKF(false, "%s: ReadSuitInputs: %s", stem.c_str(), e.what());
      continue;
    }
    CKF(in.gen_donor_cat && in.gen_bend_cat, "%s: the gen donor and bend inputs are read", stem.c_str());
    ConvertFailure f;
    MemTrace mt;
    const auto m1 = ConvertSuit(s, *girl, in, Level::kM1, nullptr, &mt, &f);
    CKF(m1.has_value(), "%s: m1 converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    if (!m1) continue;
    const auto wt = ReadFile(fs::path(odir) / (stem + ".tpr"));
    const auto wc = ReadFile(fs::path(odir) / (stem + ".cat"));
    CKF(wt.has_value() && wc.has_value(), "%s: no shipped m1 files", stem.c_str());
    if (!wt || !wc) continue;
    const bool et = CompareFile(stem.c_str(), "tpr", m1->tpr, *wt);
    const bool ec = CompareFile(stem.c_str(), "cat", m1->cat, *wc);
    CKF(et, "%s m1 .tpr equals the shipped file", stem.c_str());
    CKF(ec, "%s m1 .cat equals the shipped file", stem.c_str());
    if (et && ec) ++equal;
    const auto l3 = ConvertSuit(s, *girl, in, Level::kL3, nullptr, nullptr, &f);
    CKF(l3.has_value(), "%s: L3 converted (%s: %s)", stem.c_str(), f.stage.c_str(), f.what.c_str());
    CKF(l3 && l3->cat != m1->cat, "%s: m1 .cat differs from L3", stem.c_str());
    if (l3 && l3->cat != m1->cat) ++changed;

    // the stages the generated cloth adds, in the order the recipe runs them
    const auto find = [&](const char* line) -> int {
      for (size_t i = 0; i < mt.lines.size(); ++i)
        if (mt.lines[i] == line) return int(i);
      return -1;
    };
    const int i_prep = find("S gen_prep"), i_gen = find("S gen_cloth"), i_fit = find("S fit"), i_mpv = find("S mpv"),
              i_spares = find("S spares"), i_pins = find("S pins");
    CKF(i_prep >= 0 && i_gen > i_prep && i_mpv > i_gen && i_spares > i_mpv, "%s: stage order gen_prep < gen_cloth < mpv < spares",
        stem.c_str());
    CKF(i_pins < 0, "%s: no pins stage (there is no pin table without authored cloth)", stem.c_str());
    const std::string gen_line = TraceLine(mt.lines, "B gen_cloth ");
    const std::string fit_line = TraceLine(mt.lines, "B fit ");
    const std::string spares_line = TraceLine(mt.lines, "B spares ");
    CKF(!gen_line.empty() && !spares_line.empty(), "%s: B gen_cloth and B spares lines", stem.c_str());
    CKF(TraceLine(mt.lines, "K spares none").empty(), "%s: with_spares had a cloth to start from", stem.c_str());
    if (!gen_line.empty() && !spares_line.empty()) {
      CKF(TraceLength(spares_line) > TraceLength(gen_line), "%s: the spares make the cloth longer (%zu -> %zu)", stem.c_str(),
          TraceLength(gen_line), TraceLength(spares_line));
      ++spares_checked;
    }
    if (s.unstick_cloth || s.fit_leg_colliders) {  // fit_cloth_chunk on the generated chunk (vle_sk_h)
      ++fit_suits;
      CKF(i_fit > i_gen && i_fit < i_mpv, "%s: S fit between gen_cloth and mpv", stem.c_str());
      CKF(!fit_line.empty() && !gen_line.empty() && TraceLength(fit_line) == TraceLength(gen_line) &&
              TraceSha(fit_line) != TraceSha(gen_line),
          "%s: fit_cloth_chunk changes the generated chunk and keeps its size", stem.c_str());
    } else {
      CKF(i_fit < 0 && fit_line.empty(), "%s: no fit stage", stem.c_str());
    }
    // attach's cloth path with no pin rows: the MPP's pin-row count is 0, as the oracle's trace says (no K nrows line)
    const auto& cat = m1->cat;
    if (cat.size() < 0x18 || GetBe(cat, 0xC) == 0 || GetBe(cat, 0x14) == 0) {
      CKF(false, "%s: no MPV or no MULTI_CLOTH in the .cat", stem.c_str());
      continue;
    }
    const uint32_t mpv_at = GetBe(cat, 0xC);
    const uint32_t mpp = mpv_at + GetBe(cat, mpv_at + 36 + 16);
    const uint32_t nrows = GetBe(cat, mpp + 0x44);
    CKF(nrows == 0, "%s: MPP pin rows %u, want 0", stem.c_str(), nrows);
    if (env.traces) {
      const int64_t want_rows = OracleNrows(env, stem);
      CKF(want_rows == 0, "%s: the oracle trace's pin rows %lld, want 0 (no pins stage)", stem.c_str(), (long long)want_rows);
      ++nopin_checked;
    }
    std::printf("  %s: m1 %s, gen_cloth %zu bytes, spares %zu bytes, %s, MPP pin rows %u\n", stem.c_str(),
                et && ec ? "equal " : "DIFFERS", TraceLength(gen_line), TraceLength(spares_line),
                fit_line.empty() ? "no fit" : "fit ran", nrows);
  }
  std::printf("%s: derived list (%zu): %s\n", group, derived.size(), Names(derived).c_str());
  std::printf("%s: %d/%d equal to the shipped m1 files, %d differ from C++ L3, %d with fit_cloth_chunk, %d with_spares checks, "
              "%d pin-row checks\n",
              group, equal, n, changed, fit_suits, spares_checked, nopin_checked);
  if (!Env("DOA4_SUITCONV_ONLY")) {
    CKF(derived == want_two, "the derived list is the 2 generated-cloth suits (%zu derived)", derived.size());
    CK(n == 2 && equal == 2 && changed == 2);
    CK(fit_suits == 1 && spares_checked == 2);
    CK(!env.traces || nopin_checked == 2);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// gen_cloth_synthetic, walk_rejects_unterminated_writes, gen_prep_synthetic
// ---------------------------------------------------------------------------------------------------------------
// A hand-made skirt (no game byte): an 8-sided tube of 3 rows whose seam column is stored twice (27 vertices, 24 nodes after
// the weld), a second mesh whose pass is not a skirt mesh (its triangle must be left out), synthetic donors with the offsets
// cloth_gen reads. The expected chunk lengths and SHA-1s are what the recipe's own cloth_gen.build produced for the same
// input in the recipe's interpreter. The seam duplicate sits at x = 0.00025, which divides by the weld size 0.0005 to exactly
// 0.5: round() ties to even and welds it to the column at x = 0 (key 0); a round-half-away weld key (key 1) leaves it alone.
namespace gensy {
using doa4::suitconv::core::Vec3;

static void PutWords(std::vector<uint8_t>& b, size_t at, std::initializer_list<uint32_t> words) {
  for (uint32_t w : words) {
    b[at] = uint8_t(w >> 24);
    b[at + 1] = uint8_t(w >> 16);
    b[at + 2] = uint8_t(w >> 8);
    b[at + 3] = uint8_t(w);
    at += 4;
  }
}
static void PutBytes(std::vector<uint8_t>& b, size_t at, const char* s, size_t n) {
  for (size_t i = 0; i < n; ++i) b[at + i] = uint8_t(s[i]);
}

static std::vector<uint8_t> Donor() {
  std::vector<uint8_t> d = synth::Seeded(7, 0x80000);
  const std::pair<size_t, const char*> clouds[] = {{0x538C0, "ks_skirt"}, {0x71450, "ht_skirt"}};
  for (const auto& [base, name] : clouds) {
    const size_t c = base + 0x50;
    PutWords(d, base + 0x1C, {0x40});
    PutWords(d, base + 0x40, {0x50});
    PutBytes(d, c, "CLOTH_DATA", 10);
    for (size_t i = 10; i < 16; ++i) d[c + i] = 0;
    PutWords(d, c + 0x18, {0xF0});
    PutWords(d, c + 0x20, {0x80});
    PutWords(d, c + 0x38, {0x120});
    PutBytes(d, c + 0x70, name, 8);
    PutWords(d, c + 0x80 + 0x34, {std::bit_cast<uint32_t>(1.5f)});
    PutWords(d, c + 0x80 + 0x5C, {std::bit_cast<uint32_t>(0.75f)});
    PutWords(d, c + 0x120 + 8, {0x20});
    PutWords(d, c + 0x120 + 0x20 + 8, {0});
  }
  return d;
}
static std::vector<uint8_t> Bend() {
  std::vector<uint8_t> b = synth::Seeded(9, 0x600);
  const size_t mm = 0x100;
  PutBytes(b, mm, "MULTI_CLOTH", 11);
  b[mm + 11] = 0;
  PutWords(b, mm + 0x1C, {0x40});
  PutWords(b, mm + 0x40, {0x50});
  const size_t mc0 = mm + 0x50;
  PutWords(b, mc0 + 0x34, {0x100});
  const size_t th0 = mc0 + 0x100;
  PutWords(b, th0 + 0x28, {0x60});
  PutWords(b, th0 + 0x60, {1, 0x40});
  PutWords(b, th0 + 0x60 + 0x2C, {2});
  return b;
}

constexpr Vec3 kOwn{0.0, 0.25, -0.125};

struct Skirt {
  doa4::suitconv::core::Model suit;
  doa4::suitconv::core::PartArena arena;
  doa4::suitconv::core::Part* part = nullptr;
  std::vector<tp::ObjGeo> geos;
  doa4::suitconv::core::PyDict<int64_t, Vec3> bind4;
};

static void MakeSkirt(Skirt& sk) {
  namespace core = doa4::suitconv::core;
  const double ring[8][2] = {{0, 1}, {-0.75, 0.75}, {-1, 0}, {-0.75, -0.75}, {0, -1}, {0.75, -0.75}, {1, 0}, {0.75, 0.75}};
  const double yrow[3] = {1.0, 0.75, 0.5};
  core::Part* p = sk.arena.New();
  sk.part = p;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 9; ++c) {
      const int k = c % 8;
      double x = ring[k][0] * 0.25;
      const double z = ring[k][1] * 0.25;
      if (c >= 1 && c <= 7) x += double((c * 7 + r) % 3) * 0x1p-10;
      const double y = yrow[r] + double((r * 5 + k * 3) % 4) * 0x1p-8;
      if (c == 8 && r == 0) x = 0.00025;
      core::Vert v;
      v.pos = Vec3{x, y + kOwn[1], z + kOwn[2]};
      p->verts.push_back(v);
    }
  std::vector<uint16_t> idx;
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < 9; ++c) {
      idx.push_back(uint16_t(r * 9 + c));
      idx.push_back(uint16_t((r + 1) * 9 + c));
    }
    idx.push_back(0xFFFF);
  }
  for (uint16_t i : {0, 1, 2}) idx.push_back(i);
  p->icount = uint32_t(idx.size());
  for (uint16_t i : idx) {
    p->ib.push_back(uint8_t(i >> 8));
    p->ib.push_back(uint8_t(i));
  }
  for (const auto& [st, cnt] : {std::pair<uint32_t, uint32_t>{0, 38}, {38, 3}}) {
    std::vector<uint8_t> m(0x40, 0);
    PutWords(m, 0x10, {st, cnt});
    p->meshes.push_back(m);
  }
  p->passes = {core::PassRec{1, 0, 0}, core::PassRec{5, 0, 0}};
  tp::Decl d;
  d.head = {0, 1, 0, uint32_t(idx.size()), 3, 27, 0, 0, 1, 0, 0, 0};
  d.elems = {tp::DeclElem{0, 0, 0x2A23B9, 0, 0, 0, 0}, tp::DeclElem{0, 12, 0x2A23B9, 0, 3, 0, 0},
             tp::DeclElem{0xFF, 0, 0, 0, 0, 0, 0}};
  std::vector<uint8_t> tail(16, 0);
  PutWords(tail, 4, {24});
  d.tails = {tail};
  sk.geos.resize(2);
  sk.geos[1].decls = {d, d};
  sk.bind4.Set(1, kOwn);
}

struct Pin {
  const char* design;
  size_t length;
  const char* sha1;
};
static const Pin kPins[] = {{"vks_on_o", 7904, "3a012dac510bf2b612ddd36da3c733ad88832148"},
                            {"vle_sk_h", 7664, "2d7e599e93107b2dedd109cd16d577ee41611442"}};

static uint32_t U32At(const std::vector<uint8_t>& b, size_t at) { return GetBe(b, at); }
static std::string What(const std::exception& e) { return e.what(); }
}  // namespace gensy

static void TestGenClothSynthetic() {
  Group("gen_cloth_synthetic");
  namespace gc = doa4::suitconv::gencloth;
  const num::FpEnvGuard fp;
  const std::vector<uint8_t> donor = gensy::Donor(), bend = gensy::Bend();
  std::vector<uint8_t> first;
  for (const gensy::Pin& pin : gensy::kPins) {
    gensy::Skirt sk;
    gensy::MakeSkirt(sk);
    gc::Spec spec{pin.design, 1, 1, sk.part, {1}};
    std::vector<uint8_t> chunk;
    try {
      chunk = gc::Build(spec, sk.geos, sk.bind4, donor, bend);
    } catch (const std::exception& e) {
      CKF(false, "%s: Build: %s", pin.design, e.what());
      continue;
    }
    CKF(chunk.size() == pin.length, "%s: chunk length %zu, the recipe's %zu", pin.design, chunk.size(), pin.length);
    CKF(trace::Sha1Hex(chunk) == pin.sha1, "%s: chunk sha1 %s, the recipe's %s", pin.design, trace::Sha1Hex(chunk).c_str(),
        pin.sha1);
    // the cloth's own counts (24 welded nodes, 32 faces, 8 pins): GRID_DATA header words at the grid, FIX_GRID count
    const size_t c = 0x50;
    const size_t grid = c + gensy::U32At(chunk, c + 0x24);
    CKF(gensy::U32At(chunk, grid + 0x24) == 24 && gensy::U32At(chunk, grid + 0x2C) == 32, "%s: 24 nodes, 32 faces", pin.design);
    const size_t fix = c + gensy::U32At(chunk, c + 0x2C);
    CKF(gensy::U32At(chunk, fix + 0x1C) == 8, "%s: 8 pins", pin.design);
    const size_t th = c + gensy::U32At(chunk, c + 0x34);
    const bool soft = std::string(pin.design) == "vle_sk_h";
    CKF(gensy::U32At(chunk, th + 0x1C) == 8 && gensy::U32At(chunk, th + 0x24) == (soft ? 0u : 2u), "%s: 8 strands, %u rings",
        pin.design, soft ? 0u : 2u);
    try {
      gc::Walk(chunk, 0);
      CK(true);
    } catch (const std::exception& e) {
      CKF(false, "%s: Walk of the built chunk: %s", pin.design, e.what());
    }
    // the donors' PROPERTY: the soft hem doubles the fall (+0x34: 1.5 -> 3.0) and sets +0x5C to 0.05; the other keeps both
    const size_t prop = c + gensy::U32At(chunk, c + 0x20);
    const float fall = std::bit_cast<float>(gensy::U32At(chunk, prop + 0x34)), bendv = std::bit_cast<float>(gensy::U32At(chunk, prop + 0x5C));
    CKF(fall == (soft ? 3.0f : 1.5f) && bendv == (soft ? 0.05f : 0.75f), "%s: PROPERTY %g %g", pin.design, double(fall), double(bendv));
    if (first.empty()) first = chunk;
  }
  CK(!first.empty());
  {  // a bend block that is not the 0x40-byte, mode 2 block is refused with the recipe's assert
    gensy::Skirt sk;
    gensy::MakeSkirt(sk);
    std::vector<uint8_t> bad = bend;
    bad[0x100 + 0x50 + 0x100 + 0x60 + 4 + 3] = 0x41;
    gc::Spec spec{"vks_on_o", 1, 1, sk.part, {1}};
    std::string what;
    try {
      gc::Build(spec, sk.geos, sk.bind4, donor, bad);
    } catch (const std::exception& e) {
      what = gensy::What(e);
    }
    CKF(what.find("unexpected bend block") != std::string::npos, "bend block: %s", what.c_str());
  }
  {  // a bend pool that passes build's own asserts (size 0x40, mode 2) but not the loader's walk (its first word must be 1):
     // build ends with walk, so the chunk is refused as walk's 'bend block' (and not as build's 'unexpected bend block')
    gensy::Skirt sk;
    gensy::MakeSkirt(sk);
    std::vector<uint8_t> bad = bend;
    bad[0x100 + 0x50 + 0x100 + 0x60 + 3] = 2;
    gc::Spec spec{"vks_on_o", 1, 1, sk.part, {1}};
    std::string what;
    try {
      gc::Build(spec, sk.geos, sk.bind4, donor, bad);
    } catch (const std::exception& e) {
      what = gensy::What(e);
    }
    CKF(what == "AssertionError: bend block", "walk inside build: '%s'", what.c_str());
  }
  {  // a donor that is not the named skirt
    gensy::Skirt sk;
    gensy::MakeSkirt(sk);
    std::vector<uint8_t> bad = donor;
    bad[0x538C0 + 0x50 + 0x70] = 'x';
    gc::Spec spec{"vks_on_o", 1, 1, sk.part, {1}};
    std::string what;
    try {
      gc::Build(spec, sk.geos, sk.bind4, bad, bend);
    } catch (const std::exception& e) {
      what = gensy::What(e);
    }
    CKF(what.find("donor is not") != std::string::npos, "donor: %s", what.c_str());
  }
  {  // a decl that is not writable
    gensy::Skirt sk;
    gensy::MakeSkirt(sk);
    sk.geos[1].decls[1].head[8] = 0;
    gc::Spec spec{"vks_on_o", 1, 1, sk.part, {1}};
    std::string what;
    try {
      gc::Build(spec, sk.geos, sk.bind4, donor, bend);
    } catch (const std::exception& e) {
      what = gensy::What(e);
    }
    CKF(what.find("written decl is not writable") != std::string::npos, "writable: %s", what.c_str());
  }
}

static void TestWalkRejectsUnterminatedWrites() {
  Group("walk_rejects_unterminated_writes");
  namespace gc = doa4::suitconv::gencloth;
  const num::FpEnvGuard fp;
  const std::vector<uint8_t> donor = gensy::Donor(), bend = gensy::Bend();
  gensy::Skirt sk;
  gensy::MakeSkirt(sk);
  gc::Spec spec{"vks_on_o", 1, 1, sk.part, {1}};
  std::vector<uint8_t> chunk;
  try {
    chunk = gc::Build(spec, sk.geos, sk.bind4, donor, bend);
  } catch (const std::exception& e) {
    CKF(false, "the synthetic chunk builds: %s", e.what());
    return;
  }
  // positive control: the chunk as built walks
  try {
    gc::Walk(chunk, 0);
    CK(true);
  } catch (const std::exception& e) {
    CKF(false, "the built chunk walks: %s", e.what());
  }
  // find the GRID_WRITE list's 0xFFFFFFFF terminator (the list starts 0x60 into the GRID_WRITE section)
  const size_t c = 0x50;
  const size_t gw = c + gensy::U32At(chunk, c + 0x30);
  size_t q = gw + gensy::U32At(chunk, gw + 0x1C);
  while (gensy::U32At(chunk, q) != 0xFFFFFFFFu) q += 4;
  CK(q > gw + 0x60);
  // the same chunk with the terminator, and every later 0xFFFFFFFF word (the threads' -1 links), zeroed: the list has no end
  std::vector<uint8_t> bad = chunk;
  for (size_t at = q; at + 4 <= bad.size(); at += 4)
    if (gensy::U32At(chunk, at) == 0xFFFFFFFFu)
      for (int i = 0; i < 4; ++i) bad[at + size_t(i)] = 0;
  std::string what;
  try {
    gc::Walk(bad, 0);
  } catch (const std::exception& e) {
    what = e.what();
  }
  CKF(!what.empty(), "an unterminated write list is refused (%s)", what.c_str());
  // the same with the terminator at the very end of the chunk cut off: refused as well
  std::vector<uint8_t> cut(chunk.begin(), chunk.begin() + std::ptrdiff_t(q));
  std::string what2;
  try {
    gc::Walk(cut, 0);
  } catch (const std::exception& e) {
    what2 = e.what();
  }
  CKF(!what2.empty(), "a chunk cut before the terminator is refused (%s)", what2.c_str());
  // a write whose decl index is 46 or more is refused by rule 10
  std::vector<uint8_t> bad10 = chunk;
  const size_t first_write = gw + gensy::U32At(chunk, gw + 0x1C) + 4;  // the entry after the first node marker
  bad10[first_write] = 46;
  std::string what3;
  try {
    gc::Walk(bad10, 0);
  } catch (const std::exception& e) {
    what3 = e.what();
  }
  CKF(what3.find("rule 10") != std::string::npos, "rule 10: %s", what3.c_str());
}

static void TestGenPrepSynthetic() {
  Group("gen_prep_synthetic");
  namespace core = doa4::suitconv::core;
  namespace gc = doa4::suitconv::gencloth;
  core::Model suit, other;
  core::PartArena arena;
  core::PyDict<int64_t, int64_t> chan2id;
  chan2id.Set(0x9002, 1);
  auto make = [&](const core::Model* m, int64_t obj, int64_t decl) {
    core::Part* p = arena.New();
    p->model = m;
    p->obj_id = obj;
    p->decl_index = decl;
    p->bust = std::make_pair(std::vector<core::Vec3>{}, std::vector<core::Vec3>{});
    return p;
  };
  auto fails_with = [&](const char* design, std::vector<std::optional<core::Object>>& objs, const char* want) {
    std::string what;
    try {
      gc::Prep(design, suit, objs, chan2id);
    } catch (const std::exception& e) {
      what = e.what();
    }
    CKF(what.find(want) != std::string::npos, "%s: '%s' (got '%s')", design, want, what.c_str());
  };
  {  // the normal case: the skirt part is the second part of the hips object; a part of another model with the same ids is ignored
    core::Part* skirt = make(&suit, 113, 0);
    core::Part* decoy = make(&other, 113, 0);
    core::Part* neighbour = make(&suit, 113, 1);
    std::vector<std::optional<core::Object>> objs(3);
    objs[0] = core::Object{{decoy}, {}};
    objs[1] = core::Object{{neighbour, skirt}, {}};
    const gc::Spec spec = gc::Prep("vks_on_o", suit, objs, chan2id);
    CK(spec.design == "vks_on_o" && spec.oid == 1 && spec.k == 1 && spec.part == skirt);
    CK(spec.meshes == (std::vector<int64_t>{1, 2, 3}));
    CK(skirt->writable && !skirt->bust.has_value());
    CK(!neighbour->writable && neighbour->bust.has_value() && !decoy->writable);
  }
  {  // vle_sk_h: object 76, mesh 1
    core::Part* skirt = make(&suit, 76, 0);
    std::vector<std::optional<core::Object>> objs(2);
    objs[1] = core::Object{{skirt}, {}};
    const gc::Spec spec = gc::Prep("vle_sk_h", suit, objs, chan2id);
    CK(spec.oid == 1 && spec.k == 0 && spec.meshes == (std::vector<int64_t>{1}));
  }
  {  // found twice (in two objects)
    std::vector<std::optional<core::Object>> objs(3);
    objs[0] = core::Object{{make(&suit, 113, 0)}, {}};
    objs[1] = core::Object{{make(&suit, 113, 0)}, {}};
    fails_with("vks_on_o", objs, "skirt decl found twice");
  }
  {  // not in the converted model
    std::vector<std::optional<core::Object>> objs(2);
    objs[1] = core::Object{{make(&suit, 5, 0)}, {}};
    fails_with("vks_on_o", objs, "skirt decl not in the converted model");
  }
  {  // on an object that is not the hips
    std::vector<std::optional<core::Object>> objs(3);
    objs[2] = core::Object{{make(&suit, 113, 0)}, {}};
    fails_with("vks_on_o", objs, "skirt must be on the rigid hips object");
  }
  {  // on the hips object, but skinned (a palette)
    std::vector<std::optional<core::Object>> objs(2);
    objs[1] = core::Object{{make(&suit, 113, 0)}, {7}};
    fails_with("vks_on_o", objs, "skirt must be on the rigid hips object");
  }
  {  // a design that is not generated: the recipe's KeyError
    std::vector<std::optional<core::Object>> objs(2);
    objs[1] = core::Object{{make(&suit, 113, 0)}, {}};
    fails_with("vay_cs_b", objs, "KeyError");
  }
}

// gen_numeric: the generated cloth's census-chosen numeric sites against pins from the recipe's interpreter (numpy 1.26.3) on
// seeded synthetic inputs (no game data). Each table holds cases where the plain-arithmetic variant of the site differs from
// numpy's result (the flag columns: for cosine, "1 x" the plain dot product changes the result and "x 1" the plain norms do;
// for vertical, "1 x" sequential sums change it and "x 1" per-row 1-D norms instead of the axis reduce do), so swapping a
// site for its runner-up is red. The sites are exposed by gencloth.h: Cosine (lines 87 and 106), Distance (65, 223, 457),
// VerticalRatio (262) and LoopKey (207).
static void TestGenNumeric() {
  Group("gen_numeric");
  namespace gc = doa4::suitconv::gencloth;
  const num::FpEnvGuard fp;
  using V = core::Vec3;
  struct Cs {
    V v, w;
    double want;
  };
  struct Dd {
    V p, q;
    double want;
  };
  struct Vr {
    std::vector<V> steps;
    double want;
  };
  struct Mn {
    std::vector<double> ys;
    double want;
  };
  const auto plain_dot = [](const V& a, const V& b) { return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2]; };
  const Cs cosine[] = {
      {{0x1.089d4f525d6e8p-3, -0x1.df19f8c4e1b80p-7, -0x1.668486a25748ep+0}, {-0x1.80571cc193558p-4, 0x1.8113bb1244e50p-1, -0x1.82353fef7465cp-4}, 0x1.9f556e9b54212p-4},  // 0 1
      {{0x1.cef558cf82f60p-5, 0x1.cd85bedcad6d0p-5, -0x1.76db10f79b266p+0}, {-0x1.06b4863b28660p-8, -0x1.f423525e9aa0ap-2, -0x1.76923b48be2c8p-3}, 0x1.416a9d8a22155p-2},  // 1 1
      {{0x1.a1a275927541cp-2, 0x1.bf2471da2211ep-4, -0x1.a91331a06a300p-3}, {-0x1.f7b73901e5cccp-4, -0x1.012924e77e1c2p+1, 0x1.3ce5f71d8ba18p+1}, -0x1.0b2d8ce246b8fp-1},  // 0 1
      {{0x1.fcbba714a1a06p-1, 0x1.3bf39eb611a18p+1, 0x1.7eaca62119ab0p-3}, {-0x1.1606a046efee6p-4, -0x1.98b7562ce63dcp-4, 0x1.720cb09b2b320p-1}, -0x1.767c577ae8653p-4},  // 0 1
      {{-0x1.a96b96e7959e0p-2, -0x1.9e0b02151272cp-4, -0x1.bfdd33d9e595ap-1}, {0x1.65821c1570b3cp+0, 0x1.9883198c4c958p-3, -0x1.0dbc34871f0fep+0}, 0x1.7fdc55567e19bp-3},  // 0 1
      {{-0x1.2f6b426d9f672p-2, 0x1.251962570fc50p-2, 0x1.f885a51445444p-2}, {0x1.a06789aff83a0p-6, 0x1.1dfd7301ef82ep+0, 0x1.da285805fe038p-1}, 0x1.a624bf781f897p-1},  // 0 1
      {{-0x1.d5c288deac374p-2, 0x1.25fc5708bec50p+0, -0x1.c5d72fe970f48p-6}, {-0x1.7ad32d620c138p-3, -0x1.f5086de84b5dcp-3, -0x1.2b095e2d9bbbcp-1}, -0x1.c38c3d02944d7p-3},  // 1 0
      {{-0x1.dc75c15108d80p-4, 0x1.cc1ffdc734600p-6, 0x1.28993557f0d18p-1}, {0x1.2b8d5882622e8p-5, 0x1.1aea928d4310ap+1, -0x1.1f7aa9d0f8562p-1}, -0x1.95fb5cce9724cp-3},  // 1 1
      {{-0x1.0aab253c4daeap+1, 0x1.3d16145e1a516p+0, -0x1.2c2a4a4091220p-4}, {-0x1.d1aacdf9a928ap-4, 0x1.8c69f4ff1a9c0p-2, 0x1.d2037e9938634p-2}, 0x1.da3ca35fe8a3bp-2},  // 0 1
      {{0x1.b7dbeda49f100p-4, -0x1.18285512a44b0p-4, -0x1.80ed9e6349990p-4}, {0x1.f0d173a52afd0p-4, -0x1.e4bf45807ee18p+0, -0x1.04d22c529eb56p+1}, 0x1.8428ad0b271a6p-1},  // 1 0
      {{-0x1.4bd1bf8b5fa68p-2, 0x1.d8d8436debb1ap-2, -0x1.3dd4566e56c60p-7}, {-0x1.91ecc841f4720p-1, -0x1.8e2b67de513e8p-1, 0x1.a45340e45c658p-2}, -0x1.4ebfe71bc7cd5p-3},  // 1 0
      {{-0x1.3a1d29f761a0ep-2, -0x1.52324af0e5930p-5, -0x1.d2ccbbb958a38p-4}, {0x1.4fed762cd21dap-1, 0x1.5f5cc3f6bc0b0p-5, 0x1.447f02d955d18p-5}, -0x1.e91c2bfce9a19p-1},  // 1 0
      {{-0x1.5ad773c7fc486p-4, 0x1.adef6ff24136ep+1, 0x1.3903b9390ba48p-3}, {-0x1.cf36aa2ec1014p+0, 0x1.d840d0b532370p-4, 0x1.2831136d49990p+0}, 0x1.96276920d10d1p-4},  // 0 1
      {{0x1.3b57f8636a030p-3, 0x1.ef63706b94116p-4, 0x1.c8629ab2ccdc4p-4}, {0x1.6673cbe800350p-6, -0x1.4c3c72865e488p-1, -0x1.6711b6e19b560p+0}, -0x1.5440ef32c5c2dp-1},  // 1 0
      {{0x1.3adf45a027bd4p-2, 0x1.ac617e7cd3a78p+0, 0x1.6145f7e270f00p-7}, {-0x1.f4d93ec992934p-1, -0x1.99dfd7983debcp+0, 0x1.647bae1cf9be0p-4}, -0x1.dd48528c5ac84p-1},  // 0 1
      {{-0x1.4cb67febbcdfcp-5, 0x1.b2a74cf52ccf8p+1, -0x1.4aba1bcf64cf6p+0}, {-0x1.34f1443cdd338p-1, -0x1.47c0e70fedcfep-4, -0x1.689fdbdf35a10p-2}, 0x1.4d0d9c07cc940p-4},  // 1 0
      {{-0x1.b24248a709a1cp+0, -0x1.1aa87a61cfd8ep-3, -0x1.ee5f388517a60p-6}, {0x1.7f28043920d2cp-1, 0x1.08bcb6815f6c4p-2, 0x1.7ee4355cc0492p-1}, -0x1.6eaf4ad1aac10p-1},  // 0 1
      {{0x1.2be71f5a25d78p-6, 0x1.445ae65a4dfe4p-3, 0x1.f89547dccb68cp-5}, {0x1.9ad247b246124p+0, -0x1.c264c46f02244p-3, 0x1.53ad3e1a8b7d0p-2}, 0x1.b228b0f61eb6ap-5},  // 1 0
      {{0x1.6e62d80abd5f4p-1, -0x1.9897a0e0f4090p-3, 0x1.12f814c31e44ap-4}, {0x1.1f7958da16f40p-4, -0x1.578587e649540p+1, -0x1.a0edf49038638p-1}, 0x1.03d4df2098e98p-2},  // 1 0
      {{0x1.dccda3aadedf0p-4, -0x1.e7136be830db0p-4, 0x1.bbb1fb3d59c24p+0}, {-0x1.f01fe5d168ce4p-3, -0x1.5efa452197320p-2, -0x1.1592fdd4bfc40p-5}, -0x1.01ec8fd5e0374p-4},  // 0 1
      {{-0x1.abf94f3feea2ep-2, -0x1.2631d27ba48a8p-4, 0x1.3a1ec41f4fcb4p-4}, {-0x1.62ce16d219db4p-2, 0x1.66d8db01c055cp-5, -0x1.89f3fbc783b30p-2}, 0x1.00768083dd0fap-1},  // 1 0
      {{-0x1.2a384b69f70bep-1, -0x1.dd1242cddbe88p+0, 0x1.eff12dc6ac420p-4}, {-0x1.23af46a157380p-4, -0x1.15ce1d7695a02p+1, -0x1.32d842ef8a118p-4}, 0x1.eb19dbedefa32p-1},  // 1 1
  };
  const Dd dist[] = {
      {{-0x1.be6bcabf5e2f8p-1, -0x1.173c548ff7b92p+0, -0x1.0bcff1baf7cb8p+0}, {-0x1.93720f1ece090p+1, -0x1.7df0ad655b56ep+2, 0x1.1508f07572e6ap-3}, 0x1.60c1134234f29p+2},
      {{-0x1.535d40322f180p-4, 0x1.6f71461f0990ep-1, 0x1.7f39c296584c6p+0}, {0x1.524a5e8fedea4p-2, 0x1.96e39b2b98900p-3, 0x1.370baf83ac900p-2}, 0x1.5d7ffc8826e33p+0},
      {{0x1.e5d406fcd9200p-7, -0x1.43fd0a4f954b8p+0, 0x1.ff0e136c2490ap-1}, {-0x1.9c8336b340e00p-3, -0x1.3feb223664ae0p-4, 0x1.5a4829bfb18dcp+0}, 0x1.420b928838f46p+0},
      {{0x1.99310b83224c0p-5, -0x1.cf1ed466f7deap-2, 0x1.1ee6dfcbf3518p+0}, {-0x1.589e1b24a57c0p-5, 0x1.288691310bc70p-2, 0x1.6641265212f4cp+0}, 0x1.9879058e1567bp-1},
      {{-0x1.7b495ce7a55ccp+2, 0x1.dadf969e21aa2p-3, -0x1.9eaa610227304p+2}, {-0x1.18ed5e54c77fcp-3, 0x1.2b609c61ec440p-6, 0x1.f51a29907b3a4p-1}, 0x1.2e3147561f0b0p+3},
      {{0x1.34452c88d24e0p-4, -0x1.2c6c4bb5563a0p-2, 0x1.a05ea8bb40ff0p+1}, {-0x1.dc13dac9f7a68p-3, 0x1.a923c384c90e4p-4, 0x1.3746a2a5b65d4p+1}, 0x1.ecd70c1284c75p-1},
      {{-0x1.15d7851c4bef6p-1, -0x1.7077d8593f136p+0, -0x1.048efdd51f99ap+1}, {0x1.86551c00484e0p-2, -0x1.1dd5aa61ad3a8p+1, -0x1.adc8410670460p-7}, 0x1.2e333c95d6786p+1},
      {{0x1.a4aa1939854c4p-4, 0x1.fc8bb0352af98p+1, 0x1.5aeee381a442ap-2}, {0x1.2248054bd29a0p-3, 0x1.53eb0c01e0f00p-5, -0x1.47e7cc9be5ebap+1}, 0x1.38b1f09ba1e30p+2},
      {{-0x1.421ff5b02723cp-4, -0x1.e11e48615c23cp+1, -0x1.7c7402dfbe600p+0}, {0x1.cac4f1f4bfae2p+1, 0x1.e4b89c877ddb8p-5, 0x1.2911a4bc8a1eap+2}, 0x1.03113ef637c64p+3},
      {{0x1.a4e1c20b039d4p-2, -0x1.4d6bc24d0a954p-2, 0x1.504dbf25f3c78p+0}, {-0x1.3527cd55e5bc0p-7, 0x1.a6dc8fe3303aep-1, -0x1.0a4cf5476f848p-4}, 0x1.d849c9dfaf051p+0},
      {{-0x1.68c5ab191c54cp+1, -0x1.f71f929bc53ecp+0, -0x1.be4b2c6e39838p-2}, {0x1.62480b889196cp-3, -0x1.75954620edb90p-3, -0x1.881b27e6aa1f0p-3}, 0x1.bedbf72e3b66fp+1},
      {{-0x1.66f15444bd006p-2, -0x1.6dbf0dc545740p-2, -0x1.7c7e5a2611ff4p-3}, {-0x1.7d197c31fbc08p-3, -0x1.06f335870bd44p-1, 0x1.fbdea6a5d590cp-3}, 0x1.f54d11597d1d0p-2},
  };
  const Vr vert[] = {
      {{{-0x1.408aae915f13ep+0, -0x1.92c016c0f0ec2p-1, -0x1.fc696dcaa77e8p-1}, {0x1.37cbbc9234b4cp-2, 0x1.f319cd6346836p-4, 0x1.aecccbfd6c566p-2}, {-0x1.f2eda41be9322p-4, 0x1.b2018a9607db6p-3, -0x1.e14224bf4e572p-4}, {-0x1.f45c294e9f486p-4, 0x1.a69f28cf31af4p-3, 0x1.d5b4169b19fdap-1}, {0x1.7df8ea700ce80p-4, -0x1.9bc1a2b87411cp-5, -0x1.8da5d443abd58p-3}, {0x1.3eeb307e64086p+0, -0x1.66a9018c7ced4p-5, -0x1.88503907b6c58p-4}, {0x1.27693e24d53fcp-5, -0x1.059ea3f569394p-2, 0x1.1a1eaa6f9ce8ep-1}, {0x1.4f4ab919bb180p-7, 0x1.2e8710becbe48p-3, 0x1.0e64365c8a350p-8}, {-0x1.621c3427ea7b0p-4, 0x1.4ead9aacfc620p-2, -0x1.3cdbc6dda23dcp-4}, {0x1.387ec41da0278p-1, -0x1.f030efacb1620p-5, 0x1.927cb07223b3ap-1}, {-0x1.ed6e6cc92e726p-2, -0x1.42c8edc0eca46p-2, -0x1.beaee575d2900p-6}, {-0x1.6c0a024dfca10p-2, -0x1.1ab9417e79e9ap-2, 0x1.1cfecbebe7ab8p-4}}, 0x1.60ac414a37f4ep-2},  // 1 0
      {{{-0x1.700b90c614588p-5, 0x1.04d570c252a72p-5, 0x1.fa03cae54278cp-3}, {-0x1.77c540899ce10p-8, 0x1.cf88e4b5a27fep+0, -0x1.e52ffe9f056b6p-2}, {0x1.5dfdb7bfeb600p-9, -0x1.de1bcc622486cp-5, 0x1.1a778c791ffdcp-4}, {0x1.70ffd51dd8b20p-7, -0x1.ccd24e96ac030p-3, 0x1.4ed1fd82f5fd6p+0}, {0x1.7843a26c4e5c8p-2, 0x1.62a3bb96827d0p+0, 0x1.2671e310f39dcp-4}, {-0x1.f3aaad200cc80p-5, -0x1.4a71fc8ebf9c0p-7, 0x1.58d2fb3e34a24p-2}, {-0x1.da19d9e542722p-5, 0x1.936afd5d1c7bep-1, 0x1.3c870e5669ad0p-3}, {-0x1.7ae9d21c040a4p-5, -0x1.4f9070527172cp-6, 0x1.93f396a7a6c7ep-2}, {-0x1.c5898ac42f254p-6, 0x1.775467d3fa912p-3, -0x1.cbb93fa4ca280p-4}, {0x1.b34be12ed0160p-6, 0x1.f467aee8c76acp-5, 0x1.6f60f5a4dce1ap+0}, {-0x1.5b015a983a258p+0, 0x1.d78c640016238p-6, 0x1.34c05edb29470p-1}, {-0x1.9d2c54aad0864p-2, -0x1.bb7139b3c4ef6p-2, -0x1.bfe1c8e98feeep-4}, {0x1.086056a5168a2p-3, 0x1.1104643c7043cp-4, 0x1.1768fa62a49a0p-3}, {-0x1.8763251e141e8p-7, 0x1.95a4ded69e558p-3, 0x1.f16a91efc15e4p-3}, {0x1.3f85dad4f0cacp-4, 0x1.6d77b9bad3290p-7, 0x1.e2fba0a08d0c0p-2}, {-0x1.10aa341b5daa0p-2, 0x1.c8f3d199863d8p-5, 0x1.f5bc91853a614p-4}, {0x1.65fd7763a1bb6p+0, -0x1.e243fdd50d9bap-2, -0x1.87c287ffbbd70p-7}}, 0x1.caf42a78c123cp-2},  // 1 0
      {{{-0x1.6175021b51896p+0, -0x1.910cbe3cb14e4p-4, 0x1.9a406dfcc5016p+0}, {0x1.b09fc36315d08p-4, -0x1.6ae2ae3fbaae2p+0, 0x1.430a0e87c91c8p-2}, {-0x1.5554895fc2ff0p-5, -0x1.7441d996a9cdap-3, 0x1.559585660aebap+0}, {0x1.4357edb656bfap+0, 0x1.982d02fddb692p-2, -0x1.240d69e885820p-4}, {-0x1.b4f087e2d4d40p-7, 0x1.b63222ef02f80p+0, 0x1.876bf2fb7941cp-6}, {0x1.272f275c5d100p-9, 0x1.377ba6501be80p-7, -0x1.0b08f24358044p-3}, {-0x1.f3d61f58f690ep-2, 0x1.1942cc9991498p-4, 0x1.d3ac25ef4fdbcp-1}, {-0x1.6b638c562d5d4p-6, 0x1.39db750d58cacp-4, 0x1.93e1a0692011cp-2}, {0x1.f6d57072658ecp-2, 0x1.642632081cd20p-1, -0x1.ed6d01b699f80p-1}}, 0x1.b8f9b40133442p-2},  // 1 0
      {{{0x1.914c606c4432ap-3, -0x1.22e1807009aa8p-1, 0x1.384890b23e9c8p+0}, {0x1.29b30468567cep-2, 0x1.54afe55877534p-2, -0x1.e3a6aca855dd4p-2}, {0x1.53e4d1abc44dcp-4, -0x1.cffa27152610ep+0, 0x1.cc5281d6c2d20p-1}, {0x1.beee499dc76fep-3, -0x1.913050addfcacp-2, 0x1.975e4df96188ep-3}, {0x1.4fb375c382280p-6, -0x1.158b8b8d1dab0p+0, -0x1.6d8d706b90fa0p-6}, {-0x1.12e37b64572c4p-3, 0x1.9ca363df6fa18p-7, -0x1.5db93072af5a0p-8}, {-0x1.1ae81b79443d8p+0, 0x1.db0b75eadafb0p-4, -0x1.03220779536d2p-2}, {-0x1.e0681de130340p-6, -0x1.2cfa029848a40p-8, 0x1.69acbf485422cp-4}, {-0x1.357445a54623cp-5, 0x1.59e34ffb03dacp-2, 0x1.0b59395f36848p-5}, {-0x1.c5e970ce3a1e0p-3, -0x1.d73cb04a4cd58p-2, 0x1.69ab73cb18848p-4}, {0x1.a47a4874c0f54p+0, -0x1.e4e1d0cf2a7c0p-8, 0x1.fa19cae6ee35cp-1}, {0x1.419b877febcecp-5, 0x1.bbffdf374cc70p+0, -0x1.f91dc5f3f1b06p-4}, {0x1.e7210c4815014p-4, 0x1.8767d3961896ep-2, -0x1.c766221e6c3a8p-4}, {-0x1.89af340baba38p-6, 0x1.5a9b237a45c88p-3, 0x1.5ea5a5698db76p+0}, {-0x1.d77316b86f8bcp+0, 0x1.33d5979c92908p-4, 0x1.4d820f7fda8dap-3}, {-0x1.8670198abcb18p-5, 0x1.2c4c26296b354p-4, -0x1.0d48970fabf98p-3}}, 0x1.fa4c7f3fe11e1p-2},  // 1 0
      {{{0x1.3cf742784c128p-3, -0x1.40b054e057620p-3, 0x1.cdb9d479eb800p-10}, {0x1.e3429a8b5f9e0p-8, -0x1.bae4b03d06d92p-4, 0x1.4d3824997fca6p-5}, {-0x1.2eaf5b75219f2p-1, -0x1.12f51ac4f034cp+0, 0x1.8a4553bd2e620p-1}, {-0x1.1000d50ec350cp-2, -0x1.b8d8b72a3b892p-4, 0x1.8bef5413b338ap-2}, {0x1.1985a0ba486aap-2, -0x1.30a1809650ae2p-4, -0x1.e06ec9bf78780p-9}, {0x1.25fa79c0e3620p-8, -0x1.ad6c0685ec63cp-1, -0x1.cb9279964ac20p-6}, {-0x1.4ca66b67670e0p-6, -0x1.c1da0607ed430p-7, -0x1.72f4f7779c9d4p-5}, {-0x1.30370b235261cp-2, 0x1.315f56a5336c0p-5, 0x1.c978b58d112fep-4}, {-0x1.fad40c2b19a20p-4, 0x1.74828da5af480p-2, 0x1.ec10c212d4f18p-7}, {-0x1.a7171ef7bd438p-2, 0x1.7c4deb227ca60p-4, 0x1.ac6c4e5e9dd40p-5}, {0x1.2b802d2970480p-5, -0x1.c0e55a41e0ee0p-3, -0x1.f26b68bc23d64p-4}, {0x1.55468d99f48c4p-6, -0x1.2f21bf160aa94p-4, 0x1.526d36ae4c560p-5}}, 0x1.496a8656ec3b5p-1},  // 1 1
      {{{0x1.87c3c05ad8458p-4, -0x1.bf5a53b3af300p-3, 0x1.d6045e8f28574p-1}, {0x1.1ff964cade830p-7, 0x1.99576a6fe99cap-5, 0x1.f373f3dd77710p-1}, {0x1.06ea6781a7810p-4, -0x1.5e16b49e89050p-4, -0x1.47c3f4b70c290p-7}, {0x1.a63a999867094p-2, -0x1.44aa2a79859d0p-4, -0x1.6efa5f2c4229cp-4}, {-0x1.25761231fca40p-6, -0x1.033b5b6f8e7ecp-5, -0x1.843d201f239d2p-3}, {-0x1.708816b09d3e4p-5, 0x1.b68e416c8a2fep-2, 0x1.9f637457af0e4p-1}, {-0x1.217d08255be30p-5, 0x1.77385df22ad02p-3, 0x1.3afe3cd98a8fap-5}, {-0x1.aca1058a8eb40p-2, -0x1.86d91c3f9cd24p-5, -0x1.2e7a71a73eeacp-6}, {-0x1.3089a87672b20p-3, -0x1.22beb6b0eee78p-6, 0x1.3211ba5fdf3a8p-1}, {-0x1.e490ef58fb198p-2, 0x1.4848fd145879cp-4, -0x1.6f755f73e5e28p-5}, {0x1.97ddd6b9a414cp+0, 0x1.c763d302e31d4p-5, -0x1.4eed367dbc2dcp-5}, {0x1.d542aad19f9dcp-3, -0x1.60f88746ba600p-6, 0x1.ee70bbc8b9d18p-7}, {-0x1.e2c45601719a0p-3, -0x1.c9aadddd844c6p-3, -0x1.4518f3d8f6b7ep-4}}, 0x1.a2e1498761b0bp-3},  // 1 0
      {{{-0x1.bcc4f83082c9cp-1, -0x1.16d0cd228a380p-5, -0x1.08c42824880c0p-9}, {-0x1.710d705974c08p-2, -0x1.4dcc6acff87c2p-1, 0x1.b74a80a22a05cp-4}, {-0x1.d34a4242438b4p-5, 0x1.680cd627fe17ep+0, 0x1.b43854875e8d0p-5}, {-0x1.4454c0c348c64p-3, 0x1.859121070410cp-1, -0x1.6f6533f7f64b8p-4}, {-0x1.30fbf4987e818p-5, -0x1.f159e33c48d38p-5, -0x1.8c7336f2d3268p-6}, {-0x1.c78fafbdbad96p-5, -0x1.3c28dd67f4df4p-6, -0x1.f1c3bb50daf28p+0}, {-0x1.51046f418ac0cp-2, 0x1.8146947e1d578p-1, -0x1.142672d6f834ap-3}, {0x1.9388340c2e918p-1, -0x1.70a8e298dc374p-2, 0x1.8b5c917d973c0p-8}, {-0x1.6c29fc4326088p-2, 0x1.32fcc8d43690ep-3, 0x1.67d592d0c3e98p-3}}, 0x1.0e006a99fef13p-1},  // 0 1
      {{{0x1.afaa2373dba8cp-2, 0x1.5d3a8720af54ep-1, 0x1.eea28fbaaf140p-8}, {-0x1.84ffabd84a516p-3, -0x1.18c0e87de661ap+0, -0x1.36ea0a91bb980p-7}, {0x1.2b9fdfa50877ep-5, 0x1.a39c52bb73a5cp-5, 0x1.b49236b0f83fap-3}, {0x1.38e2274c4c35ap+0, -0x1.a3604c9fdf9e8p-7, 0x1.2839cb0284b64p-6}, {0x1.3e4fef7a4050ep-3, -0x1.37c2608b83852p-3, 0x1.f3cc5208ef168p+0}, {0x1.3d4b4fd9cb8eep-5, -0x1.a650d55519e40p-6, -0x1.11c535db639aep-3}, {-0x1.a92dcd1265dc0p-1, 0x1.b722b51f81578p-6, -0x1.e81d75b85614ep-1}, {0x1.cccced6334ba0p-3, -0x1.21865ec40ee28p-7, 0x1.843211e09167cp-6}, {0x1.803152389720cp-3, -0x1.000599a6cebeap-3, 0x1.aada23a90d4f4p+0}, {0x1.c220ef694a7eep-3, -0x1.aba07d10c45a0p-7, -0x1.15fd272252fc2p-3}, {0x1.2b3ca4d95d908p-7, 0x1.915c5a2d8c0b8p-2, 0x1.9fa527dd5b450p-4}}, 0x1.1cb410795093cp-2},  // 1 1
      {{{-0x1.8d8bbf4474392p-4, 0x1.054002cc948f4p-1, 0x1.b3e924b21f654p-1}, {-0x1.a1002d1dfe620p-6, -0x1.804f3d9485140p-9, 0x1.65a21290c5550p-4}, {0x1.647eff39d5a20p-5, 0x1.67dcd418b4eb0p-3, 0x1.dba293c6fd088p-5}, {-0x1.63b941b40087ep-2, 0x1.96988ac4b2958p-5, -0x1.a1f595744f2c8p-2}, {0x1.4d3ad2bf4490ep-1, 0x1.b3c171e185a20p-1, -0x1.2329762a6c6e4p-5}, {0x1.8eb3d5aaf2cb4p-3, 0x1.5501476b510c8p-3, -0x1.8a22f04fe5818p-5}, {0x1.3c32a4eadcfccp-5, -0x1.6b5feacb03c20p+0, 0x1.5c16f9d47ca64p-5}, {0x1.5dcd04f0eecc2p-1, 0x1.b2792560e8720p-4, 0x1.43dc2d4f9cab2p-4}, {0x1.a6cbef08d37bcp-4, -0x1.31578c37fab88p+0, 0x1.004198821ddd0p-6}, {-0x1.245f3faa86884p-1, 0x1.923b43a65ea64p-5, 0x1.6cb8b576028c8p-3}, {0x1.c19b7e4b54f30p-6, -0x1.1f1f0c826476ep-3, -0x1.358a3fb3d7a50p-2}, {0x1.d86418b4b5410p-5, -0x1.6a267e49ef5b0p-1, -0x1.efdc46a9b9f02p-1}, {0x1.f1e85cb4bd304p-3, -0x1.a0e9fa7f62accp-3, -0x1.30b0b97998220p-5}, {-0x1.6896f30b5b674p-6, 0x1.0531017e74360p-4, 0x1.93e02fb95d800p-12}}, 0x1.414ad7138a128p-1},  // 1 1
      {{{-0x1.62befaf3cbd90p-2, -0x1.0d73acc8e0c28p-4, -0x1.ebd27d81fab84p-3}, {0x1.500ab43b46c92p-2, -0x1.96fcd7af22238p-3, 0x1.5105d90b78ff0p-4}, {0x1.c3a5c2c8f4da2p-1, -0x1.9dbf1eb119e80p-5, 0x1.457a39a1618f4p-2}, {0x1.a1c71982757d8p-5, -0x1.5060703651d70p-4, -0x1.d154daa6351b0p-4}, {0x1.61a346cd55500p-7, -0x1.e5adc2504c9ecp-2, -0x1.1effee4046f28p-4}, {0x1.3dce8cc54ad18p-2, 0x1.f3ef051aaa8b2p+0, -0x1.8091782fa6560p-4}, {-0x1.f24b83e06c254p-6, 0x1.5bd5487f75bdcp-4, 0x1.75f9d962f65bep-1}, {-0x1.cac13baf46e78p-5, 0x1.bbb3ca5ea61a4p-4, 0x1.fc8627e41a028p-1}, {0x1.0dece6c6305f4p-5, -0x1.dfdb8d0f7fa2cp-2, 0x1.1a60ce2d9d51cp+0}, {0x1.ee5147db5b244p-4, -0x1.45bfddaa03494p-5, 0x1.061b030b94994p-3}, {0x1.ace5a49db6f50p-3, -0x1.e3afa4e26b060p-8, -0x1.f0b565558354ep-1}, {-0x1.27377b77a15d0p-7, 0x1.dafa707ac54d8p-2, 0x1.92365f1349982p-3}, {-0x1.1e263e972a500p-4, 0x1.2dd3f292684c4p-1, 0x1.2c245cc87c7c2p-5}, {0x1.438d69e096af4p+0, 0x1.413b37b91250ap-2, 0x1.490172e1a47b8p-3}, {0x1.aefc93febb2a0p-2, 0x1.fe962a092b268p-1, 0x1.a24c93432001ap-4}, {-0x1.f5b63dfbbb540p-4, 0x1.03acb1b964f00p+0, -0x1.3dcad2c36600cp-4}}, 0x1.104812ee650f3p-1},  // 0 1
      {{{0x1.5012aaf482024p-3, 0x1.15cfd2079b83ap-2, 0x1.95abe329e1570p-6}, {0x1.c62620276c0bap-1, -0x1.483cc8f480cb8p-2, 0x1.4cf5b132e17f6p+0}, {-0x1.49dfa0e38683cp-1, -0x1.f4cca2e505930p-4, -0x1.dd22934a2ffe8p-3}, {-0x1.49c8b36662814p-5, 0x1.8e4063b17ded2p-5, 0x1.68d02cbb34136p-3}, {0x1.e9328c4743e48p-4, 0x1.6a32048d048ecp-3, -0x1.d7bcb29d07af0p-7}, {-0x1.a2af53a2ede8ap-3, 0x1.dc96b10791c24p-3, -0x1.6ac4463a949bap-5}, {-0x1.ac714a783a720p-6, -0x1.009daafea69d0p-5, 0x1.e457420fa3996p-2}, {-0x1.c9e73ca99ecf0p-4, 0x1.be47dfae06a90p-8, 0x1.eec0f953480e6p-5}, {0x1.6ccf059ed5610p-7, 0x1.04be9af98fa70p-5, 0x1.84c2a9283de26p-2}, {0x1.145bd86bd0600p-3, 0x1.fba16f7fde754p-4, -0x1.b724172945deep-4}, {0x1.bc3c6ccb3499ep-3, 0x1.201a88906801ap-1, -0x1.96bd14c75cd28p-6}, {0x1.62085e12a751ep-3, -0x1.d6a69e48c82dep+0, -0x1.24b5177d8ae70p-5}, {-0x1.7b61b6788e9b4p-1, -0x1.cfe25f3f7ec96p-4, -0x1.47072d738925cp-5}, {-0x1.5759694af9164p-1, 0x1.e31b1034b2630p-6, -0x1.d2e1d3153afc8p-2}, {-0x1.bfa9c1b71aeb0p-3, 0x1.261869dd447f0p-6, -0x1.8eb4cac0d6438p-7}}, 0x1.cb0e28d365068p-2},  // 0 1
  };
  const Mn means[] = {
      {{0x1.40759673fa1acp+0, 0x1.5a5cbca74ceeap-1, 0x1.110a9081eeeebp-1, 0x1.022e565229e73p-1, 0x1.9ec71a1def536p-1, 0x1.526d586606c32p+0, 0x1.3d2c2273893fdp-1, 0x1.1c27ce055bc09p-1, 0x1.6b0c06685ccbbp+0, 0x1.73279679ac7acp-1, 0x1.588bf21b02f26p+0, 0x1.4fc9dafc5ccfep-1, 0x1.6e87fcf02a3c0p+0}, 0x1.d2aa74517d017p-1},
      {{0x1.542a97112932ep-1, 0x1.0964ab91216eep+0, 0x1.7605da3b05378p-1, 0x1.752c1f130297ep+0, 0x1.7b7b545855bf6p+0, 0x1.7d682d5e758ccp+0, 0x1.4e785a6226bcap-1, 0x1.0de93c805b4e4p+0, 0x1.6c69324414625p-1, 0x1.35b6e193bb928p+0, 0x1.1452f1fac8b49p+0, 0x1.53ba8fe310b83p+0, 0x1.6cd119f7818dap+0}, 0x1.1a098a3fd0752p+0},
      {{0x1.398f620577effp+0, 0x1.177be66facf78p+0, 0x1.107ed424b1d08p+0, 0x1.97050123e2aa9p-1, 0x1.786d4034af68ap+0, 0x1.5f7eea442b618p+0, 0x1.0b99d7beeec59p-1, 0x1.c45c40d58c141p-1, 0x1.48cc8be4f44d0p+0, 0x1.9630a73ae2dfep-1, 0x1.0e16924d1639fp+0, 0x1.61764f7098476p+0, 0x1.15f251e15878dp-1, 0x1.2880f46d92e7dp+0, 0x1.70eaaa002d1eep+0, 0x1.59f91cb0a4c74p-1}, 0x1.0c1c6eae5b329p+0},
      {{0x1.ccbbdd1fb2c18p-1, 0x1.d5eb264bf123cp-1, 0x1.8e3651a9e48afp-1, 0x1.6797a461b4067p+0, 0x1.1484acc40d5bep+0, 0x1.27eee53efef02p+0, 0x1.4775a5b131e22p+0, 0x1.46a9b374ce431p+0, 0x1.edf6f616d2ca2p-1, 0x1.320359da7d994p-1, 0x1.f34cb8a59a09cp-1, 0x1.4f2f9cc3b6e14p+0, 0x1.00400df454d24p-1, 0x1.9d32363dc5c94p-1}, 0x1.fe055b08d2031p-1},
      {{0x1.3fff77230b5f3p+0, 0x1.4d4df36b21573p+0, 0x1.7e4f7893b73e2p+0, 0x1.b0ca7e1d56307p-1, 0x1.14c06e7d06b4ep-1, 0x1.2bc75e1df251cp-1, 0x1.194a6a92cd922p-1, 0x1.51f41aa9ee38bp-1}, 0x1.ce7952c71a5d6p-1},
      {{0x1.ee0476ec9904dp-1, 0x1.700cdbad4c5f3p-1, 0x1.237f54251a1dap+0, 0x1.b389e02e6340cp-1, 0x1.b3a54a3e041f1p-1, 0x1.16f1698a4023ap+0, 0x1.215c3bd3c6ca4p-1, 0x1.72c41d4d42ff6p-1, 0x1.8dbe2f484cf6fp-1}, 0x1.b4e39c88b4729p-1},
      {{0x1.6e20563942fe2p+0, 0x1.d916533529a03p-1, 0x1.0c435b538d85ap-1, 0x1.bd88bfba062e7p-1, 0x1.6f1982b26acd1p+0, 0x1.56c8b839e1b05p-1, 0x1.51ca0d9cd9c36p+0, 0x1.014d962a42e65p-1, 0x1.574b1b748f834p+0, 0x1.3e8c26b9aba1ap-1, 0x1.03e099c770139p-1, 0x1.377f9aaf8e7b5p-1, 0x1.64f265e8b3998p-1, 0x1.93f37e9488af1p-1}, 0x1.bf99c92a362e3p-1},
      {{0x1.0de37ae2cd0d9p-1, 0x1.9cc9e06dac7bfp-1, 0x1.69a2691d207acp+0, 0x1.41ef7aba75f98p+0, 0x1.30e2b3f843509p+0, 0x1.9d750b7b1eda6p-1, 0x1.ea9be2e048ebep-1, 0x1.bcd0f279c06a9p-1, 0x1.162bfba837741p-1, 0x1.1e384ed1c79eap+0, 0x1.59a0ae55e7c8ap+0, 0x1.743711193a697p-1, 0x1.4b7d047cf8f62p+0, 0x1.5f17888acb843p+0, 0x1.a80a6558e88bfp-1, 0x1.03de0067bb574p-1}, 0x1.f239ef2a652c8p-1},
  };
  int ok = 0, dot_differs = 0, norm_differs = 0;
  for (const Cs& r : cosine) {
    const double got = gc::Cosine(r.v, r.w);
    CKF(std::bit_cast<uint64_t>(got) == std::bit_cast<uint64_t>(r.want), "Cosine %a, want %a", got, r.want);
    ok += got == r.want;
    const double nv = gc::Distance(r.v, V{0, 0, 0}), nw = gc::Distance(r.w, V{0, 0, 0});
    dot_differs += plain_dot(r.v, r.w) / (nv * nw + 1e-12) != r.want;
    norm_differs += num::NpDotFused(r.v.data(), r.w.data(), 3) / (std::sqrt(plain_dot(r.v, r.v)) * std::sqrt(plain_dot(r.w, r.w)) + 1e-12) != r.want;
  }
  CKF(dot_differs >= 12 && norm_differs >= 12, "vacuity: plain dot differs on %d, plain norms on %d (12 each at least)", dot_differs,
      norm_differs);
  int dist_ok = 0, dist_differs = 0;
  for (const Dd& r : dist) {
    const double got = gc::Distance(r.p, r.q);
    CKF(std::bit_cast<uint64_t>(got) == std::bit_cast<uint64_t>(r.want), "Distance %a, want %a", got, r.want);
    dist_ok += got == r.want;
    const V d{r.p[0] - r.q[0], r.p[1] - r.q[1], r.p[2] - r.q[2]};
    dist_differs += std::sqrt(plain_dot(d, d)) != r.want;
  }
  CKF(dist_differs == int(std::size(dist)), "vacuity: plain sqrt differs on %d of %zu", dist_differs, std::size(dist));
  int vert_ok = 0, seq_differs = 0, row_differs = 0;
  for (const Vr& r : vert) {
    const double got = gc::VerticalRatio(r.steps);
    CKF(std::bit_cast<uint64_t>(got) == std::bit_cast<uint64_t>(r.want), "VerticalRatio %a, want %a", got, r.want);
    vert_ok += got == r.want;
    double sn = 0.0, sd = 0.0;
    std::vector<double> rows, ay;
    for (const V& s : r.steps) {
      const double sq[3] = {s[0] * s[0], s[1] * s[1], s[2] * s[2]};
      const double n = std::sqrt(num::NpSumLastAxis(std::span<const double>(sq, 3)));
      sn += std::fabs(s[1]);
      sd += n;
      ay.push_back(std::fabs(s[1]));
      rows.push_back(gc::Distance(s, V{0, 0, 0}));
    }
    seq_differs += sn / (sd + 1e-12) != r.want;
    row_differs += num::NpSum1D(std::span<const double>(ay.data(), ay.size())) / (num::NpSum1D(std::span<const double>(rows.data(), rows.size())) + 1e-12) != r.want;
  }
  CKF(seq_differs >= 6 && row_differs >= 6, "vacuity: sequential sums differ on %d, per-row norms on %d (6 each at least)", seq_differs,
      row_differs);
  int mean_ok = 0, mean_differs = 0;
  for (const Mn& r : means) {
    const double got = gc::LoopKey(r.ys);
    CKF(std::bit_cast<uint64_t>(got) == std::bit_cast<uint64_t>(r.want), "LoopKey %a, want %a", got, r.want);
    mean_ok += got == r.want;
    double acc = 0.0;
    for (double y : r.ys) acc += y;
    mean_differs += acc / double(r.ys.size()) != r.want;
  }
  CKF(mean_differs == int(std::size(means)), "vacuity: the sequential mean differs on %d of %zu", mean_differs, std::size(means));
  std::printf("gen_numeric: %d/%zu Cosine, %d/%zu Distance, %d/%zu VerticalRatio, %d/%zu LoopKey equal to the interpreter's\n", ok,
              std::size(cosine), dist_ok, std::size(dist), vert_ok, std::size(vert), mean_ok, std::size(means));
}

// ---------------------------------------------------------------------------------------------------------------
// argsort_is_what_matters: the control for numpy's tie order (Task 2's argsort port)
// ---------------------------------------------------------------------------------------------------------------
// Python's substitution census (np.argsort with kind='stable' over all 34 suits at m1) changed exactly vcr_cm_c and
// vtn_sh_c. Under a stable argsort (ScopedArgsortForTest) the C++ must reproduce that: those two differ from the shipped
// m1 files and the other three authored-cloth suits stay equal. For each of the five the trace is compared too and the
// stage of the first differing line is printed. After the scope the port is back (GetArgsortForTest() == nullptr).
static bool NanLess(double a, double b) { return a < b || (std::isnan(b) && !std::isnan(a)); }
static std::vector<int64_t> StableArgsort(std::span<const double> v) {
  std::vector<int64_t> idx(v.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = int64_t(i);
  std::stable_sort(idx.begin(), idx.end(), [&](int64_t a, int64_t b) { return NanLess(v[size_t(a)], v[size_t(b)]); });
  return idx;
}

static void TestArgsortIsWhatMatters() {
  const char* group = "argsort_is_what_matters";
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, true)) return;
  const char* odir = Env("DOA4_SUITCONV_ORACLE_DIR");
  if (!odir) return SkipGroup(group, "DOA4_SUITCONV_ORACLE_DIR");
  Group(group);
  InputSet* set = OpenSet(env);
  if (!set) return;
  const std::set<std::string> want_differ{"vcr_cm_c", "vtn_sh_c"};
  std::set<std::string> differ, seen;
  {
    num::ScopedArgsortForTest stable(&StableArgsort);
    CK(num::GetArgsortForTest() == &StableArgsort);
    for (const SuitRecipe& s : Suits()) {
      if (!OnlyAllows(s) || !AuthoredCloth(s)) continue;
      const std::string stem = Stem(s);
      seen.insert(s.design);
      const GirlRecipe* girl = FindGirl(s.girl);
      if (!girl) continue;
      SuitInputs in;
      try {
        in = ReadSuitInputs(*set, s, *girl);
      } catch (const std::exception& e) {
        CKF(false, "%s: ReadSuitInputs: %s", stem.c_str(), e.what());
        continue;
      }
      const auto tb = ReadFile(fs::path(env.traces) / "m1" / (stem + ".trace"));
      CKF(tb.has_value(), "%s: no oracle trace", stem.c_str());
      if (!tb) continue;
      CompareTrace ct(SplitLines(*tb));
      ConvertFailure f;
      const auto m1 = ConvertSuit(s, *girl, in, Level::kM1, nullptr, &ct, &f);
      const auto wt = ReadFile(fs::path(odir) / (stem + ".tpr"));
      const auto wc = ReadFile(fs::path(odir) / (stem + ".cat"));
      const bool files_equal = m1 && wt && wc && m1->tpr == *wt && m1->cat == *wc;
      const bool trace_equal = ct.Equal();
      if (!m1) std::printf("  %s: ConvertSuit failed at stage %s: %s\n", stem.c_str(), f.stage.c_str(), f.what.c_str());
      if (!files_equal || !trace_equal) differ.insert(s.design);
      std::printf("  %s under a stable argsort: files %s, trace %s\n", stem.c_str(), files_equal ? "equal" : "DIFFER",
                  trace_equal ? "equal" : "DIFFERS");
      if (!trace_equal) ct.Print(stem.c_str());
      CKF(m1.has_value(), "%s: m1 converted under the stable argsort (%s: %s)", stem.c_str(), f.stage.c_str(),
          f.what.c_str());
      if (want_differ.count(s.design)) {
        CKF(!files_equal, "%s: the stable argsort must change the files", stem.c_str());
      } else {
        CKF(files_equal && trace_equal, "%s: the stable argsort must not change anything", stem.c_str());
      }
    }
  }
  CK(num::GetArgsortForTest() == nullptr);  // the scope put the port back
  std::printf("%s: %zu suits, differ under a stable argsort: %s\n", group, seen.size(), Names(differ).c_str());
  if (!Env("DOA4_SUITCONV_ONLY")) {
    CK(seen.size() == 5);
    CKF(differ == want_differ, "differ: %s", Names(differ).c_str());
  }
}

static void TestH4AbsL2() { H4Abs(Level::kL2, "h4_abs_L2"); }
static void TestH4AbsL3() { H4Abs(Level::kL3, "h4_abs_L3"); }
static void TestTraceM1() { TraceAll(Level::kM1, "trace_m1"); }
static void TestH4AbsM1() { H4Abs(Level::kM1, "h4_abs_m1"); }

static void RunGroup(void (*fn)(), const char* name) {
  try {
    fn();
  } catch (const std::exception& e) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL %s threw: %s\n", name, e.what());
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  RunGroup(TestSyntheticBodyConverts, "synthetic_body_converts");
  RunGroup(TestAttachNanFallback, "attach_nan_fallback");
  RunGroup(TestLevelGates, "level_gates");
  RunGroup(TestClothNumeric, "cloth_numeric");
  RunGroup(TestUnstickRounds, "unstick_rounds");
  RunGroup(TestGenPrepSynthetic, "gen_prep_synthetic");
  RunGroup(TestGenClothSynthetic, "gen_cloth_synthetic");
  RunGroup(TestGenNumeric, "gen_numeric");
  RunGroup(TestWalkRejectsUnterminatedWrites, "walk_rejects_unterminated_writes");
  RunGroup(TestAttachFrame, "attach_frame");
  RunGroup(TestAttachApa, "attach_apa_synthetic");
  RunGroup(TestAttachClothPins, "attach_cloth_pins");
  RunGroup(TestSparesMustEndFile, "spares_must_end_file");
  RunGroup(TestHeadPhysicsSynthetic, "head_physics_synthetic");
  RunGroup(TestHandsNumeric, "hands_numeric");
  RunGroup(TestMpvHeaderMarks, "mpv_header_marks");
  RunGroup(TestMpvThresholdIsMathDist, "mpv_threshold_is_math_dist");
  RunGroup(TestPrepBustSynthetic, "prep_bust_synthetic");
  RunGroup(TestBustPeakRules, "bust_peak_rules");
  RunGroup(TestL1Synthetic, "l1_synthetic");
  RunGroup(TestOracleVayOnK, "oracle_L0_vay_on_k");
  RunGroup(TestOracleAll, "oracle_L0_all");
  RunGroup(TestTraceAll, "trace_L0_all");
  RunGroup(TestOracleL1All, "oracle_L1_all");
  RunGroup(TestTraceL1All, "trace_L1_all");
  RunGroup(TestOracleL2All, "oracle_L2_all");
  RunGroup(TestTraceL2All, "trace_L2_all");
  RunGroup(TestH4AbsL2, "h4_abs_L2");
  RunGroup(TestOracleL3All, "oracle_L3_all");
  RunGroup(TestTraceL3All, "trace_L3_all");
  RunGroup(TestH4AbsL3, "h4_abs_L3");
  RunGroup(TestOracleM1WithoutExtras, "oracle_m1_without_extras");
  RunGroup(TestOracleM1SparesHead, "oracle_m1_spares_head");
  RunGroup(TestOracleM1Cloth, "oracle_m1_cloth");
  RunGroup(TestOracleM1Gen, "oracle_m1_gen");
  RunGroup(TestArgsortIsWhatMatters, "argsort_is_what_matters");
  RunGroup(TestTraceM1, "trace_m1");
  RunGroup(TestH4AbsM1, "h4_abs_m1");
  RunGroup(TestWaistBandShortFails, "waist_band_short_fails");
  RunGroup(TestWaistSynthetic, "waist_synthetic");
  std::printf("skipped sections: %d\n", g_skipped);
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
