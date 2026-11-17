// Host test for src/net/np_lanes_pure.cpp (DOA4 Online lockstep port, task B1: worker mode 4's fixed table, the placement check, the
// rules of a frame, the stall clock, the end request, the mode decision, the test switch, the wait histogram, the up-front critical
// section decision and its verdict, the log lines). Built and run by run_np_lanes_test.bat; "np_lanes_host_test.exe -v" prints each
// group and its check count. The groups run in the order of the plan's task B1 test table.
//
// Where the expected values come from (never from the code under test):
//   - the table: typed here from design 2.1 to 2.4 (notes/specs/2026-11-16-doa4-lockstep-port-design.md lines 43 to 51, 78 to 84, 95
//     to 97, 108), the same rows as CHAINS, LANES and ENTRY in np_runs/lp/controller/lanes_model.py;
//   - the Turns values: lanes_model_out.txt (np_runs/lp/controller, exit 0, 2026-11-16), lines 6 to 18, cited as "model:N";
//   - the placement positions: wd:tests/host/np_rb0_sched_host_test.cpp:84-85 (TestFact1, the same table);
//   - the stall clock and histogram values: the plan's rows, recomputed from the contracts by np_runs/lp/b/b1/recompute.py (output
//     recompute_out.txt, exit 0): no value differed;
//   - cs_decision's 14 carried checks: wd:tests/host/np_rb0_sched_host_test.cpp:375-397, values unchanged; their sources are cited there
//     (sdk-rb0 xboxkrnl_rtl.cpp:397-402, xobject.h:35 and :225-228, np_runs/rb0/t6/workers_report.md line 218).
// Likeliest wrong versions, each separated by a named group (the plan's mutant table): MayRun open for all (turns_frame_start); the join
// opening the turns, or MainReachedJoin always true (turns_no_mid_frame); a second wait over all 25 or always ready (turns_second_wait,
// turns_frame_start); a first wait that treats a mid-waiting head as running or a lane-held head as not ready (turns_first_wait); End
// that never reports "ended unseen" (turns_ended_unseen); Running as the number at their job (turns_counts); Resolve reading only
// workers set before (placement_any_call_order); Mismatch without the entry points or with chains compared as sets (placement_refusals);
// a table row exchanged (table_is_consistent); a stall clock without its cap, with the wrong threshold, counting 50 a call, or with a
// Reset that does nothing (stall_clock); the manager's rule missing from SectionsVerdict (cs_decision); the last writer winning
// (end_request); DecideMode's order or a fixed mode for a refused switch (decide_mode); a histogram bucket rounded up or a mean
// (wait_histogram); a switch parser that accepts a field twice, worker 25 or reads the stall fields the other way round
// (lanes_test_switch).

#include "../../src/net/np_lanes_pure.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace doa4::np::lanes;

