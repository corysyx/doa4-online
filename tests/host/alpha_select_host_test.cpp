// Host test for the decisions and the arithmetic of Alpha-152's select model (src/doa4_alpha_select.h). Built and run by
// run_alpha_select_test.bat.
//
// The contract:
// - the hook acts only on the select callback's own flush of a view (one return address), for views 0 and 1, when that
//   view's player is visible and is character 16, and only when the option was applied at load; anything else is not its
//   business and is passed over without a word (an ordinary character never produces a warning);
// - with all of that true it still stands down, with a reason, when the effect pass's gate is already set (a stage is
//   being drawn), the frame's opening viewport was never seen, the view's viewport is not a panel inside the screen, the
//   device cannot be read or the guest stack pointer is out of range;
// - in an online session whose swimsuits are mixed it stands down on her panel before any of those, as a ruling (by_rule),
//   and still passes over everything that is not her panel;
// - a view's players, the gate, the viewport and the device are read from the addresses the game's own code uses;
// - the panel fold maps the panel's clip square onto the panel's rectangle of the whole screen (y up in clip units, down
//   in pixels), applied to a matrix as (matrix) x (fold) in the game's row-vector layout;
// - the glow projection is (camera projection) x (zoom), with the zoom's scale given and its offset taken from the
//   matrix the game built; in a match (camera = the game's centred projection) that is the matrix the game built;
// - the scissor rectangle is the panel's left, top, right, bottom;
// - her aura (the hover crash): a slot is hers when the option was applied at load and the slot's character is 16, and
//   hers-and-empty when its file word is 0 as well; her aura is released the way the game's own aura pass releases one
//   (owner let go, life 0, handle's object word 0), and stands down without writing when its object or node cannot be read
//   or the object is not an aura; the unload site releases her slot's aura (r3's low byte); around the game's aura pass a
//   hers-and-empty slot is released first and its handle holds a stand-in during the pass, so the pass builds nothing
//   there, and is put back after; the stand-in, released by the game after all, writes the handle's first word only; the
//   draw-time check finds a released aura node in the effect list only while it is still the released one.
//
// The numbers are the test build's (ou_runs/alpha/runs/w_p5d/game.log, 2026-11-12): player 1's panel 83 93 539 338 of
// 1280x720, its fold k = 0.42109 0.46944, t = -0.44922 0.27222, the panel camera's projection and view-projection as
// logged there, the scissor corners 83 93 and 622 431.

#include "../../src/doa4_alpha_select.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::fflush(stdout);                                        \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

namespace as = doa4::alpha_select;

// Guest memory as a map: an address never written reads 0xEE (so a read from a wrong offset shows), and reads are counted.
struct Mem {
  std::map<uint32_t, uint8_t> bytes;
  mutable int reads = 0;
  uint8_t operator()(uint32_t address) const {
    ++reads;
    const auto it = bytes.find(address);
    return it == bytes.end() ? uint8_t(0xEE) : it->second;
  }
  void Put8(uint32_t a, uint8_t v) { bytes[a] = v; }
  void Put32(uint32_t a, uint32_t v) {
    for (uint32_t i = 0; i < 4; ++i) Put8(a + i, uint8_t(v >> (24 - 8 * i)));
  }
};

// Literals from the game's code, not from the header under test:
//   sub_8252B020, the select draw callback (generated/default/dead_or_alive_4_recomp.19.cpp:49510): the renderer object
//   0x83579C28, its viewport words at +4 (x, y, width, height), its device at +60, and 0x8252B15C, the return address of
//   its one sub_82783BA8 call (0x8252B164 is the next call's);
//   sub_8245D598 (recomp.12.cpp:50321): a player's record 0x83033B40 + player * 156, visible byte +32, character byte +1,
//   and the 16 it compares with; sub_8245D718 (recomp.12.cpp:50537): the gate, renderer + 1312;
//   sub_824F4878 (recomp.17.cpp:71772): 0x824F4958, the return address of its sub_824F4E90 call;
//   sub_8245ACF0 (recomp.12.cpp:44241): 0x8245AE18, the return address of its sub_8231A3D8 call, made with
//   r8 = 0x831A61CC and r9 = 0x82C4D0C4.
constexpr uint32_t kRenderer = 0x83579C28, kPlayer0 = 0x83033B40, kPlayerBytes = 156;
constexpr uint32_t kFlushReturn = 0x8252B15C, kNextCallReturn = 0x8252B164, kFrameOpenReturn = 0x824F4958;
constexpr uint32_t kGlowReturn = 0x8245AE18, kBoxOffset = 0x831A61CC, kBoxScale = 0x82C4D0C4;

// The select screen with player 1 on Alpha-152 and player 2 on Kasumi, both shown; view 0's panel is the viewport.
static Mem SelectScreen() {
  Mem m;
  for (uint32_t p = 0; p < 4; ++p) {
    const uint32_t rec = kPlayer0 + p * kPlayerBytes;
    m.Put8(rec + 0, 0x77);   // the neighbours of the two bytes read: a read one byte off shows
    m.Put8(rec + 2, 0x78);
    m.Put8(rec + 31, 0x79);
    m.Put8(rec + 33, 0x7A);
  }
  m.Put8(kPlayer0 + 1, 16);
  m.Put8(kPlayer0 + 32, 1);
  m.Put8(kPlayer0 + kPlayerBytes + 1, 5);
  m.Put8(kPlayer0 + kPlayerBytes + 32, 2);  // any non-zero value is "visible"
  m.Put8(kPlayer0 + 2 * kPlayerBytes + 1, 16);  // a third and fourth fighter (Tag): never read
  m.Put8(kPlayer0 + 2 * kPlayerBytes + 32, 1);
  m.Put32(kRenderer + 0, 0x11111111);
  m.Put32(kRenderer + 4, 83);
  m.Put32(kRenderer + 8, 93);
  m.Put32(kRenderer + 12, 539);
  m.Put32(kRenderer + 16, 338);
  m.Put32(kRenderer + 20, 0);           // min z 0.0f
  m.Put32(kRenderer + 24, 0x3F800000);  // max z 1.0f
  m.Put32(kRenderer + 56, 0x22222222);
  m.Put32(kRenderer + 60, 0x40006880);
  m.Put32(kRenderer + 64, 0x33333333);
  m.Put32(kRenderer + 1308, 0x82F52800);
  m.Put32(kRenderer + 1312, 0);
  m.Put32(kRenderer + 1316, 7);
  return m;
}

// A call the hook acts on: everything a view of player 1 on Alpha-152 shows in the test build's runs.
static as::Call Good() {
  as::Call c;
  c.enabled = true;
  c.return_address = kFlushReturn;
  c.view = 0;
  c.visible = 1;
  c.character = 16;
  c.gate = 0;
  c.panel = {83, 93, 539, 338};
  c.screen = {1280, 720};
  c.device = 0x40006880;
  c.device_ok = true;
  c.sp = 0x7004F000;
  return c;
}

static bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

