// Host test for src/doa4_idle_wait.h (the idle-wait policy and counters). Built and run by run_idle_wait_test.bat.

#include "../../src/doa4_idle_wait.h"

#include <cstdio>

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

using namespace doa4::idle_wait;

// The first kFenceSpinPasses passes poll (a short GPU wait never sleeps); every later pass sleeps.
static void TestSpinBudget() {
  CHECK(kFenceSpinPasses == 2000);
  FenceWait w;
  bool any_early_sleep = false;
  for (uint32_t i = 0; i < kFenceSpinPasses; ++i) any_early_sleep |= w.PassShouldSleep();
  CHECK(!any_early_sleep);
  CHECK(w.PassShouldSleep());  // pass 2001
  CHECK(w.PassShouldSleep());  // and every one after
}

// Sleep counting, and "ended after a timeout" follows the LAST sleep only.
static void TestSleepsAndTimeouts() {
  FenceWait w;
  CHECK(!w.EndedAfterTimeout());  // no sleep at all
  w.Slept(false);
  CHECK(w.sleeps() == 1);
  CHECK(w.timeouts() == 1);
  CHECK(w.EndedAfterTimeout());
  w.Slept(true);  // a later sleep woken by a write: the fence passed after a wake, not a timeout
  CHECK(w.sleeps() == 2);
  CHECK(w.timeouts() == 1);
  CHECK(!w.EndedAfterTimeout());
  w.Slept(false);
  CHECK(w.EndedAfterTimeout());
}

// Totals: a line every kFenceLogEvery waits, with the sums of what the waits counted.
static void TestTotals() {
  FenceTotals t;
  bool due_early = false;
  for (uint32_t i = 0; i + 1 < kFenceLogEvery; ++i) {
    FenceWait w;
    if (i % 2 == 0) {
      w.Slept(true);
      w.Slept(false);
    }
    due_early |= t.Add(w);
  }
  CHECK(!due_early);
  CHECK(t.waits == kFenceLogEvery - 1);
  CHECK(t.waits_slept == kFenceLogEvery / 2);  // i = 0, 2, ..., 598: 300 waits
  CHECK(t.sleeps == kFenceLogEvery);           // 2 each
  CHECK(t.timeouts == kFenceLogEvery / 2);
  CHECK(t.ended_after_timeout == kFenceLogEvery / 2);
  FenceWait last;
  CHECK(t.Add(last));  // the 600th
  t.Reset();
  CHECK(t.waits == 0 && t.sleeps == 0 && t.ended_after_timeout == 0);
}

// The loader: a job found right after a sleep that timed out is a missed wake-up; after a notified sleep, or with no
// sleep, it is not; an idle pass after a timeout is not either (nothing was missed).
static void TestLoaderWake() {
  LoaderWake l;
  CHECK(!l.Pass(true));  // a job with no sleep before it
  l.Slept(true);
  CHECK(!l.Pass(true));  // woken by the enqueue
  l.Slept(false);
  CHECK(!l.Pass(false));  // timed out, and still nothing queued: fine
  l.Slept(false);
  CHECK(l.Pass(true));  // timed out, and a job was waiting: missed
  CHECK(l.missed() == 1);
  CHECK(!l.Pass(true));  // the next job right after (no sleep between) is not counted again
  CHECK(l.missed() == 1);
}

int main() {
  TestSpinBudget();
  TestSleepsAndTimeouts();
  TestTotals();
  TestLoaderWake();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
