// Host test for the suit converter's geometry core (src/doa4_suitconv_core.cpp, doa4_suitconv_trace.cpp,
// doa4_suitconv_catx.cpp, doa4_suitconv.cpp): convert.py's port at recipe level L0. Built and run by
// run_suitconv_convert_test.bat with the converter's compile flags.
//
// Groups: synthetic_body_converts (synthetic models from suitconv_synth.h: the output parses with Task 4's parser, every
// skinned object's palette starts with its owner's parent, two runs give equal bytes and equal traces), level_gates
// (the levels not ported yet fail with ConvertFailure{"level", ...}; a cancelled token fails with "cancelled"),
// attach_frame (physics_port.attach at L0 and its not-ported branches); and three groups gated by environment variables
// (each prints "<group>: SKIPPED (<VAR> unset)" when a variable is unset):
//   oracle_L0_vay_on_k  DOA4_SUITCONV_DISC, DOA4_SUITCONV_GAME_DIR, DOA4_SUITCONV_LEVELS_DIR: C++ L0 equals the Python's
//                       L0 for vay_on_k, both files (with DOA4_SUITCONV_TRACE_DIR also the trace)
//   oracle_L0_all       the same three: both files, all 34 suits; prints each suit's conversion time (one thread)
//   trace_L0_all        the three and DOA4_SUITCONV_TRACE_DIR: the C++ trace equals the Python's line for line, all 34
// On a difference the oracle groups print the first differing offset and what is there (WhereInTpr / WhereInCat:
// object, decl, vertex and field; or the .cat section and record) and, with traces, the first differing trace line,
// both lines and the last S line before it. No byte of any game file is stored: the real sections read the player's
// files at test time. Last lines: "skipped sections: K", then "PASSED: N checks, 0 failures" or "FAILED: ...".

#include "../../src/doa4_suitconv.h"
#include "../../src/doa4_suitconv_catx.h"
#include "../../src/doa4_suitconv_core.h"
#include "../../src/doa4_suitconv_inputs.h"
#include "../../src/doa4_suitconv_num.h"
#include "../../src/doa4_suitconv_recipe.h"
#include "../../src/doa4_suitconv_tpr.h"
#include "../../src/doa4_suitconv_trace.h"
#include "suitconv_synth.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

static int g_checks = 0;
static int g_failures = 0;
static int g_skipped = 0;
static int g_printed = 0;
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
  void Print(const char* label) const {
    std::printf("  %s: trace differs at line %zu (C++ wrote %zu lines, Python %zu)\n", label, at_ + 1, n_, exp_.size());
    std::printf("    last S line: %s\n", last_s_.c_str());
    std::printf("    python: %.300s\n", at_ < exp_.size() ? exp_[at_].c_str() : "<end of the Python trace>");
    std::printf("    c++   : %.300s\n", got_.c_str());
  }

 private:
  std::vector<std::string> exp_;
  size_t n_ = 0, at_ = 0;
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

