// Host test for src/doa4_suitconv_num.cpp (the suit converter's floating-point environment and Python / numpy numeric
// primitives), against bit-exact pins from the real interpreter (suitconv_pins.h, written by make_suitconv_pins.py
// under CPython 3.12.1 + numpy 1.26.3). Built and run by run_suitconv_num_test.bat with the converter's compile flags.
//
// Groups: num_pins_match_python (bit equality per pin; where the pins name a plausible wrong implementation, NOT that
// value either), exp_atan2_within_one_ulp, fp_env_guard_restores, run_under_convert_env, contraction_probe_unfused.
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
  }
  Group("fp_env_guard_restores", FpEnvGuardRestores);
  Group("run_under_convert_env", RunUnderConvertEnvCheck);
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
