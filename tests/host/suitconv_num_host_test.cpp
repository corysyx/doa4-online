// Host test for src/doa4_suitconv_num.cpp (the suit converter's floating-point environment and Python / numpy numeric
// primitives), against bit-exact pins from the real interpreter (suitconv_pins.h, written by make_suitconv_pins.py
// under CPython 3.12.1 + numpy 1.26.3). Built and run by run_suitconv_num_test.bat with the converter's compile flags.
//
// Groups: num_pins_match_python (bit equality per pin; where the pins name a plausible wrong implementation, NOT that
// value either), exp_atan2_within_one_ulp, fp_env_guard_restores, run_under_convert_env, contraction_probe_unfused,
// fused_dot (SoftFma and NpDotFused: Fraction-computed fma pins, the round-to-odd pins, np.linalg.norm pins).
// The runner also scans the converter objects (suitconv_fp_scan.py) and fails before this test runs on any hit.
// Last line: "PASSED: N checks, 0 failures" or "FAILED: N checks, M failures".

#include "../../src/doa4_suitconv_num.h"
#include "suitconv_pins.h"

#include <immintrin.h>

#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace doa4::suitconv;
using namespace doa4::suitconv::num;
namespace P = suitconv_pins;

static int g_checks = 0;
static int g_failures = 0;
static int g_printed = 0;  // failure lines printed in the current group (capped, every failure still counts)
static constexpr int kPrintCap = 25;

static void Check(bool ok, const char* fmt, ...) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  if (g_printed++ >= kPrintCap) return;
  std::printf("FAIL ");
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::printf("\n");
}

static void BeginGroup(const char* name) {
  g_printed = 0;
  std::printf("== %s\n", name);
}

static void EndGroup(const char* name, int checks0, int failures0) {
  int fails = g_failures - failures0;
  if (g_printed > kPrintCap) std::printf("   (%d more failure lines not printed)\n", g_printed - kPrintCap);
  std::printf("   %s: %d checks, %d failures\n", name, g_checks - checks0, fails);
}

static double D(uint64_t b) { return std::bit_cast<double>(b); }
static uint64_t B(double d) { return std::bit_cast<uint64_t>(d); }
static unsigned long long U(uint64_t v) { return static_cast<unsigned long long>(v); }

// Runs one pin's checks; an exception from the code under test fails that pin instead of ending the run.
template <typename F>
static void Pin(const char* what, size_t index, F&& f) {
  try {
    f();
  } catch (const std::exception& e) {
    Check(false, "%s pin %zu threw: %s", what, index, e.what());
  } catch (...) {
    Check(false, "%s pin %zu threw a non-standard exception", what, index);
  }
}

// A call that must throw ConvertError with exactly `message`; any other outcome fails, naming what happened.
template <typename F>
static void CheckThrows(F&& f, const char* message, const char* what) {
  std::string got = "returned normally";
  bool ok = false;
  try {
    f();
  } catch (const ConvertError& e) {
    got = std::string("ConvertError: ") + e.what();
    ok = std::strcmp(e.what(), message) == 0;
  } catch (...) {
    got = "another exception type";
  }
  Check(ok, "%s: expected ConvertError \"%s\", %s", what, message, got.c_str());
}

// ---------------------------------------------------------------------------------------------------------------
static void PySumPins() {
  for (size_t i = 0; i < P::kSumCount; i += 4) {
    const uint64_t off = P::kSum[i], n = P::kSum[i + 1], want = P::kSum[i + 2], naive = P::kSum[i + 3];
    PySum s;
    for (uint64_t k = 0; k < n; ++k) s.Add(D(P::kSumIn[off + k]));
    const uint64_t got = B(s.Result());
    Check(got == want, "PySum pin %zu (n=%llu): got 0x%016llx, want 0x%016llx", i / 4, U(n), U(got), U(want));
    if (naive != want)
      Check(got != naive, "PySum pin %zu: got the naive loop's 0x%016llx", i / 4, U(naive));
  }
}

