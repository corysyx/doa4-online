// Alpha-152's model on the character select (the Extras option "Alpha-152 playable", doa4_tweaks.h kAlpha152).
//
// Why she is missing there: the select screen draws its fighters through the generic draw sub_8234F9A8, which leaves out
// the body of character 16 on purpose; her body is drawn by her own effect pass (sub_8245D718 -> sub_8245D538: the zoom
// box sub_8245C8D0, the glow buffer sub_8245ACF0, the body and the composite sub_8245C270), and that pass runs only when
// its gate (renderer + 1312) is set, which the stage draw does (sub_82566920 -> sub_8245D628) and the select screen, having
// no stage, never does. The study: notes/research/alpha152_select_model_2026-11-12.md.
//
// What the hooks do (doa4_alpha_select_hooks.cpp), only when the option was applied at load in this process: right after
// the select draw callback sub_8252B020 has flushed a view (its one sub_82783BA8 call) whose player is shown and is
// character 16, her pass is run once for that panel: the pass's resources (sub_8245A2B0, made once), the gate set, the
// scene copy her see-through body samples (sub_8245C860), then the three steps of sub_8245D538, and everything put back.
// Two things differ from a match, because a panel is a window on the screen and a match is the whole screen:
// - the pass runs as on a full screen: the viewport is the whole screen and the camera's projection and view-projection
//   are folded with the panel's rectangle (PanelFold, FoldMatrix), so every pixel of her lands where the panel put it and
//   her body's look-up into the scene copy, which goes by screen position, finds the place behind her; the device's
//   scissor test cuts her at the panel's edge, since the viewport no longer does;
// - the glow buffer's projection, which sub_8245ACF0 builds from the field of view and the video mode's aspect alone, is
//   replaced by (the camera's projection) x (the zoom) (ZoomProjection): in a match the two are the same matrix, in a
//   panel only the second matches the body.
//
// The gate is 0 on every select frame (no stage), so the game's own call of the pass (sub_8245D718, earlier in the frame
// than the select callback) does nothing there, and nothing has to be skipped. When the gate is already set a stage is
// being drawn in this frame and the game's own pass is at work: the hook stands down (Decide).
//
// Online: every input of these decisions is guest state, the option, which a session carries from the host to both
// PCs (np_handoff GuestSettingNames holds every tweak's cvar), or the session's swimsuit state from the same hand-off;
// no host clock, no host setting. The hooks run on the game's main thread inside the frame's draw. In a session whose
// swimsuits are mixed (the two players' imports differ) the pass stands down on both PCs: there a character's preview can
// load on different frames on the two PCs, and her pass and body draw reach random generators that are not all made
// equal again. She stays playable in such a session; her select panel stays empty.
//
// This header is the part a host test can reach (tests/host/alpha_select_host_test.cpp): what is read, when the hook
// acts, and the arithmetic. Header-only, no SDK.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace doa4::alpha_select {

