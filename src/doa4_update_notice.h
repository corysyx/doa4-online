// The start-up update notice (update G7, Task 15): at start, in the background, the release query Check for Updates uses;
// when a newer release of this build's channel exists, the title screen shows exactly "Update available: <a.b.c>" while
// PRESS START is drawn (hidden in the title's attract, back after it), until the Start press on the title (R103: then for
// the rest of the run). A found version is remembered (a small file in
// the user-data folder) so that later starts show it with no request until the running build reaches it.
//
// The rules (controller ruling R51, spec G7):
// - Off (doa4_update_notice = false), a session relaunch (SessionRelaunch below) or a self-built game (np::rules::
//   IsDevBuild: "dev"): no request, nothing shown, the memory untouched.
// - A remembered tag newer than the running one: shown, no request. A remembered tag equal to or older than the running
//   one (or one that is not a version): the memory is cleared and this start checks.
// - The check's answer: newer -> shown and remembered; up to date -> nothing, memory cleared; failure (no answer, an error,
//   no release of the channel) -> nothing, memory unchanged.
// - Check for Updates (the Online tab's button) refreshes the memory the same way (RefreshFromButton).
// - Nothing is written in an online session (the app's guard, SetUpdateMemory; a session relaunch never checks either).
//
// Pure C++ apart from the memory file's Windows rename (host-tested in tests/host/update_notice_host_test.cpp).

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "doa4_updater.h"  // updater::Found

