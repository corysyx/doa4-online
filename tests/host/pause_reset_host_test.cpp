// Host test for the START-with-BACK pad rule (src/doa4_pause_reset.h, issue #4's pause streaks). Built and run by
// run_pause_reset_test.bat.
//
// The contract:
// - when a pad's START and BACK both go down on the same game frame, the game gets BACK on that frame and START on the
//   next frame the pad is read (even if START was let go meanwhile: the press is delayed, never lost);
// - every other order passes unchanged: BACK then START a frame later, START then BACK, either alone, either held from
//   before; and every other button always passes;
// - the decision is one per pad and frame: a second read of the same frame gets the same answer;
// - pads are judged apart, and a user index past the four pads passes unchanged.
//
// Measured in the game (ou_runs/vertex/hold: g3 part C glitches with one same-frame press, g6 part E is clean with BACK
// one frame before START). This test checks the rule; the streaks themselves are checked in the game.

#include "../../src/doa4_pause_reset.h"

#include <cstdio>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::fflush(stdout);                                        \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using doa4::pause_reset::Pads;

// XINPUT_GAMEPAD bits (xinput.h), not the header's constants.
constexpr uint16_t kStart = 0x0010, kBack = 0x0020, kA = 0x1000, kLeft = 0x0004, kY = 0x8000;

