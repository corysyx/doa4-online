// DOA4 rollback: the SDK's XMA decoder state in a snapshot (rb_xma.h).
#include "rb_xma.h"

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include <rex/audio/audio_system.h>
#include <rex/audio/xma/context.h>
#include <rex/audio/xma/decoder.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

namespace doa4::rb {
namespace {

using rex::audio::XmaContext;
using rex::audio::XmaDecoder;

// The decoder's and the contexts' state is private or protected in the SDK. An explicit instantiation may name a
// private member ([temp.spec.general]), so each member's pointer is taken there and handed out through a friend.
template <typename Tag, typename Tag::type M>
struct Rob {
  friend typename Tag::type Get(Tag) { return M; }
};
#define DOA4_RB_ROB(Name, Class, Type, Member) \
  struct Name {                                \
    using type = Type Class::*;                \
    friend type Get(Name);                     \
  };                                           \
  template struct Rob<Name, &Class::Member>

constexpr uint32_t kContexts = 320;  // XmaDecoder::kContextCount
using ContextArray = XmaContext[kContexts];
using InputBuffer = std::array<uint8_t, XmaContext::kBytesPerPacketData * 2>;
using FrameBuffer = std::array<uint8_t, 1 + 4096>;
using RawFrame = std::array<uint8_t, XmaContext::kBytesPerFrameChannel * 2>;

DOA4_RB_ROB(DecContexts, XmaDecoder, ContextArray, contexts_);
DOA4_RB_ROB(DecBitmap, XmaDecoder, rex::bit::BitMap, context_bitmap_);
DOA4_RB_ROB(DecRegs, XmaDecoder, rex::audio::XmaRegisterFile, register_file_);
DOA4_RB_ROB(CtxLock, XmaContext, std::mutex, lock_);
DOA4_RB_ROB(CtxAllocated, XmaContext, std::atomic<bool>, is_allocated_);
DOA4_RB_ROB(CtxEnabled, XmaContext, std::atomic<bool>, is_enabled_);
DOA4_RB_ROB(CtxInput, XmaContext, InputBuffer, input_buffer_);
DOA4_RB_ROB(CtxFrame, XmaContext, FrameBuffer, xma_frame_);
DOA4_RB_ROB(CtxRaw, XmaContext, RawFrame, raw_frame_);
DOA4_RB_ROB(CtxRemainingBlocks, XmaContext, int32_t, remaining_subframe_blocks_in_output_buffer_);
DOA4_RB_ROB(CtxRemainingSubframes, XmaContext, uint8_t, current_frame_remaining_subframes_);
DOA4_RB_ROB(CtxLoopLimit, XmaContext, uint8_t, loop_frame_output_limit_);
DOA4_RB_ROB(CtxLoopSkip, XmaContext, bool, loop_start_skip_pending_);
#undef DOA4_RB_ROB

struct ContextState {
  bool allocated = false, enabled = false;
  InputBuffer input{};
  FrameBuffer frame{};
  RawFrame raw{};
  int32_t remaining_blocks = 0;
  uint8_t remaining_subframes = 0, loop_limit = 0;
  bool loop_skip = false;

  bool operator==(const ContextState& o) const {
    return allocated == o.allocated && enabled == o.enabled && remaining_blocks == o.remaining_blocks &&
           remaining_subframes == o.remaining_subframes && loop_limit == o.loop_limit && loop_skip == o.loop_skip &&
           input == o.input && frame == o.frame && raw == o.raw;
  }
};

struct Saved {
  bool valid = false;
  std::unique_ptr<rex::audio::XmaRegisterFile> regs;
  std::vector<uint64_t> bitmap;
  std::vector<ContextState> contexts;
};
Saved g_saved;

XmaDecoder* Decoder() {
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->emulator()) return nullptr;
  auto* audio = static_cast<rex::audio::AudioSystem*>(ks->emulator()->audio_system());
  return audio ? audio->xma_decoder() : nullptr;
}

void Read(XmaContext& c, ContextState* s) {
  s->allocated = (c.*Get(CtxAllocated{})).load(std::memory_order_acquire);
  s->enabled = (c.*Get(CtxEnabled{})).load(std::memory_order_acquire);
  s->input = c.*Get(CtxInput{});
  s->frame = c.*Get(CtxFrame{});
  s->raw = c.*Get(CtxRaw{});
  s->remaining_blocks = c.*Get(CtxRemainingBlocks{});
  s->remaining_subframes = c.*Get(CtxRemainingSubframes{});
  s->loop_limit = c.*Get(CtxLoopLimit{});
  s->loop_skip = c.*Get(CtxLoopSkip{});
}

void Write(XmaContext& c, const ContextState& s) {
  (c.*Get(CtxAllocated{})).store(s.allocated, std::memory_order_release);
  (c.*Get(CtxEnabled{})).store(s.enabled, std::memory_order_release);
  c.*Get(CtxInput{}) = s.input;
  c.*Get(CtxFrame{}) = s.frame;
  c.*Get(CtxRaw{}) = s.raw;
  c.*Get(CtxRemainingBlocks{}) = s.remaining_blocks;
  c.*Get(CtxRemainingSubframes{}) = s.remaining_subframes;
  c.*Get(CtxLoopLimit{}) = s.loop_limit;
  c.*Get(CtxLoopSkip{}) = s.loop_skip;
}

}  // namespace

bool SaveXma() {
  XmaDecoder* d = Decoder();
  if (!d) return false;
  ContextArray& contexts = d->*Get(DecContexts{});
  if (!g_saved.regs) g_saved.regs = std::make_unique<rex::audio::XmaRegisterFile>();
  *g_saved.regs = d->*Get(DecRegs{});
  g_saved.bitmap = (d->*Get(DecBitmap{})).data();
  g_saved.contexts.resize(kContexts);
  for (uint32_t i = 0; i < kContexts; ++i) {
    std::lock_guard<std::mutex> lock(contexts[i].*Get(CtxLock{}));
    Read(contexts[i], &g_saved.contexts[i]);
  }
  g_saved.valid = true;
  return true;
}

int RestoreXma() {
  XmaDecoder* d = Decoder();
  if (!d || !g_saved.valid) return -1;
  ContextArray& contexts = d->*Get(DecContexts{});
  d->*Get(DecRegs{}) = *g_saved.regs;
  (d->*Get(DecBitmap{})).data() = g_saved.bitmap;
  int changed = 0;
  ContextState now;
  for (uint32_t i = 0; i < kContexts; ++i) {
    std::lock_guard<std::mutex> lock(contexts[i].*Get(CtxLock{}));
    Read(contexts[i], &now);
    if (now == g_saved.contexts[i]) continue;
    ++changed;
    Write(contexts[i], g_saved.contexts[i]);
  }
  return changed;
}

}  // namespace doa4::rb
