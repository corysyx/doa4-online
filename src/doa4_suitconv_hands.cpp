// Suit converter (Plan 2): the hands. See doa4_suitconv_hands.h. Line numbers in the comments are physics_port.py's
// (SHA-1 b8b69003c5ae388868ed8e5fff5f50a91263f98a).
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_hands.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>

#include "doa4_suitconv_be.h"
#include "doa4_suitconv_catx.h"
#include "doa4_suitconv_num.h"
#include "doa4_suitconv_sort.h"

namespace doa4::suitconv::hands {

using core::Assert;
using core::Fail;
using core::Vec3;

// ------------------------------------------------------------------ the fused multiply-add and np.linalg.norm
namespace {

void TwoSum(double a, double b, double& s, double& e) {
  s = a + b;
  const double bb = s - a;
  e = (a - (s - bb)) + (b - bb);
}

void Split(double a, double& hi, double& lo) {
  const double c = 134217729.0 * a;  // 2^27 + 1
  hi = c - (c - a);
  lo = a - hi;
}

// p + e = a * b exactly (Dekker), for operands that neither overflow in the split nor underflow in the error term.
void TwoProd(double a, double b, double& p, double& e) {
  p = a * b;
  double ah, al, bh, bl;
  Split(a, ah, al);
  Split(b, bh, bl);
  e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
}

}  // namespace

double SoftFma(double a, double b, double c) {
  const double plain = a * b + c;
  if (!std::isfinite(plain) || a == 0.0 || b == 0.0 || c == 0.0) return plain;  // a zero operand: one rounding already
  const double big = 0x1p+500, small = 0x1p-400;
  if (std::fabs(a) > big || std::fabs(b) > big || std::fabs(a * b) < small) return plain;
  double uh, ul;
  TwoProd(a, b, uh, ul);
  double th, tl;
  TwoSum(c, uh, th, tl);
  // v = RO(tl + ul): round to odd. The exact sum is s + t; if t != 0 and s is even, the odd neighbour on t's side.
  double s, t;
  TwoSum(tl, ul, s, t);
  if (t != 0.0 && s != 0.0) {
    uint64_t bits = std::bit_cast<uint64_t>(s);
    if ((bits & 1) == 0) {
      const bool away = (t > 0.0) == (s > 0.0);  // t pushes the magnitude of s up
      bits = away ? bits + 1 : bits - 1;
      s = std::bit_cast<double>(bits);
    }
  }
  return th + s;
}

double Norm3(const Vec3& v) {
  double acc = 0.0;
  for (int i = 0; i < 3; ++i) acc = SoftFma(v[i], v[i], acc);
  return std::sqrt(acc);
}

// ndarray.astype(int) of an already integral float64 on this Windows numpy: an int32 (C long); NaN and values outside
// the int32 range give -2147483648.
static int32_t AstypeInt32(double r) {
  if (!(r >= -2147483648.0 && r < 2147483648.0)) return std::numeric_limits<int32_t>::min();
  return static_cast<int32_t>(r);
}

// np.round(x / 0.0002).astype(int): numpy's rint (half to even), then the int32 cast.
int32_t WeldCoord(double x) {
  const double q = x / 0.0002;
  if (!std::isfinite(q)) return std::numeric_limits<int32_t>::min();
  return AstypeInt32(num::RoundHalfEven(q));
}

namespace {

constexpr int64_t kHandCodes[2] = {0x900B, 0x9005};  // HAND_CHANNELS = {0: 0x900B, 1: 0x9005}

std::string Repr(int64_t v) { return std::to_string(v); }

struct OpenEdge {
  std::vector<Vec3> old_rim;  // pos[rim]
  Vec3 centre{};
  double toward = 0.0;
};

// _tuck_open_edge(p, own, strip_tris, report, along=0.016, inward=0.010), lines 767-805.
OpenEdge TuckOpenEdge(core::Part& p, double along = 0.016, double inward = 0.010) {
  const size_t nv = p.verts.size();
  std::vector<Vec3> pos(nv);
  for (size_t i = 0; i < nv; ++i) pos[i] = p.verts[i].pos;
  // idx = struct.unpack(f'>{p.icount}H', p.ib)
  if (p.ib.size() != static_cast<size_t>(p.icount) * 2) Fail("struct.error: unpack requires a buffer of the index size");
  std::vector<uint16_t> idx(p.icount);
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<uint16_t>((p.ib[2 * i] << 8) | p.ib[2 * i + 1]);
  // weld ids by first sight: key.setdefault(tuple(...), len(key)), a dict in insertion order (only the ids matter)
  std::map<std::array<int32_t, 3>, int64_t> key;
  std::vector<int64_t> weld;
  weld.reserve(nv);
  for (const Vec3& x : pos) {
    const std::array<int32_t, 3> k{WeldCoord(x[0]), WeldCoord(x[1]), WeldCoord(x[2])};
    const int64_t next = static_cast<int64_t>(key.size());
    weld.push_back(key.emplace(k, next).first->second);
  }
  // edges = collections.Counter(): a dict in insertion order, but only the counts are read (below)
  std::map<std::pair<int64_t, int64_t>, int64_t> edges;
  for (const core::Bytes& m : p.meshes) {
    const uint32_t st = core::U32(m, 0x10), cnt = core::U32(m, 0x14);
    // idx[st:st + cnt]: a slice, clamped to the tuple
    const size_t a = std::min<size_t>(st, idx.size());
    const size_t b = std::min<size_t>(static_cast<uint64_t>(st) + cnt, idx.size());
    const auto tris = core::StripTris(std::span<const uint16_t>(idx.data() + a, b - a));
    for (const auto& t : tris) {
      int64_t w[3];
      for (int i = 0; i < 3; ++i) {
        if (t[i] >= weld.size()) Fail("IndexError: list index out of range");
        w[i] = weld[t[i]];
      }
      if (w[0] != w[1] && w[1] != w[2] && w[0] != w[2]) {  // len({a, b, c}) == 3
        const int64_t e[3][2] = {{w[0], w[1]}, {w[1], w[2]}, {w[2], w[0]}};
        for (const auto& xy : e) ++edges[{std::min(xy[0], xy[1]), std::max(xy[0], xy[1])}];
      }
    }
  }
  // edge_points = {x for e, n in edges.items() if n == 1 for x in e}: a set that is only tested for membership
  // (`w in edge_points`, line 785) and for truthiness (the assert), so its iteration order reaches no output.
  std::set<int64_t> edge_points;
  for (const auto& [e, n] : edges)
    if (n == 1) {
      edge_points.insert(e.first);
      edge_points.insert(e.second);
    }
  Assert(!edge_points.empty(), "the hand has no open edge");
  std::vector<size_t> rim;  // rim = [j for j, w in enumerate(weld) if w in edge_points]
  for (size_t j = 0; j < weld.size(); ++j)
    if (edge_points.count(weld[j])) rim.push_back(j);
  // the arm side: from the hand's middle towards its rim; pos.mean(axis=0) and pos[rim].mean(axis=0)
  std::vector<double> flat(nv * 3), flat_rim(rim.size() * 3);
  for (size_t i = 0; i < nv; ++i)
    for (int c = 0; c < 3; ++c) flat[i * 3 + c] = pos[i][c];
  for (size_t i = 0; i < rim.size(); ++i)
    for (int c = 0; c < 3; ++c) flat_rim[i * 3 + c] = pos[rim[i]][c];
  double middle[3], centre[3];
  num::NpMeanAxis0(flat, 3, middle);
  num::NpMeanAxis0(flat_rim, 3, centre);
  OpenEdge out;
  out.centre = {centre[0], centre[1], centre[2]};
  out.toward = centre[0] > middle[0] ? 1.0 : -1.0;
  for (size_t j : rim) {
    Vec3 v = pos[j];
    v[0] += out.toward * along;
    const Vec3 r{0.0, centre[1] - v[1], centre[2] - v[2]};
    const double n = Norm3(r);
    if (n > 1e-6)
      for (int c = 0; c < 3; ++c) v[c] = v[c] + r[c] / n * inward;  // v += r / n * inward
    p.verts[j].pos = v;
    out.old_rim.push_back(pos[j]);
  }
  return out;
}

// _tuck_arm_edge(objects, hand, old_rim, centre, toward, report, along=0.004, inward=0.004), lines 740-766.
void TuckArmEdge(std::vector<std::optional<core::Object>>& objects, int64_t hand, const OpenEdge& edge,
                 double along = 0.004, double inward = 0.004) {
  const double near_sq = 0.0005 * 0.0005;  // 0.0005 ** 2 (pow(x, 2) is x * x here: the same double)
  int64_t moved = 0;
  for (auto& ob : objects) {
    if (!ob) continue;
    if (std::find(ob->pal.begin(), ob->pal.end(), hand) == ob->pal.end()) continue;
    for (core::Part* q : ob->ps) {
      if (q->native) continue;
      for (core::Vert& vert : q->verts) {
        Vec3 x = vert.pos;
        // ((old_rim - x) ** 2).sum(1).min() > 0.0005 ** 2: numpy's min propagates a NaN
        double mn = 0.0;
        bool first = true, nan = false;
        for (const Vec3& r : edge.old_rim) {
          const double sq[3] = {(r[0] - x[0]) * (r[0] - x[0]), (r[1] - x[1]) * (r[1] - x[1]),
                                (r[2] - x[2]) * (r[2] - x[2])};
          const double s = num::NpSumLastAxis(std::span<const double>(sq, 3));
          if (std::isnan(s)) nan = true;
          if (first || s < mn) mn = s;
          first = false;
        }
        if (nan) mn = std::numeric_limits<double>::quiet_NaN();
        if (mn > near_sq) continue;
        x[0] -= edge.toward * along;
        const Vec3 d{0.0, edge.centre[1] - x[1], edge.centre[2] - x[2]};
        const double n = Norm3(d);
        if (n > 1e-6)
          for (int c = 0; c < 3; ++c) x[c] = x[c] + d[c] / n * inward;  // x = x + d / n * inward
        vert.pos = x;
        ++moved;
      }
    }
  }
  Assert(moved != 0, "no arm vertex on the hand rim");
}

// _mpp_read(d, index), lines 705-722.
struct MppRead {
  int64_t obj = 0, kind = 0;
  std::vector<uint32_t> words;
  int64_t records = 0;
  std::vector<std::vector<double>> poses;
};

MppRead ReadMpp(std::span<const uint8_t> d, int64_t index) {
  const be::Reader r(d);
  MppRead out;
  const int64_t m = r.U32(12);
  Assert(m != 0 && r.Slice(static_cast<uint64_t>(m), static_cast<uint64_t>(m) + 4).size() == 4 &&
             std::memcmp(d.data() + m, "MPV\0", 4) == 0,
         "donor has no MPV");
  const int64_t off = r.U32(m + 36 + 4 * index);
  Assert(off != 0, "('donor has no channel', " + Repr(index) + ")");
  const int64_t p = m + off;
  const auto h = r.U32s(p + 12, 4);
  out.obj = h[0];
  out.kind = h[1];
  const int64_t nposes = h[2], mp = h[3];
  int64_t q = p + mp;
  while (r.U32(q) != 0xFFFFFFFFu) {
    out.words.push_back(r.U32(q));
    q += 4;
  }
  int64_t high = 0;
  for (uint32_t w : out.words) high += (w & 0x80000000u) ? 1 : 0;
  out.records = 1 + high;
  const int64_t stride = out.kind == 2 ? 24 : 12;
  const uint64_t per = static_cast<uint64_t>(out.records * stride / 4);
  for (int64_t i = 0; i < nposes; ++i) {
    const int64_t at = p + r.U32(p + 28 + 4 * i);
    if (per > d.size() / 4) Fail("struct.error: unpack_from requires a buffer larger than the donor");
    std::vector<double> pose(static_cast<size_t>(per));
    for (uint64_t k = 0; k < per; ++k) pose[k] = r.F32(at + static_cast<int64_t>(4 * k));
    out.poses.push_back(std::move(pose));
  }
  return out;
}

void Put32(std::vector<uint8_t>& b, size_t at, uint64_t v) {
  const uint32_t x = be::CheckU32(v);
  if (at + 4 > b.size()) Fail("struct.error: pack_into requires a buffer of at least " + std::to_string(at + 4) + " bytes");
  for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(x >> (8 * (3 - i)));
}

void Append32(std::vector<uint8_t>& b, uint64_t v) {
  const uint32_t x = be::CheckU32(v);
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(x >> (8 * (3 - i))));
}

void AppendF32(std::vector<uint8_t>& b, double v) {
  const uint32_t x = std::bit_cast<uint32_t>(num::PackF32(v));
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(x >> (8 * (3 - i))));
}

}  // namespace

