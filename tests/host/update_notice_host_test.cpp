// Host test for src/doa4_update_notice (update G7, Task 15): the start-up decision (AtStart), the answer of a check
// (AfterCheck) and of the Check for Updates button (RefreshFromButton), the display string, the session-relaunch predicate,
// the memory file (through its own reader ReadMemory, on a BOM, CRLF and spaces, and its failures), the arrivals mailbox and
// the PRESS START watch. Built and run by run_update_notice_test.bat. Files go under %TEMP%\doa4_update_notice_<pid>\ and only
// that folder is removed at the end.
// Expected versions follow np::rules::CompareVersions (numbers, not text): 0.3.10 is newer than 0.3.9, which a text
// comparison gets wrong, so those pairs are in every rule that compares.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "../../src/doa4_update_notice.h"

namespace fs = std::filesystem;
namespace un = doa4::update_notice;
using doa4::updater::Found;

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)
#define CHECK_EQ(a, b)                                                                                    \
  do {                                                                                                    \
    ++g_checks;                                                                                           \
    const std::string va_ = (a), vb_ = (b);                                                               \
    if (va_ != vb_) {                                                                                     \
      std::printf("FAIL %s:%d: %s\n   got:  [%s]\n   want: [%s]\n", __FILE__, __LINE__, #a, va_.c_str(), \
                  vb_.c_str());                                                                           \
      ++g_failures;                                                                                       \
    }                                                                                                     \
  } while (0)

static bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// ---- the display string ----
static void TestDisplay() {
  CHECK_EQ(un::DisplayText("alpha-v0.3.2"), "Update available: 0.3.2");  // the user's exact text
  CHECK_EQ(un::VersionText("alpha-v0.3.2"), "0.3.2");                     // NOT "v0.3.2" (cut at the first '-')
  CHECK_EQ(un::VersionText("netplay-v1.0.10"), "1.0.10");                  // NOT the last five characters
  CHECK_EQ(un::VersionText("alpha-v10.20.30"), "10.20.30");
  CHECK_EQ(un::VersionText("v1.2.3"), "1.2.3");
  CHECK_EQ(un::VersionText("1.2.3"), "1.2.3");
  CHECK_EQ(un::VersionText("dev"), "dev");  // not a version: as it is
}

// ---- the session-relaunch predicate ----
static void TestSessionRelaunch() {
  CHECK(!un::SessionRelaunch(nullptr, nullptr, nullptr, false));
  CHECK(!un::SessionRelaunch("", "", "", false));  // set to nothing = not set
  CHECK(un::SessionRelaunch("C:\\u\\netplay\\handoff.d4h", nullptr, nullptr, false));  // into a session
  CHECK(un::SessionRelaunch(nullptr, "1", nullptr, false));                         // back to offline after one
  CHECK(un::SessionRelaunch(nullptr, nullptr, nullptr, true));                      // a netplay session runs
  // R106: DOA4_WAIT_PID alone (the menu's Restart, the swimsuit import's restart, after an update) is a normal start
  CHECK(!un::SessionRelaunch(nullptr, nullptr, "4242", false));
  CHECK(un::SessionRelaunch("C:\\u\\netplay\\handoff.d4h", nullptr, "4242", false));  // a session relaunch carries both
  const un::StartPlan p = un::AtStart(true, "alpha-v0.3.1", false, un::SessionRelaunch(nullptr, nullptr, "4242", false), "");
  CHECK(p.check);  // a WAIT_PID-only start checks
}

