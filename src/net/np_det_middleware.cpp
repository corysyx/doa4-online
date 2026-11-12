// DOA4 netplay phase 0: the determinism layer's middleware fixes (sound and
// movie middleware, the game's loader thread, the audio callbacks). Each is a
// thread whose progress the game polls; in a session that progress happens at
// points fixed by the game's own calls instead of at host-timed points.
//   audio   the guest audio callbacks run within a per-frame budget of
//           800 samples (3 or 4 callbacks of 256) granted at the frame hook,
//           from the first frame boundary after the guest registered its audio
//           client (Tekken 6 rex_audio_lockstep / rex_audio_advance;
//           rex_audio_registrations).
//   movie   movies are not played (sub_8242A168 returns at once, "finished"
//           answers yes): CRI Sofdec playback ran on the middleware's threads in
//           host time, so a movie took a different number of frames per run.
//   loader  the loader thread's jobs run only while the frame hook holds a gate
//           open, so a job queued in frame f is done when frame f + 1 starts.
//   cri     the CRI middleware's three server threads no longer run their server
//           passes; the main thread runs one pass of the three groups at every
//           frame boundary and whenever it spins on the tick count in a frame.
//   crijob  jobs posted to the middleware's two job threads run inline on the
//           posting thread.
//   notify  (the game's XNotifyGetNext polls, not middleware) the frame boundary makes every system
//           notification queued so far visible to the guest and counts the ones the guest dequeued since
//           the last boundary (pr_notify); the fix itself is in np_det.cpp / sdk-np0 xnotifylistener.cpp.

#include <xmmintrin.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>

#include "generated/default/dead_or_alive_4_init.h"

#include "../doa4_intro_skip.h"
#include "np_det_internal.h"
#include "np_det.h"
#include "np_session.h"

extern "C" {
REX_EXTERN(__imp__sub_8242A168);
REX_EXTERN(__imp__sub_8242A3A8);
REX_EXTERN(__imp__sub_8242A0F0);
REX_EXTERN(__imp__sub_8277EFC8);
REX_EXTERN(__imp__sub_829BC7F0);
REX_EXTERN(__imp__sub_829BF498);
REX_EXTERN(__imp__sub_829C9958);
REX_EXTERN(__imp__sub_8296DFE8);
REX_EXTERN(__imp__sub_824F1360);
REX_EXTERN(__imp__sub_824F0C60);
REX_EXTERN(__imp__sub_8275AAA8);
// SDK netplay exports (sdk-np0 src/audio/audio_system.cpp, src/system/xnotifylistener.cpp).
__declspec(dllimport) void rex_audio_lockstep(int on);
__declspec(dllimport) int rex_audio_advance(uint32_t n, int timeout_ms);
__declspec(dllimport) uint32_t rex_audio_registrations();
__declspec(dllimport) void rex_notify_frame();
__declspec(dllimport) uint32_t rex_notify_delivered();
}

