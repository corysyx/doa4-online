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
//                       the other 18 stay gated
// On a difference the oracle groups print the first differing offset and what is there (WhereInTpr / WhereInCat:
// object, decl, vertex and field; or the .cat section and record) and, with traces, the first differing trace line,
// both lines and the last S line before it. No byte of any game file is stored: the real sections read the player's
// files at test time. Last lines: "skipped sections: K", then "PASSED: N checks, 0 failures" or "FAILED: ...".

#include "../../src/doa4_suitconv.h"
#include "../../src/doa4_suitconv_bust.h"
#include "../../src/doa4_suitconv_catx.h"
#include "../../src/doa4_suitconv_core.h"
#include "../../src/doa4_suitconv_hands.h"
#include "../../src/doa4_suitconv_inputs.h"
#include "../../src/doa4_suitconv_num.h"
#include "../../src/doa4_suitconv_recipe.h"
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

static void TestLevelGates() {
  Group("level_gates");
  const auto s = synth::conv::MakeInputs();
  const SuitInputs in = SynthInputs(s);
  SuitRecipe suit = SynthSuit();
  const GirlRecipe girl = SynthGirl();
  suit.port_cloth = true;  // an M1 feature comes first at M1
  {
    ConvertFailure f;
    const auto r = ConvertSuit(suit, girl, in, Level::kM1, nullptr, nullptr, &f);
    CKF(!r && f.stage == "level" && f.what == "not ported yet: port_cloth", "%s / %s", f.stage.c_str(), f.what.c_str());
  }
  suit.port_cloth = false;
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
  CK(throws([&] { catx::Attach(cat, nullptr, &one, nullptr, nullptr); }, "MULTI_CLOTH"));
  CK(throws([&] { catx::Attach(cat, &one, &one, nullptr, nullptr); }, "pin rows"));
  CK(throws([&] { catx::Attach(cat, nullptr, nullptr, &empty, nullptr); }, "read past end"));  // a donor with no header
  CK(throws([&] { catx::Attach(cat, nullptr, nullptr, nullptr, &one); }, "head physics"));
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

// ---------------------------------------------------------------------------------------------------------------
// hands_numeric: the software fused multiply-add and np.linalg.norm of a 3-vector
// ---------------------------------------------------------------------------------------------------------------
// np.linalg.norm of a small vector is OpenBLAS's ddot, which fuses each multiply-add on the PC that made the oracle.
// SoftFma is the exact a*b + c rounded once (pins: Python Fraction arithmetic, correctly rounded); Norm3 is the
// sqrt of acc = fma(v[i], v[i], acc) from 0 (pins: np.linalg.norm of [0, y, z] on that PC, numpy 1.26.3). Seeded
// values, no game data. The pins of the first table include cancellation cases and cases where the product's error
// term alone decides the last bit, so a plain a*b + c, or a sum without the round-to-odd step, differs on some of them.

static const uint64_t kFmaPins[][4] = {
    {0xbfb135f2c50fc325ull, 0x4006be149121c833ull, 0xbfa2221faf58ab6full, 0xbfccff2c7d3ca767ull},
    {0xbfa4593f57be2358ull, 0x401991bb8e52f8efull, 0x40136c49b5c7884aull, 0x40126823634db6eaull},
    {0xbf80d8c8bd0adebcull, 0x3fc45fa0d22134beull, 0xbfcbc9b90b704fdfull, 0xbfcbf4a06242df34ull},
    {0x3f7e0330479b8744ull, 0xbf57a660c9564755ull, 0x3f799016c1368d78ull, 0x3f7984ff963464f3ull},
    {0xbf50636527643a31ull, 0xbf4e3af58c5e86f6ull, 0xbfc058a7e2470945ull, 0xbfc058a02496aab1ull},
    {0xbf8880d471bfc07eull, 0x3f6797feaaccfe87ull, 0x3f72a10134fad01dull, 0x3f727cdf3dbf9ba7ull},
    {0xbf2773137319ecd0ull, 0x3f9f68021e360e56ull, 0xc002aebe2c040401ull, 0xc002aec10c7a5082ull},
    {0x3f8bf98d2af46c3cull, 0x3fa27d501fca5cbaull, 0xbf7a7a2aa00a7e94ull, 0xbf7874ed17a1e30full},
    {0x3f36c81b68d2f34dull, 0xbf8622e4c0aa448aull, 0xbf04a30aa12f3bc6ull, 0xbf069b57eb32ed99ull},
    {0x402153c1296c8bc3ull, 0x3ff758e79c70bb81ull, 0x3ffff54e38dcfa86ull, 0x402d4756ebaaf3f3ull},
    {0xbf469509b89fc691ull, 0xbfa8a14d8c03d846ull, 0x3fe8216bd7df5d6bull, 0x3fe821b15e4ed0a6ull},
    {0xbf6c0f3f1059b725ull, 0xbf628981dd9cf9ffull, 0xbfdd76b66ad99bb7ull, 0xbfdd7695e88c5dc4ull},
    {0x3f9a0010ba21256cull, 0x3fb1e997010a8603ull, 0x3ff83988a4636ad0ull, 0x3ff840cf8e6a54fcull},
    {0x3fccc55b7b89f424ull, 0x3f9cac2cf433018full, 0xbfb51b60589c910cull, 0xbfb37ee88470bd64ull},
    {0xbf8de2607ea176a1ull, 0xc01e319077d0ffe1ull, 0x3f5cc3a671a756d6ull, 0x3fbca5929ed722e7ull},
    {0xbf7f8170f4475052ull, 0xbf762f6c26afb859ull, 0xbfb26b037c284589ull, 0xbfb2684886602c11ull},
    {0xbf60acbc02177a0full, 0x3f6378153064ce5full, 0x3f67a5214107288bull, 0x3f679afc1e9a4fc0ull},
    {0x3f65f7ded4ff5c4full, 0xbfc6930f841c7b3eull, 0x3fec45fee033de4cull, 0x3fec421f0893a12dull},
    {0x400104fb8337ca47ull, 0xbff95eb460f59895ull, 0x3f613bd7bd1222b8ull, 0xc00af8372beb772bull},
    {0x3fe3cbf7d6e74c7cull, 0x401a21b084b76109ull, 0xbfd386c424b2aabcull, 0x400de4494fa8a18full},
    {0xbf7fe1659214a738ull, 0x3f7b36484db9c736ull, 0xbfe937599fe09040ull, 0xbfe937c610e8ed30ull},
    {0x3fa4d25c0d5f12e0ull, 0x4016aa19553f05d6ull, 0xbfa3e9372c18dbfeull, 0x3fc8844cd109f6ebull},
    {0xbfd7fcedea1afae8ull, 0xbf9385b75f78d61eull, 0xbf467564413d69efull, 0x3f7a76276386712aull},
    {0x4003daa0067cfeb8ull, 0x3f6ec588a374bc78ull, 0x401420ea25fd14abull, 0x40142a75e857fc4full},
    {0x3fea584d2dbfe332ull, 0x3ff3b80edf404254ull, 0xbff03bf5cd5f0351ull, 0xbca12d4429f68f98ull},
    {0x3ff308302d9f60a4ull, 0x3fee015c57d8eea7ull, 0xbff1d87c5810a2f1ull, 0xbca1d6e6cacf7d04ull},
    {0x3fe3f1ce1f02c966ull, 0x3feb613590054102ull, 0xbfe1109c45392393ull, 0x3cb2af7c50899e33ull},
    {0x3fee61f382fe7d4aull, 0x3fe8d0e8f8ea165full, 0xbfe78fd0838d2a94ull, 0x3c866f5b61b1b4ecull},
    {0x3fecc5a41054c577ull, 0x3feba2993ad3f528ull, 0xbfe8d8f4c2376ed5ull, 0x3c9de1fe654ebd98ull},
    {0x3ff4baf5dc3ff490ull, 0x3fef87c221e0e9a8ull, 0xbff46d10adfc2ba0ull, 0x3cb45935a6c7c740ull},
    {0x3ff4a7aa296d67dcull, 0x3fe897a7e8800d29ull, 0xbfefbf46562f965bull, 0x3c7efd93f65e71e0ull},
    {0x3ff68a9db43c6267ull, 0x3ff59bc54d75602aull, 0xbffe7163cc277957ull, 0x3c9a03acab871398ull},
    {0x3fecde3c6c4d024cull, 0x3fed75f1903e2618ull, 0xbfea93cd3ae8f422ull, 0x3ca39f9d71fbbf90ull},
    {0x3fed198e6548ea84ull, 0x3fe904db32f4ee7full, 0xbfe6c061c62acfbcull, 0x3ca0c303ab8187beull},
    {0x3fe17150aeaff37cull, 0x3ff2b9d1ee373fceull, 0xbfe46a0dd5497971ull, 0xbca475af89da8e38ull},
    {0x3ff079adc2baa41full, 0x3ff11a0dedd8bbd0ull, 0xbff19c1cb10975c5ull, 0x3ca8bad5755ffc60ull},
    {0x3ff11f46fbf76388ull, 0x3ff6cbc2735ed3f2ull, 0xbff8650e85c3f375ull, 0xbc800ce229568b80ull},
    {0x3ff7b44b41336afaull, 0x3fe8b09377811b56ull, 0xbff24a054b8cf976ull, 0xbc90df4d1cef6408ull},
    {0x3fe0e6f7307efd2cull, 0x3ff20f77b812a501ull, 0xbfe3142d168e9961ull, 0x3ca02b9276c4592cull},
    {0x3ff57f7561b1649full, 0x3ff1bcac102e651eull, 0xbff7d4ed8af40529ull, 0x3cb4424a740e85a2ull},
    {0x3ffbd59deb7a63c0ull, 0x3fff69c90a584656ull, 0x3cb0000000000000ull, 0x400b52f4c912b34dull},
    {0x3ff4b3f513c325deull, 0x3ffffd4d48b3265aull, 0xbcb0000000000000ull, 0x4004b23634d7d1ffull},
    {0x3ffbc06df09b2494ull, 0x3ff5913a5a337904ull, 0x3cb0000000000000ull, 0x4002b43ab3112bf0ull},
    {0x3ffe2a7204724fdaull, 0x3ff867555eb09471ull, 0xbcb0000000000000ull, 0x4007013e9c560bf3ull},
    {0x3ffe14aa673bcd3full, 0x3ffc2eba375fcb5bull, 0x3cc8000000000000ull, 0x400a7e01db74c2ffull},
    {0x3ff5bda85d679100ull, 0x3ff6dcae48413aeeull, 0x3c90000000000000ull, 0x3fff10a41c27c763ull},
    {0x3ff8e4274080e8b2ull, 0x3ffc0c7e8c392bdaull, 0xbcb0000000000000ull, 0x4005d15a56962e2aull},
    {0x3ff7fd33cf1cdab8ull, 0x3ff03420fed769ceull, 0x3ca0000000000000ull, 0x3ff84b5c2ffc263cull},
    {0x3ff2d55e5627c06eull, 0x3ff983c2a7c0becfull, 0x3ca0000000000000ull, 0x3ffe087b7b8d406full},
    {0x3ff9442493730c84ull, 0x3ff206d79a43cec4ull, 0x3c90000000000000ull, 0x3ffc77772adf2a7bull},
    {0x3ffae8e5fee4a0ffull, 0x3ff45261c6d7ccdfull, 0x3cb0000000000000ull, 0x400116d6c5dbc34cull},
    {0x3ff3d649001a0c68ull, 0x3ff01c510294223aull, 0x3c90000000000000ull, 0x3ff3f9646fde3c34ull},
    {0x3ff73071a782f9bbull, 0x3ff8e1c16a9ac7d0ull, 0x3cc8000000000000ull, 0x400207ee063228f1ull},
    {0x3ff36bf4c0aa0feaull, 0x3ff0f05a48b4a2cdull, 0x3cb8000000000000ull, 0x3ff48fb5af75d419ull},
    {0x3ffee39934b6f52eull, 0x3ff589a2e63a7360ull, 0xbcb0000000000000ull, 0x4004ca3830f5a41dull},
    {0x3ff262db7e4b08acull, 0x3ff1b722ef450497ull, 0x3cb8000000000000ull, 0x3ff45b7c09aa9e12ull},
    {0xbfbb8e917d246e5full, 0xbfbb8e917d246e5full, 0x3f0e39e59dbfc150ull, 0x3f87d94a96975559ull},
    {0x3f520820e63159d6ull, 0x3f520820e63159d6ull, 0x3ec5e2a4b372f555ull, 0x3ed005e5e37fb2f7ull},
    {0xbf43d6ea3270d6f3ull, 0xbf43d6ea3270d6f3ull, 0x3ec5f9502bfb6d3full, 0x3ec90c868bba16deull},
    {0x3fda769f85cea95cull, 0x3fda769f85cea95cull, 0x3f7229479983a86bull, 0x3fc673c531d2007full},
    {0x3f529dbf1e2c5985ull, 0x3f529dbf1e2c5985ull, 0x3e90b16f4407aa1full, 0x3eb9d55d1480baecull},
    {0x3f0b0be4d52a87bfull, 0x3f0b0be4d52a87bfull, 0x3eaa815f00f75942ull, 0x3eaa983b178ac7f7ull},
    {0x3f7b3ed45c3d9905ull, 0x3f7b3ed45c3d9905ull, 0x3e522d7c6bd71787ull, 0x3f0734c7679c2306ull},
    {0x3f774ed0919745c6ull, 0x3f774ed0919745c6ull, 0x3dd8d3b61f6e1ea2ull, 0x3f00fa1109d560f3ull},
    {0xbf733b97217089bbull, 0xbf733b97217089bbull, 0x3ef95920fd8b1a57ull, 0x3f083bc2ee90436eull},
    {0xbf0c5abf554d61e9ull, 0xbf0c5abf554d61e9ull, 0x3f0882ec4e87e536ull, 0x3f088350cdc89d33ull},
    {0xbfa08b722ef5fc68ull, 0xbfa08b722ef5fc68ull, 0x3ec802355d96bf1full, 0x3f5127a4cbb993ecull},
    {0xbf93e7f127ebd228ull, 0xbf93e7f127ebd228ull, 0x3eb9ffa28db819a3ull, 0x3f38ddfeb2f14791ull},
    {0xbf0ba07435655cbbull, 0xbf0ba07435655cbbull, 0x3f065d981b99a64bull, 0x3f065df7833c3da7ull},
    {0xbf5537ff2ad2cdedull, 0xbf5537ff2ad2cdedull, 0x3f217c937696b715ull, 0x3f21b4dafa2be16cull},
    {0xbfa34affd406bee5ull, 0xbfa34affd406bee5ull, 0x3eba64ca92703235ull, 0x3f574a1858986123ull},
    {0xbfd1ba69174c20cdull, 0xbfd1ba69174c20cdull, 0x3f854a67d832d859ull, 0x3fb64de81a980ca2ull},
};
static const uint64_t kNormPins[][3] = {
    {0xbf4a2cc1d5dc4a21ull, 0xbf4f6a44c8f6cc04ull, 0x3f5471fb28f25816ull},
    {0xbf31b7a301dcacdfull, 0x3f0da2cbd1fbbeceull, 0x3f3219b87fbc9843ull},
    {0x3f84514d9611781full, 0x3fa09acaa7f0d47full, 0x3fa15d3b1353a8c6ull},
    {0x3f07d24123f2031bull, 0xbf16c47f3bb55dd5ull, 0x3f19b1e23b39b7efull},
    {0x3f9b07124100b75cull, 0xbf760a78972dfe8eull, 0x3f9b9566c44888a8ull},
    {0xbee051ae269ba848ull, 0xbf1460e28a53717dull, 0x3f147af4d570e0caull},
    {0xbfdbda4453a0ddc0ull, 0xbfc08fe5f5751f18ull, 0x3fdd0ebe1674526aull},
    {0xbfa41376b8e028d7ull, 0x3fb2890b0ebbb16dull, 0x3fb51432c8533027ull},
    {0xbf2536312bca954bull, 0x3f07460a6634460dull, 0x3f25fec662b91dc3ull},
    {0x3f916708790092d2ull, 0x3f83e11e027e933eull, 0x3f940a81b36aaef5ull},
    {0xbf3ef92460b9ac30ull, 0xbf100cb7ae7fc8a2ull, 0x3f3f3b65ee98f091ull},
    {0x3f5fdd817ea0aad8ull, 0x3f2229ed9516dbedull, 0x3f5ff22fdf40e35bull},
    {0xbfcad041de1ab738ull, 0xbfac956a57c674f6ull, 0x3fcbbfd76b047a2cull},
    {0x3f491facf1bbf324ull, 0x3f46fc99d8dc6763ull, 0x3f5106c144d09fd3ull},
    {0x3f88eea81f6b228full, 0x3fb251bd9158e030ull, 0x3fb2951f06280237ull},
    {0x3f87b7e1767d9f1bull, 0xbf737eb1fedde9fbull, 0x3f89a4a3c7df1a23ull},
    {0xbf939bbeffac5db7ull, 0x3fcbe8a1d06ad41full, 0x3fcc0421e91b5311ull},
    {0x3f7ec311f10a1177ull, 0x3f9b7cae53b6bbddull, 0x3f9c8ae8cc86a142ull},
    {0xbf8f4ad0e8337068ull, 0x3fa29a7171bf1ce3ull, 0x3fa42e6660879d9dull},
    {0x3f85a77afe4b0d3aull, 0x3f7a2b4792044fb9ull, 0x3f894ce6f69b7630ull},
    {0x3f50efc6bbb67ca4ull, 0x3f6de7c7406ef9e8ull, 0x3f6f14cd08de2754ull},
    {0x3f3d410ed3de6d22ull, 0xbf2fc019fc820965ull, 0x3f40a45addc12075ull},
    {0xbf80fa2e7de65175ull, 0xbf8ee50de25517b7ull, 0x3f91a045e8b2a19dull},
    {0x3f3ecf76aafb3d74ull, 0x3f3923b619aab3f7ull, 0x3f43e1f3d01e8905ull},
    {0x3fa07ef9fc30c5d6ull, 0x3f973b1fa57eacb2ull, 0x3fa42cd7bcefa1a7ull},
    {0xbfcbd64b573a87e6ull, 0xbfc7177fdc95ebcdull, 0x3fd21585a2b3855aull},
    {0xbfb403a4f55f8e4bull, 0x3facf13feb4bcf31ull, 0x3fb8b2a7e4f9c99cull},
    {0xbfa4a7eafae09f98ull, 0xbf7e937c3eaf9c19ull, 0x3fa501ad5c643476ull},
    {0x3f4b7709a7b3dc99ull, 0x3f458e5ac6c2db75ull, 0x3f5174ff2cd0f279ull},
    {0xbf5ddf7e61437766ull, 0xbf4c74dbc26d10bcull, 0x3f608b4fb6d5a858ull},
    {0xbee829bbada51fe3ull, 0x3f16f32035983152ull, 0x3f1725c9d013d025ull},
    {0xbf0a6487fece6329ull, 0x3f20e97f56425114ull, 0x3f2227559e57c754ull},
};

static void TestHandsNumeric() {
  Group("hands_numeric");
  const num::FpEnvGuard env;
  int n = 0, plain_differs = 0;
  for (const auto& row : kFmaPins) {
    const double a = std::bit_cast<double>(row[0]), b = std::bit_cast<double>(row[1]), c = std::bit_cast<double>(row[2]);
    const double got = doa4::suitconv::hands::SoftFma(a, b, c);
    CKF(std::bit_cast<uint64_t>(got) == row[3], "SoftFma pin %d: got %016llx want %016llx", n,
        (unsigned long long)std::bit_cast<uint64_t>(got), (unsigned long long)row[3]);
    volatile double prod = a * b;  // two roundings
    if (std::bit_cast<uint64_t>(prod + c) != row[3]) ++plain_differs;
    ++n;
  }
  CKF(plain_differs >= 8, "%d of %d pins differ from a*b + c", plain_differs, n);
  int m = 0;
  for (const auto& row : kNormPins) {
    const core::Vec3 v{0.0, std::bit_cast<double>(row[0]), std::bit_cast<double>(row[1])};
    const double got = doa4::suitconv::hands::Norm3(v);
    CKF(std::bit_cast<uint64_t>(got) == row[2], "Norm3 pin %d", m);
    ++m;
  }
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
  // special values keep the plain result
  CK(std::isnan(doa4::suitconv::hands::SoftFma(std::numeric_limits<double>::quiet_NaN(), 1.0, 1.0)));
  CK(doa4::suitconv::hands::SoftFma(0.0, 5.0, 3.0) == 3.0);
  std::printf("hands_numeric: %d fma pins (%d differ from a*b + c), %d norm pins\n", n, plain_differs, m);
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
    if (!OnlyAllows(s)) continue;
    eq += OracleOne(*set, env, s, level, false, true, nullptr);
    ++n;
  }
  std::printf("%s: %d/%d traces equal at %s\n", group, eq, n, LevelDir(level));
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
    if (!OnlyAllows(s)) continue;
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
  CK(suits == 34 || Env("DOA4_SUITCONV_ONLY"));
  CK(values > 0);
}