std::vector<std::pair<int64_t, int64_t>> PrepHands(const core::PyDict<int64_t, int64_t>& chan2id,
                                                   std::vector<std::optional<core::Object>>& objects,
                                                   const core::PyDict<int64_t, Vec3>& bind4) {
  std::vector<std::pair<int64_t, int64_t>> out;
  for (int64_t index = 0; index < 2; ++index) {
    const int64_t code = kHandCodes[index];
    const int64_t oid = chan2id.At(code);
    if (oid < 0 || oid >= static_cast<int64_t>(objects.size())) Fail("IndexError: list index out of range");
    auto& ob = objects[static_cast<size_t>(oid)];
    Assert(ob.has_value() && ob->pal.empty(),
           "('hand object is missing or skinned', " + Repr(oid) + ", " + Repr(code) + ")");
    for (core::Part* p : ob->ps) {
      Assert(!p->native, "('hand decl is verbatim', '" + p->model_name + ":" + Repr(p->obj_id) + ":" +
                             Repr(p->decl_index) + "')");
      p->writable = true;
      bind4.At(oid);  // `bind4[oid]` is evaluated for the call (the tuck does not read it)
      const OpenEdge edge = TuckOpenEdge(*p);
      TuckArmEdge(objects, oid, edge);
    }
    out.emplace_back(index, oid);
  }
  return out;
}

