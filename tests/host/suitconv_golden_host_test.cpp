// Golden test for the suit converter: the disc-free check a second PC can run (and the friend can run on theirs). Synthetic
// inputs only, built from seeds in suitconv_synth.h and here; no environment variable, no disc, no game file, no input file.
// Every case runs a converter path on its synthetic input and prints the SHA-1 of what came out beside the pinned SHA-1:
//   golden <case>: <sha1> equal          (or "DIFFERENT, pinned <sha1>")
// and the last lines are "golden: N of N equal" and the default summary line ("PASSED: N checks, 0 failures"). A converter
// whose arithmetic differs on this PC (a different libm, a fused multiply-add, a changed sort order, a different rounding
// mode) changes at least one hash.
//
// THE PINS COME FROM THIS CODE ON THE AUTHOR'S PC. They were written down from the first passing run, not from the Python
// converter: they say "this PC's C++ gives the same bytes as the author's", which is the cross-PC question. That the C++
// equals the Python interpreter is shown elsewhere, byte for byte over the real files (the disc-gated sections of the
// convert test, the job test and the num, sort and cloth pins). A pin that no code change can move would be worthless, so
// the author ran probes (an argsort that is stable, Exp one ulp off, a plain dot product in place of the fused one, Gemv3
// adding in another order, a naive sum in place of Python's) and each changed at least one hash below.
//
// Cases and what they reach:
//   numeric             the numeric primitives on seeded random inputs (thousands of values each, so a last-bit difference shows):
//                       Gemv3, the fused dot, math.dist, Python's sum, Exp, Atan2, numpy's sums, percentile, median and
//                       linspace, Python's floor division and round, the float and half packing, SoftFma, and np.argsort on
//                       arrays full of ties. The small cases below end in float32 or half bytes, which hide a last-bit
//                       difference in a double; this case is where such a difference is caught.
//   geometry            the geometry core at L0 on the synthetic suit (decode, bind, weights, rules, encode): both files, and the
//                       stage trace; a variant whose accessory has NaN positions
//   waist               L2: prep_waist_fit, the waist weights (Exp, floor division, percentiles), the h4 doubles themselves
//   bust                prep_bust on the fixture (PySum of the morph offsets, the strap fade), with a cloth fade; bust_peak
//                       and build_mpv on a hand-made chest
//   cloth_fit_capsule   fit_cloth_chunk on a hand-built chunk with capsules: np.linalg.norm and dot (NpDotFused), (P - p0) @ u
//                       over several rows (Gemv3), the percentile, the unstick rounds
//   cloth_weld_ties     weld_chest_cloth on lattices whose neighbours are exactly equidistant: np.argsort ties (the
//                       introsort port), np.sum, the weights
//   gencloth            cloth_gen.build on the hand-made skirt, both designs (the chunk's bytes), with the recipe's own
//                       SHA-1 of the same chunk as a second check
// NOT COVERED: the hands (prep_hands, build_hands, the donor's request table, the hand channels): the converter reads them
// from a donor model with no synthetic stand-in, so nothing here reaches that code. The player's disc covers it (the job
// test's real import), not this test. The authored cloth is reached below the full suit: no synthetic suit drives
// ConvertSuit through its cloth stages.

#include "../../src/doa4_suitconv.h"
#include "../../src/doa4_suitconv_bust.h"
#include "../../src/doa4_suitconv_cloth.h"
#include "../../src/doa4_suitconv_core.h"
#include "../../src/doa4_suitconv_gencloth.h"
#include "../../src/doa4_suitconv_num.h"
#include "../../src/doa4_suitconv_recipe.h"
#include "../../src/doa4_suitconv_sort.h"
#include "../../src/doa4_suitconv_trace.h"
#include "suitconv_synth.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace doa4::suitconv;
namespace core = doa4::suitconv::core;
namespace cb = doa4::suitconv::bust;
namespace cl = doa4::suitconv::cloth;
namespace gc = doa4::suitconv::gencloth;
namespace gensy = synth::gensy;
using synth::conv::SynthGirl;
using synth::conv::SynthInputs;
using synth::conv::SynthSuit;

