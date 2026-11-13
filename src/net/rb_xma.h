// DOA4 rollback (corysyx/doa4-online): the SDK's XMA decoder state goes back with a snapshot.
//
// The game's sound library (sub_82976898 and the functions around it, run by the SDK audio worker's callback) reads
// how far each XMA context has decoded (XMAGetOutputBufferWriteOffset) to move its voices on. The decoded amount
// depends on host state of the SDK's XmaContext that guest memory does not hold: the packet bytes carried over, the
// current frame's decoded samples and the subframes still to write out (remaining_subframe_blocks_in_output_buffer_,
// current_frame_remaining_subframes_), the loop state, and the decoder's register file and allocation bitmap. Round
// 16's store watch showed the voice positions (e.g. 400E809C) moving 0x80, 0xFF, 0x100 in the replay's first frame
// against 0x100 each in the first pass. SaveXma / RestoreXma copy that state for all 320 contexts. The ffmpeg decoder
// inside each context is not copied: it shapes the decoded sample values, not how many there are.
#pragma once

#include <cstddef>
#include <cstdint>

namespace doa4::rb {

// False when the SDK's audio system or XMA decoder is not there.
// base: the guest memory base. With it the allocated contexts' XMA_CONTEXT_DATA and output buffers in physical memory
// go into the snapshot too (round 17: the host state alone changed nothing).
bool SaveXma(uint8_t* base);
// The number of contexts whose host state the restore changed (-1: nothing saved).
int RestoreXma(uint8_t* base);
// The guest bytes the last save copied (context data and output buffers).
size_t SavedXmaGuestBytes();

}  // namespace doa4::rb
