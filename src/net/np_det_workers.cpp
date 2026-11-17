// DOA4 netplay phase 0: the job workers (fix "workers").
//
// sub_827921C0 = SignalObjectAndWait(signal r3, wait r4, timeout r5, alertable
// r6) is the top of every job worker's loop (signal own done, wait for the next
// start); only the 25 job workers call it (census logged once per caller). They
// form five per-core chains that run in parallel each frame. Parallel is not
// deterministic: scene jobs on several workers draw from the shared scene RNG
// (MT 0x8357B5C0), and the interleaving of their draws changed which particle
// got which number (a petal-emitter job drew 156 vs 152 times at frame 47).
// Ordering the workers among themselves is not enough either: scene jobs and the
// main thread's update code share state (the stage-light timers, the scene RNG
// again), and a worker running beside the main thread made a Tag fight diverge
// now and then (frames 4026, 5923, 7421 in different runs; one in three runs).
// Modes (DOA4_NP_SERIAL overrides; the fix's default in a session is 3):
//   0  parallel, as the game runs them;
//   1  mutually exclusive: a worker holds one process-wide lock from its wait's
//      return to its next signal (the order is still the OS's);
//   2  one at a time in a fixed order: the order the game created the workers in
//      (the thread-creation hook, np_det.cpp), which is round-robin over the five
//      chains and agrees with each chain's own order; a token, reset by the frame
//      hook, passes from worker to worker as each one signals. A worker that
//      wakes without a rank, or a token that does not come within 2 s, is a
//      counted fallback (fb_unranked, fb_worker_token). Every worker runs exactly
//      once per frame (traced), so the token never waits for one that does not
//      come;
//   3  mode 2, and the workers never run beside the main thread: after the
//      update half sets the pool's start events the main thread waits until
//      the workers have gone as far as they can before the pool's mid event,
//      and after it sets the mid event it waits until all are done.
//   4  "lanes" (the lockstep port, task B2; design notes/specs/2026-11-16-doa4-
//      lockstep-port-design.md): the fixed table of np_lanes_pure.h, workers
//      in two lanes run one after another in each lane while the lanes and the
//      free (cloth) workers run beside each other; the lane workers never run
//      beside the main thread's own code, the free (cloth) workers may from
//      the main thread's second wait until the join (design section 3, rule
//      4); no fallback continues out of order: a wait that
//      cannot be kept ends the session (np_det_lanes.cpp, the engine).
// The decision (design section 4) is made once a process, at the first Mode(),
// by lanes::DecideMode over the inputs det::SetWorkerInputs gave (both session
// starts call it before CurrentMeta, whose workers= line is the result).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/xthread.h>

#include "generated/default/dead_or_alive_4_init.h"

#include "np_det.h"
#include "np_det_internal.h"
#include "np_det_lanes.h"
#include "np_lanes_pure.h"
#include "np_session.h"

extern "C" {
REX_EXTERN(__imp__sub_827921C0);
REX_EXTERN(__imp__sub_82792140);
REX_EXTERN(__imp__sub_82791190);
REX_EXTERN(__imp__sub_827922E0);
REX_EXTERN(__imp__sub_82732950);
}

