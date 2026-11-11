// Idle waits instead of spin loops (issue #4, CPU heat): the hooks. Policy and counters: doa4_idle_wait.h. Design:
// notes/specs/2026-11-11-doa4-cpu-spin-fix-design.md. The switch doa4_idle_waits (doa4_cvars.cpp) is read once, when the
// app attaches the GPU command processor (OnPostSetup, before the guest's first thread runs); off = both spin loops as before.
//
// The D3D fence wait sub_8275A0B8 (measured on the main thread; the hook applies on any thread that calls it): its poll
// loop (loc_8275A150) calls sub_82761AB8 once per pass and stops when that returns 0 or the GPU-written value passes the
// target. sub_82761AB8's own 2000 check reads a KTHREAD word the SDK never writes (review: not a clock and not a pass
// count), so fewer passes change nothing guest-visible. Scope (review M4): while the thread is inside a fence wait, a
// nested sub_82761AB8 poll (the ring-space wait sub_827598B0, reached through sub_82759FC0) sleeps the same way; it polls
// a word the GPU side writes back too, and anything not covered costs the limit per sleep. After kFenceSpinPasses passes,
// each pass sleeps in
// CommandProcessor::WaitForGuestWriteProgress until the GPU side writes guest memory or kFenceSleepLimit. The progress value
// a sleep compares with is read at the end of the previous pass (or at the wait's start), before the guest's next check, so
// a write between that check and the sleep ends the sleep at once.
//
// Loader thread: doa4_app_hooks.cpp runs the guest loop verbatim and calls doa4_loader_idle_wait on its idle branch; the
// enqueue sub_823FC650 wakes it.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>

#include "doa4_idle_wait.h"

extern "C" {
REX_EXTERN(__imp__sub_8275A0B8);
REX_EXTERN(__imp__sub_82761AB8);
REX_EXTERN(__imp__sub_823FC650);
}

namespace {
namespace iw = doa4::idle_wait;

std::atomic<bool> g_on{false};
std::atomic<rex::graphics::CommandProcessor*> g_cp{nullptr};

// ---- fence wait ----
thread_local iw::FenceWait* t_fence = nullptr;
thread_local uint32_t t_seen = 0;
std::mutex g_fence_totals_mu;
iw::FenceTotals g_fence_totals;
uint32_t g_fence_lines = 0;
std::atomic<uint64_t> g_limit_sleep_us_sum{0}, g_limit_sleep_us_max{0};  // limit sleeps since the last counters line

// ---- loader ----
constexpr uint32_t kLoaderIndex = 0x830ADB08;  // the ring's next index (u32); the ring's 16 entries follow, 12 bytes each
constexpr uint32_t kLoaderRing = 0x830ADB0C;   // state byte at +0: 0 free, 1 queued, 2 running, 3 done
std::mutex g_loader_mu;
std::condition_variable g_loader_cv;
uint64_t g_loader_kicks = 0;  // enqueues seen (under g_loader_mu)
iw::LoaderWake g_loader_wake;  // loader thread only

bool QueuedAtIndex(const uint8_t* base) {
  const uint32_t idx = rex::memory::load_and_swap<uint32_t>(base + kLoaderIndex);
  return idx < 16 && base[kLoaderRing + 12 * idx] == 1;
}
}  // namespace

// The app attaches the command processor once, after the runtime is ready and before the guest runs.
extern "C" void doa4_idle_waits_attach(rex::graphics::CommandProcessor* cp) {
  const bool on = cp && rex::cvar::Query<bool>("doa4_idle_waits");
  g_cp.store(cp);
  g_on.store(on);
  REXLOG_INFO("[doa4][CPU] idle waits: {}", on ? "on (loader and GPU fence waits sleep)" : "off (the game's spin loops)");
}

extern "C" bool doa4_idle_waits_on() { return g_on.load(std::memory_order_relaxed); }