// (a) x (b) for 4x4 matrices stored row by row, as the game stores them (a vector is a row: clip = v x M).
static void Multiply(const float a[16], const float b[16], float out[16]) {
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) sum += double(a[4 * r + k]) * double(b[4 * k + c]);
      out[4 * r + c] = float(sum);
    }
  }
}

static bool AllNear(const float a[16], const float b[16], float tolerance) {
  bool ok = true;
  for (int i = 0; i < 16; ++i) ok = ok && Near(a[i], b[i], tolerance);
  return ok;
}

// The panel camera's projection and view-projection of the test build's first acting frame (w_p5d, "P5 full #0").
static const float kPanelProjection[16] = {3.1739f, 0, 0, 0, 0, 5.6425f, 0, 0, -0.44922f, 0.16855f, 1, 1, 0, 0, -0.3f, 0};
static const float kPanelViewProjection[16] = {2.0824f,  0.36053f, -0.83863f, -0.83863f, 0.15203f, 5.5912f,  0.14882f,  0.14882f,
                                               -2.4323f, 0.68927f, -0.52399f, -0.52398f, 1.0265f,  -13.293f, 0.046035f, 0.34603f};

static void TestReadCall() {
  CHECK(as::kGate == kRenderer + 1312 && as::kGate == 0x8357A148);
  CHECK(as::kFlushReturn == kFlushReturn);
  {
    const Mem m = SelectScreen();
    const as::Call c = as::ReadCall(m, 0);
    CHECK(c.view == 0);
    CHECK(c.visible == 1);
    CHECK(c.character == 16);
    CHECK(c.gate == 0);
    CHECK(c.panel.x == 83 && c.panel.y == 93 && c.panel.w == 539 && c.panel.h == 338);
    CHECK(c.device == 0x40006880);
    // what the reader never fills: the hook's own facts
    CHECK(!c.enabled && c.return_address == 0 && c.screen.w == 0 && c.screen.h == 0 && !c.device_ok && c.sp == 0);
  }
  {  // view 1 is player 2's record: Kasumi, NOT player 1's 16
    const Mem m = SelectScreen();
    const as::Call c = as::ReadCall(m, 1);
    CHECK(c.view == 1);
    CHECK(c.character == 5);
    CHECK(c.visible == 2);
    CHECK(c.panel.w == 539);  // the viewport is the renderer's, whichever view
  }
  {  // the gate and a viewport left of the screen: whole words, signed
    Mem m = SelectScreen();
    m.Put32(kRenderer + 1312, 1);
    m.Put32(kRenderer + 4, 0xFFFFFFF0u);
    const as::Call c = as::ReadCall(m, 0);
    CHECK(c.gate == 1);
    CHECK(c.panel.x == -16);
    m.Put32(kRenderer + 1312, 0x01000000);  // a gate whose low byte is 0 is still set
    CHECK(as::ReadCall(m, 0).gate == 0x01000000);
  }
  for (const uint32_t view : {2u, 3u, 4u, 0xFFFFFFFFu}) {  // not a panel this hook draws: nothing is read
    const Mem m = SelectScreen();
    const as::Call c = as::ReadCall(m, view);
    CHECK(c.view == view);
    CHECK(m.reads == 0);
    CHECK(c.visible == 0 && c.character == 0);
  }
  {  // the precondition of the loop above: a view that IS read does read
    const Mem m = SelectScreen();
    as::ReadCall(m, 1);
    CHECK(m.reads > 0);
  }
}

