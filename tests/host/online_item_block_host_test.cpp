// Host test for the block on MODE SELECT's DOA ONLINE item (src/doa4_online_item_block.h). Built and run by
// run_online_item_block_test.bat.
//
// The contract:
// - while the DOA ONLINE caption (0x4000d) was drawn visible this swap or within kRecentSwaps before, a pad read loses A
//   and START and keeps every other button;
// - any other item's caption (ids captured in ou_runs/sf/runs/sf1), the DOA ONLINE icon 0x50009 (drawn at other positions
//   while other items are selected) and an invisible submission of the caption never block;
// - after the cursor leaves the item, presses work again once kRecentSwaps have passed.

#include "../../src/doa4_online_item_block.h"

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

using doa4::online_item_block::Block;

constexpr uint16_t kA = 0x1000, kB = 0x2000, kStart = 0x0010, kLeft = 0x0004, kRight = 0x0008;

int main() {
  {  // never drawn: nothing blocked
    Block b;
    CHECK(b.Filter(kA, 0) == kA);
    CHECK(b.Filter(kA | kStart, 500) == (kA | kStart));
  }
  {  // the caption on screen: A and START dropped, the rest kept (not a whole-word rule)
    Block b;
    b.NoteDraw(0x4000d, true, 1656);
    CHECK(b.Filter(kA, 1656) == 0);
    CHECK(b.Filter(kStart, 1657) == 0);
    CHECK(b.Filter(kA | kRight, 1656) == kRight);
    CHECK(b.Filter(kA | kStart | kLeft | kB, 1657) == (kLeft | kB));
    CHECK(b.Filter(kB, 1656) == kB);
    CHECK(b.Filter(0, 1656) == 0);
    // within kRecentSwaps (2) of the last draw: blocked; past it: not
    CHECK(b.Filter(kA, 1658) == 0);
    CHECK(b.Filter(kA, 1659) == kA);
    // a pad read whose swap count is behind the draw's (another thread): blocked
    CHECK(b.Filter(kA, 1655) == 0);
  }
  {  // other items' captions (captured), the DOA ONLINE icon, an invisible caption: never block
    Block b;
    for (uint32_t s : {0x40002u, 0x40005u, 0x40006u, 0x40009u, 0x4000au, 0x4000eu, 0x40003u, 0x4000cu, 0x40013u, 0x4000fu,
                       0x40011u, 0x40004u, 0x50009u, 0x4000cu})
      b.NoteDraw(s, true, 1700);
    b.NoteDraw(0x4000d, false, 1700);
    CHECK(b.Filter(kA | kStart, 1700) == (kA | kStart));
  }
  {  // the cursor leaves: the caption is no longer drawn, presses come back after kRecentSwaps
    Block b;
    for (uint32_t swap = 1656; swap < 1720; ++swap) b.NoteDraw(0x4000d, true, swap);
    b.NoteDraw(0x40002, true, 1720);  // STORY's caption: the cursor moved right
    CHECK(b.Filter(kA, 1721) == 0);
    CHECK(b.Filter(kA, 1722) == kA);
    // back onto the item: blocked again
    b.NoteDraw(0x4000d, true, 1800);
    CHECK(b.Filter(kA, 1800) == 0);
  }
  if (g_failures == 0) {
    std::printf("PASSED: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
  return 1;
}
