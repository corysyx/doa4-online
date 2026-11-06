// src/net/np_digest.h - what the sync check hashes (no SDK, no Windows; host test: np_core_host_test.cpp TestMixedMasksOnlyWhenMixed).
//
// Every session frame the check folds two rows into one checksum: the digest (DigestRegions, hashed every frame) and the sweep (rolling
// 64 KB blocks of .data, minus SweepRanges). A session with no suits, or with the same import on both PCs, hashes what v1.0.9 hashes.
//
// A mixed session (one player has an import the other lacks or has differently, so each PC draws a fighter from its own files) runs no
// sweep and hashes a positive list instead. Measured over 8 whole runs (notes/plans/2026-11-06-public-importer-plan1-foundation/
// task-6-report.md): what two such PCs differ in has no end, each new scenario added thousands of words, and Tag added 16 blocks inside its
// rounds, so a list of what to leave out could not be finished and every miss would end a live match. The rows below never differed.
// During a round both side records are read each frame, so a fighter's position, life and state are checked every frame where the sweep saw
// them once in 41. Outside a round they count for nothing: previews write to the same words. What such a session gives up: anything that
// goes wrong in .data without reaching one of these rows goes unnoticed.

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

// The game's fight sequence state, a byte, the index into the handler table 0x82C657F4 that sub_823DFA10 calls each frame. Seen: 0 outside a
// match, 1 setting a round up, 2 a round being fought (sub_823E0BD0), 3 the round is over, by KO or on time, 4 the KO's replay, 5 the result.
// Ruling 17: a side record means a fighter only at 2. A record's own +0 does not say so: it is 1 on the character select after a match, where
// previews cycle its character and model and write its words on a frame that depends on each PC's loads. Lives do not say so either: time
// over ends a round with both above 0.
inline constexpr uint32_t kFightState = 0x830ACF85;
inline constexpr uint8_t kFightRound = 2;

// False only in a mixed session (ruling 15).
inline bool SweepOn(rules::SuitSession state) { return state != rules::SuitSession::kMixed; }

// The digest's regions, read at the frame hook's first step on both peers (np_session.cpp HashRows: the state entering the frame, before
// any input of that frame).
inline std::vector<Region> DigestRegions(rules::SuitSession state) {
  std::vector<Region> r;
  Region slots{"task_slots", 0x83100F28, 24 * 28, {}};
  for (uint32_t i = 0; i < 24; ++i) slots.masks.push_back({i * 28 + 16, 8});  // mftb profile words
  if (state != rules::SuitSession::kMixed) {
    r.push_back({"game_obj", 0x8320D110, 0x63B0, {}});  // the camera and scene object
    r.push_back({"netmatch", 0x830C1CB0, 0x38C0, {}});
    r.push_back({"pad_entries", 0x831011E0, 5 * 44, {}});
    r.push_back({"pad_mgr", 0x8357C2D8, 0xD60, {}});
    r.push_back(slots);
    r.push_back({"mt_833F7600", 0x833F7600, 2504, {}});
    r.push_back({"mt_8357B5C0", 0x8357B5C0, 2504, {}});
    r.push_back({"mt_83420020", 0x83420020, 2504, {}});
    r.push_back({"mt_8321B198", 0x8321B198, 2504, {}});
    r.push_back({"frame_ctr", 0x831013E4, 4, {}});
    return r;
  }
  // Each slot's fiber pointer (+8, sub_824F3340 stores CreateFiber's result): a heap address, moved by what the loads before it took. The
  // scheduler sub_824F31E8 only hands it to the fiber calls. This is the one thing a costume changes in a region that stays on the list.
  for (uint32_t i = 0; i < 24; ++i) slots.masks.push_back({i * 28 + 8, 4});
  r.push_back({"netmatch", 0x830C1CB0, 0x38C0, {}});
  r.push_back({"pad_entries", 0x831011E0, 5 * 44, {}});
  r.push_back({"pad_mgr", 0x8357C2D8, 0xD60, {}});
  r.push_back(slots);
  r.push_back({"mt_833F7600", 0x833F7600, 2504, {}});
  r.push_back({"mt_83420020", 0x83420020, 2504, {}});
  r.push_back({"mt_8321B198", 0x8321B198, 2504, {}});
  r.push_back({"frame_ctr", 0x831013E4, 4, {}});
  // sub_82385BD0 sets this word when a file differs between d: and cache: (missing, or size or time) and, only then, -1 if the archives' sizes
  // (per PC here) do not fit the free space; it chooses each load's path. cache: is a symlink to the same folder (MountCacheDevice,
  // dead_or_alive_4_app.h), so nothing should differ and the word stays 0. On the list because nothing else would see it if that changed.
  r.push_back({"cache_flag", 0x831A595C, 4, {}});
  // The state always counts, so two peers who disagree on the gate are caught there; the records count only behind it (RegionCopy, np_core.h).
  r.push_back({"fight_state", kFightState, 1, {}});
  // The fighter table (sub_823CCF10, stride 156, one record per side): +0 set while a match is loaded, +1 character, +2 model, +4 position,
  // +0x24 life, then state words to +0x78; past that, positions of the model on show, which differ.
  r.push_back({"side0", 0x83033B40, 0x78, {}, kFightState, kFightRound});
  r.push_back({"side1", 0x83033BDC, 0x78, {}, kFightState, kFightRound});
  return r;
}

// The sweep's masked ranges; none in a session with no sweep.
inline std::vector<std::pair<uint32_t, uint32_t>> SweepRanges(rules::SuitSession state) {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  if (!SweepOn(state)) return out;
  for (const SweepMask& m : kSweepMasks) out.push_back({m.address, m.length});
  return out;
}

}  // namespace doa4::np
