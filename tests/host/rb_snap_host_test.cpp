// Host test of the pure parts of src/net/rb_snap.cpp (rollback milestone 2: the save / restore test).
// Built with DOA4_RB_HOST_TEST (no SDK). Prints "PASSED: N checks, 0 failures" or "FAILED: N checks, M failures".
#include "../../src/net/rb_snap.h"

#include <cstdio>
#include <string>
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
  std::string e;
  // the setting
  CHECK(!ParseSnapConfig("", &e).on && e.empty());
  CHECK(!ParseSnapConfig("0", &e).on && e.empty());
  CHECK(!ParseSnapConfig(" off ", &e).on && e.empty());
  SnapConfig c = ParseSnapConfig("1", &e);
  CHECK(c.on && c.at == 3600 && c.frames == 8 && c.every == 240 && c.count == 12 && e.empty());
  c = ParseSnapConfig("on", &e);
  CHECK(c.on);
  c = ParseSnapConfig("4000,6", &e);
  CHECK(c.on && c.at == 4000 && c.frames == 6 && c.every == 240 && c.count == 12);
  c = ParseSnapConfig(" 100 , 2 , 30 , 5 ", &e);
  CHECK(c.on && c.at == 100 && c.frames == 2 && c.every == 30 && c.count == 5);
  e.clear();
  CHECK(!ParseSnapConfig("4000", &e).on && !e.empty());  // frames missing
  e.clear();
  CHECK(!ParseSnapConfig("4000,0", &e).on && !e.empty());
  e.clear();
  CHECK(!ParseSnapConfig("4000,121", &e).on && !e.empty());
  e.clear();
  CHECK(!ParseSnapConfig("4000,8,10,0", &e).on && !e.empty());
  e.clear();
  CHECK(!ParseSnapConfig("4000,8,10,5,1", &e).on && !e.empty());
  e.clear();
  CHECK(!ParseSnapConfig("4000,x", &e).on && !e.empty());
  e.clear();
  CHECK(!ParseSnapConfig("-1,8", &e).on && !e.empty());

  // which pages a snapshot holds
  CHECK(!SnapPage(0, 0, 0));
  CHECK(SnapPage(Page(0x10000000), 0, 0));
  CHECK(SnapPage(Page(0x400D6000), 0, 0));
  CHECK(!SnapPage(Page(0x400D6000), Page(0x400D6000), Page(0x400DC000)));  // the device
  CHECK(SnapPage(Page(0x400DC000), Page(0x400D6000), Page(0x400DC000)));
  CHECK(!SnapPage(Page(0x7018F000), 0, 0));  // a thread stack
  CHECK(SnapPage(Page(0x83033000), 0, 0));   // XEX data
  CHECK(SnapPage(Page(0x90010000), 0, 0));
  CHECK(!SnapPage(Page(0xBCD60000), 0, 0));  // physical: GPU data
  CHECK(!SnapPage(Page(0xF7171000), 0, 0));

  // runs in address order
  std::vector<unsigned char> map(kPages, 0);
  CHECK(Runs(map.data(), map.size()).empty());
  map[5] = map[6] = map[7] = 1;
  map[2] = 1;
  map[kPages - 1] = 1;
  std::vector<Range> r = Runs(map.data(), map.size());
  CHECK(r.size() == 3);
  CHECK(r[0].first_page == 2 && r[0].pages == 1);
  CHECK(r[1].first_page == 5 && r[1].pages == 3);
  CHECK(r[2].first_page == kPages - 1 && r[2].pages == 1);

  // the per-boundary result text
  CHECK(EqualText({}) == "0 boundaries:");
  CHECK(EqualText({true, true, true}) == "3 boundaries: 0-2 equal");
  CHECK(EqualText({true, true, false, true}) == "4 boundaries: 0-1 equal, 2 differ, 3 equal");

  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
