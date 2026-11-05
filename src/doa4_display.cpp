// See doa4_display.h.

#include "doa4_display.h"

#include <cmath>

namespace doa4::display {

namespace {
constexpr double kWindowSeconds = 0.5;
}

void FpsMeter::Sample(uint32_t swaps, double now_seconds) {
  if (!started_) {
    started_ = true;
    base_swaps_ = swaps;
    base_seconds_ = now_seconds;
    return;
  }
  const double elapsed = now_seconds - base_seconds_;
  if (elapsed < kWindowSeconds) return;
  fps_ = double(uint32_t(swaps - base_swaps_)) / elapsed;  // unsigned: the counter may wrap
  has_value_ = true;
  base_swaps_ = swaps;
  base_seconds_ = now_seconds;
}

std::string FpsMeter::Text() const {
  return has_value_ ? "FPS " + std::to_string(std::llround(fps_)) : std::string("FPS --");
}

CursorAction DecideCursor(const CursorInputs& in) {
  if (in.mnk_mode) return CursorAction::kLeave;
  const bool needs_mouse = in.menu_open || in.setup_open || in.sdk_overlay_open;
  return in.focused && !needs_mouse ? CursorAction::kHide : CursorAction::kShow;
}

}  // namespace doa4::display
