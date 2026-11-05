// What the FPS box shows and when the mouse cursor hides, apart from the app: no SDK, no ImGui, host-tested
// (tests/host/display_host_test.cpp).
#pragma once

#include <cstdint>
#include <string>

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

}  // namespace doa4::display
