// The game's own online mode is blocked on MODE SELECT.
//
// MODE SELECT's "DOA ONLINE" item is the Xbox Live mode, which cannot work on PC: choosing it opens an empty DOA ONLINE
// screen where no button answers (B and START do nothing), and the player has to quit the game. Measured 2026-11-11 in a
// scripted run (ou_runs/sf/runs/sf2: LEFT from STORY onto the globe item, A, then B and START: the screen stays).
//
// The block: while MODE SELECT draws that item's caption, the game's pad read (sub_82792040) loses A and START, so the item
// cannot be chosen. The cursor still moves on to other items and B still goes back. The clock is the game's own frame count
// (intro_skip::Frames(), the frame function sub_824F4498): the caption's draw and the pad read are both guest main-thread
// work inside game frames, so two players in an online session (lockstep, the same frames and the same staged pads) make
// the same decision on the same frame, and a replay makes the recording's. Not the present count: the kick thread's
// keep-alive re-presents also count there (review 1, M1/I3).
//
// The signal is the item's caption sprite, drawn at 640,558 every frame while the item is the selected one (the caller of
// sub_823BB470 returns to 0x82530468). Every item has its own caption id (0x40002 STORY, 0x40005 TIME ATTACK, 0x40006,
// 0x40009, 0x4000a, 0x4000e, 0x40003, 0x4000c, 0x40013, 0x4000f, 0x40011, 0x40004 to the right of STORY; 0x4000d DOA ONLINE
// to its left), and the caption changes on the very frame the cursor moves (the item icons take 14 frames to slide).
#pragma once

#include <atomic>
#include <cstdint>

namespace doa4::online_item_block {

inline constexpr uint32_t kOnlineCaptionSprite = 0x4000d;
inline constexpr uint16_t kBlockedButtons = 0x1010;  // XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A
// A pad read is blocked while the caption was drawn this frame or within this many frames before it.
inline constexpr uint32_t kRecentFrames = 2;

class Block {
 public:
  // The sprite hook: every submission, with its draw bit.
  void NoteDraw(uint32_t sprite, bool visible, uint32_t frame) {
    if (sprite == kOnlineCaptionSprite && visible) last_.store(frame + 1, std::memory_order_relaxed);  // 0 = never drawn
  }
  // The pad-read hook: the buttons the game gets (A and START removed while the caption is on screen).
  uint16_t Filter(uint16_t buttons, uint32_t now_frame) const {
    const uint32_t last = last_.load(std::memory_order_relaxed);
    if (last == 0) return buttons;
    const uint32_t drawn = last - 1;
    const bool recent = now_frame <= drawn || now_frame - drawn <= kRecentFrames;
    return recent ? uint16_t(buttons & ~kBlockedButtons) : buttons;
  }

 private:
  std::atomic<uint32_t> last_{0};
};

inline Block& Get() {
  static Block block;
  return block;
}

}  // namespace doa4::online_item_block