static void FloorDivPins() {
  for (size_t i = 0; i < P::kFloorDivCount; i += 4) {
    const double x = D(P::kFloorDiv[i]), y = D(P::kFloorDiv[i + 1]);
    const uint64_t want = P::kFloorDiv[i + 2], alt = P::kFloorDiv[i + 3];
    Pin("PyFloorDiv", i / 4, [&] {
      const uint64_t got = B(PyFloorDiv(x, y));
      Check(got == want, "PyFloorDiv(0x%016llx, 0x%016llx): got 0x%016llx, want 0x%016llx", U(B(x)), U(B(y)), U(got),
            U(want));
      if (alt != want)
        Check(got != alt, "PyFloorDiv(0x%016llx, 0x%016llx): got floor(x / y) 0x%016llx", U(B(x)), U(B(y)), U(alt));
    });
  }
  CheckThrows([] { PyFloorDiv(1.0, 0.0); }, P::kFloorDivZeroMessage, "PyFloorDiv(1, 0)");
  CheckThrows([] { PyFloorDiv(-1.0, -0.0); }, P::kFloorDivZeroMessage, "PyFloorDiv(-1, -0)");
}

static void RoundPins() {
  for (size_t i = 0; i < P::kRoundCount; i += 4) {
    const double x = D(P::kRound[i]);
    const int n = static_cast<int>(P::kRound[i + 1]);
    const uint64_t want = P::kRound[i + 2], alt = P::kRound[i + 3];
    Pin("PyRoundDigits", i / 4, [&] {
      const uint64_t got = B(PyRoundDigits(x, n));
      Check(got == want, "PyRoundDigits(0x%016llx, %d): got 0x%016llx, want 0x%016llx", U(B(x)), n, U(got), U(want));
      if (alt != want)
        Check(got != alt, "PyRoundDigits(0x%016llx, %d): got the floor formula's 0x%016llx", U(B(x)), n, U(alt));
    });
  }
  for (int bad : {-1, 18}) {
    std::string got = "returned normally";
    bool ok = false;
    try {
      PyRoundDigits(1.5, bad);
    } catch (const ConvertError& e) {
      got = e.what();
      ok = got.find("ndigits") != std::string::npos;
    } catch (...) {
      got = "another exception type";
    }
    Check(ok, "PyRoundDigits(1.5, %d): expected a ConvertError naming ndigits, %s", bad, got.c_str());
  }
  for (size_t i = 0; i < P::kRoundHalfEvenCount; i += 3) {
    const double x = D(P::kRoundHalfEven[i]);
    const uint64_t want = P::kRoundHalfEven[i + 1], alt = P::kRoundHalfEven[i + 2];
    Pin("RoundHalfEven", i / 3, [&] {
      const uint64_t got = B(RoundHalfEven(x));
      Check(got == want, "RoundHalfEven(0x%016llx): got 0x%016llx, want 0x%016llx", U(B(x)), U(got), U(want));
      if (alt != want) Check(got != alt, "RoundHalfEven(0x%016llx): got C round()'s 0x%016llx", U(B(x)), U(alt));
    });
  }
  CheckThrows([] { RoundHalfEven(INFINITY); }, P::kRoundInfMessage, "RoundHalfEven(inf)");
  CheckThrows([] { RoundHalfEven(-INFINITY); }, P::kRoundInfMessage, "RoundHalfEven(-inf)");
  CheckThrows([] { RoundHalfEven(NAN); }, P::kRoundNanMessage, "RoundHalfEven(nan)");
}