// ---- the start-up decision ----
static void TestAtStart() {
  {  // toggle off: no check, nothing shown, the memory kept (NOT shown from memory)
    const un::StartPlan p = un::AtStart(false, "alpha-v0.3.1", false, false, "alpha-v0.3.2");
    CHECK(!p.check && p.show.empty() && !p.clear_memory);
    CHECK(Has(p.why, "doa4_update_notice"));
  }
  {  // a session relaunch: no check, nothing shown, the memory kept
    const un::StartPlan p = un::AtStart(true, "alpha-v0.3.1", false, true, "alpha-v0.3.2");
    CHECK(!p.check && p.show.empty() && !p.clear_memory);
    CHECK(Has(p.why, "session"));
  }
  {  // a self-built game: no check and nothing shown, although "dev" sorts below every remembered version
    const un::StartPlan p = un::AtStart(true, "dev", true, false, "alpha-v0.3.2");
    CHECK(!p.check && p.show.empty() && !p.clear_memory);
    CHECK(Has(p.why, "self-built"));
  }
  {  // remembered newer: shown, no check
    const un::StartPlan p = un::AtStart(true, "alpha-v0.3.1", false, false, "alpha-v0.3.2");
    CHECK(!p.check && !p.clear_memory);
    CHECK_EQ(p.show, "alpha-v0.3.2");
    CHECK(Has(p.why, "memory"));
    const un::StartPlan q = un::AtStart(true, "alpha-v0.3.9", false, false, "alpha-v0.3.10");  // numbers, not text
    CHECK(!q.check && !q.clear_memory);
    CHECK_EQ(q.show, "alpha-v0.3.10");
  }
  {  // remembered equal: cleared, this start checks, nothing shown yet
    const un::StartPlan p = un::AtStart(true, "alpha-v0.3.2", false, false, "alpha-v0.3.2");
    CHECK(p.check && p.clear_memory && p.show.empty());
  }
  {  // remembered older: cleared, checks (0.3.9 is older than 0.3.10, which text order gets backwards)
    const un::StartPlan p = un::AtStart(true, "alpha-v0.3.3", false, false, "alpha-v0.3.2");
    CHECK(p.check && p.clear_memory && p.show.empty());
    const un::StartPlan q = un::AtStart(true, "alpha-v0.3.10", false, false, "alpha-v0.3.9");
    CHECK(q.check && q.clear_memory && q.show.empty());
  }
  {  // nothing remembered: checks, nothing to clear
    const un::StartPlan p = un::AtStart(true, "alpha-v0.3.1", false, false, "");
    CHECK(p.check && !p.clear_memory && p.show.empty());
  }
  {  // a remembered text that is not a version: cleared, checks, NOT shown
    const un::StartPlan p = un::AtStart(true, "alpha-v0.3.1", false, false, "garbage");
    CHECK(p.check && p.clear_memory && p.show.empty());
  }
}

// ---- a check's answer ----
static void TestAfterCheck() {
  {  // newer, nothing remembered: shown and remembered
    const un::MemoryPlan p = un::AfterCheck(Found::kNewer, "alpha-v0.3.2", "");
    CHECK_EQ(p.show, "alpha-v0.3.2");
    CHECK(p.write);
    CHECK_EQ(p.memory, "alpha-v0.3.2");
  }
  {  // newer, the same tag remembered: shown, no rewrite
    const un::MemoryPlan p = un::AfterCheck(Found::kNewer, "alpha-v0.3.2", "alpha-v0.3.2");
    CHECK_EQ(p.show, "alpha-v0.3.2");
    CHECK(!p.write);
  }
  {  // newer than the remembered one: the memory moves on
    const un::MemoryPlan p = un::AfterCheck(Found::kNewer, "alpha-v0.3.3", "alpha-v0.3.2");
    CHECK_EQ(p.show, "alpha-v0.3.3");
    CHECK(p.write);
    CHECK_EQ(p.memory, "alpha-v0.3.3");
  }
  {  // up to date: nothing shown, the memory cleared
    const un::MemoryPlan p = un::AfterCheck(Found::kUpToDate, "alpha-v0.3.2", "alpha-v0.3.2");
    CHECK(p.show.empty() && p.write && p.memory.empty());
    const un::MemoryPlan q = un::AfterCheck(Found::kUpToDate, "alpha-v0.3.2", "");
    CHECK(q.show.empty() && !q.write);  // nothing to clear
  }
  {  // failure: nothing shown, the memory unchanged (NOT cleared)
    const un::MemoryPlan p = un::AfterCheck(Found::kFailed, "", "alpha-v0.3.2");
    CHECK(p.show.empty() && !p.write);
    CHECK(Has(p.why, "unchanged"));
  }
  {  // the button: the same memory change, never shown
    const un::MemoryPlan n = un::RefreshFromButton(Found::kNewer, "alpha-v0.3.3", "alpha-v0.3.2");
    CHECK(n.show.empty() && n.write);
    CHECK_EQ(n.memory, "alpha-v0.3.3");
    const un::MemoryPlan u = un::RefreshFromButton(Found::kUpToDate, "alpha-v0.3.2", "alpha-v0.3.2");
    CHECK(u.show.empty() && u.write && u.memory.empty());
    const un::MemoryPlan f = un::RefreshFromButton(Found::kFailed, "", "alpha-v0.3.2");
    CHECK(f.show.empty() && !f.write);
  }
}

