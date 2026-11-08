// Host test for src/doa4_suitconv_sort.cpp (numpy 1.26.3's default argsort and argmin, ported for their tie order),
// against pins from the real interpreter (suitconv_sort_pins.h, written by make_suitconv_pins.py under CPython 3.12.1 +
// numpy 1.26.3). Built and run by run_suitconv_sort_test.bat with the converter's compile flags.
//
// Groups, in order: prng_matches_generator (the splitmix64 below gives the generator's outputs; checked first, since
// every other pin is rebuilt from it), argsort_matches_numpy_126_windows (every array's index digest, and the full
// index array where the header has one), heapsort_branch_reached (the port's heapsort counter rises on both killer
// arrays), argmin_first_of_ties (np.argmin, NOT the last index of a tied minimum or the first minimum ignoring NaN;
// the empty-input error), set_argsort_for_test_hook.
// Last line: "PASSED: N checks, 0 failures" or "FAILED: N checks, M failures".

#include "../../src/doa4_suitconv_num.h"
#include "../../src/doa4_suitconv_sort.h"
#include "suitconv_sort_pins.h"

#include <bit>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace doa4::suitconv;
using namespace doa4::suitconv::num;
namespace P = suitconv_sort_pins;

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

template <typename F>
static void Group(const char* name, F&& f) {
  const int c0 = g_checks, f0 = g_failures;
  g_printed = 0;
  std::printf("== %s\n", name);
  f();
  if (g_printed > kPrintCap) std::printf("   (%d more failure lines not printed)\n", g_printed - kPrintCap);
  std::printf("   %s: %d checks, %d failures\n", name, g_checks - c0, g_failures - f0);
}

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

static unsigned long long U(uint64_t v) { return static_cast<unsigned long long>(v); }

// The generator's PRNG (suitconv_sort_pins.h header comment), integer only.
struct SplitMix64 {
  uint64_t s;
  uint64_t Next() {
    s += 0x9E3779B97F4A7C15ull;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
};

static std::vector<double> Values(uint64_t seed, size_t n, size_t pool) {
  SplitMix64 g{seed};
  const uint64_t* p = P::kPoolValues + P::kPoolStart[pool];
  const uint64_t size = P::kPoolSize[pool];
  std::vector<double> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = std::bit_cast<double>(p[g.Next() % size]);
  return v;
}

// FNV-1a 64 over the 8 little-endian bytes of each index, in order (the generator's idx_digest).
static uint64_t Digest(const std::vector<int64_t>& idx) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (int64_t x : idx) {
    uint64_t u = static_cast<uint64_t>(x);
    for (int b = 0; b < 8; ++b) {
      h ^= (u >> (8 * b)) & 0xff;
      h *= 0x100000001b3ull;
    }
  }
  return h;
}

// The first position where got differs from want (want.size() if none and the sizes agree).
template <typename T>
static size_t FirstDiff(const std::vector<int64_t>& got, const T* want, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (i >= got.size() || got[i] != static_cast<int64_t>(want[i])) return i;
  return got.size() == n ? n : got.size();
}

static void PrngMatchesGenerator() {
  Check(P::kPrngCheckCount == 15, "kPrngCheck has %zu values, expected 15", P::kPrngCheckCount);
  for (size_t i = 0; i + 4 < P::kPrngCheckCount; i += 5) {
    SplitMix64 g{P::kPrngCheck[i]};
    for (size_t k = 1; k <= 4; ++k) {
      const uint64_t got = g.Next();
      Check(got == P::kPrngCheck[i + k], "seed 0x%016llx output %zu: 0x%016llx, generator 0x%016llx", U(P::kPrngCheck[i]),
            k, U(got), U(P::kPrngCheck[i + k]));
    }
  }
}

static void ArgsortMatchesNumpy() {
  Check(P::kCasesCount == 3000 * 6, "kCases holds %zu values, expected 3000 arrays of 6", P::kCasesCount);
  size_t small_off = 0;
  for (size_t c = 0; c + 5 < P::kCasesCount; c += 6) {
    const uint64_t seed = P::kCases[c];
    const size_t n = static_cast<size_t>(P::kCases[c + 1]);
    const size_t pool = static_cast<size_t>(P::kCases[c + 2]);
    const uint64_t digest = P::kCases[c + 3];
    const size_t at = small_off;
    if (n <= 20) small_off += n;
    Pin("argsort", c / 6, [&] {
      const std::vector<double> v = Values(seed, n, pool);
      const std::vector<int64_t> got = NpArgsort(v);
      Check(got.size() == n && Digest(got) == digest, "argsort array %zu (seed 0x%016llx, n %zu, pool %zu): digest 0x%016llx, numpy 0x%016llx",
            c / 6, U(seed), n, pool, U(Digest(got)), U(digest));
      if (n <= 20) {
        const size_t d = FirstDiff(got, P::kSmallIndices + at, n);
        Check(d == n, "argsort array %zu (seed 0x%016llx, n %zu, pool %zu): first difference at %zu: port %lld, numpy %u",
              c / 6, U(seed), n, pool, d, d < got.size() ? static_cast<long long>(got[d]) : -1LL,
              d < n ? static_cast<unsigned>(P::kSmallIndices[at + d]) : 0u);
      }
    });
  }
  Check(small_off == P::kSmallIndicesCount, "kSmallIndices: walked %zu of %zu", small_off, P::kSmallIndicesCount);
  const uint64_t* kv[2] = {P::kKiller0Values, P::kKiller1Values};
  const uint16_t* ki[2] = {P::kKiller0Indices, P::kKiller1Indices};
  const size_t kn[2] = {P::kKiller0ValuesCount, P::kKiller1ValuesCount};
  for (int k = 0; k < 2; ++k) {
    Pin("argsort killer", static_cast<size_t>(k), [&] {
      std::vector<double> v(kn[k]);
      for (size_t i = 0; i < kn[k]; ++i) v[i] = std::bit_cast<double>(kv[k][i]);
      const std::vector<int64_t> got = NpArgsort(v);
      const size_t d = FirstDiff(got, ki[k], kn[k]);
      Check(d == kn[k], "argsort killer %d (n %zu): first difference at %zu: port %lld, numpy %u", k, kn[k], d,
            d < got.size() ? static_cast<long long>(got[d]) : -1LL, d < kn[k] ? static_cast<unsigned>(ki[k][d]) : 0u);
    });
  }
  Pin("argsort empty", 0, [] {
    Check(NpArgsort(std::span<const double>()).empty(), "argsort of an empty array is not empty");
  });
}