namespace doa4::np::det {
namespace {

// ---- notify ----
uint32_t g_notify_seen = 0;  // notifications the guest had dequeued at the last boundary (rex_notify_delivered)

// ---- audio ----
// The game registers its audio client (sub_8296DFE8, hooked below, which logs "audio: client registered
// on <main|thread start> at frame F") on the MAIN guest thread: every gate run logged "on main at frame 0".
// So whether a client exists at a frame boundary is a function of the main thread's own call sequence,
// which lockstep keeps equal, and the callbacks start at the first boundary after the registration (before
// it the budget stays where rex_audio_lockstep(1) put it). The SDK worker runs a callback only within the
// budget, also when its first WaitAny was entered before the lockstep was armed (sdk-np0 audio_system.cpp).
uint32_t g_audio_late = 0;
bool g_audio_started = false;

// The game's slow motion (the KO camera, e.g. a KO into a danger zone): a frame normally lasts one vblank; the
// game asks for longer frames by writing N into the word 0x831A6364, and sub_824F1360 (hooked below, main thread)
// then waits on the vblank counter *(0x831A7AC0) so that the frame lasts N vblanks in all, and writes 0 back. In the
// scripted Single match the game asked for 3 at each KO: those frames took 3 vblanks (50 ms) each and the output ran
// dry, because the audio budget was 800 samples per frame.
// The budget follows the frame's length instead: 800 samples (1/60 s at 48 kHz) per vblank. The frame boundary
// grants the first vblank's 800 as before; the extra vblanks' share is granted inside the limiter only for its poll
// loop: at the loop's start read (sub_824F0C60 called from lr 0x824F13B8, after the limiter's D3D call
// sub_8275AA10) and waited for before its closing D3D call (sub_8275AAA8 from lr 0x824F141C). Between the two the
// main thread only reads the vblank counter (sub_824F0C60) and writes 0 to the request word, neither of which the
// audio callbacks touch, so the callbacks see the sound state the frame left, as at the boundary, and render at the
// output's real-time pace while the game waits. The word is game state (written by sub_824F1360 and sub_823B7DA0,
// values 3 or 0): both sides grant the same budget at the same points.
constexpr uint32_t kExtraVblanksWord = 0x831A6364;
constexpr uint32_t kLimiterLoopStartLr = 0x824F13B8;  // sub_824F0C60: the loop's start read
constexpr uint32_t kLimiterLoopEndLr = 0x824F141C;    // sub_8275AAA8: the D3D call after the loop
constexpr uint32_t kMaxExtraVblanks = 8;   // a larger request counts as this many (none seen; a guard)
uint32_t g_extra_vblanks = 0;              // extra vblanks granted in the current frame (main thread only; the log)
uint32_t g_limiter_pending = 0;            // extra vblanks to grant at the loop's start (main thread only)
bool g_limiter_granted = false;            // granted, not yet waited for (main thread only)
uint32_t g_slowmo_late = 0;                // log lines for the slow-motion wait (its own cap)

// The slow-motion schedule. The game's limiter waits N ticks of the vblank counter counted from a read it takes late
// in the frame, so a PC whose frame work runs past one tick gets N + 1 ticks a frame, every frame, with no catch-up
// (friend's PC, 2026-11-03: 66.7 ms slow-motion frames against 50 ms on the host, 10 frames -> the host waited
// ~170 ms after the slow motion and both outputs ran dry). In a session the limiter's poll loop instead ends at a
// schedule point: N/60 s after the previous slow-motion frame's point (catching up after a late frame), or after the
// frame's start for the first one. The loop's exit is host-timed either way and its counter reads stay in the
// limiter's stack frame, so both sides' game state is unchanged by this; what changes is that both PCs spend the
// same time in the slow motion. Requests of 1 (a plain one-vblank wait) keep the game's own loop.
using SteadyClock = std::chrono::steady_clock;
constexpr uint32_t kLimiterPollLr = 0x824F13CC;  // sub_824F0C60 inside the poll loop
constexpr double kVblankMs = 1000.0 / 60.0;
uint32_t g_limiter_n = 0;                   // the request read at the limiter's entry (main thread only)
bool g_limiter_sched = false;               // the poll loop in this call waits for g_sched_point
uint32_t g_limiter_start = 0;               // the counter value the loop's start read returned
SteadyClock::time_point g_sched_point{};    // the current slow-motion frame's end
uint32_t g_sched_frame = ~0u;               // the frame g_sched_point belongs to
SteadyClock::time_point g_frame_hook_time{};  // the last frame boundary (MiddlewareFrame)
double g_test_slow_extra_ms = -1;           // test switch DOA4_NP_TEST_SLOWMO_MS (< 0: off)
bool g_sched_off = false;                   // test switch DOA4_NP_SLOWMO_SCHED=0: the game's own loop
uint32_t g_sched_frames = 0;                // slow-motion frames the schedule ended (since the last log line)
double g_sched_worst_late_ms = 0;           // the latest a continued frame started after its point (same window)
// A request above this many vblanks ends after this many (none seen; the game writes 3): kMaxExtraVblanks + 1.
uint64_t g_audio_units = 0;  // 1/60 s units of audio granted so far (starts at the first step's frame number)
bool g_audio_units_init = false;
uint32_t g_audio_vblanks_last = 1, g_audio_vblank_logs = 0;
uint64_t g_audio_granted = 0;              // callbacks granted since the lockstep started (the SDK's real count)
std::atomic<uint64_t> g_audio_rewound{0};  // callbacks the snapshot restores took back (see SaveAudio)
struct AudioSaved {
  bool valid = false, units_init = false;
  uint64_t units = 0, granted = 0;
  uint32_t extra_vblanks = 0, vblanks_last = 1;
} g_audio_saved;

// The slow-motion grant's wait: every granted callback has run before the main thread goes on.
void LimiterWait() {
  g_limiter_granted = false;
  if (rex_audio_advance(0, 250)) return;
  Count(kFbAudioLate);
  if (g_slowmo_late++ < 20) LogLine(fmt::format("audio: frame {}: slow-motion callbacks not done after 250 ms", Frame()));
}

// Callbacks for `units` more 1/60 s of audio (the 800 * units / 256 rounding carried across calls).
uint32_t AudioUnitsToCallbacks(uint32_t units) {
  const uint32_t n = uint32_t((g_audio_units + units) * 800 / 256 - g_audio_units * 800 / 256);
  g_audio_units += units;
  g_audio_granted += n;
  return n;
}

void AudioStep(uint32_t f, const uint8_t* base) {
  const uint32_t vblanks = 1 + g_extra_vblanks;
  g_extra_vblanks = 0;  // the frame that ran is accounted for (also before the audio client exists)
  if (rex_audio_registrations() == 0) return;
  if (!g_audio_units_init) {
    g_audio_units_init = true;
    g_audio_units = f;  // with 1 vblank per frame the counts equal the former (f + 1) * 800 / 256 - f * 800 / 256
  }
  if (vblanks != g_audio_vblanks_last) {
    if (g_audio_vblank_logs++ < 200) {
      LogLine(fmt::format("audio: frame {}: the frame limiter waited {} vblank{} a frame (was {}): {} samples a frame",
                          f - 1, vblanks, vblanks == 1 ? "" : "s", g_audio_vblanks_last, 800 * vblanks));
      if (vblanks == 1) {  // a slow motion ended: what the schedule did in it
        LogLine(fmt::format("slow motion: the schedule ended {} frames{}, the latest start {:.1f} ms after its point",
                            g_sched_frames, g_sched_off ? " (schedule off)" : "", g_sched_worst_late_ms));
        g_sched_frames = 0;
        g_sched_worst_late_ms = 0;
      }
    }
    g_audio_vblanks_last = vblanks;
  }
  const uint32_t n = AudioUnitsToCallbacks(1);
  if (rex_audio_advance(n, 250)) {
    if (!g_audio_started) LogLine("audio: callbacks start at frame " + std::to_string(f));
    g_audio_started = true;
    return;
  }
  Count(g_audio_started ? kFbAudioLate : kFbAudioStart);
  if (g_audio_late++ < 20) LogLine("audio: frame " + std::to_string(f) + ": callbacks not done after 250 ms");
  g_audio_started = true;
}

// ---- loader ----
// The loader thread (entry 0x823FC5A0, started by the main loop's init
// sub_824F4560) busy-polls a 16-entry ring at 0x830ADB0C (12 bytes each, state
// byte at +0: 0 free, 1 queued, 2 running, 3 done; index 0x830ADB08, critical
// section 0x830ADBCC) and runs each job through sub_8277EFC8; the main thread
// checks for state 3 (sub_823FC700).
constexpr uint32_t kLoaderRing = 0x830ADB0C;
constexpr uint32_t kLoaderThreadStart = 0x823FC5A0;
std::mutex g_loader_mu;
std::condition_variable g_loader_cv;
bool g_loader_open = false;
int g_loader_busy = 0;
uint32_t g_loader_jobs = 0, g_loader_timeouts = 0, g_loader_outside = 0;
double g_loader_wait_ms = 0, g_loader_wait_max = 0;

bool LoaderPending(const uint8_t* base) {
  for (uint32_t i = 0; i < 16; ++i) {
    const uint8_t s = base[kLoaderRing + 12 * i];
    if (s == 1 || s == 2) return true;
  }
  return false;
}

void LoaderBoundary(uint8_t* base, uint32_t frame) {
  const auto t0 = std::chrono::steady_clock::now();
  std::unique_lock lk(g_loader_mu);
  g_loader_open = true;
  g_loader_cv.notify_all();
  bool waited = false;
  while (g_loader_busy > 0 || LoaderPending(base)) {
    waited = true;
    if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(5)) {
      Count(kFbLoaderIdle);
      if (g_loader_timeouts++ < 10) LogLine("loader: frame " + std::to_string(frame) + ": not idle after 5 s");
      break;
    }
    g_loader_cv.wait_for(lk, std::chrono::milliseconds(1));
  }
  g_loader_open = false;
  if (waited) {
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    g_loader_wait_ms += ms;
    g_loader_wait_max = std::max(g_loader_wait_max, ms);
  }
  if (frame % 600 == 599 && g_loader_jobs) {
    LogLine(fmt::format("loader: {} jobs in the last 600 frames, boundary wait {:.1f} ms (max {:.1f} ms)",
                        g_loader_jobs, g_loader_wait_ms, g_loader_wait_max));
    g_loader_jobs = 0;
    g_loader_wait_ms = g_loader_wait_max = 0;
  }
}

// ---- cri ----
// The sound/movie middleware runs three server threads. B (entry 0x829BC8F8)
// and C (0x829BC980) loop: sub_829BC7F0 sleeps to the next 1/60 s boundary of
// the performance counter, then they run their server group through
// sub_829BF498 (B group 2, C group 4); B then resumes A (0x829BC458), which runs
// group 6 and suspends itself. The passes ran on host time, so the sound and
// load state the game polls changed at different frames from run to run (a
// stop-and-wait loop, sub_82401F70, spun 750-886 times in frame 12; the boot's
// load loops 7-112 times). In a session A, B and C still loop but their passes
// do nothing; the main thread runs groups 2, 6, 4 at every frame boundary and
// once more for every repeated tick-count read from one call site in a frame
// (the game's wait loops read it for their timeouts), so each iteration of such
// a loop sees the state after one more pass. Every pass, at a boundary or in a spin step, starts only once
// the game has created thread B (the thread-creation hook) and B has reached its loop: the pass waits for
// B (WaitCriReady), so which passes run does not depend on how fast the host starts the thread.
constexpr uint32_t kCriThreadA = 0x829BC458, kCriThreadB = 0x829BC8F8, kCriThreadC = 0x829BC980;
std::atomic<bool> g_cri_ready{false};  // thread B reached its loop: the middleware is set up
std::atomic<bool> g_cri_created{false};  // the game created server thread B (thread-creation hook)
std::atomic<bool> g_cri_gave_up{false};  // B was not in its loop 2 s after the first wait: not waited for again
std::mutex g_cri_mu;
std::condition_variable g_cri_cv;
thread_local bool t_on_main_pass = false;
uint32_t g_spin_steps_frame = 0, g_spin_steps_window = 0, g_spin_steps_max = 0;
double g_spin_ms_frame = 0;    // audio diagnostics: time in the spin steps' CRI passes this frame
uint32_t g_spin_lr_frame = 0;  // audio diagnostics: the tick-read site of this frame's first spin step
std::vector<std::pair<uint32_t, uint32_t>> g_spin_lrs;  // main thread: tick-read LR -> reads this frame

// Called on the main thread before a pass. Before the game has created thread B nothing is waited for (the
// pass does nothing, as before); once it has, the pass waits until B is in its loop. After one timeout the
// fallback is counted once and later passes do not wait again (a spin loop can read the tick count thousands
// of times a frame: a B that never comes must end the run through the counter, not through 2 s per read).
void WaitCriReady(uint32_t frame) {
  if (!g_cri_created.load(std::memory_order_acquire) || g_cri_ready.load(std::memory_order_acquire) ||
      g_cri_gave_up.load(std::memory_order_relaxed)) {
    return;
  }
  std::unique_lock lk(g_cri_mu);
  if (!g_cri_cv.wait_for(lk, std::chrono::seconds(2), [] { return g_cri_ready.load() || Stopped(); })) {
    g_cri_gave_up.store(true, std::memory_order_relaxed);
    Count(kFbCriReady);
    LogLine("cri: frame " + std::to_string(frame) + ": server thread B not in its loop 2 s after it was created");
  }
}

void CriPass(PPCContext& ctx, uint8_t* base) {
  if (!g_cri_ready.load(std::memory_order_acquire)) return;
  Count(kPrCriPasses);
  const PPCContext saved = ctx;  // a call inside a hook: every register as it was
  const unsigned csr = _mm_getcsr();
  t_on_main_pass = true;
  for (uint32_t group : {2u, 6u, 4u}) {
    ctx.r3.u64 = group;
    __imp__sub_829BF498(ctx, base);
  }
  t_on_main_pass = false;
  ctx = saved;
  _mm_setcsr(csr);  // the cached flush mode in ctx must match the hardware again
}

// Hunt instrument (DOA4_NP_WATCH=<hex address>,<hex length>): logs which step
// of the frame boundary changes the watched bytes (none should: the boundary
// runs no game code except the middleware passes).
struct Watch {
  uint32_t lo = 0, len = 0;
  std::vector<uint8_t> copy;
  uint32_t logged = 0;
  void Init() {
    const char* e = TestSwitch("DOA4_NP_WATCH");
    if (!e) return;
    lo = uint32_t(std::strtoul(e, nullptr, 16));
    const char* comma = std::strchr(e, ',');
    len = comma ? uint32_t(std::strtoul(comma + 1, nullptr, 16)) : 0;
  }
  void Check(uint8_t* base, uint32_t frame, const char* step) {
    if (!len) return;
    const uint8_t* p = base + lo;
    if (step && !copy.empty() && std::memcmp(copy.data(), p, len) != 0 && logged < 40) {
      ++logged;
      uint32_t at = 0;
      while (at < len && copy[at] == p[at]) ++at;
      LogLine(fmt::format("watch: {:08X} changed {} (frame {}, first byte +{:X})", lo, step, frame, at));
    }
    copy.assign(p, p + len);
  }
};
Watch g_watch;

}  // namespace

