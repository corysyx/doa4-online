// Suit converter (Plan 2): the suit's own authored cloth. See doa4_suitconv_cloth.h. Line numbers in the comments are
// physics_port.py's (SHA-1 b8b69003c5ae388868ed8e5fff5f50a91263f98a).
//
// Numeric paths, each chosen by a census against the recipe's interpreter on the five authored-cloth suits' real inputs
// (numpy 1.26.3, OpenBLAS 0.3.23 Cooperlake kernels, CPython 3.12.1):
//   np.linalg.norm of a 1-D 3-vector    sqrt(x.dot(x)), a BLAS ddot: num::NpDotFused (0 mismatches at every site)
//   np.linalg.norm(..., axis=1)         sqrt(add.reduce(x*x, axis=1)): num::NpSumLastAxis of the squares (0 mismatches)
//   (P - p0) @ uu, P of N > 1 rows      cblas_dgemv: Gemv3 (0 mismatches over 1.09 million rows); N == 1 is numpy's
//                                       scalar_out path, a ddot (num::NpDotFused)
//   math.dist                           CPython's vector_norm: MathDist (0 mismatches)
//   x ** 0.5                            std::sqrt (0 mismatches)
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_cloth.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "doa4_suitconv_be.h"
#include "doa4_suitconv_num.h"
#include "doa4_suitconv_sort.h"

namespace doa4::suitconv::cloth {

using core::Assert;
using core::Fail;
using core::Vec3;

namespace {

using Objects = std::vector<std::optional<core::Object>>;
using Blob = std::vector<uint8_t>;

std::string Hex(int64_t v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%s0x%llx", v < 0 ? "-" : "", static_cast<unsigned long long>(v < 0 ? -v : v));
  return buf;
}
std::string Str(int64_t v) { return std::to_string(v); }

// Python's max(a, b) / min(a, b) for floats: the first argument wins unless the second is strictly greater / less.
double PyMax(double a, double b) { return b > a ? b : a; }
double PyMin(double a, double b) { return b < a ? b : a; }
double Step(double a) { return a * a * (3 - 2 * a); }
double Clamp01(double a) { return PyMin(PyMax(a, 0.0), 1.0); }

const core::Object& Obj(const Objects& objects, int64_t i) {
  if (i < 0 || i >= static_cast<int64_t>(objects.size())) Fail("IndexError: list index out of range");
  if (!objects[static_cast<size_t>(i)]) Fail("TypeError: 'NoneType' object is not subscriptable");
  return *objects[static_cast<size_t>(i)];
}

be::Reader Rd(const Blob& b) { return be::Reader(std::span<const uint8_t>(b.data(), b.size())); }

// d[a:a+len(s)] == s
bool SliceIs(const Blob& d, int64_t a, const char* s) {
  const size_t n = std::char_traits<char>::length(s);
  if (a < 0 || static_cast<uint64_t>(a) + n > d.size()) return false;
  return std::equal(s, s + n, d.begin() + a);
}

Vec3 F3(const Blob& b, int64_t o) {
  const std::vector<uint32_t> w = Rd(b).U32s(o, 3);  // all three or none, as unpack_from('>3f')
  return Vec3{double(std::bit_cast<float>(w[0])), double(std::bit_cast<float>(w[1])), double(std::bit_cast<float>(w[2]))};
}
int32_t I32(const Blob& b, int64_t o) { return static_cast<int32_t>(Rd(b).U32(o)); }

// struct.pack_into('>3f' / '>f', b, o, ...): every value is converted (num::PackF32 raises on overflow) before a byte
// is written.
void PutF3(Blob& b, int64_t o, const Vec3& v) {
  uint32_t w[3];
  for (int c = 0; c < 3; ++c) w[c] = std::bit_cast<uint32_t>(num::PackF32(v[c]));
  const size_t p = be::ResolveOffset(o, b.size(), 12);
  for (int c = 0; c < 3; ++c)
    for (int i = 0; i < 4; ++i) b[p + 4 * c + i] = static_cast<uint8_t>(w[c] >> (8 * (3 - i)));
}
void PutF(Blob& b, int64_t o, double x) { be::PackIntoU32(b, o, std::bit_cast<uint32_t>(num::PackF32(x))); }
void PutI32(Blob& b, int64_t o, int64_t v) {
  if (v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max())
    Fail("struct.error: 'i' format requires -2147483648 <= number <= 2147483647");
  be::PackIntoU32(b, o, static_cast<uint32_t>(static_cast<int32_t>(v)));
}

// ((rows - x) ** 2).sum(1) for one row (numpy's elementwise square, then the last-axis add reduction).
double SqDist(const Vec3& a, const Vec3& x) {
  const double s[3] = {(a[0] - x[0]) * (a[0] - x[0]), (a[1] - x[1]) * (a[1] - x[1]), (a[2] - x[2]) * (a[2] - x[2])};
  return num::NpSumLastAxis(std::span<const double>(s, 3));
}
double SqNorm(const Vec3& v) {
  const double s[3] = {v[0] * v[0], v[1] * v[1], v[2] * v[2]};
  return num::NpSumLastAxis(std::span<const double>(s, 3));
}
std::vector<double> SqDists(const std::vector<Vec3>& rows, const Vec3& x) {
  std::vector<double> d(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) d[i] = SqDist(rows[i], x);
  return d;
}
// arr.min() of a float64 array: NaN if any element is NaN; ValueError on an empty array.
double NpMin(const std::vector<double>& v) {
  if (v.empty()) Fail("ValueError: zero-size array to reduction operation minimum which has no identity");
  double m = v[0];
  for (double x : v) {
    if (std::isnan(x)) return x;
    if (x < m) m = x;
  }
  return m;
}
double NpMax(const std::vector<double>& v) {
  if (v.empty()) Fail("ValueError: zero-size array to reduction operation maximum which has no identity");
  double m = v[0];
  for (double x : v) {
    if (std::isnan(x)) return x;
    if (x > m) m = x;
  }
  return m;
}
// float(np.linalg.norm(v)) of a 3-vector: a BLAS ddot (census: 0 mismatches at lines 326, 1135, 1159, 1217, 1246).
double Norm1D(const Vec3& v) { return std::sqrt(num::NpDotFused(v.data(), v.data(), 3)); }

std::vector<int64_t> First6(std::vector<int64_t> order) {
  if (order.size() > 6) order.resize(6);
  return order;
}

// (vals[order] * wt[:, None]).sum(0) for k = len(order) rows and len(wt) weights: numpy broadcasts (k, 3) * (w, 1)
// when k == w, or when k == 1 (the row repeats); any other pair is a ValueError.
Vec3 WeightedSum(const std::vector<Vec3>& vals, const std::vector<int64_t>& order, const std::vector<double>& wt) {
  const size_t k = order.size(), w = wt.size();
  if (k != w && k != 1 && w != 1)
    Fail("ValueError: operands could not be broadcast together with shapes (" + Str(int64_t(k)) + ",3) (" +
         Str(int64_t(w)) + ",1) ");
  const size_t rows = std::max(k, w);
  std::vector<double> m(rows * 3);
  for (size_t i = 0; i < rows; ++i) {
    const int64_t o = order[k == 1 ? 0 : i];
    if (o < 0 || o >= static_cast<int64_t>(vals.size())) Fail("IndexError: index out of bounds");
    const Vec3& v = vals[static_cast<size_t>(o)];
    const double x = wt[w == 1 ? 0 : i];
    for (int c = 0; c < 3; ++c) m[3 * i + c] = v[c] * x;
  }
  Vec3 out{};
  num::NpSumAxis0(std::span<const double>(m.data(), m.size()), 3, std::span<double>(out.data(), 3));
  return out;
}

// part.decl_src: the source decl a decoded part came from.
const tpr::Decl& DeclSrc(const core::Part& p) {
  if (!p.model || p.obj_id < 0 || static_cast<size_t>(p.obj_id) >= p.model->geo.size() || p.decl_index < 0 ||
      static_cast<size_t>(p.decl_index) >= p.model->geo[static_cast<size_t>(p.obj_id)].decls.size())
    Fail("AttributeError: 'NoneType' object has no attribute 'stride'");
  return p.model->geo[static_cast<size_t>(p.obj_id)].decls[static_cast<size_t>(p.decl_index)];
}

// repr() of a set of ints in CPython's iteration order.
std::string SetRepr(const std::vector<int64_t>& insertion_order) {
  const std::vector<int64_t> order = PySetOrder(insertion_order);
  std::string s = "{";
  for (size_t i = 0; i < order.size(); ++i) s += (i ? ", " : "") + Str(order[i]);
  return s + "}";
}
std::string ListRepr(const std::vector<int64_t>& v) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + Str(v[i]);
  return s + "]";
}

const char* const kIndexAsFloat = "IndexError: arrays used as indices must be of integer (or boolean) type";

}  // namespace