static int g_checks = 0;
static int g_failures = 0;
static int g_golden = 0, g_golden_equal = 0;

static void Check(bool ok, const char* file, int line, const char* expr, const char* fmt = nullptr, ...) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
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
  std::fflush(stdout);
}
#define CK(cond) Check((cond), __FILE__, __LINE__, #cond)
#define CKF(cond, ...) Check((cond), __FILE__, __LINE__, #cond, __VA_ARGS__)

// ---------------------------------------------------------------------------------------------------------------
// The pins: this PC's first passing run (see the header). A case with no pin fails and prints its hash.
// ---------------------------------------------------------------------------------------------------------------
struct Pin {
  const char* name;
  const char* sha1;
};
static const Pin kGolden[] = {
    // GOLDEN-PINS-BEGIN
    {"num.gemv3", "f8e3175015a91def4c01fafaf36245bf332bc435"},
    {"num.dot_fused", "26564352e430160dbddcc5f0e59243d083817e74"},
    {"num.math_dist", "eecfc9e5912c3ab04c4d7a22a7a70cc025b51d95"},
    {"num.pysum", "ca9af98eba4cdb10b477267318161cce987f000a"},
    {"num.exp", "40d19f6a098c1ddf7ea74912900ed4b6e84bd27e"},
    {"num.atan2", "eddbf878ad67612ae4c1bf4b40a2b982986970fa"},
    {"num.np_reductions", "63615a31c170d65b9acf71608c589c4e8cd5138d"},
    {"num.linspace_radians", "87af5620ad103c4491f783a610b1372889199871"},
    {"num.rounding_packing", "ecd543ffe851117f8cc57cc77c3f244d0aeca162"},
    {"num.softfma", "3f1099cae2e0b12dc533bbc92443741834936013"},
    {"num.argsort_ties", "be895884fc975d5474bc700c14c7f3dd7d6270c8"},
    {"geometry.tpr", "84ae6b3647330bf8eb7dbc5fa64ca1be20f97eb9"},
    {"geometry.cat", "3364fe4086bf39991584456fd2fe3a905268da23"},
    {"geometry.trace", "673ab25fe36bf4d62db6e48111864bb53c1fa467"},
    {"geometry_nan_accessory.tpr", "e15e41a10fecbab4d2d5f409c73e663936ab0c30"},
    {"geometry_nan_accessory.cat", "3364fe4086bf39991584456fd2fe3a905268da23"},
    {"geometry_nan_accessory.trace", "9a41d3132df29520ec79c2d1a0ea7b37461e8463"},
    {"waist.tpr", "0e30d54774b74f06bb5fceb39cb7d8015364ef0a"},
    {"waist.cat", "3364fe4086bf39991584456fd2fe3a905268da23"},
    {"waist.trace", "9abdfdb3bf2d5c41b3021e5322b2d686ba098ef0"},
    {"waist.h4_doubles", "b7acffed69c9112ee74dcbbad02386df03194b9e"},
    {"bust.prep", "4ab3abec064c2e1d603ab3d12e0bc5eb93525788"},
    {"bust.prep_cloth_fade", "0bca379f3eebc02f4297ffcd77982b39944f6e66"},
    {"bust.mpv", "36ba287ba409cc7779baab229cd4e88f21ebb063"},
    {"bust.peak", "f3cdb382e091b7ca246896ce08e5ec84985640d1"},
    {"cloth_fit_capsule.chunk", "f85fcafcac204eba4a81a2bae08bd21de89873f1"},
    {"cloth_weld_ties.verts", "b2810fcadb5d7f36c4f6fde075e2762a8beb1ba0"},
    {"gencloth.vks_on_o", "3a012dac510bf2b612ddd36da3c733ad88832148"},
    {"gencloth.vle_sk_h", "2d7e599e93107b2dedd109cd16d577ee41611442"},
    // GOLDEN-PINS-END
    {nullptr, nullptr},
};

