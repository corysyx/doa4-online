// Skip the boot intro (update G6). See doa4_intro_skip.h.

#include "doa4_intro_skip.h"

namespace doa4::intro_skip {

namespace {
TitleLatch g_title;
std::atomic<int> g_armed{-1};  // -1: not armed yet; 0 / 1: doa4_skip_intro at start-up
std::atomic<uint32_t> g_swaps{0};
std::atomic<uint32_t> g_frames{0};
}  // namespace

TitleLatch& Title() { return g_title; }
bool NoteSpriteDraw(uint32_t sprite) { return g_title.NoteDraw(sprite); }
bool TitleSeen() { return g_title.Seen(); }

bool Arm(bool skip_intro) {
  int expected = -1;
  return g_armed.compare_exchange_strong(expected, skip_intro ? 1 : 0, std::memory_order_acq_rel);
}

MovieRule& Movies() {
  // The first use closes arming: a rule never changes once a hook has read it.
  static MovieRule rule([] {
    int expected = -1;
    g_armed.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    return g_armed.load(std::memory_order_acquire) == 1;
  }());
  return rule;
}

void CountSwap() { g_swaps.fetch_add(1, std::memory_order_relaxed); }
void CountFrame() { g_frames.fetch_add(1, std::memory_order_relaxed); }
uint32_t Swaps() { return g_swaps.load(std::memory_order_relaxed); }
uint32_t Frames() { return g_frames.load(std::memory_order_relaxed); }

}  // namespace doa4::intro_skip
