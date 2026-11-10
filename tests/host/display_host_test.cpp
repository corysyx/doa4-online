// Host test for src/doa4_display.cpp. Built and run by run_display_test.bat.

#include "../../src/doa4_display.h"

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

// The name the user chose for the window (rebrand 2026-11-05); the exe keeps its name.
static void TestWindowTitle() { CHECK(std::string(kWindowTitle) == "Dead or Alive 4 Online"); }

// The Output size row's lists (Game tab).
static std::vector<std::string> Labels(const OutputSizeList& l) {
  std::vector<std::string> out;
  for (const auto& e : l.entries) out.push_back(e.label);
  return out;
}

static void TestRefreshText() {
  CHECK(RefreshText(60.0) == "60");
  CHECK(RefreshText(59.94) == "59.94");
  CHECK(RefreshText(59.940059) == "59.94");
  CHECK(RefreshText(164.834) == "164.83");
  CHECK(RefreshText(59.9) == "59.90");  // fractional: two decimals
  CHECK(RefreshText(59.999) == "60");   // rounds to a whole rate
  CHECK(RefreshText(144.0) == "144");
}

static void TestWindowedSizeList() {
  // A work area that fits only 1280x720 and 1600x900 (the SDK gives them largest first); the current size is one of them.
  OutputSizeList l = WindowedSizeList({{1600, 900}, {1280, 720}}, 1600, 900);
  CHECK((Labels(l) == std::vector<std::string>{"1280 x 720", "1600 x 900"}));
  CHECK(l.selected == 1);
  CHECK(l.entries[0].value == "1280x720" && l.entries[0].width == 1280 && l.entries[0].height == 720);
  CHECK(l.entries[1].value == "1600x900");

  // A current size that is not in the list is shown, selected, at its place by size.
  l = WindowedSizeList({{1920, 1080}, {1600, 900}, {1280, 720}}, 1700, 950);
  CHECK((Labels(l) == std::vector<std::string>{"1280 x 720", "1600 x 900", "1700 x 950", "1920 x 1080"}));
  CHECK(l.selected == 2);
  CHECK(l.entries[2].value == "1700x950");
  // ...also below and above every entry.
  l = WindowedSizeList({{1600, 900}, {1280, 720}}, 800, 600);
  CHECK(l.selected == 0 && l.entries.size() == 3 && l.entries[0].label == "800 x 600");
  l = WindowedSizeList({{1600, 900}, {1280, 720}}, 3000, 2000);
  CHECK(l.selected == 2 && l.entries.size() == 3 && l.entries[2].label == "3000 x 2000");

  // An empty SDK list (no window yet) still shows the current size; an unknown current size selects nothing.
  l = WindowedSizeList({}, 1280, 720);
  CHECK((Labels(l) == std::vector<std::string>{"1280 x 720"}) && l.selected == 0);
  l = WindowedSizeList({{1280, 720}}, 0, 0);
  CHECK(l.entries.size() == 1 && l.selected == -1);
  // A duplicate in the SDK list is one entry.
  l = WindowedSizeList({{1280, 720}, {1280, 720}}, 1280, 720);
  CHECK(l.entries.size() == 1);
}

static void TestExclusiveSizeList() {
  const std::vector<ExclusiveMode> modes = {{2560, 1440, 164.834, "2560x1440@164.834"},
                                            {1920, 1080, 59.94, "1920x1080@59.94"},
                                            {1920, 1080, 60.0, "1920x1080@60"}};
  // Saved "": the desktop entry first and selected; a fractional rate shows two decimals, a whole one none.
  OutputSizeList l = ExclusiveSizeList(modes, "");
  CHECK((Labels(l) == std::vector<std::string>{"Desktop (current mode)", "2560 x 1440 @ 164.83 Hz",
                                                "1920 x 1080 @ 59.94 Hz", "1920 x 1080 @ 60 Hz"}));
  CHECK(l.selected == 0);
  CHECK(l.entries[0].value.empty());
  CHECK(l.entries[2].value == "1920x1080@59.94");  // the saved text is the SDK's, not the label's
  CHECK(l.entries[2].width == 1920 && l.entries[2].height == 1080);
  // A saved mode is selected by its text.
  l = ExclusiveSizeList(modes, "1920x1080@60");
  CHECK(l.selected == 3);
  l = ExclusiveSizeList(modes, "1920x1080@59.94");
  CHECK(l.selected == 2);
  // A saved mode the monitor does not offer falls back to the desktop entry, as the SDK does.
  l = ExclusiveSizeList(modes, "800x600@60");
  CHECK(l.selected == 0);
  // No modes at all: the desktop entry alone.
  l = ExclusiveSizeList({}, "1920x1080@60");
  CHECK(l.entries.size() == 1 && l.selected == 0 && l.entries[0].label == "Desktop (current mode)");
}