// ------------------------------------------------------------------ CPython helpers
double MathDist(const Vec3& p, const Vec3& q) { return num::MathDist3(p, q); }

double Gemv3(const Vec3& a, const Vec3& u) {
  // OpenBLAS 0.3.23 kernel/x86_64/dgemv_t_4.c, the m3 == 3 remainder: y[j] += aj[0]*xtemp0 + aj[1]*xtemp1 +
  // aj[2]*xtemp2 with xtemp = x * alpha (alpha 1), compiled with contraction: the first addition becomes
  // fma(aj[0], xtemp0, aj[1]*xtemp1), the second fma(aj[2], xtemp2, that); y starts at 0 (beta 0).
  return 0.0 + num::SoftFma(a[2], u[2], num::SoftFma(a[0], u[0], a[1] * u[1]));
}

std::vector<int64_t> PySetOrder(const std::vector<int64_t>& keys) {
  // CPython v3.12.1 Objects/setobject.c: set_add_entry (LINEAR_PROBES 9, PERTURB_SHIFT 5), the resize rule
  // fill * 5 >= mask * 3 -> set_table_resize(used > 50000 ? used * 2 : used * 4) with set_insert_clean, and set_next's
  // iteration in slot order. hash(n) is n for an int (-1 hashes to -2); the values here are far below 2**61.
  struct Slot {
    bool used = false;
    int64_t key = 0;
    int64_t hash = 0;
  };
  std::vector<Slot> table(8);
  size_t mask = 7, fill = 0;
  auto insert_clean = [](std::vector<Slot>& t, size_t m, int64_t key, int64_t hash) {
    uint64_t perturb = static_cast<uint64_t>(hash);
    size_t i = static_cast<size_t>(static_cast<uint64_t>(hash) & m);
    for (;;) {
      if (!t[i].used) {
        t[i] = Slot{true, key, hash};
        return;
      }
      if (i + 9 <= m)
        for (size_t j = 1; j <= 9; ++j)
          if (!t[i + j].used) {
            t[i + j] = Slot{true, key, hash};
            return;
          }
      perturb >>= 5;
      i = static_cast<size_t>((i * 5 + 1 + perturb) & m);
    }
  };
  for (int64_t key : keys) {
    const int64_t hash = key == -1 ? -2 : key;
    uint64_t perturb = static_cast<uint64_t>(hash);
    size_t i = static_cast<size_t>(static_cast<uint64_t>(hash) & mask);
    bool present = false;
    size_t slot = 0;
    for (bool done = false; !done;) {
      const size_t probes = (i + 9 <= mask) ? 9 : 0;
      for (size_t j = 0; j <= probes; ++j) {
        const Slot& e = table[i + j];
        if (!e.used) {
          slot = i + j;
          done = true;
          break;
        }
        if (e.hash == hash && e.key == key) {
          present = true;
          done = true;
          break;
        }
      }
      if (done) break;
      perturb >>= 5;
      i = static_cast<size_t>((i * 5 + 1 + perturb) & mask);
    }
    if (present) continue;
    table[slot] = Slot{true, key, hash};
    ++fill;
    if (fill * 5 < mask * 3) continue;
    const size_t used = fill;  // no deletions: fill == used
    const size_t minused = used > 50000 ? used * 2 : used * 4;
    size_t newsize = 8;
    while (newsize <= minused) newsize <<= 1;
    std::vector<Slot> nt(newsize);
    for (const Slot& e : table)
      if (e.used) insert_clean(nt, newsize - 1, e.key, e.hash);
    table = std::move(nt);
    mask = newsize - 1;
  }
  std::vector<int64_t> order;
  for (const Slot& e : table)
    if (e.used) order.push_back(e.key);
  return order;
}

// ------------------------------------------------------------------ find_cloth (178-201)
std::vector<Found> FindCloth(const core::Model& suit, const std::optional<std::set<int64_t>>& keep) {
  const Blob& d = suit.Cat();
  const be::Reader rd = Rd(d);
  static const char kMc[] = "MULTI_CLOTH";
  std::vector<Found> out;
  size_t p = 0;
  for (;;) {
    const auto it = p > d.size() ? d.end() : std::search(d.begin() + static_cast<std::ptrdiff_t>(p), d.end(), kMc, kMc + 11);
    if (it == d.end()) return out;
    const int64_t mc = it - d.begin();
    p = static_cast<size_t>(mc) + 11;
    const std::vector<uint32_t> h = rd.U32s(mc + 0x10, 5);
    const int64_t ver = h[0], n = h[2], ctab = h[3], spare = h[4];
    Assert(ver == 2 && n >= 1 && spare == 0,
           "('unexpected MULTI_CLOTH', " + Str(ver) + ", " + Str(n) + ", " + Str(spare) + ")");
    std::vector<int64_t> cloths, objs;  // objs: a set, kept in insertion order
    for (int64_t i = 0; i < n; ++i) {
      const int64_t c = mc + rd.U32(mc + ctab + 4 * i);
      if (!SliceIs(d, c, "CLOTH_DATA")) Fail("AssertionError");
      const int64_t gw = c + rd.U32(c + 0x30);
      if (!SliceIs(d, gw, "GRID_WRITE")) Fail("AssertionError");
      Assert(rd.U32(gw + 0x28) == 0, "('GRID_WRITE writes more than one object', '" + Hex(gw) + "')");
      cloths.push_back(c);
      const int64_t o = rd.U32(gw + 0x18);
      if (std::find(objs.begin(), objs.end(), o) == objs.end()) objs.push_back(o);
    }
    Assert(objs.size() == 1, "('cloths of one chunk write several objects', " + SetRepr(objs) + ")");
    const int64_t obj = objs[0];
    if (!keep || keep->count(obj)) out.push_back(Found{mc, std::move(cloths), obj});
  }
}

