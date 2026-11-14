// Alpha-152's model on the character select: the hooks (what they do and why: doa4_alpha_select.h).
//
// Four hooks. With the option off (the default: Enable(true) is called only when its bytes are in guest memory at load)
// each is one flag test in front of, or behind, the game's own function.
//   sub_82A3DCB0  the game's boot copy of the 24th select cell (a static initialiser, run once before the game's main).
//                 After it, when the option was applied at load, Random's record is written into that cell
//                 (doa4_tweaks.h, FillAlpha152RandomCell); the record the game copied from is left as the game has it.
//   sub_824F4E90  called by the frame renderer sub_824F4878 while the frame's opening viewport is the current one: the
//                 whole screen's size is noted (the game's function then runs unchanged).
//   sub_82783BA8  runs a draw list. After the game's own run of it, when the call was the select callback's flush of a
//                 view whose player is shown and is character 16: her effect pass for that panel (RunPass).
//   sub_8231A3D8  builds a zoomed projection. During RunPass only, the one sub_8245ACF0 builds for the glow buffer is
//                 replaced by (the camera's projection) x (the zoom).
// Two more keep her aura from outliving her preview slot's file (the select hover crash; doa4_alpha_select.h, "Her aura
// while her preview slot is empty"):
//   sub_82378918  the game's aura pass. Around it: a slot that is hers and empty (its file word 0 after a load start or an
//                 unload, whichever route emptied it earlier in the frame) is released and holds a stand-in, so the pass
//                 builds no aura there (BeforeAuraPass, AfterAuraPass). The frame's effect update comes right after.
//   sub_82733A90  draws the effects. Only after a release: one line says whether the released aura was gone before this
//                 draw (StillListed); nothing is changed.
// Their log lines carry the frame (np::CurrentFrame in a harness run or an online session, -1 without one), so two peers'
// lines can be compared one for one.
//
// Guest state RunPass writes itself, all of it put back before it returns (so before the frame goes on, and long before
// the next frame's state hash): the gate (renderer + 1312), the renderer's viewport (24 bytes) and through
// sub_82758800 the device's, the camera's projection and view-projection (64 bytes each) and through sub_8276A6F0 the
// matrix store's projection, the device's scissor enable and rectangle and through sub_82758738 its window scissor.
// The first time the pass runs for a view (player 1's panel, player 2's panel), one log line says whether a snapshot of
// those fields taken before equals one taken after. What the game's own functions leave behind as they do in a match is
// not put back and needs no putting back: the pass's resources (made once), the zoom box the pass works out anew every
// time (0x82C4D0C4, 0x831A61CC), and the render states, shader constants and matrices of its own draws, which the next
// draw of the frame sets for itself.
// Every input is guest state, the option or the session's swimsuit state (np::SessionMixedSuitHolders: in a mixed
// session the pass stands down, doa4_alpha_select.h), so two PCs of an online session do the same thing on the same frame.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>

#include "doa4_alpha_select.h"
#include "doa4_tweaks.h"
#include "net/np_session.h"

extern "C" {
REX_EXTERN(__imp__sub_82A3DCB0);
REX_EXTERN(__imp__sub_824F4E90);
REX_EXTERN(__imp__sub_82783BA8);
REX_EXTERN(__imp__sub_8231A3D8);
REX_EXTERN(__imp__sub_8245A2B0);  // her pass's resources, made once (its own guard: 0x831A6174)
REX_EXTERN(__imp__sub_8245C860);  // the scene copy her body samples
REX_EXTERN(__imp__sub_8245C8D0);  // the pass, step 1: the zoom box of a player
REX_EXTERN(__imp__sub_8245ACF0);  // step 2: the glow buffer
REX_EXTERN(__imp__sub_8245C270);  // step 3: her body and the composite
REX_EXTERN(__imp__sub_8276A6F0);  // (matrix store, index, a 4x4 matrix by value)
REX_EXTERN(__imp__sub_82758738);  // (device, rectangle): the scissor rectangle
REX_EXTERN(__imp__sub_82758800);  // (device, viewport): the viewport
REX_EXTERN(__imp__sub_82378918);  // the aura pass
REX_EXTERN(__imp__sub_82733A90);  // the effect draw
}