// Task 10 fix round 1 (review I1): when the menu builds the exclusive mode list. The full table, then the cases by name.
static void TestExclusiveModesStep() {
  // arguments: (first_frame, attempted this opening, built, from_active, exclusive_active)
  int builds = 0, builds_after_attempt = 0;
  for (int first = 0; first < 2; ++first) {
    for (int attempted = 0; attempted < 2; ++attempted) {
      for (int built = 0; built < 2; ++built) {
        for (int from_active = 0; from_active < 2; ++from_active) {
          for (int active = 0; active < 2; ++active) {
            const bool build = ExclusiveModesStep(first, attempted, built, from_active, active) == ModesStep::kBuild;
            const bool want = !attempted && (active ? !built : (first || !built || from_active));
            CHECK(build == want);
            builds += build ? 1 : 0;
            builds_after_attempt += (build && attempted) ? 1 : 0;
          }
        }
      }
    }
  }
  // not yet attempted: exclusive off 7 of 8 (all but a later frame with a list made while off), exclusive on the 4
  // without a list; once attempted in this opening: never
  CHECK(builds == 7 + 4 && builds_after_attempt == 0);
  // exclusive off, the opening's first frame: a fresh list, even with one kept
  CHECK(ExclusiveModesStep(true, false, true, false, false) == ModesStep::kBuild);
  // exclusive off, later frames: use the list this opening made
  CHECK(ExclusiveModesStep(false, true, true, false, false) == ModesStep::kUse);
  // exclusive off with only a list made during exclusive, not yet attempted this opening: replace it
  CHECK(ExclusiveModesStep(false, false, true, true, false) == ModesStep::kBuild);
  // the I1 case: an opening while exclusive at a non-desktop mode, a list from before the entry kept: use it, NEVER list
  // during exclusive (its desktop entry would be the scaled 2560x1440 @ 59 Hz)
  CHECK(ExclusiveModesStep(true, false, true, false, true) == ModesStep::kUse);
  CHECK(ExclusiveModesStep(false, false, true, false, true) == ModesStep::kUse);
  // the game started in exclusive: no list yet, build one during exclusive (the desktop entry dropped), then use it
  CHECK(ExclusiveModesStep(true, false, false, false, true) == ModesStep::kBuild);
  CHECK(ExclusiveModesStep(false, true, true, true, true) == ModesStep::kUse);
  // re-review 2: a listing that failed (attempted, still nothing built) is NOT tried again on the next frames of the
  // opening, exclusive off or on; the next opening (attempted false again) tries once more
  CHECK(ExclusiveModesStep(false, true, false, false, false) == ModesStep::kUse);
  CHECK(ExclusiveModesStep(false, true, false, false, true) == ModesStep::kUse);
  CHECK(ExclusiveModesStep(true, false, false, false, false) == ModesStep::kBuild);
}

static void TestDropModeText() {
  std::vector<ExclusiveMode> modes = {{2560, 1440, 164.834, "2560x1440@164.834"},
                                      {2560, 1440, 59.951, "2560x1440@59.951"},
                                      {1920, 1080, 60.0, "1920x1080@60"}};
  DropModeText(modes, "2560x1440@59.951");
  CHECK(modes.size() == 2 && modes[0].text == "2560x1440@164.834" && modes[1].text == "1920x1080@60");
  DropModeText(modes, "1280x720@60");  // not listed: nothing changes
  CHECK(modes.size() == 2);
  // the dropped entry is no longer offered by the row; "Desktop (current mode)" still leads it
  const OutputSizeList l = ExclusiveSizeList(modes, "");
  CHECK(l.entries.size() == 3 && l.entries[0].value.empty());
  for (const OutputSizeEntry& e : l.entries) CHECK(e.value != "2560x1440@59.951");
}

int main() {
  TestRefreshText();
  TestWindowedSizeList();
  TestExclusiveSizeList();
  TestExclusiveModesStep();
  TestDropModeText();
  TestWindowTitle();
  TestFpsMeter();
  TestCursor();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
