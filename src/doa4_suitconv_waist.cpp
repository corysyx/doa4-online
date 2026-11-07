// Suit converter (Plan 2): the waist fit. See doa4_suitconv_waist.h. Line numbers in the comments are physics_port.py's
// (SHA-1 b8b69003c5ae388868ed8e5fff5f50a91263f98a).
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_waist.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>

#include "doa4_suitconv_be.h"
#include "doa4_suitconv_num.h"

namespace doa4::suitconv::waist {

using core::Assert;
using core::Fail;
using core::ReprD;
using core::Vec3;

namespace {

constexpr double kPi = 3.141592653589793;  // np.pi

using Row4 = std::array<double, 4>;

// ndarray.astype(int) of a float64: C's cast to int64, which on x86-64 gives INT64_MIN for NaN and out-of-range values.
// numpy 1.26.3 on Windows makes astype(int) an int32 (C long): there NaN and +-1e10 give -2147483648. The int64 model is
// equivalent here because the two only differ for |v| >= 2^31 or NaN, and every use feeds a small value: the grid
// index (t <= 24, a <= 8, see below) and the seam cell keys floor(q / cell), which differ between the models only for
// coordinates beyond about 6.4e6 m or NaN, which the converter never meets. Behaviour is the int64 one, unchanged.
int64_t AstypeInt(double v) {
  if (!(v >= -9223372036854775808.0 && v < 9223372036854775808.0)) return std::numeric_limits<int64_t>::min();
  return static_cast<int64_t>(v);
}

// int(v) of a numpy float64 (hashed): raises on NaN and infinity.
int64_t PyInt(double v) {
  if (std::isnan(v)) Fail("ValueError: cannot convert float NaN to integer");
  if (std::isinf(v)) Fail("OverflowError: cannot convert float infinity to integer");
  if (std::fabs(v) >= 9.0e18) Fail("not ported yet: a grid key beyond 64 bits");
  return static_cast<int64_t>(v);
}

// numpy .min() / .max() of a float64 column: NaN propagates.
double ColMin(const std::vector<Vec3>& v, int c) {
  double m = v[0][c];
  for (const Vec3& p : v) {
    if (std::isnan(p[c])) return p[c];
    if (p[c] < m) m = p[c];
  }
  return m;
}
double ColMax(const std::vector<Vec3>& v, int c) {
  double m = v[0][c];
  for (const Vec3& p : v) {
    if (std::isnan(p[c])) return p[c];
    if (p[c] > m) m = p[c];
  }
  return m;
}

// np.clip(x, 0.0, 1.0) on a float64: numpy/core/src/umath/clip.cpp, _NPY_MIN(_NPY_MAX(x, 0.0), 1.0) with
// _NPY_MAX(a, b) = isnan(a) ? a : (a > b ? a : b) and _NPY_MIN(a, b) = isnan(a) ? a : (a < b ? a : b).
double Clip01(double x) {
  if (std::isnan(x)) return x;
  const double lo = x > 0.0 ? x : 0.0;
  return lo < 1.0 ? lo : 1.0;
}

struct Cell {
  int64_t k[3];
  bool operator==(const Cell& o) const { return k[0] == o.k[0] && k[1] == o.k[1] && k[2] == o.k[2]; }
};
struct CellHash {
  size_t operator()(const Cell& c) const {
    uint64_t h = 1469598103934665603ull;
    for (int64_t v : c.k) h = (h ^ static_cast<uint64_t>(v)) * 1099511628211ull;
    return static_cast<size_t>(h ^ (h >> 29));
  }
};
// Membership only: no iteration order reaches any output.
using CellSet = std::unordered_set<Cell, CellHash>;

// hashed(pts): {tuple(int(v) for v in k) for k in np.floor(pts / cell)}  (line 662). np.floor(pts / cell) is numpy's
// elementwise division and floor, not Python's float //.
CellSet Hashed(const std::vector<Vec3>& pts, double cell) {
  CellSet s;
  for (const Vec3& p : pts) s.insert(Cell{{PyInt(std::floor(p[0] / cell)), PyInt(std::floor(p[1] / cell)),
                                          PyInt(std::floor(p[2] / cell))}});
  return s;
}

// on_seam(q, table)  (line 665): k = np.floor(q / cell).astype(int); any of the 27 neighbouring cells in the table.
bool OnSeam(const Vec3& q, double cell, const CellSet& table) {
  int64_t k[3];
  for (int i = 0; i < 3; ++i) k[i] = AstypeInt(std::floor(q[i] / cell));
  auto add = [](int64_t a, int64_t d) { return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(d)); };
  for (int a = -1; a <= 1; ++a)
    for (int b = -1; b <= 1; ++b)
      for (int c = -1; c <= 1; ++c)
        if (table.count(Cell{{add(k[0], a), add(k[1], b), add(k[2], c)}})) return true;
  return false;
}

struct Samples {
  std::vector<Vec3> pn;   // world positions
  std::vector<Row4> wn;   // the weights, each row divided by its sum
  Vec3 joint{};           // place[oid]
};

// _waist_samples(ref, chest, pelvis)  (lines 585-609): the template's 4-weight waist.
Samples WaistSamples(const core::Model& ref, int64_t chest, int64_t pelvis) {
  const auto W = tpr::WorldMats(ref.hie);
  core::PyDict<int64_t, Vec3> place;
  for (size_t i = 0; i < ref.hie.size(); ++i) place.Set(ref.hie[i].id, Vec3{W[i][12], W[i][13], W[i][14]});
  const std::span<const uint8_t> data(ref.x.data.data(), ref.x.data.size());
  const size_t nobj = std::min(ref.geo.size(), ref.info.size());  // zip
  for (size_t oid = 0; oid < nobj; ++oid) {
    const auto& g = ref.geo[oid];
    const auto& inf = ref.info[oid];
    {  // list(inf.palette)[:2] != [chest, pelvis]
      const auto& pal = inf.palette;
      if (pal.size() < 2 || static_cast<int64_t>(pal[0]) != chest || static_cast<int64_t>(pal[1]) != pelvis) continue;
    }
    std::vector<Vec3> P;
    std::vector<Row4> Q;
    for (const tpr::Decl& dc : g.decls) {
      std::vector<tpr::DeclElem> e, pes;
      for (size_t i = 0; i + 1 < dc.elems.size(); ++i) {  // dc.elems[:-1]
        const tpr::DeclElem& x = dc.elems[i];
        if (x.a == 0 && x.e == 1) e.push_back(x);
        if (x.a == 0 && x.e == 0 && x.f == 0) pes.push_back(x);
      }
      if (e.empty()) continue;
      const tpr::DtEntry* dt = tpr::DtFind(e[0].c);
      if (!dt) Fail("KeyError: " + std::to_string(e[0].c));
      if (dt->name != "H4" || dc.head[5] < 20) continue;
      if (dc.Vb() >= ref.vbs.size()) Fail("IndexError: list index out of range");
      const auto [b, size] = tpr::BufSpan(ref.vbs[dc.Vb()]);
      (void)size;
      if (pes.empty()) Fail("IndexError: list index out of range");
      const tpr::DeclElem& pe = pes[0];
      for (uint32_t j = 0; j < dc.head[5]; ++j) {
        const int64_t o = static_cast<int64_t>(b) + static_cast<int64_t>(j) * dc.Stride();
        const tpr::ElemValue pv = tpr::DecodeElem(data, o + pe.b, pe.c);
        if (pv.n < 3) Fail("ValueError: setting an array element with a sequence. The requested array has an "
                           "inhomogeneous shape");
        P.push_back(Vec3{pv.v[0], pv.v[1], pv.v[2]});
        const tpr::ElemValue qv = tpr::DecodeElem(data, o + e[0].b, e[0].c);
        if (qv.n != 4) Fail("ValueError: setting an array element with a sequence. The requested array has an "
                            "inhomogeneous shape");
        Q.push_back(Row4{qv.v[0], qv.v[1], qv.v[2], qv.v[3]});
      }
    }
    if (!P.empty()) {
      const Vec3* pl = place.Find(static_cast<int64_t>(oid));
      if (!pl) Fail("KeyError: " + std::to_string(oid));
      Samples s;
      s.joint = *pl;
      for (const Vec3& p : P) s.pn.push_back(Vec3{p[0] + (*pl)[0], p[1] + (*pl)[1], p[2] + (*pl)[2]});
      for (const Row4& q : Q) {
        const double sum = num::NpSumLastAxis(std::span<const double>(q.data(), 4));  // Q.sum(1, keepdims=True)
        s.wn.push_back(Row4{q[0] / sum, q[1] / sum, q[2] / sum, q[3] / sum});         // Q / sum, elementwise
      }
      return s;
    }
  }
  Fail("no 4-weight waist in the reference");  // AssertionError('no 4-weight waist in the reference')
}

}  // namespace

