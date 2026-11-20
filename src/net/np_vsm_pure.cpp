// DOA4 worker determinism Plan 3, Task C1: the pure rule of the vertex-stream pass. See np_vsm_pure.h.

#include "np_vsm_pure.h"

#include <algorithm>
#include <cstdio>

namespace doa4::np::vsm {
namespace {

constexpr uint64_t kPrime = 0x100000001B3ull;

uint32_t Load(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void Store(uint8_t* p, uint32_t w) {
  p[0] = uint8_t(w >> 24);
  p[1] = uint8_t(w >> 16);
  p[2] = uint8_t(w >> 8);
  p[3] = uint8_t(w);
}

uint64_t Fold(uint64_t h, uint32_t w) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    h ^= uint8_t(w >> shift);
    h *= kPrime;
  }
  return h;
}

}  // namespace

PassResult Pass(uint8_t* lists, uint32_t count0, uint32_t count1, bool write, uint64_t* hash) {
  PassResult r;
  if (count0 > uint32_t(kSlots) || count1 > uint32_t(kSlots)) return r;
  const uint32_t counts[2] = {count0, count1};
  uint64_t h = *hash;
  for (int l = 0; l < 2; ++l) {
    uint8_t* list = lists + size_t(l) * kSlots * 4;
    const int n = int(counts[l]);
    uint32_t live[kSlots];
    for (int i = 0; i < n; ++i) live[i] = Load(list + 4 * i);
    bool unsorted = false;
    for (int i = 0; i + 1 < n; ++i) unsorted = unsorted || live[i] > live[i + 1];
    if (unsorted) ++r.unsorted;
    std::sort(live, live + n);
    for (int i = 1; i < n; ++i) {
      if (live[i] == live[i - 1]) ++r.duplicates;
    }
    for (int i = n; i < kSlots; ++i) {
      if (Load(list + 4 * i) != 0) ++r.cleared;
    }
    if (write) {
      for (int i = 0; i < n; ++i) Store(list + 4 * i, live[i]);
      for (int i = n; i < kSlots; ++i) Store(list + 4 * i, 0);
    }
    h = Fold(h, counts[l]);
    for (int i = 0; i < n; ++i) h = Fold(h, live[i]);
  }
  *hash = h;
  r.ok = true;
  return r;
}

const char* StandDown(bool original_ran, uint32_t manager, bool main_thread, uint32_t lr, int running) {
  if (!original_ran) return "the game's reset did not run";
  if (manager != kManager) return "the reset was called on another object";
  if (!main_thread) return "not the main thread";
  if (lr != kLrFrameLoop) return "not the frame loop's call";
  if (running > 0) return "a worker is in its section";
  return nullptr;
}

std::string Line(uint64_t resets, uint64_t sorted, uint64_t cleared, uint64_t stood_down, uint64_t anomalies, uint64_t content) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "vsm: resets %llu, sorted %llu, cleared %llu, stood down %llu, anomalies %llu, content %016llX",
                (unsigned long long)resets, (unsigned long long)sorted, (unsigned long long)cleared,
                (unsigned long long)stood_down, (unsigned long long)anomalies, (unsigned long long)content);
  return buf;
}

std::string StoodDownLine(int32_t frame, const std::string& why) {
  return "vsm stood down at frame " + std::to_string(frame) + ": " + why;
}

std::string StoodDownWarn(const std::string& why) { return "WARN vsm: stood down (" + why + "): the run is not evidence"; }

}  // namespace doa4::np::vsm