namespace doa4::np::det {
namespace {

constexpr int kWorkerCount = 25;
std::mutex g_mu;          // mode 2: the token
std::condition_variable g_cv;
std::mutex g_serial_mu;   // mode 1: held through a worker's work section
thread_local bool t_held = false;   // mode 1
thread_local int t_rank = -1;       // mode 2
std::atomic<int> g_mode{-1};        // -1: not decided yet
std::vector<uint32_t> g_tids;       // worker thread ids in creation order: the index is the rank
int g_token = 0;
uint32_t g_timeouts = 0;
int g_mid_waiter = -1;              // rank of the worker blocked on the pool's mid event
uint32_t g_main_timeouts = 0;
constexpr uint32_t kPool = 0x8341FAA0;  // job pool: start events +20..+36, done +40.., mid +60
// The 25 job workers' entry points (phase0_facts.md 5; the proof's SignalObjectAndWait census).
constexpr uint32_t kWorkerEntries[] = {0x825414B0, 0x82573A40, 0x82573958, 0x825689E8,
                                       0x82374990, 0x8234A160, 0x82370638};
uint32_t g_unranked_logs = 0;
thread_local int t_lane = -2;       // mode 4: -2 not seen yet, -1 not placed, else the worker's creation rank
thread_local bool t_observed = false;  // mode 3: the placement observer has seen this worker's first call (P13)

// det::SetWorkerInputs (both session starts, before CurrentMeta): written once, before the decision reads them.
std::atomic<bool> g_inputs_given{false};
bool g_in_fix = true, g_in_online = false, g_in_handoff_serial = false, g_in_replay = false;
std::string g_in_meta;
std::once_flag g_decide_once;

// The decision (design section 4), once a process. Without the session's inputs (ruled, brief item 12) it decides with what it can read:
// the switch when it is valid, mode 0 when the workers fix is off, otherwise 3 (the shipped order, never lanes by default): rule (3)
// with a serial hand-off gives exactly that. The decision's inputs are built by lanes::MakeModeInputs here and nowhere else.
void Decide() {
  const char* e = TestSwitch("DOA4_NP_SERIAL");
  const bool given = g_inputs_given.load(std::memory_order_acquire);
  const bool fix = given ? g_in_fix : WorkersFixAtStart();
  const bool online = given ? g_in_online : true;
  const bool handoff_serial = given ? g_in_handoff_serial : true;
  const bool replay = given && g_in_replay;
  const char* meta = replay ? g_in_meta.c_str() : nullptr;
  const doa4::np::lanes::ModeChoice c =
      doa4::np::lanes::DecideMode(doa4::np::lanes::MakeModeInputs(e, fix, online, handoff_serial, replay, meta));
  const int v = c.mode;
  const bool by_switch = e && e[0] >= '0' && e[0] <= '4' && e[1] == '\0';
  const char* test_text = TestSwitch("DOA4_NP_LANES_TEST");
  const doa4::np::lanes::LanesTest test = doa4::np::lanes::ParseLanesTest(test_text);
  eng::Configure(v, given && g_in_online, test.ok ? test : doa4::np::lanes::LanesTest{});
  g_mode.store(v);
  if (!given) LogLine("WARN workers: the order was asked for before the session gave its inputs");
  if (v <= 3) {
    LogLine(std::string("job workers: ") +
            (v == 3   ? "one at a time in a fixed order, never beside the main thread"
             : v == 2 ? "one at a time in a fixed order"
             : v == 1 ? "mutually exclusive"
                      : "parallel") +
            (by_switch ? " (DOA4_NP_SERIAL)" : ""));
  }
  LogLine(doa4::np::lanes::DecisionLine(v, v == 4 && test.ok && test.vsm_hash));
  if (c.refused) LogLine("WARN workers: " + c.why);
  if (!test.ok) LogLine(std::string("WARN lanes: DOA4_NP_LANES_TEST '") + test_text + "': " + test.error);
}

int Mode() {
  std::call_once(g_decide_once, Decide);
  return Stopped() ? 0 : g_mode.load(std::memory_order_relaxed);
}

// A worker's first call (modes 3 and 4): its creation rank, looked up with no condition on the list's size (design 2.2: at worker k's
// first call the list holds k + 1 entries), then the engine's placement. Takes g_mu only for the lookup.
int PlaceThis(uint32_t tid, uint32_t done, uint32_t wait, const uint8_t* base) {
  int rank = -1;
  const char* why = nullptr;
  {
    std::lock_guard lk(g_mu);
    const auto n = std::count(g_tids.begin(), g_tids.end(), tid);
    if (tid == 0) {
      why = "its thread id is 0";
    } else if (n == 0) {
      why = "its thread id is not in the creation list";
    } else if (n > 1) {
      why = "its thread id is in the creation list twice";
    } else {
      rank = int(std::find(g_tids.begin(), g_tids.end(), tid) - g_tids.begin());
    }
  }
  eng::Place(rank, why, tid, ThreadStart(), done, wait, base);
  return rank;
}

uint32_t Be32(const uint8_t* base, uint32_t a) {
  return uint32_t(base[a]) << 24 | uint32_t(base[a + 1]) << 16 | uint32_t(base[a + 2]) << 8 | base[a + 3];
}

// Mode 3, main thread, right after it set the pool's last start event (until
// the token holder is blocked on the mid event, or all are done) or the mid
// event (until all are done): the workers' work never overlaps the main
// thread's own update code.
double g_main_wait_us = 0, g_main_wait_max_us = 0;  // this 600-frame window
double g_join_us = 0, g_join_max_us = 0;            // the frame's join on the workers (any mode)

// The main thread's wait for the workers in mode 3: 2 s, or DOA4_NP_MAIN_WAIT_MS (a positive
// control for the gate: 0 makes every wait that is not already over a counted fallback).
std::chrono::milliseconds MainWaitTimeout() {
  static const std::chrono::milliseconds ms([] {
    const char* e = TestSwitch("DOA4_NP_MAIN_WAIT_MS");
    return e && *e ? std::atoi(e) : 2000;
  }());
  return ms;
}

void MainWaitForWorkers(bool until_done) {
  const auto t0 = std::chrono::steady_clock::now();
  struct Timed {
    std::chrono::steady_clock::time_point t0;
    ~Timed() {
      const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
      g_main_wait_us += us;
      g_main_wait_max_us = std::max(g_main_wait_max_us, us);
      eng::CpuMainWait(uint64_t(us));  // the cpu: line's mark (mode 3 only reaches here)
    }
  } timed{t0};
  std::unique_lock lk(g_mu);
  if (int(g_tids.size()) < kWorkerCount) {  // the order cannot hold: say so in the counters
    Count(kFbUnranked);
    return;
  }
  auto ready = [until_done] {
    return Stopped() || g_token >= kWorkerCount || (!until_done && g_mid_waiter >= 0 && g_mid_waiter == g_token);
  };
  if (!g_cv.wait_for(lk, MainWaitTimeout(), ready)) {
    Count(kFbMainWait);
    if (g_main_timeouts++ < 20) {
      LogLine(fmt::format("job workers: frame {}: the main thread waited {} ms for the workers ({}, token {}, "
                          "mid waiter {})",
                          Frame(), MainWaitTimeout().count(), until_done ? "all" : "up to the mid event", g_token,
                          g_mid_waiter));
    }
  }
}

// Mode 4's main waits also count in the 600-frame "job workers: main thread waited" line (main thread only, as mode 3's).
void AddMainWait(uint64_t us) {
  g_main_wait_us += double(us);
  g_main_wait_max_us = std::max(g_main_wait_max_us, double(us));
}

}  // namespace

void WorkersSetInputs(bool workers_fix_on, bool online, bool handoff_serial, bool replay, const std::string& meta_workers) {
  g_in_fix = workers_fix_on;
  g_in_online = online;
  g_in_handoff_serial = handoff_serial;
  g_in_replay = replay;
  g_in_meta = meta_workers;
  g_inputs_given.store(true, std::memory_order_release);
}

std::string WorkersMeta() {
  std::call_once(g_decide_once, Decide);
  return doa4::np::lanes::MetaWorkers(g_mode.load(std::memory_order_relaxed));
}

void WorkersPreFrame(uint32_t frame, uint8_t* base) {
  if (!doa4::np::Active() || Mode() != 4) return;
  eng::PreFrame(frame, base);
}

void WorkerCreated(uint32_t start, uint32_t tid) {
  if (std::find(std::begin(kWorkerEntries), std::end(kWorkerEntries), start) == std::end(kWorkerEntries)) return;
  const bool lanes_mode = Mode() == 4;  // decided before any guest thread (CurrentMeta); read holding no lock (review M8)
  bool full = false;
  {
    std::lock_guard lk(g_mu);  // the check and the add under one hold
    if (lanes_mode && int(g_tids.size()) >= kWorkerCount) {
      full = true;  // a 26th worker thread: not added (design 6, the frame row)
    } else {
      g_tids.push_back(tid);  // creation order = rank
      if (int(g_tids.size()) == kWorkerCount) {
        LogLine("job workers: all 25 created at frame " + std::to_string(Frame()));
      }
    }
  }
  if (full) {  // the end request after the lock is left
    eng::RaiseEnd(doa4::np::lanes::EndCause::kFrame, fmt::format("a 26th worker thread (id {}, start {:08X})", tid, start));
  }
}

void WorkersFrame(const uint8_t* base) {
  const uint32_t frame = Frame();
  if (const int mode = Mode(); mode >= 3) eng::FrameStep(mode, frame, base);  // the cpu marks; mode 4's frame (Turns::FrameReset)
  if (frame % 600 == 599) {
    LogLine(fmt::format("job workers: main thread waited for them avg {:.1f} us max {:.1f} us a frame in the "
                        "update half, join avg {:.1f} us max {:.1f} us (600 frames)",
                        g_main_wait_us / 600, g_main_wait_max_us, g_join_us / 600, g_join_max_us));
    g_main_wait_us = g_main_wait_max_us = g_join_us = g_join_max_us = 0;
  }
  std::lock_guard lk(g_mu);
  g_token = 0;
}

void WorkersStop() {
  {
    std::lock_guard lk(g_mu);
    g_token = 0;
  }
  g_cv.notify_all();  // waiters see Stopped() and run free
  // mode 4's waiters too; then the last lanes:, vsm: and cpu: lines (Mode() answers 0 once stopped: the decided mode is read)
  eng::Stop(g_mode.load(std::memory_order_relaxed));
}

}  // namespace doa4::np::det

