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

#include <cstdint>

namespace doa4::rb {

// False when the SDK's audio system or XMA decoder is not there.
bool SaveXma();
// The number of contexts whose host state the restore changed (-1: nothing saved).
int RestoreXma();

}  // namespace doa4::rb
