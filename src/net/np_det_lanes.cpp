// DOA4 Online lockstep port, task B2: the engine of worker mode 4 ("lanes"). See np_det_lanes.h.

#include "np_det_lanes.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xobject.h>

#include "generated/default/dead_or_alive_4_init.h"

#include "np_det_internal.h"
#include "np_digest.h"
#include "np_session.h"
#include "np_vsm_pure.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace doa4::np::det::eng {
namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kPool = 0x8341FAA0;  // the job pool (np_det_workers.cpp kPool): start events at +20 to +36

uint32_t Be32(const uint8_t* base, uint32_t a) {
  return uint32_t(base[a]) << 24 | uint32_t(base[a + 1]) << 16 | uint32_t(base[a + 2]) << 8 | base[a + 3];
}

std::string Hex8(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08X", v);
  return buf;
}

// Set once by Configure, before any guest thread runs (inside the decision's call_once, which every reader passed through first).
int g_mode = -1;
bool g_online = false;
lanes::LanesTest g_test;

// ---- under the engine lock ----
std::mutex g_mu;
std::condition_variable g_cv;  // every wait of mode 4; notified at a section's end, a mid wait, an end request and the stop
lanes::Placement g_place;
bool g_resolved = false;
lanes::Turns g_turns;
uint64_t g_frames = 0, g_frames_2plus = 0, g_stalls = 0;  // the session's lanes: counts, folded at each frame step
int g_most = 0, g_beside = 0, g_lane_beside = 0;
std::vector<int> g_began[lanes::kLanes];  // this frame's section starts per lane, in order (the stall-over line)
int64_t g_stall_over_frame = -1;          // the frame whose stall-over line was written
bool g_pre_first = true;                  // PreFrame has not run yet

lanes::EndRequest g_end;  // its own mutex
std::atomic<uint64_t> g_end_requests{0};

// ---- the section objects: PreFrame only (the game thread) ----
bool g_cs_tried[lanes::kSectionCount] = {};
lanes::SectionState g_cs[lanes::kSectionCount] = {lanes::SectionState::kRefused, lanes::SectionState::kRefused,
                                                   lanes::SectionState::kRefused, lanes::SectionState::kRefused};
std::string g_cs_why[lanes::kSectionCount];
std::atomic<bool> g_vsm_ready{false};  // the manager's host object exists: the pass may run

// ---- the pass ----
std::mutex g_vsm_mu;  // the counters below; taken alone, after the guest section is left
uint64_t g_resets = 0, g_sorted = 0, g_cleared = 0, g_stood_down = 0, g_anomalies = 0;
uint64_t g_content = vsm::kHashStart;
// The counts above as the last frame step found them (ledger L37). The vsm: line covers the run up to its last frame hook, not up to
// det::Stop: a record and its replay end at different hooks (a record quits at the quit frame's hook after det::OnFrame, so that frame's
// function runs with the layer off; a replay ends at the next hook, before det::OnFrame, so it runs one more reset), and only the frames
// up to the last hook are hashed in both. "stood down" stays live in the line, so a stand-down after the last hook still shows.
uint64_t g_at_hook_resets = 0, g_at_hook_sorted = 0, g_at_hook_cleared = 0, g_at_hook_anomalies = 0;
uint64_t g_at_hook_content = vsm::kHashStart;
uint64_t g_carry = vsm::kHashStart;  // the hash the next pass folds into: the game thread only (the stand-down guarantees it)

// ---- the cpu: line ----
std::mutex g_cpu_mu;  // the session's cpu counts
lanes::WaitHistogram g_wait;
uint64_t g_fight_frames = 0, g_cycles_main = 0, g_cycles_workers = 0;
// the frame in progress: the main thread's waits, the workers' section cycles, whether its hook read a fight
std::atomic<uint64_t> g_frame_wait_us{0};
std::atomic<uint64_t> g_frame_worker_cycles{0};
bool g_frame_open = false, g_frame_fight = false;  // the game thread only (FrameStep)
uint64_t g_main_cycles_at_hook = 0;
thread_local uint64_t t_cycles_begin = 0;
thread_local bool t_in_section = false;