using namespace doa4::np::det;

REX_HOOK_RAW(sub_827921C0) {
  // Mode 1's lock is given back first, also once the session has ended: a worker that kept it through
  // the pass-through below would hold it for good while it waits for its next start event, and every
  // other worker blocked on it (DOA4_NP_SERIAL=1) would never run again.
  if (t_held) {
    t_held = false;
    g_serial_mu.unlock();
  }
  if (!doa4::np::Active()) {
    __imp__sub_827921C0(ctx, base);
    return;
  }
  rex::system::XThread* thread = rex::system::XThread::GetCurrentThread();
  const bool main = thread && thread->main_thread();
  const uint32_t tid = thread ? thread->thread_id() : 0;
  const uint32_t done = ctx.r3.u32, wait = ctx.r4.u32;  // the worker's done event and the event it waits for, before the original call
  {
    static std::mutex mu;
    static std::vector<uint32_t> seen;
    const uint32_t lr = uint32_t(ctx.lr);
    std::lock_guard lk(mu);
    if (std::find(seen.begin(), seen.end(), lr) == seen.end() && seen.size() < 64) {
      seen.push_back(lr);
      doa4::np::LogLine(fmt::format("SignalObjectAndWait caller lr {:08X} thread {} start {:08X}{} (frame {})",
                                    lr, tid, ThreadStart(), main ? " (main)" : "", Frame()));
    }
  }
  const int mode = main ? 0 : Mode();
  if (mode == 4) {
    // Mode 4 (design 5's table): at the first call the worker is placed by its creation rank; at a later call its section ends (its
    // handles must be the placed ones, and it must have been seen at its job if the frame had the mid event). After the original
    // returns, it waits for its turn, then its section begins.
    if (t_lane == -2) {
      t_lane = PlaceThis(tid, done, wait, base);
    } else if (t_lane >= 0) {
      eng::CpuSectionEnd();
      eng::WorkerEnd(t_lane, done, wait);
    }
    __imp__sub_827921C0(ctx, base);
    TraceRead(kRdWorker, tid);
    if (t_lane >= 0) {
      eng::WorkerWake(t_lane);
      eng::CpuSectionBegin();
    }
    return;
  }
  if (mode == 3) {  // P13: the placement observed in the serial order too; the cpu: line's mark at the token's hand-on
    if (!t_observed) {
      t_observed = true;
      PlaceThis(tid, done, wait, base);
    }
    if (t_rank >= 0) eng::CpuSectionEnd();
  }
  if (mode >= 2 && t_rank >= 0) {
    std::lock_guard lk(g_mu);
    if (g_token == t_rank) {
      ++g_token;  // end of this worker's work section: the next one may run
      g_cv.notify_all();
    }
  }
  __imp__sub_827921C0(ctx, base);
  TraceRead(kRdWorker, tid);
  if (mode == 1) {
    g_serial_mu.lock();
    t_held = true;
  } else if (mode >= 2) {
    std::unique_lock lk(g_mu);
    if (t_rank < 0) {
      const auto it = std::find(g_tids.begin(), g_tids.end(), tid);
      if (it != g_tids.end() && int(g_tids.size()) == kWorkerCount) {
        t_rank = int(it - g_tids.begin());
      } else {
        Count(kFbUnranked);
        if (g_unranked_logs++ < 10) {
          doa4::np::LogLine(fmt::format("job workers: thread {} (start {:08X}) woke without a rank ({} created)",
                                        tid, ThreadStart(), g_tids.size()));
        }
      }
    }
    if (t_rank >= 0) {
      const int rank = t_rank;
      if (!g_cv.wait_for(lk, std::chrono::milliseconds(2000), [rank] { return g_token == rank || Stopped(); })) {
        Count(kFbWorkerToken);
        if (g_timeouts++ < 20) {
          doa4::np::LogLine(fmt::format("job workers: frame {}: rank {} (thread {}) waited 2 s for the "
                                        "token at {}; running anyway",
                                        Frame(), rank, tid, g_token));
        }
        g_token = rank;
      }
    }
  }
  if (mode == 3 && t_rank >= 0) eng::CpuSectionBegin();  // the cpu: line's mark at the token's take
}