void MiddlewareThreadCreated(uint32_t start) {
  if (start == kCriThreadB) g_cri_created.store(true, std::memory_order_release);
}

void MiddlewareStart() {
  g_watch.Init();
  // The audio budget's latches start afresh with the session (host memory, outside the digest).
  g_audio_units_init = false;
  g_audio_units = 0;
  g_audio_granted = 0;
  g_audio_rewound = 0;
  g_audio_saved = AudioSaved{};
  g_extra_vblanks = g_limiter_pending = 0;
  g_limiter_granted = false;
  g_audio_vblanks_last = 1;
  g_limiter_n = 0;
  g_limiter_sched = false;
  g_sched_frame = ~0u;
  g_sched_frames = 0;
  g_sched_worst_late_ms = 0;
  const char* slow = TestSwitch("DOA4_NP_TEST_SLOWMO_MS");
  g_test_slow_extra_ms = slow ? std::strtod(slow, nullptr) : -1;
  const char* sched = TestSwitch("DOA4_NP_SLOWMO_SCHED");
  g_sched_off = sched && sched[0] == '0';
  if (g_test_slow_extra_ms > 0 || g_sched_off) {
    LogLine(fmt::format("slow motion: test switches: extra frame work {:.1f} ms, schedule {}", g_test_slow_extra_ms,
                        g_sched_off ? "off (the game's own loop)" : "on"));
  }
  if (On(kAudio)) rex_audio_lockstep(1);
}