static std::string PinOf(const std::string& name) {
  for (const Pin& p : kGolden)
    if (p.name && name == p.name) return p.sha1;
  return "";
}

static void Golden(const std::string& name, const std::vector<uint8_t>& bytes) {
  const std::string got = doa4::gamefiles::Sha1Hex(bytes.data(), bytes.size());
  const std::string want = PinOf(name);
  ++g_golden;
  const bool eq = got == want;
  g_golden_equal += eq;
  if (eq) {
    std::printf("golden %s: %s equal\n", name.c_str(), got.c_str());
  } else if (want.empty()) {
    std::printf("golden %s: %s NOT PINNED\n", name.c_str(), got.c_str());
  } else {
    std::printf("golden %s: %s DIFFERENT, pinned %s\n", name.c_str(), got.c_str(), want.c_str());
  }
  CKF(eq, "%s", name.c_str());
}

static void AddBytes(std::vector<uint8_t>& out, std::string_view s) { out.insert(out.end(), s.begin(), s.end()); }
static void AddBits(std::vector<uint8_t>& out, double d) {
  const uint64_t u = std::bit_cast<uint64_t>(d);
  for (int i = 0; i < 8; ++i) out.push_back(uint8_t(u >> (8 * i)));
}
static void AddVecs(std::vector<uint8_t>& out, const std::vector<core::Vec3>& v) {
  for (const core::Vec3& x : v)
    for (double c : x) AddBits(out, c);
}

// Keeps every trace line and every h4 row (the doubles behind a `B p<i>.h4` line).
class GoldenTrace : public Trace {
 public:
  void Line(std::string_view s) override { text.append(s).push_back('\n'); }
  void H4(int index, const std::vector<std::array<double, 4>>& rows) override {
    AddBits(h4, double(index));
    for (const auto& r : rows)
      for (double c : r) AddBits(h4, c);
  }
  std::string text;
  std::vector<uint8_t> h4;
};

// ---------------------------------------------------------------------------------------------------------------
// numeric
// ---------------------------------------------------------------------------------------------------------------
struct Rng {
  uint64_t s;
  uint64_t Next() {  // xorshift64*
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ull;
  }
  // A double with a random sign and mantissa and an exponent in [lo, hi].
  double Wide(int lo, int hi) {
    const uint64_t mant = Next() & ((uint64_t(1) << 52) - 1);
    const int e = lo + int(Next() >> 33) % (hi - lo + 1);
    const uint64_t sign = Next() >> 63;
    return std::bit_cast<double>((sign << 63) | (uint64_t(e + 1023) << 52) | mant);
  }
  double Unit() { return double(Next() >> 11) / double(uint64_t(1) << 53); }  // [0, 1)
};