static void TestDecide() {
  using V = as::Verdict;
  CHECK(as::Decide(Good()).verdict == V::kAct);
  CHECK(std::string(as::Decide(Good()).why).empty());
  {  // player 2's panel
    as::Call c = Good();
    c.view = 1;
    CHECK(as::Decide(c).verdict == V::kAct);
  }
  // ---- not this hook's business: passed over, with no reason to report ----
  const auto not_ours = [](const as::Call& c) {
    const as::Decision d = as::Decide(c);
    return d.verdict == V::kNotOurs && std::string(d.why).empty();
  };
  {
    as::Call c = Good();
    c.enabled = false;
    CHECK(not_ours(c));
  }
  for (const uint32_t ra : {kNextCallReturn, kFlushReturn - 4, kFlushReturn + 4, 0x8252B020u, 0u}) {
    as::Call c = Good();
    c.return_address = ra;
    CHECK(not_ours(c));
  }
  for (const uint32_t view : {2u, 3u, 4u, 0xFFFFFFFFu}) {
    as::Call c = Good();
    c.view = view;
    CHECK(not_ours(c));
  }
  {
    as::Call c = Good();
    c.visible = 0;
    CHECK(not_ours(c));
  }
  for (const uint8_t character : {uint8_t(0), uint8_t(5), uint8_t(15), uint8_t(17), uint8_t(0x17), uint8_t(0xFF)}) {
    as::Call c = Good();
    c.character = character;
    CHECK(not_ours(c));
  }
  {  // an ordinary character is passed over even where Alpha-152 would be refused: never a warning for Kasumi
    as::Call c = Good();
    c.character = 5;
    c.gate = 1;
    c.screen = {0, 0};
    c.device_ok = false;
    c.sp = 0;
    CHECK(not_ours(c));
    c.character = 16;
    c.visible = 0;
    CHECK(not_ours(c));
    c.visible = 1;
    c.view = 2;
    CHECK(not_ours(c));
    c.view = 0;
    c.return_address = kNextCallReturn;
    CHECK(not_ours(c));
    c.return_address = kFlushReturn;
    c.enabled = false;
    CHECK(not_ours(c));
    c.enabled = true;
    CHECK(as::Decide(c).verdict == V::kStandDown);  // the precondition of this block: with all five true it IS refused
  }
  // ---- hers, but a precondition fails: it stands down and says why ----
  std::set<std::string> reasons;
  const auto stands_down = [&reasons](const as::Call& c) {
    const as::Decision d = as::Decide(c);
    if (d.verdict != V::kStandDown || std::string(d.why).empty()) return false;
    if (d.by_rule) return false;  // a failed precondition is a fault to warn about, never the mixed-session ruling
    reasons.insert(d.why);
    return true;
  };
  for (const uint32_t gate : {1u, 2u, 0x01000000u, 0xFFFFFFFFu}) {
    as::Call c = Good();
    c.gate = gate;
    CHECK(stands_down(c));
  }
  CHECK(reasons.size() == 1);
  for (const as::Size screen : {as::Size{0, 0}, as::Size{0, 720}, as::Size{1280, 0}, as::Size{-1280, 720}}) {
    as::Call c = Good();
    c.screen = screen;
    CHECK(stands_down(c));
  }
  CHECK(reasons.size() == 2);
  // the panel must lie inside the screen, edges included
  const as::Rect outside[] = {{-1, 93, 539, 338}, {83, -1, 539, 338}, {83, 93, 0, 338},  {83, 93, 539, 0},
                              {83, 93, -5, 338},  {83, 93, 539, -5},  {742, 93, 539, 338}, {83, 383, 539, 338}};
  for (const as::Rect& panel : outside) {
    as::Call c = Good();
    c.panel = panel;
    CHECK(stands_down(c));
  }
  CHECK(reasons.size() == 3);
  const as::Rect inside[] = {{741, 93, 539, 338}, {83, 382, 539, 338}, {0, 0, 539, 338}, {0, 0, 1280, 719}, {0, 0, 1279, 720},
                             {1, 0, 1279, 720},  {0, 1, 1280, 719}};
  for (const as::Rect& panel : inside) {
    as::Call c = Good();
    c.panel = panel;
    CHECK(as::Decide(c).verdict == V::kAct);
  }
  {  // the whole screen is not a panel
    as::Call c = Good();
    c.panel = {0, 0, 1280, 720};
    CHECK(stands_down(c));
    CHECK(reasons.size() == 4);
    c.screen = {2560, 1440};  // the same rectangle on a larger screen is one
    CHECK(as::Decide(c).verdict == V::kAct);
  }
  {
    as::Call c = Good();
    c.device_ok = false;
    CHECK(stands_down(c));
    CHECK(reasons.size() == 5);
  }
  for (const uint32_t sp : {0u, 0x40001000u, 0x70000FFFu, 0x80000000u, 0xFFFFFFFFu}) {
    as::Call c = Good();
    c.sp = sp;
    CHECK(stands_down(c));
  }
  CHECK(reasons.size() == 6);  // six preconditions, six different reasons
  for (const uint32_t sp : {0x70001000u, 0x7004F000u, 0x7FFFFFFFu}) {
    as::Call c = Good();
    c.sp = sp;
    CHECK(as::Decide(c).verdict == V::kAct);
  }
  // ---- a mixed-swimsuit session (the two players' imports differ): her pass stands down by rule, on both panels, and
  // says so as a ruling; her panel stays empty there. Everything that is not her panel is passed over as before. ----
  CHECK(!Good().mixed_session && !as::Decide(Good()).by_rule);  // the call that acts is not a mixed session's
  const std::string mixed_reason = "the session's swimsuits are mixed (her pass stands down there; the panel stays empty)";
  for (const uint32_t view : {0u, 1u}) {
    as::Call c = Good();
    c.view = view;
    c.mixed_session = true;
    const as::Decision d = as::Decide(c);
    CHECK(d.verdict == V::kStandDown);
    CHECK(d.by_rule);
    CHECK(std::string(d.why) == mixed_reason);
  }
  CHECK(reasons.count(mixed_reason) == 0);  // a seventh reason, none of the six faults
  {  // the ruling comes before every precondition: with all of them failing too, the line is still the ruling's
    as::Call c = Good();
    c.mixed_session = true;
    c.gate = 1;
    c.screen = {0, 0};
    c.panel = {0, 0, 0, 0};
    c.device_ok = false;
    c.sp = 0;
    const as::Decision d = as::Decide(c);
    CHECK(d.verdict == V::kStandDown && d.by_rule && std::string(d.why) == mixed_reason);
    c.mixed_session = false;  // the same call outside a mixed session: a fault, with a fault's reason
    const as::Decision fault = as::Decide(c);
    CHECK(fault.verdict == V::kStandDown && !fault.by_rule && reasons.count(fault.why) == 1);
  }
  {  // in a mixed session too, what is not her panel is passed over without a word
    as::Call c = Good();
    c.mixed_session = true;
    c.character = 5;
    CHECK(not_ours(c));
    c.character = 16;
    c.visible = 0;
    CHECK(not_ours(c));
    c.visible = 1;
    c.view = 2;
    CHECK(not_ours(c));
    c.view = 0;
    c.return_address = kNextCallReturn;
    CHECK(not_ours(c));
    c.return_address = kFlushReturn;
    c.enabled = false;
    CHECK(not_ours(c));
    CHECK(!as::Decide(c).by_rule);
  }
}

static void TestOpeningScreen() {
  {
    const std::optional<as::Size> s = as::OpeningScreen(kFrameOpenReturn, 0, 0, 1280, 720);
    CHECK(s.has_value() && s->w == 1280 && s->h == 720);
  }
  CHECK(as::kFrameOpenReturn == kFrameOpenReturn);
  CHECK(!as::OpeningScreen(kFrameOpenReturn + 4, 0, 0, 1280, 720).has_value());  // another caller of the same function
  CHECK(!as::OpeningScreen(0, 0, 0, 1280, 720).has_value());
  CHECK(!as::OpeningScreen(kFrameOpenReturn, 1, 0, 1280, 720).has_value());  // a viewport that does not start at the corner
  CHECK(!as::OpeningScreen(kFrameOpenReturn, 0, 1, 1280, 720).has_value());
  CHECK(!as::OpeningScreen(kFrameOpenReturn, -1, 0, 1280, 720).has_value());
  // sizes: 320..8192 by 240..8192
  CHECK(!as::OpeningScreen(kFrameOpenReturn, 0, 0, 319, 720).has_value());
  CHECK(as::OpeningScreen(kFrameOpenReturn, 0, 0, 320, 720).has_value());
  CHECK(as::OpeningScreen(kFrameOpenReturn, 0, 0, 8192, 720).has_value());
  CHECK(!as::OpeningScreen(kFrameOpenReturn, 0, 0, 8193, 720).has_value());
  CHECK(!as::OpeningScreen(kFrameOpenReturn, 0, 0, 1280, 239).has_value());
  CHECK(as::OpeningScreen(kFrameOpenReturn, 0, 0, 1280, 240).has_value());
  CHECK(as::OpeningScreen(kFrameOpenReturn, 0, 0, 1280, 8192).has_value());
  CHECK(!as::OpeningScreen(kFrameOpenReturn, 0, 0, 1280, 8193).has_value());
  CHECK(!as::OpeningScreen(kFrameOpenReturn, 0, 0, -1280, 720).has_value());
  {
    const std::optional<as::Size> s = as::OpeningScreen(kFrameOpenReturn, 0, 0, 640, 480);
    CHECK(s.has_value() && s->w == 640 && s->h == 480);  // width and height are not swapped
  }
}

// Where a point of the panel's clip square (x right, y up, both -1..1) lands on the screen, in pixels (y down).
static void PanelPixel(const as::Rect& p, float cx, float cy, float* px, float* py) {
  *px = float(p.x) + (cx + 1.0f) * 0.5f * float(p.w);
  *py = float(p.y) + (1.0f - cy) * 0.5f * float(p.h);
}
static void ScreenPixel(const as::Size& s, float cx, float cy, float* px, float* py) {
  *px = (cx + 1.0f) * 0.5f * float(s.w);
  *py = (1.0f - cy) * 0.5f * float(s.h);
}