uint64_t AudioRewound() { return g_audio_rewound.load(std::memory_order_acquire); }

void SaveAudio() {
  g_audio_saved.valid = true;
  g_audio_saved.units_init = g_audio_units_init;
  g_audio_saved.units = g_audio_units;
  g_audio_saved.granted = g_audio_granted;
  g_audio_saved.extra_vblanks = g_extra_vblanks;
  g_audio_saved.vblanks_last = g_audio_vblanks_last;
}

int64_t RestoreAudio() {
  if (!g_audio_saved.valid) return -1;
  // Every granted callback has run at a frame boundary (AudioStep waited for them), so the SDK's count equals
  // g_audio_granted here; the callbacks since the save are replayed with the times they had the first time.
  const uint64_t back = g_audio_granted - g_audio_saved.granted;
  g_audio_rewound.fetch_add(back, std::memory_order_acq_rel);
  g_audio_units_init = g_audio_saved.units_init;
  g_audio_units = g_audio_saved.units;
  g_extra_vblanks = g_audio_saved.extra_vblanks;
  g_audio_vblanks_last = g_audio_saved.vblanks_last;
  return int64_t(back);
}

// The loader gate's waiter (the sub_8277EFC8 hook, g_loader_cv.wait_for with the predicate
// g_loader_open || Stopped()) is woken here. Taking the mutex first closes the window in which a job
// has tested the predicate (false) but not yet blocked: it holds g_loader_mu until it blocks, so the
// notify below cannot be lost.
void MiddlewareStop() {
  { std::lock_guard lk(g_loader_mu); }
  g_loader_cv.notify_all();
  // Same for the frame boundary's wait for CRI thread B (its predicate also ends on Stopped()).
  { std::lock_guard lk(g_cri_mu); }
  g_cri_cv.notify_all();
}

