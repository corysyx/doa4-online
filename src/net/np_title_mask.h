// The netplay title mask (small fixes 2, Part 3): while the game is on the title screen or any screen before it, the
// joiner's own input is sent as nothing pressed.
//
// Why: the joiner's START or A read while PRESS START is drawn makes user 1 the pressing player; the game asks Player2 to
// sign in, nobody is signed in in a session, and a "Cancel Saving" box loops every 4 frames on both PCs for ever (Task 6,
// np_runs/sf2/t6/t6a1). The host's press is the one that should open Mode Select.
//
// The latch (TitleMask) is fed by guest draws seen by the 2D sprite hook (doa4_app_hooks.cpp, sub_823BB470), which run on
// the guest main thread inside game frames, so both PCs see the same draws at the same frame (lockstep):
// - armed at session start (frame 0, before any input of the session is handed over);
// - armed by every frame that draws the title logo visible, sprite 0x30000 (doa4_intro_skip.h kTitleLogoSprite), so
//   every return to the title (from Mode Select, from the attract, from a mode) arms it again;
// - disarmed only by the menu signal: a frame that draws one of Mode Select's item captions visible (drawn at 640,558
//   every frame while Mode Select is up, one id per item: doa4_online_item_block.h; the list below) and does not draw
//   the logo. Proved on the title's paths in np_runs/sf2/t7 (task-7-report.md names the runs and the lines).
// - every other draw, and frames with no draw of either kind (the attract cinematic draws no 2D sprite at all, loads,
//   the screens between the title and the menu), leave it as it is.
// Draws are only noted (OnDraw); the frame hook applies a frame's draws at its end (EndFrame), so the order of the draws
// inside a frame does not matter.
//
// The mask itself (MaskLocalInput, JoinerFilter) is applied only on the joiner of a live session, to the joiner's pad
// before it enters the netplay stream (np_session.cpp, the frame's local pad), so both games see the same inputs
// whatever the latch says. The host, offline play and harness record and replay runs send what they were given.
//
// A press that began under the mask (JoinerFilter): when the mask goes off while the joiner still holds a button, that
// button stays "not pressed" in what is sent until the joiner has released it once, so a START held from boot does not
// act as a fresh press on the menu's first frame. Other buttons go through at once; sticks and triggers go through as
// soon as the mask is off.
//
// Pure C++ (no rex includes), host-tested (tests/host/np_title_mask_host_test.cpp). Not thread-safe: the caller holds
// one lock around each call (np_session.cpp).

#pragma once

#include <cstdint>

#include "np_core.h"

namespace doa4::np::title_mask {

inline constexpr uint32_t kTitleLogoSprite = 0x30000;
// Mode Select's item captions (doa4_online_item_block.h): STORY 0x40002, TIME ATTACK 0x40005, DOA ONLINE 0x4000d, and the
// others in the order that header lists them.
inline constexpr uint32_t kMenuCaptionSprites[] = {0x40002, 0x40005, 0x40006, 0x40009, 0x4000a, 0x4000e, 0x40003,
                                                   0x4000c, 0x40013, 0x4000f, 0x40011, 0x40004, 0x4000d};

inline bool IsMenuSignal(uint32_t sprite, bool visible) {
  if (!visible) return false;
  for (uint32_t c : kMenuCaptionSprites)
    if (sprite == c) return true;
  return false;
}

// One latch edge (kNone: the call changed nothing).
enum class Edge : uint8_t { kNone, kArmedSessionStart, kArmedTitleLogo, kOff };

class TitleMask {
 public:
  Edge OnSessionStart() { return Arm(Edge::kArmedSessionStart); }
  Edge OnTitleLogoDrawn() { return Arm(Edge::kArmedTitleLogo); }
  Edge OnMenuReached() {
    if (!armed_) return Edge::kNone;
    armed_ = false;
    return Edge::kOff;
  }
  // The sprite hook's entry: every submission with its draw bit (byte 23 & 0x08). Only notes what the frame drew.
  void OnDraw(uint32_t sprite, bool visible) {
    if (visible && sprite == kTitleLogoSprite)
      logo_drawn_ = true;
    else if (IsMenuSignal(sprite, visible) && menu_sprite_ == 0)
      menu_sprite_ = sprite;
  }
  // The frame boundary (the frame hook, before the next frame's input): applies the frame's draws and forgets them. A
  // frame that drew the logo arms, even if it drew a caption too: from the title to Mode Select the title's logo is
  // still drawn for about 23 frames over Mode Select's first frames, and in each of them the caption comes first
  // (t7s1, frames 15397-15419). A frame with a caption and no logo disarms. Any other frame changes nothing.
  Edge EndFrame() {
    Edge e = Edge::kNone;
    if (logo_drawn_)
      e = OnTitleLogoDrawn();
    else if (menu_sprite_ != 0)
      e = OnMenuReached();
    last_menu_sprite_ = menu_sprite_;
    logo_drawn_ = false;
    menu_sprite_ = 0;
    return e;
  }
  // The caption id that disarmed (for the log line): the one the last EndFrame saw.
  uint32_t LastMenuSprite() const { return last_menu_sprite_; }
  bool Armed() const { return armed_; }

 private:
  Edge Arm(Edge e) {
    if (armed_) return Edge::kNone;
    armed_ = true;
    return e;
  }
  bool armed_ = false;
  bool logo_drawn_ = false;     // this frame drew the logo
  uint32_t menu_sprite_ = 0;    // this frame's first caption id drawn, 0 = none
  uint32_t last_menu_sprite_ = 0;
};

// True only for the joiner of a live session while the latch is armed. The host and offline play never mask.
inline bool MaskLocalInput(bool is_joiner, bool in_session, bool armed) { return is_joiner && in_session && armed; }

// The joiner's pad, once a frame: masked, it is sent as nothing pressed; unmasked, the buttons held since the mask are
// held back until released once. Also counts, per armed period, the frames whose non-idle pad was replaced.
class JoinerFilter {
 public:
  struct Out {
    Pad sent;
    bool first_replaced = false;  // the first frame of this armed period on which a non-idle pad was replaced
  };
  Out Apply(bool mask, const Pad& pad) {
    Out out;
    if (mask) {
      held_ = pad.buttons;
      if (!(pad == Pad{})) {
        out.first_replaced = replaced_ == 0;
        ++replaced_;
      }
      return out;  // out.sent: nothing pressed
    }
    held_ &= pad.buttons;  // a released button is no longer held back
    out.sent = pad;
    out.sent.buttons = uint16_t(pad.buttons & ~held_);
    return out;
  }
  // The armed period's count of replaced frames; reset to zero (call at each off edge).
  uint32_t TakeReplacedCount() {
    const uint32_t n = replaced_;
    replaced_ = 0;
    return n;
  }

 private:
  uint16_t held_ = 0;
  uint32_t replaced_ = 0;
};

}  // namespace doa4::np::title_mask
