// The latency trace (DOA4_LAG_TRACE=1): one CSV row per main-loop pass, from the frame hook to the host present that
// shows the frame's guest swap. Pure parts here (the row, the swap-index join, the CSV text; host-tested by
// tests/host/lagtrace_host_test.cpp); the hook call-outs, the SDK's present marks and the writer thread are in
// doa4_lagtrace_live.cpp.
//
// The path of one frame, as the call-outs see it (all on the game's main thread unless named):
//   t_frame_start    the frame hook (np_session.cpp sub_824F4498 pre-wrap), the main-loop head
//   t_np_done        after the session's frame step (np::OnFrame: staging, and online the wait for the peer's input)
//   t_frame_fn_done  after the frame function sub_824F4498 returned
//   t_input          the first pad read of the pass (np_session.cpp sub_82792040, the game's XamInputGetState path)
//   t_swap_call      the first call of the VdSwap wrapper sub_8275AEE0 (doa4_photo_hook.cpp)
//   t_guest_swap     the SDK's VdSwap inside it (rex_lag_last_guest_swap), with its ordinal guest_swap_index
//   t_swap_return    the wrapper returned
//   t_present_call / t_host_present   the host present (another thread) that painted that swap's output: just before
//                    IDXGISwapChain::Present and just after it returned (the SDK's lag marks, joined by swap index)
// Times are microseconds on the QueryPerformanceCounter clock; 0 means "did not happen" (an empty CSV field).

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace doa4::lagtrace {

// One pass of the main loop.
struct Frame {
  uint64_t frame = 0;         // the trace's own count of frame-hook calls, from 1
  int64_t np_frame = -1;      // the session's frame (np::CurrentFrame after its frame step); -1 without a session
  int64_t t_frame_start = 0;
  int64_t t_np_done = 0;
  int64_t t_frame_fn_done = 0;
  int64_t t_input = 0;
  int64_t t_swap_call = 0;
  int64_t t_guest_swap = 0;
  int64_t t_swap_return = 0;
  uint32_t guest_swap_index = 0;  // 0: no VdSwap on the main thread in this pass
  uint32_t pad_calls_main = 0;    // pad reads on the main thread in this pass
  uint32_t pad_calls_other = 0;   // pad reads on other threads while this pass ran
  uint32_t swap_calls_main = 0;   // VdSwap wrapper calls on the main thread in this pass
  uint32_t swap_calls_other = 0;  // VdSwap wrapper calls on other threads (the D3D kick thread) while it ran
  int32_t fight_state = -1;       // the game's fight sequence byte entering the pass (np_digest.h kFightState; 2: a round)
};

// One host present (the SDK's rex::pace_stats::LagPresentMark, in microseconds).
struct Present {
  int64_t t_call = 0;
  int64_t t_return = 0;
  uint32_t swap_index = 0;  // the guest swap whose output it painted; 0: none
  uint32_t flags = 0;       // bit 0: the swap chain allows tearing
  // The GPU command processor thread's marks of that swap's packet (LagPresentMark::gpu; Task 8, R56). A "wait" is a
  // WAIT_REG_MEM whose condition did not hold at its first check; the times are those of the last wait before the
  // swap packet. 0: none.
  int64_t t_gpu_wait_enter = 0;  // its first unmet check
  int64_t t_vblank = 0;          // the last guest vblank before its wake
  int64_t t_gpu_wait_wake = 0;   // the check at which its condition held
  int64_t t_gpu_swap = 0;        // the swap packet's handler (IssueSwap, then the paint and this present)
  int64_t gpu_wait_us = 0;       // every wait since the previous swap packet, summed
  uint32_t gpu_waits = 0;        // how many
  uint32_t gpu_wait_poll = 0;    // the last wait's poll address, reference and wait_info
  uint32_t gpu_wait_ref = 0;
  uint32_t gpu_wait_info = 0;
  uint32_t gpu_after_wait_packets = 0;  // packets from that wake up to the swap packet (included; no NOPs)
  uint32_t gpu_after_wait_sig = 0;      // the first four, a byte each from the low byte (type-3 opcode; 0x80 type 0)
  uint32_t gpu_flags = 0;               // bit 0: presented early, at the wait before its swap packet (no wake yet)
};

struct Row {
  Frame frame;
  bool joined = false;  // a present of the frame's swap was found; `present` is the first one
  Present present;
};

// Joins frames to presents by swap index. Frames are added in order, presents in order (their swap indices never
// decrease: the presenter's mailbox always hands out the newest guest output). A frame's row is ready when the first
// present of its swap is known (joined), or when a present of a later swap shows that its own output was never
// painted (dropped for a newer one), or when it has no swap; Take(true) also hands out the frames still waiting.
class Joiner {
 public:
  explicit Joiner(size_t max_waiting = 600) : max_waiting_(max_waiting) {}
  void AddFrame(const Frame& frame);
  void AddPresent(const Present& present);
  // The rows that are ready, in frame order. flush: also every waiting frame (unjoined). A frame that has waited
  // while more than max_waiting frames queued behind it is handed out unjoined too.
  std::vector<Row> Take(bool flush);
  size_t waiting() const { return frames_.size(); }

 private:
  size_t max_waiting_;
  std::deque<Frame> frames_;
  std::map<uint32_t, Present> first_;  // the first present of each swap index not yet handed out
  uint32_t newest_ = 0;                // the highest swap index any present painted
  uint32_t done_ = 0;                  // swap indices up to this one were handed out (their presents dropped)
};

// The CSV: the header line and one row's line (both without the line end). The first six columns are the plan's
// (frame, t_input_us, t_frame_start_us, t_guest_swap_us, t_host_present_us, guest_swap_index); the rest name the
// other points and counts up to fight_state, then the GPU thread's marks of the joined present's swap packet (Task 8:
// t_gpu_wait_enter_us .. gpu_after_wait_sig, empty when unjoined). A time of 0 is an empty field.
std::string CsvHeader();
std::string CsvLine(const Row& row);

// QueryPerformanceCounter ticks to microseconds (rounded down; no overflow for frequencies below 9e12 Hz).
int64_t QpcToUs(int64_t qpc, int64_t frequency);

// The SDK's present mark (rex::pace_stats::LagPresentMark: its fields and its `gpu`, a LagGpuSwap) in microseconds; a
// template so the host test can pin the mapping with a stand-in of the same field names. A GPU time of 0 stays 0.
template <class Mark>
Present PresentFromMark(const Mark& m, int64_t frequency) {
  auto us = [frequency](int64_t qpc) { return qpc ? QpcToUs(qpc, frequency) : 0; };
  Present p;
  p.t_call = QpcToUs(m.qpc_before, frequency);
  p.t_return = QpcToUs(m.qpc_after, frequency);
  p.swap_index = m.swap_index;
  p.flags = m.flags;
  p.t_gpu_wait_enter = us(m.gpu.qpc_wait_enter);
  p.t_vblank = us(m.gpu.qpc_vblank);
  p.t_gpu_wait_wake = us(m.gpu.qpc_wait_wake);
  p.t_gpu_swap = us(m.gpu.qpc_swap);
  p.gpu_wait_us = QpcToUs(m.gpu.wait_ticks, frequency);
  p.gpu_waits = m.gpu.waits;
  p.gpu_wait_poll = m.gpu.wait_poll;
  p.gpu_wait_ref = m.gpu.wait_ref;
  p.gpu_wait_info = m.gpu.wait_info;
  p.gpu_after_wait_packets = m.gpu.after_wait_packets;
  p.gpu_after_wait_sig = m.gpu.after_wait_sig;
  p.gpu_flags = m.gpu.flags;
  return p;
}

// ---- Live (doa4_lagtrace_live.cpp) ----
// The gate: DOA4_LAG_TRACE=1, read once by Start(). Off, every call-out below costs its caller this one load and branch
// (each call site is `if (lagtrace::On()) lagtrace::OnX(...)`).
extern bool g_on;
inline bool On() { return g_on; }

// Before the guest's first thread: reads DOA4_LAG_TRACE; when it is 1, turns the SDK's lag marks on, opens
// <DOA4_LAG_DIR, else default_dir>/lagtrace_<pid>.csv, starts the writer thread and logs "[doa4][LAG] lagtrace on: ...".
void Start(const std::filesystem::path& default_dir);
// At shutdown: the writer's last drain (every waiting frame, unjoined) and its summary line. The rows written before
// are already on disk (flushed every pass), so a process ended by the watchdog keeps them.
void Stop();

// The call-outs (main thread unless named): the frame hook's entry, after np::OnFrame, after the frame function;
// the pad read (any thread); the VdSwap wrapper's entry and return (any thread).
// OnFrameStart: fight_state = the fight sequence byte; session / det_audio = whether a netplay session runs and whether
// the determinism layer's audio lockstep (its frame pacing wait) is on, logged once at the first frame.
void OnFrameStart(int32_t fight_state, bool session, bool det_audio);
void OnFrameNpDone(int64_t np_frame);
void OnFrameFnDone();
void OnPad();
void OnSwapCall();
void OnSwapReturn();

}  // namespace doa4::lagtrace