namespace {
namespace as = doa4::alpha_select;

std::atomic<bool> g_enabled{false};
std::atomic<int32_t> g_screen_w{0}, g_screen_h{0};  // the frame's opening viewport (sub_824F4E90)
std::atomic<bool> g_said_first[as::kPanels] = {};  // by view: the "put back" line was written for that panel
std::atomic<bool> g_said_stand_down{false};
thread_local bool t_in_pass = false;  // RunPass is between its first and its last guest call on this thread

struct GuestMem {
  const uint8_t* base;
  uint8_t operator()(uint32_t address) const { return base[address]; }
};

uint32_t Get32(const uint8_t* base, uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); }
void Put32(uint8_t* base, uint32_t a, uint32_t v) {
  base[a] = uint8_t(v >> 24), base[a + 1] = uint8_t(v >> 16), base[a + 2] = uint8_t(v >> 8), base[a + 3] = uint8_t(v);
}
void GetFloats(const uint8_t* base, uint32_t a, float* out, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t u = Get32(base, a + 4 * i);
    std::memcpy(&out[i], &u, 4);
  }
}
void PutFloats(uint8_t* base, uint32_t a, const float* in, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t u;
    std::memcpy(&u, &in[i], 4);
    Put32(base, a + 4 * i, u);
  }
}

// Whether [address, address + size) is committed host memory that can be read: the device pointer is checked before any
// of its fields is used. The answer depends only on what the guest has mapped, which two PCs of a session share.
bool Committed(const uint8_t* base, uint32_t address, uint32_t size) {
  if (address < 0x10000 || size == 0 || address > 0xFFFFFFFFu - size) return false;
#ifdef _WIN32
  const uint8_t* p = base + address;
  const uint8_t* const end = p + size;
  while (p < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || mbi.Protect == 0 || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    p = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
  }
#endif
  return true;
}

// A guest call that takes stack arguments, or whose arguments lie in memory, gets a frame of its own below the hooked
// caller's (512 bytes: the callee may write its caller's parameter area) with the back chain set.
struct GuestFrame {
  PPCContext& ctx;
  PPCRegister r1;
  uint32_t sp;
  GuestFrame(PPCContext& c, uint8_t* base) : ctx(c), r1(c.r1), sp(c.r1.u32 - 512) {
    Put32(base, sp, r1.u32);
    ctx.r1.u64 = sp;
  }
  ~GuestFrame() { ctx.r1 = r1; }
};

// sub_8276A6F0(store, 1, the 64 bytes at kProjection by value), as the end of sub_8245ACF0 calls it: six doublewords in
// r5..r10, the last two on the stack at +80.
void SetProjection(PPCContext& ctx, uint8_t* base) {
  GuestFrame frame(ctx, base);
  ctx.r3.u64 = as::kMatrixStore;
  ctx.r4.u64 = 1;
  ctx.r5.u64 = rex::memory::load_and_swap<uint64_t>(base + as::kProjection);
  ctx.r6.u64 = rex::memory::load_and_swap<uint64_t>(base + as::kProjection + 8);
  ctx.r7.u64 = rex::memory::load_and_swap<uint64_t>(base + as::kProjection + 16);
  ctx.r8.u64 = rex::memory::load_and_swap<uint64_t>(base + as::kProjection + 24);
  ctx.r9.u64 = rex::memory::load_and_swap<uint64_t>(base + as::kProjection + 32);
  ctx.r10.u64 = rex::memory::load_and_swap<uint64_t>(base + as::kProjection + 40);
  std::memcpy(base + frame.sp + 80, base + as::kProjection + 48, 16);
  __imp__sub_8276A6F0(ctx, base);
}