static void PackPins() {
  for (size_t i = 0; i < P::kPackF32Count; i += 2) {
    const double x = D(P::kPackF32[i]);
    const uint32_t want = static_cast<uint32_t>(P::kPackF32[i + 1]);
    Pin("PackF32", i / 2, [&] {
      const uint32_t got = std::bit_cast<uint32_t>(PackF32(x));
      Check(got == want, "PackF32(0x%016llx): got 0x%08x, want 0x%08x", U(B(x)), got, want);
    });
  }
  for (size_t i = 0; i < P::kPackF32OverflowCount; ++i) {
    const double x = D(P::kPackF32Overflow[i]);
    char what[64];
    std::snprintf(what, sizeof what, "PackF32(0x%016llx)", U(B(x)));
    CheckThrows([x] { PackF32(x); }, P::kPackF32Message, what);
  }
  for (size_t i = 0; i < P::kPackF16Count; i += 3) {
    const double x = D(P::kPackF16[i]);
    const uint64_t want = P::kPackF16[i + 1], alt = P::kPackF16[i + 2];
    Pin("PackF16", i / 3, [&] {
      const uint16_t got = PackF16(x);
      Check(got == want, "PackF16(0x%016llx): got 0x%04x, want 0x%04llx", U(B(x)), got, U(want));
      if (alt != want)
        Check(got != alt, "PackF16(0x%016llx): got the through-float value 0x%04llx", U(B(x)), U(alt));
    });
  }
  for (size_t i = 0; i < P::kPackF16OverflowCount; ++i) {
    const double x = D(P::kPackF16Overflow[i]);
    char what[64];
    std::snprintf(what, sizeof what, "PackF16(0x%016llx)", U(B(x)));
    CheckThrows([x] { PackF16(x); }, P::kPackF16Message, what);
  }
  for (size_t i = 0; i < P::kHalfToDoubleCount; i += 2) {
    const uint16_t h = static_cast<uint16_t>(P::kHalfToDouble[i]);
    const uint64_t want = P::kHalfToDouble[i + 1], got = B(HalfToDouble(h));
    Check(got == want, "HalfToDouble(0x%04x): got 0x%016llx, want 0x%016llx", h, U(got), U(want));
  }
  uint64_t fnv = 0xcbf29ce484222325ull;
  for (uint32_t h = 0; h < 65536; ++h) {
    const uint64_t bits = B(HalfToDouble(static_cast<uint16_t>(h)));
    for (int k = 0; k < 8; ++k) {
      fnv ^= (bits >> (8 * k)) & 0xFF;
      fnv *= 0x100000001b3ull;
    }
  }
  Check(fnv == P::kHalfToDoubleDigest, "HalfToDouble over all 65536 halves: digest 0x%016llx, want 0x%016llx", U(fnv),
        U(P::kHalfToDoubleDigest));
}

static const char* kKindName[] = {"NpSumLastAxis", "NpSumAxis0", "NpSum1D", "NpMean1D", "NpMeanAxis0",
                                  "NpMedianAxis0"};