// Audio diagnostics: the boundary's step times (game thread only, read by the session's frame hook after det::OnFrame).
BoundaryTimes g_boundary_times;
BoundaryTimes LastBoundaryTimes() { return g_boundary_times; }

void MiddlewareFrame(uint32_t frame, PPCContext& ctx, uint8_t* base) {
  using Clk = std::chrono::steady_clock;
  const auto ms_since = [](Clk::time_point t) { return std::chrono::duration<double, std::milli>(Clk::now() - t).count(); };
  g_boundary_times = BoundaryTimes{};
  g_boundary_times.spin_steps = g_spin_steps_frame;  // spin steps (CRI passes) inside the game's frame that just ran
  g_boundary_times.spin_ms = g_spin_ms_frame;
  g_boundary_times.spin_lr = g_spin_lr_frame;
  g_spin_ms_frame = 0;
  g_spin_lr_frame = 0;
  g_frame_hook_time = SteadyClock::now();
  if (On(kNotify)) {
    const uint32_t delivered = rex_notify_delivered();  // dequeued by the guest up to this boundary
    Count(kPrNotify, delivered - g_notify_seen);
    g_notify_seen = delivered;
    rex_notify_frame();
  }
  g_watch.Check(base, frame, nullptr);  // the frame's own changes: not reported
  auto t = Clk::now();
  if (On(kLoader)) LoaderBoundary(base, frame);
  g_boundary_times.loader_ms = ms_since(t);
  g_watch.Check(base, frame, "in the loader boundary");
  g_spin_lrs.clear();
  if (On(kCri)) {
    if (frame % 600 == 599 && g_spin_steps_window) {
      LogLine(fmt::format("cri: {} spin steps in the last 600 frames (most in one frame {})",
                          g_spin_steps_window, g_spin_steps_max));
      g_spin_steps_window = g_spin_steps_max = 0;
    }
    g_spin_steps_frame = 0;
    WaitCriReady(frame);  // thread B was created by the game at a fixed point: the first pass waits for it
    t = Clk::now();
    CriPass(ctx, base);
    g_boundary_times.cri_ms = ms_since(t);
  }
  g_watch.Check(base, frame, "in the CRI pass");
  t = Clk::now();
  if (On(kAudio)) AudioStep(frame, base);
  g_boundary_times.audio_ms = ms_since(t);
  g_watch.Check(base, frame, "in the audio step");
}

// The trigger (a second tick-count read from one call site on the main thread in a frame) is a function
// of the main thread's own call sequence. With the same files on both PCs lockstep keeps that sequence
// equal: the passes it adds are counted (pr_spin_steps), compared every frame, and any difference is a
// determinism signal. With suits on one PC only it is not equal everywhere: the count splits both ways, by
// up to 2 in the measured runs, with the checked regions equal; np_compare --mixed-session fails past that.
void SpinRead(PPCContext& ctx, uint8_t* base, uint32_t lr) {
  if (!On(kCri) || t_on_main_pass) return;  // a tick read inside a pass is not a wait loop of the frame
  for (auto& [site, n] : g_spin_lrs) {
    if (site == lr) {
      ++n;
      if (g_spin_steps_frame < 20000) {
        Count(kPrSpinSteps);
        ++g_spin_steps_frame;
        ++g_spin_steps_window;
        g_spin_steps_max = std::max(g_spin_steps_max, g_spin_steps_frame);
        WaitCriReady(Frame());  // a spin step before the first boundary waits for thread B like the boundary does
        const auto ts = std::chrono::steady_clock::now();
        CriPass(ctx, base);
        g_spin_ms_frame += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts).count();
        if (!g_spin_lr_frame) g_spin_lr_frame = lr;
      } else {
        Count(kFbSpinCap);
      }
      return;
    }
  }
  g_spin_lrs.emplace_back(lr, 1);
}

}  // namespace doa4::np::det

