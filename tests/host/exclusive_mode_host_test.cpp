// Host test for the SDK's exclusive fullscreen and output size decisions (sdk include/rex/ui/exclusive_fullscreen.h,
// include/rex/ui/display_mode.h; used by src/ui/presenter.cpp, src/ui/d3d12/d3d12_presenter.cpp and
// src/ui/window_win.cpp). Built and run by run_exclusive_mode_test.bat.
//
// The contracts (Task 9c and the Task 9 review's M4):
// - every update outcome maps to its actions (resize, block, retry, put the placement back);
// - SetFullscreenState(TRUE): DXGI_ERROR_NOT_CURRENTLY_AVAILABLE and a success that did not reach fullscreen are
//   "later", every other failure is final;
// - a whole-Hz refresh as Windows reports it matches a rational rate by floor or nearest;
// - the saved mode text: "" -> the desktop mode, an offered mode -> it, anything else -> the desktop mode with a reason;
//   a fractional refresh in the text matches the list's rational within 0.001 Hz;
// - the menu's list: one entry per mode text, largest first, the desktop mode included;
// - the windowed sizes: 16:9 sizes whose window fits the work area.

#include <rex/ui/display_mode.h>
#include <rex/ui/exclusive_fullscreen.h>

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

namespace ef = rex::ui::exclusive_fullscreen;
namespace dm = rex::ui::display_mode;

static bool Is(const ef::Actions& a, bool resize, bool block, bool retry, bool restore) {
  return a.resize == resize && a.block == block && a.retry == retry && a.restore_placement == restore;
}

static void TestActionsTable() {
  CHECK(Is(ef::ActionsFor(ef::Change::kUnchanged), false, false, false, false));
  CHECK(Is(ef::ActionsFor(ef::Change::kEntered), true, false, false, false));   // NOT the placement put back
  CHECK(Is(ef::ActionsFor(ef::Change::kLeft), true, false, false, true));
  CHECK(Is(ef::ActionsFor(ef::Change::kLost), true, true, false, true));
  CHECK(Is(ef::ActionsFor(ef::Change::kFailed), false, true, false, true));
  // the Step 0 fix: a refusal for now is NOT blocked, it is retried (and an entry that resized the window is undone)
  CHECK(Is(ef::ActionsFor(ef::Change::kRetryLater), false, false, true, true));
  CHECK(ef::kRetryTries == 8 && ef::kRetryIntervalMs == 500);
}

static void TestClassifyEnter() {
  using O = ef::EnterOutcome;
  // ex2v's refusal (887A0022) is "later", whatever GetFullscreenState says
  CHECK(ef::ClassifyEnter(0x887A0022u, false) == O::kRetryLater);
  CHECK(ef::ClassifyEnter(0x887A0022u, true) == O::kRetryLater);
  // other failures are final: DXGI_ERROR_INVALID_CALL, DXGI_ERROR_UNSUPPORTED, E_OUTOFMEMORY, DXGI_ERROR_DEVICE_REMOVED
  for (uint32_t hr : {0x887A0001u, 0x887A0004u, 0x8007000Eu, 0x887A0005u}) {
    CHECK(ef::ClassifyEnter(hr, false) == O::kFailed);
    CHECK(ef::ClassifyEnter(hr, true) == O::kFailed);
  }
  // S_OK: entered only if the state says so; a status code that did not reach fullscreen is "later"
  CHECK(ef::ClassifyEnter(0u, true) == O::kEntered);
  CHECK(ef::ClassifyEnter(0u, false) == O::kRetryLater);
  CHECK(ef::ClassifyEnter(0x087A0008u, false) == O::kRetryLater);  // DXGI_STATUS_MODE_CHANGE_IN_PROGRESS
  CHECK(ef::ClassifyEnter(0x087A0008u, true) == O::kEntered);
}