inline constexpr uint32_t kRenderer = 0x83579C28;     // the game's renderer object (sub_8252B020, sub_8245D718)
inline constexpr uint32_t kViewport = kRenderer + 4;  // be32 x, y, width, height, then float min z, max z
inline constexpr uint32_t kViewportBytes = 24;
inline constexpr uint32_t kDevice = kRenderer + 60;  // be32: the D3D device
inline constexpr uint32_t kGate = kRenderer + 1312;  // be32: her effect pass runs only while it is not 0
inline constexpr uint32_t kPlayers = 0x83033B40;     // one record per player (sub_8245D598: 0x83033B40 + player * 156)
inline constexpr uint32_t kPlayerSize = 156;
inline constexpr uint32_t kVisibleOffset = 32;    // u8: the player is shown
inline constexpr uint32_t kCharacterOffset = 1;   // u8: the charId
inline constexpr uint8_t kAlphaCharacter = 16;
inline constexpr uint32_t kPanels = 2;  // views 0 and 1: player 1's and player 2's panel (a Tag select's views 2 and 3 are not drawn)
// Return addresses: sub_8252B020's call of sub_82783BA8 (the flush of one view), sub_824F4878's call of sub_824F4E90 (made
// while the frame's opening viewport, the whole screen, is the current one) and sub_8245ACF0's call of sub_8231A3D8.
inline constexpr uint32_t kFlushReturn = 0x8252B15C;
inline constexpr uint32_t kFrameOpenReturn = 0x824F4958;
inline constexpr uint32_t kGlowProjectionReturn = 0x8245AE18;
inline constexpr uint32_t kBoxScale = 0x82C4D0C4;        // 4 floats: the zoom box's x and y scale, then two fixed values
inline constexpr uint32_t kBoxOffset = 0x831A61CC;       // 4 floats: the zoom box's x and y offset, then two fixed values
inline constexpr uint32_t kProjection = 0x830367D0;      // 16 floats: the camera's projection (sub_8231D750 copies it there)
inline constexpr uint32_t kViewProjection = 0x830335F0;  // 16 floats: the matrix sub_8245C8D0 projects the box corners with
inline constexpr uint32_t kMatrixStore = 0x8357B3C0;     // sub_8276A6F0's matrices, 64 bytes each; index 1 is the projection
inline constexpr uint32_t kDeviceBytes = 13000;          // the part of the device the offsets below lie in
inline constexpr uint32_t kDeviceScissorOn = 11952;      // be32: the scissor test's enable (read by sub_82758738)
inline constexpr uint32_t kDeviceScissorRect = 12832;    // 4 be32: left, top, right, bottom (stored by sub_82758738)
inline constexpr uint32_t kDeviceWindow = 11524;         // 2 be32: the packed window scissor sub_82758738 works out
inline constexpr uint32_t kStackFirst = 0x70001000, kStackEnd = 0x80000000;  // where a guest stack pointer may be

struct Rect {
  int32_t x = 0, y = 0, w = 0, h = 0;
};
struct Size {
  int32_t w = 0, h = 0;
};

// One call of the flush hook. ReadCall fills the guest state; the hook adds what only it knows.
struct Call {
  bool enabled = false;         // the option was applied at load
  uint32_t return_address = 0;  // of the sub_82783BA8 call
  uint32_t view = 0;            // the callback's view index (its r29)
  uint8_t visible = 0, character = 0;  // the view's player
  uint32_t gate = 0;
  Rect panel;   // the viewport now: the view's panel
  Size screen;  // the frame's opening viewport (0 x 0: not seen yet)
  uint32_t device = 0;
  bool device_ok = false;  // the device's bytes can be read and written
  uint32_t sp = 0;         // the guest stack pointer
  // An online session whose two players' swimsuit imports differ (the hand-off's suit state, the same on both PCs:
  // np::SessionMixedSuitHolders). False offline, in a replay and in every other session.
  bool mixed_session = false;
};

namespace detail {
template <class Mem>
uint32_t Be32(const Mem& mem, uint32_t a) {
  return (uint32_t(mem(a)) << 24) | (uint32_t(mem(a + 1)) << 16) | (uint32_t(mem(a + 2)) << 8) | uint32_t(mem(a + 3));
}
}  // namespace detail

// The guest state of a flush for `view`. `mem(address)` gives a guest byte. A view that is no panel of ours reads nothing.
template <class Mem>
Call ReadCall(const Mem& mem, uint32_t view) {
  Call c;
  c.view = view;
  if (view >= kPanels) return c;
  const uint32_t player = kPlayers + view * kPlayerSize;
  c.visible = mem(player + kVisibleOffset);
  c.character = mem(player + kCharacterOffset);
  c.gate = detail::Be32(mem, kGate);
  c.panel = {int32_t(detail::Be32(mem, kViewport)), int32_t(detail::Be32(mem, kViewport + 4)),
             int32_t(detail::Be32(mem, kViewport + 8)), int32_t(detail::Be32(mem, kViewport + 12))};
  c.device = detail::Be32(mem, kDevice);
  return c;
}

enum class Verdict : uint8_t {
  kNotOurs,    // not her select panel: nothing to do and nothing to say
  kAct,        // run her pass for this panel
  kStandDown,  // her panel, but a precondition fails or a ruling says no: draw nothing extra (the panel stays empty), say why once
};
struct Decision {
  Verdict verdict = Verdict::kNotOurs;
  const char* why = "";  // only for kStandDown
  bool by_rule = false;  // only for kStandDown: a ruling, not a fault (said once as information, not as a warning)
};