static int g_checks = 0;
static int g_failures = 0;
static bool g_verbose = false;
static const char* g_group = "";
static int g_group_start = 0;
#define CHECK(...)                                                                        \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(__VA_ARGS__)) {                                                                 \
      std::printf("FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, g_group, #__VA_ARGS__);    \
      std::fflush(stdout);                                                                \
      ++g_failures;                                                                       \
    }                                                                                     \
  } while (0)

static void EndGroup() {
  if (g_verbose && *g_group) std::printf("  %d checks\n", g_checks - g_group_start);
}

static void Group(const char* name) {
  EndGroup();
  g_group = name;
  g_group_start = g_checks;
  if (g_verbose) std::printf("%s\n", name);
}

static bool Has(const char* s, const char* part) { return s != nullptr && std::strstr(s, part) != nullptr; }

// ---- the table, typed from the design (never read back from the code under test) ----
static const int kTChainLen[5] = {7, 5, 5, 3, 5};  // design 2.2, lines 80 to 84
static const int kTChain[5][7] = {
    {0, 1, 6, 7, 12, 16, 20}, {2, 8, 13, 17, 21, -1, -1}, {3, 9, 14, 18, 22, -1, -1}, {4, 10, 23, -1, -1, -1, -1}, {5, 11, 15, 19, 24, -1, -1}};
static const int kTLaneLen[2] = {12, 8};  // design 2.3, lines 95 and 96
static const int kTLane[2][12] = {{0, 1, 6, 7, 2, 3, 4, 5, 8, 9, 10, 11}, {12, 16, 13, 14, 15, 17, 18, 19, -1, -1, -1, -1}};
static const uint32_t kTEntry[25] = {  // design 2.1, lines 45 to 51
    0x825414B0u,                                                             // 0 sound
    0x82573A40u, 0x82573A40u, 0x82573A40u, 0x82573A40u, 0x82573A40u,         // 1 to 5 stage (scene drawer)
    0x82573958u,                                                             // 6 stage
    0x825689E8u, 0x825689E8u, 0x825689E8u, 0x825689E8u, 0x825689E8u,         // 7 to 11 stage (scene drawer)
    0x82374990u, 0x82374990u, 0x82374990u, 0x82374990u,                      // 12 to 15 morphs
    0x8234A160u, 0x8234A160u, 0x8234A160u, 0x8234A160u,                      // 16 to 19 model work
    0x82370638u, 0x82370638u, 0x82370638u, 0x82370638u, 0x82370638u};        // 20 to 24 cloth
static const uint32_t kTSections[4] = {0x83302240u, 0x8321B178u, 0x83303A68u, 0x835780E0u};  // design 2.4, line 108

// ---- the handles a worker's first SignalObjectAndWait gives (wd:np_rb0_sched_host_test.cpp:63-64) ----
static const uint32_t kStart[5] = {0x100, 0x104, 0x108, 0x10C, 0x110};
static uint32_t DoneOf(int i) { return 0x1000 + 4 * uint32_t(i); }

struct Handles {
  uint32_t entry[25];
  uint32_t done[25];
  uint32_t wait[25];
};

// From a chain table (the code's kChain, or the test's own): a worker waits for its chain's start event at position 0 and for its
// predecessor's done event after it. A worker on no chain waits for 0.
static Handles HandlesFrom(const int (*chains)[7], const int* len) {
  Handles h{};
  for (int i = 0; i < 25; ++i) {
    h.entry[i] = kTEntry[i];
    h.done[i] = DoneOf(i);
    h.wait[i] = 0;
  }
  for (int c = 0; c < 5; ++c) {
    for (int p = 0; p < len[c] && p < 7; ++p) {
      const int w = chains[c][p];
      if (w < 0 || w >= 25) continue;
      h.wait[w] = p == 0 ? kStart[c] : (chains[c][p - 1] >= 0 ? DoneOf(chains[c][p - 1]) : 0);
    }
  }
  return h;
}

static Placement Place(const Handles& h, const std::vector<int>& order) {
  Placement p;
  for (int i : order) p.Set(i, h.entry[i], h.done[i], h.wait[i]);
  p.Resolve(kStart);
  return p;
}

static std::vector<int> IndexOrder(int skip = -1) {
  std::vector<int> v;
  for (int i = 0; i < 25; ++i) {
    if (i != skip) v.push_back(i);
  }
  return v;
}

// ---- table_is_consistent ----
static void TestTable() {
  Group("table_is_consistent");
  CHECK(kWorkers == 25 && kChains == 5 && kLanes == 2);
  CHECK(std::string(kTableId) == "s6v1");
  for (int c = 0; c < 5; ++c) {
    bool row = kChainLen[c] == kTChainLen[c];
    for (int p = 0; p < kTChainLen[c]; ++p) row = row && kChain[c][p] == kTChain[c][p];
    CHECK(row);  // one check a chain, row by row against design 2.2
  }
  for (int l = 0; l < 2; ++l) {
    bool row = kLaneLen[l] == kTLaneLen[l];
    for (int k = 0; k < kTLaneLen[l]; ++k) row = row && kLane[l][k] == kTLane[l][k];
    CHECK(row);  // one check a lane, against design 2.3
  }
  for (int s = 0; s < 4; ++s) CHECK(kSections[s] == kTSections[s]);
  for (int w = 0; w < 25; ++w) CHECK(kEntryOf[w] == kTEntry[w]);
  // The chains hold 0 to 24 once each; the lanes hold exactly 0 to 19 (each once).
  int on_chain[25] = {}, in_lane[25] = {};
  bool in_range = true;
  for (int c = 0; c < 5; ++c) {
    for (int p = 0; p < kChainLen[c] && p < kMaxChainLen; ++p) {
      const int w = kChain[c][p];
      if (w >= 0 && w < 25) ++on_chain[w]; else in_range = false;
    }
  }
  for (int l = 0; l < 2; ++l) {
    for (int k = 0; k < kLaneLen[l] && k < kMaxLaneLen; ++k) {
      const int w = kLane[l][k];
      if (w >= 0 && w < 25) ++in_lane[w]; else in_range = false;
    }
  }
  CHECK(in_range);
  bool chains_once = true, lanes_0_to_19 = true;
  for (int w = 0; w < 25; ++w) {
    chains_once = chains_once && on_chain[w] == 1;
    lanes_0_to_19 = lanes_0_to_19 && in_lane[w] == (w < 20 ? 1 : 0);
  }
  CHECK(chains_once);
  CHECK(lanes_0_to_19);
  // Lane 1's kinds: sound and stage; lane 2's: morphs and model work (model:2).
  bool lane1_kinds = true, lane2_kinds = true;
  for (int k = 0; k < kLaneLen[0] && k < kMaxLaneLen; ++k) {
    const int w = kLane[0][k];
    const uint32_t e = (w >= 0 && w < 25) ? kEntryOf[w] : 0;
    lane1_kinds = lane1_kinds && (e == 0x825414B0u || e == 0x82573A40u || e == 0x82573958u || e == 0x825689E8u);
  }
  for (int k = 0; k < kLaneLen[1] && k < kMaxLaneLen; ++k) {
    const int w = kLane[1][k];
    const uint32_t e = (w >= 0 && w < 25) ? kEntryOf[w] : 0;
    lane2_kinds = lane2_kinds && (e == 0x82374990u || e == 0x8234A160u);
  }
  CHECK(lane1_kinds && kLaneLen[0] > 0);
  CHECK(lane2_kinds && kLaneLen[1] > 0);
  // No cycle on the table (model:3); a cycle with lane 1's 0 and 1 swapped (model:4) and with lane 2's 12 and 16 swapped (model:5).
  CHECK(TablesAcyclic(kChain, kChainLen, kLane, kLaneLen));
  CHECK(TablesAcyclic(kTChain, kTChainLen, kTLane, kTLaneLen));
  int swapped1[2][12], swapped2[2][12];
  std::memcpy(swapped1, kTLane, sizeof swapped1);
  std::memcpy(swapped2, kTLane, sizeof swapped2);
  std::swap(swapped1[0][0], swapped1[0][1]);
  std::swap(swapped2[1][0], swapped2[1][1]);
  CHECK(swapped1[0][0] == 1 && swapped2[1][0] == 16);  // the controls are what they say
  CHECK(!TablesAcyclic(kTChain, kTChainLen, swapped1, kTLaneLen));
  CHECK(!TablesAcyclic(kTChain, kTChainLen, swapped2, kTLaneLen));
}

// ---- the placement ----
static void TestPlacementMatchesTable() {
  Group("placement_matches_table");
  const Placement p = Place(HandlesFrom(kChain, kChainLen), IndexOrder());
  CHECK(p.Complete());
  CHECK(p.Mismatch() == nullptr);
  // wd:np_rb0_sched_host_test.cpp:84-85 (same table)
  CHECK(p.ChainOf(20) == 0 && p.PosOf(20) == 6 && p.ChainOf(21) == 1 && p.PosOf(21) == 4 && p.ChainOf(23) == 3 && p.PosOf(23) == 2);
  CHECK(p.ChainOf(3) == 2 && p.PosOf(3) == 0 && p.ChainOf(9) == 2 && p.PosOf(9) == 1 && p.ChainOf(8) == 1 && p.PosOf(8) == 1);
  bool all = true;  // every index against the literal chains
  for (int c = 0; c < 5; ++c) {
    for (int k = 0; k < kTChainLen[c]; ++k) all = all && p.ChainOf(kTChain[c][k]) == c && p.PosOf(kTChain[c][k]) == k;
  }
  CHECK(all);
}

static bool SamePlaces(const Placement& a, const Placement& b) {
  for (int i = 0; i < 25; ++i) {
    if (a.ChainOf(i) != b.ChainOf(i) || a.PosOf(i) != b.PosOf(i) || a.ChainOf(i) < 0) return false;
  }
  return true;
}

static void TestPlacementAnyOrder() {
  Group("placement_any_call_order");
  const Handles h = HandlesFrom(kChain, kChainLen);
  const Placement ref = Place(h, IndexOrder());
  std::vector<int> reverse;
  for (int i = 24; i >= 0; --i) reverse.push_back(i);
  const Placement r = Place(h, reverse);
  CHECK(r.Complete() && r.Mismatch() == nullptr);
  CHECK(SamePlaces(r, ref));
  std::vector<int> mixed;  // kLane[1], then 24 down to 20, then kLane[0]
  for (int k = 0; k < kLaneLen[1] && k < kMaxLaneLen; ++k) mixed.push_back(kLane[1][k]);
  for (int i = 24; i >= 20; --i) mixed.push_back(i);
  for (int k = 0; k < kLaneLen[0] && k < kMaxLaneLen; ++k) mixed.push_back(kLane[0][k]);
  bool valid = mixed.size() == 25;  // precondition: the order holds every index once
  std::vector<int> sorted = mixed;
  std::sort(sorted.begin(), sorted.end());
  valid = valid && sorted == IndexOrder();
  CHECK(valid);
  if (valid) {
    const Placement m = Place(h, mixed);
    CHECK(m.Complete() && m.Mismatch() == nullptr);
    CHECK(SamePlaces(m, ref));
  } else {
    CHECK(false);
    CHECK(false);
  }
}

static void TestPlacementRefusals() {
  Group("placement_refusals");
  const Handles base = HandlesFrom(kTChain, kTChainLen);
  {  // 24 of 25 set
    const Placement p = Place(base, IndexOrder(24));
    CHECK(!p.Complete());
    CHECK(Has(p.Mismatch(), "worker 24: "));
  }
  {  // index 6 given entry 82573A40
    Handles h = base;
    h.entry[6] = 0x82573A40u;
    const Placement p = Place(h, IndexOrder());
    CHECK(p.Complete());
    CHECK(Has(p.Mismatch(), "worker 6: "));
  }
  {  // workers 8 and 13 exchanged on chain 1: 13 waits for 2, 8 for 13, 17 for 8
    Handles h = base;
    h.wait[13] = DoneOf(2);
    h.wait[8] = DoneOf(13);
    h.wait[17] = DoneOf(8);
    const Placement p = Place(h, IndexOrder());
    CHECK(p.ChainOf(8) == 1 && p.ChainOf(13) == 1 && p.ChainOf(17) == 1);  // precondition: every one still on chain 1 (a set compare passes)
    CHECK(Has(p.Mismatch(), "worker 8: ") || Has(p.Mismatch(), "worker 13: "));
  }
  {  // worker 23 waits for 22's done event: chain 2 six long, chain 3 two
    Handles h = base;
    h.wait[23] = DoneOf(22);
    const Placement p = Place(h, IndexOrder());
    CHECK(p.ChainOf(23) == 2 && p.PosOf(23) == 5);
    CHECK(Has(p.Mismatch(), "worker 23: "));
  }
  {  // two workers waiting for one event: 23 waits for 4's done event, as 10 does
    Handles h = base;
    h.wait[23] = DoneOf(4);
    const Placement p = Place(h, IndexOrder());
    CHECK(Has(p.Mismatch(), "worker 23: "));
  }
  {  // two workers each waiting for the other's done event (Resolve returns: this line is reached)
    Handles h = base;
    h.wait[10] = DoneOf(23);
    h.wait[23] = DoneOf(10);
    const Placement p = Place(h, IndexOrder());
    CHECK(Has(p.Mismatch(), "worker 10: ") || Has(p.Mismatch(), "worker 23: "));
  }
  {  // a wait that is no start event and nobody's done event
    Handles h = base;
    h.wait[17] = 0xDEAD0000u;
    const Placement p = Place(h, IndexOrder());
    CHECK(Has(p.Mismatch(), "worker 17: ") || Has(p.Mismatch(), "worker 21: "));
  }
  {  // index 5 set twice
    std::vector<int> order = IndexOrder();
    order.push_back(5);
    const Placement p = Place(base, order);
    CHECK(Has(p.Mismatch(), "worker 5: "));
  }
  {  // positive control on the same builder: the untouched handles match
    const Placement p = Place(base, IndexOrder());
    CHECK(p.Mismatch() == nullptr);
  }
}

static void TestPlacementSame() {
  Group("placement_same");
  const Placement p = Place(HandlesFrom(kTChain, kTChainLen), IndexOrder());
  CHECK(p.Same(7, DoneOf(7), DoneOf(6)));  // 7 follows 6 on chain 0
  CHECK(!p.Same(7, DoneOf(7) + 4, DoneOf(6)));
  CHECK(!p.Same(7, DoneOf(7), DoneOf(1)));
  CHECK(p.Same(0, DoneOf(0), kStart[0]));
  CHECK(!p.Same(0, DoneOf(0), kStart[1]));
}

// ---- the rules of a frame ----
static std::vector<int> MayRunSet(const Turns& t) {
  std::vector<int> v;
  for (int w = 0; w < 25; ++w) {
    if (t.MayRun(w)) v.push_back(w);
  }
  return v;
}

static const std::vector<int> kSeven = {0, 12, 20, 21, 22, 23, 24};  // model:6 and model:14

static void RunSection(Turns& t, int w) {  // a whole section: wake, mid wait, job, end
  t.Begin(w);
  t.MidWaitBegin(w);
  t.MidWaitEnd(w);
  t.End(w);
}

static void TestTurnsFrameStart() {
  Group("turns_frame_start");
  const Turns t;
  CHECK(MayRunSet(t) == kSeven);
  CHECK(!t.FirstWaitReady());   // model:7
  CHECK(!t.SecondWaitReady());  // model:7
}

static void TestTurnsFirstWait() {
  Group("turns_first_wait");
  Turns t;
  t.Begin(0);
  CHECK(!t.FirstWaitReady());  // model:8
  t.MidWaitBegin(0);
  CHECK(t.FirstWaitReady());  // model:9
  CHECK(!t.MayRun(2) && !t.MayRun(3) && !t.MayRun(4) && !t.MayRun(5));  // model:9, the heads held by lane 1
}

static void TestTurnsLaneHandover() {
  Group("turns_lane_handover");
  Turns t;
  t.MainReachedMid();
  for (int w : {0, 1, 6, 7}) RunSection(t, w);
  std::vector<int> open;
  for (int w = 0; w < 20; ++w) {
    if (w == 0 || w == 1 || w == 6 || w == 7) continue;
    if (t.MayRun(w)) open.push_back(w);
  }
  CHECK(open == std::vector<int>({2, 12}));  // model:10
}

static void TestTurnsSecondWait() {
  Group("turns_second_wait");
  Turns all;
  for (int w = 0; w < 20; ++w) {
    all.Begin(w);
    all.End(w);
  }
  CHECK(all.SecondWaitReady());  // model:11
  Turns one_short;
  for (int w = 0; w < 19; ++w) {
    one_short.Begin(w);
    one_short.End(w);
  }
  CHECK(!one_short.SecondWaitReady());  // model:12
}

static void TestTurnsNoMidFrame() {
  Group("turns_no_mid_frame");
  Turns t;
  const bool had_mid = t.MainReachedJoin();
  CHECK(!had_mid);
  CHECK(MayRunSet(t) == kSeven);  // the join opens nothing (P19; the model's line 13 is the engine's rule, not ported)
  Turns u;
  u.MainReachedMid();
  const bool had_mid_u = u.MainReachedJoin();
  CHECK(had_mid_u);
  CHECK(MayRunSet(u) == kSeven);  // model:14
}

static void TestTurnsEndedUnseen() {
  Group("turns_ended_unseen");
  {
    Turns t;
    t.MainReachedMid();
    t.Begin(0);
    CHECK(t.End(0));     // the mid event, and 0 never returned from a mid wait
    CHECK(t.MayRun(1));  // 0 is still marked ended: only the return value reports
  }
  {
    Turns t;
    t.MainReachedMid();
    t.Begin(0);
    t.MidWaitBegin(0);
    t.MidWaitEnd(0);
    CHECK(!t.End(0));
  }
  {
    Turns t;
    t.MainReachedMid();
    CHECK(!t.End(0));  // no section open
  }
  {
    Turns t;
    t.Begin(0);
    CHECK(!t.End(0));  // a frame without the mid event
  }
  {
    Turns t;  // a fresh frame: End with no section open leaves 0 not ended (review m3)
    CHECK(!t.End(0));
    CHECK(!t.MayRun(1));
    CHECK(t.BlockerOf(1) == 0);
  }
}

static void TestTurnsCounts() {
  Group("turns_counts");
  Turns t;
  t.MainWaiting(true);
  t.Begin(0);
  CHECK(t.Running() == 1);  // a count of workers at their job would be 0 here
  t.MidWaitBegin(0);
  t.MainReachedMid();
  t.MidWaitEnd(0);
  CHECK(t.MaxAtOnce() == 1);
  CHECK(t.MaxBesideMain() == 0);
  t.MainWaiting(false);
  CHECK(t.MaxBesideMain() == 1);  // wd:np_rb0_sched_pure.cpp:388-391
  t.End(0);
  CHECK(t.Running() == 0);
  Turns f;
  f.Begin(20);
  CHECK(f.Running() == 1);  // a free worker's section counts
  // MainReachedJoin sets the join mark, which the count "most beside main" reads (plan line 572; wd:np_rb0_sched_pure.cpp:388-391;
  // review m2): a worker at its job after the join, with the main thread not waiting, is not beside the main thread's code.
  Turns j;
  j.MainWaiting(true);
  j.Begin(0);
  j.MidWaitBegin(0);
  j.MainReachedMid();
  j.MainReachedJoin();
  j.MainWaiting(false);
  j.MidWaitEnd(0);
  CHECK(j.MaxAtOnce() == 1);      // precondition: 0 is at its job, so a count beside main would be 1
  CHECK(j.MaxBesideMain() == 0);  // traced: MidWaitEnd's Note runs with the join mark set
}

static void TestTurnsFrameReset() {
  Group("turns_frame_reset");
  Turns t;
  t.MainReachedMid();
  RunSection(t, 0);
  CHECK(t.MainReachedJoin());
  t.Begin(20);  // left open across the reset
  CHECK(t.MaxAtOnce() == 1 && t.MayRun(1));  // precondition: the frame had a job and 0 ended
  t.FrameReset();
  CHECK(t.AnyInSection() && t.Running() == 1);
  CHECK(t.MaxAtOnce() == 0 && t.MaxBesideMain() == 0);
  CHECK(!t.MayRun(1));                 // nothing has ended
  CHECK(t.SecondWaitBlocker() == 0);   // nothing has ended
  t.Begin(12);
  CHECK(!t.End(12));                   // the mid mark is clear: no "ended unseen"
  t.Begin(0);
  t.MidWaitBegin(0);
  t.MidWaitEnd(0);
  t.MainWaiting(false);
  CHECK(t.MaxBesideMain() == 1);       // the join mark is clear: the count beside the main thread counts again
}

static void TestTurnsBlockers() {
  Group("turns_blockers");
  Turns t;
  CHECK(t.BlockerOf(2) == 0);
  CHECK(t.BlockerOf(12) == -1);
  CHECK(t.BlockerOf(20) == -1);
  CHECK(t.SecondWaitBlocker() == 0);
  CHECK(t.FirstWaitBlocker() == 0);
  for (int w : {0, 1}) {
    t.Begin(w);
    t.End(w);
  }
  CHECK(t.BlockerOf(2) == 6);
  Turns u;
  for (int k = 0; k < 12; ++k) {
    u.Begin(kTLane[0][k]);
    u.End(kTLane[0][k]);
  }
  for (int w : {12, 16}) {
    u.Begin(w);
    u.End(w);
  }
  CHECK(u.SecondWaitBlocker() == 13);
  // "In lane order" (plan line 572; review m4): lane 2 is 12, 16, 13, 14, so index order would name 13 where lane order names 16.
  Turns l2;
  l2.Begin(12);
  l2.End(12);
  CHECK(l2.BlockerOf(14) == 16);
  Turns s2;
  for (int k = 0; k < 12; ++k) {
    s2.Begin(kTLane[0][k]);
    s2.End(kTLane[0][k]);
  }
  s2.Begin(12);
  s2.End(12);
  CHECK(s2.SecondWaitBlocker() == 16);
  Turns v;
  v.Begin(0);
  CHECK(v.FirstWaitBlocker() == 0);
  v.MidWaitBegin(0);
  CHECK(v.FirstWaitBlocker() == -1);
}

// ---- turns_model: random interleavings of one frame (lanes_model.py's drive) ----
// The game's rules: a worker wakes when its chain predecessor has ended (position 0: when the start events are set); awake, it begins
// when Turns lets it, waits for the mid event, does its job, ends. The main thread: start events, first wait, mid event, second wait,
// its own code, join (until all 25 have ended). Chains and lanes are the test's literal ones. control 1: the driver's own turn rule over
// lane 1 with 0 and 1 swapped stands in for MayRun (the driver must then report frames that never complete); control 2: a second wait
// that is always ready (the driver must then see a lane worker at its job beside the main thread's own code).
struct DriveResult {
  bool done = false;
  std::vector<int> begins[2];
  int at_once = 0;
  int beside_lane = 0;
  bool counts_agree = true;  // Turns' MaxAtOnce equals the driver's own count, Running 0 at the end, the join saw the mid event
};

static DriveResult Drive(std::mt19937& rng, int control) {
  Turns t;
  int pred[25], lane_of[25], lane_pos[25];
  for (int w = 0; w < 25; ++w) pred[w] = -1, lane_of[w] = -1, lane_pos[w] = -1;
  for (int c = 0; c < 5; ++c) {
    for (int k = 1; k < kTChainLen[c]; ++k) pred[kTChain[c][k]] = kTChain[c][k - 1];
  }
  int lane[2][12];
  std::memcpy(lane, kTLane, sizeof lane);
  if (control == 1) std::swap(lane[0][0], lane[0][1]);
  for (int l = 0; l < 2; ++l) {
    for (int k = 0; k < kTLaneLen[l]; ++k) lane_of[lane[l][k]] = l, lane_pos[lane[l][k]] = k;
  }
  bool ended[25] = {}, in_section[25] = {}, mid_waiting[25] = {}, at_job[25] = {};
  bool mid = false;
  enum Main { kStartM, kFirst, kSecond, kOwn, kJoin } ms = kStartM;
  DriveResult r;
  const auto model_may_run = [&](int w) {
    if (lane_of[w] < 0) return true;
    for (int k = 0; k < lane_pos[w]; ++k) {
      if (!ended[lane[lane_of[w]][k]]) return false;
    }
    return true;
  };
  struct Act {
    int kind;  // 0 begin, 1 mid wait, 2 job, 3 end, 4 main step
    int w;
  };
  std::vector<Act> acts;
  for (int step = 0; step < 1000; ++step) {
    acts.clear();
    for (int w = 0; w < 25; ++w) {
      if (ended[w]) continue;
      const bool awake = ms != kStartM && (pred[w] < 0 || ended[pred[w]]);
      if (!in_section[w]) {
        const bool may = control == 1 ? model_may_run(w) : t.MayRun(w);
        if (awake && may) acts.push_back({0, w});
      } else if (!mid_waiting[w] && !at_job[w]) {
        acts.push_back({1, w});
      } else if (mid_waiting[w] && mid) {
        acts.push_back({2, w});
      } else if (at_job[w]) {
        acts.push_back({3, w});
      }
    }
    bool all_ended = true;
    for (int w = 0; w < 25; ++w) all_ended = all_ended && ended[w];
    if (ms == kStartM) acts.push_back({4, 0});
    else if (ms == kFirst && t.FirstWaitReady()) acts.push_back({4, 0});
    else if (ms == kSecond && (control == 2 || t.SecondWaitReady())) acts.push_back({4, 0});
    else if (ms == kOwn) acts.push_back({4, 0});
    else if (ms == kJoin && all_ended) acts.push_back({4, 0});
    if (acts.empty()) return r;
    const Act a = acts[rng() % acts.size()];
    switch (a.kind) {
      case 0:
        t.Begin(a.w);
        in_section[a.w] = true;
        if (lane_of[a.w] >= 0) r.begins[lane_of[a.w]].push_back(a.w);
        break;
      case 1:
        t.MidWaitBegin(a.w);
        mid_waiting[a.w] = true;
        break;
      case 2:
        t.MidWaitEnd(a.w);
        mid_waiting[a.w] = false;
        at_job[a.w] = true;
        break;
      case 3:
        t.End(a.w);
        in_section[a.w] = at_job[a.w] = false;
        ended[a.w] = true;
        break;
      default:
        if (ms == kStartM) {
          t.MainWaiting(true);  // the start events are set; the first wait
          ms = kFirst;
        } else if (ms == kFirst) {
          t.MainReachedMid();  // the mid event is set; the second wait
          mid = true;
          ms = kSecond;
        } else if (ms == kSecond) {
          t.MainWaiting(false);  // its own code up to the join
          ms = kOwn;
        } else if (ms == kOwn) {
          r.counts_agree = r.counts_agree && t.MainReachedJoin();
          t.MainWaiting(true);
          ms = kJoin;
        } else {
          r.done = true;
          r.counts_agree = r.counts_agree && t.MaxAtOnce() == r.at_once && t.Running() == 0 && !t.AnyInSection();
          return r;
        }
    }
    int jobs = 0, lane_jobs = 0;
    for (int w = 0; w < 25; ++w) {
      if (!at_job[w]) continue;
      ++jobs;
      if (lane_of[w] >= 0) ++lane_jobs;
    }
    r.at_once = std::max(r.at_once, jobs);
    if (ms == kOwn) r.beside_lane = std::max(r.beside_lane, lane_jobs);
  }
  return r;  // more than 1000 steps: not done
}

static void TestTurnsModel() {
  Group("turns_model");
  std::mt19937 rng(20261116u);
  int done = 0, order_ok = 0, beside = 0, most = 0, least_most = 1 << 30, agree = 0;
  const std::vector<int> lane1(kTLane[0], kTLane[0] + kTLaneLen[0]), lane2(kTLane[1], kTLane[1] + kTLaneLen[1]);
  for (int i = 0; i < 20000; ++i) {
    const DriveResult r = Drive(rng, 0);
    if (!r.done) continue;
    ++done;
    order_ok += r.begins[0] == lane1 && r.begins[1] == lane2;
    beside = std::max(beside, r.beside_lane);
    most = std::max(most, r.at_once);
    least_most = std::min(least_most, r.at_once);
    agree += r.counts_agree;
  }
  CHECK(done == 20000);      // model:15
  CHECK(order_ok == 20000);  // each lane's begins in the order of the literal lanes
  CHECK(beside == 0);        // no lane worker at its job while the main thread runs its own code
  CHECK(most >= 2 && most <= 7);
  CHECK(agree == 20000);
  if (g_verbose) std::printf("  20000 frames: most at their job at once %d (least over the frames %d)\n", most, least_most);
  int done1 = 0;
  for (int i = 0; i < 200; ++i) done1 += Drive(rng, 1).done;
  CHECK(done1 == 0);  // control 1, model:16
  int beside2 = 0, done2 = 0;
  for (int i = 0; i < 20000; ++i) {
    const DriveResult r = Drive(rng, 2);
    done2 += r.done;
    beside2 = std::max(beside2, r.beside_lane);
  }
  CHECK(done2 == 20000 && beside2 > 0);  // control 2, model:17
}

// ---- stall_clock (values recomputed by np_runs/lp/b/b1/recompute.py from the contract) ----
using Step = StallClock::Step;

static void TestStallClock() {
  Group("stall_clock");
  CHECK(StallClock::kSliceMs == 50 && StallClock::kMaxStepMs == 100 && StallClock::kStallMs == 2000 && StallClock::kEndMs == 8000);
  {  // steps of 50
    StallClock c;
    c.Reset();
    int bad = 0, count_at_40 = -1, count_at_160 = -1;
    for (int call = 1; call <= 161; ++call) {
      const Step s = c.Waited(50);
      const Step want = call == 40 ? Step::kLogStall : call >= 160 ? Step::kEnd : Step::kKeepWaiting;
      bad += s != want;
      if (call == 40) count_at_40 = c.CountedMs();
      if (call == 160) count_at_160 = c.CountedMs();
    }
    CHECK(bad == 0);
    CHECK(count_at_40 == 2000);
    CHECK(count_at_160 == 8000);
  }
  {  // steps of 62 (a slice the timer stretched): kLogStall on call 33 only, kEnd from call 130
    StallClock c;
    c.Reset();
    int bad = 0, at32 = -1, at33 = -1, at129 = -1, at130 = -1;
    for (int call = 1; call <= 131; ++call) {
      const Step s = c.Waited(62);
      const Step want = call == 33 ? Step::kLogStall : call >= 130 ? Step::kEnd : Step::kKeepWaiting;
      bad += s != want;
      if (call == 32) at32 = c.CountedMs();
      if (call == 33) at33 = c.CountedMs();
      if (call == 129) at129 = c.CountedMs();
      if (call == 130) at130 = c.CountedMs();
    }
    CHECK(bad == 0);
    CHECK(at32 == 1984 && at33 == 2046);
    CHECK(at129 == 7998 && at130 == 8060);
  }
  {  // one step of 9000 after Reset (a frozen process)
    StallClock c;
    c.Reset();
    const Step s = c.Waited(9000);
    CHECK(c.CountedMs() == 100 && s == Step::kKeepWaiting);
  }
  {  // steps of 1 (wakes)
    StallClock c;
    c.Reset();
    int logs = 0, log_call = -1;
    for (int call = 1; call <= 2001; ++call) {
      if (c.Waited(1) == Step::kLogStall) ++logs, log_call = call;
    }
    CHECK(logs == 1 && log_call == 2000);
  }
  {  // a step of -5
    StallClock c;
    c.Reset();
    c.Waited(50);
    const Step s = c.Waited(-5);
    CHECK(c.CountedMs() == 50 && s == Step::kKeepWaiting);
  }
  {  // 39 steps of 50, Reset, 39 steps of 50: never kLogStall
    StallClock c;
    c.Reset();
    int logs = 0;
    for (int i = 0; i < 39; ++i) logs += c.Waited(50) == Step::kLogStall;
    CHECK(c.CountedMs() == 1950);
    c.Reset();
    CHECK(c.CountedMs() == 0);
    for (int i = 0; i < 39; ++i) logs += c.Waited(50) == Step::kLogStall;
    CHECK(logs == 0);
  }
}

// ---- end_request ----
static void TestEndRequest() {
  Group("end_request");
  {
    EndRequest e;
    CHECK(!e.Requested() && e.Cause() == EndCause::kNone && e.Text().empty());
    CHECK(e.Set(EndCause::kStall, "a"));
    CHECK(!e.Set(EndCause::kVsm, "b"));
    CHECK(e.Requested());
    CHECK(e.Cause() == EndCause::kStall);
    CHECK(e.Text() == "a");
  }
  int bad_rounds = 0, bad_text = 0;
  for (int round = 0; round < 1000; ++round) {
    EndRequest e;
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<int> won{0};
    std::atomic<int> winner{-1};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
      threads.emplace_back([&, i] {
        ready.fetch_add(1);
        while (!go.load()) {
        }
        if (e.Set(EndCause::kFrame, "thread " + std::to_string(i))) {
          won.fetch_add(1);
          winner.store(i);
        }
      });
    }
    while (ready.load() < 8) {
    }
    go.store(true);
    for (std::thread& th : threads) th.join();
    bad_rounds += won.load() != 1;
    bad_text += e.Text() != "thread " + std::to_string(winner.load()) || e.Cause() != EndCause::kFrame;
  }
  CHECK(bad_rounds == 0);  // 8 threads at once, 1000 rounds: exactly one true a round
  CHECK(bad_text == 0);    // and the stored text is the winner's
}

