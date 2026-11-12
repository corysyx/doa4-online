// Host test for the RANDOM costume's rules (src/doa4_random_costume.h). Built and run by run_random_costume_test.bat.
//
// The contract:
// - offline, or online with no swimsuits or the same import on both sides, the pool is the count the game read;
// - in a mixed session (one side imported swimsuits, or the two imports differ) both players draw from her costumes
//   before the swimsuits (table maximum less her swimsuits), never more than the count; a character without swimsuits
//   keeps the count; the pool is never 0;
// - UniformBelow(n) gives every value in [0, n) the same odds: draws at or past the largest multiple of n below 2^31 are
//   drawn again; a draw function's top bit is ignored.

#include "../../src/doa4_random_costume.h"

#include <array>
#include <cstdio>
#include <vector>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::random_costume;

int main() {
  {  // who draws without the swimsuits: in a mixed session both players, whoever holds the import (the user, 2026-11-11)
    CHECK(!SuitsOff(false, 0) && !SuitsOff(false, 1));  // offline, or a session without a mixed import: nobody
    CHECK(SuitsOff(true, 0) && SuitsOff(true, 1));      // mixed: the host and the joiner
    CHECK(!SuitsOff(true, 2) && !SuitsOff(true, 7));    // a row past the two players
  }
  {  // the pool (Tina: 5 costumes, 4 swimsuits, table maximum 9)
    CHECK(Pool(9, 9, 4, false) == 9);   // swimsuits on: all of them
    CHECK(Pool(9, 9, 4, true) == 5);    // swimsuits off: her 5, not 9 and not 4
    CHECK(Pool(3, 9, 4, true) == 3);    // fewer unlocked than her 5: the count stays
    CHECK(Pool(6, 6, 0, true) == 6);    // no swimsuits for her: the count
    CHECK(Pool(0, 9, 4, false) == 1);   // never 0
    CHECK(Pool(9, 4, 4, true) == 1);    // a table with nothing before the swimsuits: 1, never 0
  }
  {  // rejection: a draw at the limit is drawn again; one just below it is kept
    const uint32_t n = 9;
    const uint32_t limit = 0x80000000u - 0x80000000u % n;
    std::vector<uint32_t> seq = {limit, limit + 1, 5};  // limit + 1 = 0x7FFFFFFF (2^31 % 9 == 2): both rejected
    size_t i = 0;
    CHECK(UniformBelow(n, [&] { return seq[i++]; }) == 5 && i == 3);
    seq = {limit - 1};
    i = 0;
    CHECK(UniformBelow(n, [&] { return seq[i++]; }) == (limit - 1) % n && i == 1);
    seq = {0x80000000u | 7u};  // the top bit is ignored
    i = 0;
    CHECK(UniformBelow(n, [&] { return seq[i++]; }) == 7);
    CHECK(UniformBelow(1, [] { return 12345u; }) == 0);
    CHECK(UniformBelow(0, [] { return 12345u; }) == 0);
  }
  {  // evenness: 900,000 draws over 9 and over 17 (counts 20 cards do not divide); each value within 1.5 % of its share
    uint64_t s = 0x9E3779B97F4A7C15ull;
    auto next = [&] {
      s ^= s >> 12;
      s ^= s << 25;
      s ^= s >> 27;
      return uint32_t((s * 0x2545F4914F6CDD1Dull) >> 33);
    };
    for (uint32_t n : {9u, 17u}) {
      std::array<uint32_t, 17> hits{};
      const uint32_t draws = 900000;
      for (uint32_t k = 0; k < draws; ++k) ++hits[UniformBelow(n, next)];
      const double share = double(draws) / n;
      bool even = true;
      for (uint32_t v = 0; v < n; ++v) even = even && hits[v] > share * 0.985 && hits[v] < share * 1.015;
      CHECK(even);
    }
  }
  if (g_failures == 0) {
    std::printf("PASSED: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
  return 1;
}