void SetViewport(PPCContext& ctx, uint8_t* base, uint32_t device) {  // sub_82758800(device, the renderer's viewport)
  GuestFrame frame(ctx, base);
  ctx.r3.u64 = device;
  ctx.r4.u64 = as::kViewport;
  __imp__sub_82758800(ctx, base);
}

// The scissor test's enable and rectangle; sub_82758738 stores the rectangle and works the window scissor out again.
void SetScissor(PPCContext& ctx, uint8_t* base, uint32_t device, uint32_t on, const uint32_t* rect) {
  GuestFrame frame(ctx, base);
  Put32(base, device + as::kDeviceScissorOn, on);
  for (uint32_t i = 0; i < 4; ++i) Put32(base, frame.sp + 96 + 4 * i, rect[i]);
  ctx.r3.u64 = device;
  ctx.r4.u64 = frame.sp + 96;
  __imp__sub_82758738(ctx, base);
}

// The fields RunPass writes, for each view's first run's "put back" line.
struct Fields {
  uint32_t gate = 0, scissor_on = 0;
  uint8_t viewport[as::kViewportBytes] = {}, projection[64] = {}, view_projection[64] = {}, store_projection[64] = {};
  uint8_t scissor_rect[16] = {}, window[8] = {};
};
Fields ReadFields(const uint8_t* base, uint32_t device) {
  Fields f;
  f.gate = Get32(base, as::kGate);
  f.scissor_on = Get32(base, device + as::kDeviceScissorOn);
  std::memcpy(f.viewport, base + as::kViewport, sizeof(f.viewport));
  std::memcpy(f.projection, base + as::kProjection, 64);
  std::memcpy(f.view_projection, base + as::kViewProjection, 64);
  std::memcpy(f.store_projection, base + as::kMatrixStore + 64, 64);
  std::memcpy(f.scissor_rect, base + device + as::kDeviceScissorRect, 16);
  std::memcpy(f.window, base + device + as::kDeviceWindow, 8);
  return f;
}
std::string Differences(const Fields& a, const Fields& b) {
  std::string out;
  const auto add = [&out](bool same, const char* name) {
    if (!same) out += (out.empty() ? "" : ", ") + std::string(name);
  };
  add(a.gate == b.gate, "the gate");
  add(std::memcmp(a.viewport, b.viewport, sizeof(a.viewport)) == 0, "the viewport");
  add(std::memcmp(a.projection, b.projection, 64) == 0, "the projection");
  add(std::memcmp(a.view_projection, b.view_projection, 64) == 0, "the view-projection");
  add(std::memcmp(a.store_projection, b.store_projection, 64) == 0, "the matrix store's projection");
  add(a.scissor_on == b.scissor_on, "the scissor enable");
  add(std::memcmp(a.scissor_rect, b.scissor_rect, 16) == 0, "the scissor rectangle");
  add(std::memcmp(a.window, b.window, 8) == 0, "the window scissor");
  return out;
}