std::atomic<bool> g_stall_done{false};

uint64_t ThreadCycles() {
  ULONG64 c = 0;
  QueryThreadCycleTime(GetCurrentThread(), &c);
  return uint64_t(c);
}

int LaneOf(int w) {
  for (int l = 0; l < lanes::kLanes; ++l) {
    for (int k = 0; k < lanes::kLaneLen[l]; ++k) {
      if (lanes::kLane[l][k] == w) return l;
    }
  }
  return -1;
}

std::string Who(int waiter) {
  return waiter == lanes::kMainFirstWait    ? std::string("main (first wait)")
         : waiter == lanes::kMainSecondWait ? std::string("main (second wait)")
                                            : "worker " + std::to_string(waiter);
}

// Waits on g_cv (lk held on entry and on return) until ready(), the stop or an end request, in 50 ms slices under its own stall clock
// (design 6, P18). Each slice that returns with ready() still false adds the steady clock's whole milliseconds since the last count; the
// reference moves only by what was handed to the clock, so the fraction is carried (m9). 2 s: one stall line and fb_lane_stall, and it
// keeps waiting; 8 s: an end request. Lines and the request are written with lk released. Returns whether it logged a stall; *ms is the
// wait's wall time.
template <class Ready, class Blocker>
bool WaitInOrder(std::unique_lock<std::mutex>& lk, int waiter, Ready ready, Blocker blocker, int64_t* ms) {
  lanes::StallClock clock;
  const Clock::time_point t0 = Clock::now();
  Clock::time_point ref = t0;
  bool stalled = false;
  while (!ready() && !Stopped() && !g_end.Requested()) {
    g_cv.wait_for(lk, std::chrono::milliseconds(lanes::StallClock::kSliceMs));
    if (ready() || Stopped() || g_end.Requested()) break;
    const int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - ref).count();
    ref += std::chrono::milliseconds(elapsed);
    const lanes::StallClock::Step step = clock.Waited(int(std::min<int64_t>(elapsed, 1 << 20)));
    if (step == lanes::StallClock::Step::kLogStall) {
      const int b = blocker();
      ++g_stalls;
      stalled = true;
      const std::string line = b >= 0 ? lanes::StallLine(int32_t(Frame()), waiter, b) : std::string();
      lk.unlock();
      Count(kFbLaneStall);
      if (!line.empty()) LogLine(line);
      lk.lock();
    } else if (step == lanes::StallClock::Step::kEnd) {
      const int b = blocker();
      const std::string text = Who(waiter) + " has waited 8 s for " + (b >= 0 ? "worker " + std::to_string(b) : std::string("its turn"));
      lk.unlock();
      RaiseEnd(lanes::EndCause::kStall, text);
      lk.lock();
      break;
    }
  }
  *ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
  return stalled;
}

// Under the engine lock: the stall-over line, once a frame, by the first stalled waiter whose wait ended in order.
std::string StallOverLocked(bool stalled, int64_t ms) {
  if (!stalled || g_end.Requested() || Stopped()) return {};
  const int64_t f = int64_t(Frame());
  if (g_stall_over_frame == f) return {};
  g_stall_over_frame = f;
  return lanes::StallOverLine(int32_t(f), int(std::min<int64_t>(ms, 1 << 30)), g_began[0], g_began[1]);
}

void StoodDown(const std::string& why) {
  {
    std::lock_guard lk(g_vsm_mu);
    ++g_stood_down;
  }
  Count(kFbVsmStandDown);
  LogLine(vsm::StoodDownLine(int32_t(Frame()), why));
  RaiseEnd(lanes::EndCause::kVsm, "the list pass stood down: " + why);
}

