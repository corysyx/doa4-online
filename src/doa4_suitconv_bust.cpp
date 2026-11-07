// Suit converter (Plan 2): the bust channel. See doa4_suitconv_bust.h. Line numbers in the comments are physics_port.py's
// (SHA-1 b8b69003c5ae388868ed8e5fff5f50a91263f98a).
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_bust.h"

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>

#include "doa4_suitconv_be.h"
#include "doa4_suitconv_catx.h"
#include "doa4_suitconv_num.h"

namespace doa4::suitconv::bust {

using core::Fail;
using core::Vec3;

namespace {

using Offsets = std::vector<Vec3>;

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

// int(v // cell): float floor division, then int().
int64_t CellOf(double v, double cell) {
  const double q = num::PyFloorDiv(v, cell);
  if (std::isnan(q)) Fail("ValueError: cannot convert float NaN to integer");
  if (std::isinf(q)) Fail("OverflowError: cannot convert float infinity to integer");
  if (std::fabs(q) >= 9.0e18) Fail("not ported yet: a grid key beyond 64 bits");
  return static_cast<int64_t>(q);
}
Cell KeyOf(const Vec3& pos, double cell) {
  return Cell{{CellOf(pos[0], cell), CellOf(pos[1], cell), CellOf(pos[2], cell)}};
}

// math.dist(v, (0, 0, 0)) at physics_port.py 138 (build_mpv's threshold): CPython's vector_norm, num::MathDist3. (Plan row 8
// had it as sqrt of a PySum of squares; the cloth census showed that is not what math.dist computes: 3887 to 7034 bit
// mismatches over the five authored-cloth suits' calls.)
double Norm(const Vec3& v) { return num::MathDist3(v, Vec3{0.0, 0.0, 0.0}); }

// float(np.sqrt(((arr - np.array(pos)) ** 2).sum(1).min())): the row sums are numpy's last-axis sums; min propagates NaN.
double NearestDist(const std::vector<Vec3>& arr, const Vec3& pos) {
  double best = std::numeric_limits<double>::infinity();
  bool first = true, nan = false;
  for (const Vec3& q : arr) {
    const double sq[3] = {(q[0] - pos[0]) * (q[0] - pos[0]), (q[1] - pos[1]) * (q[1] - pos[1]),
                          (q[2] - pos[2]) * (q[2] - pos[2])};
    const double s = num::NpSumLastAxis(std::span<const double>(sq, 3));
    if (std::isnan(s)) nan = true;
    if (first || s < best) best = s;
    first = false;
  }
  if (nan) best = std::numeric_limits<double>::quiet_NaN();
  return std::sqrt(best);
}

// off[t] = tuple(k_f * v for v in off[t][j]) for both targets, with k_f = t * t * (3 - 2 * t), t = d / range.
void Ease(Offsets& a, Offsets& b, size_t j, double d, double range) {
  const double t = core::PyDiv(d, range);
  const double k_f = t * t * (3 - 2 * t);
  for (int c = 0; c < 3; ++c) {
    a[j][c] = k_f * a[j][c];
    b[j][c] = k_f * b[j][c];
  }
}

template <class T>
const T& At(const std::vector<T>& v, int64_t i, const char* what) {
  if (i < 0 || i >= static_cast<int64_t>(v.size())) Fail(std::string("IndexError: ") + what + " index out of range");
  return v[static_cast<size_t>(i)];
}

const core::Object& ObjectAt(const std::vector<std::optional<core::Object>>& objects, int64_t i) {
  const auto& o = At(objects, i, "objects");
  if (!o) Fail("TypeError: cannot unpack non-iterable NoneType object");
  return *o;
}

void Put32(std::vector<uint8_t>& b, size_t at, uint64_t v) {
  if (v > 0xFFFFFFFFull) Fail("struct.error: argument out of range");
  if (at + 4 > b.size()) Fail("struct.error: pack_into requires a buffer of at least " + std::to_string(at + 4) + " bytes");
  for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * (3 - i)));
}
void Put32F(std::vector<uint8_t>& b, size_t at, double x) { Put32(b, at, std::bit_cast<uint32_t>(num::PackF32(x))); }
void Append32(std::vector<uint8_t>& b, uint64_t v) {
  b.resize(b.size() + 4);
  Put32(b, b.size() - 4, v);
}
void Append32F(std::vector<uint8_t>& b, double x) {
  b.resize(b.size() + 4);
  Put32F(b, b.size() - 4, x);
}

}  // namespace

