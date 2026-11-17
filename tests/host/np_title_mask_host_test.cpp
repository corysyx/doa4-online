// Host test for src/net/np_title_mask.h (the netplay title mask: the joiner's pad is sent as nothing pressed while the
// game is on the title screen or any screen before it). Built and run by run_np_title_mask_test.bat;
// "np_title_mask_host_test.exe -v" prints each group before it runs.
//
// The sprite ids are typed here from their sources, not taken from the module's constants: the title logo 0x30000
// (doa4_intro_skip.h kTitleLogoSprite), Mode Select's item captions 0x40002, 0x40003, 0x40004, 0x40005, 0x40006, 0x40009,
// 0x4000a, 0x4000c, 0x4000d, 0x4000e, 0x4000f, 0x40011, 0x40013 (doa4_online_item_block.h). The "other" draws are ids the
// title, the attract and the screens between draw (PRESS START 0x30001, 0x30002, 0x30003, the press-confirm sprite 0xf7)
// and the caption ids' neighbours that are not captions.

#include "../../src/net/np_title_mask.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using doa4::np::Pad;  // (not the whole namespace: np_core.h's Button names clash with the test's own)
namespace mask = doa4::np::title_mask;
using mask::Edge;

static int g_checks = 0;
static int g_failures = 0;
static bool g_verbose = false;
static const char* g_group = "";
#define CHECK(...)                                                                        \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(__VA_ARGS__)) {                                                                 \
      std::printf("FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, g_group, #__VA_ARGS__);    \
      std::fflush(stdout);                                                                \
      ++g_failures;                                                                       \
    }                                                                                     \
  } while (0)

static void Group(const char* name) {
  g_group = name;
  if (g_verbose) std::printf("group %s\n", name);
}

constexpr uint32_t kLogo = 0x30000;
constexpr uint32_t kCaptions[] = {0x40002, 0x40003, 0x40004, 0x40005, 0x40006, 0x40009, 0x4000a,
                                  0x4000c, 0x4000d, 0x4000e, 0x4000f, 0x40011, 0x40013};
constexpr uint32_t kStory = 0x40002;
constexpr uint32_t kOthers[] = {0x30001, 0x30002, 0x30003, 0xf7, 0x40000, 0x40001, 0x40007, 0x40008,
                                0x4000b, 0x40010, 0x40012, 0x40014, 0x50002, 0x0};
constexpr uint16_t kStart = 0x0010, kA = 0x1000, kB = 0x2000, kUp = 0x0001;

static Pad Buttons(uint16_t b) {
  Pad p;
  p.buttons = b;
  return p;
}

static void TestMaskDecision() {
  Group("mask_joiner_while_armed");
  CHECK(mask::MaskLocalInput(true, true, true));
  CHECK(!mask::MaskLocalInput(true, true, false));  // the joiner in a session, not armed: its input goes through
  Group("never_mask_host");
  CHECK(!mask::MaskLocalInput(false, true, true));
  CHECK(!mask::MaskLocalInput(false, true, false));
  Group("never_mask_offline");
  CHECK(!mask::MaskLocalInput(true, false, true));
  CHECK(!mask::MaskLocalInput(false, false, true));
  CHECK(!mask::MaskLocalInput(true, false, false));
  CHECK(!mask::MaskLocalInput(false, false, false));
}

static void TestArmedAtSessionStart() {
  Group("armed_at_session_start");
  mask::TitleMask m;
  CHECK(!m.Armed());  // before the session starts
  CHECK(m.OnSessionStart() == Edge::kArmedSessionStart);
  CHECK(m.Armed());
  CHECK(m.OnSessionStart() == Edge::kNone);  // armed already: no second edge
  CHECK(m.Armed());
}

static void TestMenuDisarms() {
  Group("menu_disarms");
  mask::TitleMask m;
  m.OnSessionStart();
  CHECK(m.OnMenuReached() == Edge::kOff);
  CHECK(!m.Armed());
  CHECK(m.OnMenuReached() == Edge::kNone);  // off already: no second edge
  CHECK(!m.Armed());
  // through the draw entry: a frame in which a caption id is drawn visible (and the logo is not) disarms at its end
  for (uint32_t c : kCaptions) {
    mask::TitleMask k;
    k.OnSessionStart();
    k.OnDraw(c, true);
    CHECK(k.Armed());  // a draw alone changes nothing: the frame's end decides
    CHECK(k.EndFrame() == Edge::kOff);
    CHECK(!k.Armed());
    CHECK(k.LastMenuSprite() == c);  // the log line names the caption that disarmed
  }
  // a caption submitted with its draw bit clear does not
  mask::TitleMask h;
  h.OnSessionStart();
  h.OnDraw(kStory, false);
  CHECK(h.EndFrame() == Edge::kNone);
  CHECK(h.Armed());
  // a menu signal before the session starts is no edge (off already)
  mask::TitleMask n;
  n.OnDraw(kStory, true);
  CHECK(n.EndFrame() == Edge::kNone);
  CHECK(!n.Armed());
}

static void TestMenuSignalSet() {
  Group("menu_signal_set");
  for (uint32_t c : kCaptions) {
    CHECK(mask::IsMenuSignal(c, true));
    CHECK(!mask::IsMenuSignal(c, false));
  }
  for (uint32_t o : kOthers) {
    CHECK(!mask::IsMenuSignal(o, true));
    CHECK(!mask::IsMenuSignal(o, false));
  }
  CHECK(!mask::IsMenuSignal(kLogo, true));
}

static void TestTitleRearms() {
  Group("title_rearms_after_menu");
  mask::TitleMask m;
  m.OnSessionStart();
  m.OnMenuReached();
  CHECK(m.OnTitleLogoDrawn() == Edge::kArmedTitleLogo);
  CHECK(m.Armed());
  CHECK(m.OnTitleLogoDrawn() == Edge::kNone);  // the logo every frame: one edge
  CHECK(m.Armed());
  // the draw entry: a frame with the logo drawn visible re-arms, with its draw bit clear it does not
  mask::TitleMask d;
  d.OnSessionStart();
  d.OnDraw(kStory, true);
  CHECK(d.EndFrame() == Edge::kOff);
  d.OnDraw(kLogo, false);
  CHECK(d.EndFrame() == Edge::kNone);
  CHECK(!d.Armed());
  d.OnDraw(kLogo, true);
  CHECK(d.EndFrame() == Edge::kArmedTitleLogo);
  CHECK(d.Armed());
  d.OnDraw(kLogo, true);
  CHECK(d.EndFrame() == Edge::kNone);  // the logo again next frame: no second edge
  // a second menu visit and return: off, armed again
  d.OnDraw(0x40005, true);
  CHECK(d.EndFrame() == Edge::kOff);
  d.OnDraw(kLogo, true);
  CHECK(d.EndFrame() == Edge::kArmedTitleLogo);
  // the logo before the session starts arms too (the title is up)
  mask::TitleMask e;
  e.OnDraw(kLogo, true);
  CHECK(e.EndFrame() == Edge::kArmedTitleLogo);
}

static void TestLogoAndCaptionOneFrame() {
  Group("logo_over_menu_stays_armed");
  // The title -> Mode Select fade (t7s1, frames 15397-15419): Mode Select draws its caption, then the title still draws
  // its logo in the same frame, for about 23 frames. The latch stays armed, with no edge, until the first frame with the
  // caption and no logo.
  mask::TitleMask m;
  m.OnSessionStart();
  int edges = 0;
  for (int f = 0; f < 23; ++f) {
    m.OnDraw(kStory, true);  // the caption first, as the game draws them
    m.OnDraw(0x50000, true);
    m.OnDraw(kLogo, true);
    if (m.EndFrame() != Edge::kNone) ++edges;
  }
  CHECK(edges == 0);
  CHECK(m.Armed());
  // the logo before the caption in a frame: the same
  m.OnDraw(kLogo, true);
  m.OnDraw(kStory, true);
  CHECK(m.EndFrame() == Edge::kNone);
  CHECK(m.Armed());
  m.OnDraw(kStory, true);
  CHECK(m.EndFrame() == Edge::kOff);  // the fade is over
  // off, then a frame with both: the logo wins, armed
  m.OnDraw(kStory, true);
  m.OnDraw(kLogo, true);
  CHECK(m.EndFrame() == Edge::kArmedTitleLogo);
  Group("frame_end_forgets_the_frame");
  // the draws of one frame do not carry into the next
  mask::TitleMask k;
  k.OnSessionStart();
  k.OnDraw(kLogo, true);
  CHECK(k.EndFrame() == Edge::kNone);
  k.OnDraw(kStory, true);
  CHECK(k.EndFrame() == Edge::kOff);  // the logo of the frame before does not hold it
  k.OnDraw(kStory, true);
  CHECK(k.EndFrame() == Edge::kNone);
  CHECK(k.EndFrame() == Edge::kNone);  // a frame with no draw: off stays off
  k.OnDraw(kLogo, true);
  CHECK(k.EndFrame() == Edge::kArmedTitleLogo);
  CHECK(k.EndFrame() == Edge::kNone);  // the caption of two frames ago does not disarm
  CHECK(k.Armed());
}

static void TestAttractStaysArmed() {
  Group("attract_frames_stay_armed");
  // armed, the logo drawn, then 20000 frames of other draws and no logo (the attract, loads, the screens between):
  // still armed, no edge.
  mask::TitleMask m;
  m.OnSessionStart();
  m.OnDraw(kLogo, true);
  m.EndFrame();
  int edges = 0;
  for (int f = 0; f < 20000; ++f) {
    for (uint32_t o : kOthers) {
      m.OnDraw(o, true);
      m.OnDraw(o, false);
    }
    if (m.EndFrame() != Edge::kNone) ++edges;
  }
  for (int f = 0; f < 20000; ++f)
    if (m.EndFrame() != Edge::kNone) ++edges;  // frames with no draw at all (the attract cinematic draws no 2D sprite)
  CHECK(edges == 0);
  CHECK(m.Armed());
  // and off stays off through the same draws (only the logo re-arms)
  m.OnMenuReached();
  for (int f = 0; f < 1000; ++f) {
    for (uint32_t o : kOthers) m.OnDraw(o, true);
    m.EndFrame();
  }
  CHECK(!m.Armed());
}

static void TestHoldBack() {
  Group("held_through_disarm_stays_idle");
  {
    mask::JoinerFilter j;
    // START held from boot through the mask, and on after it goes off
    for (int f = 0; f < 50; ++f) CHECK(j.Apply(true, Buttons(kStart)).sent == Pad{});
    for (int f = 0; f < 20; ++f) CHECK(j.Apply(false, Buttons(kStart)).sent.buttons == 0);
  }
  Group("released_then_pressed_sent");
  {
    mask::JoinerFilter j;
    j.Apply(true, Buttons(kStart));
    CHECK(j.Apply(false, Buttons(kStart)).sent.buttons == 0);
    CHECK(j.Apply(false, Buttons(0)).sent.buttons == 0);           // released once
    CHECK(j.Apply(false, Buttons(kStart)).sent.buttons == kStart);  // a fresh press goes through
    CHECK(j.Apply(false, Buttons(kStart)).sent.buttons == kStart);  // and keeps going through while held
  }
  Group("pressed_after_disarm_sent_at_once");
  {
    mask::JoinerFilter j;
    j.Apply(true, Buttons(0));
    CHECK(j.Apply(false, Buttons(kA)).sent.buttons == kA);
    // a button pressed and released under the mask is not held back
    mask::JoinerFilter k;
    k.Apply(true, Buttons(kA));
    k.Apply(true, Buttons(0));
    CHECK(k.Apply(false, Buttons(kA)).sent.buttons == kA);
    // never masked (the host, offline): everything goes through from the first frame
    mask::JoinerFilter h;
    CHECK(h.Apply(false, Buttons(kStart | kA)).sent.buttons == (kStart | kA));
  }
  Group("other_buttons_not_held_back");
  {
    mask::JoinerFilter j;
    j.Apply(true, Buttons(kStart));
    CHECK(j.Apply(false, Buttons(kStart | kA)).sent.buttons == kA);
    CHECK(j.Apply(false, Buttons(kStart | kUp)).sent.buttons == kUp);
    // releasing START frees START only
    CHECK(j.Apply(false, Buttons(kUp)).sent.buttons == kUp);
    CHECK(j.Apply(false, Buttons(kStart | kUp)).sent.buttons == (kStart | kUp));
    // two held, one released: the other stays held back
    mask::JoinerFilter t;
    t.Apply(true, Buttons(kStart | kB));
    CHECK(t.Apply(false, Buttons(kB)).sent.buttons == 0);
    CHECK(t.Apply(false, Buttons(kB | kStart)).sent.buttons == kStart);
  }
  Group("masked_pad_is_all_idle");
  {
    mask::JoinerFilter j;
    Pad p;
    p.buttons = kStart;
    p.lt = 200;
    p.rt = 10;
    p.lx = 3000;
    p.ly = -3000;
    p.rx = 1;
    p.ry = -1;
    CHECK(j.Apply(true, p).sent == Pad{});
    // after the disarm the sticks and triggers go through at once
    const Pad after = j.Apply(false, p).sent;
    CHECK(after.lt == 200 && after.rt == 10 && after.lx == 3000 && after.ly == -3000 && after.rx == 1 && after.ry == -1);
    CHECK(after.buttons == 0);
  }
}

static void TestReplacedCounts() {
  Group("replaced_first_and_count");
  mask::JoinerFilter j;
  CHECK(!j.Apply(true, Buttons(0)).first_replaced);  // an idle pad under the mask is not "replaced"
  CHECK(j.Apply(true, Buttons(kStart)).first_replaced);
  CHECK(!j.Apply(true, Buttons(kStart)).first_replaced);  // one first per armed period
  CHECK(!j.Apply(true, Buttons(0)).first_replaced);
  Pad stick;
  stick.lx = 9000;
  CHECK(!j.Apply(true, stick).first_replaced);  // counted, not first
  CHECK(!j.Apply(false, Buttons(kA)).first_replaced);  // not masked: never "replaced"
  CHECK(j.TakeReplacedCount() == 3);  // START, START, the stick
  CHECK(j.TakeReplacedCount() == 0);  // taken: the next period starts from zero
  // the next armed period has its own first
  CHECK(j.Apply(true, Buttons(kB)).first_replaced);
  CHECK(j.TakeReplacedCount() == 1);
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "-v") == 0) g_verbose = true;
  TestMaskDecision();
  TestArmedAtSessionStart();
  TestMenuDisarms();
  TestMenuSignalSet();
  TestTitleRearms();
  TestLogoAndCaptionOneFrame();
  TestAttractStaysArmed();
  TestHoldBack();
  TestReplacedCounts();
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
