// Suit converter (Plan 2): the geometry core, convert.py's port. See doa4_suitconv_core.h. Line numbers in the
// comments are convert.py's (SHA-1 05cb00536712a89d9cd4cb8f79cebe529e6db2fa).
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <tuple>

#include "doa4_suitconv_be.h"
#include "doa4_suitconv_sort.h"

namespace doa4::suitconv::core {

using be::Reader;

[[noreturn]] void Fail(const std::string& what) { throw ConvertError(what); }

void Assert(bool cond, const std::string& text) {
  if (!cond) throw ConvertError("AssertionError: " + text);
}

double PyDiv(double a, double b) {
  if (b == 0.0) Fail("ZeroDivisionError: float division by zero");
  return a / b;
}

uint32_t U32(std::span<const uint8_t> d, int64_t o) { return Reader(d).U32(o); }

void Weights::Add(int64_t id, double w) {
  for (auto& [k, v] : items_) {
    if (k == id) {
      v = v + w;
      return;
    }
  }
  items_.emplace_back(id, 0.0 + w);
}

void Weights::Put(int64_t id, double w) {
  for (auto& [k, v] : items_) {
    if (k == id) {
      v = w;
      return;
    }
  }
  items_.emplace_back(id, w);
}

Weights Weights::Above(double threshold) const {
  Weights out;
  for (const auto& [k, v] : items_)
    if (v > threshold) out.items_.emplace_back(k, v);
  return out;
}

namespace {

// ------------------------------------------------------------------ tables (lines 41-95)
const std::map<int64_t, int64_t>& Role() {
  static const std::map<int64_t, int64_t> t = {
      {2, 0x9002},  {40, 0x9002}, {0, 0x9000},  {6, 0x9000},  {25, 0x9000}, {21, 0x9101}, {1, 0x9101},
      {8, 0x9101},  {27, 0x9101}, {18, 0x9008}, {5, 0x9108},  {11, 0x9004}, {7, 0x9104},  {12, 0x9005},
      {20, 0x9105}, {37, 0x900E}, {24, 0x910E}, {30, 0x900A}, {26, 0x910A}, {31, 0x900B}, {39, 0x910B},
      {14, 0x9006}, {16, 0x9106}, {17, 0x9007}, {15, 0x9107}, {9, 0x9003},  {3, 0x9103},  {19, 0x9003},
      {13, 0x9003}, {33, 0x900C}, {35, 0x910C}, {36, 0x900D}, {34, 0x910D}, {28, 0x9009}, {22, 0x9109},
      {38, 0x9009}, {32, 0x9009}, {4, 0x9003},  {10, 0x9003}, {23, 0x9009}, {29, 0x9009}};
  return t;
}
const std::map<int64_t, int64_t> kOwnerRole = {{6, 0x9108}, {25, 0x910E}, {40, 0x9100}};
const std::map<int64_t, int64_t> kWeightRole = {{21, 0x9000}, {1, 0x9000}, {8, 0x9000}, {27, 0x9000}};
const std::map<int64_t, int64_t> kNeckOwnerWeights = {{21, 0x9101}, {1, 0x9101}, {8, 0x9101}, {27, 0x9101}};
constexpr size_t kMaxPalette = 3;
// CHANNEL_ALIAS, in sorted(items()) order.
const std::vector<std::pair<int64_t, std::vector<int64_t>>> kChannelAlias = {
    {0x9004, {0x9120}}, {0x900A, {0x9121}}, {0x9103, {0x9003}}, {0x9109, {0x9009}}};

struct Landmark {
  int64_t body;
  int64_t code;
};
const std::map<std::string, Landmark> kLm = {
    {"hipR", {14, 0x9006}}, {"hipL", {33, 0x900C}}, {"kneeR", {17, 0x9007}}, {"kneeL", {36, 0x900D}},
    {"ankR", {9, 0x9003}},  {"ankL", {28, 0x9009}}, {"shR", {18, 0x9008}},   {"shL", {37, 0x900E}},
    {"elR", {11, 0x9004}},  {"elL", {30, 0x900A}},  {"wrR", {12, 0x9005}},   {"wrL", {31, 0x900B}}};
struct Rule {
  char kind;  // 's' seg, 'm' move
  const char* n0;
  const char* n1;
};
const std::map<int64_t, Rule> kRule = {
    {18, {'s', "shR", "elR"}},   {5, {'s', "shR", "elR"}},    {11, {'s', "elR", "wrR"}},   {7, {'s', "elR", "wrR"}},
    {12, {'m', "wrR", nullptr}}, {20, {'m', "wrR", nullptr}}, {37, {'s', "shL", "elL"}},   {24, {'s', "shL", "elL"}},
    {30, {'s', "elL", "wrL"}},   {26, {'s', "elL", "wrL"}},   {31, {'m', "wrL", nullptr}}, {39, {'m', "wrL", nullptr}},
    {14, {'s', "hipR", "kneeR"}}, {16, {'s', "hipR", "kneeR"}}, {17, {'s', "kneeR", "ankR"}},
    {15, {'s', "kneeR", "ankR"}}, {9, {'m', "ankR", nullptr}},  {3, {'m', "ankR", nullptr}},
    {19, {'m', "ankR", nullptr}}, {13, {'m', "ankR", nullptr}}, {4, {'m', "ankR", nullptr}},
    {10, {'m', "ankR", nullptr}}, {33, {'s', "hipL", "kneeL"}}, {35, {'s', "hipL", "kneeL"}},
    {36, {'s', "kneeL", "ankL"}}, {34, {'s', "kneeL", "ankL"}}, {28, {'m', "ankL", nullptr}},
    {22, {'m', "ankL", nullptr}}, {38, {'m', "ankL", nullptr}}, {32, {'m', "ankL", nullptr}},
    {23, {'m', "ankL", nullptr}}, {29, {'m', "ankL", nullptr}}};

std::string Str(int64_t v) { return std::to_string(v); }

template <class M>
int64_t MapAt(const M& m, int64_t k) {
  auto it = m.find(k);
  if (it == m.end()) Fail("KeyError: " + Str(k));
  return it->second;
}
int64_t RoleAt(int64_t k) { return MapAt(Role(), k); }
// d.get(k, default) where the default was evaluated first (ROLE[k] raises before the get).
int64_t GetOr(const std::map<int64_t, int64_t>& m, int64_t k, int64_t dflt) {
  auto it = m.find(k);
  return it == m.end() ? dflt : it->second;
}

template <class T>
const T& Idx(const std::vector<T>& v, int64_t i) {
  if (i < 0) i += static_cast<int64_t>(v.size());
  if (i < 0 || i >= static_cast<int64_t>(v.size())) Fail("IndexError: list index out of range");
  return v[static_cast<size_t>(i)];
}

// dict(cat_dir(c)): the last value of each type wins.
std::map<int64_t, uint32_t> CatDict(std::span<const uint8_t> c) {
  std::map<int64_t, uint32_t> d;
  for (const auto& [t, v] : tpr::CatDir(c)) d[t] = v;
  return d;
}

// sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5
double Dist(const Vec3& a, const Vec3& b) {
  num::PySum s;
  for (int c = 0; c < 3; ++c) {
    const double d = a[c] - b[c];
    s.Add(d * d);
  }
  return std::sqrt(s.Result());
}

std::span<const uint8_t> Sp(const Bytes& b) { return std::span<const uint8_t>(b.data(), b.size()); }

const tpr::DtEntry& Dt(uint32_t typ) {
  const tpr::DtEntry* e = tpr::DtFind(typ);
  if (!e) Fail("KeyError: " + Str(typ));
  return *e;
}
uint32_t DtByName(std::string_view name) {
  auto v = tpr::DtByName(name);
  if (!v) Fail("KeyError: " + std::string(name));
  return *v;
}

void PutU32(Bytes& b, uint32_t v) {
  b.push_back(uint8_t(v >> 24));
  b.push_back(uint8_t(v >> 16));
  b.push_back(uint8_t(v >> 8));
  b.push_back(uint8_t(v));
}
void PutF32(Bytes& b, double x) { PutU32(b, std::bit_cast<uint32_t>(num::PackF32(x))); }
void PutF16(Bytes& b, double x) {
  const uint16_t h = num::PackF16(x);
  b.push_back(uint8_t(h >> 8));
  b.push_back(uint8_t(h));
}
Bytes Pack4I(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  be::Writer w;
  w.U32(a);
  w.U32(b);
  w.U32(c);
  w.U32(d);
  return w.Take();
}
Bytes Pack8I(const std::array<uint64_t, 8>& v) {
  be::Writer w;
  for (uint64_t x : v) w.U32(x);
  return w.Take();
}

// struct.unpack(f'>{n}H', b): the length must be exactly 2n.
std::vector<uint16_t> UnpackH(const Bytes& b, uint64_t n) {
  if (b.size() != 2 * n) Fail("struct.error: unpack requires a buffer of " + Str(int64_t(2 * n)) + " bytes");
  std::vector<uint16_t> out(n);
  for (uint64_t i = 0; i < n; ++i) out[i] = uint16_t(b[2 * i] << 8 | b[2 * i + 1]);
  return out;
}

// idx[st:st + cnt] (a Python slice clamps)
std::vector<uint16_t> SliceIdx(const std::vector<uint16_t>& idx, uint64_t st, uint64_t cnt) {
  const uint64_t n = idx.size();
  const uint64_t a = std::min(st, n), b = std::min(st + cnt, n);
  if (b <= a) return {};
  return std::vector<uint16_t>(idx.begin() + a, idx.begin() + b);
}

bool IsFloatType(std::string_view nm) {
  return nm == "F1" || nm == "F2" || nm == "F3" || nm == "F4" || nm == "HEND3N" || nm == "H2" || nm == "H4";
}

}  // namespace

// ------------------------------------------------------------------ Model (line 98)
int64_t Model::ParentId(int64_t nid) const {
  const int64_t p = Hie(id2node.At(nid)).parent;
  return p >= 0 ? Hie(p).id : -1;
}
const tpr::Node& Model::Hie(int64_t i) const { return Idx(hie, i); }
const Bytes& Model::Cat() const {
  if (!cat) Fail("TypeError: the model has no .cat");
  return *cat;
}

Model LoadModel(std::string name, std::span<const uint8_t> tpr_bytes, std::shared_ptr<const Bytes> cat) {
  Model m;
  m.name = std::move(name);
  m.x = tpr::Xpr::Parse(tpr_bytes);
  static constexpr tpr::Fourcc kUser = tpr::MakeFourcc("USER"), kTx = tpr::MakeFourcc("TX2D"),
                               kVb = tpr::MakeFourcc("VBUF"), kIb = tpr::MakeFourcc("IBUF");
  const auto u = m.x.OfType(kUser);
  m.geo = tpr::ParseMdlGeo(Sp(m.x.res[Idx(u, 1)].blob));
  m.info = tpr::ParseMdlInfo(Sp(m.x.res[Idx(u, 2)].blob));
  m.hie = tpr::ParseHie(Sp(m.x.res[Idx(u, 3)].blob));
  for (size_t i : m.x.OfType(kTx)) m.tex.push_back(m.x.res[i].blob);
  for (size_t i : m.x.OfType(kVb)) m.vbs.push_back(m.x.res[i].blob);
  for (size_t i : m.x.OfType(kIb)) m.ibs.push_back(m.x.res[i].blob);
  const auto W = tpr::WorldMats(m.hie);
  for (size_t i = 0; i < m.hie.size(); ++i) m.id2node.Set(m.hie[i].id, static_cast<int64_t>(i));
  for (size_t i = 0; i < m.hie.size(); ++i) m.joint.Set(m.hie[i].id, Vec3{W[i][12], W[i][13], W[i][14]});
  // (lines 113-117 only print a note about rotated binds: no output)
  m.cat = std::move(cat);
  return m;
}

// ------------------------------------------------------------------ main_chain (line 164)
MainChain MainChainOf(const Model& ref) {
  const Bytes& c = ref.Cat();
  const auto dir = CatDict(c);
  auto it = dir.find(2);
  if (it == dir.end()) Fail("KeyError: 2");
  const auto groups = tpr::ParseT2(c, it->second).first;
  std::set<int64_t> main_ids;
  for (const auto& [code, id] : Idx(groups, 0).items) main_ids.insert(id);
  std::map<int64_t, std::set<int64_t>> subtree;
  for (size_t k = 0; k < ref.hie.size(); ++k) {
    if (ref.hie[k].parent >= 0) continue;
    std::set<int64_t> ids;
    std::vector<int64_t> stack{static_cast<int64_t>(k)};
    size_t guard = 0;
    while (!stack.empty()) {
      const int64_t j = stack.back();
      stack.pop_back();
      if (++guard > 1000000) Fail("main_chain: the node tree has a cycle");
      ids.insert(ref.Hie(j).id);
      for (int64_t ch : ref.Hie(j).children) stack.push_back(ch);
    }
    subtree[ref.hie[k].id] = ids;
  }
  std::set<int64_t> keep;
  for (const auto& [r, ids] : subtree) {
    bool hit = false;
    for (int64_t i : ids)
      if (main_ids.count(i)) hit = true;
    if (hit) keep.insert(ids.begin(), ids.end());
  }
  if (keep.empty()) Fail("ValueError: max() arg is an empty sequence");
  MainChain mc;
  mc.main_max = *keep.rbegin();
  for (int64_t i = 0; i <= mc.main_max; ++i)
    if (!keep.count(i)) mc.orphans.push_back(i);
  return mc;
}

// ------------------------------------------------------------------ build_skeleton (line 193)
std::vector<tpr::Node> BuildSkeleton(const Model& ref, int64_t main_max, const std::vector<int64_t>& orphans,
                                     int64_t lower_root) {
  std::vector<int64_t> keep;
  for (size_t i = 0; i < ref.hie.size(); ++i)
    if (ref.hie[i].id <= main_max) keep.push_back(static_cast<int64_t>(i));
  std::unordered_map<int64_t, int64_t> newidx;
  for (size_t k = 0; k < keep.size(); ++k) newidx[keep[k]] = static_cast<int64_t>(k);
  auto NewIdx = [&](int64_t old) {
    auto it = newidx.find(old);
    if (it == newidx.end()) Fail("KeyError: " + Str(old));
    return it->second;
  };
  std::vector<tpr::Node> nodes;
  const auto W = tpr::WorldMats(ref.hie);
  const std::set<int64_t> orphan_set(orphans.begin(), orphans.end());
  for (int64_t old : keep) {
    const tpr::Node& nd = ref.hie[static_cast<size_t>(old)];
    bool use_world = false;
    int64_t par;
    if (orphan_set.count(nd.id)) {
      const auto& lr = Idx(W, ref.id2node.At(lower_root));
      double mx = 0.0;
      for (int k = 12; k < 15; ++k) mx = std::max(mx, std::fabs(lr[k]));
      Assert(mx < 1e-6, "('lower root not at the origin', " + Str(lower_root) + ")");
      const int64_t p = nd.parent;
      bool far = p < 0;
      if (!far) {
        const auto& wp = Idx(W, p);
        double m2 = std::fabs(wp[12]);
        for (int k = 13; k < 15; ++k)
          if (std::fabs(wp[k]) > m2) m2 = std::fabs(wp[k]);
        far = m2 >= 1e-6;
      }
      if (far) {
        bool ident = true;
        for (int k = 0; k < 16; ++k)
          if (!(std::fabs(lr[k] - ((k == 0 || k == 5 || k == 10 || k == 15) ? 1.0 : 0.0)) < 1e-6)) ident = false;
        Assert(ident, "('lower root bind is not the identity', " + Str(lower_root) + ")");
        use_world = true;
      }
      par = NewIdx(ref.id2node.At(lower_root));
    } else {
      par = nd.parent >= 0 ? NewIdx(nd.parent) : -1;
    }
    std::vector<int64_t> ch;
    for (int64_t c : nd.children)
      if (newidx.count(c) && !orphan_set.count(ref.Hie(c).id)) ch.push_back(newidx[c]);
    if (!orphans.empty() && nd.id == lower_root)
      for (int64_t o : orphans) ch.push_back(NewIdx(ref.id2node.At(o)));
    if (use_world) {
      const auto& wm = W[static_cast<size_t>(old)];
      nodes.push_back(tpr::Node::FromFloats(wm, nd.id, par, ch));
    } else if (nd.raw) {
      nodes.push_back(tpr::Node::FromBytes(nd.mat_bytes, nd.id, par, ch));
    } else {
      nodes.push_back(tpr::Node::FromFloats(nd.mat_floats, nd.id, par, ch));
    }
  }
  std::vector<int64_t> ids;
  for (const auto& nd : nodes) ids.push_back(nd.id);
  std::sort(ids.begin(), ids.end());
  bool ok = static_cast<int64_t>(ids.size()) == main_max + 1;
  for (size_t i = 0; ok && i < ids.size(); ++i) ok = ids[i] == static_cast<int64_t>(i);
  Assert(ok, "ids == list(range(main_max + 1))");
  return nodes;
}

// ------------------------------------------------------------------ channel_map (line 235)
ChannelMap ChannelMapOf(const Model& ref) {
  const Bytes& c = ref.Cat();
  const auto dir = CatDict(c);
  auto it = dir.find(2);
  if (it == dir.end()) Fail("KeyError: 2");
  const auto groups = tpr::ParseT2(c, it->second).first;
  ChannelMap cm;
  cm.main = Idx(groups, 0).items;
  std::unordered_map<int64_t, int64_t> depth;
  for (size_t k = 0; k < ref.hie.size(); ++k) {
    int64_t d = 0, j = static_cast<int64_t>(k);
    while (ref.Hie(j).parent >= 0) {
      j = ref.Hie(j).parent;
      if (++d > 100000) Fail("channel_map: the node tree has a cycle");
    }
    depth[ref.hie[k].id] = d;
  }
  auto Depth = [&](int64_t nid) {
    auto f = depth.find(nid);
    if (f == depth.end()) Fail("KeyError: " + Str(nid));
    return f->second;
  };
  for (const auto& [code, nid] : cm.main) {
    const int64_t* cur = cm.out.Find(code);
    if (!cur || std::make_pair(Depth(nid), int64_t(nid)) < std::make_pair(Depth(*cur), *cur)) cm.out.Set(code, nid);
  }
  for (const auto& [code, alts] : kChannelAlias) {
    if (cm.out.Contains(code)) continue;
    for (int64_t alt : alts) {
      if (const int64_t* v = cm.out.Find(alt)) {
        cm.out.Set(code, *v);
        break;
      }
    }
  }
  return cm;
}

// ------------------------------------------------------------------ _sig (line 262)
namespace {
struct Sig {
  Vec3 joint;
  std::optional<Vec3> parent;
  std::vector<Vec3> desc;  // sorted
};
Sig SigOf(const Model& model, int64_t nid, const std::set<int64_t>& skip) {
  Sig s;
  const int64_t p = model.ParentId(nid);
  std::vector<int64_t> stack;
  for (int64_t c : model.Hie(model.id2node.At(nid)).children)
    if (!skip.count(model.Hie(c).id)) stack.push_back(c);
  size_t guard = 0;
  while (!stack.empty()) {
    const int64_t c = stack.back();
    stack.pop_back();
    if (++guard > 1000000) Fail("_sig: the node tree has a cycle");
    const Vec3& j = model.joint.At(model.Hie(c).id);
    s.desc.push_back(Vec3{num::PyRoundDigits(j[0], 4), num::PyRoundDigits(j[1], 4), num::PyRoundDigits(j[2], 4)});
    for (int64_t k : model.Hie(c).children)
      if (!skip.count(model.Hie(k).id)) stack.push_back(k);
  }
  std::stable_sort(s.desc.begin(), s.desc.end());
  s.joint = model.joint.At(nid);
  if (p >= 0) s.parent = model.joint.At(p);
  return s;
}

// d += 0.0 if (spj is None and bpj is None) else (9.0 if (spj is None) != (bpj is None) else dist(spj, bpj))
double ParentTerm(const std::optional<Vec3>& a, const std::optional<Vec3>& b) {
  if (!a && !b) return 0.0;
  if (a.has_value() != b.has_value()) return 9.0;
  return Dist(*a, *b);
}
}  // namespace

// ------------------------------------------------------------------ select_copy (line 278)
std::optional<std::set<int64_t>> SelectCopy(const Model& suit, const Model& body) {
  std::vector<std::pair<int64_t, std::vector<int64_t>>> roots;
  for (size_t i = 0; i < suit.hie.size(); ++i) {
    if (suit.hie[i].parent >= 0) continue;
    std::vector<int64_t> ids, stack{static_cast<int64_t>(i)};
    size_t guard = 0;
    while (!stack.empty()) {
      const int64_t k = stack.back();
      stack.pop_back();
      if (++guard > 1000000) Fail("select_copy: the node tree has a cycle");
      ids.push_back(suit.Hie(k).id);
      for (int64_t c : suit.Hie(k).children) stack.push_back(c);
    }
    std::sort(ids.begin(), ids.end());
    roots.emplace_back(suit.hie[i].id, ids);
  }
  std::vector<std::pair<int64_t, std::vector<int64_t>>> big;
  for (const auto& r : roots)
    if (r.second.size() >= 10) big.push_back(r);
  if (big.size() <= 1) return std::nullopt;
  struct Scored {
    double s;
    int64_t r;
    std::vector<int64_t> ids;
    bool operator<(const Scored& o) const { return std::tie(s, r, ids) < std::tie(o.s, o.r, o.ids); }
  };
  std::vector<Scored> scored;
  for (const auto& [r, ids] : big) {
    num::PySum total;
    for (int64_t i : ids) {
      const Vec3& sj = suit.joint.At(i);
      bool first = true;
      double best = 0.0;
      for (const auto& [j, bj] : body.joint.items()) {
        const double d = Dist(sj, bj);
        if (first || d < best) best = d;
        first = false;
      }
      if (first) Fail("ValueError: min() arg is an empty sequence");
      total.Add(best);
    }
    scored.push_back({num::PyRoundDigits(PyDiv(total.Result(), double(ids.size())), 6), r, ids});
  }
  std::vector<Scored> sorted = scored;
  std::stable_sort(sorted.begin(), sorted.end());
  const Scored& best = sorted[0];
  Assert(best.s < 0.005, "('no suit copy fits the body', ...)");
  const Scored& second = sorted[1];
  if (best.s == 0.0 && second.s > 0.0) {
    // (report only)
  } else {
    Assert(second.s > 0.01, "('two suit copies fit the body', ...)");
  }
  return std::set<int64_t>(best.ids.begin(), best.ids.end());
}

// ------------------------------------------------------------------ accessory_ids (line 317)
std::vector<int64_t> AccessoryIds(const Model& suit, const Model& body, const std::optional<std::set<int64_t>>& only,
                                  double tol) {
  std::vector<int64_t> sids;
  for (const auto& [sid, j] : suit.joint.items()) sids.push_back(sid);
  std::sort(sids.begin(), sids.end());
  std::vector<int64_t> out;
  for (int64_t sid : sids) {
    if (only && !only->count(sid)) continue;
    const int64_t sp = suit.ParentId(sid);
    std::optional<Vec3> spj;
    if (sp >= 0) spj = suit.joint.At(sp);
    double best = 9e9;
    for (const auto& [bid, bj] : body.joint.items()) {
      if (DropObjectsBody().count(bid)) continue;
      const int64_t bp = body.ParentId(bid);
      std::optional<Vec3> bpj;
      if (bp >= 0) bpj = body.joint.At(bp);
      double d = Dist(suit.joint.At(sid), bj);
      d += ParentTerm(spj, bpj);
      if (d < best) best = d;
    }
    if (best >= tol) out.push_back(sid);
  }
  return out;
}

// ------------------------------------------------------------------ suit_to_body_ids (line 345)
PyDict<int64_t, int64_t> SuitToBodyIds(const Model& suit, const Model& body,
                                       const std::optional<std::set<int64_t>>& only, const std::set<int64_t>& skip) {
  PyDict<int64_t, int64_t> out;
  // _sig(body, bid) does not depend on the suit id: computed once per body id, at the first suit id that needs it
  // (same values, and no call at all when no suit id gets that far, as in the Python).
  std::vector<std::pair<int64_t, Sig>> bsigs;
  bool have_bsigs = false;
  for (const auto& [sid, sjv] : suit.joint.items()) {
    if (only && !only->count(sid)) continue;
    if (skip.count(sid)) continue;
    const Sig ss = SigOf(suit, sid, skip);
    if (!have_bsigs) {
      for (const auto& [bid, bj] : body.joint.items()) {
        if (DropObjectsBody().count(bid)) continue;
        bsigs.emplace_back(bid, SigOf(body, bid, {}));
      }
      have_bsigs = true;
    }
    std::vector<std::pair<double, int64_t>> cands;
    for (const auto& [bid, bs] : bsigs) {
      double d = Dist(ss.joint, bs.joint);
      d += ParentTerm(ss.parent, bs.parent);
      num::PySum sum;
      for (const Vec3& a : ss.desc) {
        double m = 9.0;
        bool first = true;
        for (const Vec3& b : bs.desc) {
          const double x = Dist(a, b);
          if (first || x < m) m = x;
          first = false;
        }
        sum.Add(m);
      }
      d += sum.Result();
      const int64_t dl = static_cast<int64_t>(bs.desc.size()) - static_cast<int64_t>(ss.desc.size());
      d += 0.001 * static_cast<double>(dl < 0 ? -dl : dl);
      cands.emplace_back(num::PyRoundDigits(d, 6), bid);
    }
    std::stable_sort(cands.begin(), cands.end());
    if (cands.empty()) Fail("IndexError: list index out of range");
    Assert(cands[0].first < 0.05, "('suit id without a body match', " + Str(sid) + ")");
    Assert(cands.size() < 2 || cands[1].first > cands[0].first, "('ambiguous suit id', " + Str(sid) + ")");
    out.Set(sid, cands[0].second);
  }
  std::set<int64_t> vals;
  for (const auto& [s, b] : out.items()) vals.insert(b);
  Assert(vals.size() == out.size(), "('two suit ids matched one body id', ...)");
  return out;
}

// ------------------------------------------------------------------ fit_maps (line 375)
Vec3 RetargetMap::operator()(const Vec3& p) const {
  switch (kind) {
    case kTorso: {
      const TorsoFit& f = *torso;
      const double t = PyDiv(p[1] - f.yh2, f.ys2 - f.yh2);
      return Vec3{p[0] * (f.xh + (f.xs - f.xh) * t), f.yh4 + f.a * (p[1] - f.yh2),
                  p[2] + (f.zh4 - f.zh2) + ((f.zs4 - f.zs2) - (f.zh4 - f.zh2)) * t};
    }
    case kSeg: {
      Vec3 out;
      for (int c = 0; c < 3; ++c) out[c] = q0[c] + (p[c] - p0[c]) * (c == ax ? s : 1.0);
      if (taper) {
        const double r = PyDiv(std::fabs(p[0]), std::fabs(p0[0]));
        const double k = r < 1.0 ? r : 1.0;  // min(1.0, r): 1.0 unless r < 1.0
        out[0] = p[0] + (q0[0] - p0[0]) * k;
      }
      return out;
    }
    case kMove:
      return Vec3{p[0] + q0[0] - p0[0], p[1] + q0[1] - p0[1], p[2] + q0[2] - p0[2]};
  }
  return p;
}

Maps FitMaps(const Model& body, const Model& ref, const PyDict<int64_t, int64_t>& chan2id, bool leg_midline) {
  const auto& j2 = body.joint;
  PyDict<int64_t, Vec3> j4;
  for (const auto& [code, id] : chan2id.items()) j4.Set(code, ref.joint.At(id));
  auto lm = [&](const std::string& name) {
    auto it = kLm.find(name);
    if (it == kLm.end()) Fail("KeyError: " + name);
    return std::make_pair(j2.At(it->second.body), j4.At(it->second.code));
  };
  Maps m;
  m.torso = std::make_unique<TorsoFit>();
  TorsoFit& f = *m.torso;
  const auto [hR2, hR4] = lm("hipR");
  const auto [hL2, hL4] = lm("hipL");
  const auto [sR2, sR4] = lm("shR");
  const auto [sL2, sL4] = lm("shL");
  f.yh2 = (hR2[1] + hL2[1]) / 2;
  f.yh4 = (hR4[1] + hL4[1]) / 2;
  f.ys2 = (sR2[1] + sL2[1]) / 2;
  f.ys4 = (sR4[1] + sL4[1]) / 2;
  f.zh2 = (hR2[2] + hL2[2]) / 2;
  f.zh4 = (hR4[2] + hL4[2]) / 2;
  f.zs2 = (sR2[2] + sL2[2]) / 2;
  f.zs4 = (sR4[2] + sL4[2]) / 2;
  f.xh = PyDiv(hR4[0] - hL4[0], hR2[0] - hL2[0]);
  f.xs = PyDiv(sR4[0] - sL4[0], sR2[0] - sL2[0]);
  f.a = PyDiv(f.ys4 - f.yh4, f.ys2 - f.yh2);
  auto seg = [&](const std::string& n0, const std::string& n1) {
    const auto [p0, q0] = lm(n0);
    const auto [p1, q1] = lm(n1);
    int ax = 0;
    double best = std::fabs(p1[0] - p0[0]);
    for (int c = 1; c < 3; ++c) {
      const double v = std::fabs(p1[c] - p0[c]);
      if (v > best) {
        best = v;
        ax = c;
      }
    }
    RetargetMap r;
    r.kind = RetargetMap::kSeg;
    r.p0 = p0;
    r.q0 = q0;
    r.ax = ax;
    r.s = PyDiv(q1[ax] - q0[ax], p1[ax] - p0[ax]);
    r.taper = leg_midline && (n0 == "hipR" || n0 == "hipL");
    return r;
  };
  auto move = [&](const std::string& n) {
    const auto [p0, q0] = lm(n);
    RetargetMap r;
    r.kind = RetargetMap::kMove;
    r.p0 = p0;
    r.q0 = q0;
    return r;
  };
  for (const auto& [bid, bj] : body.joint.items()) {
    auto it = kRule.find(bid);
    RetargetMap r;
    if (it == kRule.end()) {
      r.kind = RetargetMap::kTorso;
      r.torso = m.torso.get();
    } else if (it->second.kind == 's') {
      r = seg(it->second.n0, it->second.n1);
    } else {
      r = move(it->second.n0);
    }
    m.maps.Set(bid, r);
  }
  m.info.torso_y_scale = f.a;
  m.info.torso_x_scale_hips = f.xh;
  m.info.torso_x_scale_shoulders = f.xs;
  m.info.hips_y = {f.yh2, f.yh4};
  m.info.shoulders_y = {f.ys2, f.ys4};
  return m;
}

// ------------------------------------------------------------------ parts
namespace {

// source_pass (line 486): the mesh is model.geo[oid].meshes[mesh_index] (Python finds it by identity).
PassRec SourcePass(const Model& model, int64_t oid, size_t mesh_index) {
  const tpr::ObjInfo& inf = Idx(model.info, oid);
  const Bytes& recb = Idx(inf.infos, static_cast<int64_t>(mesh_index));
  const auto rec = Reader(recb).U32s(0, 8);
  Assert(rec[0] == mesh_index, "('ObjInfo record names another mesh', " + Str(oid) + ")");
  Assert((rec[2] == 2 || rec[2] == 4 || rec[2] == 8) && (rec[4] == 0 || rec[4] == 1),
         "('unknown draw pass', " + Str(oid) + ")");
  return PassRec{static_cast<int64_t>(mesh_index), rec[2], rec[4]};
}

// The vertex-format element fields as convert.py names them: e[0] stream, e[1] offset, e[2] type, e[4] usage, e[5]
// usage index.
int StreamOf(const tpr::DeclElem& e) { return e.a; }

// decode_part (line 546). meshes: indices into model.geo[obj_id].meshes.
Part* DecodePart(const Context& ctx, PartArena& arena, const Model& model, int64_t obj_id, const tpr::Decl& decl,
                 int64_t decl_index, const std::vector<size_t>& meshes, const IdMap* id_map, int64_t owner_doax2,
                 const Maps& maps, int64_t tex_offset, const PyDict<int64_t, int64_t>& chan2id,
                 const AttachInfo* attach) {
  Part* p = arena.New();
  p->has_srcv = true;
  const auto& pal16 = Idx(model.info, obj_id).palette;
  std::vector<int64_t> pal(pal16.begin(), pal16.end());
  const auto [base, size] = tpr::BufSpan(Idx(model.vbs, decl.Vb()));
  (void)size;
  std::vector<tpr::DeclElem> els;
  for (size_t i = 0; i + 1 < decl.elems.size(); ++i)
    if (StreamOf(decl.elems[i]) == 0) els.push_back(decl.elems[i]);
  std::vector<tpr::DeclElem> pos_e, bw_e, kept;
  for (const auto& e : els) {
    if (e.e == 0 && e.f == 0) pos_e.push_back(e);
    if (e.e == 1) bw_e.push_back(e);
  }
  Assert(pos_e.size() == 1 && Dt(pos_e[0].c).name == "F3", "els");
  for (const auto& e : els)
    if (!(e.e == 0 || e.e == 1 || e.e == 2) && !(e.e == 5 && e.f >= 3)) kept.push_back(e);
  for (const auto& e : kept) p->kept.push_back(KeptElem{e.c, e.e, e.f});
  std::vector<std::pair<size_t, std::string>> expand;  // insertion order (k ascending)
  if (ctx.expand) {
    for (size_t k = 0; k < kept.size(); ++k) {
      const std::string nm(Dt(kept[k].c).name);
      const char* to = nm == "HEND3N" ? "F3" : nm == "H2" ? "F2" : nullptr;
      if (to) {
        expand.emplace_back(k, nm);
        p->kept[k] = KeptElem{DtByName(to), kept[k].e, kept[k].f};
      }
    }
  }
  int64_t owner_b = 0;
  bool neck_part = false;
  if (!attach) {
    owner_b = id_map->At(owner_doax2);
    if (ctx.neck_mode) neck_part = GetOr(kOwnerRole, owner_b, RoleAt(owner_b)) == 0x9101;
    for (int64_t b : pal)
      Assert(id_map->Contains(b) || b == 65535,
             "('palette names a bone outside the body match (an accessory bone inside a skinned object is not "
             "supported)', ...)");
  }
  const Vec3 own2 = model.joint.At(owner_doax2);
  const auto data = Sp(model.x.data);
  const Reader rd(data);
  // raw element sizes: the kept elements' DT sizes, the expanded ones 4 * value count (known after the first decode)
  std::vector<int> raw_lens(kept.size());
  for (size_t k = 0; k < kept.size(); ++k) raw_lens[k] = Dt(kept[k].c).size;
  const uint32_t nverts = decl.head[5];
  const uint32_t stride = decl.Stride();
  p->verts.reserve(nverts);
  bool lens_fixed = false;
  auto AppendRaws = [&](int64_t o) {
    // raws = [bytes(data[o + e[1]: o + e[1] + size]) for e in kept]; expanded: struct.pack(f'>{len(v)}f', *v)
    std::vector<Bytes> raws(kept.size());
    for (size_t k = 0; k < kept.size(); ++k) {
      const uint64_t a = static_cast<uint64_t>(o) + kept[k].b;
      const auto s = rd.Slice(std::min<uint64_t>(a, data.size()),
                              std::min<uint64_t>(a + Dt(kept[k].c).size, data.size()));
      raws[k].assign(s.begin(), s.end());
    }
    for (const auto& [k, nm] : expand) {
      const tpr::ElemValue v = tpr::DecodeElem(data, o + kept[k].b, kept[k].c);
      Bytes r;
      for (int i = 0; i < v.n; ++i) PutF32(r, v.v[i]);
      raws[k] = std::move(r);
    }
    if (!lens_fixed) {
      for (size_t k = 0; k < kept.size(); ++k) raw_lens[k] = static_cast<int>(raws[k].size());
      lens_fixed = true;
    }
    for (size_t k = 0; k < kept.size(); ++k) {
      if (static_cast<int>(raws[k].size()) != raw_lens[k]) Fail("decode_part: raw element sizes differ per vertex");
      p->raws.insert(p->raws.end(), raws[k].begin(), raws[k].end());
    }
  };
  for (uint32_t j = 0; j < nverts; ++j) {
    const int64_t o = static_cast<int64_t>(base) + static_cast<int64_t>(j) * stride;
    const Vec3 lp{rd.F32(o + pos_e[0].b), rd.F32(o + pos_e[0].b + 4), rd.F32(o + pos_e[0].b + 8)};
    Vec3 mp{lp[0] + own2[0], lp[1] + own2[1], lp[2] + own2[2]};
    if (attach) {
      for (int c = 0; c < 3; ++c) mp[c] = mp[c] + attach->shift[c];
      Vec3 q{0.0, 0.0, 0.0};
      for (const auto& [bb, wt] : attach->blend) {
        const Vec3 t = maps.At(bb)(mp);
        for (int c = 0; c < 3; ++c) q[c] += wt * t[c];
      }
      AppendRaws(o);
      p->verts.push_back(Vert{q, attach->w4});
      p->srcv.push_back(SrcV{mp, attach->blend});
      continue;
    }
    Blend bones;
    if (!bw_e.empty() && !ctx.twist_rigid_body.count(owner_b)) {
      const std::string_view bwname = Dt(bw_e[0].c).name;
      if (!IsFloatType(bwname)) Fail("not ported yet: integer blend weights (" + std::string(bwname) + ")");
      const tpr::ElemValue w = tpr::DecodeElem(data, o + bw_e[0].b, bw_e[0].c);
      for (int k = 0; k < w.n; ++k)
        if (static_cast<size_t>(k) < pal.size()) bones.emplace_back(pal[k], w.v[k]);
      num::PySum extra;
      for (int k = static_cast<int>(pal.size()); k < w.n; ++k) extra.Add(w.v[k]);
      if (extra.Result() > 1e-4) bones.emplace_back(0, extra.Result());
    } else {
      bones.emplace_back(owner_doax2, 1.0);
    }
    num::PySum tots;
    for (const auto& [b, wt] : bones) tots.Add(wt);
    double tot = tots.Result();
    if (tot == 0.0) tot = 1.0;  // `or 1.0`
    Vec3 q{0.0, 0.0, 0.0};
    Weights w4;
    SrcV sv;
    sv.pos = mp;
    for (const auto& [b, wt] : bones) sv.blend.emplace_back(id_map->At(b), wt / tot);
    p->srcv.push_back(std::move(sv));
    for (const auto& [b, wt0] : bones) {
      const double wt = wt0 / tot;
      const int64_t bb = id_map->At(b);
      const Vec3 t = maps.At(bb)(mp);
      for (int c = 0; c < 3; ++c) q[c] += wt * t[c];
      int64_t d4;
      if (neck_part && kNeckOwnerWeights.count(bb)) {
        d4 = chan2id.At(kNeckOwnerWeights.at(bb));
      } else {
        d4 = chan2id.At(GetOr(kWeightRole, bb, RoleAt(bb)));
      }
      w4.Add(d4, wt);
    }
    AppendRaws(o);
    p->verts.push_back(Vert{q, std::move(w4)});
  }
  p->raw_lens = raw_lens;
  p->raw_stride = 0;
  for (int l : raw_lens) p->raw_stride += l;
  const auto [ib_base, ib_size] = tpr::BufSpan(Idx(model.ibs, decl.head[2]));
  (void)ib_size;
  p->icount = decl.head[3];
  {
    const uint64_t a = std::min<uint64_t>(ib_base, data.size());
    const uint64_t b = std::min<uint64_t>(uint64_t(ib_base) + 2ull * p->icount, data.size());
    const auto s = rd.Slice(a, std::max(a, b));
    p->ib.assign(s.begin(), s.end());
  }
  const tpr::ObjGeo& g = Idx(model.geo, obj_id);
  for (size_t mi : meshes) {
    const Bytes& m = g.meshes[mi];
    const auto w = tpr::MeshWords(m);
    const uint32_t nst = Idx(w, 6);
    Bytes mm = m;
    if (nst == 4) {
      const auto stages = tpr::MeshTextures(m);
      Assert(Idx(stages, 3).kind == 7, "stages");
      if (mm.size() < 0x40) Fail("ValueError: mesh record shorter than its stages");
      mm.resize(mm.size() - 0x40);
      be::PackIntoU32(mm, 0x18, 3);
      Assert(Idx(w, 12) == 0x3B, "hex(w[12])");
      be::PackIntoU32(mm, 0x30, 0x34);
    }
    p->meshes.push_back(tpr::MeshPatch(mm, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                       [&](uint32_t t) { return uint64_t(t) + uint64_t(tex_offset); }));
    p->passes.push_back(SourcePass(model, obj_id, mi));
  }
  p->owner = attach ? attach->owner : chan2id.At(GetOr(kOwnerRole, owner_b, RoleAt(owner_b)));
  p->has_model = true;
  p->model_name = model.name;
  p->obj_id = obj_id;
  p->decl_index = decl_index;
  if (attach) p->attach = *attach;
  return p;
}

// by_decl: mesh indices of g grouped by u32(m, 8), in mesh order.
std::map<int64_t, std::vector<size_t>> MeshesByDecl(const tpr::ObjGeo& g) {
  std::map<int64_t, std::vector<size_t>> by;
  for (size_t i = 0; i < g.meshes.size(); ++i) by[U32(g.meshes[i], 8)].push_back(i);
  return by;
}

}  // namespace

// ------------------------------------------------------------------ carry_ref_decl (line 497)
std::pair<Part*, std::vector<uint32_t>> CarryRefDecl(PartArena& arena, const Model& ref, int64_t oid, int64_t k,
                                                     int64_t tex_offset) {
  const tpr::ObjGeo& g = Idx(ref.geo, oid);
  const tpr::Decl& dc = Idx(g.decls, k);
  Part* p = arena.New();
  p->obj_id = oid;
  p->decl_index = k;
  const auto [base, sz] = tpr::BufSpan(Idx(ref.vbs, dc.Vb()));
  (void)sz;
  const auto data = Sp(ref.x.data);
  const uint64_t vb_end = std::min<uint64_t>(uint64_t(base) + uint64_t(dc.Stride()) * dc.head[5], data.size());
  const uint64_t vb_beg = std::min<uint64_t>(base, data.size());
  Bytes vb(data.begin() + vb_beg, data.begin() + std::max(vb_beg, vb_end));
  const auto& pal16 = Idx(ref.info, oid).palette;
  std::vector<int64_t> src_pal(pal16.begin(), pal16.end());
  const Vec3 own = ref.joint.At(oid);
  std::vector<tpr::DeclElem> els;
  for (size_t i = 0; i + 1 < dc.elems.size(); ++i)
    if (StreamOf(dc.elems[i]) == 0) els.push_back(dc.elems[i]);
  std::vector<tpr::DeclElem> pe, bw;
  for (const auto& e : els) {
    if (e.e == 0 && e.f == 0) pe.push_back(e);
    if (e.e == 1) bw.push_back(e);
  }
  if (pe.empty()) Fail("IndexError: list index out of range");
  const Reader rv(vb);
  for (uint32_t j = 0; j < dc.head[5]; ++j) {
    const int64_t o = static_cast<int64_t>(j) * dc.Stride();
    const Vec3 lp{rv.F32(o + pe[0].b), rv.F32(o + pe[0].b + 4), rv.F32(o + pe[0].b + 8)};
    Weights w4;
    if (!bw.empty()) {
      const std::string_view bwname = Dt(bw[0].c).name;
      if (!IsFloatType(bwname)) Fail("not ported yet: integer blend weights (" + std::string(bwname) + ")");
      const tpr::ElemValue w = tpr::DecodeElem(vb, o + bw[0].b, bw[0].c);
      for (int kk = 0; kk < w.n; ++kk) {
        const int64_t b = static_cast<size_t>(kk) < src_pal.size() ? src_pal[kk] : 0;
        w4.Add(b, w.v[kk]);
      }
    } else {
      w4.Put(oid, 1.0);
    }
    p->verts.push_back(Vert{Vec3{lp[0] + own[0], lp[1] + own[1], lp[2] + own[2]}, std::move(w4)});
  }
  p->native = NativeDecl{dc.elems, dc.Stride(), vb, src_pal};
  const auto [ib_base, ib_sz] = tpr::BufSpan(Idx(ref.ibs, dc.head[2]));
  (void)ib_sz;
  p->icount = dc.head[3];
  {
    const uint64_t a = std::min<uint64_t>(ib_base, data.size());
    const uint64_t b = std::min<uint64_t>(uint64_t(ib_base) + 2ull * p->icount, data.size());
    p->ib.assign(data.begin() + a, data.begin() + std::max(a, b));
  }
  std::vector<uint32_t> used;
  for (const Bytes& m : g.meshes) {
    if (U32(m, 8) != k) continue;
    for (const auto& st : tpr::MeshTextures(m))
      if (st.texture != 0xFFFFFFFFu && std::find(used.begin(), used.end(), st.texture) == used.end())
        used.push_back(st.texture);
  }
  std::sort(used.begin(), used.end());
  std::map<uint32_t, uint64_t> remap;
  for (size_t i = 0; i < used.size(); ++i) remap[used[i]] = uint64_t(tex_offset) + i;
  for (size_t mi = 0; mi < g.meshes.size(); ++mi) {
    const Bytes& m = g.meshes[mi];
    if (U32(m, 8) != k) continue;
    p->meshes.push_back(tpr::MeshPatch(m, std::nullopt, std::nullopt, std::nullopt, std::nullopt, [&](uint32_t t) {
      auto it = remap.find(t);
      if (it == remap.end()) Fail("KeyError: " + Str(t));
      return it->second;
    }));
    p->passes.push_back(SourcePass(ref, oid, mi));
  }
  p->owner = oid;
  return {p, used};
}

// ------------------------------------------------------------------ collect_parts (line 652)
std::vector<Part*> CollectParts(const Context& ctx, PartArena& arena, const Model& model, const IdMap& id_map,
                                const Maps& maps, int64_t tex_offset, const PyDict<int64_t, int64_t>& chan2id,
                                const std::set<int64_t>& drop_objects, const std::set<int64_t>& skip) {
  std::vector<Part*> parts;
  const size_t n = std::min(model.geo.size(), model.info.size());
  for (size_t oi = 0; oi < n; ++oi) {
    const int64_t oid = static_cast<int64_t>(oi);
    if (skip.count(oid)) continue;
    if (!id_map.Contains(oid)) continue;
    if (drop_objects.count(id_map.At(oid))) continue;
    const tpr::ObjGeo& g = model.geo[oi];
    const auto by_decl = MeshesByDecl(g);
    for (size_t k = 0; k < g.decls.size(); ++k) {
      const tpr::Decl& decl = g.decls[k];
      if (decl.head[5] <= 1) continue;
      auto it = by_decl.find(static_cast<int64_t>(k));
      static const std::vector<size_t> kNone;
      parts.push_back(DecodePart(ctx, arena, model, oid, decl, static_cast<int64_t>(k),
                                 it == by_decl.end() ? kNone : it->second, &id_map, oid, maps, tex_offset, chan2id,
                                 nullptr));
    }
  }
  return parts;
}

// ------------------------------------------------------------------ snap_suit_weights (line 675)
namespace {
struct KeyHash {
  size_t operator()(const std::array<int64_t, 3>& k) const {
    uint64_t h = 1469598103934665603ull;
    for (int64_t v : k) h = (h ^ static_cast<uint64_t>(v)) * 1099511628211ull;
    return static_cast<size_t>(h);
  }
};
std::array<int64_t, 3> CellKey(const Vec3& pos, double cell) {
  std::array<int64_t, 3> k;
  for (int c = 0; c < 3; ++c) {
    const double v = num::PyFloorDiv(pos[c], cell);
    if (!(std::fabs(v) < 9.2e18)) Fail("OverflowError: cannot convert float to integer");
    k[c] = static_cast<int64_t>(v);
  }
  return k;
}
}  // namespace

std::vector<std::pair<Part*, size_t>> SnapSuitWeights(const std::vector<Part*>& body_parts,
                                                      const std::vector<Part*>& suit_parts, double tol,
                                                      const std::set<int64_t>& rigid_owners) {
  const double cell = tol * 2;
  std::unordered_map<std::array<int64_t, 3>, std::vector<std::pair<Vec3, Weights>>, KeyHash> grid;
  for (const Part* p : body_parts) {
    if (p->native) continue;
    for (const Vert& v : p->verts) grid[CellKey(v.pos, cell)].emplace_back(v.pos, v.w4);
  }
  std::vector<std::pair<Part*, size_t>> replaced;
  const double tol2 = tol * tol;
  for (Part* p : suit_parts) {
    if (rigid_owners.count(p->owner)) {
      bool all = true;
      for (const Vert& v : p->verts)
        if (!v.w4.OnlyKey(p->owner)) all = false;
      if (all) continue;  // kept_rigid
    }
    for (size_t k = 0; k < p->verts.size(); ++k) {
      const Vec3 pos = p->verts[k].pos;
      const auto key = CellKey(pos, cell);
      bool have = false;
      double bd = 0.0;
      const Weights* bw = nullptr;
      for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
          for (int dz = -1; dz <= 1; ++dz) {
            auto it = grid.find({key[0] + dx, key[1] + dy, key[2] + dz});
            if (it == grid.end()) continue;
            for (const auto& [q, w] : it->second) {
              num::PySum s;
              for (int c = 0; c < 3; ++c) {
                const double d = q[c] - pos[c];
                s.Add(d * d);
              }
              const double d = s.Result();
              if (d <= tol2 && (!have || d < bd)) {
                have = true;
                bd = d;
                bw = &w;
              }
            }
          }
      if (!have) continue;
      // diff = sum(abs(w4.get(b, 0.0) - bw.get(b, 0.0)) for b in set(w4) | set(bw)). The set's iteration order is
      // CPython's hash order; here the union is taken as w4's keys then bw's other keys (see the report: the sum
      // only meets a 1e-6 threshold).
      const Weights& w4 = p->verts[k].w4;
      num::PySum diff;
      for (const auto& [b, w] : w4.items()) diff.Add(std::fabs(w - bw->Get(b, 0.0)));
      for (const auto& [b, w] : bw->items())
        if (!w4.Contains(b)) diff.Add(std::fabs(0.0 - w));
      if (diff.Result() > 1e-6) {
        p->verts[k].w4 = bw->Above(1e-6);
        replaced.emplace_back(p, k);
      }
    }
  }
  return replaced;
}

// ------------------------------------------------------------------ attach_accessories (line 717)
std::vector<Part*> AttachAccessories(const Context& ctx, PartArena& arena, const Model& suit,
                                     const std::vector<int64_t>& acc, const std::vector<Part*>& parts,
                                     const Maps& maps, int64_t tex_offset, const PyDict<int64_t, int64_t>& chan2id,
                                     double far) {
  if (acc.empty()) return {};
  std::vector<std::pair<const Part*, size_t>> cand;
  for (const Part* p : parts)
    if (!p->native)
      for (size_t i = 0; i < p->srcv.size(); ++i) cand.emplace_back(p, i);
  std::vector<Vec3> C;
  C.reserve(cand.size());
  for (const auto& [p, i] : cand) C.push_back(p->srcv[i].pos);
  const std::set<int64_t> accset(acc.begin(), acc.end());
  std::map<int64_t, std::vector<int64_t>> comp;  // iterated sorted(comp)
  for (int64_t sid : acc) {
    int64_t top = sid;
    size_t guard = 0;
    while (accset.count(suit.ParentId(top))) {
      top = suit.ParentId(top);
      if (++guard > 100000) Fail("attach_accessories: the node tree has a cycle");
    }
    comp[top].push_back(sid);
  }
  std::vector<Part*> out;
  for (auto& [top, ids0] : comp) {
    std::vector<int64_t> ids = ids0;
    std::sort(ids.begin(), ids.end());
    std::vector<int64_t> objs;
    for (int64_t o : ids)
      if (o < static_cast<int64_t>(suit.geo.size())) objs.push_back(o);
    std::vector<Vec3> pts;
    const auto data = Sp(suit.x.data);
    const Reader rd(data);
    for (int64_t o : objs) {
      const Vec3 own = suit.joint.At(o);
      for (const tpr::Decl& dc : suit.geo[static_cast<size_t>(o)].decls) {
        if (dc.head[5] <= 1) continue;
        const auto [base, sz] = tpr::BufSpan(Idx(suit.vbs, dc.Vb()));
        (void)sz;
        const tpr::DeclElem* pe = nullptr;
        for (const auto& e : dc.elems)
          if (e.a == 0 && e.e == 0 && e.f == 0) {
            pe = &e;
            break;
          }
        if (!pe) Fail("IndexError: list index out of range");
        for (uint32_t j = 0; j < dc.head[5]; ++j) {
          const int64_t at = static_cast<int64_t>(base) + static_cast<int64_t>(j) * dc.Stride() + pe->b;
          pts.push_back(Vec3{rd.F32(at) + own[0], rd.F32(at + 4) + own[1], rd.F32(at + 8) + own[2]});
        }
      }
    }
    if (pts.empty()) continue;  // no geometry (locator bones only)
    // best = (np.inf, -1, -1); per chunk of 256 rows: d2 = ((A[s:s+256, None, :] - C[None, :, :]) ** 2).sum(-1);
    // k = argmin(d2) (first minimum in C order; a NaN wins); if d2[r, c] < best[0]: best = (d2[r, c], s + r, c)
    double best_d = std::numeric_limits<double>::infinity();
    int64_t best_a = -1, best_c = -1;
    const size_t nC = C.size();
    if (nC == 0) Fail("ValueError: attempt to get argmin of an empty sequence");
    for (size_t s = 0; s < pts.size(); s += 256) {
      const size_t rows = std::min<size_t>(256, pts.size() - s);
      double mv = 0.0;
      size_t mr = 0, mc = 0;
      bool first = true, nan = false;
      for (size_t r = 0; r < rows && !nan; ++r) {
        const Vec3& a = pts[s + r];
        for (size_t c = 0; c < nC; ++c) {
          const Vec3& b = C[c];
          double sq[3];
          for (int i = 0; i < 3; ++i) {
            const double d = a[i] - b[i];
            sq[i] = d * d;
          }
          const double v = num::NpSumLastAxis(std::span<const double>(sq, 3));
          if (std::isnan(v)) {
            mv = v;
            mr = r;
            mc = c;
            nan = true;
            break;
          }
          if (first || v < mv) {
            mv = v;
            mr = r;
            mc = c;
            first = false;
          }
        }
      }
      if (mv < best_d) {
        best_d = mv;
        best_a = static_cast<int64_t>(s + mr);
        best_c = static_cast<int64_t>(mc);
      }
    }
    if (best_c < 0) Fail("IndexError: list index out of range");  // cand[-1] with no candidate under inf
    const double d = std::sqrt(best_d);
    const auto& [cp, ci] = cand[static_cast<size_t>(best_c)];
    AttachInfo at;
    at.blend = cp->srcv[ci].blend;
    at.w4 = cp->verts[ci].w4.Above(1e-6);
    at.shift = Vec3{0.0, 0.0, 0.0};
    const Vec3 a = pts[static_cast<size_t>(best_a)];
    const int64_t* neck = chan2id.Find(0x9101);
    const bool head_acc = neck && cp->owner == *neck;
    if (d > far && !head_acc) {
      const Vec3& c0 = cp->srcv[ci].pos;
      at.shift = Vec3{c0[0] - a[0], c0[1] - a[1], c0[2] - a[2]};
    }
    at.owner = cp->owner;
    for (int64_t o : objs) {
      const tpr::ObjGeo& g = suit.geo[static_cast<size_t>(o)];
      const auto by_decl = MeshesByDecl(g);
      for (size_t k = 0; k < g.decls.size(); ++k) {
        const tpr::Decl& decl = g.decls[k];
        if (decl.head[5] <= 1) continue;
        auto it = by_decl.find(static_cast<int64_t>(k));
        static const std::vector<size_t> kNone;
        out.push_back(DecodePart(ctx, arena, suit, o, decl, static_cast<int64_t>(k),
                                 it == by_decl.end() ? kNone : it->second, nullptr, o, maps, tex_offset, chan2id, &at));
      }
    }
  }
  return out;
}

// ------------------------------------------------------------------ build_objects (line 813)
BuiltObjects BuildObjects(Context& ctx, const std::vector<Part*>& parts, const std::vector<tpr::Node>& nodes) {
  BuiltObjects bo;
  const auto W = tpr::WorldMats(nodes);
  for (size_t i = 0; i < nodes.size(); ++i) bo.bind4.Set(nodes[i].id, Vec3{W[i][12], W[i][13], W[i][14]});
  PyDict<int64_t, std::vector<Part*>> by_owner;
  for (Part* p : parts) {
    if (auto* v = by_owner.Find(p->owner)) {
      v->push_back(p);
    } else {
      by_owner.Set(p->owner, {p});
    }
  }
  for (size_t oi = 0; oi < nodes.size(); ++oi) {
    const int64_t oid = static_cast<int64_t>(oi);
    const auto* psp = by_owner.Find(oid);
    if (!psp || psp->empty()) {
      bo.objects.push_back(std::nullopt);
      continue;
    }
    const std::vector<Part*>& ps = *psp;
    std::vector<int64_t> pal;
    bool skinned = false;
    for (const Part* p : ps) {
      for (const Vert& v : p->verts) {
        for (const auto& [b, w] : v.w4.items())
          if (std::find(pal.begin(), pal.end(), b) == pal.end()) pal.push_back(b);
        if (!v.w4.OnlyKey(oid)) skinned = true;
      }
    }
    if (!skinned) {
      pal.clear();
    } else {
      int64_t at = -1;
      for (size_t k = 0; k < nodes.size(); ++k)
        if (nodes[k].id == oid) {
          at = static_cast<int64_t>(k);
          break;
        }
      if (at < 0) Fail("IndexError: list index out of range");
      const int64_t par = nodes[static_cast<size_t>(at)].parent;
      Assert(par >= 0, "('skinned object owned by a root node', " + Str(oid) + ")");
      const int64_t pid = Idx(nodes, par).id;
      std::vector<int64_t> np{pid};
      if (std::find(pal.begin(), pal.end(), oid) != pal.end()) {
        for (int64_t b : pal)
          if (b != oid) np.push_back(b);
        if (np.size() == 1) np = {np[0], np[0]};
        ctx.owner_slot.insert(oid);
      } else {
        for (int64_t b : pal)
          if (b != pid) np.push_back(b);
      }
      pal = np;
      if (pal.size() == 1) pal = {pal[0], pal[0]};
      Assert(pal.size() <= kMaxPalette, "('palette too long', " + Str(oid) + ")");
      Assert(!ctx.owner_slot.count(oid) || (ctx.neck_id && oid == *ctx.neck_id) || ctx.allow_owner_slot,
             "('owner-slot palette off the neck (R13)', " + Str(oid) + ")");
    }
    std::vector<const std::vector<int64_t>*> natives;
    for (const Part* p : ps)
      if (p->native) natives.push_back(&p->native->src_pal);
    if (!natives.empty() && natives.size() == ps.size()) {
      bool same = true;
      for (const auto* n : natives)
        if (*n != *natives[0]) same = false;
      if (same) {
        pal = *natives[0];
        ctx.owner_slot.erase(oid);
      }
    }
    bo.objects.push_back(Object{ps, pal});
  }
  return bo;
}

// ------------------------------------------------------------------ bounds (line 986)
Bytes Bounds(const std::vector<Vec3>& pts) {
  if (pts.empty()) Fail("ValueError: min() arg is an empty sequence");
  Vec3 lo, hi;
  for (int c = 0; c < 3; ++c) {
    lo[c] = pts[0][c];
    hi[c] = pts[0][c];
    for (size_t i = 1; i < pts.size(); ++i) {
      if (pts[i][c] < lo[c]) lo[c] = pts[i][c];  // min: the first of equal values stays
      if (pts[i][c] > hi[c]) hi[c] = pts[i][c];
    }
  }
  Vec3 ctr;
  for (int c = 0; c < 3; ++c) ctr[c] = (lo[c] + hi[c]) / 2;
  double r = 0.0;
  for (size_t i = 0; i < pts.size(); ++i) {
    num::PySum s;
    for (int c = 0; c < 3; ++c) {
      const double d = pts[i][c] - ctr[c];
      s.Add(d * d);
    }
    const double v = std::sqrt(s.Result());
    if (i == 0 || v > r) r = v;
  }
  Vec3 half;
  for (int c = 0; c < 3; ++c) half[c] = (hi[c] - lo[c]) / 2;
  Bytes out;
  for (double v : {ctr[0], ctr[1], ctr[2], r, ctr[0], ctr[1], ctr[2], 1.0, half[0], half[1], half[2], 0.0})
    PutF32(out, v);
  return out;
}

// ------------------------------------------------------------------ encode_objects (line 874)
Encoded EncodeObjects(const Context& ctx, const std::vector<std::optional<Object>>& objects,
                      const PyDict<int64_t, Vec3>& bind4) {
  Encoded e;
  const uint32_t kF3 = DtByName("F3");
  const tpr::DeclElem kEnd{0xFF, 0, 0xFFFFFFFFu, 0, 0, 0, 0};
  for (size_t oi = 0; oi < objects.size(); ++oi) {
    const int64_t oid = static_cast<int64_t>(oi);
    if (!objects[oi]) {
      const uint64_t vb_i = e.vbs.size(), ib_i = e.ibs.size();
      Bytes vb;
      for (int i = 0; i < 3; ++i) PutF32(vb, 0.0);
      e.vbs.push_back(vb);
      e.ibs.push_back(Bytes(6, 0));
      tpr::Decl dcl;
      dcl.head = {0, 1, be::CheckU32(ib_i), 3, 2, 1, 1, 0, 0, 0, 0, 0};
      dcl.elems = {tpr::DeclElem{0, 0, kF3, 0, 0, 0, 0}, kEnd};
      dcl.tails = {Pack4I(vb_i, 12, 0, 0)};
      if (!ctx.dummy_mesh) Fail("TypeError: DUMMY_MESH is None");
      tpr::ObjGeo g;
      g.pre = Pack4I(0x00020000, oi, 0, 0);
      g.decls = {dcl};
      g.meshes = {*ctx.dummy_mesh};
      e.geos.push_back(std::move(g));
      const Bytes b = Bounds({Vec3{0.0, 0.0, 0.0}});
      tpr::ObjInfo inf;
      inf.words = {0x00010001, be::CheckU32(oi), 0, 0, 1, 0, 0, 2, 0, 0, 0, 0};
      inf.bounds = b;
      Bytes rec = Pack8I({0, 1, 2, 0, 0, 0, 0, 0});
      rec.insert(rec.end(), b.begin(), b.end());
      inf.infos = {rec};
      e.infos.push_back(std::move(inf));
      continue;
    }
    const Object& ob = *objects[oi];
    const auto& ps = ob.ps;
    const auto& pal = ob.pal;
    const Vec3 own = bind4.At(oid);
    std::vector<tpr::Decl> decls;
    std::vector<Bytes> meshes, minfos;
    std::vector<Vec3> allpts;
    std::vector<int64_t> srcs;
    uint32_t mask8 = 0, mask10 = 0;
    for (size_t k = 0; k < ps.size(); ++k) {
      const Part* p = ps[k];
      if (p->native) {
        const NativeDecl& nat = *p->native;
        Assert(pal == nat.src_pal, "('verbatim decl needs its own palette', " + Str(oid) + ")");
        std::vector<Vec3> pts;
        for (const Vert& v : p->verts) pts.push_back(Vec3{v.pos[0] - own[0], v.pos[1] - own[1], v.pos[2] - own[2]});
        const uint64_t vb_i = e.vbs.size(), ib_i = e.ibs.size();
        e.vbs.push_back(nat.vb);
        if (p->writable) {
          e.vbs.push_back(nat.vb);
          e.vbs.push_back(nat.vb);
        }
        e.ibs.push_back(p->ib);
        tpr::Decl dcl;
        dcl.head = {0, 1, be::CheckU32(ib_i), p->icount, be::CheckU32(nat.elems.size()),
                    be::CheckU32(p->verts.size()), be::CheckU32(p->meshes.size()), 0, p->writable ? 1u : 0u, 0, 0, 0};
        dcl.elems = nat.elems;
        dcl.tails = {Pack4I(vb_i, nat.stride, 0, 0)};
        decls.push_back(std::move(dcl));
        const auto idx = UnpackH(p->ib, p->icount);
        for (size_t mi = 0; mi < p->meshes.size(); ++mi) {
          const Bytes& m = p->meshes[mi];
          const auto sl = SliceIdx(idx, U32(m, 0x10), U32(m, 0x14));
          std::vector<Vec3> mp;
          for (uint16_t i : sl)
            if (i != 0xFFFF) mp.push_back(Idx(pts, i));
          const PassRec pr = ctx.mesh_passes ? Idx(p->passes, static_cast<int64_t>(mi)) : PassRec{0, 2, 0};
          mask8 |= pr.pass;
          mask10 |= pr.d10;
          srcs.push_back(pr.src);
          Bytes rec = Pack8I({meshes.size(), 1, pr.pass, 0, pr.d10, 0, 0, 0});
          const Bytes b = Bounds(mp.empty() ? pts : mp);
          rec.insert(rec.end(), b.begin(), b.end());
          minfos.push_back(std::move(rec));
          meshes.push_back(tpr::MeshPatch(m, meshes.size(), k));
        }
        allpts.insert(allpts.end(), pts.begin(), pts.end());
        continue;
      }
      std::vector<tpr::DeclElem> elems{tpr::DeclElem{0, 0, kF3, 0, 0, 0, 0}};
      uint64_t off = 12;
      for (const KeptElem& ke : p->kept) {
        elems.push_back(tpr::DeclElem{0, be::CheckU16(off), ke.typ, 0, ke.usage, ke.ui, 0});
        off += Dt(ke.typ).size;
      }
      std::string bwt;
      if (!pal.empty()) {
        if (p->h4) {
          bwt = "H4";
        } else if (pal.size() == 2) {
          bwt = "F2";
        } else if (pal.size() == 3) {
          bwt = "F3";
        } else if (pal.size() == 4) {
          bwt = "H4";
        } else {
          Fail("KeyError: " + Str(int64_t(pal.size())));
        }
        elems.push_back(tpr::DeclElem{0, be::CheckU16(off), DtByName(bwt), 0, 1, 0, 0});
        off += bwt == "F3" ? 12 : 8;
      }
      elems.push_back(kEnd);
      const uint64_t stride = off;
      Bytes vb;
      vb.reserve(stride * p->verts.size());
      std::vector<Vec3> pts;
      pts.reserve(p->verts.size());
      const bool slot = ctx.owner_slot.count(oid) != 0;
      for (size_t vi = 0; vi < p->verts.size(); ++vi) {
        const Vert& v = p->verts[vi];
        const Vec3 lp{v.pos[0] - own[0], v.pos[1] - own[1], v.pos[2] - own[2]};
        pts.push_back(lp);
        for (int c = 0; c < 3; ++c) PutF32(vb, lp[c]);
        if (p->raw_stride) {
          const auto r = p->Raw(vi);
          vb.insert(vb.end(), r.begin(), r.end());
        }
        if (p->h4) {
          for (double x : (*p->h4)[vi]) PutF16(vb, x);
        } else if (!pal.empty()) {
          std::vector<double> ws(pal.size(), 0.0);
          for (const auto& [b, wt] : v.w4.items()) {
            size_t at;
            if (slot) {
              if (b == oid) {
                at = 0;
              } else {
                auto it = std::find(pal.begin() + 1, pal.end(), b);
                if (it == pal.end()) Fail("ValueError: " + Str(b) + " is not in list");
                at = static_cast<size_t>(it - pal.begin());
              }
            } else {
              auto it = std::find(pal.begin(), pal.end(), b);
              if (it == pal.end()) Fail("ValueError: " + Str(b) + " is not in list");
              at = static_cast<size_t>(it - pal.begin());
            }
            ws[at] += wt;
          }
          if (bwt == "H4") {
            for (double x : ws) PutF16(vb, x);
          } else {
            for (double x : ws) PutF32(vb, x);
          }
        }
      }
      Assert(vb.size() == stride * p->verts.size(), "len(vb) == stride * len(p.verts)");
      const uint64_t vb_i = e.vbs.size(), ib_i = e.ibs.size();
      e.vbs.push_back(vb);
      if (p->writable) {
        e.vbs.push_back(vb);
        e.vbs.push_back(vb);
      }
      e.ibs.push_back(p->ib);
      tpr::Decl dcl;
      dcl.head = {0, 1, be::CheckU32(ib_i), p->icount, be::CheckU32(elems.size()), be::CheckU32(p->verts.size()),
                  be::CheckU32(p->meshes.size()), 0, p->writable ? 1u : 0u, 0, 0, 0};
      dcl.elems = elems;
      dcl.tails = {Pack4I(vb_i, stride, 0, 0)};
      decls.push_back(std::move(dcl));
      const auto idx = UnpackH(p->ib, p->icount);
      for (size_t mi = 0; mi < p->meshes.size(); ++mi) {
        const Bytes& m = p->meshes[mi];
        const auto sl = SliceIdx(idx, U32(m, 0x10), U32(m, 0x14));
        std::vector<Vec3> mp;
        for (uint16_t i : sl)
          if (i != 0xFFFF) mp.push_back(Idx(pts, i));
        const Bytes b = Bounds(mp.empty() ? pts : mp);
        const PassRec pr = ctx.mesh_passes ? Idx(p->passes, static_cast<int64_t>(mi)) : PassRec{0, 2, 0};
        mask8 |= pr.pass;
        mask10 |= pr.d10;
        Bytes rec = Pack8I({meshes.size(), 1, pr.pass, 0, pr.d10, 0, 0, 0});
        rec.insert(rec.end(), b.begin(), b.end());
        minfos.push_back(std::move(rec));
        meshes.push_back(tpr::MeshPatch(m, meshes.size(), k));
      }
      allpts.insert(allpts.end(), pts.begin(), pts.end());
    }
    bool all_native = true;
    for (const Part* p : ps)
      if (!p->native) all_native = false;
    if (ctx.mesh_passes && all_native && ps.size() > 1) {
      std::vector<size_t> order(meshes.size());
      for (size_t n = 0; n < order.size(); ++n) order[n] = n;
      std::stable_sort(order.begin(), order.end(),
                       [&](size_t a, size_t b) { return Idx(srcs, int64_t(a)) < Idx(srcs, int64_t(b)); });
      std::vector<Bytes> m2, i2;
      for (size_t nw = 0; nw < order.size(); ++nw) {
        m2.push_back(tpr::MeshPatch(meshes[order[nw]], nw));
        Bytes r;
        PutU32(r, be::CheckU32(nw));
        const Bytes& old = minfos[order[nw]];
        r.insert(r.end(), old.begin() + 4, old.end());
        i2.push_back(std::move(r));
      }
      meshes = std::move(m2);
      minfos = std::move(i2);
    }
    tpr::ObjGeo g;
    g.pre = Pack4I(0x00020000, oi, 0, 0);
    g.decls = std::move(decls);
    g.meshes = std::move(meshes);
    e.geos.push_back(std::move(g));
    tpr::ObjInfo inf;
    inf.words = {0x00010001, be::CheckU32(oi), 0, be::CheckU32(pal.size()), 1, 0, 0, mask8, mask10, 0, 0, 0};
    inf.bounds = Bounds(allpts);
    for (int64_t b : pal) inf.palette.push_back(be::CheckU16(static_cast<uint64_t>(b)));
    inf.infos = std::move(minfos);
    e.infos.push_back(std::move(inf));
  }
  return e;
}

// ------------------------------------------------------------------ tex_spans (line 999)
std::vector<TexSpan> TexSpans(const Model& model) {
  static constexpr tpr::Fourcc kTx = tpr::MakeFourcc("TX2D"), kVb = tpr::MakeFourcc("VBUF"),
                               kIb = tpr::MakeFourcc("IBUF");
  std::vector<uint64_t> addrs;
  for (const tpr::Res& r : model.x.res) {
    if (r.type == kTx) {
      addrs.push_back(tpr::TexAddrs(r.blob).first);
    } else if (r.type == kVb || r.type == kIb) {
      addrs.push_back(tpr::BufSpan(r.blob).first);
    }
  }
  std::sort(addrs.begin(), addrs.end());
  addrs.push_back(model.x.data.size());
  std::vector<TexSpan> out;
  for (const Bytes& b : model.tex) {
    const auto [base, mip] = tpr::TexAddrs(b);
    bool found = false;
    uint64_t nxt = 0;
    for (uint64_t a : addrs)
      if (a > base && (!found || a < nxt)) {
        nxt = a;
        found = true;
      }
    if (!found) Fail("ValueError: min() arg is an empty sequence");
    Assert(mip == 0 || (base < mip && mip < nxt), "('texture mip chain not inside its span', ...)");
    TexSpan t;
    t.desc = b;
    const uint64_t e = std::min<uint64_t>(nxt, model.x.data.size());
    t.span.assign(model.x.data.begin() + std::min<uint64_t>(base, e), model.x.data.begin() + e);
    t.base = base;
    t.mip = mip;
    out.push_back(std::move(t));
  }
  return out;
}

// ------------------------------------------------------------------ doa4_tex_desc (line 1018)
namespace {
const uint8_t kDoa4TxHead[16] = {0, 3, 0, 1, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 0, 0};
const uint8_t kDoax2TxHead[28] = {0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 0, 0};
const uint8_t kDoa4VbHead[12] = {0, 1, 0, 1, 0, 0, 0, 0, 0xff, 0xff, 0, 0};
const uint8_t kDoa4IbHead[12] = {0, 2, 0, 1, 0, 0, 0, 0, 0xff, 0xff, 0, 0};
}  // namespace

Bytes Doa4TexDesc(std::span<const uint8_t> desc) {
  if (desc.size() == 0x28) {
    Assert(std::memcmp(desc.data(), kDoa4TxHead, 16) == 0, "desc[:16].hex()");
    return Bytes(desc.begin(), desc.end());
  }
  Assert(desc.size() == 0x34 && std::memcmp(desc.data(), kDoax2TxHead, 28) == 0, "desc.hex()");
  Bytes out(kDoa4TxHead, kDoa4TxHead + 16);
  out.insert(out.end(), desc.begin() + 28, desc.end());
  return out;
}

// ------------------------------------------------------------------ build_tpr (line 1026)
Bytes BuildTpr(const std::vector<tpr::Node>& nodes, const std::vector<tpr::ObjGeo>& geos,
               const std::vector<tpr::ObjInfo>& infos, const std::vector<Bytes>& vbs, const std::vector<Bytes>& ibs,
               const std::vector<TexSpan>& textures) {
  Bytes data;
  std::vector<Bytes> tex_descs;
  for (const TexSpan& t : textures) {
    const uint64_t pos = tpr::Al(data.size(), 0x1000);
    data.resize(pos, 0);
    data.insert(data.end(), t.span.begin(), t.span.end());
    Bytes d4 = Doa4TexDesc(t.desc);
    d4 = tpr::TexSet(d4, static_cast<int64_t>(pos),
                     t.mip ? static_cast<int64_t>(pos) + int64_t(t.mip) - int64_t(t.base) : 0);
    tex_descs.push_back(std::move(d4));
  }
  std::vector<Bytes> vb_descs, ib_descs;
  for (int kind = 0; kind < 2; ++kind) {
    const auto& bufs = kind == 0 ? vbs : ibs;
    const uint8_t* head = kind == 0 ? kDoa4VbHead : kDoa4IbHead;
    auto& out = kind == 0 ? vb_descs : ib_descs;
    const uint32_t low0 = kind == 0 ? 3 : 0, low1 = kind == 0 ? 2 : 0;
    for (const Bytes& b : bufs) {
      const uint64_t pos = tpr::Al(data.size(), 0x1000);
      data.resize(pos, 0);
      const uint64_t size = tpr::Al(b.size(), 16);
      data.insert(data.end(), b.begin(), b.end());
      data.resize(data.size() + (size - b.size()), 0);
      Bytes d(head, head + 12);
      PutU32(d, be::CheckU32(pos | low0));
      PutU32(d, be::CheckU32(size | low1));
      out.push_back(std::move(d));
    }
  }
  data.resize(tpr::Al(data.size(), 0x1000), 0);
  const uint64_t ntex = tex_descs.size(), nvb = vb_descs.size(), nib = ib_descs.size();
  be::Writer u0;
  for (uint64_t v : {uint64_t(7), uint64_t(4), ntex, uint64_t(4), nvb, 4 + ntex, nib, 4 + ntex + nvb, uint64_t(1),
                     uint64_t(2), uint64_t(3), uint64_t(16)})
    u0.U32(v);
  static constexpr tpr::Fourcc kUser = tpr::MakeFourcc("USER"), kTx = tpr::MakeFourcc("TX2D"),
                               kVb = tpr::MakeFourcc("VBUF"), kIb = tpr::MakeFourcc("IBUF");
  tpr::Xpr x;
  x.res.push_back({kUser, u0.Take()});
  x.res.push_back({kUser, tpr::BuildMdlGeo(geos)});
  x.res.push_back({kUser, tpr::BuildMdlInfo(infos)});
  x.res.push_back({kUser, tpr::BuildHie(nodes)});
  for (auto& t : tex_descs) x.res.push_back({kTx, std::move(t)});
  for (auto& v : vb_descs) x.res.push_back({kVb, std::move(v)});
  for (auto& i : ib_descs) x.res.push_back({kIb, std::move(i)});
  x.data = std::move(data);
  return x.Build(1);
}

// ------------------------------------------------------------------ build_cat (line 1055)
Bytes BuildCat(const Model& ref, const Model& body, const std::vector<int>& sections, const std::vector<int64_t>& kept_ids,
               const std::vector<std::string>& sections_opts) {
  auto has = [&](int s) { return std::find(sections.begin(), sections.end(), s) != sections.end(); };
  auto opt = [&](const char* s) { return std::find(sections_opts.begin(), sections_opts.end(), s) != sections_opts.end(); };
  const Bytes& rc = ref.Cat();
  const auto rdir = CatDict(rc);
  auto RDir = [&](int t) {
    auto it = rdir.find(t);
    if (it == rdir.end()) Fail("KeyError: " + Str(t));
    return it->second;
  };
  const std::set<int64_t> kept(kept_ids.begin(), kept_ids.end());
  Bytes out{'C', 'A', 'T', 0};
  out.resize(24, 0);
  std::vector<std::pair<int64_t, uint64_t>> recs;
  auto put = [&](int64_t typ, const Bytes& blob) {
    const uint64_t pos = tpr::Al(out.size(), 8);
    out.resize(pos, 0);
    recs.emplace_back(typ, pos);
    out.insert(out.end(), blob.begin(), blob.end());
  };
  if (has(2)) {
    const auto groups = tpr::ParseT2(rc, RDir(2)).first;
    Bytes blob;
    for (const auto& [c, i] : Idx(groups, 0).items)
      if (kept.count(i)) {
        blob.push_back(uint8_t(c >> 8));
        blob.push_back(uint8_t(c));
        blob.push_back(i);
      }
    for (size_t gi = 1; gi < groups.size(); ++gi) {
      bool all = true;
      for (const auto& [c, i] : groups[gi].items)
        if (!kept.count(i)) all = false;
      if (!all) continue;
      blob.push_back(groups[gi].kind);
      for (const auto& [c, i] : groups[gi].items) {
        blob.push_back(uint8_t(c >> 8));
        blob.push_back(uint8_t(c));
        blob.push_back(i);
      }
    }
    blob.push_back(0);
    if (out.size() > 0x18) Fail("ValueError: negative count");
    out.resize(0x18, 0);
    recs.emplace_back(2, out.size());
    out.insert(out.end(), blob.begin(), blob.end());
  }
  if (has(5)) {
    const uint64_t t2_end = tpr::ParseT2(rc, RDir(2)).second;
    const uint64_t t5 = RDir(5);
    bool found = false;
    uint64_t t5_end = 0;
    for (const auto& [t, v] : tpr::CatDir(rc))
      if (v > t5 && t != 9 && (!found || v < t5_end)) {
        t5_end = v;
        found = true;
      }
    if (!found) Fail("ValueError: min() arg is an empty sequence");
    Assert(out.size() <= t2_end, "('our type-2 map is longer than the reference one', ...)");
    out.resize(t2_end, 0);
    const auto s = Reader(rc).Slice(std::min<uint64_t>(t2_end, rc.size()), std::min<uint64_t>(t5_end, rc.size()));
    out.insert(out.end(), s.begin(), s.end());
    recs.emplace_back(5, t5);
  }
  if (has(4)) {
    const Bytes& bc = body.Cat();
    const auto bdir = CatDict(bc);
    auto bit = bdir.find(4);
    if (bit == bdir.end()) Fail("KeyError: 4");
    const auto mine = tpr::T4Records(bc, bit->second);
    std::map<uint32_t, Bytes> keyed;  // iterated sorted by key, as the Python's sorted(keyed)
    if (opt("ref4"))
      for (const Bytes& rec : tpr::T4Records(rc, RDir(4))) keyed[U32(rec, 4)] = rec;
    if (!opt("refonly"))
      for (const Bytes& rec : mine) {
        const uint32_t k = U32(rec, 4);
        if (!keyed.count(k)) keyed[k] = rec;
      }
    Bytes blob;
    for (const auto& [k, rec] : keyed) blob.insert(blob.end(), rec.begin(), rec.end());
    for (uint8_t b : {0xff, 0xff, 0x00, 0x00}) blob.push_back(b);
    put(4, blob);
  }
  if (has(6)) {
    const uint64_t o = RDir(6);
    uint64_t nxt = U32(rc, 4);
    for (const auto& [t, v] : tpr::CatDir(rc))
      if (v > o && v < nxt) nxt = v;
    Assert(nxt >= o + 0x18, "('reference type-6 block shorter than 24 bytes', ...)");
    const auto s = Reader(rc).Slice(std::min<uint64_t>(o, rc.size()), std::min<uint64_t>(o + 0x18, rc.size()));
    put(6, Bytes(s.begin(), s.end()));
  }
  if (has(9)) recs.emplace_back(9, RDir(9));
  out.resize(tpr::Al(out.size(), 8), 0);
  const uint64_t dir_off = out.size();
  for (const auto& [t, v] : recs) {
    out.push_back(1);
    out.push_back(0);
    out.push_back(be::CheckU8(static_cast<uint64_t>(t)));
    out.push_back(0);
    PutU32(out, be::CheckU32(v));
  }
  out.resize(out.size() + 8, 0);
  out.resize(tpr::Al(out.size(), 16), 0);
  be::PackIntoU32(out, 4, dir_off);
  be::PackIntoU32(out, 8, 0);
  be::PackIntoU32(out, 12, 0);
  if (opt("cloth")) Fail("not ported yet: build_cat 'cloth' (multi_cloth_spare_only)");
  return out;
}

// ------------------------------------------------------------------ strip_tris (line 1173)
std::vector<std::array<uint16_t, 3>> StripTris(std::span<const uint16_t> idx) {
  std::vector<std::array<uint16_t, 3>> out;
  std::vector<uint16_t> run;
  for (uint16_t i : idx) {
    if (i == 0xFFFF) {
      run.clear();
      continue;
    }
    run.push_back(i);
    if (run.size() >= 3) {
      const uint16_t a = run[run.size() - 3], b = run[run.size() - 2], c = run[run.size() - 1];
      if (a != b && b != c && a != c) out.push_back(run.size() % 2 ? std::array<uint16_t, 3>{a, b, c}
                                                                     : std::array<uint16_t, 3>{b, a, c});
    }
  }
  return out;
}

std::vector<int64_t> RoleCodesSorted() {
  std::set<int64_t> s;
  for (const auto& [k, v] : Role()) s.insert(v);
  return std::vector<int64_t>(s.begin(), s.end());
}

const std::set<int64_t>& DropObjectsBody() {
  static const std::set<int64_t> s{4, 10, 23, 29};
  return s;
}

}  // namespace doa4::suitconv::core