inline bool PanelInside(const Rect& p, const Size& s) {
  return p.w > 0 && p.h > 0 && p.x >= 0 && p.y >= 0 && p.x <= s.w - p.w && p.y <= s.h - p.h;
}

inline Decision Decide(const Call& c) {
  if (!c.enabled || c.return_address != kFlushReturn || c.view >= kPanels) return {};
  if (c.visible == 0 || c.character != kAlphaCharacter) return {};
  // A mixed-swimsuit session: character previews load on frames that can differ between the two PCs there, and her pass
  // and body draw reach random generators that are not all made equal again, so the pass stands down (a ruling: she
  // stays playable, her panel stays empty). Before every precondition: it is the first thing true of such a session.
  if (c.mixed_session) {
    return {Verdict::kStandDown, "the session's swimsuits are mixed (her pass stands down there; the panel stays empty)", true};
  }
  if (c.gate != 0) return {Verdict::kStandDown, "the effect pass's gate is already set (a stage is being drawn)"};
  if (c.screen.w <= 0 || c.screen.h <= 0) return {Verdict::kStandDown, "the frame's opening viewport was not seen"};
  if (!PanelInside(c.panel, c.screen)) return {Verdict::kStandDown, "the view's viewport is not inside the screen"};
  if (c.panel.w == c.screen.w && c.panel.h == c.screen.h) {
    return {Verdict::kStandDown, "the view's viewport is the whole screen, not a panel"};
  }
  if (!c.device_ok) return {Verdict::kStandDown, "the device cannot be read"};
  if (c.sp < kStackFirst || c.sp >= kStackEnd) return {Verdict::kStandDown, "the guest stack pointer is out of range"};
  return {Verdict::kAct, ""};
}

// The whole screen's size, from the viewport at sub_824F4E90's call by the frame renderer; nothing for another caller or
// a viewport that is not a screen.
inline std::optional<Size> OpeningScreen(uint32_t return_address, int32_t x, int32_t y, int32_t w, int32_t h) {
  if (return_address != kFrameOpenReturn || x != 0 || y != 0) return std::nullopt;
  if (w < 320 || w > 8192 || h < 240 || h > 8192) return std::nullopt;
  return Size{w, h};
}

// The panel's rectangle in the screen's clip units (x right, y up): x' = kx * x + tx * w, y' = ky * y + ty * w.
struct Fold {
  float kx = 1.0f, ky = 1.0f, tx = 0.0f, ty = 0.0f;
};
inline Fold PanelFold(const Rect& p, const Size& s) {
  Fold f;
  f.kx = float(p.w) / float(s.w);
  f.ky = float(p.h) / float(s.h);
  f.tx = float(2 * p.x + p.w) / float(s.w) - 1.0f;
  f.ty = 1.0f - float(2 * p.y + p.h) / float(s.h);
  return f;
}

// (m) x (fold), in place. m is 16 floats row by row, a vector being a row (clip = v x m): each row's x and y take the
// fold, its z and w stay.
inline void FoldMatrix(float* m, const Fold& f) {
  for (int row = 0; row < 4; ++row) {
    const float x = m[4 * row], y = m[4 * row + 1], w = m[4 * row + 3];
    m[4 * row] = f.kx * x + f.tx * w;
    m[4 * row + 1] = f.ky * y + f.ty * w;
  }
}

// The glow buffer's projection: (camera) x (zoom), the zoom being clip x' = sx * x + bx * w, y' = sy * y + by * w. sx and
// sy are the zoom box's scale; bx and by are taken from the matrix the game built (`built`, its row 2: a projection whose
// w is z holds the offset there), so the offset is the game's own, sign and all.
inline void ZoomProjection(const float* built, const float* camera, float sx, float sy, float* out) {
  const float bx = built[8], by = built[9];
  for (int row = 0; row < 4; ++row) {
    out[4 * row] = sx * camera[4 * row] + bx * camera[4 * row + 3];
    out[4 * row + 1] = sy * camera[4 * row + 1] + by * camera[4 * row + 3];
    out[4 * row + 2] = camera[4 * row + 2];
    out[4 * row + 3] = camera[4 * row + 3];
  }
}

