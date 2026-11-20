// DOA4 issue #10 (Savannah Safari: one-frame slits through the distant buffalo): the stage-worker wait's bookkeeping, free of
// the guest and of the SDK so the host test (tests/host/np_stage_wait_host_test.cpp) can drive it.
//
// The defect (notes/research/issue10_safari_animals_2026-11-13.md, "Third session"): the ten stage job workers (entry points
// 82573A40 and 825689E8, five each) rebuild a stage's per-part instance buffers (Safari's herd: 19 parts) after the job pool's mid
// event, while the main thread carries on through the rest of its update half and moves the herd one step (the stage update,
// sub_82566920, called at 824F4974 or from 824F4E90's own states). Parts built after that step and parts built before it end up
// in one frame. The fix: before any main-thread call of the stage update between the mid event and the join, the main thread
// waits until every stage worker has come back to the top of its loop in this round. The other 15 workers keep running beside
// the main thread, and so do the main thread's calls between the mid event and the stage update.
//
// The round: the main thread's set of the pool's first start event opens it (RoundStart); each stage worker's arrival at its loop
// top (SignalObjectAndWait, before the original call signals its done event) counts once (Arrive); the pool's join waits on those
// done events, so a round's arrivals are all counted before the next RoundStart. The wait (MustWait) holds only once all ten stage
// workers have been seen (they are made while the game loads, before any fight), and it stands down for good if an eleventh stage
// worker thread appears (the counting would no longer mean "all done").

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace doa4::np::stage_wait {

constexpr uint32_t kStageEntryA = 0x82573A40u;  // stage slot +356's worker, five threads
constexpr uint32_t kStageEntryB = 0x825689E8u;  // stage slot +348's worker, five threads
constexpr int kStageWorkers = 10;

inline bool IsStageEntry(uint32_t start) { return start == kStageEntryA || start == kStageEntryB; }

class Rounds {
 public:
  // A stage worker thread's first arrival: registers its id. Returns false (and stands the wait down for good) for an eleventh.
  // Called once a thread (the caller keeps a thread_local "seen" flag).
  bool Register(uint32_t tid) {
    const int n = seen_.fetch_add(1, std::memory_order_acq_rel) + 1;
    (void)tid;
    if (n > kStageWorkers) {
      broken_.store(true, std::memory_order_release);
      return false;
    }
    return true;
  }

  // A stage worker at the top of its loop (its job, if any, is done). The tenth wakes a blocked waiter: the mutex is taken before
  // the notify, so a waiter that tested the count under the mutex and found nine is already waiting when the notify comes.
  void Arrive() {
    if (arrivals_.fetch_add(1, std::memory_order_acq_rel) + 1 == kStageWorkers) {
      std::lock_guard lk(mu_);
      cv_.notify_all();
    }
  }

  // The main thread's wait: polls up to `spin` (a short round ends without a sleep), then blocks until MustWait() is false or
  // `cap` has passed since the call. Returns false on the cap (the caller counts it and goes on: the wait stands down).
  bool WaitDone(std::chrono::microseconds spin, std::chrono::milliseconds cap) {
    const auto t0 = std::chrono::steady_clock::now();
    while (MustWait()) {
      if (std::chrono::steady_clock::now() - t0 >= spin) {
        std::unique_lock lk(mu_);
        return cv_.wait_until(lk, t0 + cap, [this] { return !MustWait(); });
      }
    }
    return true;
  }

  // The main thread opens a round. Returns the arrivals the round before counted (-1 for the first round seen), so the caller can
  // count rounds that did not see exactly ten (the measured invariant the wait relies on).
  int RoundStart() {
    const int prev = arrivals_.exchange(0, std::memory_order_acq_rel);
    const bool had = started_;
    started_ = true;
    return had ? prev : -1;
  }

  // True while the main thread must keep waiting: armed (all ten seen, none more, a round open) and not all ten back yet.
  bool MustWait() const {
    if (!Armed()) return false;
    return arrivals_.load(std::memory_order_acquire) < kStageWorkers;
  }

  bool Armed() const {
    return started_ && !broken_.load(std::memory_order_acquire) && seen_.load(std::memory_order_acquire) == kStageWorkers;
  }
  bool Broken() const { return broken_.load(std::memory_order_acquire); }
  int Seen() const { return seen_.load(std::memory_order_acquire); }
  int Arrivals() const { return arrivals_.load(std::memory_order_acquire); }

 private:
  std::atomic<int> seen_{0};
  std::atomic<int> arrivals_{0};
  std::atomic<bool> broken_{false};
  bool started_ = false;  // main thread only (RoundStart, MustWait, Armed, WaitDone)
  std::mutex mu_;
  std::condition_variable cv_;
};

}  // namespace doa4::np::stage_wait