int main() {
  {  // a one-frame press of both: BACK now, START on the next frame, then nothing
    Pads p;
    CHECK(p.Filter(0, kStart | kBack, 100) == kBack);
    CHECK(p.Filter(0, 0, 101) == kStart);
    CHECK(p.Filter(0, 0, 102) == 0);
    CHECK(p.Delayed() == 1);
  }
  {  // both held: BACK first, then both, with no second START edge while held, and nothing after the release
    Pads p;
    CHECK(p.Filter(0, kStart | kBack, 100) == kBack);
    for (uint32_t f = 101; f <= 104; ++f) CHECK(p.Filter(0, kStart | kBack, f) == (kStart | kBack));
    CHECK(p.Filter(0, 0, 105) == 0);
    CHECK(p.Delayed() == 1);
  }
  {  // BACK, then START on the next frame (the order measured clean): unchanged, with BACK let go or still held
    Pads p;
    CHECK(p.Filter(0, kBack, 100) == kBack);
    CHECK(p.Filter(0, kStart, 101) == kStart);
    CHECK(p.Filter(0, 0, 102) == 0);
    CHECK(p.Filter(0, kBack, 200) == kBack);
    CHECK(p.Filter(0, kStart | kBack, 201) == (kStart | kBack));
    CHECK(p.Filter(0, 0, 202) == 0);
    CHECK(p.Delayed() == 0);
  }
  {  // START, then BACK on the next frame, with START let go or still held: unchanged
    Pads p;
    CHECK(p.Filter(0, kStart, 100) == kStart);
    CHECK(p.Filter(0, kBack, 101) == kBack);
    CHECK(p.Filter(0, 0, 102) == 0);
    CHECK(p.Filter(0, kStart, 200) == kStart);
    CHECK(p.Filter(0, kStart | kBack, 201) == (kStart | kBack));
    CHECK(p.Delayed() == 0);
  }
  {  // either alone, and the other buttons through a delayed press
    Pads p;
    CHECK(p.Filter(0, kStart, 10) == kStart);
    CHECK(p.Filter(0, 0, 11) == 0);
    CHECK(p.Filter(0, kBack, 12) == kBack);
    CHECK(p.Filter(0, 0, 13) == 0);
    CHECK(p.Filter(0, kStart | kBack | kA | kLeft, 20) == (kBack | kA | kLeft));
    CHECK(p.Filter(0, kY, 21) == (kY | kStart));
    CHECK(p.Filter(0, kY, 22) == kY);
  }
  {  // one decision per pad and frame: a second and third read of the same frame get the same answer
    Pads p;
    CHECK(p.Filter(0, kStart | kBack, 100) == kBack);
    CHECK(p.Filter(0, kStart | kBack, 100) == kBack);  // NOT START|BACK because BACK "was already down"
    CHECK(p.Filter(0, kStart | kBack | kA, 100) == (kBack | kA));
    CHECK(p.Filter(0, 0, 101) == kStart);
    CHECK(p.Filter(0, 0, 101) == kStart);  // the delayed START is on every read of its frame
    CHECK(p.Filter(0, 0, 102) == 0);
    CHECK(p.Delayed() == 1);
  }
  {  // a pad read twice in one frame with DIFFERENT values (not seen in the game: one poll a frame): the game was last given
     // nothing, so both buttons on the next frame go down together again. The withhold wins over the delayed START (the
     // game must never get the pair in one frame), and the START stays owed to the frame after.
    Pads p;
    CHECK(p.Filter(0, kStart | kBack, 100) == kBack);
    CHECK(p.Filter(0, 0, 100) == 0);
    CHECK(p.Filter(0, kStart | kBack, 101) == kBack);  // NOT START|BACK (the delayed START joining a BACK that goes down)
    CHECK(p.Filter(0, kStart | kBack, 102) == (kStart | kBack));
    CHECK(p.Filter(0, 0, 103) == 0);
  }
  {  // the pad is not read for a while: the delayed START comes on the next frame that reads it
    Pads p;
    CHECK(p.Filter(0, kStart | kBack, 100) == kBack);
    CHECK(p.Filter(0, 0, 130) == kStart);
    CHECK(p.Filter(0, 0, 131) == 0);
  }
  {  // again later: the rule has no memory of the earlier press
    Pads p;
    CHECK(p.Filter(0, kStart | kBack, 100) == kBack);
    CHECK(p.Filter(0, kStart | kBack, 101) == (kStart | kBack));
    CHECK(p.Filter(0, 0, 102) == 0);
    CHECK(p.Filter(0, kStart | kBack, 300) == kBack);
    CHECK(p.Filter(0, 0, 301) == kStart);
    CHECK(p.Delayed() == 2);
  }
  {  // the rule remembers what the GAME got, not the pad: after a delayed START (the game saw it on 101), START still
     // counts as down for the game on 102, so BACK joining it there is START-then-BACK (measured clean), not a new pair
    Pads p;
    CHECK(p.Filter(0, kStart | kBack, 100) == kBack);
    CHECK(p.Filter(0, 0, 101) == kStart);
    CHECK(p.Filter(0, kStart | kBack, 102) == (kStart | kBack));
    CHECK(p.Filter(0, 0, 103) == 0);
    CHECK(p.Delayed() == 1);
  }
  {  // held from before: BACK down for a while, then START (and the reverse): unchanged
    Pads p;
    CHECK(p.Filter(0, kBack, 100) == kBack);
    CHECK(p.Filter(0, kBack, 101) == kBack);
    CHECK(p.Filter(0, kBack | kStart, 102) == (kBack | kStart));
    CHECK(p.Filter(0, 0, 103) == 0);
    CHECK(p.Filter(0, kStart, 200) == kStart);
    CHECK(p.Filter(0, kStart, 201) == kStart);
    CHECK(p.Filter(0, kStart | kBack, 202) == (kStart | kBack));
    CHECK(p.Delayed() == 0);
  }
  {  // pads apart: one pad's START with another pad's BACK is not this rule; each pad keeps its own delayed press
    Pads p;
    CHECK(p.Filter(0, kStart, 100) == kStart);
    CHECK(p.Filter(1, kBack, 100) == kBack);
    CHECK(p.Filter(0, 0, 101) == 0);
    CHECK(p.Filter(1, 0, 101) == 0);
    CHECK(p.Filter(2, kStart | kBack, 200) == kBack);
    CHECK(p.Filter(3, kStart | kBack, 200) == kBack);
    CHECK(p.Filter(0, 0, 201) == 0);  // pad 0 owes nothing
    CHECK(p.Filter(2, 0, 201) == kStart);
    CHECK(p.Filter(3, kA, 201) == (kA | kStart));
    CHECK(p.Delayed() == 2);
  }
  {  // the very first frame a pad is read can be frame 0: it is decided like any other
    Pads p;
    CHECK(p.Filter(0, kStart | kBack, 0) == kBack);
    CHECK(p.Filter(0, 0, 1) == kStart);
  }
  {  // a user index past the four pads: unchanged, no state
    Pads p;
    CHECK(p.Filter(4, kStart | kBack, 100) == (kStart | kBack));
    CHECK(p.Filter(0xFFFFFFFFu, kStart | kBack, 100) == (kStart | kBack));
    CHECK(p.Delayed() == 0);
  }
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