// The device's scissor rectangle for a panel: left, top, right, bottom.
inline std::array<uint32_t, 4> ScissorOf(const Rect& p) {
  return {uint32_t(p.x), uint32_t(p.y), uint32_t(p.x + p.w), uint32_t(p.y + p.h)};
}

// Whether a sub_8231A3D8 call is the glow buffer's projection being built inside her select pass (r8 and r9 are the
// call's offset and scale pointers). Outside the pass, in a match too, nothing is replaced.
inline bool IsGlowProjectionCall(bool in_select_pass, uint32_t return_address, uint32_t r8, uint32_t r9) {
  return in_select_pass && return_address == kGlowProjectionReturn && r8 == kBoxOffset && r9 == kBoxScale;
}

// ======== Her aura while her preview slot is empty (the select hover crash) ========
//
// The crash (notes/research/alpha152_hover_crash_2026-11-12.md; the reap rule: ou_runs/alpha_opt/fix4/reap_rule.txt): the
// game's aura pass sub_82378918 (run every frame by sub_82354DB0) builds her aura for every shown slot whose character is
// 16; the aura's spawners and particles step through her sprite frames in the slot's character file (0x830B5340[slot]).
// On the select screen a cursor move onto another loadable cell reloads the player's preview slot: the slot loader tick
// sub_823FE150 sets the slot's file word to 0 when the load starts (recomp.9.cpp:53185), but the slot's character byte
// stays 16 until the load ENDS (sub_82354F80), so her aura lives on and its next step reads the empty word (and a new aura
// built on the empty slot would read it in its own constructor). The game never meets this in a match of its own, where
// she is not on the select grid.
//
// What the hooks do, only when the option was applied at load in this process (all of it decided from guest state): around
// the aura pass (sub_82378918), a slot that is hers and empty is released first, the way the aura pass releases one
// (ReleaseAura), and while the pass runs its handle holds a stand-in, so the pass builds no aura there; the handle is put
// back right after (BeforeAuraPass, AfterAuraPass). Once her file is loaded again the pass builds her aura as it always
// does. This one place covers every route that empties the word (the load start in sub_823FE150, the unload
// sub_823FEC28, sub_823FBD00): whichever of them ran earlier in the frame, the frame's first reader of her aura is the
// effect update sub_82733BC8, which sub_82354DB0 calls right after the pass (recomp.3.cpp:35396-35416), so the release is
// made before her aura or any of its particles is stepped on the emptied slot. (The unload sub_823FEC28 is not hooked:
// a cursor move on the select screen does not reach it, and it runs in the game's own Story fight against her.)
//
// Why it is safe where the game has her without the option (her Story fight, the one place the unpatched game builds her
// aura): the wrap writes only when her slot's character byte is 16 AND its file word is 0 (HerSlotEmpty). That is exactly
// the state in which the unpatched pass would build an aura whose constructor reads the empty entry, or her live aura and
// particles would step through it; in any other state it writes nothing. So in that fight it can act only where the game
// itself would have read an empty entry.
//
// When the release is reaped: a released aura (owner 0, life 0) is destroyed by the effect update sub_82733BC8 together
// with every spawner and particle it owns (they live in its own sub-manager), without their updates being called. But
// sub_82354DB0 runs that update only when the byte 0x82FCCC3F is 0 and the word 0x831013D8 is not 1 (recomp.3.cpp:
// 35397-35409). In a frame where either fails, the released aura stays in the effect list: it is drawn by the effect
// draw, but not stepped, so nothing of it reads the empty entry; it is reaped by the first frame whose update runs. The
// draw-time check (StillListed) logs whether it was gone before the next effect draw.
inline constexpr uint32_t kSlots = 4;
inline constexpr uint32_t kSlotTable = 0x8302BB34;  // 8 bytes per slot (sub_82378918: r26 + 8 * slot)
inline constexpr uint32_t kSlotTableStride = 8;
inline constexpr uint32_t kSlotCharacter = 2;        // u8 at +2: the slot's character
inline constexpr uint32_t kSlotFiles = 0x830B5340;  // be32 per slot: the slot's character file (0: not loaded)
inline constexpr uint32_t kAuras = 0x83303BBC;      // 12 bytes per slot: her aura's handle {object, node, flag}
inline constexpr uint32_t kAuraHandleBytes = 12;
inline constexpr uint32_t kAuraVtable = 0x822BA024;  // the aura object's vtable (sub_827349F8)
inline constexpr uint32_t kEffects = 0x8357D748;     // the effect manager (sub_82733980); +4: its active list
inline constexpr uint32_t kListLimit = 65536;         // nodes walked by StillListed before it gives up
struct AuraHandle {
  uint32_t object = 0, node = 0, flag = 0;
};
enum class Release : uint8_t {
  kNothing,    // no aura in the handle
  kReleased,   // released as the game's aura pass releases one
  kStandDown,  // the aura's object or node cannot be read, or the object is not an aura: nothing written
};
enum class Found : uint8_t { kNo, kYes, kUnreadable };
// What BeforeAuraPass did per slot, for AfterAuraPass and the log.
struct AuraHold {
  std::array<bool, kSlots> held{};             // the handle holds the stand-in during the pass
  std::array<AuraHandle, kSlots> saved{};      // the handle as it was before the stand-in went in
  std::array<Release, kSlots> released{};      // the release made before the pass
  std::array<AuraHandle, kSlots> released_aura{};  // the handle as it was before that release (for the draw-time check)
};