namespace doa4::update_notice {

// The memory: <user data>/update_notice.txt holds one tag and a newline ("alpha-v0.3.2\n"); no file = nothing remembered.
inline constexpr const char* kMemoryFile = "update_notice.txt";

// "Update available: 0.3.2": the tag's three numbers without the channel prefix (np::rules::ParseVersion); a tag that is
// not a version is shown as it is.
std::string VersionText(std::string_view tag);
std::string DisplayText(std::string_view tag);

// A session relaunch (ruling R106): DOA4_NP_HANDOFF (the relaunch into an online session) or DOA4_NP_AFTER_SESSION (the
// relaunch back to offline after one) set and non-empty at start-up, or a netplay session active in this process
// (np::Active()). nullptr = not set. `wait_pid` (DOA4_WAIT_PID) does NOT count: alone it is a restart the player chose
// (the menu's Restart, the swimsuit import's restart, after an update), a normal start that checks; every session
// relaunch also carries one of the other two.
bool SessionRelaunch(const char* handoff, const char* after_session, const char* wait_pid, bool netplay_active);

// The start-up decision. `running`: this build's tag (DOA4_RELEASE_TAG); `dev_build`: np::rules::IsDevBuild(running);
// `remembered`: the memory file's tag ("" = none).
struct StartPlan {
  bool check = false;         // ask GitHub now (on its own thread)
  std::string show;           // the tag to show ("" = nothing)
  bool clear_memory = false;  // delete the memory before the check
  std::string why;            // the log line's text
};
StartPlan AtStart(bool enabled, std::string_view running, bool dev_build, bool session_relaunch,
                  std::string_view remembered);

// A check's answer (the start-up check or the button's). `tag`: the release it looked at.
struct MemoryPlan {
  std::string show;          // the tag to show ("" = nothing); the button's answer never shows (RefreshFromButton)
  bool write = false;        // the memory changes
  std::string memory;        // its new tag ("" = delete the file)
  std::string why;
};
MemoryPlan AfterCheck(updater::Found found, std::string_view tag, std::string_view remembered);
// Check for Updates: the same memory change, nothing shown.
MemoryPlan RefreshFromButton(updater::Found found, std::string_view tag, std::string_view remembered);

// The memory file. ReadMemory: the first line, without a UTF-8 BOM, CR or spaces ("" when there is no file or it cannot be
// read). WriteMemory: `tag` + "\n" into <dir>/update_notice.txt.tmp, flushed, then renamed over update_notice.txt
// (MoveFileExW, replace, write-through); an empty `tag` deletes the file. Returns "" or why it failed (the old file is
// then unchanged, and no .tmp is left).
std::string ReadMemory(const std::filesystem::path& dir);
std::string WriteMemory(const std::filesystem::path& dir, std::string_view tag);

// Answers arriving from the checks' threads, taken on the UI thread (no thread holds the app).
struct Arrival {
  bool from_button = false;
  updater::Found found = updater::Found::kFailed;
  std::string tag;
};
void Post(Arrival a);
std::vector<Arrival> TakeArrivals();

// PRESS START on the title (sprite 0x30001, the text; 0x30002 is its shadow), from the sub_823BB470 hook: every call's
// sprite id and its draw bit (byte 23 of the description, bit 0x08: clear = the original returns without drawing), with
// the guest swap count. "Drawn now" = a visible 0x30001 submission within the last kGapSwaps swaps.
// Controller ruling R103 (spec G7): the notice ends for the run only on the player's Start press on the title. The title's
// own attract sequence (about 30 s idle: the helicopters, then the demo fight) stops drawing PRESS START too, and the title
// comes back after it; then the notice shows again. Measured in Task 15 run 6 (ou_runs/t15/runs/r6_start_press): PRESS
// START drawn from swap 21 to 1845, gone in the attract; START in the attract -> the title again (shot 2646); START on the
// title (frame 2946) -> MODE SELECT (shot 3612); no PRESS START submission ever had its draw bit clear. The title's own
// state could not be read from code (its draw callback, sub_823A7FB8, is reached only through data tables), so the Start
// press is the game's own pad read: sub_82792040 (its only XamInputGetState path) returning START or A for any user
// while PRESS START was drawn this swap or at most kPressDrawnSwaps before (NotePad). That press is only PENDING (ruling
// R105): the notice hides at once (M2), and the press is CONFIRMED (Ended, latched for the rest of the process) only by a
// signal an accepted press alone produces: the next screen's own sprite through the same sub_823BB470 hook, i.e. a visible
// submission of a sprite id never submitted while the title was on screen with no press pending (the title's own sprites
// are learned as it draws them) and outside the title's own family (R117, kTitleSpriteFamily below). The pending press
// is CANCELLED, and the notice shows again with the title, when PRESS
// START has been gone more than kCancelGapSwaps with no confirmation (the attract's start) or kConfirmSwaps passed (a
// press the title ignored). Measured (fix round 1, ou15c, np_run sessions): an accepted START is confirmed by sprite 0xf7
// (the first of MODE SELECT's: 0xf7-0xfa, 0x12a, 0x4003d, 0x5000x) exactly 35 swaps after the press, both on the title
// (r8: press 600, 0xf7 at 635) and when pressed 2 swaps after PRESS START's last draw as the attract began (r9: press
// 1847, PRESS START last drawn at 1845, 0xf7 at 1882 = 37 swaps after it: the game accepted it and went to MODE SELECT).
// A 30-swap cancel gap (ou15c) cancelled that accepted press; kCancelGapSwaps is 90.
// The hooks write, the UI thread reads (atomics); the learned sprite set is behind a mutex.
inline constexpr uint32_t kPressStartSprite = 0x30001;
inline constexpr uint32_t kGapSwaps = 30;
inline constexpr uint32_t kPressDrawnSwaps = 2;    // a pad read counts when PRESS START was drawn within these swaps
inline constexpr uint32_t kConfirmSwaps = 300;     // 5 s at 60 swaps a second from the press to the next screen
inline constexpr uint32_t kCancelGapSwaps = 90;    // PRESS START gone this long with no confirmation: the attract
inline constexpr uint16_t kPressButtons = 0x1010;  // XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A
inline constexpr size_t kSpriteSetSize = 256;      // learned sprite ids; past it an unknown id counts as not the title's
// Ruling R117 (Task 12, residual I1 of the Task 15 re-review): the title's own sprite family, (id >> 16) == 0x3 (0x30000 the
// logo, 0x30001 PRESS START, 0x30002 its shadow, 0x30003; 0x30005 seen), never confirms a pending press, learned or not.
// Measured on ou12 (ou_runs/t12/runs/r12_idle_attract/game.log:516, :519): at the SECOND title -> attract change, 0x30005,
// which the title had never drawn, was first drawn at swap 18680, 2 swaps after PRESS START's last draw (18678): with a
// press pending (an A the title ignored up to kConfirmSwaps before) it would have confirmed it and ended the notice
// falsely. Every confirming sprite measured lies outside the family: MODE SELECT's 0xf7 (35-37 swaps after an accepted
// press: r8, r9, r12_attract_start), and its 0xf8-0xfa, 0x12a, 0x4003d, 0x40002, 0x5000x.
inline constexpr uint32_t kTitleSpriteFamily = 0x3;
constexpr bool InTitleFamily(uint32_t sprite) { return (sprite >> 16) == kTitleSpriteFamily; }
class TitleWatch {
 public:
  // kNewSprite: a sprite id's first visible submission (the census); kConfirmed: it confirmed the pending press.
  enum class Event { kNone, kFirstVisible, kHidden, kVisibleAgain, kNewSprite, kConfirmed };
  // The sprite hook: every submission (for its capped log lines).
  Event NoteDraw(uint32_t sprite, bool visible, uint32_t swap);
  // The pad-read hook: the buttons the game read for `user` (0-3). True when this read starts a press (START or A newly
  // down for that user) on the title: it becomes the pending press.
  bool NotePad(uint32_t user, uint16_t buttons, uint32_t swap);
  bool DrawnNow(uint32_t now_swap);  // drawn within the last kGapSwaps, no press pending, not Ended
  bool Pending(uint32_t now_swap);   // a press on the title waits for its confirmation
  bool Ended() const;                // the press was confirmed (latched)
  uint32_t LastVisibleSwap() const;  // 0 = never (the swap number + 1 is kept, so swap 0 counts)
  uint32_t PressSwap() const;        // the last press recorded on the title, + 1; 0 = none
  uint32_t ConfirmSprite() const { return confirm_sprite_.load(std::memory_order_acquire); }
  uint32_t VisibleCount() const { return visible_n_.load(std::memory_order_relaxed); }
  uint32_t HiddenCount() const { return hidden_n_.load(std::memory_order_relaxed); }

 private:
  struct Seen {
    uint32_t id = 0;
    bool title = false;  // submitted while the title was on screen with no press pending
  };
  std::atomic<uint32_t> last_visible_{0};  // swap + 1 of the last visible 0x30001 submission; 0 = never
  std::atomic<bool> last_was_visible_{false};
  std::atomic<uint32_t> press_{0};         // swap + 1 of the last press recorded on the title; 0 = none
  std::atomic<uint32_t> cancelled_{0};     // press_'s value once that press was cancelled
  std::atomic<uint32_t> pad_down_{0};      // bit u: kPressButtons were down at user u's last read
  std::atomic<bool> ended_{false};
  std::atomic<uint32_t> confirm_sprite_{0};
  std::atomic<uint32_t> visible_n_{0}, hidden_n_{0};
  std::mutex mu_;  // seen_
  Seen seen_[kSpriteSetSize];
  size_t seen_n_ = 0;
};
TitleWatch& Title();  // the process's watch (the hook and the app)

}  // namespace doa4::update_notice
