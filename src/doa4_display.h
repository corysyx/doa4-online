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

// The Output size row's exclusive mode list, owned by the app so it outlives the menu (rebuilt at every opening) (Task 10
// review I1). The SDK's listing merges the output's live mode in as its desktop entry, and while exclusive fullscreen is
// active that is the mode exclusive fullscreen set (GPU-scaled: 2560x1440 @ 59 Hz on the run PC), not a mode the monitor
// offers: so the list is made while exclusive is off, and kept for the openings made while it is on.
struct ExclusiveModesCache {
  std::vector<ExclusiveMode> modes;
  bool built = false;
  bool from_active = false;  // made while exclusive was active (the game started in it): its desktop entry dropped
};
enum class ModesStep { kUse, kBuild };
// What the menu does with the cache in a frame. Exclusive off: build at the opening's first frame (a fresh list for this
// opening, made before any radio can turn exclusive on), and whenever no list made with exclusive off exists; else use.
// Exclusive on: use any list (a pre-entry one, or one made during exclusive); build only if there is none, which is the
// game started in exclusive (that build drops the desktop entry: DropModeText). At most one build per menu opening
// (attempted: one was started in this opening, whatever its outcome; Task 10 re-review 2): a listing that fails is not
// tried again on every drawn frame (each try builds a DXGI factory), only at the next opening.
constexpr ModesStep ExclusiveModesStep(bool first_frame, bool attempted, bool built, bool from_active,
                                       bool exclusive_active) {
  if (attempted) return ModesStep::kUse;
  if (!exclusive_active) return (first_frame || !built || from_active) ? ModesStep::kBuild : ModesStep::kUse;
  return built ? ModesStep::kUse : ModesStep::kBuild;
}
// Removes every mode whose text is `text` (the listing's desktop entry when it was made during exclusive).
void DropModeText(std::vector<ExclusiveMode>& modes, const std::string& text);

// The title's "Update available: <a.b.c>" box (Task 15; NetOverlay), centred between PRESS START and the copyright line.
// The free band, measured on the user's 2560x1440 title screenshot (Task 12 fix round 2): PRESS START's glyphs end at
// y 1113 and the copyright line's begin at y 1208, i.e. 556.5 and 604 of the title's 720 (47.5 high, centre 580.25).
inline constexpr float kNoticeBandTop = 556.5f / 720.0f;
inline constexpr float kNoticeBandBottom = 604.0f / 720.0f;
inline constexpr float kNoticeMargin = 2.0f / 720.0f;  // kept clear of each edge of the band
inline constexpr float kNoticeBaseFontScale = 1.35f;   // Task 15's text size (the F7 line's), the font's size times this
inline constexpr float kNoticeScale = 1.3f;            // round 2 (the user): the text, and the box's padding, 1.3x that
struct NoticeLayout {
  float center_y = 0;    // the box's centre (ImGui display units)
  float font_scale = 0;  // the window font scale
  float pad_x = 0, pad_y = 0;  // the window padding
};
// display_h: ImGui's display height; font_px: the font's size; pad_x/pad_y: the style's window padding. The box (one text
// line: font_px * font_scale, plus pad_y twice) is kNoticeScale times Task 15's, unless that would not fit the band less
// its margins: then text and padding shrink together until it does (a small or high-DPI window: ImGui's units are
// physical pixels * 96 / DPI, so a 720-pixel window at 150 % is 480 high).
NoticeLayout NoticeLayoutFor(float display_h, float font_px, float pad_x, float pad_y);

// The Esc menu's width (Task 12 fix round 2: it opened at 560 and cut off its longest lines). The menu measures every row
// it draws without wrapping, in every tab (a text with ImGui::CalcTextSize, a radio button or checkbox with ToggleWidth, a
// row of widgets on one line with RowWidth); the window opens that wide and cannot be made narrower (a size constraint).
inline constexpr float kMenuMinWidth = 560.0f;  // the width it opened at before: never narrower than that
// The Esc menu's text size (the user, 2026-11-11): "a few sizes larger" in fullscreen (borderless or exclusive), the SDK's
// own 10 px in a window (doa4_menu.h AddMenuFont). Every fixed width the menu draws (combo boxes, buttons, the tables' first
// column, the code fields) follows the font in use: MenuPx(px, font_px) is px at the 10 px font, scaled to font_px.
inline constexpr float kMenuBaseFontPx = 10.0f;
inline constexpr float kMenuLargeFontPx = 13.0f;  // the fullscreen font (ProggyClean's own size; doa4_menu AddMenuFont)
inline constexpr float kMenuFontScale = kMenuLargeFontPx / kMenuBaseFontPx;
constexpr float MenuPx(float px, float font_px) { return px * font_px / kMenuBaseFontPx; }
// A radio button or checkbox as ImGui sizes it: the square (the frame height), then the inner spacing and the label.
constexpr float ToggleWidth(float frame_h, float inner_spacing, float label_w) {
  return frame_h + (label_w > 0 ? inner_spacing + label_w : 0);
}
// Widgets on one line (ImGui::SameLine): their widths plus the item spacing between each two.
float RowWidth(const std::vector<float>& items, float spacing);
// The window: the widest row plus the window padding on both sides and room for a scrollbar, at least kMenuMinWidth,
// at most the display's width.
float MenuWindowWidth(float widest_row, float padding_x, float scrollbar, float display_w);

}  // namespace doa4::display