using namespace doa4::np::det;

// ---- movie ----
// sub_8242A168(r3 = movie index) starts a CRI Sofdec movie, sub_8242A3A8 says
// whether it has finished (CRI status 0/3/4) and sub_8242A0F0 stops it. Movie 18
// (ninja_vi_hd.sfd) is the boot's opening (offline: guest swaps 27-1242, then the title).
namespace {
bool MoviesSkipped() { return doa4::np::Active() && On(kMovie); }

// The movie log (Task 14, offline and in a session, game log only): one [doa4][INTRO] line per start, per first "finished"
// answer and per stop, with the movie's index and file, two clocks (guest swaps and frame-function calls) and the caller.
// Capped, for a long play session (the boot starts one movie; the title's attract sequence started none in 240 s).
constexpr uint32_t kMovieNames = 0x82F69FE0;  // sub_8242A168 -> sub_8242A2C8(r3 = 0x82F69FE0 + 40 * index): 40-byte names
constexpr int kMovieLineCap = 64;
std::atomic<int> g_movie_lines{0};
std::atomic<uint32_t> g_movie_index{~0u};  // the last started (or skipped) movie
std::atomic<uint32_t> g_movie_polls{0};    // "finished?" calls since that start
std::atomic<bool> g_movie_finish_logged{false};

std::string MovieName(const uint8_t* base, uint32_t index) {
  if (index >= 64) return "?";
  const uint8_t* p = base + kMovieNames + 40 * index;
  std::string name;
  for (int i = 0; i < 40 && p[i]; ++i) {
    if (p[i] < 0x20 || p[i] > 0x7E) return "?";
    name += char(p[i]);
  }
  return name.empty() ? "?" : name;
}

std::string MovieWhere(uint32_t lr) {
  return fmt::format("at swap {} frame {} (lr {:08X}, {})", doa4::intro_skip::Swaps(), doa4::intro_skip::Frames(), lr,
                     IsMainThread() ? std::string("main thread") : fmt::format("thread start {:08X}", ThreadStart()));
}

void MovieLine(const std::string& text) {
  const int n = g_movie_lines.fetch_add(1, std::memory_order_relaxed);
  if (n < kMovieLineCap) REXLOG_INFO("[doa4][INTRO] {}", text);
  if (n == kMovieLineCap - 1) REXLOG_INFO("[doa4][INTRO] (further movie lines are not logged)");
}

void NoteMovieStart(const uint8_t* base, uint32_t index, uint32_t lr, const char* what) {
  g_movie_index.store(index, std::memory_order_relaxed);
  g_movie_polls.store(0, std::memory_order_relaxed);
  g_movie_finish_logged.store(false, std::memory_order_relaxed);
  MovieLine(fmt::format("movie start {} ({}) {}: {}", index, MovieName(base, index), MovieWhere(lr), what));
}
}  // namespace

// The frame limiter's slow-motion wait (see AudioStep): note the extra vblanks this frame asks for, then run it.
REX_HOOK_RAW(sub_824F1360) {
  uint32_t extra = 0;
  if (On(kAudio) && IsMainThread() && g_audio_units_init) {  // the audio client exists (the boundary started it)
    // A request of N makes the frame last N vblanks in all (measured: N = 3 -> the vblank counter rose by 3 a frame
    // without this fix), so N - 1 come on top of the boundary's one.
    const uint32_t n = rex::memory::load_and_swap<uint32_t>(base + kExtraVblanksWord);
    extra = std::min(n > 1 ? n - 1 : 0u, kMaxExtraVblanks - std::min(g_extra_vblanks, kMaxExtraVblanks));
  }
  g_limiter_pending = extra;  // granted at the poll loop's start read (sub_824F0C60 hook)
  g_limiter_n = 0;
  if (On(kAudio) && IsMainThread()) g_limiter_n = rex::memory::load_and_swap<uint32_t>(base + kExtraVblanksWord);
  __imp__sub_824F1360(ctx, base);
  g_limiter_pending = 0;
  g_limiter_sched = false;
  if (g_limiter_granted) LimiterWait();  // a guard: the closing D3D call's hook normally waited already
}

