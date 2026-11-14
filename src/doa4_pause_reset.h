// START pressed together with BACK: the pause opens one frame after the reset (issue #4's pause streaks).
//
// In Sparring, BACK is Reset Position and START opens the pause menu. When both go down on the same frame the game resets
// the fighters and the stage and pauses in the same update, so the first update after the reset never runs, and on the
// waterfall bridge the paused frames draw magenta and blue streaks for as long as the menu stays open (reported in issue
// #4; reproduced 2026-11-12, notes/research/pause_streaks_issue4_2026-11-12_reproduced.md: one same-frame press is enough,
// BACK one frame before START is clean).
//
// The rule (applied in the pad read, sub_82792040, np_session.cpp): on the frame a pad's START and BACK both go down the
// game gets BACK only, and START on the next frame the pad is read, whether or not it is still held. The press is delayed
// by one frame, never lost; both held keep counting towards the game's own soft reset (START + BACK for 180 frames) from
// the second frame on. Nothing else changes: any other order, either button alone or held from before, and every other
// button pass as they are. Pads are judged apart.
//
// The cause the rule is tied to is the same-frame pair, not Sparring or the bridge: elsewhere a START that comes one frame
// late is all it does. Its clock is the game's frame count (intro_skip::Frames()), and the pad state it remembers is what
// the game was given, so two players of an online session, fed the same pads on the same frames, decide alike. A pad read
// more than once in a frame gets one decision for that frame (the game polls each pad once a frame, sub_82774D60).
#pragma once

#include <array>
#include <cstdint>

namespace doa4::pause_reset {

inline constexpr uint16_t kStart = 0x0010;  // XINPUT_GAMEPAD_START
inline constexpr uint16_t kBack = 0x0020;   // XINPUT_GAMEPAD_BACK
inline constexpr uint32_t kPads = 4;

class Pads {
 public:
  // The buttons the game gets for `user` on game frame `frame`. One thread at a time (the caller locks).
  uint16_t Filter(uint32_t user, uint16_t buttons, uint32_t frame) {
    if (user >= kPads) return buttons;
    Pad& p = pads_[user];
    if (!p.read || p.frame != frame) {  // the frame's first read decides
      const bool start_down = (buttons & kStart) && !(p.given & kStart);
      const bool back_down = (buttons & kBack) && !(p.given & kBack);
      p.force = p.owed;
      p.owed = false;
      // (On a frame that carries a delayed START this is false as long as the pad is read once a frame: the game got BACK
      // on the frame before. If it is true anyway, the withhold wins below and the START stays owed.)
      p.hold = start_down && back_down;
      if (p.hold) {
        p.owed = true;
        ++delayed_;
      }
      p.read = true;
      p.frame = frame;
    }
    uint16_t out = buttons;
    if (p.hold) {
      out = uint16_t(out & ~kStart);
    } else if (p.force) {
      out = uint16_t(out | kStart);
    }
    p.given = out;
    return out;
  }
  // Presses delayed so far (for the log).
  uint32_t Delayed() const { return delayed_; }

 private:
  struct Pad {
    bool read = false;   // a frame has been decided
    uint32_t frame = 0;  // that frame
    uint16_t given = 0;  // what the game got on the last read
    bool hold = false;   // this frame: START withheld
    bool force = false;  // this frame: the delayed START
    bool owed = false;   // a START is owed to the next frame
  };
  std::array<Pad, kPads> pads_{};
  uint32_t delayed_ = 0;
};

}  // namespace doa4::pause_reset