// The periodic and last lines, from the session's folded values (taken under each lock in turn, logged after).
std::vector<std::string> SessionLines(int mode) {
  std::vector<std::string> out;
  if (mode == 4) {
    {
      std::lock_guard lk(g_mu);
      out.push_back(lanes::LanesLine(g_frames, g_frames_2plus, g_most, g_beside, g_lane_beside, g_stalls, g_end_requests.load()));
    }
    std::lock_guard lk(g_vsm_mu);  // the copy of the last frame step, with "stood down" live (L37)
    out.push_back(vsm::Line(g_at_hook_resets, g_at_hook_sorted, g_at_hook_cleared, g_stood_down, g_at_hook_anomalies, g_at_hook_content));
  }
  if (mode == 3 || mode == 4) {
    std::lock_guard lk(g_cpu_mu);
    out.push_back(lanes::CpuLine(g_fight_frames, g_wait, g_cycles_main, g_cycles_workers));
  }
  return out;
}

void WakeAll() {
  { std::lock_guard lk(g_mu); }  // a waiter between its test and its wait holds the lock: it sees the change, or it is woken
  g_cv.notify_all();
}

}  // namespace

void Configure(int mode, bool online, const lanes::LanesTest& test) {
  g_mode = mode;
  g_online = online;
  g_test = test;
}

// ---- the placement ----
void Place(int rank, const char* why_not, uint32_t tid, uint32_t entry, uint32_t done, uint32_t wait, const uint8_t* base) {
  std::string line, end_text;
  lanes::EndCause end = lanes::EndCause::kNone;
  if (rank < 0) {
    if (g_mode == 4) {
      end = lanes::EndCause::kPlacement;
      end_text = "worker thread " + std::to_string(tid) + " (start " + Hex8(entry) + ") cannot be placed: " + (why_not ? why_not : "no rank");
    }
  } else {
    std::lock_guard lk(g_mu);
    g_place.Set(rank, entry, done, wait);
    if (!g_resolved && g_place.Complete()) {
      uint32_t start[lanes::kChains];
      for (int c = 0; c < lanes::kChains; ++c) start[c] = Be32(base, kPool + 20 + 4 * uint32_t(c));
      g_place.Resolve(start);
      g_resolved = true;
      line = lanes::PlacementLine(g_place);  // copied before the lock is left (Mismatch's text lives until the next call)
      const char* why = g_place.Mismatch();
      if (g_mode == 4 && why != nullptr) {
        end = lanes::EndCause::kPlacement;
        end_text = std::string("the placement is not the table (") + why + ")";
      } else if (g_mode == 4 && g_test.refuse_placement) {
        end = lanes::EndCause::kTest;
        end_text = "the placement is refused by the test value refuse=placement";
      }
    }
  }
  if (!line.empty()) LogLine(line);
  if (end != lanes::EndCause::kNone) RaiseEnd(end, end_text);
}

// ---- mode 4, a worker ----
void WorkerEnd(int w, uint32_t done, uint32_t wait) {
  std::string end_text;
  {
    std::lock_guard lk(g_mu);
    const bool same = g_place.Same(w, done, wait);
    const bool unseen = g_turns.End(w);
    if (!same) {
      end_text = "worker " + std::to_string(w) + ": its handles changed (done " + Hex8(done) + ", wait " + Hex8(wait) + ")";
    } else if (unseen) {
      end_text = "worker " + std::to_string(w) + ": a section ended unseen (no wait for the mid event in a frame that had it)";
    }
  }
  g_cv.notify_all();
  if (!end_text.empty()) RaiseEnd(lanes::EndCause::kFrame, end_text);
}

void WorkerWake(int w) {
  std::unique_lock lk(g_mu);
  if (!g_place.Complete()) {
    lk.unlock();
    RaiseEnd(lanes::EndCause::kPlacement, "worker " + std::to_string(w) + " woke with the placement incomplete");
    lk.lock();
  }
  int64_t ms = 0;
  const bool stalled = WaitInOrder(
      lk, w, [w] { return g_turns.MayRun(w); }, [w] { return g_turns.BlockerOf(w); }, &ms);
  g_turns.Begin(w);  // also after a wait that returned for the stop or an end request: the frame finishes without order
  if (const int l = LaneOf(w); l >= 0) g_began[l].push_back(w);
  const std::string over = StallOverLocked(stalled, ms);
  lk.unlock();
  if (!over.empty()) LogLine(over);
}