static void ReductionPins() {
  std::vector<double> pool(P::kPoolCount);
  for (size_t i = 0; i < P::kPoolCount; ++i) pool[i] = D(P::kPool[i]);
  for (size_t c = 0; c < P::kRedCount; c += 5) {
    const uint32_t kind = P::kRed[c], off = P::kRed[c + 1], rows = P::kRed[c + 2], cols = P::kRed[c + 3],
                   out0 = P::kRed[c + 4];
    const std::span<const double> in(pool.data() + off, size_t(rows) * cols);
    std::vector<double> got;
    if (kind > 5) {
      Check(false, "reduction case %zu: unknown kind %u", c / 5, kind);
      continue;
    }
    bool threw = false;
    Pin(kKindName[kind], c / 5, [&] {
      try {
        switch (kind) {
          case 0:
            for (uint32_t r = 0; r < rows; ++r) got.push_back(NpSumLastAxis(in.subspan(size_t(r) * cols, cols)));
            break;
          case 1:
            got.resize(cols);
            NpSumAxis0(in, cols, got);
            break;
          case 2:
            got.push_back(NpSum1D(in));
            break;
          case 3:
            got.push_back(NpMean1D(in));
            break;
          case 4:
            got.resize(cols);
            NpMeanAxis0(in, cols, got);
            break;
          case 5:
            got.resize(cols);
            NpMedianAxis0(in, cols, got);
            break;
        }
      } catch (...) {
        threw = true;
        throw;
      }
    });
    if (threw) continue;
    const size_t want_n = kind == 0 ? rows : (kind == 2 || kind == 3) ? 1 : cols;
    Check(got.size() == want_n, "%s case %zu: %zu outputs, want %zu", kKindName[kind], c / 5, got.size(), want_n);
    for (size_t k = 0; k < got.size(); ++k) {
      const uint64_t* o = &P::kRedOut[(size_t(out0) + k) * 4];
      const uint64_t g = B(got[k]);
      Check(g == o[0], "%s case %zu (%ux%u) output %zu: got 0x%016llx, want 0x%016llx", kKindName[kind], c / 5, rows,
            cols, k, U(g), U(o[0]));
      static const char* kCand[] = {"sequential", "pairwise", "pairwise per 8192 chunk"};
      for (int j = 0; j < 3; ++j)
        if (o[1 + j] != o[0])
          Check(g != o[1 + j], "%s case %zu output %zu: got the %s association's 0x%016llx", kKindName[kind], c / 5, k,
                kCand[j], U(o[1 + j]));
    }
  }
  for (size_t i = 0; i < P::kPercentileCount; i += 6) {
    const uint64_t off = P::kPercentile[i], n = P::kPercentile[i + 1];
    const double q = D(P::kPercentile[i + 2]);
    const uint64_t want = P::kPercentile[i + 3], other = P::kPercentile[i + 4], hi = P::kPercentile[i + 5];
    std::vector<double> v(pool.begin() + off, pool.begin() + off + n);
    Pin("NpPercentileLinear", i / 6, [&] {
      const uint64_t got = B(NpPercentileLinear(v, q));
      Check(got == want, "NpPercentileLinear pin %zu (n=%llu, q=%g, t %s 0.5): got 0x%016llx, want 0x%016llx", i / 6,
            U(n), q, hi ? ">=" : "<", U(got), U(want));
      if (other != want)
        Check(got != other, "NpPercentileLinear pin %zu (t %s 0.5): got the other _lerp branch 0x%016llx", i / 6,
              hi ? ">=" : "<", U(other));
    });
  }
  for (size_t i = 0; i < P::kLinspaceCount; i += 4) {
    const double a = D(P::kLinspace[i]), b = D(P::kLinspace[i + 1]);
    const int num = static_cast<int>(P::kLinspace[i + 2]);
    const uint64_t out0 = P::kLinspace[i + 3];
    Pin("NpLinspace", i / 4, [&] {
      const std::vector<double> y = NpLinspace(a, b, num);
      Check(y.size() == size_t(num), "NpLinspace pin %zu: %zu values, want %d", i / 4, y.size(), num);
      for (size_t k = 0; k < y.size() && k < size_t(num); ++k)
        Check(B(y[k]) == P::kLinspaceOut[out0 + k], "NpLinspace pin %zu value %zu: got 0x%016llx, want 0x%016llx",
              i / 4, k, U(B(y[k])), U(P::kLinspaceOut[out0 + k]));
    });
  }
  for (size_t i = 0; i < P::kRadiansCount; i += 2) {
    const uint64_t got = B(NpRadians(D(P::kRadians[i])));
    Check(got == P::kRadians[i + 1], "NpRadians(0x%016llx): got 0x%016llx, want 0x%016llx", U(P::kRadians[i]), U(got),
          U(P::kRadians[i + 1]));
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Distance in units in the last place between two doubles of the same kind; NaN only matches NaN, and two zeros
// of different sign are 1 apart (a sign error on a zero is an error).
static uint64_t UlpDistance(double a, double b) {
  if (std::isnan(a) || std::isnan(b)) return (std::isnan(a) && std::isnan(b)) ? 0 : UINT64_MAX;
  if (a == 0.0 && b == 0.0) return std::signbit(a) == std::signbit(b) ? 0 : 1;
  auto ordered = [](double d) {
    const int64_t i = std::bit_cast<int64_t>(d);
    return i < 0 ? INT64_MIN - i : i;
  };
  const int64_t ia = ordered(a), ib = ordered(b);
  return ia > ib ? uint64_t(ia) - uint64_t(ib) : uint64_t(ib) - uint64_t(ia);
}

static void ExpAtan2() {
  uint64_t worst = 0, worst_in = 0, ones = 0;
  for (size_t i = 0; i < P::kExpCount; i += 2) {
    const double x = D(P::kExp[i]), y = Exp(x), want = D(P::kExp[i + 1]);
    const uint64_t d = UlpDistance(y, want);
    if (d > worst) worst = d, worst_in = B(x);
    ones += d == 1;
    Check(d <= 1, "Exp(0x%016llx) = 0x%016llx, numpy 0x%016llx: %llu ulp", U(B(x)), U(B(y)), U(B(want)), U(d));
  }
  std::printf("   Exp: %zu pins, %llu at 1 ulp, max %llu ulp (at x = 0x%016llx)\n", P::kExpCount / 2, U(ones), U(worst),
              U(worst_in));
  uint64_t aworst = 0, aworst_y = 0, aworst_x = 0, aones = 0;
  for (size_t i = 0; i < P::kAtan2Count; i += 3) {
    const double y = D(P::kAtan2[i]), x = D(P::kAtan2[i + 1]), z = Atan2(y, x), want = D(P::kAtan2[i + 2]);
    const uint64_t d = UlpDistance(z, want);
    if (d > aworst) aworst = d, aworst_y = B(y), aworst_x = B(x);
    aones += d == 1;
    Check(d <= 1, "Atan2(0x%016llx, 0x%016llx) = 0x%016llx, numpy 0x%016llx: %llu ulp", U(B(y)), U(B(x)), U(B(z)),
          U(B(want)), U(d));
  }
  std::printf("   Atan2: %zu pins, %llu at 1 ulp, max %llu ulp (at y = 0x%016llx, x = 0x%016llx)\n",
              P::kAtan2Count / 3, U(aones), U(aworst), U(aworst_y), U(aworst_x));
  // Exact values (bits): pi = 0x400921fb54442d18, pi/2 = 0x3ff921fb54442d18.
  struct Exact {
    double y, x;
    uint64_t want;
  };
  const Exact exact[] = {{0.0, -1.0, 0x400921fb54442d18ull}, {-0.0, -1.0, 0xc00921fb54442d18ull},
                         {0.0, 1.0, 0x0000000000000000ull},  {-0.0, 1.0, 0x8000000000000000ull},
                         {1.0, 0.0, 0x3ff921fb54442d18ull},  {-1.0, 0.0, 0xbff921fb54442d18ull}};
  for (const Exact& e : exact) {
    const uint64_t got = B(Atan2(e.y, e.x));
    Check(got == e.want, "Atan2(%g, %g): got 0x%016llx, want exactly 0x%016llx", e.y, e.x, U(got), U(e.want));
  }
  Check(B(Exp(0.0)) == B(1.0), "Exp(0): got 0x%016llx, want exactly 1.0", U(B(Exp(0.0))));
}

// ---------------------------------------------------------------------------------------------------------------
static void FpEnvGuardRestores() {
  const uint32_t original = _mm_getcsr();
  const uint32_t odd = kConvertMxcsr | 0x8000u /* FTZ */ | 0x0040u /* DAZ */ | 0x4000u /* round up */;
  static const char* kName[] = {"FTZ (a denormal result)", "DAZ (denormal inputs)", "round-up"};
  _mm_setcsr(odd);
  // Control: under the odd mode each pin's result differs, so the checks inside the guard can see the mode.
  double control[3];
  for (int k = 0; k < 3; ++k) {
    const double ab[2] = {D(P::kFpEnv[3 * k]), D(P::kFpEnv[3 * k + 1])};
    control[k] = NpSum1D(ab);
  }
  const uint32_t before = Mxcsr();
  bool inside_ok = false;
  uint32_t inside = 0;
  double result[3];
  {
    FpEnvGuard guard;
    inside = Mxcsr();
    inside_ok = inside == kConvertMxcsr;
    for (int k = 0; k < 3; ++k) {
      const double ab[2] = {D(P::kFpEnv[3 * k]), D(P::kFpEnv[3 * k + 1])};
      result[k] = NpSum1D(ab);
    }
  }
  const uint32_t after = Mxcsr();
  _mm_setcsr(original);
  Check((before & 0xE040u) == 0xC040u, "the odd mode was not in force before the guard: MXCSR 0x%08x", before);
  for (int k = 0; k < 3; ++k)
    Check(B(control[k]) != P::kFpEnv[3 * k + 2], "control: %s left the pin 0x%016llx unchanged", kName[k],
          U(P::kFpEnv[3 * k + 2]));
  Check(inside_ok, "inside the guard MXCSR is 0x%08x, want 0x%08x", inside, kConvertMxcsr);
  for (int k = 0; k < 3; ++k)
    Check(B(result[k]) == P::kFpEnv[3 * k + 2], "inside the guard, %s pin: got 0x%016llx, want 0x%016llx", kName[k],
          U(B(result[k])), U(P::kFpEnv[3 * k + 2]));
  Check(after == before, "after the guard MXCSR is 0x%08x, want the old 0x%08x", after, before);
}

// RunUnderConvertEnv: the caller runs FTZ + DAZ + round-up; the callback must see kConvertMxcsr and compute the three
// FP-environment pins exactly, and the caller's control word must be back afterwards, also when the callback throws.
struct EnvProbe {
  uint32_t inside = 0;
  double result[3] = {0, 0, 0};
  bool throw_after = false;
};

static void EnvProbeBody(void* ctx) {
  EnvProbe& p = *static_cast<EnvProbe*>(ctx);
  p.inside = Mxcsr();
  for (int k = 0; k < 3; ++k) {
    const double ab[2] = {D(P::kFpEnv[3 * k]), D(P::kFpEnv[3 * k + 1])};
    p.result[k] = NpSum1D(ab);
  }
  if (p.throw_after) throw ConvertError("thrown inside RunUnderConvertEnv");
}

static void RunUnderConvertEnvCheck() {
  const uint32_t original = _mm_getcsr();
  const uint32_t odd = kConvertMxcsr | 0x8000u /* FTZ */ | 0x0040u /* DAZ */ | 0x4000u /* round up */;
  static const char* kName[] = {"FTZ (a denormal result)", "DAZ (denormal inputs)", "round-up"};
  for (int throwing = 0; throwing < 2; ++throwing) {
    const char* how = throwing ? "throwing callback" : "returning callback";
    EnvProbe probe;
    probe.throw_after = throwing != 0;
    _mm_setcsr(odd);
    const uint32_t before = Mxcsr();
    std::string caught = "nothing";
    try {
      RunUnderConvertEnv(EnvProbeBody, &probe);
    } catch (const ConvertError& e) {
      caught = e.what();
    } catch (...) {
      caught = "another exception type";
    }
    const uint32_t after = Mxcsr();
    _mm_setcsr(original);
    Check((before & 0xE040u) == 0xC040u, "%s: the odd mode was not in force before the call: MXCSR 0x%08x", how, before);
    Check(probe.inside == kConvertMxcsr, "%s: inside RunUnderConvertEnv MXCSR is 0x%08x, want 0x%08x", how,
          probe.inside, kConvertMxcsr);
    for (int k = 0; k < 3; ++k)
      Check(B(probe.result[k]) == P::kFpEnv[3 * k + 2], "%s: %s pin inside: got 0x%016llx, want 0x%016llx", how,
            kName[k], U(B(probe.result[k])), U(P::kFpEnv[3 * k + 2]));
    Check(after == before, "%s: after RunUnderConvertEnv MXCSR is 0x%08x, want the caller's 0x%08x", how, after,
          before);
    const char* want_caught = throwing ? "thrown inside RunUnderConvertEnv" : "nothing";
    Check(caught == want_caught, "%s: the caller caught \"%s\", want \"%s\"", how, caught.c_str(), want_caught);
  }
}

static void ContractionProbeUnfused() {
  const double a = 1.0 + 0x1p-30, b = 1.0 - 0x1p-30, c = -1.0;
  const uint64_t got = B(ContractionProbe(a, b, c));
  Check(got == P::kContractionUnfused, "ContractionProbe: got 0x%016llx, want +0.0 (0x%016llx)", U(got),
        U(P::kContractionUnfused));
  Check(got != P::kContractionFused, "ContractionProbe: got the fused value -2^-60 (0x%016llx)", U(got));
}

// ---------------------------------------------------------------------------------------------------------------
// fused_dot: SoftFma and NpDotFused
// ---------------------------------------------------------------------------------------------------------------
// np.linalg.norm of a small vector is OpenBLAS's ddot, which fuses each multiply-add on the PC that made the oracle.
// SoftFma is the exact a*b + c rounded once (pins: Python Fraction arithmetic, correctly rounded); the norm pins are
// np.linalg.norm of [0, y, z] on that PC (numpy 1.26.3), equal to sqrt(NpDotFused(v, v, 3)). Seeded values, no game
// data. The first table's pins include cancellation cases and cases where the product's error term alone decides the
// last bit, so a plain a*b + c differs on some of them. A sum that rounds to nearest where the round-to-odd middle step
// belongs is separated only by the two round-to-odd pins after the tables (probe-checked: with a plain tl + ul the
// runner fails on exactly those).

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

// Correctly rounded a*b + c where the middle sum tl + ul needs round-to-odd: with round-to-nearest there the result is one
// ulp off (the plain a*b + c is too). Each expected value is float(Fraction(a) * Fraction(b) + Fraction(c)).
struct RtoPin {
  double a, b, c, want;
};
static const RtoPin kRtoPins[] = {
    {0x1.0fca3930b4aa8p+0, 0x1.153d732dbe6b1p+0, -0x1.9b14fb331c501p-57, 0x1.26570d59bc7e3p+0},
    {0x1.1d30aad288f3bp+0, 0x1.d4fb37fbfe551p+0, 0x1.c049f30179d55p-54, 0x1.053a6aaf46c4dp+1},
};

static void FusedDot() {
  int n = 0, plain_differs = 0;
  for (const auto& row : kFmaPins) {
    const double a = D(row[0]), b = D(row[1]), c = D(row[2]);
    const double got = SoftFma(a, b, c);
    Check(B(got) == row[3], "SoftFma pin %d: got %016llx want %016llx", n, U(B(got)), U(row[3]));
    volatile double prod = a * b;  // two roundings
    if (B(prod + c) != row[3]) ++plain_differs;
    ++n;
  }
  Check(plain_differs >= 8, "%d of %d pins differ from a*b + c", plain_differs, n);
  int r = 0;
  for (const auto& p : kRtoPins) {
    const double got = SoftFma(p.a, p.b, p.c);
    Check(B(got) == B(p.want), "SoftFma round-to-odd pin %d: got %016llx want %016llx", r, U(B(got)), U(B(p.want)));
    volatile double prod = p.a * p.b;
    Check(B(prod + p.c) != B(p.want), "round-to-odd pin %d is also off for a*b + c", r);
    ++r;
  }
  int m = 0;
  for (const auto& row : kNormPins) {
    const double v[3] = {0.0, D(row[0]), D(row[1])};
    const double got = std::sqrt(NpDotFused(v, v, 3));
    Check(B(got) == row[2], "norm pin %d: got %016llx want %016llx", m, U(B(got)), U(row[2]));
    ++m;
  }
  // special values keep the plain result
  Check(std::isnan(SoftFma(std::numeric_limits<double>::quiet_NaN(), 1.0, 1.0)), "NaN operand");
  Check(SoftFma(0.0, 5.0, 3.0) == 3.0, "zero operand");
  const double x1[2] = {2.0, 3.0}, y1[2] = {5.0, 7.0};
  Check(NpDotFused(x1, y1, 2) == 31.0, "NpDotFused 2*5 + 3*7");
  Check(NpDotFused(x1, y1, 0) == 0.0, "NpDotFused of nothing is 0.0");
  std::printf("fused_dot: %d fma pins (%d differ from a*b + c), %d round-to-odd pins, %d norm pins\n", n, plain_differs, r,
              m);
}

template <typename F>
static void Group(const char* name, F&& f) {
  const int c0 = g_checks, f0 = g_failures;
  BeginGroup(name);
  f();
  EndGroup(name, c0, f0);
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // nothing printed is lost if a check crashes the process
  {
    FpEnvGuard guard;  // the pins were computed in the default environment, which is the one conversion runs in
    Group("num_pins_match_python", [] {
      PySumPins();
      FloorDivPins();
      RoundPins();
      PackPins();
      ReductionPins();
    });
    Group("exp_atan2_within_one_ulp", ExpAtan2);
    Group("contraction_probe_unfused", ContractionProbeUnfused);
    Group("fused_dot", FusedDot);
  }
  Group("fp_env_guard_restores", FpEnvGuardRestores);
  Group("run_under_convert_env", RunUnderConvertEnvCheck);
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
