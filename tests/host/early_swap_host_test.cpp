// Host test for the SDK's present_before_vblank_wait decision at the swap packet (sdk include/rex/graphics/early_swap.h,
// used by src/graphics/command_processor.cpp ExecutePacketType3_XE_SWAP). Built and run by run_early_swap_test.bat.
//
// The contract (Task 8 review M1): every guest swap is counted in swap_count_ exactly once, whichever path presents it
// (swap_count's readers: the photo snapshot watcher's "gpu swaps >= n" and the FPS meter); a swap is skipped at its
// packet only when it was presented early AND the packet is the swap presented.

#include <rex/graphics/early_swap.h>

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

using rex::graphics::early_swap::AtSwapPacket;
using rex::graphics::early_swap::PacketAction;

// The count one swap gets over both points: the early present counts it when presented_early, the packet when
// count_swap. Wrong version excluded: the present-again path counting again (2 for one guest swap).
static int CountsForOneSwap(bool presented_early, bool matches) {
  const PacketAction a = AtSwapPacket(presented_early, matches);
  return (presented_early ? 1 : 0) + (a.count_swap ? 1 : 0);
}

static void TestNotPresentedEarly() {
  // the base path (cut off, no sleeping wait, a peek that refused, or an early IssueSwap that did not refresh):
  // the packet presents and counts, whatever the match says
  for (bool matches : {false, true}) {
    const PacketAction a = AtSwapPacket(false, matches);
    CHECK(a.issue_swap && a.count_swap);
    CHECK(CountsForOneSwap(false, matches) == 1);
  }
}

static void TestPresentedEarlyAndMatching() {
  const PacketAction a = AtSwapPacket(true, true);
  CHECK(!a.issue_swap);  // NOT presented a second time
  CHECK(!a.count_swap);
  CHECK(CountsForOneSwap(true, true) == 1);
}

static void TestPresentedEarlyButNotMatching() {
  // the present-again path: the packet's picture is shown, but the swap was counted at the early present already
  const PacketAction a = AtSwapPacket(true, false);
  CHECK(a.issue_swap);
  CHECK(!a.count_swap);
  CHECK(CountsForOneSwap(true, false) == 1);  // NOT 2
}

int main() {
  TestNotPresentedEarly();
  TestPresentedEarlyAndMatching();
  TestPresentedEarlyButNotMatching();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