// ------------------------------------------------------------------ prep_cloth (204-228)
std::vector<Spec> PrepCloth(const core::Model& suit, const std::optional<std::set<int64_t>>& keep, Objects& objects) {
  std::vector<Spec> out;
  for (Found& f : FindCloth(suit, keep)) {
    Where where;
    for (size_t oid = 0; oid < objects.size(); ++oid) {
      if (!objects[oid]) continue;
      const auto& ps = objects[oid]->ps;
      for (size_t k = 0; k < ps.size(); ++k)
        if (ps[k]->model == &suit && ps[k]->obj_id == f.obj)
          where.Set(ps[k]->decl_index, WhereEntry{static_cast<int64_t>(oid), static_cast<int64_t>(k), ps[k]});
    }
    Assert(!where.empty(), "('cloth object not in the converted model', " + Str(f.obj) + ")");
    std::vector<int64_t> owners;
    for (const auto& [decl, e] : where.items())
      if (std::find(owners.begin(), owners.end(), e.oid) == owners.end()) owners.push_back(e.oid);
    Assert(owners.size() == 1, "('cloth decls landed in several objects', " + SetRepr(owners) + ")");
    const int64_t owner = owners[0];
    const core::Object& ob = Obj(objects, owner);
    Assert(ob.pal.empty(), "('cloth writes a skinned object', " + Str(owner) + ", " + ListRepr(ob.pal) + ")");
    for (const auto& [decl, e] : where.items()) {
      e.part->writable = true;
      e.part->bust.reset();  // one writer per decl: the cloth owns these vertices
    }
    out.push_back(Spec{f.mc, std::move(f.cloths), f.obj, owner, std::move(where)});
  }
  return out;
}

// ------------------------------------------------------------------ band_motion (231-331)
void BandMotion(const Spec& spec, Objects& objects, int64_t chest, double fade, double sink) {
  if (spec.owner != chest) return;
  if (spec.where.empty()) Fail("StopIteration");
  const core::Model* model = spec.where.items()[0].second.part->model;
  if (!model) Fail("AttributeError: 'NoneType' object has no attribute 'cat'");
  const Blob& d = model->Cat();
  const be::Reader rd = Rd(d);
  std::map<int64_t, std::set<int64_t>> written;  // decl -> written vertices (membership only)
  for (int64_t c : spec.cloths) {
    const int64_t gw = c + rd.U32(c + 0x30);
    if (!SliceIs(d, gw, "GRID_WRITE")) Fail("AssertionError");
    Assert(rd.U32(gw + 0x28) == 0, "a second GRID_WRITE on a DOAX2 shirt");
    int64_t p = gw + rd.U32(gw + 0x14), seen = 0;
    while (seen < static_cast<int64_t>(rd.U32(gw + 0x24))) {
      const uint32_t w = rd.U32(p);
      if (!(w & 0x80000000u)) {
        const int64_t dk = (w >> 24) & 0x7F, off = w & 0xFFFFFF;
        const WhereEntry* e = spec.where.Find(dk);
        if (!e) Fail("KeyError: " + Str(dk));
        const int64_t stride = DeclSrc(*e->part).Stride();
        if (stride == 0) Fail("ZeroDivisionError: integer modulo by zero");
        if (off % stride != 0) Fail("AssertionError");
        written[dk].insert(off / stride);
        ++seen;
      }
      p += 4;
    }
  }
  const core::Object& chest_ob = Obj(objects, chest);
  std::vector<Vec3> pts, dn, up;
  bool any_top = false;
  for (const core::Part* p : chest_ob.ps) {
    if (!p->bust || p->model != model) continue;
    any_top = true;
    for (const core::Vert& v : p->verts) pts.push_back(v.pos);
    for (const Vec3& x : p->bust->first) dn.push_back(x);
    for (const Vec3& x : p->bust->second) up.push_back(x);
  }
  Assert(any_top, "a chest cloth with no moving piece above it");
  for (const auto& [dk, e] : spec.where.items()) {
    core::Part& part = *e.part;
    const auto wit = written.find(dk);
    std::vector<int64_t> band;
    for (int64_t j = 0; j < static_cast<int64_t>(part.verts.size()); ++j)
      if (wit == written.end() || !wit->second.count(j)) band.push_back(j);
    if (band.empty()) continue;
    const Vec3 zero{0.0, 0.0, 0.0};
    std::vector<Vec3> d_out(part.verts.size(), zero), u_out(part.verts.size(), zero);
    for (int64_t j : band) {
      const std::vector<double> d2 = SqDists(pts, part.verts[static_cast<size_t>(j)].pos);
      const std::vector<int64_t> order = First6(num::NpArgsort(d2));
      if (order.empty()) Fail("IndexError: index 0 is out of bounds for axis 0 with size 0");
      std::vector<double> wt;
      if (d2[static_cast<size_t>(order[0])] < 1e-8) {
        wt.assign(6, 0.0);
        wt[0] = 1.0;
      } else {
        for (int64_t o : order) wt.push_back(1.0 / (d2[static_cast<size_t>(o)] + 1e-8));
        const double s = num::NpSum1D(std::span<const double>(wt.data(), wt.size()));
        for (double& x : wt) x = x / s;
      }
      d_out[static_cast<size_t>(j)] = WeightedSum(dn, order, wt);
      u_out[static_cast<size_t>(j)] = WeightedSum(up, order, wt);
    }
    part.bust = std::make_pair(std::move(d_out), std::move(u_out));
    part.band = std::set<int64_t>(band.begin(), band.end());
  }
  // The skin under the shirt. band_pts walks each part's band set in CPython's iteration order (it reaches the argsort
  // below through ties).
  std::vector<Vec3> band_pts, cloth_pts;
  for (const auto& [dk, e] : spec.where.items())
    if (e.part->band)
      for (int64_t j : PySetOrder(std::vector<int64_t>(e.part->band->begin(), e.part->band->end())))
        band_pts.push_back(e.part->verts[static_cast<size_t>(j)].pos);
  for (const auto& [dk, e] : spec.where.items())
    for (const core::Vert& v : e.part->verts) cloth_pts.push_back(v.pos);
  if (band_pts.empty()) return;
  for (core::Part* p : chest_ob.ps) {
    if (!p->bust || p->model == model) continue;
    std::vector<Vec3> d_new = p->bust->first, u_new = p->bust->second;
    for (size_t j = 0; j < p->verts.size(); ++j) {
      if (p->border && p->border->count(static_cast<int64_t>(j))) continue;
      const Vec3& x = p->verts[j].pos;
      if (NpMin(SqDists(cloth_pts, x)) >= fade * fade) continue;
      const std::vector<int64_t> near_band = First6(num::NpArgsort(SqDists(band_pts, x)));
      std::vector<double> ys;
      for (int64_t o : near_band) ys.push_back(band_pts[static_cast<size_t>(o)][1]);
      const double t = PyMin(PyMax((x[1] - NpMin(ys) + fade) / fade, 0.0), 1.0);
      const double k_f = t * t * (3 - 2 * t);
      const std::vector<double> d2 = SqDists(pts, x);
      const std::vector<int64_t> order = First6(num::NpArgsort(d2));
      std::vector<double> wt;
      for (int64_t o : order) wt.push_back(1.0 / (d2[static_cast<size_t>(o)] + 1e-8));
      const double s = num::NpSum1D(std::span<const double>(wt.data(), wt.size()));
      for (double& w : wt) w = w / s;
      const Vec3 sd = WeightedSum(dn, order, wt), su = WeightedSum(up, order, wt);
      if (j >= d_new.size() || j >= u_new.size()) Fail("IndexError: list assignment index out of range");
      d_new[j] = Vec3{sd[0] * k_f, sd[1] * k_f, sd[2] * k_f};
      u_new[j] = Vec3{su[0] * k_f, su[1] * k_f, su[2] * k_f};
    }
    p->bust = std::make_pair(std::move(d_new), std::move(u_new));
    std::vector<double> by;
    for (const Vec3& b : band_pts) by.push_back(b[1]);
    const double low = NpMin(by), high = NpMax(by);
    Vec3 centre{};
    {
      std::vector<double> flat;
      for (const Vec3& v : cloth_pts) flat.insert(flat.end(), v.begin(), v.end());
      num::NpMeanAxis0(std::span<const double>(flat.data(), flat.size()), 3, std::span<double>(centre.data(), 3));
    }
    if (!p->border) Fail("TypeError: argument of type 'NoneType' is not iterable");
    std::vector<core::Vert> new_verts = p->verts;
    for (size_t j = 0; j < p->verts.size(); ++j) {
      if (p->border->count(static_cast<int64_t>(j))) continue;
      const Vec3& x = p->verts[j].pos;
      if (NpMin(SqDists(cloth_pts, x)) >= fade * fade) continue;
      const double k_s = Step(PyMin(PyMax((x[1] - (low - 0.06)) / 0.02, 0.0), 1.0)) *
                         (1.0 - Step(PyMin(PyMax((x[1] - (high + 0.01)) / 0.03, 0.0), 1.0)));
      if (k_s <= 0.0) continue;
      const Vec3 v{centre[0] - x[0], 0.0, centre[2] - x[2]};
      const double n = Norm1D(v);  // line 326: ddot
      if (n > 1e-6)
        for (int c = 0; c < 3; ++c) new_verts[j].pos[c] = x[c] + v[c] / n * sink * k_s;
    }
    for (size_t j = 0; j < p->verts.size(); ++j) p->verts[j].pos = new_verts[j].pos;
  }
}