void MidWaitBegin(int w) {
  {
    std::lock_guard lk(g_mu);
    g_turns.MidWaitBegin(w);
  }
  g_cv.notify_all();  // the main thread's first wait
}

void MidWaitEnd(int w) {
  std::lock_guard lk(g_mu);
  g_turns.MidWaitEnd(w);
}

void StallTest(int w) {
  if (g_test.stall_worker != w || int64_t(Frame()) != int64_t(g_test.stall_frame)) return;
  if (g_stall_done.exchange(true)) return;
  std::this_thread::sleep_for(std::chrono::milliseconds(g_test.stall_ms));
}

// ---- mode 4, the main thread ----
void MainReachedMid() {
  std::lock_guard lk(g_mu);
  g_turns.MainReachedMid();
  // review M1: from the mid mark on, the main thread runs only hook code until its second wait, which marks it waiting again and
  // clears the mark at its end. The flag is read only by Turns::Note (the two counts beside main): it moves no turn and no wait.
  g_turns.MainWaiting(true);
}

uint64_t MainWait(bool second) {
  const Clock::time_point t0 = Clock::now();
  std::unique_lock lk(g_mu);
  if (!second && !g_place.Complete()) {
    lk.unlock();
    RaiseEnd(lanes::EndCause::kPlacement, "the main thread reached its first wait with the placement incomplete");
    lk.lock();
  }
  g_turns.MainWaiting(true);
  int64_t ms = 0;
  bool stalled;
  if (second) {
    stalled = WaitInOrder(
        lk, lanes::kMainSecondWait, [] { return g_turns.SecondWaitReady(); }, [] { return g_turns.SecondWaitBlocker(); }, &ms);
  } else {
    stalled = WaitInOrder(
        lk, lanes::kMainFirstWait, [] { return g_turns.FirstWaitReady(); }, [] { return g_turns.FirstWaitBlocker(); }, &ms);
  }
  g_turns.MainWaiting(false);
  const std::string over = StallOverLocked(stalled, ms);
  lk.unlock();
  if (!over.empty()) LogLine(over);
  const uint64_t us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count());
  CpuMainWait(us);
  return us;
}

void MainJoinBegin() {
  bool had_mid;
  {
    std::lock_guard lk(g_mu);
    had_mid = g_turns.MainReachedJoin();
    g_turns.MainWaiting(true);
  }
  if (!had_mid) RaiseEnd(lanes::EndCause::kFrame, "the join in a frame without the mid event");
}

void MainJoinEnd() {
  std::lock_guard lk(g_mu);
  g_turns.MainWaiting(false);
}