namespace detail {
template <class Mem>
void Put32(Mem& mem, uint32_t a, uint32_t v) {
  for (uint32_t i = 0; i < 4; ++i) mem.Put8(a + i, uint8_t(v >> (24 - 8 * i)));
}
}  // namespace detail

template <class Mem>
AuraHandle ReadAura(const Mem& mem, uint32_t slot) {
  const uint32_t h = kAuras + kAuraHandleBytes * slot;
  return {detail::Be32(mem, h), detail::Be32(mem, h + 4), detail::Be32(mem, h + 8)};
}
template <class Mem>
void WriteAura(Mem& mem, uint32_t slot, const AuraHandle& a) {
  const uint32_t h = kAuras + kAuraHandleBytes * slot;
  detail::Put32(mem, h, a.object);
  detail::Put32(mem, h + 4, a.node);
  detail::Put32(mem, h + 8, a.flag);
}

// The slot is hers: the option was applied at load and the slot's character is 16. A slot above 3 reads nothing.
template <class Mem>
bool HerSlot(const Mem& mem, bool enabled, uint32_t slot) {
  if (!enabled || slot >= kSlots) return false;
  return mem(kSlotTable + kSlotTableStride * slot + kSlotCharacter) == kAlphaCharacter;
}
// ... and its file is not loaded (unloaded, or a load under way).
template <class Mem>
bool HerSlotEmpty(const Mem& mem, bool enabled, uint32_t slot) {
  return HerSlot(mem, enabled, slot) && detail::Be32(mem, kSlotFiles + 4 * slot) == 0;
}

// Releases the slot's aura as sub_82378918's release branch does (recomp.5.cpp:27684-27736): the node's owner word 0, the
// life 0 when the flag is 1 or the flag and the life are both -1, the handle's object word 0. `ok(address, size)` says
// whether guest memory there can be read and written; the hook checks the object (vtable and life) and the node's owner
// word before writing either.
template <class Mem, class Ok>
Release ReleaseAura(Mem& mem, uint32_t slot, const Ok& ok) {
  const AuraHandle a = ReadAura(mem, slot);
  if (a.object == 0) return Release::kNothing;
  if (!ok(a.object, 8) || !ok(a.node + 4, 4)) return Release::kStandDown;
  if (detail::Be32(mem, a.object) != kAuraVtable) return Release::kStandDown;
  detail::Put32(mem, a.node + 4, 0);
  if (a.flag == 1 || (a.flag == 0xFFFFFFFFu && detail::Be32(mem, a.object + 4) == 0xFFFFFFFFu)) {
    detail::Put32(mem, a.object + 4, 0);
  }
  detail::Put32(mem, kAuras + kAuraHandleBytes * slot, 0);
  return Release::kReleased;
}