// ------------------------------------------------------------------ prep_bust (lines 28-121)
int64_t PrepBust(std::vector<std::optional<core::Object>>& objects, const std::vector<core::Part*>& parts,
                 const core::PyDict<int64_t, int64_t>& chan2id, double down_scale, double up_scale,
                 const std::set<int64_t>& cloth_objs, const core::Model* cloth_model, double strap_fade, double tol,
                 double fade) {
  const int64_t chest = chan2id.At(0x9000);
  if (chest < 0 || chest >= static_cast<int64_t>(objects.size())) Fail("IndexError: list index out of range");
  core::Assert(objects[static_cast<size_t>(chest)].has_value(), "no chest object");
  const std::vector<core::Part*>& ps = objects[static_cast<size_t>(chest)]->ps;
  const double cell = tol * 2;
  std::unordered_set<Cell, CellHash> grid;
  for (const core::Part* p : parts) {
    if (p->owner == chest) continue;
    for (const core::Vert& v : p->verts) grid.insert(KeyOf(v.pos, cell));
  }
  // a cloth piece on the chest takes no morph (cloth_objs, --port-cloth): the body under it fades near the cloth
  auto is_cloth = [&](const core::Part* part) { return part->model == cloth_model && cloth_objs.count(part->obj_id) != 0; };
  std::vector<Vec3> cloth_arr;
  for (const core::Part* p : ps)
    if (is_cloth(p))
      for (const core::Vert& v : p->verts) cloth_arr.push_back(v.pos);
  // A suit's strap belongs to the shoulder object, so the chest eases to rest around it.
  std::vector<Vec3> strap_arr;
  if (strap_fade > 0)
    for (const core::Part* p : parts)
      if (p->owner != chest && p->model == cloth_model)
        for (const core::Vert& v : p->verts) strap_arr.push_back(v.pos);
  for (size_t k = 0; k < ps.size(); ++k) {
    core::Part* p = ps[k];
    if (p->native || !p->model || p->model->geo.size() <= static_cast<size_t>(p->obj_id) ||
        p->model->geo[static_cast<size_t>(p->obj_id)].decls.size() <= static_cast<size_t>(p->decl_index))
      continue;  // p.native is not None or p.decl_src is None
    const tpr::Decl& dc = p->model->geo[static_cast<size_t>(p->obj_id)].decls[static_cast<size_t>(p->decl_index)];
    if (dc.head[1] != 7) continue;
    const std::vector<uint8_t>& data = p->model->x.data;
    const int64_t n = dc.head[5];
    const auto [base, bsize] = tpr::BufSpan(At(p->model->vbs, dc.Vb(), "vbs"));
    (void)bsize;
    const tpr::DeclElem* pe = nullptr;
    for (size_t i = 0; i + 1 < dc.elems.size(); ++i) {
      const tpr::DeclElem& e = dc.elems[i];
      if (e.a == 0 && e.e == 0 && e.f == 0) {
        pe = &e;
        break;
      }
    }
    if (!pe) Fail("IndexError: list index out of range");
    const be::Reader rd(std::span<const uint8_t>(data.data(), data.size()));
    std::vector<Vec3> P;
    for (int64_t j = 0; j < n; ++j) {
      const int64_t o = static_cast<int64_t>(base) + j * dc.Stride() + pe->b;
      P.push_back(Vec3{rd.F32(o), rd.F32(o + 4), rd.F32(o + 8)});
    }
    Offsets off[7];  // off[5] up, off[6] down
    for (int t : {5, 6}) {
      const tpr::Bytes& tail = At(dc.tails, t, "tails");
      const be::Reader tr(std::span<const uint8_t>(tail.data(), tail.size()));
      const uint32_t vb = tr.U32(0), st = tr.U32(4);
      core::Assert(st == 12, "('morph buffer stride', " + std::to_string(st) + ")");
      const auto [b, bs] = tpr::BufSpan(At(p->model->vbs, vb, "vbs"));
      (void)bs;
      const double scale = t == 5 ? up_scale : down_scale;
      for (int64_t j = 0; j < n; ++j) {
        const int64_t o = static_cast<int64_t>(b) + j * 12;
        const Vec3 q{rd.F32(o), rd.F32(o + 4), rd.F32(o + 8)};
        off[t].push_back(Vec3{scale * (q[0] - P[j][0]), scale * (q[1] - P[j][1]), scale * (q[2] - P[j][2])});
      }
    }
    // buffer 5 must be up and buffer 6 down on this file, or the channel would run backwards
    num::PySum us, ds;
    for (const Vec3& v : off[5]) us.Add(v[1]);
    for (const Vec3& v : off[6]) ds.Add(v[1]);
    const double up_y = us.Result(), down_y = ds.Result();
    if (!(up_y > 0 && 0 > down_y)) {
      // a piece the source does not move (a strap, say): it stays still with the bones. The Python takes max() of
      // the offsets' lengths here for its report, which raises on an empty decl.
      if (off[5].empty() && off[6].empty()) Fail("ValueError: max() arg is an empty sequence");
      continue;
    }
    if (!cloth_arr.empty() && fade > 0 && p->model != cloth_model) {
      for (size_t j = 0; j < p->verts.size(); ++j) {
        const double d = NearestDist(cloth_arr, p->verts[j].pos);
        if (d < fade) {
          if (j >= off[5].size()) Fail("IndexError: list index out of range");
          Ease(off[5], off[6], j, d, fade);
        }
      }
    }
    if (strap_fade > 0 && !strap_arr.empty()) {
      for (size_t j = 0; j < p->verts.size(); ++j) {
        const double d = NearestDist(strap_arr, p->verts[j].pos);
        if (d < strap_fade) {
          if (j >= off[5].size()) Fail("IndexError: list index out of range");
          Ease(off[5], off[6], j, d, strap_fade);
        }
      }
    }
    p->border = std::set<int64_t>();
    for (size_t j = 0; j < p->verts.size(); ++j) {
      const Cell key = KeyOf(p->verts[j].pos, cell);
      bool hit = false;
      for (int a = -1; a <= 1 && !hit; ++a)
        for (int b2 = -1; b2 <= 1 && !hit; ++b2)
          for (int c2 = -1; c2 <= 1 && !hit; ++c2)
            hit = grid.count(Cell{{key.k[0] + a, key.k[1] + b2, key.k[2] + c2}}) != 0;
      if (hit) {
        if (j >= off[5].size()) Fail("IndexError: list index out of range");
        off[5][j] = off[6][j] = Vec3{0.0, 0.0, 0.0};
        p->border->insert(static_cast<int64_t>(j));
      }
    }
    p->bust = std::make_pair(std::move(off[6]), std::move(off[5]));
    p->writable = true;
  }
  return chest;
}

