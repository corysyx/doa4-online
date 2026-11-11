// The overlay's paint gate (F7 FPS line / F9 box / online banner / update notice; doa4_net_overlay.h). The overlay is a
// display-only ImGui dialog (WantsContinuousPaint false): the guest output's frames repaint it, so the host presents once
// per guest frame. While no guest frame arrives (a session hold: "Waiting for the other player...", a stalled game) the
// app's UI tick asks the gate whether the overlay still needs a paint and requests one. A paint is needed while the
// content differs from what was last drawn, and until one content has been drawn kSettleDraws times: ImGui keeps a new
// auto-resized window hidden for its first frame while it measures it, so one paint would not show a new banner.
// Header-only and free of the SDK, for the host test (tests/host/overlay_paint_host_test.cpp). UI thread only.

#pragma once

#include <string>
#include <vector>

namespace doa4::overlay_paint {

struct Content {
  std::string banner;              // "" = none
  std::string fps;                 // the F7 line, drawn larger at the top of the box; "" = none
  std::vector<std::string> stats;  // the F9 box lines; no line and no fps = no box
  std::string notice;              // the start-up "Update available: <a.b.c>" (Task 15), under PRESS START; "" = none
  bool operator==(const Content&) const = default;
};

// Draws of one content after which the gate stops asking for paints (the first is ImGui's hidden measuring frame).
inline constexpr int kSettleDraws = 3;

class Gate {
 public:
  // The overlay drew `drawn` (called from its OnDraw). Returns true when it differs from the content drawn before.
  bool Drew(const Content& drawn) {
    if (draws_ > 0 && drawn == last_) {
      if (draws_ < kSettleDraws) ++draws_;
      return false;
    }
    last_ = drawn;
    draws_ = 1;
    return true;
  }
  // Whether a paint is still needed to show `now`.
  bool NeedsPaint(const Content& now) const { return draws_ < kSettleDraws || !(now == last_); }

 private:
  Content last_;
  int draws_ = 0;
};

// The UI tick's hold detector (review I1): a paint is due when the overlay still needs one AND no guest frame arrived
// since the previous tick (the guest swap count did not move: a whole tick, 100 ms, without a frame). Keyed on the
// game's progress, not on the gate's settled state: at 25 fps a change is drawn only twice in a tick, which is not a
// hold, and a paint requested there would only re-present the last frame. Called on every UI tick, also while there is
// no overlay (needs_paint false), so the previous count is always the previous tick's.
class HoldWatch {
 public:
  // Returns true when a paint should be requested on this tick.
  bool Tick(bool needs_paint, unsigned swaps) {
    const bool stalled = has_last_ && swaps == last_swaps_;
    has_last_ = true;
    last_swaps_ = swaps;
    if (!needs_paint || !stalled) {
      streak_ = 0;
      return false;
    }
    ++streak_;
    return true;
  }
  // True on the first due tick of a run of due ticks (the app logs one line per hold).
  bool StreakStarted() const { return streak_ == 1; }

 private:
  bool has_last_ = false;
  unsigned last_swaps_ = 0;
  int streak_ = 0;
};

}  // namespace doa4::overlay_paint