static void TestPanelFold() {
  struct Case {
    as::Rect panel;
    as::Size screen;
  };
  const Case cases[] = {
      {{83, 93, 539, 338}, {1280, 720}},   // player 1's panel on hover
      {{83, 93, 261, 338}, {1280, 720}},   // the narrow panel
      {{658, 93, 539, 338}, {1280, 720}},  // a panel on the right half
      {{166, 500, 1078, 676}, {2560, 1440}},
      {{0, 0, 1279, 719}, {1280, 720}},    // almost the whole screen
  };
  for (const Case& k : cases) {
    const as::Fold f = as::PanelFold(k.panel, k.screen);
    // the fixture's own precondition: the panel is centred in neither direction, so a mirrored fold lands elsewhere
    CHECK(2 * k.panel.x + k.panel.w != k.screen.w);
    CHECK(2 * k.panel.y + k.panel.h != k.screen.h);
    for (const float cx : {-1.0f, 0.0f, 1.0f, 0.37f}) {
      for (const float cy : {-1.0f, 0.0f, 1.0f, -0.61f}) {
        float want_x, want_y, got_x, got_y, mirrored_x, mirrored_y;
        PanelPixel(k.panel, cx, cy, &want_x, &want_y);
        ScreenPixel(k.screen, f.kx * cx + f.tx, f.ky * cy + f.ty, &got_x, &got_y);
        CHECK(Near(got_x, want_x, 0.01f) && Near(got_y, want_y, 0.01f));
        ScreenPixel(k.screen, f.kx * cx + f.tx, f.ky * cy - f.ty, &mirrored_x, &mirrored_y);
        CHECK(!Near(mirrored_y, want_y, 0.4f));  // NOT the fold with its y term mirrored
      }
    }
  }
  {  // the test build's logged fold for player 1's panel, and for the narrow one
    const as::Fold f = as::PanelFold({83, 93, 539, 338}, {1280, 720});
    CHECK(Near(f.kx, 0.42109f, 6e-6f) && Near(f.ky, 0.46944f, 6e-6f) && Near(f.tx, -0.44922f, 6e-6f) && Near(f.ty, 0.27222f, 6e-6f));
    const as::Fold g = as::PanelFold({83, 93, 261, 338}, {1280, 720});
    CHECK(Near(g.kx, 0.20391f, 6e-6f) && Near(g.tx, -0.66641f, 6e-6f) && Near(g.ty, 0.27222f, 6e-6f));
  }
  {  // the whole screen folds onto itself
    const as::Fold f = as::PanelFold({0, 0, 1280, 720}, {1280, 720});
    CHECK(f.kx == 1.0f && f.ky == 1.0f && f.tx == 0.0f && f.ty == 0.0f);
  }
}

static void TestFoldMatrix() {
  const as::Fold f = as::PanelFold({83, 93, 539, 338}, {1280, 720});
  // (matrix) x (fold): the fold as a matrix in the same layout
  const float fold[16] = {f.kx, 0, 0, 0, 0, f.ky, 0, 0, 0, 0, 1, 0, f.tx, f.ty, 0, 1};
  for (const float* matrix : {kPanelProjection, kPanelViewProjection}) {
    float got[16], want[16];
    std::memcpy(got, matrix, sizeof(got));
    as::FoldMatrix(got, f);
    Multiply(matrix, fold, want);
    CHECK(AllNear(got, want, 2e-6f));
    for (int r = 0; r < 4; ++r) {  // depth and w are the camera's own, bit for bit
      CHECK(std::memcmp(&got[4 * r + 2], &matrix[4 * r + 2], 2 * sizeof(float)) == 0);
    }
    CHECK(!AllNear(got, matrix, 0.01f));  // the fixture can tell a fold from none: it changes the matrix
  }
  {  // the view-projection's last row has a w of its own (0.34603), so that row moves too: NOT a fold of rows 0-2 only,
     // and NOT one that mixes in row 3 (the transposed layout)
    float got[16];
    std::memcpy(got, kPanelViewProjection, sizeof(got));
    as::FoldMatrix(got, f);
    CHECK(Near(got[12], f.kx * 1.0265f + f.tx * 0.34603f, 2e-6f));
    CHECK(Near(got[13], f.ky * -13.293f + f.ty * 0.34603f, 2e-5f));
    CHECK(!Near(got[12], 1.0265f, 0.05f));
    CHECK(Near(got[0], f.kx * 2.0824f + f.tx * -0.83863f, 2e-6f));
    CHECK(!Near(got[0], f.kx * 2.0824f + f.tx * 1.0265f, 0.05f));  // NOT row 3's x as the w
  }
  {  // a point through the folded camera lands where the panel puts it
    const float point[4] = {0.3f, 1.2f, 4.0f, 1.0f};
    float clip[4] = {}, folded_clip[4] = {}, m[16];
    std::memcpy(m, kPanelViewProjection, sizeof(m));
    as::FoldMatrix(m, f);
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) {
        clip[c] += point[r] * kPanelViewProjection[4 * r + c];
        folded_clip[c] += point[r] * m[4 * r + c];
      }
    }
    CHECK(clip[3] != 0.0f && Near(folded_clip[3], clip[3], 1e-6f));
    float want_x, want_y, got_x, got_y;
    PanelPixel({83, 93, 539, 338}, clip[0] / clip[3], clip[1] / clip[3], &want_x, &want_y);
    ScreenPixel({1280, 720}, folded_clip[0] / folded_clip[3], folded_clip[1] / folded_clip[3], &got_x, &got_y);
    CHECK(Near(got_x, want_x, 0.02f) && Near(got_y, want_y, 0.02f));
  }
}