// The limiter's poll loop: at its start read, grant the pending slow-motion share (grant only; the worker renders
// it while the loop waits) and set the schedule point; its poll reads then report the request as reached exactly at
// that point. Every other call of sub_824F0C60 (a plain read of the vblank counter) passes straight through.
REX_HOOK_RAW(sub_824F0C60) {
  const uint32_t lr = uint32_t(ctx.lr);
  if (lr == kLimiterLoopStartLr && g_limiter_n >= 2 && IsMainThread()) {
    if (g_test_slow_extra_ms > 0) {  // test switch: this PC's slow-motion frame work runs longer
      std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(g_test_slow_extra_ms));
    }
    if (g_limiter_pending) {
      g_extra_vblanks += g_limiter_pending;
      rex_audio_advance(AudioUnitsToCallbacks(g_limiter_pending), 0);
      g_limiter_pending = 0;
      g_limiter_granted = true;
    }
    __imp__sub_824F0C60(ctx, base);
    if (!g_sched_off) {
      const uint32_t f = Frame();
      const auto step = std::chrono::duration_cast<SteadyClock::duration>(
          std::chrono::duration<double, std::milli>(kVblankMs * std::min(g_limiter_n, kMaxExtraVblanks + 1)));
      // The first slow-motion frame (or a resumed one) counts from its own start; later ones from the previous point,
      // so a late frame is made up by the next ones, but by at most one step (a long stall in the slow motion, e.g. a
      // wait for the other side, is not run off as a burst of unpaced frames).
      const bool continued = g_sched_frame != ~0u && f == g_sched_frame + 1;
      const auto now = SteadyClock::now();
      if (continued && now > g_sched_point) {
        g_sched_worst_late_ms = std::max(
            g_sched_worst_late_ms, std::chrono::duration<double, std::milli>(now - g_sched_point).count());
      }
      g_sched_point = continued ? std::max(g_sched_point, now - step) + step : g_frame_hook_time + step;
      ++g_sched_frames;
      g_sched_frame = f;
      g_limiter_start = ctx.r3.u32;
      g_limiter_sched = true;
    }
    return;
  }
  if (lr == kLimiterPollLr && g_limiter_sched && IsMainThread()) {
    // The loop compares the read against start + the request word, which it re-reads every pass: from the schedule
    // point on, report a value that reaches the word as it is NOW (so a request raised mid-loop cannot hold it).
    if (SteadyClock::now() >= g_sched_point) {
      const uint32_t word = rex::memory::load_and_swap<uint32_t>(base + kExtraVblanksWord);
      ctx.r3.u64 = uint64_t(uint32_t(g_limiter_start + std::max(word, g_limiter_n)));
    } else {
      ctx.r3.u64 = g_limiter_start;
    }
    return;
  }
  __imp__sub_824F0C60(ctx, base);
}

// The limiter's closing D3D call: the granted callbacks have run before it (and before the game goes on).
REX_HOOK_RAW(sub_8275AAA8) {
  if (uint32_t(ctx.lr) == kLimiterLoopEndLr && IsMainThread()) {
    g_limiter_sched = false;
    if (g_limiter_granted) LimiterWait();
  }
  __imp__sub_8275AAA8(ctx, base);
}

// The intro skip (doa4_intro_skip.h) ORs with the session's gate: with doa4_skip_intro off, or in a session, every call does
// what it did before; on, a movie started before the first title logo draw is skipped the way a session skips it.
REX_HOOK_RAW(sub_8242A168) {
  const uint32_t index = ctx.r3.u32, lr = uint32_t(ctx.lr);
  const bool session_skips = MoviesSkipped();
  if (doa4::intro_skip::Movies().SkipStart(doa4::np::Active(), session_skips, doa4::intro_skip::TitleSeen())) {
    if (session_skips) doa4::np::LogLine(fmt::format("movie {} skipped at frame {} (lr {:08X})", index, Frame(), lr));
    NoteMovieStart(base, index, lr, session_skips ? "skipped (session)" : "skipped (doa4_skip_intro, before the title)");
    return;
  }
  NoteMovieStart(base, index, lr, "plays");
  __imp__sub_8242A168(ctx, base);
}

// A skipped movie (in a session every movie) is "finished" at once, and "stop" has nothing to stop.
REX_HOOK_RAW(sub_8242A3A8) {
  const uint32_t lr = uint32_t(ctx.lr);
  const uint32_t polls = g_movie_polls.fetch_add(1, std::memory_order_relaxed) + 1;
  const bool session_skips = MoviesSkipped();
  const bool answered = doa4::intro_skip::Movies().AnswerFinished(session_skips);
  if (answered) {
    ctx.r3.u64 = 1;
  } else {
    __imp__sub_8242A3A8(ctx, base);
  }
  if (ctx.r3.u32 != 0 && !g_movie_finish_logged.exchange(true, std::memory_order_relaxed)) {
    const uint32_t index = g_movie_index.load(std::memory_order_relaxed);
    MovieLine(fmt::format("movie finished {} ({}) {}: {}, poll {}", index, MovieName(base, index), MovieWhere(lr),
                          !answered ? "the game's answer" : session_skips ? "answered (session)" : "answered (doa4_skip_intro)",
                          polls));
  }
}

REX_HOOK_RAW(sub_8242A0F0) {
  const uint32_t lr = uint32_t(ctx.lr), index = g_movie_index.load(std::memory_order_relaxed);
  const bool session_skips = MoviesSkipped();
  const bool nothing = doa4::intro_skip::Movies().SkipStop(session_skips);
  MovieLine(fmt::format("movie stop {} ({}) {}: {}", index, MovieName(base, index), MovieWhere(lr),
                        !nothing ? "stopped" : session_skips ? "nothing to stop (session)" : "nothing to stop (doa4_skip_intro)"));
  if (nothing) return;
  __imp__sub_8242A0F0(ctx, base);
}

