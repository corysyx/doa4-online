// Host test for src/net/np_vsm_pure.cpp (worker determinism Plan 3, Task C1: the vertex-stream manager's two lists put into one form
// after the frame reset, sorted below the count and zero past it, and a hash of their content). Built and run by
// run_np_vsm_test.bat; "np_vsm_host_test.exe -v" prints each group.
//
// Expected values: every list offset, count offset and expected hash is taken from np_runs/wd3/study/b/vsm_fixture_values.out (a model
// written from the design's words, not from this C++); the line of that file is cited beside each value as "out:N". The frame loop's
// return address is from generated/default/dead_or_alive_4_recomp.17.cpp:72341 ("ctx.lr = 0x824F4C3C;" before the one call of
// sub_82732950). Pointer values are made up; no game bytes. Every Pass fixture lives in a buffer of 1536 + 8 bytes whose last 8 bytes are
// a guard, checked after every call. Likeliest wrong versions, each separated by a named group: a sort over all 192 slots, or no clearing
// of the tail, or clearing from count + 1 (tail_smaller_than_live, count_zero_clears); a sort on host-order words (byte_order); equal
// neighbours counted as out of order (equal_neighbours_in_order); duplicates counted across the two lists (one_buffer_in_both_lists) or
// not at all (one_buffer_twice); only list 0 handled (second_list); a write with write off (write_off); a count of 193 accepted, or list
// 0 handled before list 1's count is checked (count_above_192); a hash restarted at every pass, over the order found, over 192 slots or
// without the counts (hash_pins, two_orders_one_form); a stand-down check missing or out of order (stand_down_table).

#include "../../src/net/np_vsm_pure.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

namespace vsm = doa4::np::vsm;

