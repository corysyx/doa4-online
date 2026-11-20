// DOA4 worker determinism Plan 3, Task C1: the pure rule of the vertex-stream pass (no SDK, no guest includes:
// tests/host/np_vsm_host_test.cpp compiles np_vsm_pure.cpp with plain clang-cl). The game's vertex-stream manager (one object at
// kManager) keeps two lists of 192 big-endian buffer addresses with a count each. Two runs of one script under schedule S6 end with the
// same buffers in the lists, but in another order and with stale slots past the count. After every frame reset (the one call of the
// reset, from the frame loop: return address kLrFrameLoop) the pass puts both lists into a form that depends only on which buffers are in
// them, and folds that content into a hash carried over the run (design: notes/specs/2026-11-15-doa4-vsm-pool-fix-design.md).
//
// THE RULE (Pass):
//   - A count above 192 is not a count: the pass returns ok false with every counter 0, reads and writes no slot, and leaves *hash as it
//     was.
//   - Otherwise, for each list, the entries below its count are put in rising order of their decoded (big-endian) value; equal entries stay
//     next to each other. With write, the list's bytes become that order below the count and zero from the count to slot 191; without
//     write no byte changes. The counters are the same either way.
//   - *hash is then folded, FNV-1a 64 (start kHashStart, prime 0x100000001B3), over the words count0, list 0's entries in rising order,
//     count1, list 1's entries in rising order, each word's four bytes most significant first. A second pass continues from the first's
//     value.

#pragma once

#include <cstdint>
#include <string>

namespace doa4::np::vsm {

constexpr uint32_t kManager = 0x835780E0;
constexpr uint32_t kOffLists = 5420;                    // list 0, then list 1: 2 x 192 big-endian 32-bit words
constexpr uint32_t kOffCount0 = 6956, kOffCount1 = 6960;
constexpr int kSlots = 192;
constexpr uint32_t kLrFrameLoop = 0x824F4C3C;           // the reset's one call site, in the frame loop
constexpr uint64_t kHashStart = 0xCBF29CE484222325ull;  // FNV-1a 64

struct PassResult {
  bool ok = false;     // false: a count is above 192; then no slot was read or written and *hash is unchanged
  int unsorted = 0;    // lists (0 to 2) in which some entry below the count is greater than the entry after it (equal neighbours are in order)
  int cleared = 0;     // slots past a count that held a non-zero word
  int duplicates = 0;  // entries below a count that equal another entry of the SAME list (each extra copy counts 1); one buffer in both lists is ordinary and counts 0
};

// lists: the 1536 bytes at manager + kOffLists. count0, count1: the decoded count words. hash: not null.
PassResult Pass(uint8_t* lists, uint32_t count0, uint32_t count1, bool write, uint64_t* hash);
// nullptr when the pass may run, else why not, checked in this order.
const char* StandDown(bool original_ran, uint32_t manager, bool main_thread, uint32_t lr, int running);
// "vsm: resets N, sorted S, cleared C, stood down D, anomalies A, content <16 upper-case hex digits>"
std::string Line(uint64_t resets, uint64_t sorted, uint64_t cleared, uint64_t stood_down, uint64_t anomalies, uint64_t content);
// "vsm stood down at frame <f>: <why>"
std::string StoodDownLine(int32_t frame, const std::string& why);
// "WARN vsm: stood down (<why>): the run is not evidence". Kept from the measurement branch with its host test; no build calls it:
// in mode 4 a stand-down raises an end request, whose own "WARN lanes:" line fails an offline run (np_det_lanes.cpp).
std::string StoodDownWarn(const std::string& why);

}  // namespace doa4::np::vsm