// ------------------------------------------------------------------ _vertex_mapper, _mapper (334-368)
namespace {
class Mapper {
 public:
  Mapper(const core::Model& suit, int64_t obj, const Where& where, const core::Maps& maps, const Vec3& own)
      : maps_(maps), own_(own) {
    const Vec3* joint = suit.joint.Find(obj);
    if (!joint) Fail("KeyError: " + Str(obj));
    joint_ = *joint;
    if (where.empty()) Fail("StopIteration");
    const core::Part& part = *where.items()[0].second.part;
    if (!part.attach || part.welded) {
      vertex_ = true;
      for (const auto& [dk, e] : where.items()) {
        const core::Part& p = *e.part;
        if (!p.has_srcv) Fail("AttributeError: 'Part' object has no attribute 'srcv'");
        Assert(p.srcv.size() == p.verts.size(), (p.model ? p.model->name : std::string("None")) + " obj " + Str(p.obj_id));
        for (size_t i = 0; i < p.srcv.size(); ++i) {
          src_.push_back(p.srcv[i].pos);
          dst_.push_back(p.verts[i].pos);
        }
      }
    } else {
      blend_ = part.attach->blend;
      shift_ = part.attach->shift;
    }
  }
  Vec3 operator()(const Vec3& v) const {
    if (vertex_) {
      const Vec3 mp{v[0] + joint_[0], v[1] + joint_[1], v[2] + joint_[2]};
      const std::vector<double> d = SqDists(src_, mp);
      const int64_t i = num::NpArgmin(std::span<const double>(d.data(), d.size()));
      const Vec3& s = src_[static_cast<size_t>(i)];
      const Vec3& t = dst_[static_cast<size_t>(i)];
      return Vec3{t[0] + (mp[0] - s[0]) - own_[0], t[1] + (mp[1] - s[1]) - own_[1], t[2] + (mp[2] - s[2]) - own_[2]};
    }
    const Vec3 mp{v[0] + joint_[0] + shift_[0], v[1] + joint_[1] + shift_[1], v[2] + joint_[2] + shift_[2]};
    Vec3 q{0.0, 0.0, 0.0};
    for (const auto& [bb, wt] : blend_) {
      const Vec3 t = maps_.At(bb)(mp);
      for (int c = 0; c < 3; ++c) q[c] += wt * t[c];
    }
    return Vec3{q[0] - own_[0], q[1] - own_[1], q[2] - own_[2]};
  }

