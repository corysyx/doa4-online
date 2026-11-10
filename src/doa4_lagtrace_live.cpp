// The latency trace in the game (doa4_lagtrace.h): the call-outs, the SDK's lag marks and the CSV writer thread.
//
// The main loop (the frame hook's host thread and fiber) owns the frame being built; other threads and fibers only bump
// two atomic counters.
// A finished frame goes through an SPSC ring to the writer thread, which drains it and the SDK's present marks every
// 50 ms, joins them (Joiner) and appends the ready rows to lagtrace_<pid>.csv, flushing each pass.

#include "doa4_lagtrace.h"

#ifndef NOMINMAX
#define NOMINMAX  // rex/pace_stats.h uses std::min / std::max
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <rex/logging.h>
#include <rex/pace_stats.h>

#include "net/np_spsc.h"

// The SDK's lag marks (sdk-ou1 src/ui/d3d12/d3d12_presenter.cpp).
extern "C" {
__declspec(dllimport) void rex_lag_marks_enable(int on);
__declspec(dllimport) uint32_t rex_lag_marks_drain(rex::pace_stats::LagPresentMark* out, uint32_t max);
__declspec(dllimport) uint64_t rex_lag_marks_dropped();
__declspec(dllimport) uint32_t rex_lag_last_guest_swap(int64_t* qpc_out);
}