std::vector<std::pair<int64_t, std::vector<uint8_t>>> BuildHands(
    const core::Model& donor, const std::vector<std::pair<int64_t, int64_t>>& hand_objects,
    const std::vector<std::optional<core::Object>>& objects, const std::vector<tpr::ObjGeo>& geos,
    const core::PyDict<int64_t, Vec3>& bind4, double tol) {
  std::vector<std::pair<int64_t, std::vector<uint8_t>>> out;
  const core::Bytes& dcat = donor.Cat();
  for (const auto& [index, oid] : hand_objects) {
    const MppRead mr = ReadMpp(std::span<const uint8_t>(dcat.data(), dcat.size()), index);
    Assert(mr.kind == 2, "('hand channel is not position + normal', " + Repr(mr.kind) + ")");
    const size_t records = static_cast<size_t>(mr.records);
    // where each donor record sits at rest, in its object's own frame
    std::vector<Vec3> rest(records, Vec3{0.0, 0.0, 0.0});
    {
      if (mr.obj < 0 || mr.obj >= static_cast<int64_t>(donor.geo.size())) Fail("IndexError: list index out of range");
      const be::Reader xr(std::span<const uint8_t>(donor.x.data.data(), donor.x.data.size()));
      int64_t r = -1;
      for (uint32_t w : mr.words) {
        const bool first = r < 0;
        if ((w & 0x80000000u) || first) {
          ++r;
          const uint32_t dk = (w >> 24) & 0x7F, at = w & 0xFFFFFF;
          const auto& decls = donor.geo[static_cast<size_t>(mr.obj)].decls;
          if (dk >= decls.size()) Fail("IndexError: list index out of range");
          const uint32_t vb = decls[dk].Vb();
          if (vb >= donor.vbs.size()) Fail("IndexError: list index out of range");
          const int64_t b = tpr::BufSpan(donor.vbs[vb]).first;
          if (r >= static_cast<int64_t>(records)) Fail("IndexError: index out of bounds for axis 0");
          for (int c = 0; c < 3; ++c) rest[static_cast<size_t>(r)][c] = xr.F32(b + at + 4 * c);
        }
      }
    }
    const Vec3 own = bind4.At(oid);
    if (oid < 0 || oid >= static_cast<int64_t>(objects.size()) || !objects[static_cast<size_t>(oid)])
      Fail("TypeError: 'NoneType' object is not subscriptable");
    const auto& ps = objects[static_cast<size_t>(oid)]->ps;
    std::vector<Vec3> P;
    std::vector<std::pair<size_t, size_t>> where;
    for (size_t k = 0; k < ps.size(); ++k)
      for (size_t j = 0; j < ps[k]->verts.size(); ++j) {
        const Vec3& v = ps[k]->verts[j].pos;
        P.push_back(Vec3{v[0] - own[0], v[1] - own[1], v[2] - own[2]});
        where.emplace_back(k, j);
      }
    if (oid >= static_cast<int64_t>(geos.size())) Fail("IndexError: list index out of range");
    const tpr::ObjGeo& geo = geos[static_cast<size_t>(oid)];
    for (size_t k = 0; k < ps.size(); ++k) {
      if (k >= geo.decls.size()) Fail("IndexError: list index out of range");
      const tpr::Decl& dc = geo.decls[k];
      bool pos_f3 = false, nrm_f3 = false;
      for (size_t i = 0; i + 1 < dc.elems.size(); ++i) {  // dc.elems[:-1]
        const tpr::DeclElem& e = dc.elems[i];
        const tpr::DtEntry* dt = tpr::DtFind(e.c);
        if (!dt) Fail("KeyError: " + std::to_string(e.c));
        const bool f3 = dt->name == "F3";
        if (e.e == 0 && e.b == 0 && f3) pos_f3 = true;
        if (e.e == 3 && e.b == 12 && f3) nrm_f3 = true;
      }
      Assert(pos_f3 && nrm_f3, "('hand decl: position and normal are not float at 0 and 12', [...])");
    }
    // the frames can differ by a fitted translation: take it from the data
    // near = np.array([P[np.argmin(((P - q) ** 2).sum(1))] - q for q in rest[::8]]); delta = np.median(near, axis=0)
    std::vector<double> sums(P.size());
    std::vector<double> near_flat;
    for (size_t ri = 0; ri < records; ri += 8) {
      const Vec3& q = rest[ri];
      for (size_t i = 0; i < P.size(); ++i) {
        const double sq[3] = {(P[i][0] - q[0]) * (P[i][0] - q[0]), (P[i][1] - q[1]) * (P[i][1] - q[1]),
                              (P[i][2] - q[2]) * (P[i][2] - q[2])};
        sums[i] = num::NpSumLastAxis(std::span<const double>(sq, 3));
      }
      const int64_t am = num::NpArgmin(sums);
      for (int c = 0; c < 3; ++c) near_flat.push_back(P[static_cast<size_t>(am)][c] - q[c]);
    }
    double delta[3];
    num::NpMedianAxis0(near_flat, 3, delta);
    const double tol_sq = tol * tol;
    std::vector<uint64_t> new_words;
    std::vector<size_t> keep;
    for (size_t r = 0; r < records; ++r) {
      const double rc[3] = {rest[r][0] + delta[0], rest[r][1] + delta[1], rest[r][2] + delta[2]};
      bool any = false;
      size_t n = 0;
      for (size_t v = 0; v < P.size(); ++v) {  // np.nonzero(...)[0]: ascending index order
        const double sq[3] = {(P[v][0] - rc[0]) * (P[v][0] - rc[0]), (P[v][1] - rc[1]) * (P[v][1] - rc[1]),
                              (P[v][2] - rc[2]) * (P[v][2] - rc[2])};
        const double s = num::NpSumLastAxis(std::span<const double>(sq, 3));
        if (!(s <= tol_sq)) continue;
        if (!any) {
          keep.push_back(r);
          any = true;
        }
        const auto [k, j] = where[v];
        new_words.push_back((static_cast<uint64_t>(k) << 24) | (static_cast<uint64_t>(j) * geo.decls[k].Stride()) |
                            ((n == 0 && keep.size() > 1) ? 0x80000000ull : 0ull));
        ++n;
      }
    }
    Assert(!keep.empty(), "('no hand vertex on the donor points', " + Repr(oid) + ")");
    const size_t nposes = mr.poses.size();
    double wrist_x = std::fabs(rest[0][0]);  // float(np.abs(rest[:, 0]).min()); a NaN propagates
    bool wnan = std::isnan(wrist_x);
    for (size_t r = 1; r < records; ++r) {
      const double a = std::fabs(rest[r][0]);
      if (std::isnan(a)) wnan = true;
      if (a < wrist_x) wrist_x = a;
    }
    if (wnan) wrist_x = std::numeric_limits<double>::quiet_NaN();
    const size_t head = (28 + 4 * nposes + 15) & ~static_cast<size_t>(15);
    std::vector<uint8_t> mpp(head, 0);
    mpp[0] = 'M';
    mpp[1] = 'P';
    mpp[2] = 'P';
    Put32(mpp, 4, 1);
    Put32(mpp, 12, static_cast<uint64_t>(oid));
    Put32(mpp, 16, 2);
    Put32(mpp, 20, nposes);
    Put32(mpp, 24, head);
    for (uint64_t w : new_words) Append32(mpp, w);
    Append32(mpp, 0xFFFFFFFFull);
    for (size_t i = 0; i < nposes; ++i) {
      const size_t at = catx::Al(mpp, 16);
      Put32(mpp, 28 + 4 * i, at);
      const auto& pose = mr.poses[i];
      for (size_t r : keep) {
        const double* x = pose.data() + r * 6;
        // the wrist end stays where the forearm expects it: t = max(0.0, min(1.0, (abs(rest[r][0]) - wrist_x - 0.012) / 0.018))
        const double raw = (std::fabs(rest[r][0]) - wrist_x - 0.012) / 0.018;
        const double lo = raw < 1.0 ? raw : 1.0;  // min(1.0, raw): the first wins on a tie, a NaN gives 1.0
        const double t = lo > 0.0 ? lo : 0.0;     // max(0.0, lo)
        const double k_w = t * t * (3 - 2 * t);
        double q[3];
        for (int c = 0; c < 3; ++c) q[c] = rest[r][c] + k_w * (x[c] - rest[r][c]);
        for (int c = 0; c < 3; ++c) AppendF32(mpp, q[c] + delta[c]);
        for (int c = 3; c < 6; ++c) AppendF32(mpp, x[c]);
      }
    }
    out.emplace_back(index, std::move(mpp));
  }
  return out;
}

}  // namespace doa4::suitconv::hands

