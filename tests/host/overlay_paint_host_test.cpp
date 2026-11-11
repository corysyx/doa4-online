// Host test for src/doa4_overlay_paint.h (the overlay's paint gate). Built and run by run_overlay_paint_test.bat.

#include "../../src/doa4_overlay_paint.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using doa4::overlay_paint::Content;
using doa4::overlay_paint::Gate;
using doa4::overlay_paint::kSettleDraws;

static Content Fps(const char* text) {
  Content c;
  c.fps = text;
  return c;
}

// A new overlay has drawn nothing: it needs a paint even for empty content (it was just created to show something).
static void TestFresh() {
  Gate g;
  CHECK(g.NeedsPaint(Content{}));
  CHECK(g.NeedsPaint(Fps("FPS 60")));
}

// One content keeps asking for paints until it has been drawn kSettleDraws times (ImGui hides a new auto-resized window
// for its first frame), then stops: the wrong gate that compares content only would stop after the first draw.
static void TestSettle() {
  CHECK(kSettleDraws == 3);
  Gate g;
  const Content c = Fps("FPS 60");
  CHECK(g.Drew(c));  // new content
  CHECK(g.NeedsPaint(c));
  CHECK(!g.Drew(c));  // the same content again
  CHECK(g.NeedsPaint(c));
  CHECK(!g.Drew(c));
  CHECK(!g.NeedsPaint(c));  // drawn 3 times: settled, no paint of its own
  CHECK(!g.Drew(c));        // more draws (guest frames) keep it settled
  CHECK(!g.NeedsPaint(c));
}

// A change after settling needs paints again, and the count starts over for the new content (a gate that kept counting
// across contents would say settled after one draw of the new banner).
static void TestChangeRestartsCount() {
  Gate g;
  const Content a = Fps("FPS 60");
  for (int i = 0; i < kSettleDraws; ++i) g.Drew(a);
  CHECK(!g.NeedsPaint(a));
  Content b = a;
  b.banner = "Waiting for the other player...";
  CHECK(g.NeedsPaint(b));  // not drawn yet: a hold with no guest frame must still get its paint
  CHECK(g.Drew(b));
  CHECK(g.NeedsPaint(b));  // drawn once: ImGui's hidden measuring frame
  CHECK(!g.Drew(b));
  CHECK(!g.Drew(b));
  CHECK(!g.NeedsPaint(b));
  // Back to a: a change again.
  CHECK(g.NeedsPaint(a));
  CHECK(g.Drew(a));
}

// Every field takes part in the comparison (a gate that ignored one would never paint its change during a hold).
static void TestEveryField() {
  Content base;
  base.banner = "b";
  base.fps = "FPS 60";
  base.stats = {"Ping 17 ms", "Delay 2 frames (33 ms)"};
  base.notice = "Update available: 0.3.2";
  Gate g;
  for (int i = 0; i < kSettleDraws; ++i) g.Drew(base);
  CHECK(!g.NeedsPaint(base));
  Content c = base;
  c.banner = "";
  CHECK(g.NeedsPaint(c));
  c = base;
  c.fps = "FPS 59";
  CHECK(g.NeedsPaint(c));
  c = base;
  c.stats[1] = "Delay 3 frames (50 ms)";
  CHECK(g.NeedsPaint(c));
  c = base;
  c.stats.push_back("x");
  CHECK(g.NeedsPaint(c));
  c = base;
  c.notice = "";
  CHECK(g.NeedsPaint(c));
  CHECK(!g.NeedsPaint(base));  // and the unchanged content still needs none
}

using doa4::overlay_paint::HoldWatch;

// A slow but live game (25 fps: the count moves every tick) never gets a paint of its own, however long the overlay
// stays unsettled (review I1: a gate keyed on "not settled for two ticks" fired here).
static void TestSlowGameIsNotAHold() {
  HoldWatch h;
  unsigned swaps = 100;
  for (int tick = 0; tick < 20; ++tick) {
    CHECK(!h.Tick(true, swaps));
    swaps += 2;  // 2-3 guest frames per 100 ms tick at 25 fps
  }
}

// A hold: the count stops. The first tick has no previous count to compare with; from the next tick on, a paint is
// due on every tick while the overlay needs one, and the streak starts once.
static void TestHold() {
  HoldWatch h;
  CHECK(!h.Tick(true, 500));  // first tick ever: no baseline
  CHECK(h.Tick(true, 500));   // no frame for a tick
  CHECK(h.StreakStarted());
  CHECK(h.Tick(true, 500));
  CHECK(!h.StreakStarted());  // same hold: no second log line
  CHECK(!h.Tick(false, 500));  // settled during the hold: nothing due
  CHECK(h.Tick(true, 500));    // the next change in the same hold (a new banner): due again, a new streak
  CHECK(h.StreakStarted());
  CHECK(!h.Tick(true, 501));  // a frame came: not a hold any more
  CHECK(h.Tick(true, 501));   // and stopped again
}

// No overlay (needs_paint false) still keeps the count current: the tick after the overlay appears compares with the
// previous tick, not with a count from before the overlay existed.
static void TestCountKeptWithoutOverlay() {
  HoldWatch h;
  CHECK(!h.Tick(false, 10));
  CHECK(!h.Tick(false, 20));
  CHECK(!h.Tick(true, 30));  // frames are coming
  CHECK(h.Tick(true, 30));   // now they stopped
}

// The first tick has no previous tick even when the count is 0 (the boot: no guest frame yet), so nothing is due on it.
static void TestFirstTickAtZero() {
  HoldWatch h;
  CHECK(!h.Tick(true, 0u));
  CHECK(h.Tick(true, 0u));
}

// The 32-bit count wraps: a wrap is progress.
static void TestWrap() {
  HoldWatch h;
  CHECK(!h.Tick(true, 0xFFFFFFFFu));
  CHECK(!h.Tick(true, 0u));
  CHECK(h.Tick(true, 0u));
}

int main() {
  TestFresh();
  TestSettle();
  TestChangeRestartsCount();
  TestEveryField();
  TestSlowGameIsNotAHold();
  TestHold();
  TestCountKeptWithoutOverlay();
  TestFirstTickAtZero();
  TestWrap();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
