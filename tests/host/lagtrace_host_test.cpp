// Host test for src/doa4_lagtrace.cpp (the latency trace's swap-index join and CSV text).
// Built and run by run_lagtrace_test.bat.

#include "../../src/doa4_lagtrace.h"

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

using namespace doa4::lagtrace;

static Frame F(uint64_t n, uint32_t swap) {
  Frame f;
  f.frame = n;
  f.t_frame_start = 1000 * int64_t(n);
  f.guest_swap_index = swap;
  f.t_guest_swap = swap ? 1000 * int64_t(n) + 500 : 0;
  return f;
}

static Present P(uint32_t swap, int64_t t) {
  Present p;
  p.swap_index = swap;
  p.t_call = t;
  p.t_return = t + 7;
  p.flags = 1;
  return p;
}

// The first present of a swap is its row's present; later presents of the same swap (UI repaints) are not.
// Wrong version excluded: keeping the last present of the swap (t 9000 instead of 2000).
static void TestFirstPresentOfASwap() {
  Joiner j;
  j.AddPresent(P(1, 2000));
  j.AddPresent(P(1, 9000));  // a repaint of the same guest output
  j.AddFrame(F(1, 1));
  const std::vector<Row> rows = j.Take(false);
  CHECK(rows.size() == 1);
  CHECK(rows.size() == 1 && rows[0].joined);
  CHECK(rows.size() == 1 && rows[0].present.t_call == 2000);
  CHECK(rows.size() == 1 && rows[0].present.t_return == 2007);
  CHECK(rows.size() == 1 && rows[0].present.t_call != 9000);
}

// A frame whose swap has no present yet waits (no row) until the present comes, without a flush.
static void TestWaitsForItsPresent() {
  Joiner j;
  j.AddFrame(F(5, 5));
  CHECK(j.Take(false).empty());
  CHECK(j.waiting() == 1);
  j.AddPresent(P(5, 6100));
  const std::vector<Row> rows = j.Take(false);
  CHECK(rows.size() == 1 && rows[0].joined && rows[0].present.t_call == 6100);
  CHECK(j.waiting() == 0);
}

// A present of a later swap shows the frame's own output was never painted (the mailbox dropped it): the row is
// ready, unjoined. Wrong version excluded: joining the frame to the next swap's present.
static void TestDroppedSwapIsUnjoined() {
  Joiner j;
  j.AddFrame(F(6, 6));
  j.AddPresent(P(7, 8000));
  j.AddFrame(F(7, 7));
  const std::vector<Row> rows = j.Take(false);
  CHECK(rows.size() == 2);
  CHECK(rows.size() == 2 && !rows[0].joined);
  CHECK(rows.size() == 2 && rows[0].present.t_call == 0);  // NOT the 8000 of swap 7
  CHECK(rows.size() == 2 && rows[1].joined && rows[1].present.t_call == 8000);
}

// Swaps that belong to no frame (the D3D kick thread's re-present, index 9) sit between the frames' swaps.
// Wrong version excluded: joining by position (the second frame would get swap 9's present at 9500).
static void TestJoinIsByIndexNotPosition() {
  Joiner j;
  j.AddPresent(P(8, 8500));
  j.AddPresent(P(9, 9500));
  j.AddPresent(P(10, 10500));
  j.AddFrame(F(8, 8));
  j.AddFrame(F(9, 10));
  const std::vector<Row> rows = j.Take(false);
  CHECK(rows.size() == 2);
  CHECK(rows.size() == 2 && rows[0].joined && rows[0].present.t_call == 8500);
  CHECK(rows.size() == 2 && rows[1].joined && rows[1].present.t_call == 10500);
  CHECK(rows.size() == 2 && rows[1].present.swap_index == 10);
}