// ---------------------------------------------------------------------------------------------------------------
// oracle_m1_without_extras: at level m1 the 16 suits that need no feature after the hands equal the shipped m1 files
// ---------------------------------------------------------------------------------------------------------------
// DOA4_SUITCONV_ORACLE_DIR is the folder of the shipped m1 outputs (<design>_<colour>.tpr / .cat). A suit is in the group
// when none of port_cloth, gen_cloth, fit_leg_colliders, unstick_cloth, back_margin, sink_hamstrings, and its girl's
// hair_spares and head_physics, applies: for those the recipe's m1 output is its L3 output (the later features do not
// run), so the C++ m1 must equal both the shipped file and the C++ L3. The other 18 stay gated at m1.
static void TestOracleM1WithoutExtras() {
  const char* group = "oracle_m1_without_extras";
  OracleEnv env;
  if (!OracleEnvOrSkip(group, &env, false)) return;
  const char* odir = Env("DOA4_SUITCONV_ORACLE_DIR");
  if (!odir) return SkipGroup(group, "DOA4_SUITCONV_ORACLE_DIR");
  Group(group);
  InputSet* set = OpenSet(env);
  if (!set) return;
  static const char* const kSixteen[] = {"vay_cs_b", "vay_on_k", "vay_sk_d", "vht_cs_f", "vht_sp_h", "vht_on_e",
                                         "vks_wi_b", "vhe_cm_a", "vhe_on_d", "vhe_dl_l", "vls_cs_h", "vls_on_j",
                                         "vls_sp_a", "vcr_cs_g", "vcr_on_n", "vcr_ch_e"};
  std::set<std::string> want16(std::begin(kSixteen), std::end(kSixteen)), derived;
  int equal = 0, same_as_l3 = 0, gated = 0, n = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s)) continue;
    ++n;
    const std::string stem = Stem(s);
    const GirlRecipe* girl = FindGirl(s.girl);
    CKF(girl != nullptr, "%s: no girl", stem.c_str());
    if (!girl) continue;
    const bool free = !s.port_cloth && !s.gen_cloth && !s.fit_leg_colliders && !s.unstick_cloth && s.back_margin == 0.0 &&
                      s.sink_hamstrings == 0.0 && !girl->hair_spares && !girl->head_physics;
    if (free) derived.insert(s.design);
    SuitInputs in;
    try {
      in = ReadSuitInputs(*set, s, *girl);
    } catch (const std::exception& e) {
      CKF(false, "%s: ReadSuitInputs: %s", stem.c_str(), e.what());
      continue;
    }
    ConvertFailure f;
    const auto m1 = ConvertSuit(s, *girl, in, Level::kM1, nullptr, nullptr, &f);
    if (!free) {  // vacuity control: a suit with a later feature is still refused at m1
      CKF(!m1 && f.stage == "level" && f.what.rfind("not ported yet: ", 0) == 0, "%s: m1 gate: %s / %s", stem.c_str(),
          f.stage.c_str(), f.what.c_str());
      if (!m1) ++gated;
      continue;
    }
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
  std::string names;
  for (const std::string& d : derived) names += (names.empty() ? "" : " ") + d;
  std::printf("%s: derived list (%zu): %s\n", group, derived.size(), names.c_str());
  std::printf("%s: %d/%zu equal to the shipped m1 files, %d equal to C++ L3, %d suits with a later feature still gated\n",
              group, equal, want16.size(), same_as_l3, gated);
  if (!Env("DOA4_SUITCONV_ONLY")) {
    CKF(derived == want16, "the derived list is the brief's 16 (%zu derived)", derived.size());
    CK(n == 34 && gated == 18);
    CK(equal == 16 && same_as_l3 == 16);
  }
}

static void TestH4AbsL2() { H4Abs(Level::kL2, "h4_abs_L2"); }
static void TestH4AbsL3() { H4Abs(Level::kL3, "h4_abs_L3"); }

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
  RunGroup(TestAttachFrame, "attach_frame");
  RunGroup(TestAttachApa, "attach_apa_synthetic");
  RunGroup(TestHandsNumeric, "hands_numeric");
  RunGroup(TestMpvHeaderMarks, "mpv_header_marks");
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