// sub_82792140 = SetEvent(r3). The main loop's update half sets the job pool's
// five start events (call site 0x824F48AC) and later its mid event (0x824F491C);
// in mode 3 the main thread then waits for the workers (MainWaitForWorkers).
REX_HOOK_RAW(sub_82792140) {
  const uint32_t handle = ctx.r3.u32;
  const uint32_t lr = uint32_t(ctx.lr);
  // Mode 4 only: the mid event is marked before the original call (design 5); every other mode keeps today's order below.
  const bool lanes_main = doa4::np::Active() && (lr == 0x824F48ACu || lr == 0x824F491Cu) && Mode() == 4 && IsMainThread();
  if (lanes_main && lr == 0x824F491Cu) eng::MainReachedMid();
  __imp__sub_82792140(ctx, base);
  if (lanes_main) {
    if (lr == 0x824F48ACu && handle == Be32(base, kPool + 36)) {
      AddMainWait(eng::MainWait(false));  // the first wait, after the fifth start event (rule 3)
    } else if (lr == 0x824F491Cu) {
      AddMainWait(eng::MainWait(true));  // the second wait (rule 4)
    }
    return;
  }
  if (!doa4::np::Active() || (lr != 0x824F48ACu && lr != 0x824F491Cu) || Mode() != 3 || !IsMainThread()) return;
  if (lr == 0x824F48ACu && handle == Be32(base, kPool + 36)) {
    MainWaitForWorkers(false);
  } else if (lr == 0x824F491Cu) {
    MainWaitForWorkers(true);
  }
}

