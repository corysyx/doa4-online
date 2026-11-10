// Host test of the pure parts of src/net/rb_wmap.cpp (rollback milestone 1: the guest write map).
// Built with DOA4_RB_HOST_TEST (no SDK). Prints "PASSED: N checks, 0 failures" or "FAILED: N checks, M failures".
#include "../../src/net/rb_wmap.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int g_checks = 0, g_failures = 0;
#define CHECK(x)                                                         \
  do {                                                                   \
    ++g_checks;                                                          \
    if (!(x)) {                                                          \
      ++g_failures;                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);           \
    }                                                                    \
  } while (0)

using namespace doa4::rb;

static uint32_t Page(uint32_t address) { return address >> kPageShift; }

int main() {
  std::vector<unsigned char> map(kPages, 0);

  // areas
  CHECK(AreaOf(Page(0x00001000)) == Area::kHeap4K);
  CHECK(AreaOf(Page(0x40000000)) == Area::kHeap64K);
  CHECK(AreaOf(Page(0x70010000)) == Area::kStacks);
  CHECK(AreaOf(Page(0x82B70000)) == Area::kXexData);
  CHECK(AreaOf(Page(0x835AF000)) == Area::kXexData);
  CHECK(AreaOf(Page(0x835B0000)) == Area::kXexOther);
  CHECK(AreaOf(Page(0x82000000)) == Area::kXexOther);
  CHECK(AreaOf(Page(0x90000000)) == Area::kHeap90);
  CHECK(AreaOf(Page(0xA0000000)) == Area::kPhysical);
  CHECK(AreaOf(kPages - 1) == Area::kPhysical);
  for (size_t a = 0; a < size_t(Area::kCount); ++a) CHECK(std::strcmp(AreaName(Area(a)), "?") != 0);

  // empty map
  CHECK(Ranges(map.data(), map.size(), 10).empty());
  CHECK(Count(map.data(), map.size()).total == 0);

  // three runs: 2 pages at 0x40000000, 5 at 0x82B70000, 2 at 0xA0000000 (a tie with the first: lower address first)
  for (uint32_t p = 0; p < 2; ++p) map[Page(0x40000000) + p] = 1;
  for (uint32_t p = 0; p < 5; ++p) map[Page(0x82B70000) + p] = 1;
  for (uint32_t p = 0; p < 2; ++p) map[Page(0xA0000000) + p] = 1;
  std::vector<Range> r = Ranges(map.data(), map.size(), 10);
  CHECK(r.size() == 3);
  CHECK(r[0].first_page == Page(0x82B70000) && r[0].pages == 5);
  CHECK(r[1].first_page == Page(0x40000000) && r[1].pages == 2);
  CHECK(r[2].first_page == Page(0xA0000000) && r[2].pages == 2);
  CHECK(Ranges(map.data(), map.size(), 1).size() == 1);
  CHECK(RangeText(r[0]) == "0x82B70000 +20 KB");

  const Census c = Count(map.data(), map.size());
  CHECK(c.total == 9);
  CHECK(c.by_area[size_t(Area::kXexData)] == 5);
  CHECK(c.by_area[size_t(Area::kHeap64K)] == 2);
  CHECK(c.by_area[size_t(Area::kPhysical)] == 2);
  CHECK(c.by_area[size_t(Area::kHeap4K)] == 0);

  // a run that reaches the last page
  map[kPages - 1] = 1;
  map[kPages - 2] = 1;
  r = Ranges(map.data(), map.size(), 10);
  bool last = false;
  for (const Range& x : r) last = last || (x.first_page == kPages - 2 && x.pages == 2);
  CHECK(last);

  // the map the generated code writes exists and is 1 MB
  CHECK(sizeof doa4_wmap == kPages);

  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