// ---- the memory file ----
static fs::path g_root;
// A file's bytes, with every byte outside printable ASCII written as <hex> (a failed check prints ASCII only: the host
// runner's console encoding cannot print a BOM).
static std::string Bytes(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  const std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::string out;
  for (unsigned char c : raw) {
    if (c >= 0x20 && c < 0x7F) {
      out += char(c);
    } else {
      char b[8];
      std::snprintf(b, sizeof b, "<%02X>", c);
      out += b;
    }
  }
  return out;
}
static void WriteBytes(const fs::path& p, const std::string& b) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(b.data(), std::streamsize(b.size()));
}
static bool Exists(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p, ec);
}

static void TestMemoryFile() {
  const fs::path d = g_root / "mem";
  fs::create_directories(d);
  const fs::path file = d / un::kMemoryFile, tmp = d / (std::string(un::kMemoryFile) + ".tmp");
  CHECK_EQ(un::ReadMemory(d), "");  // no file
  CHECK_EQ(un::WriteMemory(d, "alpha-v0.3.2"), "");
  CHECK_EQ(Bytes(file), "alpha-v0.3.2<0A>");
  CHECK_EQ(un::ReadMemory(d), "alpha-v0.3.2");
  CHECK(!Exists(tmp));
  CHECK_EQ(un::WriteMemory(d, "alpha-v0.3.10"), "");  // replaces
  CHECK_EQ(un::ReadMemory(d), "alpha-v0.3.10");
  CHECK_EQ(un::WriteMemory(d, ""), "");  // cleared = no file
  CHECK(!Exists(file));
  CHECK_EQ(un::ReadMemory(d), "");
  CHECK_EQ(un::WriteMemory(d, ""), "");  // clearing nothing is fine
  // a hand-edited file: BOM, spaces, CRLF, a second line
  WriteBytes(file, "\xEF\xBB\xBF alpha-v0.3.2 \r\nsecond\r\n");
  CHECK_EQ(un::ReadMemory(d), "alpha-v0.3.2");
  // no folder: an error, nothing created
  const fs::path missing = g_root / "no_such_folder";
  CHECK(!un::WriteMemory(missing, "alpha-v0.3.2").empty());
  CHECK(!Exists(missing));
  // the temp file cannot be created (a folder is in its place): an error, the old memory unchanged
  CHECK_EQ(un::WriteMemory(d, "alpha-v0.3.2"), "");
  fs::create_directories(tmp);
  CHECK(!un::WriteMemory(d, "alpha-v0.3.5").empty());
  CHECK_EQ(Bytes(file), "alpha-v0.3.2<0A>");
  fs::remove(tmp);
  // the rename cannot replace (the memory is held open without delete sharing): an error, the old bytes kept, no .tmp
  {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    const std::string err = un::WriteMemory(d, "alpha-v0.3.6");
    CHECK(!err.empty());
    const std::string err_clear = un::WriteMemory(d, "");
    CHECK(!err_clear.empty());  // a delete that fails is said too
    CloseHandle(h);
  }
  CHECK_EQ(Bytes(file), "alpha-v0.3.2<0A>");
  CHECK(!Exists(tmp));
}