static void TestLevelGates() {
  Group("level_gates");
  const auto s = synth::conv::MakeInputs();
  const SuitInputs in = SynthInputs(s);
  SuitRecipe suit = SynthSuit();
  const GirlRecipe girl = SynthGirl();
  struct Case {
    Level level;
    const char* what;
  };
  for (const Case& k : {Case{Level::kL1, "not ported yet: bust"}, Case{Level::kL2, "not ported yet: waist"},
                        Case{Level::kL3, "not ported yet: hands"}, Case{Level::kM1, "not ported yet: hands"}}) {
    ConvertFailure f;
    const auto r = ConvertSuit(suit, girl, in, k.level, nullptr, nullptr, &f);
    CKF(!r && f.stage == "level" && f.what == k.what, "level %d: %s / %s", int(k.level), f.stage.c_str(), f.what.c_str());
  }
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
  CK(throws([&] { catx::Attach(cat, &one, nullptr, nullptr, nullptr); }, "MPV"));
  CK(throws([&] { catx::Attach(cat, nullptr, &one, nullptr, nullptr); }, "MULTI_CLOTH"));
  CK(throws([&] { catx::Attach(cat, nullptr, nullptr, &empty, nullptr); }, "donor"));
  CK(throws([&] { catx::Attach(cat, nullptr, nullptr, nullptr, &one); }, "head physics"));
  std::vector<uint8_t> b(17, 1);
  CK(catx::Al(b, 16) == 32);
  CK(catx::Al(b, 16) == 32);
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
static int OracleOne(InputSet& set, const OracleEnv& env, const SuitRecipe& s, bool files, bool compare_trace,
                     double* secs) {
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
    const auto tb = ReadFile(fs::path(env.traces) / "L0" / (stem + ".trace"));
    CKF(tb.has_value(), "%s: no oracle trace", stem.c_str());
    if (!tb) return 0;
    ct = std::make_unique<CompareTrace>(SplitLines(*tb));
  }
  ConvertFailure f;
  const auto t0 = std::chrono::steady_clock::now();
  const auto out = ConvertSuit(s, *girl, in, Level::kL0, nullptr, ct.get(), &f);
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
  CKF(!ct || ct->Equal(), "%s: trace", stem.c_str());
  if (files && out) {
    const auto wt = ReadFile(fs::path(env.levels) / "L0" / (stem + ".tpr"));
    const auto wc = ReadFile(fs::path(env.levels) / "L0" / (stem + ".cat"));
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
  const int eq = OracleOne(*set, env, *s, true, env.traces != nullptr, &secs);
  std::printf("oracle_L0_vay_on_k: %s (%s), %.2f s\n", eq ? "equal" : "DIFFERS",
              env.traces ? "files and trace" : "files", secs);
}

static void TestOracleAll() {
  OracleEnv env;
  if (!OracleEnvOrSkip("oracle_L0_all", &env, false)) return;
  Group("oracle_L0_all");
  InputSet* set = OpenSet(env);
  if (!set) return;
  int eq = 0, n = 0;
  double total = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s)) continue;
    double secs = 0;
    const int e = OracleOne(*set, env, s, true, false, &secs);
    eq += e;
    ++n;
    total += secs;
    std::printf("  L0 %-10s %s %.2f s\n", Stem(s).c_str(), e ? "equal " : "DIFFERS", secs);
  }
  std::printf("oracle_L0_all: %d/%d suits equal at L0 (both files), %.1f s converting (one thread)\n", eq, n, total);
  CK(n == 34 || Env("DOA4_SUITCONV_ONLY"));
  // The same suits again in reverse order, in this process: no state may carry from one conversion to the next (the
  // Python ran each suit in a fresh process; plan row 11).
  int req = 0, rn = 0;
  const auto suits = Suits();
  for (size_t i = suits.size(); i-- > 0;) {
    if (!OnlyAllows(suits[i])) continue;
    const int e = OracleOne(*set, env, suits[i], true, false, nullptr);
    if (!e) std::printf("  L0 reverse %-10s DIFFERS\n", Stem(suits[i]).c_str());
    req += e;
    ++rn;
  }
  std::printf("oracle_L0_all: %d/%d suits equal at L0 in reverse order (same process)\n", req, rn);
}

static void TestTraceAll() {
  OracleEnv env;
  if (!OracleEnvOrSkip("trace_L0_all", &env, true)) return;
  Group("trace_L0_all");
  InputSet* set = OpenSet(env);
  if (!set) return;
  int eq = 0, n = 0;
  for (const SuitRecipe& s : Suits()) {
    if (!OnlyAllows(s)) continue;
    eq += OracleOne(*set, env, s, false, true, nullptr);
    ++n;
  }
  std::printf("trace_L0_all: %d/%d traces equal at L0\n", eq, n);
}

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
  RunGroup(TestLevelGates, "level_gates");
  RunGroup(TestAttachFrame, "attach_frame");
  RunGroup(TestOracleVayOnK, "oracle_L0_vay_on_k");
  RunGroup(TestOracleAll, "oracle_L0_all");
  RunGroup(TestTraceAll, "trace_L0_all");
  std::printf("skipped sections: %d\n", g_skipped);
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