// ------------------------------------------------------------------ bust_peak (lines 971-982)
std::optional<std::array<double, 2>> BustPeak(const std::vector<std::optional<core::Object>>& objects, int64_t chest,
                                              const core::PyDict<int64_t, Vec3>& bind4) {
  const Vec3& own = bind4.At(chest);
  std::optional<std::array<double, 2>> best;
  for (const core::Part* p : ObjectAt(objects, chest).ps) {
    if (!p->bust || p->band) continue;
    for (const core::Vert& v : p->verts) {
      const double z = v.pos[2] - own[2];
      if (!best || z > (*best)[1]) best = std::array<double, 2>{v.pos[1] - own[1], z};
    }
  }
  return best;
}

// ------------------------------------------------------------------ build_mpv (lines 124-174)
std::vector<uint8_t> BuildMpv(const std::vector<std::optional<core::Object>>& objects,
                              const std::vector<tpr::ObjGeo>& geos, const core::PyDict<int64_t, Vec3>& bind4,
                              int64_t chest, double thresh, double full_down, double full_up,
                              const std::map<int64_t, std::vector<uint8_t>>* extra, const std::vector<PinRow>* pins) {
  const std::vector<core::Part*>& ps = ObjectAt(objects, chest).ps;
  const Vec3& own = bind4.At(chest);
  std::vector<uint64_t> words;
  std::vector<Vec3> rest, down, up;
  for (size_t k = 0; k < ps.size(); ++k) {
    const core::Part* p = ps[k];
    if (!p->bust) continue;
    const tpr::ObjGeo& g = At(geos, chest, "geos");
    const tpr::Decl& dcl = At(g.decls, static_cast<int64_t>(k), "decls");
    const uint32_t stride = dcl.Stride();
    core::Assert(!dcl.elems.empty() && dcl.elems[0].b == 0, "position is not first");
    const auto& dn = p->bust->first;
    const auto& u = p->bust->second;
    for (size_t j = 0; j < p->verts.size(); ++j) {
      if (j >= dn.size() || j >= u.size()) Fail("IndexError: list index out of range");
      const double m1 = Norm(dn[j]), m2 = Norm(u[j]);
      if ((m2 > m1 ? m2 : m1) < thresh) continue;  // max(a, b): the first wins on a tie, equal values either way
      const uint64_t at = (static_cast<uint64_t>(k) << 24) | (static_cast<uint64_t>(j) * stride);
      words.push_back(at | (words.empty() ? 0 : 0x80000000ull));
      const Vec3& pos = p->verts[j].pos;
      const Vec3 lp{pos[0] - own[0], pos[1] - own[1], pos[2] - own[2]};
      rest.push_back(lp);
      down.push_back(Vec3{lp[0] + dn[j][0], lp[1] + dn[j][1], lp[2] + dn[j][2]});
      up.push_back(Vec3{lp[0] + u[j][0], lp[1] + u[j][1], lp[2] + u[j][2]});
    }
  }
  core::Assert(!words.empty(), "no bust vertex");
  std::vector<uint8_t> mpp(0x50, 0);
  mpp[0] = 'M';
  mpp[1] = 'P';
  mpp[2] = 'P';
  Put32(mpp, 4, 1);
  Put32(mpp, 12, static_cast<uint64_t>(chest));
  Put32(mpp, 16, 0);
  Put32(mpp, 20, 3);
  Put32(mpp, 24, 0x50);
  Put32(mpp, 0x28, 0x44583242);
  Put32(mpp, 0x2C, words.size());
  Put32F(mpp, 0x30, 1.0);
  Put32F(mpp, 0x34, full_down);
  Put32F(mpp, 0x38, 1.0);
  Put32F(mpp, 0x3C, full_up);
  for (uint64_t w : words) Append32(mpp, w);
  Append32(mpp, 0xFFFFFFFFull);
  const std::vector<Vec3>* poses[3] = {&rest, &down, &up};
  for (int i = 0; i < 3; ++i) {
    const size_t at = catx::Al(mpp, 16);
    Put32(mpp, 28 + 4 * i, at);
    for (const Vec3& v : *poses[i])
      for (double x : v) Append32F(mpp, x);
  }
  if (pins && !pins->empty()) {
    const size_t at = catx::Al(mpp, 16);
    Put32(mpp, 0x40, at);
    Put32(mpp, 0x44, pins->size());
    for (const PinRow& r : *pins) {
      Append32(mpp, static_cast<uint32_t>(r.rel));
      for (const auto* a : {&r.rest, &r.down, &r.up})
        for (double x : *a) Append32F(mpp, x);
    }
  }
  std::map<int64_t, std::vector<uint8_t>> channels;
  if (extra) channels = *extra;
  channels[4] = mpp;
  std::vector<uint8_t> mpv(0x40, 0);
  mpv[0] = 'M';
  mpv[1] = 'P';
  mpv[2] = 'V';
  Put32(mpv, 4, 1);
  Put32(mpv, 12, 5);
  Put32(mpv, 32, 5);
  for (const auto& [index, blob] : channels) {  // sorted(channels)
    const size_t at = catx::Al(mpv, 16);
    if (index < 0) Fail("struct.error: pack_into offset out of range");
    Put32(mpv, 36 + 4 * static_cast<size_t>(index), at);
    mpv.insert(mpv.end(), blob.begin(), blob.end());
  }
  return mpv;
}

}  // namespace doa4::suitconv::bust