static void TestZoomProjection() {
  // The game's glow projection: its own centred projection (fov and the video mode's aspect) x the zoom box.
  const float sx = 1.7f, sy = 2.3f, bx = 0.31f, by = -0.42f;
  const float centred[16] = {1.3365f, 0, 0, 0, 0, 2.6488f, 0, 0, 0, 0, 1, 1, 0, 0, -0.25f, 0};
  const float zoom[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 1, 0, bx, by, 0, 1};
  float built[16];
  Multiply(centred, zoom, built);
  CHECK(built[8] == bx && built[9] == by);  // the fixture's precondition: the built matrix carries the offset in row 2
  {  // in a panel: the camera's own projection, zoomed
    float got[16], want[16];
    as::ZoomProjection(built, kPanelProjection, sx, sy, got);
    Multiply(kPanelProjection, zoom, want);
    CHECK(AllNear(got, want, 2e-6f));
    CHECK(!AllNear(got, built, 0.05f));             // NOT the matrix the game built
    CHECK(!AllNear(got, kPanelProjection, 0.05f));  // NOT the camera projection unzoomed
    CHECK(Near(got[0], sx * 3.1739f, 2e-6f) && Near(got[5], sy * 5.6425f, 2e-6f));  // each axis its own scale
    CHECK(Near(got[8], sx * -0.44922f + bx, 2e-6f) && Near(got[9], sy * 0.16855f + by, 2e-6f));
    CHECK(got[14] == -0.3f && got[10] == 1.0f && got[11] == 1.0f && got[15] == 0.0f);  // depth and w: the camera's (-0.3), not the built one's (-0.25)
  }
  {  // in a match the camera's projection is the centred one, and the replacement is the matrix the game built
    float got[16];
    as::ZoomProjection(built, centred, sx, sy, got);
    CHECK(AllNear(got, built, 1e-6f));
  }
  {  // a camera matrix whose w column is not the built matrix's: every row of the result takes the CAMERA's w, and its x
     // and y take the offset times that w. (The two projections above share the w column 0 0 1 0 with the built matrix, so
     // they cannot tell whose w is used; the panel's view-projection, a full matrix from the same log, can.)
    const float* const camera = kPanelViewProjection;
    CHECK(built[3] == 0.0f && built[7] == 0.0f && built[11] == 1.0f && built[15] == 0.0f);  // the built matrix's w column
    for (int row = 0; row < 4; ++row) CHECK(std::fabs(camera[4 * row + 3] - built[4 * row + 3]) > 0.1f);  // the precondition
    float got[16], want[16];
    as::ZoomProjection(built, camera, sx, sy, got);
    Multiply(camera, zoom, want);
    CHECK(AllNear(got, want, 2e-6f));
    CHECK(got[3] == -0.83863f && got[7] == 0.14882f && got[11] == -0.52398f && got[15] == 0.34603f);  // w: the camera's, row by row
    CHECK(got[2] == -0.83863f && got[6] == 0.14882f && got[10] == -0.52399f && got[14] == 0.046035f);  // depth: the camera's
    CHECK(Near(got[12], sx * 1.0265f + bx * 0.34603f, 2e-6f) && Near(got[13], sy * -13.293f + by * 0.34603f, 2e-6f));
    CHECK(Near(got[0], sx * 2.0824f + bx * -0.83863f, 2e-6f) && Near(got[1], sy * 0.36053f + by * -0.83863f, 2e-6f));
  }
}

static void TestScissorAndGlowCall() {
  const std::array<uint32_t, 4> s = as::ScissorOf({83, 93, 539, 338});
  CHECK(s[0] == 83 && s[1] == 93 && s[2] == 622 && s[3] == 431);  // left, top, right, bottom (the test build's "83 93 and 622 431")
  const std::array<uint32_t, 4> n = as::ScissorOf({83, 93, 261, 338});
  CHECK(n[0] == 83 && n[1] == 93 && n[2] == 344 && n[3] == 431);

  CHECK(as::IsGlowProjectionCall(true, kGlowReturn, kBoxOffset, kBoxScale));
  CHECK(!as::IsGlowProjectionCall(false, kGlowReturn, kBoxOffset, kBoxScale));     // outside her select pass: a match is untouched
  CHECK(!as::IsGlowProjectionCall(true, kGlowReturn + 4, kBoxOffset, kBoxScale));  // another caller
  CHECK(!as::IsGlowProjectionCall(true, 0x8231D7A0, kBoxOffset, kBoxScale));
  CHECK(!as::IsGlowProjectionCall(true, kGlowReturn, kBoxScale, kBoxOffset));      // the two pointers swapped
  CHECK(!as::IsGlowProjectionCall(true, kGlowReturn, kBoxOffset + 4, kBoxScale));
  CHECK(!as::IsGlowProjectionCall(true, kGlowReturn, kBoxOffset, kBoxScale + 4));
  CHECK(!as::IsGlowProjectionCall(true, kGlowReturn, 0, 0));
}

// ======== Her aura while her preview slot is empty (the hover crash) ========
//
// Literals from the game's code, not from the header under test (generated/default, recompN = dead_or_alive_4_recomp.N.cpp):
//   sub_82378918 (recomp5.cpp:27273), the aura pass: the slot table 0x8302BB34 + 8 * slot, character at +2 (r26 + 2,
//   rlwinm r6 = slot << 3, :27410-27418), compared with 16 (:27420); the aura handles 0x83303BBC + 12 * slot (r17, :27329,
//   r4 = 3 * slot, << 2, :27428-27434), each {object, node, flag}; a new one gets flag -1 (r19, :27546) and the node's +4 the
//   handle's address (:27550); the release branch (:27684-27736): node+4 = 0, life (object+4) = 0 when the flag is 1, or
//   the flag is -1 and the life is -1, then the handle's object word = 0;
//   the slot's file word 0x830B5340 + 4 * slot (sub_823FEC28, recomp9.cpp: r27 = -31989 << 16 + 21312, :54467, rlwinm
//   r28 = slot << 2, :54471); the slot loader tick sub_823FE150 sets it to 0 when a load starts (:53185);
//   sub_827349F8 (recomp41.cpp:62567): the aura's vtable 0x822BA024 (:62620-62624), its life -1 (:62626);
//   sub_82733980 (recomp41.cpp:59955): the effect manager 0x8357D748; sub_82733A90 / sub_82733BC8: its active list at +4,
//   a node's next at +0, its owner at +4, its object at +8.
constexpr uint32_t kSlotTable = 0x8302BB34, kSlotFiles = 0x830B5340, kAuras = 0x83303BBC, kAuraVtable = 0x822BA024;
constexpr uint32_t kEffects = 0x8357D748;
constexpr uint32_t kObjects[4] = {0x401D61FC, 0x401E0010, 0x401E1020, 0x401E2030};  // an aura object per slot
constexpr uint32_t kNodes[4] = {0x401D61F0, 0x401E0004, 0x401E1014, 0x401E2024};    // its node (object - 12)

static uint32_t Be32(const Mem& m, uint32_t a) {
  return (uint32_t(m(a)) << 24) | (uint32_t(m(a + 1)) << 16) | (uint32_t(m(a + 2)) << 8) | uint32_t(m(a + 3));
}
// Every address the guest reads here is readable in the fixture, except those a test takes away.
struct Readable {
  std::set<uint32_t> bad;
  bool operator()(uint32_t address, uint32_t size) const {
    for (uint32_t a : bad) {
      if (a >= address && a < address + size) return false;
    }
    return true;
  }
};

// The select screen just after player 1 left her cell: slot 0 (player 1) is character 16 with its file word 0 (the
// slot is reloading) and her live aura in its handle; slot 1 (player 2) is Kasumi, loaded, no aura; slots 2 and 3 unused.
// The neighbours of every byte read hold values that would pass the test (16, a non-zero file) if read by mistake.
static Mem AuraScreen() {
  Mem m;
  for (uint32_t s = 0; s < 4; ++s) {
    const uint32_t row = kSlotTable + 8 * s;
    for (uint32_t i = 0; i < 8; ++i) m.Put8(row + i, 16);  // the row's other bytes: 16 everywhere but the character
    m.Put32(kSlotFiles + 4 * s, 0x40AA0000u + s);
    m.Put32(kAuras + 12 * s, 0);
    m.Put32(kAuras + 12 * s + 4, 0);
    m.Put32(kAuras + 12 * s + 8, 0);
  }
  m.Put8(kSlotTable + 0 * 8 + 2, 16);
  m.Put8(kSlotTable + 1 * 8 + 2, 5);
  m.Put8(kSlotTable + 2 * 8 + 2, 7);
  m.Put8(kSlotTable + 3 * 8 + 2, 9);
  m.Put32(kSlotFiles + 0, 0);
  // her aura on slot 0, as sub_82378918 made it
  m.Put32(kAuras + 0, kObjects[0]);
  m.Put32(kAuras + 4, kNodes[0]);
  m.Put32(kAuras + 8, 0xFFFFFFFFu);
  m.Put32(kObjects[0], kAuraVtable);
  m.Put32(kObjects[0] + 4, 0xFFFFFFFFu);
  m.Put32(kObjects[0] + 8, 0);  // its slot
  m.Put32(kNodes[0], 0);        // next
  m.Put32(kNodes[0] + 4, kAuras + 0);
  m.Put32(kNodes[0] + 8, kObjects[0]);
  return m;
}