// ---- decide_mode ----
static ModeChoice M(const char* sw, bool fix, bool online, bool hserial, bool replay, const char* meta) {
  ModeInputs in;
  in.serial_switch = sw;
  in.workers_fix_on = fix;
  in.online = online;
  in.handoff_serial = hserial;
  in.replay = replay;
  in.meta_workers = meta;
  return DecideMode(in);
}
static bool Is(const ModeChoice& c, int mode, bool refused) { return c.mode == mode && c.refused == refused && c.why.empty() != refused; }

static void TestDecideMode() {
  Group("decide_mode");
  CHECK(Is(M(nullptr, true, false, false, false, nullptr), 4, false));  // no switch, fix on, offline record
  CHECK(Is(M(nullptr, false, true, false, false, nullptr), 0, false));  // fix off, online, hand-off lanes
  CHECK(Is(M("3", true, true, false, false, nullptr), 3, false));       // the switch before online
  CHECK(Is(M("4", true, true, true, false, nullptr), 4, false));
  CHECK(Is(M("0", true, false, false, false, nullptr), 0, false));
  CHECK(Is(M("", true, false, false, false, nullptr), 4, false));       // "" as no switch
  CHECK(Is(M("", true, true, true, false, nullptr), 3, false));
  CHECK(Is(M(nullptr, true, true, false, false, nullptr), 4, false));   // online, hand-off lanes
  CHECK(Is(M(nullptr, true, true, true, false, nullptr), 3, false));    // online, hand-off serial
  CHECK(Is(M(nullptr, true, false, false, true, "serial"), 3, false));  // replay
  CHECK(Is(M(nullptr, true, false, false, true, "lanes"), 4, false));
  CHECK(Is(M(nullptr, true, false, false, true, nullptr), 3, true));
  CHECK(Is(M(nullptr, true, false, false, true, "mode2"), 3, true));
  for (const char* sw : {"5", "x", "-1", "3x", " 3", "03"}) CHECK(Is(M(sw, true, false, false, false, nullptr), 4, true));
  CHECK(Is(M("3x", true, true, false, false, nullptr), 4, true));
  CHECK(Is(M("3x", true, true, true, false, nullptr), 3, true));
  CHECK(Is(M("x", false, false, false, false, nullptr), 0, true));      // a refused switch never turns into a fixed mode
  CHECK(Is(M(nullptr, false, false, false, true, "lanes"), 0, false));  // the fix is asked before the replay (rules 2 and 4; review m5)
  CHECK(Is(M(nullptr, false, false, false, true, nullptr), 0, false));
  // the reader's round trip
  for (int m : {3, 4}) {
    const std::string meta = MetaWorkers(m);
    CHECK(Is(M(nullptr, true, false, false, true, meta.c_str()), m, false));
  }
  CHECK(MetaWorkers(4) == "lanes" && MetaWorkers(3) == "serial");
  CHECK(MetaWorkers(0) == "mode0" && MetaWorkers(1) == "mode1" && MetaWorkers(2) == "mode2");
}

