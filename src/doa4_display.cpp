// See doa4_display.h.

#include "doa4_display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

std::string RefreshText(double hz) {
  const double rounded = std::round(hz * 100.0) / 100.0;
  if (std::fabs(rounded - std::round(rounded)) < 1e-9) return std::to_string(std::llround(rounded));
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", rounded);
  return buf;
}

OutputSizeList WindowedSizeList(const std::vector<std::pair<uint32_t, uint32_t>>& fitting, uint32_t current_width,
                                uint32_t current_height) {
  std::vector<std::pair<uint32_t, uint32_t>> sizes = fitting;
  if (current_width && current_height) sizes.emplace_back(current_width, current_height);
  std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) {
    const uint64_t pa = uint64_t(a.first) * a.second, pb = uint64_t(b.first) * b.second;
    return pa != pb ? pa < pb : a.first < b.first;
  });
  sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
  OutputSizeList out;
  for (const auto& s : sizes) {
    OutputSizeEntry e;
    e.width = s.first;
    e.height = s.second;
    e.label = std::to_string(s.first) + " x " + std::to_string(s.second);
    e.value = std::to_string(s.first) + "x" + std::to_string(s.second);
    if (s.first == current_width && s.second == current_height) out.selected = int(out.entries.size());
    out.entries.push_back(std::move(e));
  }
  return out;
}

OutputSizeList ExclusiveSizeList(const std::vector<ExclusiveMode>& modes, const std::string& saved) {
  OutputSizeList out;
  out.entries.push_back({"Desktop (current mode)", "", 0, 0});
  out.selected = 0;
  for (const ExclusiveMode& m : modes) {
    OutputSizeEntry e;
    e.width = m.width;
    e.height = m.height;
    e.value = m.text;
    e.label = std::to_string(m.width) + " x " + std::to_string(m.height) + " @ " + RefreshText(m.hz) + " Hz";
    if (!saved.empty() && m.text == saved) out.selected = int(out.entries.size());
    out.entries.push_back(std::move(e));
  }
  return out;
}

}  // namespace doa4::display
