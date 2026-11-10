// Host test for the SDK's Present call choice (sdk include/rex/ui/present_choice.h, used by
// src/ui/d3d12/d3d12_presenter.cpp PaintAndPresentImpl). Built and run by run_present_choice_test.bat.
//
// The contract (Task 9b, with Task 9's exclusive fullscreen):
// - present_vsync off: today's call exactly: sync interval 0, the tearing flag when the swap chain allows tearing and is
//   not in exclusive fullscreen (the flag is an invalid call in exclusive fullscreen);
// - present_vsync on: sync interval 1 and never the tearing flag (DXGI_PRESENT_ALLOW_TEARING is valid only with sync
//   interval 0), in exclusive fullscreen too.

#include <rex/ui/present_choice.h>

#include <cstdio>
#include <initializer_list>

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

using rex::ui::present_choice::Choose;
using rex::ui::present_choice::PresentChoice;

static void TestVsyncOffIsTodaysCall() {
  // windowed or borderless, a tearing swap chain: interval 0 with the flag (variable refresh rate and tearing)
  const PresentChoice a = Choose(false, false, true);
  CHECK(a.sync_interval == 0 && a.allow_tearing);
  // a swap chain without tearing: interval 0, no flag
  const PresentChoice b = Choose(false, false, false);
  CHECK(b.sync_interval == 0 && !b.allow_tearing);
  // exclusive fullscreen: interval 0, never the flag, whatever the swap chain allows
  for (bool tearing : {false, true}) {
    const PresentChoice c = Choose(true, false, tearing);
    CHECK(c.sync_interval == 0);
    CHECK(!c.allow_tearing);
  }
}

static void TestVsyncOnWaitsWithoutTheFlag() {
  // every other input: interval 1 and NOT the flag (excluded wrong version: the flag kept from the swap chain's)
  for (bool exclusive : {false, true}) {
    for (bool tearing : {false, true}) {
      const PresentChoice c = Choose(exclusive, true, tearing);
      CHECK(c.sync_interval == 1);
      CHECK(!c.allow_tearing);
    }
  }
}

static void TestTheSwitchMovesOnlyTheInterval() {
  // the one input that tells "vsync drops the flag" from "vsync sets the interval": a tearing swap chain not exclusive
  const PresentChoice off = Choose(false, false, true), on = Choose(false, true, true);
  CHECK(off.sync_interval == 0 && on.sync_interval == 1);
  CHECK(off.allow_tearing && !on.allow_tearing);
  // in exclusive fullscreen the switch changes only the interval (the flag is off both ways)
  const PresentChoice ex_off = Choose(true, false, true), ex_on = Choose(true, true, true);
  CHECK(ex_off.allow_tearing == ex_on.allow_tearing && !ex_on.allow_tearing);
  CHECK(ex_off.sync_interval == 0 && ex_on.sync_interval == 1);
}

int main() {
  TestVsyncOffIsTodaysCall();
  TestVsyncOnWaitsWithoutTheFlag();
  TestTheSwitchMovesOnlyTheInterval();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