// ---- audio ----
// sub_8296DFE8(this r3, render callback r4): the game's audio client setup (reached through a vtable, no
// direct callers); it unregisters the old client and registers the new one with
// XAudioRegisterRenderDriverClient. In a session the line says which thread registered the client and in
// which frame, because AudioStep's start frame is only a function of the game if that thread is ordered.
REX_HOOK_RAW(sub_8296DFE8) {
  if (!doa4::np::Active()) {
    __imp__sub_8296DFE8(ctx, base);
    return;
  }
  const uint32_t before = rex_audio_registrations();
  __imp__sub_8296DFE8(ctx, base);
  if (rex_audio_registrations() != before) {
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 8) {
      doa4::np::LogLine(
          IsMainThread() ? fmt::format("audio: client registered on main at frame {}", Frame())
                         : fmt::format("audio: client registered on thread start {:08X} at frame {}", ThreadStart(),
                                       Frame()));
    }
  }
}

// ---- loader ----
// sub_8277EFC8(r3, r4): the loader's job (also called directly by other code,
// which is never gated).
REX_HOOK_RAW(sub_8277EFC8) {
  if (!doa4::np::Active() || !On(kLoader) || ThreadStart() != kLoaderThreadStart) {
    __imp__sub_8277EFC8(ctx, base);
    return;
  }
  {
    std::unique_lock lk(g_loader_mu);
    if (!g_loader_cv.wait_for(lk, std::chrono::milliseconds(500), [] { return g_loader_open || Stopped(); })) {
      // The main thread did not reach a boundary in time (it may be waiting for
      // this job inside a frame): run anyway, and say so.
      Count(kFbLoaderOutside);
      if (g_loader_outside++ < 10) {
        doa4::np::LogLine("loader: a job ran outside the frame boundary (frame " + std::to_string(Frame()) + ")");
      }
    }
    ++g_loader_busy;
    ++g_loader_jobs;
    Count(kPrLoaderJobs);
  }
  __imp__sub_8277EFC8(ctx, base);
  {
    std::lock_guard lk(g_loader_mu);
    --g_loader_busy;
  }
  g_loader_cv.notify_all();
}

// ---- cri ----
// sub_829BC7F0: the server threads' sleep to the next 1/60 s; marks the
// middleware ready (thread B is in its loop).
REX_HOOK_RAW(sub_829BC7F0) {
  if (doa4::np::Active() && On(kCri) && ThreadStart() == kCriThreadB && !g_cri_ready.exchange(true)) {
    doa4::np::LogLine("cri: server threads running at frame " + std::to_string(Frame()));
    { std::lock_guard lk(g_cri_mu); }
    g_cri_cv.notify_all();
  }
  __imp__sub_829BC7F0(ctx, base);
}

// sub_829BF498(group): one pass of a CRI server group. In a session the server
// threads' own calls do nothing; the main thread runs the passes (CriPass).
REX_HOOK_RAW(sub_829BF498) {
  if (doa4::np::Active() && On(kCri) && !t_on_main_pass) {
    const uint32_t start = ThreadStart();
    if (start == kCriThreadA || start == kCriThreadB || start == kCriThreadC) {
      ctx.r3.u64 = 0;
      return;
    }
  }
  __imp__sub_829BF498(ctx, base);
}

// ---- crijob ----
// sub_829C9958(job thread object r3, function r4, argument r5): posts a job to
// one of the middleware's two job threads (entry 0x829C96B0, event-woken): if
// the thread is idle it stores the job, marks it busy (+4), bumps the job
// counter (+60) and signals the thread, which runs fn(arg) and clears busy.
// Loads waited on those jobs at host-timed points. In a session the job runs
// inline on the posting thread before the post returns (busy is never set), so
// everyone who polls the object sees it finished at a point fixed by the game.
REX_HOOK_RAW(sub_829C9958) {
  const uint32_t obj = ctx.r3.u32;
  if (!doa4::np::Active() || !On(kCriJob) || !obj ||
      rex::memory::load_and_swap<uint32_t>(base + obj + 0) == 0 ||
      rex::memory::load_and_swap<uint32_t>(base + obj + 4) != 0) {
    __imp__sub_829C9958(ctx, base);  // error paths and a busy thread: as the game does
    return;
  }
  const uint32_t fn = ctx.r4.u32, arg = ctx.r5.u32;
  uint32_t id = rex::memory::load_and_swap<uint32_t>(base + obj + 60) + 1;
  if (id == 0x7FFFFFFFu) id = 1;
  rex::memory::store_and_swap<uint32_t>(base + obj + 60, id);
  if (fn) {
    const PPCContext saved = ctx;
    const unsigned csr = _mm_getcsr();
    ctx.r3.u64 = arg;
    REX_CALL_INDIRECT_FUNC(fn);
    ctx = saved;
    _mm_setcsr(csr);
  }
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1) < 4) {
    doa4::np::LogLine(fmt::format("crijob: job {:08X}({:08X}) of {:08X} ran inline (frame {})", fn, arg, obj,
                                  Frame()));
  }
  Count(kPrCriJobs);
  ctx.r3.u64 = id;
}
