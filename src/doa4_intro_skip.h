// Skip the boot intro (update G6): with doa4_skip_intro on, the game starts at the title screen (DEAD OR ALIVE 4 ONLINE /
// PRESS START). Off by default; off, nothing changes.
//
// What the game does at boot (measured 2026-11-10, Task 14 Step 1, ou_runs/t14/runs/s1_off): sub_82403518 starts movie 18,
// ninja_vi_hd.sfd (sub_8242A168, r3 = the index into the 40-byte name table at 0x82F69FE0) at guest swap 27, polls
// sub_8242A3A8 ("finished?") once a frame until it says yes at swap 1242 (about 20 s: the stormy sea, the mountains, the Team
// NINJA logo), then stops it (sub_8242A0F0); the title's first logo draw follows at swap 1245. It is the only screen before
// the title. The title draws its logo, sprite 0x30000, every frame through sub_823BB470, all on the guest main thread.
// The title's attract sequence (the helicopters over the moon from about 30 s on the title, then the demo fight) is drawn by
// the game in real time: no movie start was logged in it.
//
// The rule: a movie whose start comes before the first title logo draw of this run is skipped the way a netplay session skips
// every movie (np_det_middleware.cpp: the start does nothing, "finished?" answers yes, the stop does nothing), only when
// doa4_skip_intro was on at start-up, and never in a session: there the session's own gate decides alone (it skips every
// movie on both PCs), so the player's own value never changes a session. The skip ORs with the session's gate, never
// replaces it. A movie that starts after the first title logo draw (any later movie of the run) always plays.
//
// The first-title latch is set by the first draw of sprite 0x30000 (doa4_app_hooks.cpp, the sub_823BB470 hook), once per
// process, never reset. NoteSpriteDraw is the one entry for it: the sprite hook hands it every draw's sprite id, and Task 15
// (the start-up update notice) reads the title signal from the same hook.
//
// Pure C++ (no rex includes), so host tests build it (tests/host/intro_skip_host_test.cpp).

#pragma once

#include <atomic>
#include <cstdint>

namespace doa4::intro_skip {

inline constexpr uint32_t kTitleLogoSprite = 0x30000;  // = title_word::kLogoSprite (doa4_title_word.h:42)

// The first title logo draw of the run: unseen until the first draw of sprite 0x30000, then seen for the rest of the
// process (never reset). Lock-free: one writer value (false -> true, once) and readers that only need to see it eventually.
class TitleLatch {
 public:
  // A 2D draw of `sprite`: true exactly once, for the draw that sets the latch (the first title logo draw).
  bool NoteDraw(uint32_t sprite) {
    if (sprite != kTitleLogoSprite || seen_.load(std::memory_order_acquire)) return false;
    return !seen_.exchange(true, std::memory_order_acq_rel);
  }
  bool Seen() const { return seen_.load(std::memory_order_acquire); }

 private:
  std::atomic<bool> seen_{false};
};

// The three movie hooks' answers. skip_intro is doa4_skip_intro as read at start-up (the setting applies from the next
// start). in_session: a netplay session runs (np::Active()); session_skips: the session's own movie gate (MoviesSkipped()).
class MovieRule {
 public:
  explicit MovieRule(bool skip_intro = false) : skip_intro_(skip_intro) {}

  // A movie start: true = skip it (do not start it). Records whether this movie was skipped by the intro rule, which the
  // next "finished?" and "stop" calls answer from.
  bool SkipStart(bool in_session, bool session_skips, bool title_seen) {
    const bool intro = !in_session && skip_intro_ && !title_seen;
    current_skipped_.store(intro, std::memory_order_release);
    return session_skips || intro;
  }
  // "finished?": true = answer "finished" without asking the game (the current movie was skipped).
  bool AnswerFinished(bool session_skips) const {
    return session_skips || current_skipped_.load(std::memory_order_acquire);
  }
  // "stop": true = nothing to stop (the current movie was skipped).
  bool SkipStop(bool session_skips) const { return AnswerFinished(session_skips); }

  bool skip_intro() const { return skip_intro_; }

 private:
  bool skip_intro_;
  std::atomic<bool> current_skipped_{false};
};

// The process's latch, for the sprite hook (and Task 15). NoteSpriteDraw(sprite) == Title().NoteDraw(sprite).
TitleLatch& Title();
bool NoteSpriteDraw(uint32_t sprite);
bool TitleSeen();

// The process's movie rule: Arm sets doa4_skip_intro's start-up value, once, before any guest code runs
// (dead_or_alive_4_app.h OnPostLoadXexImage); a later call changes nothing and returns false.
bool Arm(bool skip_intro);
MovieRule& Movies();

// Two clocks for the log lines (relaxed counters, logging only): guest swaps (sub_8275AEE0 calls that reached VdSwap) and
// frame-function calls (sub_824F4498).
void CountSwap();
void CountFrame();
uint32_t Swaps();
uint32_t Frames();

}  // namespace doa4::intro_skip
