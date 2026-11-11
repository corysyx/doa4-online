// Idle waits instead of spin loops (issue #4, CPU heat; notes/specs/2026-11-11-doa4-cpu-spin-fix-design.md). The two guest
// loops that held a host core each:
// - the loader thread (sub_823FC5A0): a pass that finds no queued job now sleeps until the enqueue (sub_823FC650) wakes it
//   or kLoaderSleepLimit passes;
// - the main thread's D3D fence wait (sub_8275A0B8, one sub_82761AB8 call per poll pass): after kFenceSpinPasses passes a
//   pass sleeps until the GPU side writes guest memory (CommandProcessor::WaitForGuestWriteProgress) or kFenceSleepLimit.
// The guest code still makes every check it made; only the host time between checks changes.
// This header holds the policy and the counters (header-only, free of the SDK, for tests/host/idle_wait_host_test.cpp).

#pragma once

#include <chrono>
#include <cstdint>

namespace doa4::idle_wait {

inline constexpr uint32_t kFenceSpinPasses = 2000;                 // ~25-40 us of polling before the first sleep
inline constexpr std::chrono::microseconds kFenceSleepLimit{1000};  // a missed wake-up costs at most this
inline constexpr std::chrono::milliseconds kLoaderSleepLimit{5};    // likewise for the loader
inline constexpr uint32_t kFenceLogEvery = 600;                     // fence waits per counters line

// One fence wait (one call of sub_8275A0B8) on one thread.
class FenceWait {
 public:
  // Called once per poll pass (the guest's check failed). True when this pass should sleep.
  bool PassShouldSleep() { return ++passes_ > kFenceSpinPasses; }
  // A sleep ended: `moved` = the GPU side wrote guest memory (false: the time limit passed).
  void Slept(bool moved) {
    ++sleeps_;
    last_timed_out_ = !moved;
    if (!moved) ++timeouts_;
  }
  uint32_t sleeps() const { return sleeps_; }
  uint32_t timeouts() const { return timeouts_; }
  // The fence passed right after a sleep that ended by the time limit: the write that passed it woke nobody (a write
  // site without NoteGuestWrite, or a writer the SDK does not know).
  bool EndedAfterTimeout() const { return sleeps_ > 0 && last_timed_out_; }

 private:
  uint32_t passes_ = 0, sleeps_ = 0, timeouts_ = 0;
  bool last_timed_out_ = false;
};

// Totals over many fence waits, for one counters line every kFenceLogEvery waits.
struct FenceTotals {
  uint32_t waits = 0, waits_slept = 0, sleeps = 0, timeouts = 0, ended_after_timeout = 0;
  // Adds a finished wait; true when a line is due (then the caller logs and calls Reset).
  bool Add(const FenceWait& w) {
    ++waits;
    if (w.sleeps()) ++waits_slept;
    sleeps += w.sleeps();
    timeouts += w.timeouts();
    if (w.EndedAfterTimeout()) ++ended_after_timeout;
    return waits >= kFenceLogEvery;
  }
  void Reset() { *this = FenceTotals{}; }
};

// The loader's idle sleeps: a job found right after a sleep that ended by the time limit means the enqueue that queued
// it woke nobody (another writer of the ring than sub_823FC650).
class LoaderWake {
 public:
  // A sleep ended: `woken` = notified or the slot was seen queued (false: the time limit passed).
  void Slept(bool woken) { last_timed_out_ = !woken; }
  // A pass ran (`ran_job`). True when that job had waited for the time limit (a missed wake-up).
  bool Pass(bool ran_job) {
    const bool missed = ran_job && last_timed_out_;
    last_timed_out_ = false;  // one pass per sleep: the next pass is judged by its own sleep
    if (missed) ++missed_;
    return missed;
  }
  uint32_t missed() const { return missed_; }

 private:
  bool last_timed_out_ = false;
  uint32_t missed_ = 0;
};

}  // namespace doa4::idle_wait
