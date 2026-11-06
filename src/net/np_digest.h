// src/net/np_digest.h - what the sync check hashes (no SDK, no Windows; host test: np_core_host_test.cpp TestMixedMasksOnlyWhenMixed).
//
// Every session frame the check folds two rows into one checksum: the digest (DigestRegions, hashed every frame) and the sweep (rolling
// 64 KB blocks of .data, minus SweepRanges). A session with no suits, or with the same import on both PCs, hashes what v1.0.9 hashes.
// A mixed session (one player has an import the other lacks or has differently, so each PC draws the fighter from its own files) also
// leaves out kMixedMasks. It never leaves out the fighter table's first 0x78 bytes per side (active, character, model, position, life,
// state), the online generator, the match, the pads or the frame counter: the test names them.
// NOT FINISHED: this list holds only the file-size tables and the fiber pointers, which any answer needs. On it alone a mixed session still
// ends in a desync, at frame 2089 in the one pair run (Task 6 report, section 1, awaits a ruling on what else a mixed session leaves out).

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#include "np_core.h"
#include "np_rules.h"

namespace doa4::np {

// Host bookkeeping inside the .data sweep, hashed as zero: each range is touched only by middleware,
// driver or lock code and never by the game's update (classification: notes/research/
// netplay_phase0_results.md, "Sweep words"). One exception, by judgement: the vblank count is also read
// by game code, but only as a wait's exit condition (nothing derived from it is stored); a frame-keyed
// value would never advance inside that wait.
struct SweepMask {
  uint32_t address;
  uint32_t length;
  const char* why;
};
inline constexpr SweepMask kSweepMasks[] = {
    // debug 82B70000 (notes/plans/2026-10-30-doa4-netplay-phase1/debug-82b70000-report.md): online pair p1t14b desynced
    // at frame 10 from this block at frame 0. The host had stashed the SDK handle in this critical section's header
    // (wait-list words "REX\0" + handle), the joiner had not: the CRT thread-start/exit notifier sub_827939E0 (called
    // by every new thread's start trampoline sub_82797768 and by ExitThread's sub_82791288) holds the section while it
    // walks the notify list, so two of the ~30 threads starting at boot meet in it only by host timing, and the waiter
    // (RtlEnterCriticalSection -> KeWaitForSingleObject) makes the SDK stash. Census over 1849 kept .data dumps: the
    // words held their static self-pointer 82B747D8 82B747D8 in 1848 and the stash in 1 (p1t14b host); the lock count,
    // recursion count and owner were at rest (-1, 0, 0) in all 1849. Readers: np_xref over 82B747C0..82B74800 lists
    // sub_827939E0 and sub_82793A40, which pass 0x82B747D0 only to RtlEnter/LeaveCriticalSection (SDK imports), and
    // sub_827930E0, which reads the table 82B747C0..82B747CF; a second search of the generated code for offsets
    // 18384..18407 from a 0x82B7 base found no other use, and no .data word holds a pointer into the header. So no
    // guest code reads these 8 bytes; only the SDK writes and reads them.
    {0x82B747D8, 8, "wait-list words of the CRT thread-notify critical section 0x82B747D0 (sub_827939E0, "
                    "sub_82793A40): the SDK's handle stash appears there only when two starting threads meet in it"},
    {0x82F6C9D0, 8, "CRI server threads B and A: loop-pass counters (sub_829BC8F8, sub_829BC458; zeroed by "
                    "sub_829BCAC8), counted by their host-timed idle loops"},
    {0x830ADBCC, 0x1C, "critical section of the 16-slot request table 0x830ADB08 (initialised in sub_824F4560, "
                       "entered by sub_823FC5A0..sub_823FC808): lock count, recursion count, owner thread"},
    {0x831A7AC0, 4, "vblank count stored by the D3D vblank callback sub_82766400 on the host vblank interrupt; "
                    "read only as the wait condition of the vsync wait sub_824F1360 (via sub_824F0C60)"},
    {0x8357A560, 0x400, "GPU timestamp ring 2 of the graphics object 0x83579C28 (+1292): 64 start and 64 end "
                        "slots stored by the GPU-interrupt callback sub_82765540; read only by sub_82765EF8, "
                        "whose result HoldFrameTiming replaces"},
    {0x8357D520, 4, "D3D resource id counter (atomic increment in sub_8275D658): ids for D3D state caching"},
};

// What a costume legitimately changes: masked only when the hand-off says kMixed. np_dumpdiff.py MIXED_MASKS is the same list, pinned by
// test_sweep_masks_match_source.
inline constexpr const char* kWhySize = "the file's size in the partition rows (sub_823FCBA0, sub_823851F8 store it from a stat through the per-PC overlay; read only by sub_82385BD0's free-space sum; the flag it sets, 0x831A595C, stays checked)";
inline constexpr const char* kWhySizes = "u32 per archive entry: the suits' file sizes, per PC (the overlay's table of contents, read at boot); read by CRI's sub_829BB5D0 for the host read's length";
inline constexpr const char* kWhySectors = "u16 sectors per archive entry: as the u32 table; read by CRI's sub_829BB5D0 for the host read's offset and length";
inline constexpr const char* kWhySlot = "the task slot's fiber pointer (sub_824F3340 stores CreateFiber's result), a heap address moved by what the loads before it took; the scheduler sub_824F31E8 only hands it to the fiber calls";
inline constexpr SweepMask kMixedMasks[] = {
    // MIXED MASKS BEGIN
    {0x82C5ED18, 4, kWhySize},
    {0x82C5ED34, 4, kWhySize},
    {0x82C5ED50, 4, kWhySize},
    {0x82C7F7F8, 4, kWhySize},
    {0x82C7F814, 4, kWhySize},
    {0x82C7F830, 4, kWhySize},
    {0x8303A10C, 132, kWhySectors},
    {0x8303A350, 208, kWhySectors},
    {0x8303A664, 156, kWhySectors},
    {0x830B3D04, 264, kWhySizes},
    {0x830B3F4C, 408, kWhySizes},
    {0x830B4254, 304, kWhySizes},
    {0x83100F30, 4, kWhySlot},
    {0x83100F4C, 4, kWhySlot},
    {0x83100F68, 4, kWhySlot},
    {0x83100F84, 4, kWhySlot},
    {0x83100FA0, 4, kWhySlot},
    {0x83100FBC, 4, kWhySlot},
    {0x83100FD8, 4, kWhySlot},
    {0x83100FF4, 4, kWhySlot},
    {0x83101010, 4, kWhySlot},
    {0x8310102C, 4, kWhySlot},
    {0x83101048, 4, kWhySlot},
    {0x83101064, 4, kWhySlot},
    {0x83101080, 4, kWhySlot},
    {0x8310109C, 4, kWhySlot},
    {0x831010B8, 4, kWhySlot},
    {0x831010D4, 4, kWhySlot},
    {0x831010F0, 4, kWhySlot},
    {0x8310110C, 4, kWhySlot},
    {0x83101128, 4, kWhySlot},
    {0x83101144, 4, kWhySlot},
    {0x83101160, 4, kWhySlot},
    {0x8310117C, 4, kWhySlot},
    {0x83101198, 4, kWhySlot},
    {0x831011B4, 4, kWhySlot},
    // MIXED MASKS END
};

// The digest's regions. kNone and kSame: as v1.0.9. kMixed: the same regions with every part of kMixedMasks that falls inside one added.
inline std::vector<Region> DigestRegions(rules::SuitSession state) {
  std::vector<Region> r;
  r.push_back({"game_obj", 0x8320D110, 0x63B0, {}});  // + 5 x 4336 B fighter blocks at +3836
  r.push_back({"netmatch", 0x830C1CB0, 0x38C0, {}});
  r.push_back({"pad_entries", 0x831011E0, 5 * 44, {}});
  r.push_back({"pad_mgr", 0x8357C2D8, 0xD60, {}});
  Region slots{"task_slots", 0x83100F28, 24 * 28, {}};
  for (uint32_t i = 0; i < 24; ++i) slots.masks.push_back({i * 28 + 16, 8});  // mftb profile words
  r.push_back(slots);
  r.push_back({"mt_833F7600", 0x833F7600, 2504, {}});
  r.push_back({"mt_8357B5C0", 0x8357B5C0, 2504, {}});
  r.push_back({"mt_83420020", 0x83420020, 2504, {}});
  r.push_back({"mt_8321B198", 0x8321B198, 2504, {}});
  r.push_back({"frame_ctr", 0x831013E4, 4, {}});
  if (state == rules::SuitSession::kMixed) {
    for (Region& region : r) {
      for (const SweepMask& m : kMixedMasks) {
        const uint64_t from = std::max<uint64_t>(m.address, region.address);
        const uint64_t to = std::min<uint64_t>(uint64_t(m.address) + m.length, uint64_t(region.address) + region.length);
        if (from < to) region.masks.push_back({uint32_t(from - region.address), uint32_t(to - from)});
      }
    }
  }
  return r;
}

// The sweep's masked ranges: kSweepMasks, then kMixedMasks when mixed.
inline std::vector<std::pair<uint32_t, uint32_t>> SweepRanges(rules::SuitSession state) {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  for (const SweepMask& m : kSweepMasks) out.push_back({m.address, m.length});
  if (state == rules::SuitSession::kMixed) {
    for (const SweepMask& m : kMixedMasks) out.push_back({m.address, m.length});
  }
  return out;
}

}  // namespace doa4::np