static void TestRefreshMatchesWholeHz() {
  // 59.94 (60000/1001) is reported as 59 or 60; not 58 or 61
  CHECK(ef::RefreshMatchesWholeHz(60000, 1001, 59) && ef::RefreshMatchesWholeHz(60000, 1001, 60));
  CHECK(!ef::RefreshMatchesWholeHz(60000, 1001, 58) && !ef::RefreshMatchesWholeHz(60000, 1001, 61));
  // the run's 164.834 (164834/1000): 164 or 165
  CHECK(ef::RefreshMatchesWholeHz(164834, 1000, 165) && ef::RefreshMatchesWholeHz(164834, 1000, 164));
  CHECK(!ef::RefreshMatchesWholeHz(164834, 1000, 166));
  // an exact rate: only itself (60/1: NOT 59)
  CHECK(ef::RefreshMatchesWholeHz(60, 1, 60) && !ef::RefreshMatchesWholeHz(60, 1, 59) &&
        !ef::RefreshMatchesWholeHz(60, 1, 61));
  // 59.6 rounds to 60, floors to 59: both; 59.4 rounds to 59: only 59
  CHECK(ef::RefreshMatchesWholeHz(596, 10, 60) && ef::RefreshMatchesWholeHz(596, 10, 59));
  CHECK(ef::RefreshMatchesWholeHz(594, 10, 59) && !ef::RefreshMatchesWholeHz(594, 10, 60));
  // denominator 0 (unspecified) cannot be checked: a match
  CHECK(ef::RefreshMatchesWholeHz(0, 0, 144) && ef::RefreshMatchesWholeHz(60, 0, 1));
}

static dm::DisplayMode M(uint32_t w, uint32_t h, uint32_t n, uint32_t d) { return {w, h, n, d}; }

static void TestModeText() {
  CHECK(dm::ModeText(M(1920, 1080, 60, 1)) == "1920x1080@60");
  CHECK(dm::ModeText(M(1920, 1080, 60000, 1001)) == "1920x1080@59.94");
  CHECK(dm::ModeText(M(2560, 1440, 164834, 1000)) == "2560x1440@164.834");
  CHECK(dm::ModeText(M(1280, 720, 1199, 20)) == "1280x720@59.95");
  CHECK(dm::ModeText(M(800, 600, 0, 0)) == "800x600@0");
}

static void TestParse() {
  uint32_t w = 0, h = 0;
  double hz = 0;
  CHECK(dm::ParseModeText("1920x1080@60", w, h, hz) && w == 1920 && h == 1080 && hz == 60.0);
  CHECK(dm::ParseModeText("1920x1080@59.94", w, h, hz) && hz > 59.9399 && hz < 59.9401);
  CHECK(dm::ParseModeText("2560x1440@164.834", w, h, hz) && hz > 164.8339 && hz < 164.8341);
  for (const char* bad : {"", "abc", "1920x1080", "1920x1080@", "-1920x1080@60", "1920x-1080@60", "0x1080@60",
                          "1920x0@60", "1920x1080@0", "1920x1080@-60", "1920x1080@60.", "1920x1080@60.1234",
                          "1920x1080@60Hz", " 1920x1080@60", "1920X1080@60", "1920x1080@@60", "123456x1080@60"}) {
    CHECK(!dm::ParseModeText(bad, w, h, hz));
  }
  CHECK(dm::ParseSizeText("1600x900", w, h) && w == 1600 && h == 900);
  for (const char* bad : {"", "1600", "1600x", "x900", "1600x900@60", "0x900", "1600x0", "16 00x900", "1600x900 "}) {
    CHECK(!dm::ParseSizeText(bad, w, h));
  }
}