// ---- the frame ----
void PreFrame(uint32_t frame, uint8_t* base) {
  if (g_mode != 4 || Stopped()) return;
  std::string end_text;
  lanes::EndCause end = lanes::EndCause::kNone;
  {
    std::lock_guard lk(g_mu);
    const bool first = g_pre_first;
    g_pre_first = false;
    if (first && !g_place.Complete()) {
      end = lanes::EndCause::kPlacement;
      end_text = "fewer than 25 workers placed at the first frame hook";
    } else if (g_turns.AnyInSection()) {
      end = lanes::EndCause::kFrame;
      end_text = "a worker in a section at the frame hook (" + std::to_string(g_turns.Running()) + " in a section)";
    }
  }
  if (end != lanes::EndCause::kNone) {
    RaiseEnd(end, end_text, int64_t(frame));
    return;
  }
  // (3) the section objects (design 2.4): every worker is parked (2.4) and the engine lock is not held across the SDK call, which takes
  // its own global lock. The first call tries all four in kSections' order, later calls the refused ones; a line each time a state changes.
  auto* ks = rex::system::kernel_state();
  for (int i = 0; i < lanes::kSectionCount; ++i) {
    if (g_cs_tried[i] && (g_cs[i] == lanes::SectionState::kMade || g_cs[i] == lanes::SectionState::kAlready)) continue;
    const uint32_t a = lanes::kSections[i];
    uint8_t* p = base + a;
    int32_t lock = 0;
    std::memcpy(&lock, p + 0x10, sizeof lock);  // host order, as the SDK's X_RTL_CRITICAL_SECTION keeps it
    const lanes::CsDecision d = lanes::DecideCs(p[0], lock, Be32(p, 8), Be32(p, 12));
    std::string why = d.why;
    const char* after = nullptr;
    std::string after_text;
    if (d.action == lanes::CsAction::kMake) {
      // the contended enter's own call (xeKeWaitForSingleObject): the host address of the guest header, the type taken from it (-1);
      // the reference is dropped at once, the object table keeps the object (wd:np_rb0_cseager.cpp:61-63)
      const bool got = ks != nullptr && static_cast<bool>(rex::system::XObject::GetNativeObject(ks, p));
      after = ks == nullptr ? "no kernel state" : lanes::CsAfterMake(got, Be32(p, 8), Be32(p, 12));
      if (after != nullptr) {
        after_text = std::string(after) + " (+8 and +12 now " + Hex8(Be32(p, 8)) + " " + Hex8(Be32(p, 12)) + ")";
        why = after_text;
      }
    }
    const lanes::SectionState state = lanes::StateAfter(d, after == nullptr ? nullptr : after_text.c_str());
    const bool changed = !g_cs_tried[i] || state != g_cs[i];
    g_cs_tried[i] = true;
    g_cs[i] = state;
    g_cs_why[i] = why;
    if (changed) LogLine(lanes::SectionLine(a, state, Be32(p, 8), Be32(p, 12), why));
  }
  const lanes::SectionState mgr = g_cs[lanes::kSectionCount - 1];
  if (mgr == lanes::SectionState::kMade || mgr == lanes::SectionState::kAlready) g_vsm_ready.store(true, std::memory_order_release);
  // (4) the verdict, with the fight byte as the fight frame
  const bool fight = base[kFightState] == kFightRound;
  if (lanes::SectionsVerdict(g_cs, fight)) {
    int s = lanes::kSectionCount - 1;
    if (g_cs[s] != lanes::SectionState::kRefused) {
      for (s = 0; s < lanes::kSectionCount; ++s) {
        if (g_cs[s] != lanes::SectionState::kMade && g_cs[s] != lanes::SectionState::kAlready) break;
      }
    }
    if (s >= lanes::kSectionCount) s = lanes::kSectionCount - 1;
    RaiseEnd(lanes::EndCause::kSection, "section " + Hex8(lanes::kSections[s]) + " has no host object" +
                                            (fight ? " at a fight frame" : "") + " (frame " + std::to_string(frame) + "): " + g_cs_why[s],
             int64_t(frame));
  }
}

