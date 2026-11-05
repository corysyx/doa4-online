// Host test for src/doa4_display.cpp. Built and run by run_display_test.bat.

#include "../../src/doa4_display.h"

#include <cstdio>
#include <string>

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

using namespace doa4::display;

static void TestFpsMeter() {
  FpsMeter m;
  CHECK(m.Text() == "FPS --");
  m.Sample(1000, 10.0);
  CHECK(m.Text() == "FPS --");  // one sample is no rate
  m.Sample(1010, 10.25);
  CHECK(m.Text() == "FPS --");  // a quarter of a second is not a window yet
  m.Sample(1030, 10.5);         // 30 swaps in 0.5 s, from the first sample and not from the last (NOT 80)
  CHECK(m.Text() == "FPS 60");
  m.Sample(1044, 11.0);  // 14 swaps in 0.5 s: the window restarted at the last value, rounded
  CHECK(m.Text() == "FPS 28");
  m.Sample(1059, 11.5 + 0.0);  // 15 swaps in 0.5 s
  CHECK(m.Text() == "FPS 30");
  // Inside a window the shown value stays.
  m.Sample(5000, 11.6);
  CHECK(m.Text() == "FPS 30");

  // 59.9 rounds to 60; a long gap (the window was not painted) is averaged over the gap.
  FpsMeter r;
  r.Sample(0, 0.0);
  r.Sample(599, 10.0);
  CHECK(r.Text() == "FPS 60");

  // The counter wraps at 2^32.
  FpsMeter w;
  w.Sample(0xFFFFFFF0u, 1.0);
  w.Sample(14, 1.5);  // 30 swaps
  CHECK(w.Text() == "FPS 60");

  // A stopped game reads 0, not the last value.
  FpsMeter s;
  s.Sample(10, 1.0);
  s.Sample(40, 1.5);
  CHECK(s.Text() == "FPS 60");
  s.Sample(40, 2.0);
  CHECK(s.Text() == "FPS 0");
}

static void TestCursor() {
  // Every combination of the five inputs.
  for (int bits = 0; bits < 32; ++bits) {
    CursorInputs in;
    in.focused = bits & 1;
    in.menu_open = bits & 2;
    in.setup_open = bits & 4;
    in.sdk_overlay_open = bits & 8;
    in.mnk_mode = bits & 16;
    const CursorAction expected = in.mnk_mode ? CursorAction::kLeave
                                  : (in.focused && !in.menu_open && !in.setup_open && !in.sdk_overlay_open)
                                      ? CursorAction::kHide
                                      : CursorAction::kShow;
    CHECK(DecideCursor(in) == expected);
  }
  // The named cases, spelled out.
  CursorInputs playing;
  playing.focused = true;
  CHECK(DecideCursor(playing) == CursorAction::kHide);
  CursorInputs away;
  CHECK(DecideCursor(away) == CursorAction::kShow);
  CursorInputs menu = playing;
  menu.menu_open = true;
  CHECK(DecideCursor(menu) == CursorAction::kShow);
}

int main() {
  TestFpsMeter();
  TestCursor();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