// ---- arrivals ----
static void TestArrivals() {
  CHECK(un::TakeArrivals().empty());
  std::thread t([] {
    un::Post({false, Found::kNewer, "alpha-v0.3.2"});
    un::Post({true, Found::kUpToDate, "alpha-v0.3.2"});
  });
  t.join();
  const std::vector<un::Arrival> a = un::TakeArrivals();
  CHECK(a.size() == 2);
  if (a.size() == 2) {
    CHECK(!a[0].from_button && a[0].found == Found::kNewer);
    CHECK_EQ(a[0].tag, "alpha-v0.3.2");
    CHECK(a[1].from_button && a[1].found == Found::kUpToDate);
  }
  CHECK(un::TakeArrivals().empty());  // taken once
}

// ---- the PRESS START watch ----
static void TestTitleWatch() {
  using E = un::TitleWatch::Event;
  constexpr uint16_t kStart = 0x0010, kA = 0x1000, kB = 0x2000;
  constexpr uint32_t kLogo = 0x30000, kShadow = 0x30002;
  // A stand-in for the next screen's own sprite (MODE SELECT's id is named from run evidence in the report): any sprite
  // never submitted while the title was on screen with no press pending.
  constexpr uint32_t kMenu = 0x50001;
  // One title frame in the game's order: the logo first, then PRESS START and its shadow (the logo of the very first title
  // frame comes before PRESS START was ever drawn, so it must still count as a title sprite from the next frame on).
  const auto frame = [&](un::TitleWatch& w, uint32_t s) {
    w.NoteDraw(kLogo, true, s);
    w.NoteDraw(un::kPressStartSprite, true, s);
    w.NoteDraw(kShadow, true, s);
  };
  const auto title = [&](un::TitleWatch& w, uint32_t from, uint32_t to) {
    for (uint32_t s = from; s <= to; ++s) frame(w, s);
  };
  {  // before the title (the boot movie: nothing submitted yet): neither drawn nor ended; a press then is not on the title
    un::TitleWatch w;
    CHECK(!w.DrawnNow(100) && !w.Ended() && !w.Pending(100));
    CHECK(!w.NotePad(0, kStart, 100));
    CHECK(w.PressSwap() == 0);
  }
  {  // the shadow, the logo and a PRESS START submission with the draw bit clear: not "drawn"; the census reports new ids
    un::TitleWatch w;
    CHECK(w.NoteDraw(un::kPressStartSprite, false, 5) == E::kNone);
    CHECK(w.NoteDraw(kLogo, true, 5) == E::kNewSprite);
    CHECK(w.NoteDraw(kLogo, true, 6) == E::kNone);
    CHECK(!w.DrawnNow(6));
    CHECK(w.HiddenCount() == 1 && w.VisibleCount() == 0);
  }
  {  // drawn at a gap of exactly kGapSwaps, not one swap later; a gap alone never ends it
    un::TitleWatch w;
    CHECK(w.NoteDraw(un::kPressStartSprite, true, 10) == E::kFirstVisible);
    CHECK(w.LastVisibleSwap() == 11);
    CHECK(w.DrawnNow(10) && w.DrawnNow(40));
    CHECK(!w.DrawnNow(41) && !w.Ended());
  }
  {  // R103: the attract (no press): hidden in the gap, shown again with the title, never ended
    un::TitleWatch w;
    title(w, 21, 1845);
    CHECK(w.DrawnNow(1845));
    CHECK(!w.DrawnNow(2000) && !w.Ended());
    CHECK(!w.NotePad(0, kStart, 2400));  // START deep in the attract: not a press on the title
    CHECK(!w.NotePad(0, 0, 2410));
    title(w, 2640, 2700);
    CHECK(w.DrawnNow(2700) && !w.Ended());
  }
  {  // R105, the confirm path (run 6b: START at 2946, PRESS START last drawn at 3005, MODE SELECT on screen at 3012):
     // hidden while the press is pending (M2), ended by the next screen's own sprite, and never shown again
    un::TitleWatch w;
    title(w, 2640, 2946);
    CHECK(w.DrawnNow(2946));
    CHECK(w.NotePad(0, kStart, 2946));
    CHECK(w.PressSwap() == 2947);
    CHECK(!w.NotePad(0, kStart, 2947));                      // held: not a new press
    CHECK(w.Pending(2947) && !w.DrawnNow(2947));             // M2: hidden at once
    title(w, 2947, 3005);                                    // the title's own sprites while it fades: no confirmation
    CHECK(!w.Ended() && w.Pending(3005));
    CHECK(w.NoteDraw(kMenu, true, 3006) == E::kConfirmed);   // the next screen
    CHECK(w.Ended() && w.ConfirmSprite() == kMenu);
    CHECK(!w.DrawnNow(3006) && !w.Pending(3006));
    title(w, 4000, 4100);                                    // back at the title (B from the menu)
    CHECK(!w.DrawnNow(4100) && w.Ended());
  }
  {  // review window (a): a press in the attract's first 30 swaps never ends it
    un::TitleWatch w;
    title(w, 21, 1845);
    CHECK(!w.NotePad(0, kStart, 1855));  // 10 swaps after PRESS START's last draw: not a press on the title
    CHECK(!w.NotePad(0, 0, 1856));
    CHECK(w.NotePad(1, kStart, 1846));   // 1 swap after: counted as pending ...
    CHECK(w.Pending(1846) && !w.DrawnNow(1846));
    CHECK(w.Pending(1845 + un::kCancelGapSwaps) && !w.DrawnNow(1845 + un::kCancelGapSwaps));
    CHECK(!w.Pending(1846 + un::kCancelGapSwaps));  // ... and cancelled once PRESS START has been gone kCancelGapSwaps
    CHECK(!w.DrawnNow(1846 + un::kCancelGapSwaps));  // hidden in the attract (the app asks this every tick)
    CHECK(w.NoteDraw(kMenu, true, 1850 + un::kCancelGapSwaps) == E::kNewSprite);  // a later new sprite: no confirmation
    CHECK(!w.Ended());
    title(w, 2640, 2700);
    CHECK(w.DrawnNow(2700) && !w.Ended());
  }
  {  // fix round 1 run r9 (ou15c): START 2 swaps after PRESS START's last draw as the attract began WAS accepted (MODE
     // SELECT's 0xf7 came 37 swaps after that last draw, 35 after the press): confirmed, NOT cancelled by the gap
    un::TitleWatch w;
    title(w, 21, 1845);
    CHECK(w.NotePad(0, kStart, 1847));
    CHECK(w.Pending(1879) && !w.DrawnNow(1879));  // ou15c's 30-swap gap cancelled it here
    CHECK(w.NoteDraw(0xf7, true, 1882) == E::kConfirmed);
    CHECK(w.Ended() && w.ConfirmSprite() == 0xf7);
  }
  {  // review window (b): a press the title ignores, the attract starting 145 swaps later: never ends it
    un::TitleWatch w;
    title(w, 21, 1700);
    CHECK(w.NotePad(0, kA, 1700));
    CHECK(w.Pending(1700) && !w.DrawnNow(1700));  // hidden while pending
    title(w, 1701, 1845);                          // the title goes on: no new sprite
    CHECK(!w.Pending(1846 + un::kCancelGapSwaps) && !w.Ended());  // the attract's start cancels it
    CHECK(!w.DrawnNow(1846 + un::kCancelGapSwaps));
    CHECK(w.NoteDraw(kMenu, true, 1850 + un::kCancelGapSwaps) == E::kNewSprite);
    CHECK(!w.Ended());
    title(w, 2640, 2700);
    CHECK(w.DrawnNow(2700));
  }
  {  // a cancelled press stays cancelled: PRESS START back within kConfirmSwaps of the press does not revive it
    un::TitleWatch w;
    title(w, 10, 100);
    CHECK(w.NotePad(0, kStart, 100));
    CHECK(!w.Pending(101 + un::kCancelGapSwaps));  // gone longer than kCancelGapSwaps: cancelled
    title(w, 200, 210);
    CHECK(!w.Pending(210) && w.DrawnNow(210));
    CHECK(w.NoteDraw(kMenu, true, 210) == E::kNewSprite && !w.Ended());
  }
  {  // an ignored press with the title still on: shown again once kConfirmSwaps passed with no confirmation
    un::TitleWatch w;
    title(w, 10, 100 + un::kConfirmSwaps + 1);
    CHECK(w.NotePad(0, kA, 100));
    CHECK(w.Pending(100 + un::kConfirmSwaps) && !w.DrawnNow(100 + un::kConfirmSwaps));
    CHECK(!w.Pending(101 + un::kConfirmSwaps) && w.DrawnNow(101 + un::kConfirmSwaps));
    CHECK(w.NoteDraw(kMenu, true, 101 + un::kConfirmSwaps) == E::kNewSprite);  // too late: no confirmation
    CHECK(!w.Ended());
  }
  {  // A counts (confirmed by the next screen); B never does; another user's pad counts; the edge is per user
    un::TitleWatch a;
    title(a, 10, 100);
    CHECK(a.NotePad(1, kA, 100));
    CHECK(a.NoteDraw(kMenu, true, 120) == E::kConfirmed && a.Ended());
    un::TitleWatch b;
    title(b, 10, 100);
    CHECK(!b.NotePad(0, kB, 100));
    CHECK(!b.Pending(100) && b.PressSwap() == 0);
    un::TitleWatch u;
    title(u, 10, 100);
    CHECK(u.NotePad(1, kStart, 50));
    CHECK(u.NotePad(0, kStart, 60));  // user 1 still holding does not hide user 0's new press
    CHECK(u.PressSwap() == 61);
    CHECK(!u.NotePad(4, kStart, 70));  // no fifth user
  }
  {  // the draw bit's changes are reported; a hidden stretch shorter than the gap is still "drawn"
    un::TitleWatch w;
    CHECK(w.NoteDraw(un::kPressStartSprite, true, 10) == E::kFirstVisible);
    CHECK(w.NoteDraw(un::kPressStartSprite, true, 11) == E::kNone);
    CHECK(w.NoteDraw(un::kPressStartSprite, false, 12) == E::kHidden);
    CHECK(w.NoteDraw(un::kPressStartSprite, false, 13) == E::kNone);
    CHECK(w.DrawnNow(40));
    CHECK(w.NoteDraw(un::kPressStartSprite, true, 41) == E::kVisibleAgain);
    CHECK(w.DrawnNow(71));
    CHECK(w.VisibleCount() == 3 && w.HiddenCount() == 2);
  }
  {  // swap 0 counts; a reader with an older swap count than the last draw sees it drawn (no wrap-around)
    un::TitleWatch w;
    w.NoteDraw(un::kPressStartSprite, true, 0);
    CHECK(w.DrawnNow(0) && w.DrawnNow(30) && !w.DrawnNow(31));
    un::TitleWatch v;
    v.NoteDraw(un::kPressStartSprite, true, 1000);
    CHECK(v.DrawnNow(990));
    CHECK(v.NotePad(0, kStart, 990));  // a pad read stamped before the draw's swap: still on the title
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  wchar_t tmp[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  g_root = fs::path(tmp) / ("doa4_update_notice_" + std::to_string(GetCurrentProcessId()));
  std::error_code ec;
  fs::remove_all(g_root, ec);
  fs::create_directories(g_root, ec);
  TestDisplay();
  TestSessionRelaunch();
  TestAtStart();
  TestAfterCheck();
  TestMemoryFile();
  TestArrivals();
  TestTitleWatch();
  fs::remove_all(g_root, ec);
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
