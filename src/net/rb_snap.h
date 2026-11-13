// DOA4 rollback, milestone 2 (corysyx/doa4-online): the save / restore test (SnapTest).
//
// Rollback restores the game's state some frames back and runs those frames again with corrected input. This test
// does exactly that on one PC, in a scripted record run (DOA4_NP=record, tools/np_scripts), with no network:
//
//   DOA4_NP_SNAPTEST=<at>,<frames>[,<every>[,<count>]]   (or "1": 3600,8,240,12 - inside single.txt's fight)
//
// At the frame boundary entering frame <at> (the loop head, sub_824F4498: the main loop keeps nothing in registers
// across it, rb_snap.cpp) the snapshot is taken: every 4 KB page the game's code wrote since the session started
// (doa4_wmap, milestone 1) in the areas rollback restores (the heaps and the XEX data; not the thread stacks, not
// physical memory: that is the GPU's command and vertex data, measured in milestone 1). Some bytes keep their current
// value at a load: the D3D device (its 20608 bytes; it follows the GPU, which is not rolled back - with
// DOA4_NP_SNAPTEST_FENCE=rewind its fence counter goes back anyway, after the GPU has finished every fence; with
// DOA4_NP_SNAPTEST_DEVICE=restore all of it goes back except the ring pointers +0/+4/+8, every word that moved with the
// ring since the save, and the offsets in DOA4_NP_SNAPTEST_DEVLIVE, since the game reads its shader-constant shadow back) and every
// word seen changing while the game stood still at a save or a load (written by the GPU or by a thread that runs on).
// Every guest thread's virtual clock (np_det.cpp) is saved and put back with the snapshot. The run goes on <frames> frames, recording their input and a hash of
// the snapshot's pages at every boundary. Then the snapshot is restored, the frame counter goes back to <at>, and the
// same frames run again with the recorded input. At every boundary the hash must be the one of the first pass; at
// the end the memory is compared word by word with the first pass's end, and what differs is logged by area, by
// address and by how much the values moved ("[doa4][RB] snaptest" lines in np_log.txt and the game log). The replayed frames are drawn again (a short
// visible rewind): skipping the draw is the next milestone.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rb_wmap.h"

#ifndef DOA4_RB_HOST_TEST
#include "np_core.h"
#endif

struct PPCContext;

namespace doa4::rb {

struct SnapConfig {
  bool on = false;
  uint32_t at = 3600;    // the first test's frame
  uint32_t frames = 8;   // frames per test (the rollback depth)
  uint32_t every = 240;  // frames from one test's end to the next one's start
  uint32_t count = 12;   // tests per run
};
// Pure (host-tested): "" / "0" / "off" -> off; "1" / "on" -> the defaults; "at,frames[,every[,count]]" (frames
// 1-120, count 1-1000). A malformed text is off, with the reason in *error.
SnapConfig ParseSnapConfig(std::string_view text, std::string* error);

// Pure: whether a guest page belongs in the snapshot: in an area rollback restores and outside the excluded pages
// [skip_lo, skip_hi) (the D3D device).
bool SnapPage(uint32_t page, uint32_t skip_lo, uint32_t skip_hi);

// Pure: every maximal run of marked pages, in address order.
std::vector<Range> Runs(const unsigned char* map, size_t pages);

// Pure: "12 frames: 0-7 equal, 8 differs, ..." style text of a list of per-boundary results.
std::string EqualText(const std::vector<bool>& equal);

#ifndef DOA4_RB_HOST_TEST

// The D3D device (r3 of the swap wrapper sub_8275AEE0, doa4_photo_hook.cpp): its bytes are kept at a load.
void NoteD3DDevice(uint32_t device);

// What the session does at this boundary (np_session.cpp OnFrame, record mode).
struct SnapStep {
  bool rewind = false;  // the snapshot was restored: the frame counter goes back to `frame`
  uint32_t frame = 0;
  bool replay = false;  // this frame's input is *input (the first pass's), not the script's
  const np::FrameInput* input = nullptr;
  bool record = false;  // a first-pass frame: give its input to SnapRecordInput
};
bool SnapActive();  // DOA4_NP_SNAPTEST, read once (logs the setting)
// ctx: the main thread's registers at the frame hook; they are saved with the snapshot and put back at a load
// (DOA4_NP_SNAPTEST_REGS=leave: left as they are).
SnapStep SnapBoundary(uint32_t frame, uint8_t* base, uint8_t fight_state, ::PPCContext* ctx);
void SnapRecordInput(const np::FrameInput& input);

#endif

}  // namespace doa4::rb
