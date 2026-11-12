// DOA4 rollback, milestone 1: the guest write map (rb_wmap.h says what it is for).
#include "rb_wmap.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef DOA4_RB_HOST_TEST
#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#endif

extern "C" {
alignas(64) unsigned char doa4_wmap[1u << 20] = {};
// The store watch's pages (rb_snap.cpp, SnapTest only): a store to a marked page calls doa4_wwatch_hit.
alignas(64) unsigned char doa4_wwatch[1u << 20] = {};
}

namespace doa4::rb {

Area AreaOf(uint32_t page) {
  const uint32_t a = page << kPageShift;
  if (a < 0x40000000u) return Area::kHeap4K;
  if (a < 0x70000000u) return Area::kHeap64K;
  if (a < 0x80000000u) return Area::kStacks;
  if (a >= 0x82B70000u && a < 0x835B0000u) return Area::kXexData;
  if (a < 0x90000000u) return Area::kXexOther;
  if (a < 0xA0000000u) return Area::kHeap90;
  return Area::kPhysical;
}

const char* AreaName(Area a) {
  switch (a) {
    case Area::kHeap4K: return "heap 0x0-0x3FFFFFFF";
    case Area::kHeap64K: return "heap 0x40000000-0x6FFFFFFF";
    case Area::kStacks: return "stacks/kernel 0x70000000-0x7EFFFFFF";
    case Area::kXexData: return "XEX data/bss";
    case Area::kXexOther: return "XEX other";
    case Area::kHeap90: return "heap 0x90000000-0x9FFFFFFF";
    case Area::kPhysical: return "physical 0xA0000000+";
    default: return "?";
  }
}

std::vector<Range> Ranges(const unsigned char* map, size_t pages, size_t max) {
  std::vector<Range> all;
  size_t i = 0;
  while (i < pages) {
    if (!map[i]) {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < pages && map[j]) ++j;
    all.push_back(Range{uint32_t(i), uint32_t(j - i)});
    i = j;
  }
  std::stable_sort(all.begin(), all.end(), [](const Range& a, const Range& b) { return a.pages > b.pages; });
  if (all.size() > max) all.resize(max);
  return all;
}

Census Count(const unsigned char* map, size_t pages) {
  Census c;
  for (size_t i = 0; i < pages; ++i) {
    if (!map[i]) continue;
    ++c.by_area[size_t(AreaOf(uint32_t(i)))];
    ++c.total;
  }
  return c;
}

std::string RangeText(const Range& r) {
  char text[64];
  std::snprintf(text, sizeof text, "0x%08X +%u KB", r.first_page << kPageShift, r.pages * 4u);
  return text;
}

#ifndef DOA4_RB_HOST_TEST

bool WmapOn() {
  static const bool on = rex::cvar::Query<bool>("doa4_rb_wmap");
  return on;
}

namespace {

constexpr uint32_t kReportFrames = 600;  // 10 s at 60 fps

struct Stats {
  unsigned char window[kPages] = {};  // pages written in this window (union of its frames)
  uint64_t frames = 0;                // since the start
  uint32_t window_frames = 0;
  uint64_t window_sum = 0;            // pages per frame, summed over the window
  uint32_t window_max = 0;
  uint64_t area_sum[size_t(Area::kCount)] = {};
};

Stats& S() {
  static Stats* s = new Stats();  // 1 MB: on the heap, made on first use (only when doa4_rb_wmap is on)
  return *s;
}

void Report(Stats& s) {
  const Census window = Count(s.window, kPages);
  const double frames = s.window_frames ? double(s.window_frames) : 1.0;
  REXLOG_INFO("[doa4][RB] wmap: frames {}-{}: {:.0f} pages/frame average ({:.1f} MB), {} max; {} pages in the window",
              s.frames - s.window_frames, s.frames - 1, double(s.window_sum) / frames,
              double(s.window_sum) / frames * 4096.0 / (1024.0 * 1024.0), s.window_max, window.total);
  for (size_t a = 0; a < size_t(Area::kCount); ++a) {
    if (!window.by_area[a] && !s.area_sum[a]) continue;
    REXLOG_INFO("[doa4][RB] wmap:   {}: {:.0f} pages/frame, {} pages in the window ({} KB)", AreaName(Area(a)),
                double(s.area_sum[a]) / frames, window.by_area[a], window.by_area[a] * 4u);
  }
  const std::vector<Range> ranges = Ranges(s.window, kPages, 40);
  std::string line;
  for (size_t i = 0; i < ranges.size(); ++i) {
    line += (line.empty() ? "" : ", ") + RangeText(ranges[i]);
    if ((i + 1) % 8 == 0 || i + 1 == ranges.size()) {
      REXLOG_INFO("[doa4][RB] wmap:   largest runs: {}", line);
      line.clear();
    }
  }
  std::memset(s.window, 0, sizeof s.window);
  s.window_frames = 0;
  s.window_sum = 0;
  s.window_max = 0;
  std::fill(std::begin(s.area_sum), std::end(s.area_sum), 0);
}

}  // namespace

void WmapFrame(uint8_t* /*base*/) {
  Stats& s = S();
  if (s.frames == 0) {
    std::memset(doa4_wmap, 0, sizeof doa4_wmap);  // the first boundary: what boot wrote is not a frame's work
    REXLOG_INFO("[doa4][RB] wmap: on (every guest store of the generated code marks its 4 KB page; a summary every {} frames)",
                kReportFrames);
    ++s.frames;
    return;
  }
  // This frame's pages: counted by area, added to the window, cleared. 8 bytes at a time: most of the map is zero.
  uint32_t marked = 0;
  const uint64_t* words = reinterpret_cast<const uint64_t*>(doa4_wmap);
  for (uint32_t w = 0; w < kPages / 8; ++w) {
    if (!words[w]) continue;
    for (uint32_t k = 0; k < 8; ++k) {
      const uint32_t page = w * 8 + k;
      if (!doa4_wmap[page]) continue;
      doa4_wmap[page] = 0;
      s.window[page] = 1;
      ++s.area_sum[size_t(AreaOf(page))];
      ++marked;
    }
  }
  s.window_sum += marked;
  s.window_max = std::max(s.window_max, marked);
  ++s.window_frames;
  ++s.frames;
  if (s.window_frames >= kReportFrames) Report(s);
}

#endif  // DOA4_RB_HOST_TEST

}  // namespace doa4::rb
