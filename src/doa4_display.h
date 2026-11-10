// What the FPS box shows and when the mouse cursor hides, apart from the app: no SDK, no ImGui, host-tested
// (tests/host/display_host_test.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace doa4::display {

inline constexpr const char* kWindowTitle = "Dead or Alive 4 Online";

// Frames per second from the GPU's guest-swap counter: one value per window of at least half a second.
class FpsMeter {
 public:
  void Sample(uint32_t swaps, double now_seconds);
  // "FPS 60", or "FPS --" until a whole window has passed.
  std::string Text() const;

 private:
  bool started_ = false;
  bool has_value_ = false;
  uint32_t base_swaps_ = 0;
  double base_seconds_ = 0;
  double fps_ = 0;
};

struct CursorInputs {
  bool focused = false;           // the game's window has the keyboard focus
  bool menu_open = false;         // the Esc menu
  bool setup_open = false;        // the first-run setup screen
  bool sdk_overlay_open = false;  // F3, F4 or the console
  bool mnk_mode = false;          // keyboard and mouse as a controller: the SDK's input driver owns the cursor
};
enum class CursorAction { kLeave, kShow, kHide };
CursorAction DecideCursor(const CursorInputs& in);

// The Game tab's Output size row: the choices and which one is selected. Pure (the menu fills it from the SDK).
struct OutputSizeEntry {
  std::string label;
  std::string value;  // what is saved: "WxH" (windowed), "WxH@Hz" in the SDK's ModeText (exclusive), "" (desktop mode)
  uint32_t width = 0;
  uint32_t height = 0;
};
struct OutputSizeList {
  std::vector<OutputSizeEntry> entries;
  int selected = -1;  // an index into entries, -1 for none
};

// A refresh rate for a label: "60" when it rounds to a whole number at two decimals, else two decimals ("59.94").
std::string RefreshText(double hz);

// Windowed: the sizes that fit the work area (the SDK's list, any order), smallest first; the current size (0 x 0 = unknown)
// is added at its place when the list lacks it, and is the selected entry either way.
OutputSizeList WindowedSizeList(const std::vector<std::pair<uint32_t, uint32_t>>& fitting, uint32_t current_width,
                                uint32_t current_height);

// One exclusive mode as the SDK lists it: `text` is the saved "WxH@Hz".
struct ExclusiveMode {
  uint32_t width = 0;
  uint32_t height = 0;
  double hz = 0;
  std::string text;
};
// Exclusive: "Desktop (current mode)" (saved "") first, then the modes in the order given. `saved` is the saved
// fullscreen_exclusive_mode; one the list lacks selects the desktop entry, as the SDK falls back to the desktop mode.
OutputSizeList ExclusiveSizeList(const std::vector<ExclusiveMode>& modes, const std::string& saved);

}  // namespace doa4::display