// Her pass for one select panel. The caller has checked everything Decide checks (so call.view is below kPanels).
void RunPass(PPCContext& ctx, uint8_t* base, const as::Call& call) {
  const uint32_t device = call.device;
  const bool first = !g_said_first[call.view].exchange(true, std::memory_order_relaxed);
  Fields before;
  if (first) before = ReadFields(base, device);
  const auto saved_r3 = ctx.r3;
  const auto saved_lr = ctx.lr;

  // What a stage's draw does for her at the start of a match frame: the resources, the gate, the scene copy.
  __imp__sub_8245A2B0(ctx, base);
  Put32(base, as::kGate, 1);
  __imp__sub_8245C860(ctx, base);

  // The pass as on a full screen: the viewport is the screen, the camera sees through the panel's rectangle.
  uint8_t viewport[as::kViewportBytes], projection[64], view_projection[64];
  uint32_t scissor_rect[4];
  std::memcpy(viewport, base + as::kViewport, sizeof(viewport));
  std::memcpy(projection, base + as::kProjection, 64);
  std::memcpy(view_projection, base + as::kViewProjection, 64);
  const uint32_t scissor_on = Get32(base, device + as::kDeviceScissorOn);
  for (uint32_t i = 0; i < 4; ++i) scissor_rect[i] = Get32(base, device + as::kDeviceScissorRect + 4 * i);
  const as::Fold fold = as::PanelFold(call.panel, call.screen);
  for (const uint32_t matrix : {as::kProjection, as::kViewProjection}) {
    float m[16];
    GetFloats(base, matrix, m, 16);
    as::FoldMatrix(m, fold);
    PutFloats(base, matrix, m, 16);
  }
  Put32(base, as::kViewport, 0);
  Put32(base, as::kViewport + 4, 0);
  Put32(base, as::kViewport + 8, uint32_t(call.screen.w));
  Put32(base, as::kViewport + 12, uint32_t(call.screen.h));
  SetViewport(ctx, base, device);
  SetProjection(ctx, base);

  // sub_8245D538's three steps (its two tests, the gate and a shown character 16, hold here).
  t_in_pass = true;
  ctx.r3.u64 = call.view;
  __imp__sub_8245C8D0(ctx, base);
  ctx.r3.u64 = call.view;
  __imp__sub_8245ACF0(ctx, base);  // the sub_8231A3D8 hook replaces its projection
  t_in_pass = false;
  const std::array<uint32_t, 4> panel = as::ScissorOf(call.panel);
  SetScissor(ctx, base, device, 1, panel.data());  // her body and the composite are cut at the panel's edge
  ctx.r3.u64 = call.view;
  __imp__sub_8245C270(ctx, base);

  // Everything back: scissor, viewport, the camera's matrices and the store's projection, the gate.
  SetScissor(ctx, base, device, scissor_on, scissor_rect);
  std::memcpy(base + as::kViewport, viewport, sizeof(viewport));
  SetViewport(ctx, base, device);
  std::memcpy(base + as::kProjection, projection, 64);
  std::memcpy(base + as::kViewProjection, view_projection, 64);
  SetProjection(ctx, base);
  Put32(base, as::kGate, call.gate);
  ctx.r3 = saved_r3;
  ctx.lr = saved_lr;

  if (first) {
    const std::string differences = Differences(before, ReadFields(base, device));
    REXLOG_INFO("[doa4][ALPHA152] select model: her pass ran for the first time for this view (view {}, panel {} {} {} {} of "
                "{}x{}); the state it changes was {}",
                call.view, call.panel.x, call.panel.y, call.panel.w, call.panel.h, call.screen.w, call.screen.h,
                differences.empty() ? "all put back" : "NOT all put back: " + differences);
  }
}

// ---- Her aura while her preview slot is empty. Both hooks run on the game's main thread. ----
struct GuestRW {
  uint8_t* base;
  uint8_t operator()(uint32_t address) const { return base[address]; }
  void Put8(uint32_t address, uint8_t value) const { base[address] = value; }
};
struct GuestOk {  // every guest pointer the aura code follows is checked first
  const uint8_t* base;
  bool operator()(uint32_t address, uint32_t size) const { return Committed(base, address, size); }
};
// A release waiting for the next effect draw's check: the aura's node and object (0: none), by slot.
std::atomic<uint32_t> g_pending_node[as::kSlots] = {}, g_pending_object[as::kSlots] = {};
as::SlotTally g_tally[as::kSlots];  // main thread only (the aura pass)

// The frame the log lines carry: the harness's or the online session's frame (the same number on both peers), -1 without one.
int64_t LogFrame() { return doa4::np::Active() ? int64_t(doa4::np::CurrentFrame()) : -1; }