static void TestChooseMode() {
  const dm::DisplayMode desktop = M(2560, 1440, 164834, 1000);
  const std::vector<dm::DisplayMode> offered = {M(2560, 1440, 164834, 1000), M(2560, 1440, 60000, 1000),
                                                M(1920, 1080, 60000, 1000), M(1920, 1080, 60000, 1001),
                                                M(1280, 720, 59940, 1000)};
  // "" -> the desktop mode, no reason
  dm::Choice c = dm::ChooseMode("", offered, desktop);
  CHECK(c.pick == dm::Pick::kDesktop && c.mode.refresh_num == 164834 && c.reason == nullptr);
  // an offered mode -> it (60 picks 60000/1000, NOT 60000/1001)
  c = dm::ChooseMode("1920x1080@60", offered, desktop);
  CHECK(c.pick == dm::Pick::kOffered && c.mode.width == 1920 && c.mode.refresh_den == 1000 && c.reason == nullptr);
  // the fractional one by its text (59.94 against 60000/1001 = 59.94006: within 0.001)
  c = dm::ChooseMode("1920x1080@59.94", offered, desktop);
  CHECK(c.pick == dm::Pick::kOffered && c.mode.refresh_den == 1001);
  // 59.94 against the list's 59940/1000 (exact)
  c = dm::ChooseMode("1280x720@59.94", offered, desktop);
  CHECK(c.pick == dm::Pick::kOffered && c.mode.width == 1280 && c.mode.refresh_num == 59940);
  // a refresh 0.01 off is another mode: not offered -> the desktop, with the reason
  c = dm::ChooseMode("1920x1080@59.95", offered, desktop);
  CHECK(c.pick == dm::Pick::kFallback && c.mode.width == 2560 && c.reason != nullptr);
  // a mode not offered
  c = dm::ChooseMode("3840x2160@60", offered, desktop);
  CHECK(c.pick == dm::Pick::kFallback && c.mode.refresh_num == 164834 && c.reason != nullptr &&
        std::string(c.reason).find("does not offer") != std::string::npos);
  // garbage -> the desktop with the "not a mode text" reason
  for (const char* bad : {"abc", "1920x1080", "1920x1080@", "-1x1@60", "0x0@60", "1920x1080@0"}) {
    c = dm::ChooseMode(bad, offered, desktop);
    CHECK(c.pick == dm::Pick::kFallback && c.mode.width == 2560 && c.reason != nullptr &&
          std::string(c.reason).find("not a mode text") != std::string::npos);
  }
  // an empty list: any text falls back
  c = dm::ChooseMode("1920x1080@60", {}, desktop);
  CHECK(c.pick == dm::Pick::kFallback);
  // a listed mode without a rate (den 0) never matches a text
  c = dm::ChooseMode("1024x768@60", {M(1024, 768, 0, 0)}, desktop);
  CHECK(c.pick == dm::Pick::kFallback);
}

static void TestSortedUniqueModes() {
  const dm::DisplayMode desktop = M(2560, 1440, 164834, 1000);
  // duplicates (the same text: scaling variants, 60000/1001 vs 59940/1000), unsorted, the desktop missing
  const std::vector<dm::DisplayMode> offered = {M(1280, 720, 60, 1), M(1920, 1080, 60000, 1001),
                                                M(1920, 1080, 59940, 1000), M(2560, 1440, 60, 1),
                                                M(1920, 1080, 144, 1), M(1280, 720, 60, 1), M(0, 0, 60, 1)};
  const std::vector<dm::DisplayMode> list = dm::SortedUniqueModes(offered, desktop);
  std::vector<std::string> texts;
  for (const dm::DisplayMode& m : list) texts.push_back(dm::ModeText(m));
  const std::vector<std::string> expected = {"2560x1440@164.834", "2560x1440@60", "1920x1080@144", "1920x1080@59.94",
                                             "1280x720@60"};
  CHECK(texts == expected);
  // the desktop already listed: once
  const std::vector<dm::DisplayMode> list2 = dm::SortedUniqueModes({desktop, desktop}, desktop);
  CHECK(list2.size() == 1);
}

static void TestFittingClientSizes() {
  // a 2560x1400 work area (taskbar 40), frame 16x39: 2560x1440 does NOT fit, 1920x1080 does
  auto s = dm::FittingClientSizes(2560, 1400, 16, 39);
  CHECK(s.size() == 3 && s[0].first == 1920 && s[1].first == 1600 && s[2].first == 1280);
  // exactly fitting counts: 1280+16 = 1296, 720+39 = 759
  s = dm::FittingClientSizes(1296, 759, 16, 39);
  CHECK(s.size() == 1 && s[0].first == 1280 && s[0].second == 720);
  s = dm::FittingClientSizes(1295, 759, 16, 39);
  CHECK(s.empty());
  // a 4K work area takes 3840x2160 only with room for the frame (NOT here: the largest is 3200x1800)
  s = dm::FittingClientSizes(3840, 2120, 16, 39);
  CHECK(!s.empty() && s[0].first == 3200);
  s = dm::FittingClientSizes(3856, 2199, 16, 39);
  CHECK(!s.empty() && s[0].first == 3840 && s.size() == 6);
}

int main() {
  TestActionsTable();
  TestClassifyEnter();
  TestRefreshMatchesWholeHz();
  TestModeText();
  TestParse();
  TestChooseMode();
  TestSortedUniqueModes();
  TestFittingClientSizes();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