// A frame with no swap is ready at once, unjoined, and rows keep frame order: it waits behind a waiting frame.
static void TestNoSwapAndOrder() {
  Joiner j;
  j.AddFrame(F(1, 0));
  std::vector<Row> rows = j.Take(false);
  CHECK(rows.size() == 1 && !rows[0].joined && rows[0].frame.frame == 1);
  j.AddFrame(F(2, 2));
  j.AddFrame(F(3, 0));
  CHECK(j.Take(false).empty());  // frame 3 waits behind frame 2
  j.AddPresent(P(2, 2200));
  rows = j.Take(false);
  CHECK(rows.size() == 2 && rows[0].frame.frame == 2 && rows[0].joined && rows[1].frame.frame == 3 && !rows[1].joined);
}

// Take(true) hands out every waiting frame, unjoined; a present that arrives afterwards changes nothing.
static void TestFlush() {
  Joiner j;
  j.AddFrame(F(4, 4));
  j.AddFrame(F(5, 5));
  const std::vector<Row> rows = j.Take(true);
  CHECK(rows.size() == 2 && !rows[0].joined && !rows[1].joined);
  j.AddPresent(P(5, 5500));
  CHECK(j.Take(true).empty());
}

// More waiting frames than max_waiting: the oldest is handed out unjoined (the presenter stalled, e.g. minimized).
static void TestWaitingCap() {
  Joiner j(2);
  j.AddFrame(F(1, 1));
  j.AddFrame(F(2, 2));
  CHECK(j.Take(false).empty());  // precondition: 2 waiting = the cap, nothing handed out
  j.AddFrame(F(3, 3));
  CHECK(j.waiting() == 3);  // precondition: one past the cap
  const std::vector<Row> rows = j.Take(false);
  CHECK(rows.size() == 1 && rows[0].frame.frame == 1 && !rows[0].joined);
  CHECK(j.waiting() == 2);
}

// A frame whose swap index was already handed out (the same index again: no newer present can show it was dropped)
// is ready at once, unjoined, and blocks nothing. Presents with swap index 0 (no guest output painted) are ignored.
static void TestStaleIndexAndNoOutput() {
  Joiner j;
  j.AddPresent(P(5, 5000));
  j.AddFrame(F(5, 5));
  CHECK(j.Take(false).size() == 1);
  j.AddFrame(F(6, 5));
  std::vector<Row> rows = j.Take(false);
  CHECK(rows.size() == 1 && !rows[0].joined);
  j.AddPresent(P(0, 7000));
  j.AddFrame(F(7, 7));
  CHECK(j.Take(false).empty());  // the index-0 present did not stand in for swap 7
  j.AddPresent(P(7, 7700));
  rows = j.Take(false);
  CHECK(rows.size() == 1 && rows[0].joined && rows[0].present.t_call == 7700);
}

static void TestCsv() {
  const std::string h = CsvHeader();
  CHECK(h.rfind("frame,t_input_us,t_frame_start_us,t_guest_swap_us,t_host_present_us,guest_swap_index,", 0) == 0);
  CHECK(h.find('\n') == std::string::npos);
  Row r;
  r.frame.frame = 12;
  r.frame.np_frame = -1;
  r.frame.t_input = 111;
  r.frame.t_frame_start = 100;
  r.frame.t_guest_swap = 222;
  r.frame.guest_swap_index = 9;
  r.frame.t_np_done = 101;
  r.frame.t_frame_fn_done = 105;
  r.frame.t_swap_call = 220;
  r.frame.t_swap_return = 230;
  r.frame.pad_calls_main = 4;
  r.frame.pad_calls_other = 0;
  r.frame.swap_calls_main = 1;
  r.frame.swap_calls_other = 2;
  r.joined = true;
  r.present = P(9, 300);  // call 300, return 307, flags 1
  r.frame.fight_state = 3;  // the last column; 3, NOT the present flags (1) or swap calls (2) beside it
  CHECK(h.find(",fight_state,t_gpu_wait_enter_us,") != std::string::npos);
  // the GPU-thread columns (Task 8) of a joined present whose GPU marks are all 0: empty times, counts printed as 0
  CHECK(CsvLine(r) == "12,111,100,222,307,9,-1,101,105,220,230,300,4,0,1,2,1,3,,,,,0,0,0,0,0,0,0,0");
  r.joined = false;  // unjoined: the present fields are empty, NOT the stored present's times
  r.frame.t_input = 0;
  CHECK(CsvLine(r) == "12,,100,222,,9,-1,101,105,220,230,,4,0,1,2,,3,,,,,,,,,,,,");
  r.frame.fight_state = -1;  // not read: -1, printed (0 is a real state: outside a match)
  CHECK(CsvLine(r) == "12,,100,222,,9,-1,101,105,220,230,,4,0,1,2,,-1,,,,,,,,,,,,");
  r.frame.fight_state = 0;
  CHECK(CsvLine(r) == "12,,100,222,,9,-1,101,105,220,230,,4,0,1,2,,0,,,,,,,,,,,,");
  // as many fields as the header has columns
  size_t commas_h = 0, commas_l = 0;
  for (char c : h) commas_h += c == ',';
  for (char c : CsvLine(r)) commas_l += c == ',';
  CHECK(commas_h == 29 && commas_l == commas_h);
}