void NoteHold(const as::AuraHold& hold) {
  const int64_t frame = LogFrame();
  for (uint32_t s = 0; s < as::kSlots; ++s) {
    const as::AuraHandle& aura = hold.released_aura[s];
    if (hold.released[s] == as::Release::kReleased) {
      g_pending_object[s].store(aura.object, std::memory_order_relaxed);
      g_pending_node[s].store(aura.node, std::memory_order_relaxed);
      REXLOG_INFO("[doa4][ALPHA152] frame {}: her aura on slot {} released before the aura pass (object {:#010x}, node "
                  "{:#010x})",
                  frame, s, aura.object, aura.node);
    }
    const as::TallyLines lines = as::Tally(g_tally[s], hold.held[s], hold.released[s]);
    if (lines.hold_ended != 0) {
      REXLOG_INFO("[doa4][ALPHA152] frame {}: her slot {}: the aura pass held for {} passes; it decides for itself again "
                  "(stand-downs on this slot so far: {})",
                  frame, s, lines.hold_ended, g_tally[s].stand_downs);
    }
    if (lines.stand_down_ended != 0) {
      REXLOG_WARN("[doa4][ALPHA152] frame {}: her slot {}: the release stood down on {} passes in a row (stand-downs on "
                  "this slot so far: {})",
                  frame, s, lines.stand_down_ended, g_tally[s].stand_downs);
    }
    if (lines.hold_started) {
      REXLOG_INFO("[doa4][ALPHA152] frame {}: her slot {} is empty (its file is not loaded): the aura pass builds no aura "
                  "there until it is",
                  frame, s);
    }
    if (lines.stand_down_started) {
      REXLOG_WARN("[doa4][ALPHA152] frame {}: her aura on slot {} could not be released before the aura pass (object "
                  "{:#010x}, node {:#010x}: not readable, or not an aura); the slot is left as the game has it until "
                  "this stops (stand-downs on this slot so far: {})",
                  frame, s, aura.object, aura.node, g_tally[s].stand_downs);
    }
  }
}

// At an effect draw: for each release since the last draw, was the released aura gone (destroyed by the effect update,
// with its spawners and particles) before this draw?
void CheckReleased(uint8_t* base) {
  for (uint32_t s = 0; s < as::kSlots; ++s) {
    const uint32_t node = g_pending_node[s].exchange(0, std::memory_order_relaxed);
    if (node == 0) continue;
    const uint32_t object = g_pending_object[s].load(std::memory_order_relaxed);
    switch (as::StillListed(GuestRW{base}, GuestOk{base}, node, object, as::kListLimit)) {
      case as::Found::kNo:
        REXLOG_INFO("[doa4][ALPHA152] frame {}: her released aura on slot {} was gone before the next effect draw",
                    LogFrame(), s);
        break;
      case as::Found::kYes:
        REXLOG_WARN("[doa4][ALPHA152] frame {}: her released aura on slot {} (node {:#010x}) is still in the effect list "
                    "at an effect draw: it was not destroyed first (the effect update did not run since the release)",
                    LogFrame(), s, node);
        break;
      case as::Found::kUnreadable:
        REXLOG_WARN("[doa4][ALPHA152] frame {}: her released aura on slot {}: the effect list could not be walked at the "
                    "draw",
                    LogFrame(), s);
        break;
    }
  }
}
}  // namespace

namespace doa4::alpha_select {
void Enable(bool on) { g_enabled.store(on, std::memory_order_release); }
}  // namespace doa4::alpha_select

// The game's boot copy of the 24th select cell, then (option applied at load only) Random's record into that cell. The
// app calls Enable before any guest code runs, so the flag is this process's final answer when the initialiser runs.
REX_HOOK_RAW(sub_82A3DCB0) {
  __imp__sub_82A3DCB0(ctx, base);
  const doa4::GuestBytes guest{[base](uint32_t address) { return base[address]; },
                               [base](uint32_t address, uint8_t value) { base[address] = value; }};
  const size_t written = doa4::tweaks::FillAlpha152RandomCell(guest, g_enabled.load(std::memory_order_acquire));
  if (written != 0) {
    REXLOG_INFO("[doa4][ALPHA152] the 24th select cell: Random's record written after the game's boot copy ({} bytes at "
                "{:#010x})",
                written, doa4::tweaks::kSelectLastCell);
  }
}