std::optional<int64_t> PrepWaistFit(const core::Model& ref, int64_t t_chest, int64_t t_pelvis,
                                    std::vector<std::optional<core::Object>>& objects,
                                    const std::vector<core::Part*>& parts, const std::vector<tpr::Node>& nodes,
                                    const core::PyDict<int64_t, int64_t>& chan2id, double seam_tol) {
  const int64_t chest = chan2id.At(0x9000), pelvis = chan2id.At(0x9002);
  const Samples tm = WaistSamples(ref, t_chest, t_pelvis);
  const std::vector<Vec3>& Pn0 = tm.pn;
  const std::vector<Row4>& Wn = tm.wn;

  std::vector<int64_t> candidates;
  for (size_t oid = 0; oid < objects.size(); ++oid)
    if (objects[oid] && objects[oid]->pal == std::vector<int64_t>{chest, pelvis})
      candidates.push_back(static_cast<int64_t>(oid));
  if (candidates.size() != 1) return std::nullopt;  // "waist fit: SKIPPED"
  const int64_t target = candidates[0];
  const std::vector<core::Part*>& ps = objects[static_cast<size_t>(target)]->ps;

  std::vector<Vec3> Pc;  // np.array([v[0] for p in ps for v in p.verts])
  for (const core::Part* p : ps)
    for (const core::Vert& v : p->verts) Pc.push_back(v.pos);
  const auto W4 = tpr::WorldMats(nodes);
  Vec3 joint{};
  {
    size_t at = nodes.size();
    for (size_t i = 0; i < nodes.size(); ++i)
      if (nodes[i].id == target) {
        at = i;
        break;
      }
    if (at == nodes.size()) Fail("IndexError: list index out of range");
    joint = Vec3{W4[at][12], W4[at][13], W4[at][14]};
  }
  std::vector<Vec3> Pn;  // Pn - t_joint + joint
  Pn.reserve(Pn0.size());
  for (const Vec3& p : Pn0) {
    Vec3 q;
    for (int c = 0; c < 3; ++c) q[c] = (p[c] - tm.joint[c]) + joint[c];
    Pn.push_back(q);
  }
  if (Pc.empty()) Fail("IndexError: too many indices for array: array is 1-dimensional, but 2 were indexed");
  const double zc = 0.5 * (ColMin(Pc, 2) + ColMax(Pc, 2));
  // ang = lambda P: np.arctan2(np.abs(P[:, 0]), P[:, 2] - zc)   0 = front, pi = back
  // Known gap: numpy's arctan2 and exp on this PC are the C runtime's. The kit's musl Atan2 and Exp are within 1 ulp of
  // them, not bit-equal, so the weights' doubles differ in the last bits for some vertices; after the round to half
  // the .tpr and .cat bytes are equal for all 34 suits, the trace's `B p<i>.h4` hashes are not. The test checks the
  // doubles themselves against a sidecar of the Python's h4 values (see tests/host/suitconv_convert_host_test.cpp).
  auto ang = [&](const Vec3& p) { return num::Atan2(std::fabs(p[0]), p[2] - zc); };

  // 1. the template, from the reference
  const double lo = ColMin(Pn, 1), hi = ColMax(Pn, 1);
  std::vector<double> an;
  for (const Vec3& p : Pn) an.push_back(ang(p));
  const std::vector<double> ys = num::NpLinspace(lo, hi, 25);
  const std::vector<double> angs = num::NpLinspace(0.0, kPi, 9);
  std::vector<Row4> grid(ys.size() * angs.size(), Row4{0.0, 0.0, 0.0, 0.0});
  const double sy = 0.0075, sa = num::NpRadians(22.0);
  const size_t n = Pn.size();
  std::vector<double> g(n), wg(n * 4);
  for (size_t i = 0; i < ys.size(); ++i) {
    for (size_t j = 0; j < angs.size(); ++j) {
      for (size_t k = 0; k < n; ++k) {
        const double ay = (Pn[k][1] - ys[i]) / sy, aa = (an[k] - angs[j]) / sa;
        g[k] = num::Exp(-0.5 * (ay * ay + aa * aa)) + 1e-12;
        for (int c = 0; c < 4; ++c) wg[k * 4 + c] = Wn[k][c] * g[k];
      }
      double col[4];
      num::NpSumAxis0(wg, 4, col);
      const double gs = num::NpSum1D(g);
      grid[i * angs.size() + j] = Row4{col[0] / gs, col[1] / gs, col[2] / gs, col[3] / gs};
    }
  }
  for (size_t j = 0; j < angs.size(); ++j) {
    grid[j] = Row4{0.0, 1.0, 0.0, 0.0};
    grid[(ys.size() - 1) * angs.size() + j] = Row4{1.0, 0.0, 0.0, 0.0};
  }

  // 2. the seams of this mesh
  const double cell = seam_tol * 2;
  std::vector<Vec3> pc_pts, pp_pts;
  for (const core::Part* p : parts) {
    if (p->owner == chest)
      for (const core::Vert& v : p->verts) pc_pts.push_back(v.pos);
    if (p->owner == pelvis)
      for (const core::Vert& v : p->verts) pp_pts.push_back(v.pos);
  }
  const CellSet seam_c = Hashed(pc_pts, cell);
  const CellSet seam_p = Hashed(pp_pts, cell);
  std::vector<char> on_c, on_p;
  for (const Vec3& q : Pc) on_c.push_back(OnSeam(q, cell, seam_c));
  for (const Vec3& q : Pc) on_p.push_back(OnSeam(q, cell, seam_p));
  // the band comes from the body's waist alone (parts list the body first) ...
  if (ps.empty()) Fail("IndexError: list index out of range");
  const size_t n_body = ps[0]->verts.size();
  const core::Model* body_model = ps[0]->model;
  auto only_body = [&](int64_t oid) {
    std::vector<Vec3> pts;
    for (const core::Part* p : parts)
      if (p->owner == oid && p->model == body_model)
        for (const core::Vert& v : p->verts) pts.push_back(v.pos);
    return Hashed(pts, cell);
  };
  const CellSet ring_c = only_body(chest), ring_p = only_body(pelvis);
  std::vector<double> body_c_y, body_p_y;
  for (size_t i = 0; i < std::min(n_body, Pc.size()); ++i) {  // Pc[:n_body]
    if (OnSeam(Pc[i], cell, ring_c)) body_c_y.push_back(Pc[i][1]);
    if (OnSeam(Pc[i], cell, ring_p)) body_p_y.push_back(Pc[i][1]);
  }
  const double top = !body_c_y.empty() ? num::NpPercentileLinear(body_c_y, 10) : hi;
  const double bottom = !body_p_y.empty() ? num::NpPercentileLinear(body_p_y, 90) : lo;
  // min(hi, top) and max(lo, bottom): the first argument wins unless the second is strictly smaller / larger
  const double band_hi = top < hi ? top : hi;
  const double band_lo = bottom > lo ? bottom : lo;
  Assert(band_hi - band_lo > 0.05, "('waist band too short', " + ReprD(band_lo) + ", " + ReprD(band_hi) + ")");

  // 3. this mesh's weights
  size_t i0 = 0;
  const int64_t ny = static_cast<int64_t>(ys.size()), na = static_cast<int64_t>(angs.size());
  for (core::Part* p : ps) {
    const size_t cnt = p->verts.size();
    std::vector<Row4> h4;
    h4.reserve(cnt);
    for (size_t r = 0; r < cnt; ++r) {
      const Vec3& Q = Pc[i0 + r];
      const bool c = on_c[i0 + r] != 0, pv = on_p[i0 + r] != 0;
      const double t = Clip01((Q[1] - band_lo) / (band_hi - band_lo)) * static_cast<double>(ny - 1);
      const double a = ang(Q) / kPi * static_cast<double>(na - 1);
      const int64_t i1 = std::min(AstypeInt(t), ny - 2);
      const int64_t j1 = std::min(AstypeInt(a), na - 2);
      if (i1 < 0 || j1 < 0) Fail("IndexError: index " + std::to_string(i1 < 0 ? i1 : j1) + " is out of bounds");
      const double ft = t - static_cast<double>(i1), fa = a - static_cast<double>(j1);
      const Row4& g00 = grid[static_cast<size_t>(i1 * na + j1)];
      const Row4& g10 = grid[static_cast<size_t>((i1 + 1) * na + j1)];
      const Row4& g01 = grid[static_cast<size_t>(i1 * na + j1 + 1)];
      const Row4& g11 = grid[static_cast<size_t>((i1 + 1) * na + j1 + 1)];
      // ((1 - ft) * (1 - fa) * g00 + ft * (1 - fa) * g10) + (1 - ft) * fa * g01) + ft * fa * g11, each product left to right
      const double c00 = (1 - ft) * (1 - fa), c10 = ft * (1 - fa), c01 = (1 - ft) * fa, c11 = ft * fa;
      Row4 w;
      for (int k = 0; k < 4; ++k) w[k] = Clip01(((c00 * g00[k] + c10 * g10[k]) + c01 * g01[k]) + c11 * g11[k]);
      if (c && !pv) w = Row4{1.0, 0.0, 0.0, 0.0};
      if (pv && !c) w = Row4{0.0, 1.0, 0.0, 0.0};
      const double sum = num::NpSumLastAxis(std::span<const double>(w.data(), 4));  // w.sum(1, keepdims=True)
      h4.push_back(Row4{w[0] / sum, w[1] / sum, w[2] / sum, w[3] / sum});
    }
    i0 += cnt;
    p->h4 = std::move(h4);
  }
  return target;
}

}  // namespace doa4::suitconv::waist