// ---- lanes_test_switch ----
static bool NothingSet(const LanesTest& t) {
  return !t.vsm_hash && !t.refuse_placement && t.stall_worker == -1 && t.stall_frame == -1 && t.stall_ms == 0;
}
static bool Refused(const char* text) {
  const LanesTest t = ParseLanesTest(text);
  return !t.ok && NothingSet(t) && !t.error.empty();
}

static void TestLanesTestSwitch() {
  Group("lanes_test_switch");
  {
    const LanesTest a = ParseLanesTest(nullptr), b = ParseLanesTest("");
    CHECK(a.ok && NothingSet(a) && a.error.empty());
    CHECK(b.ok && NothingSet(b) && b.error.empty());
  }
  {
    const LanesTest t = ParseLanesTest("vsm=hash");
    CHECK(t.ok && t.vsm_hash && !t.refuse_placement && t.stall_worker == -1);
  }
  {
    const LanesTest t = ParseLanesTest("refuse=placement");
    CHECK(t.ok && !t.vsm_hash && t.refuse_placement && t.stall_worker == -1);
  }
  {
    const LanesTest t = ParseLanesTest("stall=13@600:3000");
    CHECK(t.ok && t.stall_worker == 13 && t.stall_frame == 600 && t.stall_ms == 3000 && !t.vsm_hash && !t.refuse_placement);
  }
  {
    const LanesTest t = ParseLanesTest("stall=24@0:1;vsm=hash");
    CHECK(t.ok && t.stall_worker == 24 && t.stall_frame == 0 && t.stall_ms == 1 && t.vsm_hash && !t.refuse_placement);
  }
  {
    const LanesTest t = ParseLanesTest("refuse=placement;stall=0@7:20000");
    CHECK(t.ok && t.stall_worker == 0 && t.stall_frame == 7 && t.stall_ms == 20000 && t.refuse_placement && !t.vsm_hash);
  }
  {
    const LanesTest t = ParseLanesTest("stall=1@999999999:5");  // 9 digits: the largest number accepted (review m3)
    CHECK(t.ok && t.stall_worker == 1 && t.stall_frame == 999999999 && t.stall_ms == 5 && !t.vsm_hash && !t.refuse_placement);
  }
  for (const char* bad : {"vsm=sort", "stall=25@1:1", "stall=13@600:0", "stall=13@600:20001", "stall=13@-1:5", "stall=13", "stall=013@1:1",
                          "late=24:1500", "refuse=x", "vsm=hash;vsm=hash", "vsm=hash;", " vsm=hash", "x",
                          "refuse=placement;refuse=placement", "stall=1@1:1;stall=1@1:1",  // each at most once (review m6)
                          "stall=1@1234567890:5"}) {                                         // more than 9 digits
    const bool refused = Refused(bad);
    CHECK(refused);
    if (!refused && g_verbose) std::printf("  accepted: '%s'\n", bad);
  }
}