namespace doa4::lagtrace {

bool g_on = false;

namespace {

int64_t g_frequency = 1;  // QPC ticks per second, set by Start
// The main loop's flow of control: the frame hook's host thread AND its Win32 fiber. The game runs a cooperative fiber
// scheduler (doa4_app_hooks.cpp FIX 3: every guest fiber is a Win32 fiber), so several guest flows share the main host
// thread; a call from another fiber of that thread is not the main loop's.
std::atomic<DWORD> g_main{0};
std::atomic<void*> g_main_fiber{nullptr};
std::atomic<uint32_t> g_other_pad{0}, g_other_swap{0};
// main thread only
Frame g_frame;
uint64_t g_frames_seen = 0;
uint32_t g_last_swap_index = 0;
bool g_warned_thread = false;

doa4::np::Spsc<Frame, 1024> g_ring;
std::atomic<uint64_t> g_ring_dropped{0};
std::FILE* g_csv = nullptr;
std::thread g_writer;
std::atomic<bool> g_stop{false};

int64_t NowUs() {
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return QpcToUs(c.QuadPart, g_frequency);
}

bool OnMain() {
  return GetCurrentThreadId() == g_main.load(std::memory_order_relaxed) &&
         GetCurrentFiber() == g_main_fiber.load(std::memory_order_relaxed);
}
bool g_logged_pad_main = false;  // main only: the first pad read of the main loop is logged once
std::atomic<bool> g_logged_pad_other{false};  // the first pad read anywhere else, once

void WriterLoop() {
  Joiner joiner;
  std::vector<rex::pace_stats::LagPresentMark> marks(512);
  uint64_t rows = 0, joined = 0, presents = 0;
  auto pass = [&](bool flush) {
    Frame f;
    while (g_ring.Pop(&f)) joiner.AddFrame(f);
    for (;;) {
      const uint32_t n = rex_lag_marks_drain(marks.data(), uint32_t(marks.size()));
      for (uint32_t i = 0; i < n; ++i) {
        Present p;
        p.t_call = QpcToUs(marks[i].qpc_before, g_frequency);
        p.t_return = QpcToUs(marks[i].qpc_after, g_frequency);
        p.swap_index = marks[i].swap_index;
        p.flags = marks[i].flags;
        joiner.AddPresent(p);
      }
      presents += n;
      if (n < marks.size()) break;
    }
    std::string text;
    for (const Row& r : joiner.Take(flush)) {
      text += CsvLine(r);
      text += '\n';
      ++rows;
      joined += r.joined ? 1 : 0;
    }
    if (!text.empty()) {
      std::fwrite(text.data(), 1, text.size(), g_csv);
      std::fflush(g_csv);
    }
  };
  while (!g_stop.load(std::memory_order_acquire)) {
    pass(false);
    Sleep(50);
  }
  pass(true);
  REXLOG_INFO("[doa4][LAG] lagtrace off: {} rows ({} joined to a present), {} presents, {} present marks dropped, "
              "{} frames dropped",
              rows, joined, presents, rex_lag_marks_dropped(), g_ring_dropped.load());
}

}  // namespace

void Start(const std::filesystem::path& default_dir) {
  const char* v = std::getenv("DOA4_LAG_TRACE");
  if (!v || std::string(v) != "1") return;
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  g_frequency = freq.QuadPart;
  std::filesystem::path dir = default_dir;
  if (const wchar_t* d = _wgetenv(L"DOA4_LAG_DIR"); d && *d) dir = d;
  const std::filesystem::path path = dir / ("lagtrace_" + std::to_string(GetCurrentProcessId()) + ".csv");
  g_csv = _wfopen(path.c_str(), L"wb");
  if (!g_csv) {
    REXLOG_WARN("[doa4][LAG] lagtrace: cannot open {}; the trace stays off", path.string());
    return;
  }
  const std::string header = CsvHeader() + "\n";
  std::fwrite(header.data(), 1, header.size(), g_csv);
  std::fflush(g_csv);
  rex_lag_marks_enable(1);
  g_on = true;
  g_writer = std::thread(WriterLoop);
  REXLOG_INFO("[doa4][LAG] lagtrace on: {} (QPC frequency {} Hz)", path.string(), g_frequency);
}

void Stop() {
  if (!g_on || !g_writer.joinable()) return;
  g_stop.store(true, std::memory_order_release);
  g_writer.join();
  std::fclose(g_csv);
  g_csv = nullptr;
}

void OnFrameStart() {
  const int64_t now = NowUs();
  const DWORD self = GetCurrentThreadId();
  void* const fiber = GetCurrentFiber();
  if (g_frames_seen == 0) {
    g_main.store(self, std::memory_order_relaxed);
    g_main_fiber.store(fiber, std::memory_order_relaxed);
    REXLOG_INFO("[doa4][LAG] lagtrace: the frame hook runs on host thread {}, fiber {}", self, fiber);
  } else if (!OnMain()) {
    if (!g_warned_thread) {
      g_warned_thread = true;
      REXLOG_INFO("[doa4][LAG] lagtrace: the frame hook ran on host thread {} fiber {}, not the first frame's; such "
                  "calls are not traced",
                  self, fiber);
    }
    return;
  } else {
    g_frame.pad_calls_other = g_other_pad.exchange(0, std::memory_order_relaxed);
    g_frame.swap_calls_other = g_other_swap.exchange(0, std::memory_order_relaxed);
    if (!g_ring.Push(std::move(g_frame))) g_ring_dropped.fetch_add(1, std::memory_order_relaxed);
  }
  g_frame = Frame{};
  g_frame.frame = ++g_frames_seen;
  g_frame.t_frame_start = now;
}

void OnFrameNpDone(int64_t np_frame) {
  if (!OnMain()) return;
  g_frame.t_np_done = NowUs();
  g_frame.np_frame = np_frame;
}

void OnFrameFnDone() {
  if (!OnMain()) return;
  g_frame.t_frame_fn_done = NowUs();
}

void OnPad() {
  if (!OnMain()) {
    g_other_pad.fetch_add(1, std::memory_order_relaxed);
    // after the first frame hook only (before it every read counts here)
    if (g_main.load(std::memory_order_relaxed) != 0 && !g_logged_pad_other.exchange(true, std::memory_order_relaxed)) {
      REXLOG_INFO("[doa4][LAG] lagtrace: a pad read on host thread {} fiber {}, not the frame hook's (thread {} fiber {})",
                  GetCurrentThreadId(), GetCurrentFiber(), g_main.load(std::memory_order_relaxed),
                  g_main_fiber.load(std::memory_order_relaxed));
    }
    return;
  }
  if (g_frame.pad_calls_main++ == 0) g_frame.t_input = NowUs();
  if (!g_logged_pad_main) {
    g_logged_pad_main = true;
    REXLOG_INFO("[doa4][LAG] lagtrace: the first pad read on the frame hook's thread and fiber, at trace frame {}",
                g_frames_seen);
  }
}

void OnSwapCall() {
  if (!OnMain()) {
    g_other_swap.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (g_frame.swap_calls_main++ == 0) g_frame.t_swap_call = NowUs();
}

void OnSwapReturn() {
  if (!OnMain() || g_frame.swap_calls_main != 1 || g_frame.t_swap_return != 0) return;
  g_frame.t_swap_return = NowUs();
  int64_t qpc = 0;
  const uint32_t index = rex_lag_last_guest_swap(&qpc);
  if (index != 0 && index != g_last_swap_index) {  // this call reached VdSwap
    g_last_swap_index = index;
    g_frame.guest_swap_index = index;
    g_frame.t_guest_swap = QpcToUs(qpc, g_frequency);
  }
}

}  // namespace doa4::lagtrace
