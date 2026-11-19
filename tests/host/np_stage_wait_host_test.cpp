// Host test for src/net/np_stage_wait_pure.h (DOA4 Online issue #10: the main thread's wait for the ten stage workers before the
// stage update). Built and run by run_np_stage_wait_test.bat; "np_stage_wait_host_test.exe -v" prints each group and its count.
//
// Where the expected values come from (never from the code under test): ten stage workers, five on each of the entry points
// 82573A40 and 825689E8 (notes/research/issue10_safari_animals_2026-11-13.md, "Second session", the worker map: ranks 1-5 and
// 7-11); one arrival per worker per round (the same note: "Every worker runs exactly once per frame (traced)").
// Likeliest wrong versions, each separated by a named group: a wait that ends at nine arrivals (<= for <) or never ends
// (round_counts); a round start that keeps the last round's arrivals (round_start_clears); a wait armed before all ten are seen or
// before the first round (arming); an eleventh worker that does not stand the wait down, or a tenth that does (eleventh); the
// entry check matching any worker kind (entries); arrivals lost between threads (threads_stress; "threads" only shows ten
// concurrent arrivals end the wait, and cannot catch a lost count: review minor 3); a wait that ignores its cap or reports
// success at the cap (wait_done); a tenth arrival that does not wake a blocked waiter (wakeup).

#include "../../src/net/np_stage_wait_pure.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace doa4::np::stage_wait;

static int g_checks = 0, g_fail = 0, g_group = 0;
static bool g_verbose = false;

#define CHECK(c)                                                         \
  do {                                                                   \
    ++g_checks;                                                          \
    ++g_group;                                                           \
    if (!(c)) {                                                          \
      ++g_fail;                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
    }                                                                    \
  } while (0)

static void Group(const char* name, void (*fn)()) {
  g_group = 0;
  const int before = g_fail;
  fn();
  if (g_verbose || g_fail != before) std::printf("%s: %d checks%s\n", name, g_group, g_fail != before ? " (FAILED)" : "");
}

static void RegisterTen(Rounds& r) {
  for (uint32_t t = 1; t <= 10; ++t) CHECK(r.Register(100 + t));
}

static void Entries() {
  CHECK(IsStageEntry(0x82573A40u));
  CHECK(IsStageEntry(0x825689E8u));
  // The other five worker kinds (np_det_workers.cpp kWorkerEntries) are not stage workers.
  for (uint32_t e : {0x825414B0u, 0x82573958u, 0x82374990u, 0x8234A160u, 0x82370638u}) CHECK(!IsStageEntry(e));
  CHECK(!IsStageEntry(0));
}

static void Arming() {
  Rounds r;
  CHECK(!r.MustWait());  // nothing seen, no round
  for (uint32_t t = 1; t <= 9; ++t) r.Register(t);
  r.RoundStart();
  CHECK(!r.Armed());
  CHECK(!r.MustWait());  // nine seen: never armed (a wait for a tenth that does not exist would always time out)
  r.Register(10);
  CHECK(r.Armed());
  CHECK(r.MustWait());   // ten seen, a round open, no arrival yet

  Rounds q;
  RegisterTen(q);
  CHECK(!q.Armed());     // ten seen but no round opened yet
  CHECK(!q.MustWait());
}

static void RoundCounts() {
  Rounds r;
  RegisterTen(r);
  CHECK(r.RoundStart() == -1);  // the first round has no round before it
  for (int i = 0; i < 9; ++i) {
    CHECK(r.MustWait());
    r.Arrive();
  }
  CHECK(r.Arrivals() == 9);
  CHECK(r.MustWait());   // nine of ten back: still waiting
  r.Arrive();
  CHECK(!r.MustWait());  // all ten back
  CHECK(r.RoundStart() == 10);
}

static void RoundStartClears() {
  Rounds r;
  RegisterTen(r);
  // Each worker's first loop-top arrival (thread start, no job) comes before the first round.
  for (int i = 0; i < 10; ++i) r.Arrive();
  CHECK(r.RoundStart() == -1);
  CHECK(r.Arrivals() == 0);
  CHECK(r.MustWait());   // the warm-up arrivals do not count for the round
  for (int i = 0; i < 7; ++i) r.Arrive();
  CHECK(r.RoundStart() == 7);  // a round that saw seven: the caller counts it as "not ten"
  CHECK(r.MustWait());
  for (int i = 0; i < 12; ++i) r.Arrive();
  CHECK(!r.MustWait());
  CHECK(r.RoundStart() == 12);
}