 private:
  const core::Maps& maps_;
  Vec3 own_{}, joint_{}, shift_{};
  bool vertex_ = false;
  std::vector<Vec3> src_, dst_;
  core::Blend blend_;
};

// ------------------------------------------------------------------ _move_bust_colliders (985-1021)
void MoveBustColliders(Blob& blob, int64_t C, const std::optional<std::array<double, 2>>& peak) {
  const int64_t chain = C + Rd(blob).U32(C + 0x38);
  int64_t at = chain;
  std::vector<int64_t> records;
  for (int n = 0; n < 40;) {
    records.push_back(at);
    ++n;
    const int64_t nxt = Rd(blob).U32(at + 8);
    if (!nxt) break;
    at = chain + nxt;
  }
  bool any = false;
  for (int64_t r : records)
    if (I32(blob, r + 4) == 23 || (I32(blob, r) == 9 && I32(blob, r + 0x60) == 23)) {
      any = true;
      break;
    }
  if (!any) return;
  Assert(peak.has_value(), "bone 23 colliders with no bust on this object");
  const Vec3 origin{0.0, (*peak)[0] - 0.005, (*peak)[1] - 0.015 - 0.08 - 0.01};
  auto shift = [&](int64_t o) {
    const Vec3 x = F3(blob, o);
    PutF3(blob, o, Vec3{x[0] + origin[0], x[1] + origin[1], x[2] + origin[2]});
  };
  for (int64_t r : records) {
    if (I32(blob, r + 4) == 23) {
      shift(r + 0x30);
      PutI32(blob, r + 4, 0);
    }
    if (I32(blob, r) == 9 && I32(blob, r + 0x60) == 23) {
      shift(r + 0x50);
      PutI32(blob, r + 0x60, 0);
    }
  }
  std::set<int64_t> left;
  for (int64_t r : records) left.insert(I32(blob, r + 4));
  bool ok = true;
  for (int64_t b : left) ok = ok && b <= 14;
  Assert(ok, "('collider bone outside DOA4 range', " + ListRepr(std::vector<int64_t>(left.begin(), left.end())) + ")");
}

// ------------------------------------------------------------------ _port_cloth (385-463)
void PortCloth(Blob& blob, int64_t C, const core::Model& suit, const Spec& spec, const std::vector<tpr::ObjGeo>& geos,
               const core::PyDict<int64_t, Vec3>& bind4, const core::Maps& maps, std::vector<Pin>* pins_out,
               const std::optional<std::array<double, 2>>& peak) {
  auto u = [&](int64_t o) { return static_cast<int64_t>(Rd(blob).U32(o)); };
  (void)u(C + 0x14);  // the cloth's name (report only); its offset read can raise
  const Mapper f(suit, spec.obj, spec.where, maps, bind4.At(spec.owner));
  // grid nodes
  const int64_t gd = C + u(C + 0x24);
  if (!SliceIs(blob, gd, "GRID_DATA")) Fail("AssertionError");
  const int64_t base = gd + u(gd + 0x14) + 0x10;
  std::vector<Vec3> nodes_old, nodes_new;
  for (int64_t q = base; std::fabs(Rd(blob).F32(q + 12) - 1.0) < 1e-4; q += 48) {
    const Vec3 v = F3(blob, q);
    nodes_old.push_back(v);
    nodes_new.push_back(f(v));
  }
  const int64_t n = static_cast<int64_t>(nodes_old.size());
  if (n == 0) Fail("ValueError: max() arg is an empty sequence");  // `moved` (report) is max() over the nodes
  // springs: each authored rest length scales with the distance it spans (math.dist: CPython's vector_norm)
  for (int64_t i = 0; i < n; ++i) {
    const int64_t cnt = u(base + 48 * i + 0x10), off = u(base + 48 * i + 0x14);
    for (int64_t s = 0; s < cnt; ++s) {
      const int64_t e = gd + off + 16 * s;
      const int64_t noff = u(e);
      const double rest = Rd(blob).F32(e + 4);
      const int64_t rel = gd + noff - base;  // b = rel / 48 (a float): a node when it is integral and 0 <= b < n
      Assert(rel % 48 == 0 && rel >= 0 && rel / 48 < n,
             "('spring target is not a node', " + Str(i) + ", " + Str(noff) + ")");
      const size_t b = static_cast<size_t>(rel / 48);
      const double od = MathDist(nodes_old[static_cast<size_t>(i)], nodes_old[b]);
      if (od > 1e-6) PutF(blob, e + 4, rest * MathDist(nodes_new[static_cast<size_t>(i)], nodes_new[b]) / od);
    }
  }
  for (int64_t i = 0; i < n; ++i) PutF3(blob, base + 48 * i, nodes_new[static_cast<size_t>(i)]);
  // pinned nodes: rest position in the same frame
  const int64_t fg = C + u(C + 0x2C);
  if (!SliceIs(blob, fg, "FIX_GRID")) Fail("AssertionError");
  const int64_t fo = u(fg + 0x14), fc = u(fg + 0x1C);
  const int64_t form = u(fg + 0x10);
  Assert(form == 1 || form == 2, "('FIX_GRID form', " + Str(form) + ")");
  if (u(fg + 0x10) == 2) {
    Assert(u(fg + 0x24) == spec.obj,
           "('FIX_GRID names another object', " + Str(u(fg + 0x24)) + ", " + Str(spec.obj) + ")");
    be::PackIntoU32(blob, fg + 0x24, static_cast<uint64_t>(spec.owner));
  }
  for (int64_t k = 0; k < fc; ++k) {
    const int64_t at = fg + fo + 0x20 * k;
    const Vec3 nv = f(F3(blob, at));
    PutF3(blob, at, nv);
    if (pins_out) pins_out->push_back(Pin{at, nv});
  }
  MoveBustColliders(blob, C, peak);
  // writes
  const int64_t gw = C + u(C + 0x30);
  if (!SliceIs(blob, gw, "GRID_WRITE")) Fail("AssertionError");
  be::PackIntoU32(blob, gw + 0x18, static_cast<uint64_t>(spec.owner));
  (void)u(gw + 0x20);  // nodes_listed (report only)
  const int64_t nwrite = u(gw + 0x24);
  int64_t p = gw + u(gw + 0x14), seen = 0;
  std::optional<int64_t> node;
  double worst = 0.0;
  while (seen < nwrite) {
    const uint32_t w = static_cast<uint32_t>(u(p));
    if (w & 0x80000000u) {
      node = w & 0x7FFFFFFF;
    } else {
      const int64_t dk = (w >> 24) & 0x7F, off = w & 0xFFFFFF;
      const WhereEntry* e = spec.where.Find(dk);
      if (!e) Fail("KeyError: " + Str(dk));
      const int64_t sstride = DeclSrc(*e->part).Stride();
      if (sstride == 0) Fail("ZeroDivisionError: integer modulo by zero");
      Assert(off % sstride == 0, "('write is not at a vertex start', " + Str(off) + ", " + Str(sstride) + ")");
      const int64_t j = off / sstride;
      if (e->oid < 0 || static_cast<size_t>(e->oid) >= geos.size() || e->k < 0 ||
          static_cast<size_t>(e->k) >= geos[static_cast<size_t>(e->oid)].decls.size())
        Fail("IndexError: list index out of range");
      const int64_t stride = geos[static_cast<size_t>(e->oid)].decls[static_cast<size_t>(e->k)].Stride();
      be::PackIntoU32(blob, p, static_cast<uint64_t>((e->k << 24) | (j * stride)));
      if (j >= static_cast<int64_t>(e->part->verts.size())) Fail("IndexError: list index out of range");
      const Vec3& pos = e->part->verts[static_cast<size_t>(j)].pos;
      const Vec3& own = bind4.At(e->oid);
      if (!node) Fail("TypeError: 'NoneType' object is not subscriptable");
      if (*node >= n) Fail("IndexError: list index out of range");
      worst = PyMax(worst, MathDist(Vec3{pos[0] - own[0], pos[1] - own[1], pos[2] - own[2]},
                                    nodes_new[static_cast<size_t>(*node)]));
      ++seen;
    }
    p += 4;
  }
  Assert(worst < 0.001, "('a grid node left its vertex', " + core::ReprD(worst) + ")");
  (void)u(C + u(C + 0x20) + 0x30);  // the report's root bone and first pin bone: reads that can raise
  (void)u(fg + fo + 0x10);
}
}  // namespace

// ------------------------------------------------------------------ build_cloth (371-382)
std::vector<uint8_t> BuildCloth(const core::Model& suit, const Spec& spec, const std::vector<tpr::ObjGeo>& geos,
                                const core::PyDict<int64_t, Vec3>& bind4, const core::Maps& maps,
                                std::vector<Pin>* pins_out, const std::optional<std::array<double, 2>>& peak) {
  const Blob& d = suit.Cat();
  const be::Reader rd = Rd(d);
  if (spec.cloths.empty()) Fail("ValueError: max() arg is an empty sequence");
  int64_t end = 0;
  bool first = true;
  for (int64_t c : spec.cloths) {
    const int64_t e = c + rd.U32(c + 0x14) + rd.U32(c + 0x18);
    if (first || e > end) end = e;
    first = false;
  }
  const int64_t len = static_cast<int64_t>(d.size());
  const int64_t a = std::clamp<int64_t>(spec.mc, 0, len), b = std::clamp<int64_t>(end, 0, len);
  Blob blob(d.begin() + a, d.begin() + std::max(a, b));
  Assert(end == len || SliceIs(d, end, "MULTI_CLOTH"), "('cloth does not end at the next chunk', '" + Hex(end) + "')");
  for (int64_t c : spec.cloths) PortCloth(blob, c - spec.mc, suit, spec, geos, bind4, maps, pins_out, peak);
  return blob;
}