REX_HOOK_RAW(sub_824F4E90) {
  if (g_enabled.load(std::memory_order_relaxed)) {
    const std::optional<as::Size> screen =
        as::OpeningScreen(uint32_t(ctx.lr), int32_t(Get32(base, as::kViewport)), int32_t(Get32(base, as::kViewport + 4)),
                          int32_t(Get32(base, as::kViewport + 8)), int32_t(Get32(base, as::kViewport + 12)));
    if (screen) {
      g_screen_w.store(screen->w, std::memory_order_relaxed);
      g_screen_h.store(screen->h, std::memory_order_relaxed);
    }
  }
  __imp__sub_824F4E90(ctx, base);
}

REX_HOOK_RAW(sub_82783BA8) {
  const uint32_t lr = uint32_t(ctx.lr), sp = ctx.r1.u32;
  __imp__sub_82783BA8(ctx, base);
  if (lr != as::kFlushReturn || !g_enabled.load(std::memory_order_relaxed)) return;
  as::Call call = as::ReadCall(GuestMem{base}, ctx.r29.u32);  // r29: the callback's view index, kept across the call
  call.enabled = true;
  call.return_address = lr;
  call.sp = sp;
  call.screen = {g_screen_w.load(std::memory_order_relaxed), g_screen_h.load(std::memory_order_relaxed)};
  call.device_ok = Committed(base, call.device, as::kDeviceBytes);
  call.mixed_session = doa4::np::SessionMixedSuitHolders().has_value();  // fixed before guest code runs; equal on both PCs
  const as::Decision decision = as::Decide(call);
  if (decision.verdict == as::Verdict::kAct) {
    RunPass(ctx, base, call);
  } else if (decision.verdict == as::Verdict::kStandDown && !g_said_stand_down.exchange(true, std::memory_order_relaxed)) {
    if (decision.by_rule) {
      REXLOG_INFO("[doa4][ALPHA152] select model: not drawn (view {}): {}", call.view, decision.why);
    } else {
      REXLOG_WARN("[doa4][ALPHA152] select model: not drawn (view {}): {}", call.view, decision.why);
    }
  }
}

// sub_82378918, the aura pass (no arguments, no result): a slot that is hers and empty is released and holds the stand-in
// while the pass runs, then gets its handle back.
REX_HOOK_RAW(sub_82378918) {
  if (!g_enabled.load(std::memory_order_relaxed)) {
    __imp__sub_82378918(ctx, base);
    return;
  }
  GuestRW mem{base};
  const as::AuraHold hold = as::BeforeAuraPass(mem, true, GuestOk{base});
  __imp__sub_82378918(ctx, base);
  as::AfterAuraPass(mem, hold);
  NoteHold(hold);
}

// sub_82733A90, the effect draw: after a release, a line on whether the aura was gone first; the draw runs unchanged.
REX_HOOK_RAW(sub_82733A90) {
  if (g_enabled.load(std::memory_order_relaxed)) CheckReleased(base);
  __imp__sub_82733A90(ctx, base);
}

REX_HOOK_RAW(sub_8231A3D8) {
  if (!t_in_pass) {
    __imp__sub_8231A3D8(ctx, base);
    return;
  }
  const uint32_t lr = uint32_t(ctx.lr), out = ctx.r3.u32, offset = ctx.r8.u32, scale = ctx.r9.u32;
  __imp__sub_8231A3D8(ctx, base);
  if (!as::IsGlowProjectionCall(true, lr, offset, scale)) return;
  float built[16], camera[16], box[2], zoomed[16];
  GetFloats(base, out, built, 16);
  GetFloats(base, as::kProjection, camera, 16);
  GetFloats(base, as::kBoxScale, box, 2);
  as::ZoomProjection(built, camera, box[0], box[1], zoomed);
  PutFloats(base, out, zoomed, 16);
}