REX_HOOK_RAW(sub_8275A0B8) {
  rex::graphics::CommandProcessor* cp = g_on.load(std::memory_order_relaxed) ? g_cp.load(std::memory_order_relaxed) : nullptr;
  if (!cp) {
    __imp__sub_8275A0B8(ctx, base);
    return;
  }
  iw::FenceWait wait;
  iw::FenceWait* const outer = t_fence;
  t_fence = &wait;
  t_seen = cp->guest_write_progress();  // before the guest's first check
  __imp__sub_8275A0B8(ctx, base);
  t_fence = outer;
  std::lock_guard lk(g_fence_totals_mu);
  if (g_fence_totals.Add(wait)) {
    const iw::FenceTotals& t = g_fence_totals;
    // The first line and every 100th: the counters; a fence passed right after a time-limit wake means a GPU-side write
    // woke nobody (a missing NoteGuestWrite site). Lines are every 600 waits (~5 s at 120 waits/s).
    const uint64_t limit_sum = g_limit_sleep_us_sum.exchange(0), limit_max = g_limit_sleep_us_max.exchange(0);
    if (g_fence_lines++ % 100 == 0 || t.ended_after_timeout) {
      REXLOG_INFO("[doa4][CPU] fence waits: {}, {} slept ({} sleeps, {} ended by the {} us limit: avg {} us, max {} us), "
                  "{} passed right after a limit wake",
                  t.waits, t.waits_slept, t.sleeps, t.timeouts, iw::kFenceSleepLimit.count(),
                  t.timeouts ? limit_sum / t.timeouts : 0, limit_max, t.ended_after_timeout);
    }
    g_fence_totals.Reset();
  }
}

REX_HOOK_RAW(sub_82761AB8) {
  __imp__sub_82761AB8(ctx, base);
  iw::FenceWait* const wait = t_fence;
  if (!wait) return;  // not inside a fence wait (another caller), or idle waits off
  rex::graphics::CommandProcessor* const cp = g_cp.load(std::memory_order_relaxed);
  if (ctx.r3.u32 != 0 && wait->PassShouldSleep()) {  // r3 == 0 ends the guest's loop: no sleep then
    const auto t0 = std::chrono::steady_clock::now();
    const bool moved = cp->WaitForGuestWriteProgress(t_seen, iw::kFenceSleepLimit);
    wait->Slept(moved);
    if (!moved) {  // the real length of a limit sleep: the limit is a timeout at the process's timer resolution (review M3)
      const uint64_t us = uint64_t(
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
      g_limit_sleep_us_sum.fetch_add(us, std::memory_order_relaxed);
      uint64_t prev = g_limit_sleep_us_max.load(std::memory_order_relaxed);
      while (us > prev && !g_limit_sleep_us_max.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
      }
    }
  }
  t_seen = cp->guest_write_progress();  // before the guest's next check
}

// The enqueue: under the loader's critical section it puts a job in a free slot with state 1 (r3 = the slot, or -1).
REX_HOOK_RAW(sub_823FC650) {
  __imp__sub_823FC650(ctx, base);
  if (!g_on.load(std::memory_order_relaxed)) return;
  {
    std::lock_guard lk(g_loader_mu);
    ++g_loader_kicks;
  }
  g_loader_cv.notify_all();
}

extern "C" void doa4_loader_idle_wait(uint8_t* base) {
  std::unique_lock lk(g_loader_mu);
  const uint64_t seen = g_loader_kicks;
  // The slot peek closes the window between the guest's check (under its critical section) and this lock: an enqueue in
  // it is seen here even though its kick was counted before `seen` was read.
  const bool woken = g_loader_cv.wait_for(lk, iw::kLoaderSleepLimit,
                                          [&] { return g_loader_kicks != seen || QueuedAtIndex(base); });
  g_loader_wake.Slept(woken);
}

// The missed-wake count can include a job enqueued just as the limit fired (its kick not counted yet when the sleep ended):
// a diagnostic count, never a decision (review M5).
extern "C" void doa4_loader_ran_job() {
  if (g_loader_wake.Pass(true)) {
    const uint32_t n = g_loader_wake.missed();
    if (n == 1 || n % 100 == 0) {
      REXLOG_INFO("[doa4][CPU] loader: a job waited for the {} ms limit ({} so far): a writer of the ring other than the "
                  "enqueue sub_823FC650",
                  iw::kLoaderSleepLimit.count(), n);
    }
  }
}