// ------------------------------------------------------------------ pin_rows (1024-1055)
std::vector<bust::PinRow> PinRows(const std::vector<Pin>& pins, const Objects& objects, int64_t chest,
                                  const core::PyDict<int64_t, Vec3>& bind4, const core::Model& suit) {
  const Vec3& own = bind4.At(chest);
  std::vector<Vec3> pts, dn, up;
  for (const core::Part* p : Obj(objects, chest).ps) {
    if (!p->bust || p->model != &suit) continue;
    for (size_t j = 0; j < p->verts.size(); ++j) {
      if (p->band && !p->band->count(static_cast<int64_t>(j))) continue;
      if (j >= p->bust->first.size() || j >= p->bust->second.size()) Fail("IndexError: list index out of range");
      const Vec3& pos = p->verts[j].pos;
      pts.push_back(Vec3{pos[0] - own[0], pos[1] - own[1], pos[2] - own[2]});
      dn.push_back(p->bust->first[j]);
      up.push_back(p->bust->second[j]);
    }
  }
  std::vector<bust::PinRow> rows;
  for (const Pin& pin : pins) {
    if (pts.empty()) Fail("ValueError: operands could not be broadcast together with shapes (0,) (3,) ");
    const std::vector<double> dist = SqDists(pts, pin.nv);
    const size_t k = static_cast<size_t>(num::NpArgmin(std::span<const double>(dist.data(), dist.size())));
    if (pin.at < std::numeric_limits<int32_t>::min() || pin.at > std::numeric_limits<int32_t>::max())
      Fail("not ported yet: a pin offset beyond 32 bits");
    bust::PinRow r;
    r.rel = static_cast<int32_t>(pin.at);
    r.rest = pin.nv;
    if (std::sqrt(dist[k]) > 0.025) {  // line 1046, `** 0.5`: sqrt (census)
      r.down = {0.0, 0.0, 0.0};
      r.up = {0.0, 0.0, 0.0};
    } else {
      r.down = dn[k];
      r.up = up[k];
    }
    rows.push_back(r);
  }
  if (rows.empty()) Fail("ValueError: max() arg is an empty sequence");  // the report's max(moves)
  return rows;
}

// ------------------------------------------------------------------ weld_chest_cloth (1059-1091)
void WeldChestCloth(Objects& objects, int64_t chest, const core::Model& suit, const std::set<int64_t>& cloth_objs) {
  const core::Object& ob = Obj(objects, chest);
  auto srcv_of = [](const core::Part* p) -> const std::vector<core::SrcV>& {
    if (!p->has_srcv) Fail("AttributeError: 'Part' object has no attribute 'srcv'");
    return p->srcv;
  };
  std::vector<core::Part*> cloth, others;
  for (core::Part* p : ob.ps)
    if (p->model == &suit && cloth_objs.count(p->obj_id)) cloth.push_back(p);
  for (core::Part* p : ob.ps)
    if (p->model == &suit && !cloth_objs.count(p->obj_id) && !srcv_of(p).empty()) others.push_back(p);
  if (cloth.empty() || others.empty()) return;
  std::vector<Vec3> src, dst, cl;
  for (const core::Part* o : others)
    for (const core::SrcV& s : o->srcv) src.push_back(s.pos);
  for (const core::Part* o : others)
    for (const core::Vert& v : o->verts) dst.push_back(v.pos);
  for (const core::Part* p : cloth) {
    if (srcv_of(p).empty())
      Fail("ValueError: all the input array dimensions except for the concatenation axis must match exactly");
    for (const core::SrcV& s : p->srcv) cl.push_back(s.pos);
  }
  if (dst.size() != src.size()) Fail("IndexError: boolean index did not match indexed array");
  std::vector<Vec3> A, D;
  for (size_t i = 0; i < src.size(); ++i) {
    const double near = std::sqrt(NpMin(SqDists(cl, src[i])));
    if (near < 0.001) {
      A.push_back(src[i]);
      D.push_back(Vec3{dst[i][0] - src[i][0], dst[i][1] - src[i][1], dst[i][2] - src[i][2]});
    }
  }
  Assert(A.size() >= 8,
         "('the cloth does not touch the piece above it in the source', " + Str(static_cast<int64_t>(A.size())) + ")");
  for (core::Part* p : cloth) {
    if (p->srcv.size() != p->verts.size()) Fail("not ported yet: a cloth part whose srcv and verts differ in length");
    for (size_t i = 0; i < p->srcv.size(); ++i) {
      const Vec3& m = p->srcv[i].pos;
      const std::vector<double> d2 = SqDists(A, m);
      const std::vector<int64_t> order = First6(num::NpArgsort(d2));  // numpy's tie order (Task 2)
      Vec3 q;
      if (d2[static_cast<size_t>(order[0])] < 1e-8) {
        const Vec3& d0 = D[static_cast<size_t>(order[0])];
        q = Vec3{m[0] + d0[0], m[1] + d0[1], m[2] + d0[2]};
      } else {
        std::vector<double> wt;
        for (int64_t o : order) wt.push_back(1.0 / (d2[static_cast<size_t>(o)] + 1e-8));
        const Vec3 s = WeightedSum(D, order, wt);
        const double ws = num::NpSum1D(std::span<const double>(wt.data(), wt.size()));
        q = Vec3{m[0] + s[0] / ws, m[1] + s[1] / ws, m[2] + s[2] / ws};
      }
      p->verts[i].pos = q;
    }
    p->welded = true;
  }
}

// ------------------------------------------------------------------ sink_hamstrings (1197-1225)
void SinkHamstrings(Objects& objects, const core::Model& suit, double amount) {
  std::set<const core::Part*> seen;
  for (auto& ob : objects) {
    if (!ob) continue;
    for (core::Part* p : ob->ps) {
      if (p->model == &suit || p->native || seen.count(p)) continue;
      seen.insert(p);
      std::vector<Vec3> next(p->verts.size());
      for (size_t j = 0; j < p->verts.size(); ++j) next[j] = p->verts[j].pos;
      for (size_t j = 0; j < p->verts.size(); ++j) {
        const Vec3& x = p->verts[j].pos;
        if (std::fabs(x[0]) < 0.005 || !(-0.42 < x[1] && x[1] < -0.06)) continue;
        const double sign = std::isnan(x[0]) ? x[0] : x[0] > 0 ? 1.0 : x[0] < 0 ? -1.0 : 0.0;  // np.sign
        const Vec3 v{x[0] - sign * 0.07, 0.0, x[2] + 0.02};
        const double n = Norm1D(v);  // line 1217: ddot
        if (n < 1e-6) continue;
        const double k = Step(Clamp01((-v[2] / n - 0.2) / 0.5)) * Step(Clamp01((-0.06 - x[1]) / 0.05)) *
                         Step(Clamp01((x[1] + 0.42) / 0.06));
        if (k > 0)
          for (int c = 0; c < 3; ++c) next[j][c] = x[c] - v[c] / n * amount * k;
      }
      for (size_t j = 0; j < p->verts.size(); ++j) p->verts[j].pos = next[j];
    }
  }
}