static void HeapsortBranchReached() {
  Check(P::kKiller0ValuesCount == 4096 && P::kKiller1ValuesCount == 4096, "killer lengths %zu, %zu, expected 4096",
        P::kKiller0ValuesCount, P::kKiller1ValuesCount);
  const uint64_t* kv[2] = {P::kKiller0Values, P::kKiller1Values};
  const size_t kn[2] = {P::kKiller0ValuesCount, P::kKiller1ValuesCount};
  for (int k = 0; k < 2; ++k) {
    std::vector<double> v(kn[k]);
    for (size_t i = 0; i < kn[k]; ++i) v[i] = std::bit_cast<double>(kv[k][i]);
    const uint64_t before = ArgsortHeapsortCount();
    Pin("heapsort killer", static_cast<size_t>(k), [&] { (void)NpArgsort(v); });
    const uint64_t calls = ArgsortHeapsortCount() - before;
    std::printf("   killer %d: heapsort fallback taken %llu times\n", k, U(calls));
    Check(calls > 0, "killer %d: the heapsort fallback was never taken", k);
  }
}

static void ArgminFirstOfTies() {
  for (size_t c = 0; c + 5 < P::kCasesCount; c += 6) {
    const uint64_t seed = P::kCases[c];
    const size_t n = static_cast<size_t>(P::kCases[c + 1]);
    const size_t pool = static_cast<size_t>(P::kCases[c + 2]);
    const int64_t want = static_cast<int64_t>(P::kCases[c + 4]);
    const int64_t wrong = static_cast<int64_t>(P::kCases[c + 5]);
    Pin("argmin", c / 6, [&] {
      const int64_t got = NpArgmin(Values(seed, n, pool));
      Check(got == want, "argmin array %zu (seed 0x%016llx, n %zu, pool %zu): port %lld, numpy %lld", c / 6, U(seed), n,
            pool, static_cast<long long>(got), static_cast<long long>(want));
      if (wrong != want)
        Check(got != wrong, "argmin array %zu: port %lld is the wrong (last-of-ties / NaN-skipping) answer", c / 6,
              static_cast<long long>(got));
    });
  }
  std::string got = "returned normally";
  bool ok = false;
  try {
    (void)NpArgmin(std::span<const double>());
  } catch (const ConvertError& e) {
    got = std::string("ConvertError: ") + e.what();
    ok = std::strcmp(e.what(), P::kArgminEmptyMessage) == 0;
  } catch (const std::exception& e) {
    got = std::string("other exception: ") + e.what();
  }
  Check(ok, "argmin of an empty array: %s, expected ConvertError \"%s\"", got.c_str(), P::kArgminEmptyMessage);
}

static int g_stub_calls = 0;
static std::vector<int64_t> StubArgsort(std::span<const double> v) {
  ++g_stub_calls;
  return std::vector<int64_t>(v.size(), 7);
}

static void SetArgsortForTestHook() {
  const double v[] = {2.0, 1.0, 0.0};
  SetArgsortForTest(StubArgsort);
  const std::vector<int64_t> hooked = NpArgsort(v);
  SetArgsortForTest(nullptr);
  const std::vector<int64_t> port = NpArgsort(v);
  Check(g_stub_calls == 1 && hooked == std::vector<int64_t>{7, 7, 7}, "the hook was not called (calls %d)", g_stub_calls);
  Check(port == std::vector<int64_t>{2, 1, 0}, "after SetArgsortForTest(nullptr) NpArgsort is not the port");
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // nothing printed is lost if a check crashes the process
  Group("prng_matches_generator", PrngMatchesGenerator);
  Group("argsort_matches_numpy_126_windows", ArgsortMatchesNumpy);
  Group("heapsort_branch_reached", HeapsortBranchReached);
  Group("argmin_first_of_ties", ArgminFirstOfTies);
  Group("set_argsort_for_test_hook", SetArgsortForTestHook);
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