// sub_82791190 = WaitForSingleObject(r3, timeout r4): a worker that waits for the
// pool's mid event (set by the main thread midway through its update half) is
// marked, so the main thread's first wait in mode 3 knows the workers have gone
// as far as they can before the mid event.
REX_HOOK_RAW(sub_82791190) {
  // Mode 4 (P10: the mid wait is marked here, no hook of its own): a placed worker's wait for the mid event; the test value stall=
  // sleeps after the mark, inside the worker's section, holding no lock.
  if (doa4::np::Active() && t_lane >= 0 && ctx.r3.u32 == Be32(base, kPool + 60) && Mode() == 4) {
    const int w = t_lane;
    eng::MidWaitBegin(w);
    __imp__sub_82791190(ctx, base);
    eng::MidWaitEnd(w);
    eng::StallTest(w);
    return;
  }
  if (!doa4::np::Active() || t_rank < 0 || ctx.r3.u32 != Be32(base, kPool + 60)) {
    __imp__sub_82791190(ctx, base);
    return;
  }
  {
    std::lock_guard lk(g_mu);
    g_mid_waiter = t_rank;
  }
  g_cv.notify_all();
  __imp__sub_82791190(ctx, base);
  std::lock_guard lk(g_mu);
  if (g_mid_waiter == t_rank) g_mid_waiter = -1;
}

// sub_827922E0 = WaitForMultipleObjects: the update/draw function's join on the
// five chains (call site 0x824F4AE8) is timed on the main thread, in every mode,
// so the cost of the ordered modes can be read beside the parallel one.
REX_HOOK_RAW(sub_827922E0) {
  if (!doa4::np::Active() || uint32_t(ctx.lr) != 0x824F4AE8u) {
    __imp__sub_827922E0(ctx, base);
    return;
  }
  const int mode = Mode();
  if (mode == 4) eng::MainJoinBegin();  // the join mark (rule 5, P19: without the mid event, an end request)
  const auto t0 = std::chrono::steady_clock::now();
  __imp__sub_827922E0(ctx, base);
  const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
  g_join_us += us;
  g_join_max_us = std::max(g_join_max_us, us);
  if (mode == 4) eng::MainJoinEnd();
  if (mode >= 3) eng::CpuMainWait(uint64_t(us));  // the join is part of the cpu: line's wait, in both orders
}

// sub_82732950: the vertex-stream manager's frame reset (one caller, the frame function at 824F4C38, return address 824F4C3C; address
// script np_runs/lp/b/b2/addr_b2.py). Mode 4: the manager (r3) and lr are read before the original call, the original runs, then the
// list pass (np_det_lanes.cpp VsmAfterReset: the stand-down rule first; the pass once the manager's host object exists). With no session
// and in every other mode: the original call only.
REX_HOOK_RAW(sub_82732950) {
  const uint32_t manager = ctx.r3.u32;
  const uint32_t lr = uint32_t(ctx.lr);
  __imp__sub_82732950(ctx, base);
  if (!doa4::np::Active() || Mode() != 4) return;
  eng::VsmAfterReset(ctx, base, manager, lr);
}
