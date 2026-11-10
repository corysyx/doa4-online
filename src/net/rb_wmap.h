// DOA4 rollback, milestone 1 (corysyx/doa4-online): the guest write map.
//
// Rollback needs to save and restore the game's state every frame, and so needs to know which guest memory the game
// writes. tools/np_wmap_patch.py makes every store of the generated code (REX_STORE_U8/16/32/64, the vector stores,
// dcbz and the atomic stores) also set doa4_wmap[address >> 12]: one byte per 4 KB guest page, set by any guest thread,
// never read by the game. Writes made outside the generated code (the SDK's kernel calls, file reads, the GPU) are not
// in it; milestone 1 measures only what the game's own code writes.
//
// --doa4_rb_wmap=true (offline only): at every frame boundary (sub_824F4498, the frame function) the pages marked
// since the last one are counted, added to a 10 s window and cleared; every 600 frames one summary goes to the log:
// pages per frame (average, maximum), the window's pages by area, and its largest runs of pages ("[doa4][RB] wmap").
// That is the input to milestone 2 (what a snapshot must hold).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" unsigned char doa4_wmap[1u << 20];  // defined in rb_wmap.cpp; written by the generated code

namespace doa4::rb {

inline constexpr uint32_t kPageShift = 12;
inline constexpr uint32_t kPages = 1u << 20;  // 4 GB of guest address space / 4 KB

// Where a guest page is (the Xbox 360 address map as the SDK lays it out).
enum class Area : uint8_t {
  kHeap4K,     // 0x00000000-0x3FFFFFFF virtual, 4 KB pages
  kHeap64K,    // 0x40000000-0x6FFFFFFF virtual, 64 KB pages
  kStacks,     // 0x70000000-0x7EFFFFFF thread stacks and kernel objects
  kXexData,    // 0x82B70000-0x835AFFFF the XEX's .data/.bss (what the desync check hashes)
  kXexOther,   // the rest of 0x80000000-0x8FFFFFFF
  kHeap90,     // 0x90000000-0x9FFFFFFF virtual
  kPhysical,   // 0xA0000000-0xFFFFFFFF physical memory (three views of the same 512 MB)
  kCount
};
Area AreaOf(uint32_t page);
const char* AreaName(Area a);

struct Range {
  uint32_t first_page = 0;
  uint32_t pages = 0;
};

// Pure (host-tested): the maximal runs of marked pages (map[i] != 0), largest first, ties by lower address; at most
// `max` of them.
std::vector<Range> Ranges(const unsigned char* map, size_t pages, size_t max);
// Pure: marked pages per area.
struct Census {
  uint32_t by_area[size_t(Area::kCount)] = {};
  uint32_t total = 0;
};
Census Count(const unsigned char* map, size_t pages);
// Pure: "0x82B70000 +12 KB" style text of a range.
std::string RangeText(const Range& r);

// The game: called at every frame boundary of an offline game when doa4_rb_wmap is on (main thread).
void WmapFrame(uint8_t* base);
bool WmapOn();  // doa4_rb_wmap, read once

}  // namespace doa4::rb
