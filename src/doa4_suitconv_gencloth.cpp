// Suit converter (Plan 2): the generated cloth. See doa4_suitconv_gencloth.h. Line numbers in the comments are cloth_gen.py's
// (the Python recipe's cloth_gen.py, SHA-1 b86ca95f6b19adf68f9e2caa9850fdf496e7d422).
//
// Numeric paths, each chosen by a census against the recipe's interpreter on the two generated-cloth suits' real inputs
// (numpy 1.26.3, OpenBLAS 0.3.23 Cooperlake kernels, CPython 3.12.1):
//   np.linalg.norm of a 1-D 3-vector    sqrt(x.dot(x)), a BLAS ddot: num::NpDotFused (0 mismatches at lines 65, 87, 106, 180,
//                                       223 and 457; the plain sum misses 224 of 2250, 394 of 4952 (twice), 0 of 2178, 205 of 2096 and
//                                       12 of 4192 rows, the axis reduce the same)
//   v @ w of two 1-D 3-vectors          a BLAS ddot as well: num::NpDotFused (0 mismatches over 4952 pairs; plain 687, PySum 855)
//   np.linalg.norm(steps, axis=1)       sqrt(add.reduce(x*x, axis=1)): num::NpSumLastAxis of the squares (0 mismatches; the
//                                       fused ddot misses 65 of 832 rows)
//   .sum() of a 1-D array               num::NpSum1D (0 mismatches over 538 sums; a sequential sum misses 7, builtin sum 62)
//   P[L, 1].mean()                      num::NpMean1D (0 mismatches over 4 loops; a sequential sum misses 3)
//   round(x) of the weld key            num::RoundHalfEven (CPython's float.__round__; np.round(x) with no digits is rint,
//                                       also half to even: the same keys)
//   x ** 2, x ** 0.5                    not used: the only roots are the norms above
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_gencloth.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <tuple>

#include "doa4_suitconv_be.h"
#include "doa4_suitconv_num.h"