// A model of what sub_82378918 does to one slot's aura handle, from the listing above; `local` is its per-slot verdict
// (0: no shown player on the slot, 1: a shown player who is not character 16, 2: a shown player who is). It builds a new
// aura (object `fresh`) into an empty handle of a slot whose verdict is 2, and releases the handle of a slot whose verdict
// is 0 or 1. Returns whether it built one. This is the fake of the game the pass's hold is tested against; the harness
// runs check it against the game itself.
static bool GameAuraPass(Mem& m, uint32_t slot, int local, uint32_t fresh) {
  const uint32_t h = kAuras + 12 * slot;
  const uint32_t object = Be32(m, h);
  if (local == 2) {
    if (object != 0) return false;
    const uint32_t node = fresh - 12;
    m.Put32(fresh, kAuraVtable);
    m.Put32(fresh + 4, 0xFFFFFFFFu);
    m.Put32(h, fresh);
    m.Put32(h + 4, node);
    m.Put32(h + 8, 0xFFFFFFFFu);
    m.Put32(node + 4, h);
    return true;
  }
  if (object != 0) {
    const uint32_t node = Be32(m, h + 4), flag = Be32(m, h + 8);
    m.Put32(node + 4, 0);
    if (flag == 1 || (flag == 0xFFFFFFFFu && Be32(m, object + 4) == 0xFFFFFFFFu)) m.Put32(object + 4, 0);
    m.Put32(h, 0);
  }
  return false;
}

static void TestAuraSlots() {
  {  // the addresses are the game's
    CHECK(as::kSlotTable == kSlotTable && as::kSlotFiles == kSlotFiles && as::kAuras == kAuras);
    CHECK(as::kAuraVtable == kAuraVtable && as::kEffects == kEffects && as::kSlots == 4);
  }
  {  // slot 0: hers and empty; the option decides
    const Mem m = AuraScreen();
    CHECK(as::HerSlot(m, true, 0));
    CHECK(as::HerSlotEmpty(m, true, 0));
    CHECK(!as::HerSlot(m, false, 0));       // option not applied at load: never
    CHECK(!as::HerSlotEmpty(m, false, 0));
  }
  {  // the character decides: only 16, read at the slot's own +2
    for (const uint8_t character : {uint8_t(0), uint8_t(5), uint8_t(15), uint8_t(17), uint8_t(0x90), uint8_t(0xFF)}) {
      Mem m = AuraScreen();
      m.Put8(kSlotTable + 2, character);
      CHECK(!as::HerSlot(m, true, 0));
      CHECK(!as::HerSlotEmpty(m, true, 0));
    }
  }
  {  // the slot decides: slots 1-3 are not hers although every byte around their characters is 16
    const Mem m = AuraScreen();
    for (uint32_t s = 1; s < 4; ++s) {
      CHECK(!as::HerSlot(m, true, s));
      CHECK(!as::HerSlotEmpty(m, true, s));
    }
    Mem n = AuraScreen();  // slot 2 hers instead of slot 0: only slot 2 answers
    n.Put8(kSlotTable + 2, 5);
    n.Put8(kSlotTable + 2 * 8 + 2, 16);
    n.Put32(kSlotFiles + 2 * 4, 0);
    n.Put32(kSlotFiles + 0, 0x40AA0000u);
    CHECK(!as::HerSlot(n, true, 0) && as::HerSlot(n, true, 2) && as::HerSlotEmpty(n, true, 2) && !as::HerSlotEmpty(n, true, 0));
  }
  for (const uint32_t slot : {4u, 5u, 0xFFu, 0xFFFFFFFFu}) {  // no such slot: nothing is read
    const Mem m = AuraScreen();
    CHECK(!as::HerSlot(m, true, slot));
    CHECK(!as::HerSlotEmpty(m, true, slot));
    CHECK(m.reads == 0);
  }
  {  // the file word decides "empty": the whole word, not one byte of it
    for (const uint32_t file : {0x00000001u, 0x00000100u, 0x00010000u, 0x01000000u, 0x401D6100u}) {
      Mem m = AuraScreen();
      m.Put32(kSlotFiles + 0, file);
      CHECK(as::HerSlot(m, true, 0));
      CHECK(!as::HerSlotEmpty(m, true, 0));
    }
  }
}