static void CaseNumeric() {
  const num::FpEnvGuard fp;
  Rng rng{0x9E3779B97F4A7C15ull};
  constexpr int kN = 4096;
  std::vector<uint8_t> out;
  const auto flush = [&](const char* name) {
    Golden(name, out);
    out.clear();
  };
  for (int i = 0; i < kN; ++i) {  // (P - p0) @ u over several rows, and the 3-vector dot of a ddot
    const core::Vec3 a{rng.Wide(-6, 3), rng.Wide(-6, 3), rng.Wide(-6, 3)}, u{rng.Wide(-3, 1), rng.Wide(-3, 1), rng.Wide(-3, 1)};
    AddBits(out, cl::Gemv3(a, u));
  }
  flush("num.gemv3");
  for (int i = 0; i < kN; ++i) {
    double x[7], y[7];
    for (int k = 0; k < 7; ++k) x[k] = rng.Wide(-4, 4), y[k] = rng.Wide(-4, 4);
    AddBits(out, num::NpDotFused(x, y, 3));
    AddBits(out, num::NpDotFused(x, y, 7));
  }
  flush("num.dot_fused");
  for (int i = 0; i < kN; ++i) {
    const core::Vec3 p{rng.Wide(-4, 2), rng.Wide(-4, 2), rng.Wide(-4, 2)}, q{rng.Wide(-4, 2), rng.Wide(-4, 2), rng.Wide(-4, 2)};
    AddBits(out, num::MathDist3(p, q));
  }
  flush("num.math_dist");
  for (int i = 0; i < 256; ++i) {  // Python's sum: cancellation and mixed magnitudes, where Neumaier differs from a naive sum
    num::PySum s;
    const int len = 2 + int(rng.Next() >> 40) % 300;
    for (int k = 0; k < len; ++k) s.Add(rng.Wide(-12, 12));
    AddBits(out, s.Result());
  }
  flush("num.pysum");
  for (int i = 0; i < kN; ++i) AddBits(out, num::Exp(80.0 * rng.Unit() - 40.0));
  flush("num.exp");
  for (int i = 0; i < kN; ++i) AddBits(out, num::Atan2(rng.Wide(-3, 3), rng.Wide(-3, 3)));
  flush("num.atan2");
  for (int i = 0; i < 64; ++i) {  // numpy's pairwise sums over lengths from a few to several buffer chunks
    const size_t len = 1 + size_t(rng.Next() >> 40) % (i % 4 == 0 ? 20000 : 700);
    std::vector<double> v(len);
    for (double& x : v) x = rng.Wide(-8, 8);
    AddBits(out, num::NpSum1D(v));
    AddBits(out, num::NpMean1D(v));
    const size_t cols = 2 + size_t(i % 5), rows = len / cols;
    if (rows == 0) continue;
    std::vector<double> res(cols);
    num::NpSumAxis0(std::span<const double>(v.data(), rows * cols), cols, res);
    for (double c : res) AddBits(out, c);
    num::NpMeanAxis0(std::span<const double>(v.data(), rows * cols), cols, res);
    for (double c : res) AddBits(out, c);
    num::NpMedianAxis0(std::span<const double>(v.data(), rows * cols), cols, res);
    for (double c : res) AddBits(out, c);
    AddBits(out, num::NpPercentileLinear(v, 97.0));
    AddBits(out, num::NpPercentileLinear(v, 100.0 * rng.Unit()));
  }
  flush("num.np_reductions");
  for (int i = 0; i < 64; ++i) {
    const double a = rng.Wide(-8, 8), b = rng.Wide(-8, 8);
    for (double c : num::NpLinspace(a, b, 2 + i % 60)) AddBits(out, c);
    AddBits(out, num::NpRadians(rng.Wide(-4, 9)));
  }
  flush("num.linspace_radians");
  for (int i = 0; i < kN; ++i) {  // Python's x // y, round(x, n), round(x), struct.pack f and e
    const double x = rng.Wide(-10, 10), y = rng.Wide(-10, 10);
    AddBits(out, num::PyFloorDiv(x, y));
    AddBits(out, num::PyRoundDigits(rng.Wide(-6, 6), 1 + i % 6));
    AddBits(out, num::RoundHalfEven(rng.Wide(-4, 20)));
    AddBits(out, double(num::PackF32(rng.Wide(-30, 30))));
    const uint16_t half = num::PackF16(rng.Wide(-12, 14));
    AddBits(out, double(half));
    AddBits(out, num::HalfToDouble(uint16_t(rng.Next() >> 48)));
  }
  flush("num.rounding_packing");
  for (int i = 0; i < kN; ++i) AddBits(out, num::SoftFma(rng.Wide(-6, 6), rng.Wide(-6, 6), rng.Wide(-12, 6)));
  flush("num.softfma");
  for (int i = 0; i < 64; ++i) {  // np.argsort of arrays with ties: the order of the equal values
    const size_t len = 17 + size_t(rng.Next() >> 40) % 400;
    const int distinct = 2 + int(rng.Next() >> 40) % 12;
    std::vector<double> v(len);
    for (double& x : v) x = double(int(rng.Next() >> 40) % distinct) * 0.25;
    for (int64_t k : num::NpArgsort(v)) AddBits(out, double(k));
  }
  flush("num.argsort_ties");
}