// ---- wait_histogram (recompute_out.txt lines 1 to 10; the plan's row, ledger L25) ----
static WaitHistogram H(const std::vector<uint64_t>& samples) {
  WaitHistogram h;
  for (uint64_t us : samples) h.Add(us);
  return h;
}
static bool Hist(const WaitHistogram& h, uint64_t p50, uint64_t p99, uint64_t max, uint64_t count) {
  return h.P50Us() == p50 && h.P99Us() == p99 && h.MaxUs() == max && h.Count() == count;
}

static void TestWaitHistogram() {
  Group("wait_histogram");
  std::vector<uint64_t> a;
  for (uint64_t k = 1; k <= 100; ++k) a.push_back(100 * k);
  CHECK(Hist(H(a), 5250, 10000, 10000, 100));
  std::vector<uint64_t> b(99, 2000);
  b.push_back(9000000);
  CHECK(Hist(H(b), 2250, 2250, 9000000, 100));
  CHECK(Hist(H({249}), 250, 250, 249, 1));
  CHECK(Hist(H({250}), 500, 500, 250, 1));
  const WaitHistogram c = H({1000, 1000, 7000});
  CHECK(Hist(c, 1250, 7250, 7000, 3));
  CHECK(c.P50Us() != 3250);  // NOT the mean's bucket
  CHECK(Hist(H({15749}), 15750, 15750, 15749, 1));
  CHECK(Hist(H({15999}), 15999, 15999, 15999, 1));  // bucket 63: the largest sample
  CHECK(Hist(H({20000, 30000}), 30000, 30000, 30000, 2));
  CHECK(Hist(H({}), 0, 0, 0, 0));
}