namespace doa4::suitconv::gencloth {

using core::Assert;
using core::Fail;
using core::Vec3;

namespace {

using Blob = std::vector<uint8_t>;
using Edge = std::pair<int, int>;

// GENERATED (cloth_gen.py line 15): design -> (source object, decl, source mesh indices that are the skirt)
struct GenEntry {
  const char* design;
  int64_t obj, decl;
  std::vector<int64_t> meshes;
};
const GenEntry kGenerated[] = {{"vks_on_o", 113, 0, {1, 2, 3}}, {"vle_sk_h", 76, 0, {1}}};
// SOFT_HEM (line 17): no ring threads, and this strength for PROPERTY +0x5C
constexpr double kSoftHemStrength = 0.05;
bool SoftHem(const std::string& design) { return design == "vle_sk_h"; }
// DONOR_PROPERTY (line 20): design -> (offset of the MULTI_CLOTH in the donor, the skirt's name there)
struct DonorProp {
  const char* design;
  int64_t mc;
  const char* name;
};
const DonorProp kDonorProperty[] = {{"vks_on_o", 0x538C0, "ks_skirt"}, {"vle_sk_h", 0x71450, "ht_skirt"}};
constexpr double kWeld = 0.0005;  // WELD (line 22)

std::string Str(int64_t v) { return std::to_string(v); }
std::string Q(const std::string& s) { return "'" + s + "'"; }
std::string Hex(int64_t v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
  return Q(buf);
}

be::Reader Rd(std::span<const uint8_t> b) { return be::Reader(b); }
uint32_t U32(std::span<const uint8_t> d, int64_t o) { return Rd(d).U32(o); }
int32_t I32(std::span<const uint8_t> d, int64_t o) { return static_cast<int32_t>(Rd(d).U32(o)); }
double F32(std::span<const uint8_t> d, int64_t o) { return Rd(d).F32(o); }

// _tag(name): the name padded with zeros to 16 bytes
void PutTag(Blob& b, const char* name) {
  const size_t n = std::char_traits<char>::length(name);
  b.insert(b.end(), name, name + n);
  b.insert(b.end(), 16 - n, uint8_t(0));
}
// _pad(b, n): zeros up to a multiple of n
void Pad(Blob& b, size_t n = 16) {
  while (b.size() % n) b.push_back(0);
}
// struct.pack('>I' ...) appended; every value is range checked as struct.pack does.
void PutU32(Blob& b, uint64_t v) {
  const uint32_t x = be::CheckU32(v);
  for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void PutI32(Blob& b, int64_t v) {
  if (v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max())
    Fail("struct.error: 'i' format requires -2147483648 <= number <= 2147483647");
  PutU32(b, static_cast<uint32_t>(static_cast<int32_t>(v)));
}
void PutF32(Blob& b, double x) { PutU32(b, std::bit_cast<uint32_t>(num::PackF32(x))); }
// struct.pack_into('>I', b, o, v)
void PackInto(Blob& b, int64_t o, std::initializer_list<uint64_t> words) {
  int64_t at = o;
  for (uint64_t w : words) {
    be::PackIntoU32(b, at, w);
    at += 4;
  }
}
void PackIntoF(Blob& b, int64_t o, double x) { be::PackIntoU32(b, o, std::bit_cast<uint32_t>(num::PackF32(x))); }

// np.linalg.norm of a 1-D 3-vector, float(...): a BLAS ddot (census: 0 mismatches at lines 65, 87, 106, 180, 223, 457).
double Dot3(const Vec3& a, const Vec3& b) { return num::NpDotFused(a.data(), b.data(), 3); }
double Norm1D(const Vec3& v) { return std::sqrt(Dot3(v, v)); }
Vec3 Sub(const Vec3& a, const Vec3& b) { return Vec3{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

Edge MakeEdge(int x, int y) { return Edge{std::min(x, y), std::max(x, y)}; }

// ------------------------------------------------------------------ derive (lines 59-95)
struct Derived {
  std::set<Edge> t1, t2, t3;
  std::map<int, std::set<int>> adj1;
  std::set<Edge> diag;
  std::map<Edge, std::vector<std::pair<int, int>>> edge_tris;  // edge -> [(triangle, opposite node)] in append order
};

Derived Derive(const std::vector<Vec3>& P, const std::vector<std::array<int, 3>>& tris) {
  Derived d;
  for (size_t t = 0; t < tris.size(); ++t) {
    const auto& [a, b, c] = tris[t];
    const int rot[3][3] = {{a, b, c}, {b, c, a}, {c, a, b}};
    for (const auto& r : rot) d.edge_tris[MakeEdge(r[0], r[1])].emplace_back(int(t), r[2]);
  }
  auto L = [&](int x, int y) { return Norm1D(Sub(P[size_t(x)], P[size_t(y)])); };  // line 65
  std::vector<Edge> longest(tris.size());
  for (size_t t = 0; t < tris.size(); ++t) {
    const auto& [a, b, c] = tris[t];
    // max((L(x, y), (min, max)) for x, y in ((a, b), (b, c), (c, a))): tuples compare, the first wins a tie
    const int pairs[3][2] = {{a, b}, {b, c}, {c, a}};
    double best_l = 0.0;
    Edge best_e{};
    for (int i = 0; i < 3; ++i) {
      const double l = L(pairs[i][0], pairs[i][1]);
      const Edge e = MakeEdge(pairs[i][0], pairs[i][1]);
      if (i == 0 || l > best_l || (l == best_l && e > best_e)) {
        best_l = l;
        best_e = e;
      }
    }
    longest[t] = best_e;
  }
  for (const auto& [e, ts] : d.edge_tris)
    if (ts.size() == 2 && longest[size_t(ts[0].first)] == e && longest[size_t(ts[1].first)] == e) d.diag.insert(e);
  for (const auto& kv : d.edge_tris)
    if (!d.diag.count(kv.first)) d.t1.insert(kv.first);
  for (const Edge& e : d.diag) {
    const auto& ts = d.edge_tris[e];
    const int za = ts[0].second, zb = ts[1].second;
    d.t2.insert(e);
    d.t2.insert(MakeEdge(za, zb));
  }
  for (const Edge& e : d.t1) {
    d.adj1[e.first].insert(e.second);
    d.adj1[e.second].insert(e.first);
  }
  for (const auto& [m, nset] : d.adj1) {
    const std::vector<int> ns(nset.begin(), nset.end());  // sorted(adj1[m])
    std::map<int, std::pair<double, int>> best;           // q -> (c, r); only looked up, so its order is free
    for (size_t i = 0; i < ns.size(); ++i)
      for (size_t j = i + 1; j < ns.size(); ++j) {
        const int a = ns[i], b = ns[j];
        const Vec3 v = Sub(P[size_t(a)], P[size_t(m)]), w = Sub(P[size_t(b)], P[size_t(m)]);
        const double c = Cosine(v, w);  // line 87
        if (c < -0.5) {
          const int qr[2][2] = {{a, b}, {b, a}};
          for (const auto& x : qr) {
            auto it = best.find(x[0]);
            if (it == best.end() || c < it->second.first) best[x[0]] = {c, x[1]};
          }
        }
      }
    for (const auto& [q, cr] : best) {
      const int r = cr.second;
      auto it = best.find(r);
      if (it != best.end() && it->second.second == q) d.t3.insert(MakeEdge(q, r));
    }
  }
  return d;
}

// ------------------------------------------------------------------ straight_lines (lines 98-144)
struct Line {
  bool closed;
  std::vector<int> seq;
};

std::vector<Line> StraightLines(const std::vector<Vec3>& P, const std::map<int, std::set<int>>& adj1, double min_cos = -0.5) {
  std::map<int, std::map<int, int>> cont;
  for (const auto& [m, nset] : adj1) {
    const std::vector<int> ns(nset.begin(), nset.end());
    std::vector<std::tuple<double, int, int>> cands;
    for (size_t i = 0; i < ns.size(); ++i)
      for (size_t j = i + 1; j < ns.size(); ++j) {
        const Vec3 v = Sub(P[size_t(ns[i])], P[size_t(m)]), w = Sub(P[size_t(ns[j])], P[size_t(m)]);
        const double c = Cosine(v, w);  // line 106
        if (c < min_cos) cands.emplace_back(c, ns[i], ns[j]);
      }
    std::set<int> used;
    std::sort(cands.begin(), cands.end());
    for (const auto& [c, a, b] : cands) {
      (void)c;
      if (used.count(a) || used.count(b)) continue;
      used.insert(a);
      used.insert(b);
      cont[m][a] = b;
      cont[m][b] = a;
    }
  }
  auto get = [&](int y, int x) -> std::optional<int> {  // cont[y].get(x)
    auto it = cont.find(y);
    if (it == cont.end()) return std::nullopt;
    auto jt = it->second.find(x);
    if (jt == it->second.end()) return std::nullopt;
    return jt->second;
  };
  std::set<Edge> seen;
  std::vector<Line> out;
  for (const auto& [m, nset] : adj1)
    for (int a : nset) {
      const Edge key = MakeEdge(m, a);
      if (seen.count(key)) continue;
      std::vector<int> seq{a, m};
      bool closed = false;
      seen.insert(key);
      while (seq.size() < 400) {
        const int x = seq[seq.size() - 2], y = seq.back();
        const auto nx = get(y, x);
        if (!nx) break;
        if (*nx == seq[0] && get(*nx, y) == std::optional<int>(seq[1])) {
          closed = true;
          seen.insert(MakeEdge(y, *nx));
          break;
        }
        seq.push_back(*nx);
        seen.insert(MakeEdge(y, *nx));
      }
      if (!closed) {
        while (seq.size() < 400) {
          const int x = seq[1], y = seq[0];
          const auto nx = get(y, x);
          if (!nx) break;
          seq.insert(seq.begin(), *nx);
          seen.insert(MakeEdge(y, *nx));
        }
      }
      out.push_back(Line{closed, std::move(seq)});
    }
  return out;
}

// ------------------------------------------------------------------ build helpers
struct Spring {
  int target;
  double rest;
  int typ;
};

// Python's repr of a dict of (usage, usage index) -> (offset, type name), in insertion order (for the assert's text).
std::string NamesRepr(const std::vector<std::pair<std::pair<int, int>, std::pair<int, std::string>>>& names) {
  std::string s = "{";
  for (size_t i = 0; i < names.size(); ++i)
    s += (i ? ", " : "") + std::string("(") + Str(names[i].first.first) + ", " + Str(names[i].first.second) + "): (" +
         Str(names[i].second.first) + ", " + Q(names[i].second.second) + ")";
  return s + "}";
}

}  // namespace

// ------------------------------------------------------------------ the numeric sites, exposed for the test
// float(v @ w / (np.linalg.norm(v) * np.linalg.norm(w) + 1e-12)) (lines 87 and 106; census: 0 mismatches over 4952 pairs)
double Cosine(const Vec3& v, const Vec3& w) { return Dot3(v, w) / (Norm1D(v) * Norm1D(w) + 1e-12); }

// float(np.linalg.norm(a - b)) (lines 65, 223, 457)
double Distance(const Vec3& a, const Vec3& b) { return Norm1D(Sub(a, b)); }

// np.abs(steps[:, 1]).sum() / (np.linalg.norm(steps, axis=1).sum() + 1e-12) (line 262)
double VerticalRatio(const std::vector<Vec3>& steps) {
  if (steps.empty()) Fail("IndexError: too many indices for array: array is 1-dimensional, but 2 were indexed");
  std::vector<double> absy, norms;
  for (const Vec3& step : steps) {
    absy.push_back(std::fabs(step[1]));
    const double sq[3] = {step[0] * step[0], step[1] * step[1], step[2] * step[2]};
    norms.push_back(std::sqrt(num::NpSumLastAxis(std::span<const double>(sq, 3))));  // census: the axis reduce
  }
  const double num_sum = num::NpSum1D(std::span<const double>(absy.data(), absy.size()));
  const double den_sum = num::NpSum1D(std::span<const double>(norms.data(), norms.size()));
  return num_sum / (den_sum + 1e-12);
}

// float(P[L, 1].mean()) (line 207)
double LoopKey(const std::vector<double>& ys) { return num::NpMean1D(std::span<const double>(ys.data(), ys.size())); }

// ------------------------------------------------------------------ prep (lines 38-56)
Spec Prep(std::string_view design, const core::Model& suit, std::vector<std::optional<core::Object>>& objects,
          const core::PyDict<int64_t, int64_t>& chan2id) {
  const GenEntry* g = nullptr;
  for (const GenEntry& e : kGenerated)
    if (design == e.design) g = &e;
  if (!g) Fail("KeyError: " + Q(std::string(design)));
  const int64_t hips = chan2id.At(0x9002);
  struct Found {
    int64_t oid, k;
    core::Part* p;
  };
  std::optional<Found> found;
  for (size_t oid = 0; oid < objects.size(); ++oid) {
    if (!objects[oid]) continue;
    const auto& ps = objects[oid]->ps;
    for (size_t k = 0; k < ps.size(); ++k) {
      core::Part* p = ps[k];
      if (p->model == &suit && p->obj_id == g->obj && p->decl_index == g->decl) {
        Assert(!found, "skirt decl found twice");
        found = Found{int64_t(oid), int64_t(k), p};
      }
    }
  }
  Assert(found.has_value(), "('skirt decl not in the converted model', " + Q(std::string(design)) + ", " + Str(g->obj) + ", " +
                                Str(g->decl) + ")");
  const auto [oid, k, p] = *found;
  const auto& pal = objects[size_t(oid)]->pal;
  std::string l = "[";
  for (size_t i = 0; i < pal.size(); ++i) l += (i ? ", " : "") + Str(pal[i]);
  Assert(oid == hips && pal.empty(), "('skirt must be on the rigid hips object', " + Str(oid) + ", " + Str(hips) + ", " + l + "])");
  p->writable = true;
  p->bust.reset();
  return Spec{std::string(design), oid, k, p, g->meshes};
}

// ------------------------------------------------------------------ walk (lines 420-529)
void Walk(std::span<const uint8_t> d, int64_t mc) {
  const auto u = [&](int64_t o) { return int64_t(U32(d, o)); };
  const auto slice_is = [&](int64_t a, const char* s) {
    const size_t n = std::char_traits<char>::length(s);
    const auto sl = Rd(d).Slice(uint64_t(a), uint64_t(a) + n);
    return sl.size() == n && std::equal(sl.begin(), sl.end(), s);
  };
  Assert(mc % 16 == 0 && slice_is(mc, "MULTI_CLOTH"), "chunk");
  Assert(u(mc + 0x10) == 2 && u(mc + 0x14) == 0, "chunk head");
  for (int64_t ci = 0; ci < u(mc + 0x18); ++ci) {
    const int64_t c = mc + u(mc + u(mc + 0x1C) + 4 * ci);
    Assert(slice_is(c, "CLOTH_DATA") && (c - mc) % 16 == 0, "cloth tag");
    Assert(u(c + 0x1C) == 0, "rule 2: cloth already fixed up");
    for (int64_t o : {0x20, 0x24, 0x2C, 0x30, 0x34, 0x38}) {
      Assert(0 < u(c + o) && u(c + o) < u(c + 0x14) + u(c + 0x18), "('section out of range', " + Hex(o) + ")");
      Assert(u(c + o) % 16 == 0 || o == 0x34, "('rule 1: section not aligned', " + Hex(o) + ")");
    }
    const int64_t gd = c + u(c + 0x24);
    Assert(slice_is(gd, "GRID_DATA"), "grid tag");
    const int64_t no = u(gd + 0x20), nn = u(gd + 0x24), fo = u(gd + 0x28), fc = u(gd + 0x2C);
    const int64_t limit = u(gd + 0x1C);
    Assert(no == 0x40 && no + 48 * nn <= limit && fo + 16 * fc <= limit, "rule 4: grid counts");
    const auto node_ok = [&](int64_t off) {
      const int64_t diff = off - no;
      return diff >= 0 && diff % 48 == 0 && diff / 48 < nn;
    };
    std::vector<Vec3> P;
    P.reserve(static_cast<size_t>(nn));
    for (int64_t i = 0; i < nn; ++i) {
      const int64_t b = gd + no + 48 * i;
      P.push_back(Vec3{F32(d, b), F32(d, b + 4), F32(d, b + 8)});
    }
    std::vector<std::vector<int64_t>> lists;
    int64_t faces_per = 0;
    for (int64_t i = 0; i < nn; ++i) {
      const int64_t b = gd + no + 48 * i;
      Assert(std::fabs(F32(d, b + 12) - 1.0) < 1e-5, "node w");
      Assert(u(b + 0x2C) == 0, "node +2C is written by the game");
      int64_t f[6];
      for (int z = 0; z < 6; ++z) f[z] = u(b + 0x10 + 4 * z);
      const int64_t sc = f[0], so = f[1], nf = f[2], fa = f[3], nr = f[4], ro = f[5];
      Assert(so + 16 * sc <= limit && fa + 8 * nf <= limit && ro + 4 * nr <= limit, "node lists out of range");
      std::vector<int64_t> lst;
      for (int64_t z = 0; z < sc; ++z) {
        const int64_t e = gd + so + 16 * z;
        const int64_t tgt = u(e);
        Assert(node_ok(tgt), "('rule 5: spring target', " + Str(i) + ", " + Hex(tgt) + ")");
        Assert(u(e + 8) <= 5, "rule 6: spring type");
        Assert(u(e + 12) == 0, "spring +0C is written by the game");
        const double rest = F32(d, e + 4);
        const double dist = Norm1D(Sub(P[size_t(i)], P[size_t((tgt - no) / 48)]));  // line 457 (census: ddot)
        const double mx = 0.01 > dist ? 0.01 : dist;                                // max(dist, 0.01)
        Assert(std::fabs(rest - dist) <= 0.05 * mx + 1e-3,
               "('spring rest is not the distance', " + Str(i) + ", " + core::ReprD(rest) + ", " + core::ReprD(dist) + ")");
        lst.push_back(tgt);
      }
      lists.push_back(std::move(lst));
      for (int64_t z = 0; z < nf; ++z)
        Assert(node_ok(u(gd + fa + 8 * z)) && node_ok(u(gd + fa + 8 * z + 4)), "fan pair");
      faces_per = std::max(faces_per, nr);
      for (int64_t z = 0; z < nr; ++z) {
        const int64_t r = u(gd + ro + 4 * z);
        Assert((r - fo) % 16 == 0 && 0 <= (r - fo) / 16 && (r - fo) / 16 < fc, "face ref");
      }
    }
    Assert(faces_per <= 44, "rule 14: faces per node");
    for (int64_t z = 0; z < fc; ++z) {
      const int64_t r = gd + fo + 16 * z;
      bool ok = u(r) == 0;
      for (int q = 0; ok && q < 3; ++q) ok = node_ok(u(r + 4 + 4 * q));
      Assert(ok, "face record");
    }
    const int64_t fg = c + u(c + 0x2C);
    Assert(slice_is(fg, "FIX_GRID"), "fix tag");
    const int64_t eo = u(fg + 0x18), ec = u(fg + 0x1C);
    Assert((fg - c + eo) % 16 == 0 && ec > 0, "rule 1: pin entries");
    for (int64_t z = 0; z < ec; ++z) Assert(node_ok(u(fg + eo + 32 * z + 0x14)), "pin node");
    const int64_t gw = c + u(c + 0x30);
    Assert(slice_is(gw, "GRID_WRITE") && u(gw + 0x14) == 0x60, "rule 8: GRID_WRITE head");
    Assert(u(gw + 0x1C) == 0x60, "GRID_WRITE list offset");
    int64_t q = gw + u(gw + 0x1C);
    Assert((u(q) & 0x80000000) != 0, "rule 9: list starts with a node");
    while (u(q) != 0xFFFFFFFF) {
      const int64_t w = u(q);
      if (w & 0x80000000) {
        Assert((w & 0x7FFFFFFF) < nn, "write node");
      } else {
        Assert((w >> 24) < 46, "rule 10: decl");
      }
      q += 4;
      Assert(q < int64_t(d.size()), "rule 9: no terminator");
    }
    const int64_t th = c + u(c + 0x34);
    Assert(slice_is(th, "THREAD_DATA"), "thread tag");
    const int64_t tables[2][2] = {{u(th + 0x18), u(th + 0x1C)}, {u(th + 0x20), u(th + 0x24)}};
    for (const auto& oc : tables) {
      for (int64_t r = 0; r < oc[1]; ++r) {
        const std::vector<uint32_t> rec = Rd(d).U32s(th + oc[0] + 24 * r, 6);
        const int64_t typ = rec[0], sz = rec[1], eo2 = rec[2], ec2 = rec[3], so = rec[4], sc = rec[5];
        Assert((typ == 1 || typ == 3) && sz == 0x18, "thread record");
        struct Ent {
          int64_t node, nxt, prv;
        };
        std::vector<Ent> ents;
        for (int64_t z = 0; z < ec2; ++z)
          ents.push_back(Ent{u(th + eo2 + 12 * z), I32(d, th + eo2 + 12 * z + 4), I32(d, th + eo2 + 12 * z + 8)});
        for (int64_t z = 0; z < ec2; ++z) {
          const Ent& e = ents[size_t(z)];
          Assert(node_ok(e.node), "thread node");
          const auto& own = lists[size_t((e.node - no) / 48)];
          if (e.nxt >= 0) {
            Assert(e.nxt < int64_t(own.size()) && own[size_t(e.nxt)] == ents[size_t((z + 1) % ec2)].node,
                   "('rule 7: next index', " + Str(r) + ", " + Str(z) + ")");
          } else {
            Assert(typ == 1 && z == ec2 - 1, "rule 7: -1 only at a far end");
          }
          if (e.prv >= 0) {
            Assert(e.prv < int64_t(own.size()) && own[size_t(e.prv)] == ents[size_t((z + ec2 - 1) % ec2)].node,
                   "('rule 7: previous index', " + Str(r) + ", " + Str(z) + ")");
          } else {
            Assert(typ == 1 && z == 0, "rule 7: -1 only at a far end");
          }
        }
        for (int64_t z = 0; z < (sc & 0xFFFF); ++z) {
          const int64_t t = u(th + so + 4 * z);
          Assert(u(th + t) == 1 && u(th + t + 4) == 0x40, "bend block");
        }
      }
    }
    const int64_t ch = c + u(c + 0x38);
    int64_t at = ch, count = 0;
    for (;;) {
      ++count;
      Assert(count <= 32, "rule 15: colliders");
      const int64_t nxt = u(at + 8);
      if (!nxt) break;
      at = ch + nxt;
    }
  }
}

// ------------------------------------------------------------------ build (lines 147-417)
std::vector<uint8_t> Build(const Spec& spec, const std::vector<tpr::ObjGeo>& geos, const core::PyDict<int64_t, Vec3>& bind4,
                           std::span<const uint8_t> donor, std::span<const uint8_t> bend) {
  core::Part* p = spec.part;
  const int64_t oid = spec.oid, k = spec.k;
  const Vec3& own = bind4.At(oid);
  if (oid < 0 || oid >= int64_t(geos.size())) Fail("IndexError: list index out of range");
  const auto& decls = geos[size_t(oid)].decls;
  if (k < 0 || k >= int64_t(decls.size())) Fail("IndexError: list index out of range");
  const tpr::Decl& dc = decls[size_t(k)];
  const int64_t stride = dc.Stride();
  // names = {(e[4], e[5]): (e[1], DT[e[2]][0]) for e in dc.elems[:-1] if e[0] == 0}
  std::vector<std::pair<std::pair<int, int>, std::pair<int, std::string>>> names;
  const size_t nel = dc.elems.empty() ? 0 : dc.elems.size() - 1;
  const auto dt_name = [](uint32_t typ) -> std::string {
    const tpr::DtEntry* e = tpr::DtFind(typ);
    if (!e) Fail("KeyError: " + Str(typ));
    return std::string(e->name);
  };
  for (size_t i = 0; i < nel; ++i) {
    const tpr::DeclElem& e = dc.elems[i];
    if (e.a != 0) continue;
    const std::pair<int, int> key{e.e, e.f};
    const std::pair<int, std::string> val{e.b, dt_name(e.c)};
    bool replaced = false;
    for (auto& kv : names)
      if (kv.first == key) {
        kv.second = val;
        replaced = true;
      }
    if (!replaced) names.emplace_back(key, val);
  }
  const auto name_at = [&](int u0, int u1) -> const std::pair<int, std::string>* {
    for (const auto& kv : names)
      if (kv.first == std::pair<int, int>{u0, u1}) return &kv.second;
    return nullptr;
  };
  const auto* n00 = name_at(0, 0);
  const auto* n30 = name_at(3, 0);
  Assert(n00 && *n00 == std::pair<int, std::string>{0, "F3"} && n30 && *n30 == std::pair<int, std::string>{12, "F3"},
         "('written decl needs position and float normal at 0 and 12', " + NamesRepr(names) + ")");
  for (size_t i = 0; i < nel; ++i) Assert(dt_name(dc.elems[i].c) != "HEND3N", "packed normals in the written decl");
  Assert(dc.head[8] == 1, "written decl is not writable");
  if (p->ib.size() != 2 * size_t(p->icount))
    Fail("struct.error: unpack requires a buffer of " + Str(int64_t(2 * size_t(p->icount))) + " bytes");
  std::vector<uint16_t> idx(p->icount);
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = uint16_t((p->ib[2 * i] << 8) | p->ib[2 * i + 1]);
  std::vector<Vec3> pos;
  pos.reserve(p->verts.size());
  for (const core::Vert& v : p->verts) pos.push_back(Vec3{v.pos[0] - own[0], v.pos[1] - own[1], v.pos[2] - own[2]});

  // weld the vertices of the skirt's meshes (lines 159-178)
  std::map<std::array<int64_t, 3>, int> key2node;
  std::vector<Vec3> nodes;
  std::vector<std::set<int>> writes;
  std::vector<std::array<int, 3>> tris;
  const std::set<int64_t> wanted(spec.meshes.begin(), spec.meshes.end());
  for (size_t mi = 0; mi < p->meshes.size(); ++mi) {
    if (mi >= p->passes.size()) Fail("IndexError: list index out of range");
    if (!wanted.count(p->passes[mi].src)) continue;
    const tpr::Bytes& m = p->meshes[mi];
    const uint32_t st = U32(m, 0x10), cnt = U32(m, 0x14);
    const uint64_t lo = std::min<uint64_t>(st, idx.size()), hi = std::min<uint64_t>(uint64_t(st) + cnt, idx.size());
    const auto strip = core::StripTris(std::span<const uint16_t>(idx.data() + lo, size_t(hi > lo ? hi - lo : 0)));
    for (const auto& tri : strip) {
      std::array<int, 3> t{};
      for (int ti = 0; ti < 3; ++ti) {
        const size_t j = tri[size_t(ti)];
        if (j >= pos.size()) Fail("IndexError: list index out of range");
        std::array<int64_t, 3> key{};
        for (int c = 0; c < 3; ++c) {
          const double r = num::RoundHalfEven(pos[j][size_t(c)] / kWeld);  // builtin round(): ties to even
          if (std::fabs(r) >= 9.0e18) Fail("not ported yet: a weld key beyond 64 bits");
          key[size_t(c)] = static_cast<int64_t>(r);
        }
        auto it = key2node.find(key);
        int n;
        if (it == key2node.end()) {
          n = int(nodes.size());
          key2node.emplace(key, n);
          nodes.push_back(pos[j]);
          writes.emplace_back();
        } else {
          n = it->second;
        }
        writes[size_t(n)].insert(int(j));
        t[size_t(ti)] = n;
      }
      if (t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) tris.push_back(t);
    }
  }
  const std::vector<Vec3>& P = nodes;
  const int64_t nn = int64_t(P.size());
  // worst = max(float(np.linalg.norm(np.array(pos[j]) - P[n])) for n, js in enumerate(writes) for j in js)  (line 180)
  bool any_write = false;
  double worst = 0.0;
  for (size_t n = 0; n < writes.size(); ++n)
    for (int j : writes[n]) {
      const double w = Norm1D(Sub(pos[size_t(j)], P[n]));
      if (!any_write || w > worst) worst = w;
      any_write = true;
    }
  if (!any_write) Fail("ValueError: max() arg is an empty sequence");
  Assert(worst < 0.001, "('a vertex is far from its node', " + core::ReprD(worst) + ")");

  // pins: the top boundary loop (lines 182-207)
  std::vector<Edge> ecount_keys;  // Counter insertion order
  std::map<Edge, int> ecount;
  for (const auto& t : tris) {
    const int pr[3][2] = {{t[0], t[1]}, {t[1], t[2]}, {t[2], t[0]}};
    for (const auto& e : pr) {
      const Edge key = MakeEdge(e[0], e[1]);
      auto it = ecount.find(key);
      if (it == ecount.end()) {
        ecount.emplace(key, 1);
        ecount_keys.push_back(key);
      } else {
        ++it->second;
      }
    }
  }
  if (ecount.empty()) Fail("ValueError: max() arg is an empty sequence");
  int max_count = 0;
  for (const auto& kv : ecount) max_count = std::max(max_count, kv.second);
  Assert(max_count <= 2, "the skirt is not manifold");
  std::vector<int> badj_keys;  // defaultdict insertion order
  std::map<int, std::vector<int>> badj;
  const auto badj_add = [&](int a, int b) {
    auto it = badj.find(a);
    if (it == badj.end()) {
      badj_keys.push_back(a);
      it = badj.emplace(a, std::vector<int>{}).first;
    }
    it->second.push_back(b);
  };
  for (const Edge& e : ecount_keys)
    if (ecount[e] == 1) {
      badj_add(e.first, e.second);
      badj_add(e.second, e.first);
    }
  std::set<int> seen_nodes;
  std::vector<std::vector<int>> loops;
  for (int s : badj_keys) {
    if (seen_nodes.count(s)) continue;
    std::vector<int> loop, st{s};
    while (!st.empty()) {
      const int x = st.back();
      st.pop_back();
      if (seen_nodes.count(x)) continue;
      seen_nodes.insert(x);
      loop.push_back(x);
      const auto& nb = badj[x];
      st.insert(st.end(), nb.begin(), nb.end());
    }
    loops.push_back(std::move(loop));
  }
  std::string band_lens = "[";
  for (size_t i = 0; i < loops.size(); ++i) band_lens += (i ? ", " : "") + Str(int64_t(loops[i].size()));
  Assert(loops.size() == 2, "('expected a band: a hem and a top', " + band_lens + "])");
  // max(loops, key=lambda L: float(P[L, 1].mean())): the first loop wins a tie; P[L, 1] is in the loop's order
  size_t top = 0;
  double top_key = 0.0;
  for (size_t li = 0; li < loops.size(); ++li) {
    std::vector<double> ys;
    for (int x : loops[li]) ys.push_back(P[size_t(x)][1]);
    const double key = LoopKey(ys);  // line 207 (census: 0 mismatches)
    if (li == 0 || key > top_key) {
      top = li;
      top_key = key;
    }
  }
  std::vector<int> pins = loops[top];
  std::sort(pins.begin(), pins.end());
  const std::set<int> pinset(pins.begin(), pins.end());

  const Derived dv = Derive(P, tris);
  // hops over type 1 only (lines 210-218)
  std::map<int, int> hop;
  std::vector<int> bfs(pins.begin(), pins.end());
  for (int x : pins) hop[x] = 0;
  for (size_t qi = 0; qi < bfs.size(); ++qi) {
    const int x = bfs[qi];
    auto it = dv.adj1.find(x);
    if (it == dv.adj1.end()) continue;
    for (int y : it->second)
      if (!hop.count(y)) {
        hop[y] = hop[x] + 1;
        bfs.push_back(y);
      }
  }
  Assert(int64_t(hop.size()) == nn, "('nodes the pins do not reach', " + Str(nn - int64_t(hop.size())) + ")");
  // springs per node, type 1, 2, 3 (lines 220-227)
  std::vector<std::vector<Spring>> springs(static_cast<size_t>(nn));
  const std::set<Edge>* kinds[3] = {&dv.t1, &dv.t2, &dv.t3};
  for (int typ = 1; typ <= 3; ++typ)
    for (const Edge& e : *kinds[typ - 1]) {
      const double rest = Norm1D(Sub(P[size_t(e.first)], P[size_t(e.second)]));  // line 223
      springs[size_t(e.first)].push_back(Spring{e.second, rest, typ});
      springs[size_t(e.second)].push_back(Spring{e.first, rest, typ});
    }
  for (auto& lst : springs)
    std::stable_sort(lst.begin(), lst.end(), [](const Spring& a, const Spring& b) {
      return std::tie(a.typ, a.target) < std::tie(b.typ, b.target);
    });
  std::vector<std::map<int, int>> index_of(static_cast<size_t>(nn));
  for (size_t n = 0; n < springs.size(); ++n)
    for (size_t i = 0; i < springs[n].size(); ++i)
      if (springs[n][i].typ == 1) index_of[n][springs[n][i].target] = int(i);
  // normal fans: a pair per quad corner, a pair per leftover triangle (lines 229-251)
  std::vector<std::vector<std::pair<int, int>>> fans(static_cast<size_t>(nn));
  std::set<int> merged;
  for (const Edge& e : dv.diag) {
    const int x = e.first, y = e.second;
    const auto& ets = dv.edge_tris.at(e);
    const int ta = ets[0].first, tb = ets[1].first;
    merged.insert(ta);
    merged.insert(tb);
    const auto& A = tris[size_t(ta)];
    int i = 0;
    while (i < 3 && A[size_t(i)] != x) ++i;
    if (i == 3) Fail("ValueError: tuple.index(x): x not in tuple");
    int first, second;
    if (A[size_t((i + 1) % 3)] == y) {
      first = ta;
      second = tb;
    } else {
      first = tb;
      second = ta;
    }
    const auto& a_tri = tris[size_t(first)];
    const auto& b_tri = tris[size_t(second)];
    int za = -1, zb = -1;
    for (int v : a_tri)
      if (v != x && v != y) {
        za = v;
        break;
      }
    for (int v : b_tri)
      if (v != x && v != y) {
        zb = v;
        break;
      }
    if (za < 0 || zb < 0) Fail("StopIteration");
    const int cycle[4] = {x, zb, y, za};  // first holds x->y, second y->x: the quad's outline in winding order
    for (int i2 = 0; i2 < 4; ++i2) fans[size_t(cycle[i2])].emplace_back(cycle[(i2 + 1) % 4], cycle[(i2 + 3) % 4]);
  }
  for (size_t t = 0; t < tris.size(); ++t) {
    if (merged.count(int(t))) continue;
    const auto& [a, b, c] = tris[t];
    fans[size_t(a)].emplace_back(b, c);
    fans[size_t(b)].emplace_back(c, a);
    fans[size_t(c)].emplace_back(a, b);
  }
  // faces (lines 253-257)
  std::vector<std::vector<int>> frefs(static_cast<size_t>(nn));
  for (size_t t = 0; t < tris.size(); ++t)
    for (int c : tris[t]) frefs[size_t(c)].push_back(int(t));
  {
    bool any = false;
    size_t mx = 0;
    for (const auto& v : frefs)
      if (!v.empty()) {
        any = true;
        mx = std::max(mx, v.size());
      }
    if (!any) Fail("ValueError: max() arg is an empty sequence");
    Assert(mx <= 44, "too many faces on a node");
  }
  // threads: strands from pins, closed rows (lines 259-272)
  std::vector<std::vector<int>> strands, rings;
  for (const Line& ln : StraightLines(P, dv.adj1)) {
    std::vector<int> s = ln.seq;
    // steps = np.array([P[b] - P[a] for a, b in zip(s, s[1:])]); one row per step
    std::vector<Vec3> steps;
    for (size_t i = 0; i + 1 < s.size(); ++i) steps.push_back(Sub(P[size_t(s[i + 1])], P[size_t(s[i])]));
    const bool vertical = VerticalRatio(steps) > 0.6;  // line 262
    if (vertical && !ln.closed) {
      if (pinset.count(s.back()) && !pinset.count(s.front())) std::reverse(s.begin(), s.end());
      if (pinset.count(s.front()) && s.size() > 2) strands.push_back(std::move(s));
    } else if (ln.closed && !vertical) {
      bool any_pin = false;
      for (int x : s) any_pin = any_pin || pinset.count(x);
      if (!any_pin) rings.push_back(std::move(s));
    }
  }
  const bool soft = SoftHem(spec.design);
  if (soft) rings.clear();

  // ---------------- bytes (lines 275-417)
  const auto nd = [](int64_t i) { return uint64_t(0x40 + 48 * i); };
  const int64_t n = nn;
  Blob grid;
  PutTag(grid, "GRID_DATA");
  for (uint64_t w : {1u, 0x30u, 0u, 0u}) PutU32(grid, w);
  for (uint64_t w : {uint64_t(0x40), uint64_t(n), uint64_t(0), uint64_t(tris.size())}) PutU32(grid, w);  // faces offset patched below
  PutTag(grid, "GRID");
  Assert(grid.size() == 0x40, "grid head");
  const size_t node_at = grid.size();
  grid.insert(grid.end(), size_t(48 * n), uint8_t(0));
  PutTag(grid, "EDGE");
  std::vector<size_t> spring_at(static_cast<size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    spring_at[size_t(i)] = grid.size();
    for (const Spring& sp : springs[size_t(i)]) {
      PutU32(grid, nd(sp.target));
      PutF32(grid, sp.rest);
      PutU32(grid, uint64_t(sp.typ));
      PutU32(grid, 0);
    }
  }
  Pad(grid);
  PutTag(grid, "TRIANGLE");
  std::vector<size_t> fan_at(static_cast<size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    fan_at[size_t(i)] = grid.size();
    for (const auto& [a, b] : fans[size_t(i)]) {
      PutU32(grid, nd(a));
      PutU32(grid, nd(b));
    }
  }
  Pad(grid);
  PutTag(grid, "FACE_TABLE");
  std::vector<size_t> fref_at(static_cast<size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    fref_at[size_t(i)] = grid.size();
    grid.insert(grid.end(), 4 * frefs[size_t(i)].size(), uint8_t(0));
  }
  Pad(grid);
  PutTag(grid, "FACE");
  const size_t face_at = grid.size();
  for (const auto& [a, b, c] : tris) {
    PutU32(grid, 0);
    PutU32(grid, nd(a));
    PutU32(grid, nd(b));
    PutU32(grid, nd(c));
  }
  Pad(grid);
  for (int64_t i = 0; i < n; ++i) {
    for (size_t z = 0; z < frefs[size_t(i)].size(); ++z)
      PackInto(grid, int64_t(fref_at[size_t(i)] + 4 * z), {face_at + 16 * uint64_t(frefs[size_t(i)][z])});
    const Vec3& pt = P[size_t(i)];
    const int64_t na = int64_t(node_at) + 48 * i;
    for (int c = 0; c < 3; ++c) PackIntoF(grid, na + 4 * c, pt[size_t(c)]);
    PackInto(grid, na + 12, {0x3F800000});
    PackInto(grid, na + 0x10,
             {springs[size_t(i)].size(), spring_at[size_t(i)], fans[size_t(i)].size(), fan_at[size_t(i)],
              frefs[size_t(i)].size(), fref_at[size_t(i)], uint64_t(hop[int(i)]), 0});
  }
  PackInto(grid, 0x18, {grid.size() - 0x30, grid.size()});
  PackInto(grid, 0x28, {face_at});
  // pins
  Blob fix;
  PutTag(fix, "FIX_GRID");
  for (uint64_t w : {uint64_t(1), uint64_t(0x20), uint64_t(0x20), uint64_t(pins.size())}) PutU32(fix, w);
  for (int i : pins) {
    for (int c = 0; c < 3; ++c) PutF32(fix, P[size_t(i)][size_t(c)]);
    PutU32(fix, 0x3F800000);
    PutI32(fix, 2);
    PutU32(fix, nd(i));
    PutU32(fix, 2);
    PutU32(fix, 0);
  }
  // writes
  Blob gw;
  PutTag(gw, "GRID_WRITE");
  for (uint64_t w : {uint64_t(1), uint64_t(0x60), uint64_t(oid), uint64_t(0x60)}) PutU32(gw, w);
  uint64_t total = 0;
  for (const auto& v : writes) total += v.size();
  for (uint64_t w : {uint64_t(n), total, uint64_t(0), uint64_t(0)}) PutU32(gw, w);
  PutI32(gw, 2);
  gw.insert(gw.end(), 0x60 - gw.size(), uint8_t(0));
  for (int64_t i = 0; i < n; ++i) {
    PutU32(gw, 0x80000000ull | uint64_t(i));
    for (int j : writes[size_t(i)]) PutU32(gw, (uint64_t(k) << 24) | (uint64_t(j) * uint64_t(stride)));
  }
  PutU32(gw, 0xFFFFFFFF);
  Pad(gw);
  // threads: the donor's bend limit block (lines 335-376)
  const auto bu = [&](int64_t o) { return int64_t(U32(bend, o)); };
  int64_t mm = -1;
  {
    static const char kTag[] = "MULTI_CLOTH";  // b'MULTI_CLOTH\0': 11 characters and a zero byte
    for (size_t i = 0; i + 12 <= bend.size(); ++i)
      if (std::equal(kTag, kTag + 11, bend.begin() + std::ptrdiff_t(i)) && bend[i + 11] == 0) {
        mm = int64_t(i);
        break;
      }
  }
  if (mm < 0) Fail("ConvertError: the bend donor has no MULTI_CLOTH");
  const int64_t mc0 = mm + bu(mm + bu(mm + 0x1C));
  const int64_t th0 = mc0 + bu(mc0 + 0x34);
  const int64_t pool_off = th0 + bu(th0 + 0x28);
  const auto pool_span = Rd(bend).Slice(uint64_t(pool_off), uint64_t(pool_off) + 0x40);
  const Blob pool(pool_span.begin(), pool_span.end());
  Assert(U32(pool, 4) == 0x40 && I32(pool, 0x2C) == 2, "unexpected bend block");
  Blob th;
  PutTag(th, "THREAD_DATA");
  th.insert(th.end(), 0x20, uint8_t(0));
  const size_t table_a = th.size();
  th.insert(th.end(), 24 * strands.size(), uint8_t(0));
  const size_t table_b = th.size();
  th.insert(th.end(), 24 * rings.size(), uint8_t(0));
  const auto entries = [&](const std::vector<int>& line, bool closed) {
    const size_t at = th.size();
    const size_t count = line.size();
    for (size_t i2 = 0; i2 < count; ++i2) {
      const int x = line[i2];
      std::optional<int> nx, pv;
      if (closed || i2 + 1 < count) nx = line[(i2 + 1) % count];
      if (closed || i2 > 0) pv = line[(i2 + count - 1) % count];
      const auto idx_of = [&](int a) {
        auto it = index_of[size_t(x)].find(a);
        if (it == index_of[size_t(x)].end()) Fail("KeyError: " + Str(a));
        return it->second;
      };
      PutU32(th, nd(x));
      PutI32(th, nx ? idx_of(*nx) : -1);
      PutI32(th, pv ? idx_of(*pv) : -1);
    }
    return at;
  };
  std::vector<size_t> entries_at, ring_at;
  for (const auto& s : strands) entries_at.push_back(entries(s, false));
  for (const auto& s : rings) ring_at.push_back(entries(s, true));
  Pad(th, 4);
  std::vector<size_t> sub_at;
  for (size_t i = 0; i < strands.size(); ++i) {
    sub_at.push_back(th.size());
    th.insert(th.end(), 4, uint8_t(0));
  }
  Pad(th);
  const size_t pool_at = th.size();
  th.insert(th.end(), pool.begin(), pool.end());
  for (size_t i2 = 0; i2 < strands.size(); ++i2) {
    PackInto(th, int64_t(sub_at[i2]), {pool_at});
    // the bend limit holds each strand nearly straight from the hip (SOFT_HEM suits have no subs)
    const uint64_t sub_o = soft ? 0 : sub_at[i2], sub_n = soft ? 0 : 1;
    PackInto(th, int64_t(table_a + 24 * i2), {1, 0x18, entries_at[i2], strands[i2].size(), sub_o, sub_n});
  }
  for (size_t i2 = 0; i2 < rings.size(); ++i2)
    PackInto(th, int64_t(table_b + 24 * i2), {3, 0x18, ring_at[i2], rings[i2].size(), 0, 0});
  PackInto(th, 0x10, {1, 0x30, table_a, strands.size(), table_b, rings.size(), pool_at, 1});
  // the donor's PROPERTY and colliders (lines 377-387)
  const DonorProp* dp = nullptr;
  for (const DonorProp& e : kDonorProperty)
    if (spec.design == e.design) dp = &e;
  if (!dp) Fail("KeyError: " + Q(spec.design));
  const std::string donor_name = dp->name;
  const int64_t mcd = dp->mc;
  const int64_t c0 = mcd + int64_t(U32(donor, mcd + int64_t(U32(donor, mcd + 0x1C))));
  {
    const auto tag = Rd(donor).Slice(uint64_t(c0), uint64_t(c0) + 10);
    const auto nm = Rd(donor).Slice(uint64_t(c0) + 0x70, uint64_t(c0) + 0x78);
    const std::string want_tag = "CLOTH_DATA";
    Assert(std::string(tag.begin(), tag.end()) == want_tag && std::string(nm.begin(), nm.end()) == donor_name,
           "('donor is not', b" + Q(donor_name) + ")");
  }
  const int64_t prop_off = c0 + int64_t(U32(donor, c0 + 0x20));
  const auto prop_span = Rd(donor).Slice(uint64_t(prop_off), uint64_t(prop_off) + 0xA0);
  Blob prop(prop_span.begin(), prop_span.end());
  if (soft) {
    PackIntoF(prop, 0x5C, kSoftHemStrength);
    PackIntoF(prop, 0x34, 2.0 * F32(prop, 0x34));  // twice the fall: it lies down over the thigh
  }
  const int64_t chain_a = c0 + int64_t(U32(donor, c0 + 0x38));
  const int64_t chain_b = c0 + 0x70 + int64_t(U32(donor, c0 + 0x18));
  const auto chain_span = Rd(donor).Slice(uint64_t(chain_a), uint64_t(chain_b));
  const Blob chain(chain_span.begin(), chain_span.end());
  // the cloth
  Blob cloth;
  PutTag(cloth, "CLOTH_DATA");
  cloth.insert(cloth.end(), 0x70 - cloth.size(), uint8_t(0));
  cloth.push_back(uint8_t(donor_name[0]));  // donor_name[:2] + b'_gen\0\0' + bytes(8)
  cloth.push_back(uint8_t(donor_name[1]));
  for (char ch : {'_', 'g', 'e', 'n', '\0', '\0'}) cloth.push_back(uint8_t(ch));
  cloth.insert(cloth.end(), 8, uint8_t(0));
  const size_t prop_at = cloth.size();
  cloth.insert(cloth.end(), prop.begin(), prop.end());
  Pad(cloth);
  const size_t grid_at = cloth.size();
  cloth.insert(cloth.end(), grid.begin(), grid.end());
  const size_t fix_at = cloth.size();
  cloth.insert(cloth.end(), fix.begin(), fix.end());
  Pad(cloth);
  const size_t gw_at = cloth.size();
  cloth.insert(cloth.end(), gw.begin(), gw.end());
  const size_t th_at = cloth.size();
  cloth.insert(cloth.end(), th.begin(), th.end());
  Pad(cloth);
  const size_t chain_at = cloth.size();
  cloth.insert(cloth.end(), chain.begin(), chain.end());
  Pad(cloth);
  PackInto(cloth, 0x10, {1, 0x70, cloth.size() - 0x70, 0});
  PackInto(cloth, 0x20, {prop_at, grid_at, 0, fix_at, gw_at, th_at, chain_at, 0x70});
  Blob chunk;
  PutTag(chunk, "MULTI_CLOTH");
  for (uint64_t w : {2u, 0u, 1u, 0x40u, 0u}) PutU32(chunk, w);
  chunk.insert(chunk.end(), 0x40 - chunk.size(), uint8_t(0));
  PutU32(chunk, 0x50);
  chunk.insert(chunk.end(), 12, uint8_t(0));
  chunk.insert(chunk.end(), cloth.begin(), cloth.end());
  Walk(chunk, 0);
  return chunk;
}

}  // namespace doa4::suitconv::gencloth