// The GPU thread's marks of the joined present's swap packet (Task 8, R56): each value in its own column, in the
// header's order; unjoined, every one of them empty. Wrong versions excluded: wait entry and wake swapped (distinct
// values), the stored present's counts printed for an unjoined row (stale values from a dropped swap).
static void TestCsvGpuMarks() {
  const std::string h = CsvHeader();
  CHECK(h.size() > 180 && h.substr(h.size() - 180) ==
        ",t_gpu_wait_enter_us,t_vblank_us,t_gpu_wait_wake_us,t_gpu_swap_us,gpu_wait_us,gpu_waits,gpu_wait_poll,"
        "gpu_wait_ref,gpu_wait_info,gpu_after_wait_packets,gpu_after_wait_sig,gpu_flags");
  Row r;
  r.frame.frame = 3;
  r.frame.np_frame = 40;
  r.frame.t_frame_start = 1000;
  r.frame.guest_swap_index = 7;
  r.frame.fight_state = 2;
  r.joined = true;
  r.present = P(7, 5000);
  r.present.t_gpu_wait_enter = 2100;
  r.present.t_vblank = 4700;
  r.present.t_gpu_wait_wake = 4750;
  r.present.t_gpu_swap = 4800;
  r.present.gpu_wait_us = 2650;
  r.present.gpu_waits = 1;
  r.present.gpu_wait_poll = 0x1F4C3A82;
  r.present.gpu_wait_ref = 6;
  r.present.gpu_wait_info = 0x13;
  r.present.gpu_after_wait_packets = 2;
  r.present.gpu_after_wait_sig = 0x6480;
  r.present.gpu_flags = 1;
  CHECK(CsvLine(r) == "3,,1000,,5007,7,40,,,,,5000,0,0,0,0,1,2,2100,4700,4750,4800,2650,1,525089410,6,19,2,25728,1");
  r.joined = false;
  CHECK(CsvLine(r) == "3,,1000,,,7,40,,,,,,0,0,0,0,,2,,,,,,,,,,,,");
}

// Wrong version excluded: qpc * 1000000 / frequency, which overflows int64 for this tick count.
static void TestQpcToUs() {
  CHECK(QpcToUs(123456789, 10000000) == 12345678);
  CHECK(QpcToUs(900000000000000000LL, 10000000) == 90000000000000000LL);
  CHECK(QpcToUs(5, 3) == 1666666);  // 5/3 s, rounded down
}

int main() {
  TestFirstPresentOfASwap();
  TestWaitsForItsPresent();
  TestDroppedSwapIsUnjoined();
  TestJoinIsByIndexNotPosition();
  TestNoSwapAndOrder();
  TestFlush();
  TestWaitingCap();
  TestStaleIndexAndNoOutput();
  TestCsv();
  TestCsvGpuMarks();
  TestQpcToUs();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