// ---- cs_decision ----
// The 14 checks of wd:np_rb0_sched_host_test.cpp:375-397, values unchanged. The values a free, initialised critical section holds come
// from the SDK's initialisation (sdk-rb0 src/kernel/xboxkrnl/xboxkrnl_rtl.cpp:397-402, xeRtlInitializeCriticalSection: header type 1,
// lock count -1, no stash), and the stash a first contention writes at +8 and +12 (sdk-rb0 include/rex/system/xobject.h:35
// kXObjSignature 'REX\0' = 52455800 and :225-228 StashHandle; the handle F80095F8 is the one np_runs/rb0/t6/workers_report.md line 218
// read from a dump at 83302248).
static bool HasS(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

static void TestCsDecision() {
  Group("cs_decision");
  const auto act = [](uint8_t type, int32_t lock, uint32_t w8, uint32_t w12) { return DecideCs(type, lock, w8, w12).action; };
  const auto why = [](uint8_t type, int32_t lock, uint32_t w8, uint32_t w12) { return DecideCs(type, lock, w8, w12).why; };
  // an initialised, free section with zero words at +8 is made
  CHECK(act(1, -1, 0, 0) == CsAction::kMake && why(1, -1, 0, 0).empty());
  // a section with its object already (non-zero words at +8) is 'already', never made twice
  CHECK(act(1, -1, 0x52455800u, 0xF80095F8u) == CsAction::kAlready);
  CHECK(act(1, -1, 0x52455800u, 0) == CsAction::kAlready && act(1, -1, 0, 0xF80095F8u) == CsAction::kAlready);  // NOT kMake: either word
  // a type byte other than 1 with a free lock count is refused (the SDK would take the object's type from it)
  CHECK(act(0, -1, 0, 0) == CsAction::kRefuse && HasS(why(0, -1, 0, 0), "type"));  // 0 would make a manual-reset event
  CHECK(act(5, -1, 0, 0) == CsAction::kRefuse && act(2, -1, 0, 0) == CsAction::kRefuse);  // a semaphore, a mutant
  CHECK(act(0, 0, 0, 0) == CsAction::kRefuse);  // zero-filled memory: not initialised
  CHECK(act(5, -1, 0x52455800u, 0xF80095F8u) == CsAction::kRefuse);  // NOT 'already': a stash on something that is not a section
  // type 1 with a held lock count is refused, with or without an object
  CHECK(act(1, 0, 0, 0) == CsAction::kRefuse && HasS(why(1, 0, 0, 0), "held"));
  CHECK(act(1, 3, 0, 0) == CsAction::kRefuse);
  CHECK(act(1, 0, 0x52455800u, 0xF80095F8u) == CsAction::kRefuse);  // NOT 'already': held at a frame hook is not a state to pass over
  // a lock count below -1 is no critical section's
  CHECK(act(1, -2, 0, 0) == CsAction::kRefuse && act(1, INT32_MIN, 0, 0) == CsAction::kRefuse);
  // cs_after_make: the object is made only when the call returned one and +8 holds the signature and +12 a handle
  CHECK(CsAfterMake(true, 0x52455800u, 0xF80095F8u) == nullptr);
  CHECK(CsAfterMake(false, 0x52455800u, 0xF80095F8u) != nullptr && CsAfterMake(true, 0, 0) != nullptr);
  CHECK(CsAfterMake(true, 0x52455801u, 0xF80095F8u) != nullptr && CsAfterMake(true, 0x52455800u, 0) != nullptr);

  // StateAfter (ledger L30), each from a real DecideCs call
  const char* made_ok = CsAfterMake(true, 0x52455800u, 0xF80095F8u);
  const char* made_bad = CsAfterMake(false, 0, 0);
  CHECK(made_ok == nullptr && made_bad != nullptr);  // precondition: the two after_make values are what they stand for
  const SectionState made = StateAfter(DecideCs(1, -1, 0, 0), made_ok);
  const SectionState make_failed = StateAfter(DecideCs(1, -1, 0, 0), made_bad);
  const SectionState already = StateAfter(DecideCs(1, -1, 0x52455800u, 0xF80095F8u), nullptr);
  const SectionState not_init = StateAfter(DecideCs(0, 0, 0, 0), nullptr);
  const SectionState held = StateAfter(DecideCs(1, 0, 0, 0), nullptr);
  CHECK(made == SectionState::kMade);
  CHECK(make_failed == SectionState::kRefused);
  CHECK(already == SectionState::kAlready);
  CHECK(not_init == SectionState::kNotInitialised);
  CHECK(held == SectionState::kRefused);  // held is refused, NOT "not initialised"

  // SectionsVerdict
  const SectionState all_good[4] = {made, already, made, made};
  CHECK(!SectionsVerdict(all_good, true));
  const SectionState first_refused[4] = {held, made, made, made};
  CHECK(SectionsVerdict(first_refused, true));
  CHECK(!SectionsVerdict(first_refused, false));
  const SectionState fourth_not_init[4] = {made, made, made, not_init};
  CHECK(!SectionsVerdict(fourth_not_init, false));
  const SectionState fourth_held[4] = {made, made, made, held};
  CHECK(SectionsVerdict(fourth_held, false));
  const SectionState first_not_init[4] = {not_init, made, made, made};
  CHECK(SectionsVerdict(first_not_init, true));
  // On a fight frame any one of the four decides, not only the first (brief item 2, ledger L30; review m1)
  const SectionState second_held[4] = {made, held, made, made};
  CHECK(SectionsVerdict(second_held, true));
  const SectionState third_not_init[4] = {made, made, not_init, made};
  CHECK(SectionsVerdict(third_not_init, true));
  CHECK(SectionsVerdict(fourth_not_init, true));  // {made, made, made, not initialised}
}

// ---- lines ----
static void TestLines() {
  Group("lines");
  CHECK(DecisionLine(4, false) == "workers: lanes, table s6v1, cs 4, vsm sort");
  CHECK(DecisionLine(4, true) == "workers: lanes, table s6v1, cs 4, vsm hash");
  CHECK(DecisionLine(3, false) == "workers: serial");
  CHECK(DecisionLine(1, false) == "workers: mode 1");
  const Handles h = HandlesFrom(kTChain, kTChainLen);
  CHECK(PlacementLine(Place(h, IndexOrder())) ==
        "workers placed: chain 0: 0 1 6 7 12 16 20; chain 1: 2 8 13 17 21; chain 2: 3 9 14 18 22; chain 3: 4 10 23; chain 4: 5 11 15 19 24; "
        "table: yes");
  Handles x = h;  // 8 and 13 exchanged on chain 1
  x.wait[13] = DoneOf(2);
  x.wait[8] = DoneOf(13);
  x.wait[17] = DoneOf(8);
  CHECK(PlacementLine(Place(x, IndexOrder())) ==
        "workers placed: chain 0: 0 1 6 7 12 16 20; chain 1: 2 13 8 17 21; chain 2: 3 9 14 18 22; chain 3: 4 10 23; chain 4: 5 11 15 19 24; "
        "table: NO (worker 8: chain 1 position 2, the table has chain 1 position 1)");
  CHECK(SectionLine(0x83302240u, SectionState::kMade, 0x52455800u, 0xF80095F8u, "") == "section 83302240: made 52455800 F80095F8");
  CHECK(SectionLine(0x8321B178u, SectionState::kAlready, 0x52455800u, 0xF8001ABCu, "") == "section 8321B178: already 52455800 F8001ABC");
  CHECK(SectionLine(0x83303A68u, SectionState::kRefused, 0, 0, "x") == "section 83303A68: refused: x");
  CHECK(SectionLine(0x835780E0u, SectionState::kNotInitialised, 0, 0, "y") == "section 835780E0: refused: y");
  CHECK(LanesLine(600, 412, 5, 0, 1, 0) == "lanes: frames 600, 2+ at once 412, most at once 5, most beside main 0, stalls 1, end requests 0");
  CHECK(CpuLine(3, H({1000, 1000, 7000}), 123456789, 987654321) ==
        "cpu: fight frames 3, wait p50 1.25 ms p99 7.25 ms max 7.00 ms, cycles main 123456789 workers 987654321");
  CHECK(CpuLine(1, H({15749}), 0, 0) == "cpu: fight frames 1, wait p50 15.75 ms p99 15.75 ms max 15.75 ms, cycles main 0 workers 0");
  CHECK(CpuLine(0, H({}), 0, 0) == "cpu: fight frames 0, wait p50 0.00 ms p99 0.00 ms max 0.00 ms, cycles main 0 workers 0");
  CHECK(StallLine(611, 13, 2) == "lanes stall at frame 611: worker 13 has waited 2 s for worker 2");
  CHECK(StallLine(611, kMainFirstWait, 0) == "lanes stall at frame 611: main (first wait) has waited 2 s for worker 0");
  CHECK(StallLine(611, kMainSecondWait, 19) == "lanes stall at frame 611: main (second wait) has waited 2 s for worker 19");
  CHECK(StallOverLine(611, 2350, {0, 1, 6, 7, 2}, {12, 16}) ==
        "lanes stall over at frame 611 after 2350 ms: lane 1 began 0 1 6 7 2; lane 2 began 12 16");
  CHECK(StallOverLine(5, 2000, {}, {12}) == "lanes stall over at frame 5 after 2000 ms: lane 1 began none; lane 2 began 12");
  CHECK(EndRequestLine(42, "x") == "lanes end request at frame 42: x");
  CHECK(EndRequestWarn("x") == "WARN lanes: x: the run is not evidence");
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-v") == 0) g_verbose = true;
  }
  TestTable();
  TestPlacementMatchesTable();
  TestPlacementAnyOrder();
  TestPlacementRefusals();
  TestPlacementSame();
  TestTurnsFrameStart();
  TestTurnsFirstWait();
  TestTurnsLaneHandover();
  TestTurnsSecondWait();
  TestTurnsNoMidFrame();
  TestTurnsEndedUnseen();
  TestTurnsCounts();
  TestTurnsFrameReset();
  TestTurnsBlockers();
  TestTurnsModel();
  TestStallClock();
  TestEndRequest();
  TestDecideMode();
  TestLanesTestSwitch();
  TestWaitHistogram();
  TestCsDecision();
  TestLines();
  EndGroup();
  if (g_failures == 0) {
    std::printf("PASSED: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
  return 1;
}