static void TestReleaseAura() {
  using R = as::Release;
  const Readable all;
  {  // her aura as built: owner let go, life 0, handle's object word 0; node and flag words left as the game leaves them
    Mem m = AuraScreen();
    const Mem before = m;
    CHECK(as::ReleaseAura(m, 0, all) == R::kReleased);
    CHECK(Be32(m, kNodes[0] + 4) == 0);
    CHECK(Be32(m, kObjects[0] + 4) == 0);
    CHECK(Be32(m, kAuras + 0) == 0);
    CHECK(Be32(m, kAuras + 4) == kNodes[0] && Be32(m, kAuras + 8) == 0xFFFFFFFFu);
    int changed = 0;  // exactly those three words, 12 bytes at most
    for (const auto& [a, v] : m.bytes) changed += before.bytes.count(a) == 0 || before.bytes.at(a) != v;
    CHECK(changed > 0 && changed <= 12);
    CHECK(Be32(m, kObjects[0]) == kAuraVtable && Be32(m, kNodes[0] + 8) == kObjects[0] && Be32(m, kNodes[0]) == 0);
    CHECK(as::ReleaseAura(m, 0, all) == R::kNothing);  // a second time: nothing left to release
  }
  {  // flag 1: life to 0 whatever it was; flag -1 with a life of its own, or flag 0: life kept
    Mem m = AuraScreen();
    m.Put32(kAuras + 8, 1);
    m.Put32(kObjects[0] + 4, 5);
    CHECK(as::ReleaseAura(m, 0, all) == R::kReleased && Be32(m, kObjects[0] + 4) == 0 && Be32(m, kNodes[0] + 4) == 0);
    Mem n = AuraScreen();
    n.Put32(kObjects[0] + 4, 7);
    CHECK(as::ReleaseAura(n, 0, all) == R::kReleased && Be32(n, kObjects[0] + 4) == 7 && Be32(n, kAuras) == 0);
    Mem k = AuraScreen();
    k.Put32(kAuras + 8, 0);
    CHECK(as::ReleaseAura(k, 0, all) == R::kReleased && Be32(k, kObjects[0] + 4) == 0xFFFFFFFFu && Be32(k, kNodes[0] + 4) == 0);
  }
  {  // another slot's handle: slot 2's, 24 bytes on; slot 0's left alone
    Mem m = AuraScreen();
    m.Put32(kAuras + 24, kObjects[2]);
    m.Put32(kAuras + 28, kNodes[2]);
    m.Put32(kAuras + 32, 0xFFFFFFFFu);
    m.Put32(kObjects[2], kAuraVtable);
    m.Put32(kObjects[2] + 4, 0xFFFFFFFFu);
    m.Put32(kNodes[2] + 4, kAuras + 24);
    CHECK(as::ReleaseAura(m, 2, all) == R::kReleased);
    CHECK(Be32(m, kAuras + 24) == 0 && Be32(m, kNodes[2] + 4) == 0 && Be32(m, kObjects[2] + 4) == 0);
    CHECK(Be32(m, kAuras + 0) == kObjects[0] && Be32(m, kNodes[0] + 4) == kAuras && Be32(m, kObjects[0] + 4) == 0xFFFFFFFFu);
  }
  {  // no aura: nothing, nothing written
    Mem m = AuraScreen();
    m.Put32(kAuras + 0, 0);
    const Mem before = m;
    CHECK(as::ReleaseAura(m, 0, all) == R::kNothing);
    CHECK(m.bytes == before.bytes);
  }
  // stand down, writing nothing: the object or the node cannot be read, or the object is not an aura
  for (const uint32_t bad : {kObjects[0], kObjects[0] + 4, kNodes[0] + 4, kNodes[0] + 7}) {
    Mem m = AuraScreen();
    const Mem before = m;
    Readable r;
    r.bad.insert(bad);
    CHECK(as::ReleaseAura(m, 0, r) == R::kStandDown);
    CHECK(m.bytes == before.bytes);
  }
  {
    Mem m = AuraScreen();
    m.Put32(kObjects[0], 0x822BA048);  // a particle's vtable
    const Mem before = m;
    CHECK(as::ReleaseAura(m, 0, all) == R::kStandDown);
    CHECK(m.bytes == before.bytes);
  }
}

static void TestAuraPassHold() {
  using R = as::Release;
  const Readable all;
  const int locals[4] = {2, 1, 0, 0};  // player 1 shown on slot 0 (character 16), player 2 on slot 1 (Kasumi)
  {  // the crash's moment, option on: released before the pass, no new aura built in it, the handle as released after
    Mem m = AuraScreen();
    const as::AuraHold hold = as::BeforeAuraPass(m, true, all);
    CHECK(hold.released[0] == R::kReleased && hold.held[0]);
    CHECK(!hold.held[1] && !hold.held[2] && !hold.held[3]);
    bool built = false;
    for (uint32_t s = 0; s < 4; ++s) built = GameAuraPass(m, s, locals[s], 0x40500000u + 0x100 * s) || built;
    as::AfterAuraPass(m, hold);
    CHECK(!built);
    CHECK(Be32(m, kAuras) == 0 && Be32(m, kAuras + 4) == kNodes[0] && Be32(m, kAuras + 8) == 0xFFFFFFFFu);
    CHECK(Be32(m, kNodes[0] + 4) == 0 && Be32(m, kObjects[0] + 4) == 0);  // what the effect update reaps
  }
  {  // the same moment, option off: the game builds nothing either (its aura is still there) and releases nothing
    Mem m = AuraScreen();
    const as::AuraHold hold = as::BeforeAuraPass(m, false, all);
    CHECK(!hold.held[0] && hold.released[0] == R::kNothing);
    for (uint32_t s = 0; s < 4; ++s) GameAuraPass(m, s, locals[s], 0x40500000u + 0x100 * s);
    as::AfterAuraPass(m, hold);
    CHECK(Be32(m, kAuras) == kObjects[0] && Be32(m, kNodes[0] + 4) == kAuras && Be32(m, kObjects[0] + 4) == 0xFFFFFFFFu);
  }
  {  // the next frames: her handle already empty, the slot still loading. Option on: nothing built.
    Mem m = AuraScreen();
    m.Put32(kAuras, 0);
    const as::AuraHold hold = as::BeforeAuraPass(m, true, all);
    CHECK(hold.held[0] && hold.released[0] == R::kNothing);
    bool built = false;
    for (uint32_t s = 0; s < 4; ++s) built = GameAuraPass(m, s, locals[s], 0x40500000u + 0x100 * s) || built;
    as::AfterAuraPass(m, hold);
    CHECK(!built);
    CHECK(Be32(m, kAuras) == 0 && Be32(m, kAuras + 4) == kNodes[0] && Be32(m, kAuras + 8) == 0xFFFFFFFFu);
    Mem off = AuraScreen();  // the precondition of this block: without the hold the game builds one on the empty slot
    off.Put32(kAuras, 0);
    CHECK(GameAuraPass(off, 0, 2, 0x40500000u));
  }
  {  // her slot loaded again (her file word set): nothing held, the game builds her aura as it always does
    Mem m = AuraScreen();
    m.Put32(kAuras, 0);
    m.Put32(kSlotFiles, 0x40AB0000u);
    const as::AuraHold hold = as::BeforeAuraPass(m, true, all);
    CHECK(!hold.held[0] && !hold.held[1] && !hold.held[2] && !hold.held[3]);
    bool built = false;
    for (uint32_t s = 0; s < 4; ++s) built = GameAuraPass(m, s, locals[s], 0x40500000u + 0x100 * s) || built;
    as::AfterAuraPass(m, hold);
    CHECK(built && Be32(m, kAuras) == 0x40500000u);
  }
  {  // the stand-in is safe if the game releases it after all (no shown player on the slot): it writes the handle only
    Mem m = AuraScreen();
    m.Put32(kAuras, 0);
    const as::AuraHold hold = as::BeforeAuraPass(m, true, all);
    CHECK(hold.held[0]);
    const as::AuraHandle stand_in = as::Placeholder(0);
    CHECK(Be32(m, kAuras) == stand_in.object && Be32(m, kAuras + 4) == stand_in.node && Be32(m, kAuras + 8) == stand_in.flag);
    CHECK(stand_in.object != 0);  // the game builds nothing into a handle whose object word is not 0
    const Mem during = m;
    GameAuraPass(m, 0, 0, 0x40500000u);
    int changed = 0;
    for (const auto& [a, v] : m.bytes) {
      const bool differs = during.bytes.count(a) == 0 || during.bytes.at(a) != v;
      if (differs) {
        ++changed;
        CHECK(a >= kAuras && a < kAuras + 4);  // only the handle's object word
      }
    }
    CHECK(changed > 0);
    as::AfterAuraPass(m, hold);
    CHECK(Be32(m, kAuras) == 0 && Be32(m, kAuras + 4) == kNodes[0] && Be32(m, kAuras + 8) == 0xFFFFFFFFu);
  }
  {  // player 2 on her too, her file loaded: slot 1 is left to the game; slot 0 held
    Mem m = AuraScreen();
    m.Put8(kSlotTable + 8 + 2, 16);
    const int both[4] = {2, 2, 0, 0};
    const as::AuraHold hold = as::BeforeAuraPass(m, true, all);
    CHECK(hold.held[0] && !hold.held[1]);
    bool built[4] = {};
    for (uint32_t s = 0; s < 4; ++s) built[s] = GameAuraPass(m, s, both[s], 0x40500000u + 0x100 * s);
    as::AfterAuraPass(m, hold);
    CHECK(!built[0] && built[1] && Be32(m, kAuras + 12) == 0x40500100u);
  }
  {  // her aura cannot be read: the release stands down, nothing is held, the handle is left as it was
    Mem m = AuraScreen();
    Readable r;
    r.bad.insert(kObjects[0] + 4);
    const Mem before = m;
    const as::AuraHold hold = as::BeforeAuraPass(m, true, r);
    CHECK(hold.released[0] == R::kStandDown && !hold.held[0]);
    CHECK(m.bytes == before.bytes);
  }
}