static int g_checks = 0;
static int g_failures = 0;
static bool g_verbose = false;
static const char* g_group = "";
#define CHECK(...)                                                                        \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(__VA_ARGS__)) {                                                                 \
      std::printf("FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, g_group, #__VA_ARGS__);    \
      std::fflush(stdout);                                                                \
      ++g_failures;                                                                       \
    }                                                                                     \
  } while (0)

static void Group(const char* name) {
  g_group = name;
  if (g_verbose) std::printf("%s\n", name);
}

// ---- the buffer: list 0 (192 words), list 1 (192 words), then the 8-byte guard (out:23: 192 slots a list, 1536 bytes together) ----
constexpr size_t kListBytes = 1536;
constexpr size_t kBufBytes = kListBytes + 8;
using Buf = std::array<uint8_t, kBufBytes>;
static_assert(sizeof(Buf) == kBufBytes, "the guard must sit right after the 1536 list bytes, with nothing between");
static const uint8_t kGuard[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF};

static size_t At(int list, int slot) { return size_t(list) * 768 + size_t(slot) * 4; }

static void Put(Buf& b, int list, int slot, uint32_t w) {  // big-endian, as the guest stores it
  const size_t at = At(list, slot);
  b[at] = uint8_t(w >> 24);
  b[at + 1] = uint8_t(w >> 16);
  b[at + 2] = uint8_t(w >> 8);
  b[at + 3] = uint8_t(w);
}

static uint32_t Get(const Buf& b, int list, int slot) {
  const size_t at = At(list, slot);
  return (uint32_t(b[at]) << 24) | (uint32_t(b[at + 1]) << 16) | (uint32_t(b[at + 2]) << 8) | uint32_t(b[at + 3]);
}

static Buf Make(std::initializer_list<uint32_t> l0, std::initializer_list<uint32_t> l1) {
  Buf b{};
  std::memcpy(b.data() + kListBytes, kGuard, 8);
  int i = 0;
  for (uint32_t w : l0) Put(b, 0, i++, w);
  i = 0;
  for (uint32_t w : l1) Put(b, 1, i++, w);
  return b;
}

static bool GuardOk(const Buf& b) { return std::memcmp(b.data() + kListBytes, kGuard, 8) == 0; }

static bool ZeroFrom(const Buf& b, int list, int from) {  // slots from..191 of the list are zero
  for (int s = from; s < 192; ++s) {
    if (Get(b, list, s) != 0) return false;
  }
  return true;
}

struct Out {
  vsm::PassResult r;
  uint64_t hash = 0;
};

// One call of the code under test; always adds exactly one check (the guard).
static Out Run(Buf& b, uint32_t c0, uint32_t c1, bool write, uint64_t start = 0xCBF29CE484222325ull /* out:10 */) {
  Out o;
  o.hash = start;
  o.r = vsm::Pass(b.data(), c0, c1, write, &o.hash);
  CHECK(GuardOk(b));
  return o;
}

static bool SameCounters(const vsm::PassResult& a, const vsm::PassResult& b) {
  return a.ok == b.ok && a.unsorted == b.unsorted && a.cleared == b.cleared && a.duplicates == b.duplicates;
}

// ---- the fixtures the plan's write_off group reruns ----
struct Fixture {
  const char* name;
  Buf b;
  uint32_t c0, c1;
};

static Fixture FixA() { return {"A", Make({0x83A01000, 0x83501000, 0x83901000, 0x83001000, 0x83501000}, {}), 3, 0}; }
static Fixture FixB() { return {"B", Make({0x83500100, 0x83500002}, {}), 2, 0}; }
static Fixture FixC() { return {"C", Make({0x83501000}, {}), 0, 0}; }
static Fixture FixD() {
  Fixture f{"D", Make({}, {0x83500002}), 192, 1};
  for (int i = 0; i < 192; ++i) Put(f.b, 0, i, 0x83600000u - 16u * uint32_t(i));
  return f;
}
static Fixture FixG() { return {"G", Make({0x83501000, 0x83901000, 0x83501000}, {}), 3, 0}; }
static Fixture FixSecond() { return {"second_list", Make({}, {0x83500100, 0x83500002, 0x83001000}), 0, 2}; }

// ---- groups ----

static void TestConstants() {
  Group("constants");
  CHECK(vsm::kManager == 0x835780E0u);                // out:24
  CHECK(vsm::kOffLists == 5420u);                     // out:18 (list 0: M+5420)
  CHECK(vsm::kOffCount0 == 6956u);                    // out:20
  CHECK(vsm::kOffCount1 == 6960u);                    // out:21
  CHECK(vsm::kSlots == 192);                          // out:23
  CHECK(vsm::kOffCount0 - vsm::kOffLists == 1536u);   // out:23 (list bytes together; list 1 ends at count 0)
  CHECK(vsm::kLrFrameLoop == 0x824F4C3Cu);            // 17.cpp:72341
  CHECK(vsm::kHashStart == 0xCBF29CE484222325ull);    // out:10
}

static void TestTailSmallerThanLive() {
  Group("tail_smaller_than_live (A)");
  Fixture f = FixA();
  const Out o = Run(f.b, f.c0, f.c1, true);
  CHECK(o.r.ok);
  CHECK(Get(f.b, 0, 0) == 0x83501000u && Get(f.b, 0, 1) == 0x83901000u && Get(f.b, 0, 2) == 0x83A01000u);  // out:1
  CHECK(ZeroFrom(f.b, 0, 3));
  CHECK(ZeroFrom(f.b, 1, 0));
  CHECK(o.r.unsorted == 1);
  CHECK(o.r.cleared == 2);
  CHECK(o.r.duplicates == 0);  // 83501000 also sits in slot 4, past the count: not a duplicate
}

static void TestByteOrder() {
  Group("byte_order (B)");
  Fixture f = FixB();
  // The fixture's point (out:3): read in host (little-endian) order the two words are already rising, so a sort on raw host words leaves
  // them as they are and is told apart here.
  uint32_t host0 = 0, host1 = 0;
  std::memcpy(&host0, f.b.data() + At(0, 0), 4);
  std::memcpy(&host1, f.b.data() + At(0, 1), 4);
  CHECK(host0 == 0x00015083u && host1 == 0x02005083u && host0 < host1);
  const Out o = Run(f.b, f.c0, f.c1, true);
  CHECK(o.r.ok);
  CHECK(Get(f.b, 0, 0) == 0x83500002u && Get(f.b, 0, 1) == 0x83500100u);  // out:2
  CHECK(ZeroFrom(f.b, 0, 2));
  CHECK(o.r.unsorted == 1);
  CHECK(o.r.cleared == 0);
  CHECK(o.r.duplicates == 0);
}

static void TestCountZeroClears() {
  Group("count_zero_clears (C)");
  Fixture f = FixC();
  const Out o = Run(f.b, f.c0, f.c1, true);
  CHECK(o.r.ok);
  CHECK(ZeroFrom(f.b, 0, 0));  // out:4
  CHECK(o.r.unsorted == 0);
  CHECK(o.r.cleared == 1);
  CHECK(o.r.duplicates == 0);
}

static void TestCount192() {
  Group("count_192 (D)");
  Fixture f = FixD();
  const Out o = Run(f.b, f.c0, f.c1, true);
  CHECK(o.r.ok);  // 192 is the last count accepted (193: count_above_192)
  CHECK(Get(f.b, 0, 0) == 0x835FF410u);    // out:6
  CHECK(Get(f.b, 0, 191) == 0x83600000u);  // out:6
  bool rising = true;
  for (int s = 0; s < 191; ++s) rising = rising && Get(f.b, 0, s) < Get(f.b, 0, s + 1);
  CHECK(rising);
  CHECK(Get(f.b, 1, 0) == 0x83500002u);  // list 1's slot 0 sits right after list 0's slot 191
  CHECK(ZeroFrom(f.b, 1, 1));
  CHECK(o.r.unsorted == 1);  // out:5
  CHECK(o.r.cleared == 0);
  CHECK(o.r.duplicates == 0);
}

static void TestCountAbove192() {
  Group("count_above_192 (E)");
  const uint64_t start = 0x0123456789ABCDEFull;  // not kHashStart, so "unchanged" cannot be a restart
  struct Case {
    uint32_t c0, c1;
  };
  // Second case: list 0 is valid (count 0) and holds a stale slot, so a pass that handles list 0 before it checks list 1's count changes
  // bytes. Third: list 1's edge (list 0's 192 is accepted in count_192).
  const Case cases[] = {{193, 0}, {0, 0xFFFFFFFFu}, {192, 193}};
  for (const Case& c : cases) {
    Buf b = Make({0x83A01000, 0x83501000, 0x83901000, 0x83001000, 0x83501000}, {0x83500100, 0x83500002, 0x83001000});
    const Buf before = b;
    Buf probe = b;  // precondition: the same buffer under a valid pass with list 0's count 0 does change bytes
    Run(probe, 0, 0, true);
    CHECK(probe != before);
    const Out o = Run(b, c.c0, c.c1, true, start);
    CHECK(!o.r.ok);
    CHECK(o.r.unsorted == 0 && o.r.cleared == 0 && o.r.duplicates == 0);
    CHECK(b == before);
    CHECK(o.hash == start);
  }
}

static void TestAlreadyInForm() {
  Group("already_in_form (F)");
  Buf b = Make({0x83501000, 0x83901000, 0x83A01000}, {});
  const Buf before = b;
  const Out o = Run(b, 3, 0, true);
  CHECK(o.r.ok);
  CHECK(b == before);  // out:8
  CHECK(o.r.unsorted == 0);
  CHECK(o.r.cleared == 0);
  CHECK(o.r.duplicates == 0);
}

static void TestOneBufferTwice() {
  Group("one_buffer_twice (G)");
  Fixture f = FixG();
  const Out o = Run(f.b, f.c0, f.c1, true);
  CHECK(o.r.ok);
  CHECK(Get(f.b, 0, 0) == 0x83501000u && Get(f.b, 0, 1) == 0x83501000u && Get(f.b, 0, 2) == 0x83901000u);  // out:9
  CHECK(ZeroFrom(f.b, 0, 3));
  CHECK(o.r.duplicates == 1);
  CHECK(o.r.unsorted == 1);  // out:9
  CHECK(o.r.cleared == 0);
}

static void TestEqualNeighboursInOrder() {
  Group("equal_neighbours_in_order");
  Buf b = Make({0x83501000, 0x83501000, 0x83901000}, {});
  const Buf before = b;
  const Out o = Run(b, 3, 0, true);
  CHECK(o.r.ok);
  CHECK(b == before);
  CHECK(o.r.unsorted == 0);
  CHECK(o.r.duplicates == 1);
  CHECK(o.r.cleared == 0);
}

static void TestOneBufferInBothLists() {
  Group("one_buffer_in_both_lists");
  Buf b = Make({0x83501000, 0x83901000}, {0x83901000, 0x83A01000});
  const Buf before = b;
  const Out o = Run(b, 2, 2, true);
  CHECK(o.r.ok);
  CHECK(b == before);
  CHECK(o.r.unsorted == 0);
  CHECK(o.r.cleared == 0);
  CHECK(o.r.duplicates == 0);
}

static void TestSecondList() {
  Group("second_list");
  Fixture f = FixSecond();
  const Out o = Run(f.b, f.c0, f.c1, true);
  CHECK(o.r.ok);
  CHECK(Get(f.b, 1, 0) == 0x83500002u && Get(f.b, 1, 1) == 0x83500100u);
  CHECK(ZeroFrom(f.b, 1, 2));
  CHECK(ZeroFrom(f.b, 0, 0));
  CHECK(o.r.unsorted == 1);
  CHECK(o.r.cleared == 1);
  CHECK(o.r.duplicates == 0);
}

static void TestWriteOff() {
  Group("write_off");
  const Fixture fixtures[] = {FixA(), FixB(), FixC(), FixD(), FixG(), FixSecond()};
  for (const Fixture& f : fixtures) {
    Buf written = f.b;
    const Out ow = Run(written, f.c0, f.c1, true);
    CHECK(written != f.b);  // precondition: the written pass changes this fixture, so "no byte changed" below can fail
    Buf kept = f.b;
    const Out ok_ = Run(kept, f.c0, f.c1, false);
    CHECK(kept == f.b);
    CHECK(ow.r.ok && SameCounters(ow.r, ok_.r));
    CHECK(ow.hash == ok_.hash);
  }
}

static void TestTwoOrdersOneForm() {
  Group("two_orders_one_form");
  // X: A's entries in A's order with A's stale slots; Y: the same three in another order, no stale slots. List 1: B's two in both orders.
  Buf x = Make({0x83A01000, 0x83501000, 0x83901000, 0x83001000, 0x83501000}, {0x83500100, 0x83500002});
  Buf y = Make({0x83901000, 0x83A01000, 0x83501000}, {0x83500002, 0x83500100});
  CHECK(x != y);
  const Out ox = Run(x, 3, 2, true);
  const Out oy = Run(y, 3, 2, true);
  CHECK(ox.r.ok && oy.r.ok);
  CHECK(std::memcmp(x.data(), y.data(), kListBytes) == 0);
  CHECK(Get(x, 0, 0) == 0x83501000u && Get(x, 1, 0) == 0x83500002u);  // the equal form is not an empty one
  CHECK(ox.hash == oy.hash);
  CHECK(ox.hash == 0x6CB2B5F7A1D4B76Aull);  // the same sets as H1, out:11
}

static void TestHashPins() {
  Group("hash_pins");
  Buf h1 = Make({0x83501000, 0x83901000, 0x83A01000}, {0x83500002, 0x83500100});
  const Out o1 = Run(h1, 3, 2, true);
  CHECK(o1.hash == 0x6CB2B5F7A1D4B76Aull);  // out:11, H1
  const Out o2 = Run(h1, 3, 2, true, o1.hash);
  CHECK(o2.hash == 0x0252838382A5FF69ull);  // out:12, H2: carried
  Buf h3 = Make({0x83501000, 0x83901000, 0x83A01004}, {0x83500002, 0x83500100});
  CHECK(Run(h3, 3, 2, true).hash == 0xE1C9ABF7859F0DBEull);  // out:13, H3
  Buf h4 = Make({0x83500002, 0x83500100}, {0x83501000, 0x83901000, 0x83A01000});
  CHECK(Run(h4, 2, 3, true).hash == 0xD41D74022CE5DD12ull);  // out:14, H4: the lists exchanged
  Buf h5 = Make({}, {});
  CHECK(Run(h5, 0, 0, true).hash == 0xA8C7F832281A39C5ull);  // out:15, H5: both empty
  Buf h6 = Make({0x83501000, 0x83901000, 0x83A01000}, {0x83500002, 0x83500100});
  CHECK(Run(h6, 2, 2, true).hash == 0x30334107A3C0CA3Aull);  // out:16, H6: count0 2, 83A01000 past the count
}

static bool Is(const char* got, const char* want) {
  if (!got || !want) return got == want;
  return std::strcmp(got, want) == 0;
}

static void TestStandDownTable() {
  Group("stand_down_table");
  CHECK(Is(vsm::StandDown(true, 0x835780E0u, true, 0x824F4C3Cu, 0), nullptr));
  CHECK(Is(vsm::StandDown(false, 0x835780E0u, true, 0x824F4C3Cu, 0), "the game's reset did not run"));
  CHECK(Is(vsm::StandDown(true, 0x835780E4u, true, 0x824F4C3Cu, 0), "the reset was called on another object"));
  CHECK(Is(vsm::StandDown(true, 0x835780E0u, false, 0x824F4C3Cu, 0), "not the main thread"));
  CHECK(Is(vsm::StandDown(true, 0x835780E0u, true, 0x824F4AB0u, 0), "not the frame loop's call"));
  CHECK(Is(vsm::StandDown(true, 0x835780E0u, true, 0x824F4C3Cu, 1), "a worker is in its section"));
  CHECK(Is(vsm::StandDown(false, 0x835780E0u, false, 0x824F4C3Cu, 0), "the game's reset did not run"));
  CHECK(Is(vsm::StandDown(true, 0x835780E4u, true, 0x824F4C3Cu, 1), "the reset was called on another object"));
}

static void TestLines() {
  Group("lines");
  // Re-derived for the port (lockstep port plan, task B1, "The log texts"): no "[rb0]", no mode word (the mode is in the decision line).
  CHECK(vsm::Line(599, 12, 40, 0, 0, 0xAB) ==
        "vsm: resets 599, sorted 12, cleared 40, stood down 0, anomalies 0, content 00000000000000AB");
  CHECK(vsm::Line(0, 0, 0, 0, 0, 0x6CB2B5F7A1D4B76Aull) ==
        "vsm: resets 0, sorted 0, cleared 0, stood down 0, anomalies 0, content 6CB2B5F7A1D4B76A");
  CHECK(vsm::Line(1, 2, 3, 4, 5, 0x0123456789ABCDEFull) ==  // every field a different value: no two fields can trade places unseen
        "vsm: resets 1, sorted 2, cleared 3, stood down 4, anomalies 5, content 0123456789ABCDEF");
  CHECK(vsm::StoodDownLine(26, "not the main thread") == "vsm stood down at frame 26: not the main thread");
  CHECK(vsm::StoodDownWarn("not the main thread") == "WARN vsm: stood down (not the main thread): the run is not evidence");
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-v") == 0) g_verbose = true;
  }
  TestConstants();
  TestTailSmallerThanLive();
  TestByteOrder();
  TestCountZeroClears();
  TestCount192();
  TestCountAbove192();
  TestAlreadyInForm();
  TestOneBufferTwice();
  TestEqualNeighboursInOrder();
  TestOneBufferInBothLists();
  TestSecondList();
  TestWriteOff();
  TestTwoOrdersOneForm();
  TestHashPins();
  TestStandDownTable();
  TestLines();
  if (g_failures == 0) {
    std::printf("PASSED: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
  return 1;
}