void FrameStep(int mode, uint32_t frame, const uint8_t* base) {
  // the cpu marks: the frame that ends here is folded if its own hook read a fight
  const uint64_t cycles = ThreadCycles();
  {
    std::lock_guard lk(g_cpu_mu);
    if (g_frame_open && g_frame_fight) {
      ++g_fight_frames;
      g_wait.Add(g_frame_wait_us.load());
      g_cycles_main += cycles - g_main_cycles_at_hook;
      g_cycles_workers += g_frame_worker_cycles.load();
    }
  }
  g_frame_wait_us.store(0);
  g_frame_worker_cycles.store(0);
  g_main_cycles_at_hook = cycles;
  g_frame_open = true;
  g_frame_fight = base[kFightState] == kFightRound;
  if (mode == 4) {
    std::lock_guard lk(g_vsm_mu);  // the vsm counts as this frame step finds them (L37; see g_at_hook_resets)
    g_at_hook_resets = g_resets;
    g_at_hook_sorted = g_sorted;
    g_at_hook_cleared = g_cleared;
    g_at_hook_anomalies = g_anomalies;
    g_at_hook_content = g_content;
  }
  if (mode == 4) {
    std::lock_guard lk(g_mu);
    const int most = g_turns.MaxAtOnce(), beside = g_turns.MaxBesideMain(), lane_beside = g_turns.MaxLaneBesideMain();
    ++g_frames;
    if (most >= 2) ++g_frames_2plus;
    g_most = std::max(g_most, most);
    g_beside = std::max(g_beside, beside);
    g_lane_beside = std::max(g_lane_beside, lane_beside);
    g_turns.FrameReset();
    for (auto& b : g_began) b.clear();
  }
  if (frame % 600 == 599) {
    for (const std::string& line : SessionLines(mode)) LogLine(line);
  }
}

void Stop(int mode) {
  WakeAll();  // every waiter sees Stopped() and begins at once
  for (const std::string& line : SessionLines(mode)) LogLine(line);
}

// ---- the pass (wd:np_rb0_vsm.cpp:76-103, with the stand-down an end request) ----
void VsmAfterReset(PPCContext& ctx, uint8_t* base, uint32_t manager, uint32_t lr) {
  int running;
  {
    std::lock_guard lk(g_mu);
    running = g_turns.Running();
  }
  if (const char* why = vsm::StandDown(true, manager, IsMainThread(), lr, running)) {
    StoodDown(why);
    return;
  }
  if (!g_vsm_ready.load(std::memory_order_acquire)) return;  // the pass starts once the manager's host object exists
  uint64_t hash = g_carry;
  const uint64_t r3 = ctx.r3.u64;
  ctx.r3.u64 = manager;
  __imp__RtlEnterCriticalSection(ctx, base);
  const uint32_t count0 = Be32(base, manager + vsm::kOffCount0), count1 = Be32(base, manager + vsm::kOffCount1);
  const vsm::PassResult r = vsm::Pass(base + manager + vsm::kOffLists, count0, count1, !g_test.vsm_hash, &hash);
  ctx.r3.u64 = manager;
  __imp__RtlLeaveCriticalSection(ctx, base);
  ctx.r3.u64 = r3;
  if (!r.ok) {
    StoodDown("a count above 192");
    return;
  }
  g_carry = hash;
  std::lock_guard lk(g_vsm_mu);
  ++g_resets;
  g_sorted += uint64_t(r.unsorted);
  g_cleared += uint64_t(r.cleared);
  g_anomalies += uint64_t(r.duplicates);
  g_content = hash;
}

// ---- the end request ----
void RaiseEnd(lanes::EndCause cause, const std::string& text, int64_t frame) {
  if (cause == lanes::EndCause::kNone) return;
  const bool first = g_end.Set(cause, text);
  g_end_requests.fetch_add(1);
  // review M2: PreFrame runs before det::OnFrame stores its frame, so it passes the frame it was called with; every other caller -1
  LogLine(lanes::EndRequestLine(frame >= 0 ? int32_t(frame) : int32_t(Frame()), text));
  if (first) {
    Count(kFbLaneEnd);
    if (!g_online) LogLine(lanes::EndRequestWarn(text));
  }
  WakeAll();
}

bool EndRequested(std::string* cause) {
  if (!g_end.Requested()) return false;
  if (cause != nullptr) *cause = g_end.Text();
  return true;
}

// ---- the cpu marks ----
void CpuSectionBegin() {
  t_cycles_begin = ThreadCycles();
  t_in_section = true;
}

void CpuSectionEnd() {
  if (!t_in_section) return;
  t_in_section = false;
  g_frame_worker_cycles.fetch_add(ThreadCycles() - t_cycles_begin);
}

void CpuMainWait(uint64_t us) { g_frame_wait_us.fetch_add(us); }

}  // namespace doa4::np::det::eng