static void Eleventh() {
  Rounds r;
  RegisterTen(r);
  CHECK(!r.Broken());    // the tenth does not stand the wait down
  r.RoundStart();
  CHECK(r.MustWait());
  CHECK(!r.Register(999));
  CHECK(r.Broken());
  CHECK(!r.Armed());
  CHECK(!r.MustWait());  // stood down for good
  r.RoundStart();
  CHECK(!r.MustWait());
}

static void Threads() {
  for (int rep = 0; rep < 200; ++rep) {
    Rounds r;
    RegisterTen(r);
    r.RoundStart();
    std::vector<std::thread> ts;
    for (int t = 0; t < 10; ++t) ts.emplace_back([&r] { r.Arrive(); });
    for (auto& t : ts) t.join();
    CHECK(!r.MustWait());
    CHECK(r.RoundStart() == 10);
  }
}

static double MsSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// WaitDone: true at once when all ten are back; false at the cap when they are not (the caller stands down); true when the
// tenth arrives during the blocking part, well before the cap.
static void WaitDoneGroup() {
  using namespace std::chrono;
  {
    Rounds r;
    RegisterTen(r);
    r.RoundStart();
    for (int i = 0; i < 10; ++i) r.Arrive();
    const auto t0 = steady_clock::now();
    CHECK(r.WaitDone(microseconds(50), milliseconds(1000)));
    CHECK(MsSince(t0) < 100);
  }
  {
    Rounds r;
    RegisterTen(r);
    r.RoundStart();
    for (int i = 0; i < 9; ++i) r.Arrive();
    const auto t0 = steady_clock::now();
    CHECK(!r.WaitDone(microseconds(50), milliseconds(30)));  // nine of ten: the cap
    const double ms = MsSince(t0);
    CHECK(ms >= 29 && ms < 1000);
  }
  {
    Rounds r;  // not armed (nine seen): never waits
    for (uint32_t t = 1; t <= 9; ++t) r.Register(t);
    r.RoundStart();
    const auto t0 = steady_clock::now();
    CHECK(r.WaitDone(microseconds(0), milliseconds(1000)));
    CHECK(MsSince(t0) < 100);
  }
}

// The tenth arrival wakes a waiter that is already blocked (spin 0): a lost or missing notify shows as a wait that runs to the
// 200 ms cap. 200 rounds with the arrivals 0-3 ms after the waiter starts.
static void Wakeup() {
  using namespace std::chrono;
  int slow = 0;
  for (int rep = 0; rep < 200; ++rep) {
    Rounds r;
    RegisterTen(r);
    r.RoundStart();
    std::thread t([&r, rep] {
      std::this_thread::sleep_for(microseconds((rep * 37) % 3000));
      for (int i = 0; i < 10; ++i) r.Arrive();
    });
    const auto t0 = steady_clock::now();
    const bool ok = r.WaitDone(microseconds(0), milliseconds(200));
    const double ms = MsSince(t0);
    t.join();
    CHECK(ok);
    if (ms > 100) ++slow;
  }
  CHECK(slow == 0);
}

// Ten threads arriving 10,000 times each lose no count (a non-atomic increment loses some almost every run).
static void ThreadsStress() {
  Rounds r;
  RegisterTen(r);
  r.RoundStart();
  std::vector<std::thread> ts;
  for (int t = 0; t < 10; ++t) ts.emplace_back([&r] {
    for (int i = 0; i < 10000; ++i) r.Arrive();
  });
  for (auto& t : ts) t.join();
  CHECK(r.Arrivals() == 100000);
  CHECK(!r.MustWait());
}

int main(int argc, char** argv) {
  g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
  Group("entries", Entries);
  Group("arming", Arming);
  Group("round_counts", RoundCounts);
  Group("round_start_clears", RoundStartClears);
  Group("eleventh", Eleventh);
  Group("threads", Threads);
  Group("wait_done", WaitDoneGroup);
  Group("wakeup", Wakeup);
  Group("threads_stress", ThreadsStress);
  // The summary line in tools/run_host_tests.py's default shape (its last line).
  if (g_fail == 0) {
    std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  } else {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_fail);
  }
  return g_fail == 0 ? 0 : 1;
}