// ------------------------------------------------------------------ colliders: _shapes, _inside, _push_out
namespace {
struct Shape {
  int64_t at = 0;
  int32_t type = 0, bone = 0;
  bool bound = false;
  Vec3 c{}, p0{}, p1{};
  int64_t r_at = -1, h_at = -1;
};

Vec3 Add(const Vec3& a, const Vec3& b) { return Vec3{a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 Sub(const Vec3& a, const Vec3& b) { return Vec3{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

// _shapes (1094-1123)
std::vector<Shape> Shapes(const Blob& blob, int64_t C, const core::PyDict<int64_t, Vec3>& bones) {
  const be::Reader rd = Rd(blob);
  const int64_t chain = C + rd.U32(C + 0x38);
  int64_t at = chain;
  std::vector<Shape> rows;
  for (int it = 0; it < 40; ++it) {
    const int32_t t = I32(blob, at), b = I32(blob, at + 4);
    if ((t == 1 || t == 2 || t == 4 || t == 9) && bones.Contains(b)) {
      Shape r;
      r.at = at;
      r.type = t;
      r.bone = b;
      r.bound = rd.U32(at + 0x14) != 0;
      bool keep = true;
      if (t == 2) {
        r.c = Add(F3(blob, at + 0x30), bones.At(b));
        r.r_at = at + 0x3C;
      } else if (t == 4) {
        r.c = Add(F3(blob, at + 0x30), bones.At(b));
        r.h_at = at + 0x40;
      } else if (t == 1) {
        r.p0 = Add(F3(blob, at + 0x30), bones.At(b));
        r.p1 = Add(r.p0, F3(blob, at + 0x40));
        r.r_at = at + 0x4C;
      } else if (bones.Contains(I32(blob, at + 0x60))) {
        r.p0 = Add(F3(blob, at + 0x30), bones.At(b));
        r.p1 = Add(F3(blob, at + 0x50), bones.At(I32(blob, at + 0x60)));
        r.r_at = at + 0x64;
      } else {
        keep = false;
      }
      if (keep) rows.push_back(r);
    }
    const int64_t nxt = rd.U32(at + 8);
    if (!nxt) break;
    at = chain + nxt;
  }
  return rows;
}

// (P - p0) @ uu over the rows of P: numpy's scalar_out path (a ddot) for one row, cblas_dgemv for more (lines 1137,
// 1161, 1247; census).
std::vector<double> MatVec(const std::vector<Vec3>& A, const Vec3& uu) {
  std::vector<double> tt(A.size());
  if (A.size() == 1) {
    tt[0] = num::NpDotFused(A[0].data(), uu.data(), 3);
  } else {
    for (size_t i = 0; i < A.size(); ++i) tt[i] = Gemv3(A[i], uu);
  }
  return tt;
}

// _inside (1126-1139)
std::vector<char> Inside(const Blob& blob, const Shape& row, const std::vector<Vec3>& P) {
  std::vector<char> in(P.size());
  if (row.type == 4) {
    const Vec3 h = F3(blob, row.h_at);
    for (size_t i = 0; i < P.size(); ++i) {
      const Vec3 v{(P[i][0] - row.c[0]) / h[0], (P[i][1] - row.c[1]) / h[1], (P[i][2] - row.c[2]) / h[2]};
      in[i] = SqNorm(v) < 1;
    }
    return in;
  }
  const double r = Rd(blob).F32(row.r_at);
  if (row.type == 2) {
    for (size_t i = 0; i < P.size(); ++i) in[i] = SqDist(P[i], row.c) < r * r;
    return in;
  }
  const Vec3 ax = Sub(row.p1, row.p0);
  const double L = Norm1D(ax);  // line 1135: ddot
  const Vec3 uu{ax[0] / L, ax[1] / L, ax[2] / L};
  std::vector<Vec3> A(P.size());
  for (size_t i = 0; i < P.size(); ++i) A[i] = Sub(P[i], row.p0);
  const std::vector<double> tt = MatVec(A, uu);
  for (size_t i = 0; i < P.size(); ++i) {
    const Vec3 q{A[i][0] - tt[i] * uu[0], A[i][1] - tt[i] * uu[1], A[i][2] - tt[i] * uu[2]};
    in[i] = (tt[i] >= 0) && (tt[i] <= L) && (SqNorm(q) < r * r);
  }
  return in;
}

// _push_out (1228-1251)
std::vector<Vec3> PushOut(const Blob& blob, const Shape& row, const std::vector<Vec3>& P, double margin) {
  const std::vector<char> inside = Inside(blob, row, P);
  std::vector<size_t> idx;
  for (size_t i = 0; i < P.size(); ++i)
    if (inside[i]) idx.push_back(i);
  if (idx.empty()) return P;
  std::vector<Vec3> Q = P;
  if (row.type == 4) {
    const Vec3 h = F3(blob, row.h_at);
    for (size_t i : idx) {
      const Vec3 v{(P[i][0] - row.c[0]) / h[0], (P[i][1] - row.c[1]) / h[1], (P[i][2] - row.c[2]) / h[2]};
      const double n = std::sqrt(SqNorm(v));  // line 1238: the axis-1 reduction
      for (int c = 0; c < 3; ++c) Q[i][c] = row.c[c] + v[c] / n * (h[c] + margin);
    }
    return Q;
  }
  const double r = Rd(blob).F32(row.r_at) + margin;
  if (row.type == 2) {
    for (size_t i : idx) {
      const Vec3 v = Sub(P[i], row.c);
      const double n = std::sqrt(SqNorm(v));  // line 1243
      for (int c = 0; c < 3; ++c) Q[i][c] = row.c[c] + v[c] / n * r;
    }
    return Q;
  }
  const Vec3 ax = Sub(row.p1, row.p0);
  const double L = Norm1D(ax);  // line 1246: ddot
  const Vec3 uu{ax[0] / L, ax[1] / L, ax[2] / L};
  std::vector<Vec3> A;
  for (size_t i : idx) A.push_back(Sub(P[i], row.p0));
  const std::vector<double> tt = MatVec(A, uu);  // line 1247
  for (size_t m = 0; m < idx.size(); ++m) {
    const size_t i = idx[m];
    const Vec3 foot{row.p0[0] + tt[m] * uu[0], row.p0[1] + tt[m] * uu[1], row.p0[2] + tt[m] * uu[2]};
    const Vec3 v = Sub(P[i], foot);
    const double n = std::sqrt(SqNorm(v));  // line 1250
    for (int c = 0; c < 3; ++c) Q[i][c] = foot[c] + v[c] / n * r;
  }
  return Q;
}

std::vector<int64_t> FreeNodes(size_t n, const std::set<int64_t>& pinned) {
  std::vector<int64_t> free;
  for (size_t k = 0; k < n; ++k)
    if (!pinned.count(static_cast<int64_t>(k))) free.push_back(static_cast<int64_t>(k));
  if (free.empty()) Fail(kIndexAsFloat);  // np.array([]) is float64: not an index
  return free;
}

// fit_leg_colliders (1142-1194)
void FitLegColliders(Blob& blob, int64_t C, const std::vector<Vec3>& nodes_model, const std::set<int64_t>& pinned,
                     const core::PyDict<int64_t, Vec3>& bones, const std::map<int64_t, std::vector<Vec3>>& legs,
                     const std::vector<Vec3>& hips) {
  const std::vector<Shape> rows = Shapes(blob, C, bones);
  std::vector<Vec3> N;
  for (int64_t k : FreeNodes(nodes_model.size(), pinned)) N.push_back(nodes_model[static_cast<size_t>(k)]);
  std::vector<const Shape*> solid;
  for (const Shape& r : rows)
    if (!r.bound) solid.push_back(&r);
  for (const Shape* r : solid) (void)Inside(blob, *r, N);  // `before` (report only): its reads can raise
  double grew = 0.0;
  for (const Shape* r : solid) {
    if ((r->bone != 6 && r->bone != 12) || (r->type != 1 && r->type != 9)) continue;
    const auto lit = legs.find(r->bone);
    if (lit == legs.end()) Fail("KeyError: " + Str(r->bone));
    const std::vector<Vec3>& P = lit->second;
    const Vec3 ax = Sub(r->p1, r->p0);
    const double L = Norm1D(ax);  // line 1159: ddot
    const Vec3 uu{ax[0] / L, ax[1] / L, ax[2] / L};
    std::vector<Vec3> A(P.size());
    for (size_t i = 0; i < P.size(); ++i) A[i] = Sub(P[i], r->p0);
    const std::vector<double> tt = MatVec(A, uu);  // line 1161
    std::vector<double> dist;
    for (size_t i = 0; i < P.size(); ++i) {
      const Vec3 q{A[i][0] - tt[i] * uu[0], A[i][1] - tt[i] * uu[1], A[i][2] - tt[i] * uu[2]};
      const double d = std::sqrt(SqNorm(q));  // line 1162: the axis-1 reduction
      if (tt[i] >= 0 && tt[i] <= L) dist.push_back(d);
    }
    if (dist.size() < 20) continue;
    const double r0 = Rd(blob).F32(r->r_at);
    const double r1 = PyMin(PyMax(r0, num::NpPercentileLinear(dist, 97) + 0.004), r0 + 0.03);
    if (r1 > r0) {
      PutF(blob, r->r_at, r1);
      grew = PyMax(grew, r1 - r0);
    }
  }
  // hips
  std::vector<Vec3> skin = hips;
  for (int64_t b : {6, 12}) {
    const auto lit = legs.find(b);
    if (lit == legs.end()) Fail("KeyError: " + Str(b));
    const double y = bones.At(b)[1] - 0.15;
    for (const Vec3& v : lit->second)
      if (v[1] > y) skin.push_back(v);
  }
  auto uncovered = [&]() {
    if (solid.empty()) return 1.0;  // np.any([], axis=0) is False: mean 0.0
    std::vector<char> any(skin.size(), 0);
    for (const Shape* r : solid) {
      const std::vector<char> in = Inside(blob, *r, skin);
      for (size_t i = 0; i < skin.size(); ++i) any[i] = any[i] || in[i];
    }
    if (skin.empty()) return 1.0 - std::numeric_limits<double>::quiet_NaN();
    double cnt = 0.0;
    for (char a : any) cnt += a ? 1.0 : 0.0;
    return 1.0 - cnt / static_cast<double>(skin.size());
  };
  std::vector<const Shape*> hip_shapes;
  for (const Shape* r : solid)
    if (r->bone == 2 && r->type == 4) hip_shapes.push_back(r);
  double scale = 1.0;
  std::map<int64_t, Vec3> base_h;
  for (const Shape* r : hip_shapes) base_h[r->at] = F3(blob, r->h_at);
  while (!hip_shapes.empty() && scale < 1.10 && uncovered() > 0.03) {
    scale += 0.05;
    for (const Shape* r : hip_shapes) {
      const Vec3& h = base_h.at(r->at);
      PutF3(blob, r->h_at, Vec3{h[0] * scale, h[1] * scale, h[2] * scale});
    }
  }
  // bound shapes keep containing their groups
  if (grew != 0.0 || scale > 1.0) {
    const double extra = PyMax(grew, 0.02 * (scale > 1.0 ? 1.0 : 0.0));
    for (const Shape& r : rows) {
      if (!r.bound) continue;
      if (r.type == 2) {
        PutF(blob, r.r_at, Rd(blob).F32(r.r_at) + extra);
      } else if (r.type == 4) {
        const Vec3 h = F3(blob, r.h_at);
        PutF3(blob, r.h_at, Vec3{h[0] + extra, h[1] + extra, h[2] + extra});
      }
    }
  }
}

// unstick_nodes (1254-1270)
void UnstickNodes(Blob& blob, int64_t C, int64_t gd, int64_t no, const std::vector<Vec3>& N,
                  const std::set<int64_t>& pinned, const core::PyDict<int64_t, Vec3>& bones, const Vec3& bind_own,
                  double margin = 0.004) {
  std::vector<Shape> rows;
  for (const Shape& r : Shapes(blob, C, bones))
    if (!r.bound) rows.push_back(r);
  const std::vector<int64_t> free = FreeNodes(N.size(), pinned);
  std::vector<Vec3> P;
  for (int64_t k : free) P.push_back(N[static_cast<size_t>(k)]);
  for (const Shape& r : rows) (void)Inside(blob, r, P);  // `before` (report only): its reads can raise
  for (int round = 0; round < 6; ++round)
    for (const Shape& r : rows) P = PushOut(blob, r, P, margin);
  for (size_t m = 0; m < free.size(); ++m)
    PutF3(blob, gd + no + 48 * free[m], Sub(P[m], bind_own));
}

// back_margin (1273-1285)
void BackMargin(Blob& blob, int64_t C, const std::vector<Vec3>& N, const core::PyDict<int64_t, Vec3>& bones,
                double back) {
  std::vector<double> z;
  for (const Vec3& v : N) z.push_back(v[2]);
  if (num::NpMean1D(std::span<const double>(z.data(), z.size())) >= -0.02) return;
  for (const Shape& r : Shapes(blob, C, bones)) {
    if ((r.bone != 6 && r.bone != 12) || r.bound) continue;
    for (int64_t off : (r.type == 1 || r.type == 2 || r.type == 4) ? std::vector<int64_t>{0x30}
                                                                    : std::vector<int64_t>{0x30, 0x50}) {
      const Vec3 x = F3(blob, r.at + off);
      PutF3(blob, r.at + off, Vec3{x[0], x[1], x[2] - back});
    }
  }
}
}  // namespace

// ------------------------------------------------------------------ fit_cloth_chunk (1288-1306)
std::vector<uint8_t> FitClothChunk(const std::vector<uint8_t>& chunk, const core::PyDict<int64_t, Vec3>& bones,
                                   const Vec3& bind_own, const std::map<int64_t, std::vector<Vec3>>& legs,
                                   const std::vector<Vec3>& hips, bool unstick, bool fit, double back) {
  Blob out = chunk;
  auto u = [&](int64_t o) { return static_cast<int64_t>(Rd(out).U32(o)); };
  const int64_t chunks = u(0x18);  // range(u(0x18)) is evaluated once
  for (int64_t ci = 0; ci < chunks; ++ci) {
    const int64_t C = u(u(0x1C) + 4 * ci);
    const int64_t gd = C + u(C + 0x24);
    const int64_t no = u(gd + 0x20), nn = u(gd + 0x24);
    if (nn == 0) Fail("ValueError: operands could not be broadcast together with shapes (0,) (3,) ");
    std::vector<Vec3> N;
    for (int64_t k = 0; k < nn; ++k) N.push_back(Add(F3(out, gd + no + 48 * k), bind_own));
    const int64_t fg = C + u(C + 0x2C);
    std::set<int64_t> pinned;  // membership only
    const int64_t npins = u(fg + 0x1C);
    for (int64_t k = 0; k < npins; ++k) {
      const int64_t v = u(fg + u(fg + 0x18) + 32 * k + 0x14) - no;
      pinned.insert(v >= 0 ? v / 48 : -((-v + 47) / 48));  // Python's floor division
    }
    if (fit) FitLegColliders(out, C, N, pinned, bones, legs, hips);
    if (back > 0) BackMargin(out, C, N, bones, back);
    if (unstick) UnstickNodes(out, C, gd, no, N, pinned, bones, bind_own);
  }
  return out;
}

}  // namespace doa4::suitconv::cloth