// ---------------------------------------------------------------------------------------------------------------
// geometry, waist
// ---------------------------------------------------------------------------------------------------------------
static void ConvertCase(const char* name, const SuitRecipe& suit, const SuitInputs& in, Level level) {
  GoldenTrace t;
  ConvertFailure f;
  const auto r = ConvertSuit(suit, SynthGirl(), in, level, nullptr, &t, &f);
  CKF(r.has_value(), "%s failed at %s: %s", name, f.stage.c_str(), f.what.c_str());
  if (!r) return;
  Golden(std::string(name) + ".tpr", r->tpr);
  Golden(std::string(name) + ".cat", r->cat);
  Golden(std::string(name) + ".trace", std::vector<uint8_t>(t.text.begin(), t.text.end()));
  if (!t.h4.empty()) Golden(std::string(name) + ".h4_doubles", t.h4);
}

static void CaseGeometry() {
  ConvertCase("geometry", SynthSuit(), SynthInputs(synth::conv::MakeInputs()), Level::kL0);
  ConvertCase("geometry_nan_accessory", SynthSuit(), SynthInputs(synth::conv::MakeInputs(true)), Level::kL0);
}

static void CaseWaist() {
  SuitInputs in = SynthInputs(synth::conv::MakeInputs());
  in.waist_tpr = std::make_shared<const std::vector<uint8_t>>(synth::conv::MakeWaistTemplateTpr(0.2f));
  SuitRecipe nob = SynthSuit();
  nob.bust_down = nob.bust_up = 0.0;  // no bust: the MPV is not asked for
  ConvertCase("waist", nob, in, Level::kL2);
}

// ---------------------------------------------------------------------------------------------------------------
// bust
// ---------------------------------------------------------------------------------------------------------------
static void AddMorph(std::vector<uint8_t>& out, const core::Part& p) {
  AddBytes(out, p.writable ? "w" : "-");
  if (p.bust) {
    AddVecs(out, p.bust->first);
    AddVecs(out, p.bust->second);
  } else {
    AddBytes(out, "nobust");
  }
  if (p.border)
    for (int64_t j : *p.border) AddBits(out, double(j));
}