// The log's bookkeeping (ruling R4: every stand-down is counted, not only the first said).
static void TestTally() {
  using R = as::Release;
  {  // a hold of three passes: started on the first, said ended (3) on the first pass that does not hold
    as::SlotTally t;
    const as::TallyLines a = as::Tally(t, true, R::kReleased);
    CHECK(a.hold_started && a.hold_ended == 0 && !a.stand_down_started && a.stand_down_ended == 0);
    const as::TallyLines b = as::Tally(t, true, R::kNothing);
    CHECK(!b.hold_started && b.hold_ended == 0);
    const as::TallyLines c = as::Tally(t, true, R::kNothing);
    CHECK(!c.hold_started && c.hold_ended == 0 && t.held_passes == 3);
    const as::TallyLines d = as::Tally(t, false, R::kNothing);
    CHECK(!d.hold_started && d.hold_ended == 3 && t.held_passes == 0);
    const as::TallyLines e = as::Tally(t, false, R::kNothing);  // said once: the next idle pass says nothing
    CHECK(!e.hold_started && e.hold_ended == 0 && !e.stand_down_started && e.stand_down_ended == 0);
    const as::TallyLines f = as::Tally(t, true, R::kNothing);  // a second hold starts again from 1
    const as::TallyLines g = as::Tally(t, false, R::kNothing);
    CHECK(f.hold_started && g.hold_ended == 1);
    CHECK(t.stand_downs == 0);
  }
  {  // stand-downs: every one counted (a run of 4, then a run of 2), each run started and ended once
    as::SlotTally t;
    uint32_t started = 0;
    for (int i = 0; i < 4; ++i) {
      const as::TallyLines l = as::Tally(t, false, R::kStandDown);
      started += l.stand_down_started;
      CHECK(l.stand_down_ended == 0 && !l.hold_started && l.hold_ended == 0);
      CHECK(l.stand_down_started == (i == 0));
    }
    CHECK(started == 1 && t.stand_downs == 4 && t.stand_down_passes == 4);
    const as::TallyLines end1 = as::Tally(t, true, R::kNothing);  // the release worked again: held, the run said ended
    CHECK(end1.stand_down_ended == 4 && end1.hold_started && t.stand_down_passes == 0 && t.stand_downs == 4);
    const as::TallyLines s2 = as::Tally(t, false, R::kStandDown);
    CHECK(s2.stand_down_started && s2.hold_ended == 1);  // the one-pass hold ends at the same pass
    const as::TallyLines s3 = as::Tally(t, false, R::kStandDown);
    CHECK(!s3.stand_down_started && s3.stand_down_ended == 0);
    const as::TallyLines end2 = as::Tally(t, false, R::kNothing);  // her slot no longer empty
    CHECK(end2.stand_down_ended == 2 && !end2.stand_down_started && end2.hold_ended == 0);
    CHECK(t.stand_downs == 6 && t.stand_down_passes == 0 && t.held_passes == 0);
  }
  {  // a release that worked is no stand-down, held or not
    as::SlotTally t;
    as::Tally(t, true, R::kReleased);
    as::Tally(t, false, R::kReleased);
    as::Tally(t, false, R::kNothing);
    CHECK(t.stand_downs == 0 && t.stand_down_passes == 0);
  }
}

static void TestReapedCheck() {
  using F = as::Found;
  const Readable all;
  // the active list: head at manager + 4, three nodes A -> released aura node -> C
  Mem m = AuraScreen();
  const uint32_t a = 0x40600000, c = 0x40600100;
  m.Put32(kEffects + 4, a);
  m.Put32(a, kNodes[0]);
  m.Put32(a + 4, 0x83300000);
  m.Put32(kNodes[0], c);
  m.Put32(kNodes[0] + 4, 0);  // released
  m.Put32(c, 0);
  m.Put32(c + 4, 0);
  CHECK(as::StillListed(m, all, kNodes[0], kObjects[0], 64) == F::kYes);
  CHECK(as::StillListed(m, all, c, kObjects[0], 64) == F::kNo);  // c holds another object
  CHECK(as::StillListed(m, all, 0x40700000, kObjects[0], 64) == F::kNo);
  {  // the same node, owned again (a new aura was built at the freed node's address): not the released one
    Mem n = m;
    n.Put32(kNodes[0] + 4, kAuras + 12);
    CHECK(as::StillListed(n, all, kNodes[0], kObjects[0], 64) == F::kNo);
  }
  {  // an empty list
    Mem n = m;
    n.Put32(kEffects + 4, 0);
    CHECK(as::StillListed(n, all, kNodes[0], kObjects[0], 64) == F::kNo);
  }
  {  // a node that cannot be read, or a list longer than the limit (a loop): unreadable, never "no"
    Readable r;
    r.bad.insert(a + 2);
    CHECK(as::StillListed(m, r, kNodes[0], kObjects[0], 64) == F::kUnreadable);
    Mem n = m;
    n.Put32(c, a);  // c -> a: a loop that does not pass the node looked for
    CHECK(as::StillListed(n, all, 0x40700000, kObjects[0], 64) == F::kUnreadable);
    CHECK(as::StillListed(m, all, kNodes[0], kObjects[0], 1) == F::kUnreadable);  // limit 1: the node is second
    CHECK(as::StillListed(m, all, kNodes[0], kObjects[0], 2) == F::kYes);
  }
}

int main() {
  TestReadCall();
  TestDecide();
  TestOpeningScreen();
  TestPanelFold();
  TestFoldMatrix();
  TestZoomProjection();
  TestScissorAndGlowCall();
  TestAuraSlots();
  TestReleaseAura();
  TestAuraPassHold();
  TestTally();
  TestReapedCheck();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
