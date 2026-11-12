// The character select's RANDOM costume: every costume of the pool equally likely (the user, 2026-11-11).
//
// The game (research ou_runs/rnd/REPORT.md): sub_82523120 fills each RANDOM slot; for the costume it reads
// count = the select screen's count copy u8[0x83103000 + 25 * row + character] (row = the player's struct + 8), draws
// from a 20-card shuffle bag (sub_825211D0 on the bag 0x82E7628C, refilled by sub_8252D9E8 with only 5 random swaps)
// and takes costume = card % count; if the other player already has that character in that costume it takes
// (costume + 1) % count. So COS 1 came out far more often than the rest (a barely shuffled bag, and 20 cards that do
// not divide by most counts).
//
// The change (doa4_suits_hooks.cpp, the hook on sub_825211D0 at this one call site, return address kDrawReturn): the bag
// still deals its card (the game's state moves as before), then the card is replaced by a costume drawn evenly from the
// pool with the game's own gameplay generator (sub_824061A0, the Mersenne Twister at 0x833F7600, 31 bits a draw):
// both PCs of an online session make the same draws on the same frame. The pool is the count the game read (r31), the
// player's unlocked costumes, plus the swimsuits when this PC has them imported (the offline count copy raise,
// doa4_suits_hooks.cpp; in a session the table maximum). In a MIXED session (one player imported swimsuits, doa4_suits.h)
// both players draw only from the costumes before the swimsuits (SuitsOff below); the count (r31) is lowered to the pool
// too, so the game's "+1" step stays in it.
#pragma once

#include <algorithm>
#include <cstdint>

namespace doa4::random_costume {

inline constexpr uint32_t kDrawReturn = 0x8252352C;  // sub_82523120's costume draw: bl sub_825211D0 (recomp.19.cpp)
inline constexpr uint32_t kCostumeBag = 0x82E7628C;
inline constexpr uint32_t kRowCount = 2;              // the count copy rows sub_82523120 fills: player 0 and 1
inline constexpr uint32_t kMaxTries = 64;             // rejection draws before the plain remainder (never reached in practice)

// True when `row`'s player draws without the swimsuits: in a MIXED session, both players (the user, 2026-11-11). A suit
// that RANDOM picks is first loaded during the fight's load, where the PC without it loads COS 1 instead; the loads end on
// different frames and the session desyncs (ou_runs/rnd/pairs mixA_s1 and mixA_s1b, both at frame 3577; a hand-picked
// suit, loaded on the select screen, stays in step: ctrlC1_s1). Offline and in a same-import session the pool keeps them.
constexpr bool SuitsOff(bool mixed_session, uint32_t row) { return mixed_session && row < kRowCount; }

// The pool one RANDOM costume draws from: the count the game read; with the swimsuits off (SuitsOff), at most her
// costumes before the swimsuits (table maximum less her swimsuits). Never 0.
constexpr uint32_t Pool(uint32_t count, uint8_t table_max, uint8_t suits, bool suits_off) {
  const uint32_t c = std::max<uint32_t>(count, 1);
  if (!suits_off || suits == 0) return c;
  const uint32_t before_suits = table_max > suits ? uint32_t(table_max - suits) : 1u;
  return std::min(c, before_suits);
}

// A value in [0, n), every value equally likely, from 31-bit draws: draws at or past the largest multiple of n below 2^31
// are drawn again (at most kMaxTries, then the remainder of the last).
template <typename Draw31>
uint32_t UniformBelow(uint32_t n, Draw31&& draw31) {
  if (n <= 1) return 0;
  const uint32_t range = 0x80000000u;
  const uint32_t limit = range - range % n;
  uint32_t v = 0;
  for (uint32_t i = 0; i < kMaxTries; ++i) {
    v = uint32_t(draw31()) & 0x7FFFFFFFu;
    if (v < limit) return v % n;
  }
  return v % n;
}

}  // namespace doa4::random_costume