static void CaseBust() {
  const num::FpEnvGuard fp;
  {
    synth::conv::BustFixture fx;
    cb::PrepBust(fx.objects, fx.parts, fx.chan2id, 0.25, 0.5, {}, &fx.strap_model, 0.04);
    std::vector<uint8_t> out;
    for (const core::Part* p : {fx.morph, fx.reversed, fx.thin, fx.carried}) AddMorph(out, *p);
    Golden("bust.prep", out);
  }
  {  // the cloth fade: a piece of the suit's cloth on the chest object, a part of the same model not in cloth_objs
    synth::conv::BustFixture fx;
    core::Part* cloth = fx.Make(&fx.strap_model, 0, fx.chest, {{10.0, 0.015625, 0.0}});
    core::Part* not_cloth = fx.Make(&fx.strap_model, 0, fx.chest, {{0.0, 0.015625, 0.0}});
    not_cloth->obj_id = 1;
    fx.objects[0] = core::Object{{fx.morph, fx.reversed, fx.thin, fx.carried, cloth, not_cloth}, {1}};
    cb::PrepBust(fx.objects, fx.parts, fx.chan2id, 0.25, 0.5, {0}, &fx.strap_model, 0.0, 0.001, 0.0625);
    std::vector<uint8_t> out;
    AddMorph(out, *fx.morph);
    Golden("bust.prep_cloth_fade", out);
  }
  {
    const auto ch = synth::conv::MakeChest();
    const auto mpv = cb::BuildMpv(ch.objects, ch.geos, ch.bind4, ch.chest, 0.0005, 1.0 / 0.562, 1.0 / 0.704);
    Golden("bust.mpv", mpv);
    const auto peak = cb::BustPeak(ch.objects, ch.chest, ch.bind4);
    std::vector<uint8_t> out;
    AddBytes(out, peak ? "peak" : "none");
    if (peak) {
      AddBits(out, (*peak)[0]);
      AddBits(out, (*peak)[1]);
    }
    Golden("bust.peak", out);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// cloth
// ---------------------------------------------------------------------------------------------------------------
struct Lcg {
  uint32_t x;
  double Next() {  // [0, 1)
    x = x * 1664525u + 1013904223u;
    return double(x >> 8) / double(1 << 24);
  }
};

static void CaseClothFitCapsule() {
  const num::FpEnvGuard fp;
  Lcg rng{2024};
  core::PyDict<int64_t, core::Vec3> bones;
  for (int64_t n = 0; n <= 14; ++n) {
    core::Vec3 p{0.01 * double(n) * double(n % 3 - 1), -0.05 * double(n), 0.002 * double(n)};
    if (n == 6) p = {0.09, -0.5, 0.0};
    if (n == 12) p = {-0.09, -0.5, 0.0};
    bones.Set(n, p);
  }
  // Two capsules, one on each leg bone, and the nodes of a cloth hanging inside the first: 24 free nodes and a pin.
  const synth::conv::ClothShape cap6{1, 6, {0.0f, -0.1f, 0.0f}, {0.02f, -0.4f, 0.01f}, 0.08f};
  const synth::conv::ClothShape cap12{1, 12, {0.0f, -0.1f, 0.0f}, {-0.02f, -0.4f, 0.0f}, 0.08f};
  std::vector<std::array<float, 3>> nodes;
  for (int k = 0; k < 24; ++k) {
    const double t = (double(k) + 0.5) / 24.0;
    nodes.push_back({float(0.09 + 0.02 * t + 0.03 * std::cos(double(k))), float(-0.6 - 0.4 * t),
                     float(0.01 * t + 0.03 * std::sin(double(k)))});
  }
  nodes[0] = {1.0f, 1.0f, 1.0f};  // the pinned node, far from every shape
  const std::vector<uint8_t> chunk = synth::conv::ClothChunk(nodes, 0, {cap6, cap12});
  std::map<int64_t, std::vector<core::Vec3>> legs;
  for (int64_t bone : {6, 12}) {
    const double sx = bone == 6 ? 0.02 : -0.02, sz = bone == 6 ? 0.01 : 0.0;
    for (int i = 0; i < 40; ++i) {
      const double t = (double(i) + 0.5) / 40.0;
      legs[bone].push_back({bones.At(bone)[0] + sx * t + 0.07 * (rng.Next() - 0.5),
                            bones.At(bone)[1] - 0.1 - 0.4 * t + 0.01 * (rng.Next() - 0.5),
                            bones.At(bone)[2] + sz * t + 0.07 * (rng.Next() - 0.5)});
    }
  }
  std::vector<core::Vec3> hips;
  for (int i = 0; i < 12; ++i) hips.push_back({0.2 * (rng.Next() - 0.5), -0.3 * rng.Next(), 0.2 * (rng.Next() - 0.5)});
  std::vector<uint8_t> out;
  try {
    out = cl::FitClothChunk(chunk, bones, core::Vec3{0, 0, 0}, legs, hips, true, true, 0.0);
  } catch (const std::exception& e) {
    CKF(false, "FitClothChunk threw: %s", e.what());
    return;
  }
  CK(out.size() == chunk.size() && out != chunk);  // the radius grew and the nodes moved: the case reaches the code
  Golden("cloth_fit_capsule.chunk", out);
}

static void CaseClothWeldTies() {
  const num::FpEnvGuard fp;
  core::Model suit;  // only its address is used: the parts' model
  core::PartArena arena;
  core::Part* others = arena.New();
  core::Part* cloth = arena.New();
  others->model = cloth->model = &suit;
  others->obj_id = 1;
  cloth->obj_id = 2;
  others->has_srcv = cloth->has_srcv = true;
  // A 9 x 9 lattice, step 2^-11 (every distance below is exact): the piece under the cloth sits on it and is moved by seeded
  // multiples of 2^-13. The cloth's source holds the lattice again (so every point of the piece is within 1 mm of it) and the
  // centre of every cell, which has four lattice points at the same distance, then four more at the next.
  const double step = 0x1p-11;
  const int kN = 9;
  for (int iy = 0; iy < kN; ++iy)
    for (int ix = 0; ix < kN; ++ix) {
      const core::Vec3 p{double(ix) * step, double(iy) * step, 0.0};
      others->srcv.push_back(core::SrcV{p, {}});
      core::Vert v;
      v.pos = {p[0] + double((ix * 5 + iy * 3) % 7) * 0x1p-13, p[1] + double((ix * 3 + iy * 11) % 5) * 0x1p-13,
               p[2] + double((ix + iy) % 3) * 0x1p-13};
      others->verts.push_back(v);
    }
  std::vector<core::Vec3> src_pos;
  for (const core::SrcV& s : others->srcv) src_pos.push_back(s.pos);
  for (int iy = 0; iy + 1 < kN; ++iy)
    for (int ix = 0; ix + 1 < kN; ++ix) src_pos.push_back({(double(ix) + 0.5) * step, (double(iy) + 0.5) * step, 0.0});
  for (const core::Vec3& p : src_pos) {
    cloth->srcv.push_back(core::SrcV{p, {}});
    core::Vert v;
    v.pos = p;
    cloth->verts.push_back(v);
  }
  std::vector<std::optional<core::Object>> objects(1);
  objects[0] = core::Object{{others, cloth}, {}};
  try {
    cl::WeldChestCloth(objects, 0, suit, {2});
  } catch (const std::exception& e) {
    CKF(false, "WeldChestCloth threw: %s", e.what());
    return;
  }
  CK(cloth->welded);
  std::vector<uint8_t> out;
  for (const core::Vert& v : cloth->verts)
    for (double c : v.pos) AddBits(out, c);
  Golden("cloth_weld_ties.verts", out);
}

// ---------------------------------------------------------------------------------------------------------------
// generated cloth
// ---------------------------------------------------------------------------------------------------------------
static void CaseGenCloth() {
  const num::FpEnvGuard fp;
  const std::vector<uint8_t> donor = gensy::Donor(), bend = gensy::Bend();
  for (const gensy::Pin& pin : gensy::kPins) {
    gensy::Skirt sk;
    gensy::MakeSkirt(sk);
    gc::Spec spec{pin.design, 1, 1, sk.part, {1}};
    std::vector<uint8_t> chunk;
    try {
      chunk = gc::Build(spec, sk.geos, sk.bind4, donor, bend);
    } catch (const std::exception& e) {
      CKF(false, "%s: Build threw: %s", pin.design, e.what());
      continue;
    }
    CKF(chunk.size() == pin.length && trace::Sha1Hex(chunk) == pin.sha1, "%s: not the recipe's own chunk", pin.design);
    Golden(std::string("gencloth.") + pin.design, chunk);
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  try {
    CaseNumeric();
    CaseGeometry();
    CaseWaist();
    CaseBust();
    CaseClothFitCapsule();
    CaseClothWeldTies();
    CaseGenCloth();
  } catch (const std::exception& e) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: uncaught exception: %s\n", e.what());
  }
  std::printf("golden: %d of %d equal\n", g_golden_equal, g_golden);
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