// The stand-in her handle holds during the aura pass: an object word that is not 0 (the pass builds an aura only into a
// handle whose object word is 0, recomp.5.cpp:27436-27448), and, should the pass release it after all (no shown player on
// the slot), a node and a flag that make its release write 0 into the handle's object word and nowhere else: the node is
// the handle minus 4 (its owner word is the handle's object word) and the flag 0 leaves the life alone.
inline AuraHandle Placeholder(uint32_t slot) {
  const uint32_t h = kAuras + kAuraHandleBytes * slot;
  return {h, h - 4, 0};
}

// Before the game's aura pass: for each slot that is hers and empty, release her aura if there is one, then put the
// stand-in in the handle. A release that stands down leaves the slot as it is (the game's own behaviour).
template <class Mem, class Ok>
AuraHold BeforeAuraPass(Mem& mem, bool enabled, const Ok& ok) {
  AuraHold hold;
  for (uint32_t s = 0; s < kSlots; ++s) {
    hold.released[s] = Release::kNothing;
    if (!HerSlotEmpty(mem, enabled, s)) continue;
    hold.released_aura[s] = ReadAura(mem, s);
    hold.released[s] = ReleaseAura(mem, s, ok);
    if (hold.released[s] == Release::kStandDown) continue;
    hold.saved[s] = ReadAura(mem, s);
    WriteAura(mem, s, Placeholder(s));
    hold.held[s] = true;
  }
  return hold;
}
// After it: every held handle back as it was before the stand-in.
template <class Mem>
void AfterAuraPass(Mem& mem, const AuraHold& hold) {
  for (uint32_t s = 0; s < kSlots; ++s) {
    if (hold.held[s]) WriteAura(mem, s, hold.saved[s]);
  }
}

// The log's bookkeeping for one slot across aura passes (the hook writes the lines; Tally decides them). A hold is a run of
// passes in a row in which the slot held the stand-in; a stand-down run is a run of passes in a row in which her slot was
// empty but its aura could not be released (ReleaseAura kStandDown: nothing written, nothing held, the slot left as the
// game has it, so the protection did not act there).
struct SlotTally {
  uint32_t held_passes = 0;        // passes in the hold under way
  uint32_t stand_down_passes = 0;  // passes in the stand-down run under way
  uint64_t stand_downs = 0;        // every stand-down on the slot so far in this process
};
struct TallyLines {
  bool hold_started = false;        // this pass began a hold
  uint32_t hold_ended = 0;          // a hold of this many passes ended at this pass (0: none)
  bool stand_down_started = false;  // this pass began a stand-down run
  uint32_t stand_down_ended = 0;    // a stand-down run of this many passes ended at this pass (0: none)
};
inline TallyLines Tally(SlotTally& t, bool held, Release released) {
  TallyLines out;
  if (held) {
    out.hold_started = t.held_passes++ == 0;
  } else if (t.held_passes != 0) {
    out.hold_ended = t.held_passes;
    t.held_passes = 0;
  }
  if (released == Release::kStandDown) {
    ++t.stand_downs;
    out.stand_down_started = t.stand_down_passes++ == 0;
  } else if (t.stand_down_passes != 0) {
    out.stand_down_ended = t.stand_down_passes;
    t.stand_down_passes = 0;
  }
  return out;
}

// Whether the released aura's node is still in the effect manager's active list as the released one (owner 0, the same
// object): kUnreadable when a node of the list cannot be read or the list is longer than `limit`.
template <class Mem, class Ok>
Found StillListed(const Mem& mem, const Ok& ok, uint32_t node, uint32_t object, uint32_t limit) {
  uint32_t p = detail::Be32(mem, kEffects + 4);
  for (uint32_t i = 0; p != 0; ++i) {
    if (i >= limit || !ok(p, 12)) return Found::kUnreadable;
    if (p == node && detail::Be32(mem, p + 4) == 0 && detail::Be32(mem, p + 8) == object) return Found::kYes;
    p = detail::Be32(mem, p);
  }
  return Found::kNo;
}

// Set once at load by the app: whether the option's bytes are in guest memory (the running state, not the saved one).
void Enable(bool on);

}  // namespace doa4::alpha_select
