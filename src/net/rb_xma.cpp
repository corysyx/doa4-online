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
DOA4_RB_ROB(CtxGuestPtr, XmaContext, uint32_t, guest_ptr_);
#undef DOA4_RB_ROB

struct ContextState {
  bool allocated = false, enabled = false;
  InputBuffer input{};
  FrameBuffer frame{};
  RawFrame raw{};
  int32_t remaining_blocks = 0;
  uint8_t remaining_subframes = 0, loop_limit = 0;
  bool loop_skip = false;
  // The guest side, in physical memory: the 64-byte XMA_CONTEXT_DATA (read and write offsets, valid bits) and the
  // output buffer the decoder writes (block count x 256 bytes). Only for a context allocated at the save.
  uint32_t guest_ptr = 0, output_ptr = 0;
  std::vector<uint8_t> data, output;
  // The two input buffers (packet count x 2048 bytes each): the compressed packets the decoder reads. The game's
  // streaming fills them by file reads the host does, which the store marks never see (round 18: the replay's first
  // callback found one output block less and the voice starved, 400E67CC 1->4).
  uint32_t input_ptr[2] = {0, 0};
  std::vector<uint8_t> inputs[2];

  bool operator==(const ContextState& o) const {
    return allocated == o.allocated && enabled == o.enabled && remaining_blocks == o.remaining_blocks &&
           remaining_subframes == o.remaining_subframes && loop_limit == o.loop_limit && loop_skip == o.loop_skip &&
           input == o.input && frame == o.frame && raw == o.raw;
  }
};

// Round 19: the context array lives in a heap the SDK maps with a host offset (4 KB on Windows), so the address must go
// through the SDK's own translation; adding the guest address to the membase by hand read 64 unrelated bytes per
// context (0.3.119-0.3.122 saved and put back the wrong memory and took the buffer pointers from it).
rex::memory::Memory* Mem() {
  auto* ks = rex::system::kernel_state();
  return ks ? ks->memory() : nullptr;
}
uint8_t* Virt(uint32_t addr) { return Mem()->TranslateVirtual<uint8_t*>(addr); }
uint8_t* Phys(uint32_t addr) { return Mem()->TranslatePhysical<uint8_t*>(addr); }
uint32_t Be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
constexpr uint32_t kPacketBytes = 2048;
constexpr uint32_t kMaxInputBytes = 4u << 20;  // a buffer larger than this is not copied (logged)

size_t Differ(const uint8_t* a, const uint8_t* b, size_t n) {
  size_t d = 0;
  for (size_t i = 0; i < n; ++i) d += a[i] != b[i];
  return d;
}

struct Saved {
  bool valid = false;
  size_t guest_bytes = 0;
  size_t too_large = 0;
  std::unique_ptr<rex::audio::XmaRegisterFile> regs;
  std::vector<uint64_t> bitmap;
  std::vector<ContextState> contexts;
};
Saved g_saved;
XmaRestoreStats g_stats;

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

bool SaveXma(uint8_t* base) {
  XmaDecoder* d = Decoder();
  if (!d) return false;
  g_saved.too_large = 0;
  ContextArray& contexts = d->*Get(DecContexts{});
  if (!g_saved.regs) g_saved.regs = std::make_unique<rex::audio::XmaRegisterFile>();
  *g_saved.regs = d->*Get(DecRegs{});
  g_saved.bitmap = (d->*Get(DecBitmap{})).data();
  g_saved.contexts.resize(kContexts);
  for (uint32_t i = 0; i < kContexts; ++i) {
    std::lock_guard<std::mutex> lock(contexts[i].*Get(CtxLock{}));
    ContextState& st = g_saved.contexts[i];
    Read(contexts[i], &st);
    st.guest_ptr = contexts[i].*Get(CtxGuestPtr{});
    st.data.clear();
    st.output.clear();
    st.output_ptr = 0;
    for (int b = 0; b < 2; ++b) {
      st.input_ptr[b] = 0;
      st.inputs[b].clear();
    }
    if (st.allocated && st.guest_ptr && base && Mem()) {
      const uint8_t* ctx_data = Virt(st.guest_ptr);
      st.data.assign(ctx_data, ctx_data + 64);
      const uint32_t dw0 = Be32(ctx_data), dw1 = Be32(ctx_data + 4);
      const uint32_t blocks = (dw0 >> 22) & 0x1F;  // output_buffer_block_count
      st.output_ptr = Be32(ctx_data + 0x1C);       // DWORD 7: output_buffer_ptr
      if (st.output_ptr && blocks) {
        const uint8_t* out = Phys(st.output_ptr);
        st.output.assign(out, out + blocks * 256u);
      }
      const uint32_t packets[2] = {dw0 & 0xFFF, dw1 & 0xFFF};  // input_buffer_0/1_packet_count
      const uint32_t ptrs[2] = {Be32(ctx_data + 0x14), Be32(ctx_data + 0x18)};  // DWORD 5, 6
      for (int b = 0; b < 2; ++b) {
        const uint32_t bytes = packets[b] * kPacketBytes;
        if (!ptrs[b] || !bytes) continue;
        if (bytes > kMaxInputBytes) {
          ++g_saved.too_large;
          continue;
        }
        st.input_ptr[b] = ptrs[b];
        const uint8_t* in = Phys(ptrs[b]);
        st.inputs[b].assign(in, in + bytes);
      }
    }
  }
  g_saved.guest_bytes = 0;
  for (const ContextState& st : g_saved.contexts)
    g_saved.guest_bytes += st.data.size() + st.output.size() + st.inputs[0].size() + st.inputs[1].size();
  g_saved.valid = true;
  return true;
}

int RestoreXma(uint8_t* base) {
  XmaDecoder* d = Decoder();
  if (!d || !g_saved.valid) return -1;
  ContextArray& contexts = d->*Get(DecContexts{});
  d->*Get(DecRegs{}) = *g_saved.regs;
  (d->*Get(DecBitmap{})).data() = g_saved.bitmap;
  int changed = 0;
  g_stats = {};
  g_stats.too_large = g_saved.too_large;
  ContextState now;
  for (uint32_t i = 0; i < kContexts; ++i) {
    std::lock_guard<std::mutex> lock(contexts[i].*Get(CtxLock{}));
    const ContextState& st = g_saved.contexts[i];
    if (base && Mem() && st.guest_ptr && st.data.size() == 64) {
      uint8_t* ctx_data = Virt(st.guest_ptr);
      g_stats.data_differ += Differ(ctx_data, st.data.data(), 64);
      std::memcpy(ctx_data, st.data.data(), 64);
      if (st.output_ptr && !st.output.empty()) {
        uint8_t* out = Phys(st.output_ptr);
        g_stats.output_differ += Differ(out, st.output.data(), st.output.size());
        std::memcpy(out, st.output.data(), st.output.size());
      }
      for (int b = 0; b < 2; ++b) {
        if (!st.input_ptr[b] || st.inputs[b].empty()) continue;
        uint8_t* in = Phys(st.input_ptr[b]);
        g_stats.input_differ += Differ(in, st.inputs[b].data(), st.inputs[b].size());
        g_stats.input_bytes += st.inputs[b].size();
        std::memcpy(in, st.inputs[b].data(), st.inputs[b].size());
      }
    }
    Read(contexts[i], &now);
    if (now == st) continue;
    ++changed;
    Write(contexts[i], st);
  }
  return changed;
}

size_t SavedXmaGuestBytes() { return g_saved.valid ? g_saved.guest_bytes : 0; }
XmaRestoreStats LastXmaRestore() { return g_stats; }

}  // namespace doa4::rb
