// In-game menu, opened and closed with Escape (bind_doa4_menu).
//
// Tabs: Game (display mode, picture, speakers, language, where the game files are), Controls (per-slot controller
// remap, keyboard binds), Photo, Extras (unlocks, game tweaks) and Online (hosting and joining a friend). Every setting is
// applied to its cvar and saved to dead_or_alive_4.toml with doa4::SaveTomlKey. The game keeps running while
// the menu is open; its input is blocked (DeadOrAlive4App::OnBlocksGuestInput),
// so the game sees an untouched controller and keyboard.
//
// Owned by DeadOrAlive4App: created when opened, destroyed when closed, so the
// presenter goes back to direct GPU-thread presents as soon as it closes.

#pragma once

#include <optional>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <rex/input/input.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window_listener.h>

#include "doa4_display.h"
#include "doa4_extras.h"
#include "doa4_guest.h"
#include "doa4_online_tab.h"
#include "doa4_online_view.h"
#include "doa4_photo_service.h"
#include "doa4_suits_import_flow.h"
#include "doa4_tweaks.h"

namespace rex::input {
class InputSystem;
}
namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui
struct ImFontAtlas;

namespace doa4 {

// The Esc menu's font (the user, 2026-11-11: text "a few sizes larger"): ImGui's built-in ProggyClean at 13 px, made for that
// size, with the SDK's Japanese merge, added to the atlas from the app's OnConfigureFonts. The SDK's own font is ProggyTiny
// at 10 px, and its renderer bakes each font at one size only (no ImGuiBackendFlags_RendererHasTextures), so scaling it
// would draw soft glyphs (review 1, I2). Without this font the menu falls back to the scaled default.
void AddMenuFont(ImFontAtlas* atlas);

struct MenuHost {
  rex::ui::Window* window = nullptr;
  std::filesystem::path config_path;  // <exe dir>/dead_or_alive_4.toml
  int running_scale = 1;              // resolution_scale the game started with
  // resolution_scale saved for the next start; owned by the app so it survives
  // the menu being closed and reopened. Changed only after a successful save.
  int* saved_scale = nullptr;
  // Speakers (in the Game tab): audio_speakers as the game started with (the audio driver reads
  // it once, when it opens), and as last saved for the next start (owned by the
  // app like saved_scale; changed only after a successful save).
  std::string running_speakers = "auto";
  std::string* saved_speakers = nullptr;
  // The runtime's input system, or null (before the runtime exists).
  std::function<rex::input::InputSystem*()> input;
  // Runs `fn` on the UI thread after the current frame: window changes and
  // closing the menu must not happen while the menu is drawing.
  std::function<void(std::function<void()> fn)> defer;
  // Destroys the menu (called through `defer`).
  std::function<void()> close_menu;
  // Switches the running game's anti-aliasing to graphics::kAntiAliasing[index]
  // (the SDK reads swap_post_effect only at start).
  std::function<void(int index)> set_post_effect;
  // Photo tab: the photo service's last result, and the folder photos go to
  // when doa4_photo_dir is empty.
  std::function<photo::Result()> photo_result;
  std::filesystem::path photo_default_dir;

  // Extras tab. `guest` is null until the XEX is loaded into guest memory.
  const GuestBytes* guest = nullptr;
  tweaks::TweakPatcher* patcher = nullptr;  // owned by the app
  tweaks::ProtectFn protect;
  // DOAX2 swimsuits: the import's one-line status, and each suit not loaded with why.
  std::string suits_summary;
  std::vector<std::string> suits_notes;
  // The Esc import (Plan 3, Task 6). The app owns the flow (the import survives the menu closing; the app destroys it after
  // this menu, before the runtime, at shutdown): the menu only borrows the pointer. Null: no import section.
  suits_import::Flow* suits_import = nullptr;
  // Answered by the app when asked (the menu may be built before the game paths and the runtime exist): the game files the
  // import reads, the resolved user data it writes, whether an import loaded at start (the button reads "Import again..."),
  // and the note (why the finished import could not be put in place at start, else the older-importer note; "" for neither).
  struct SuitsImportInfo {
    std::filesystem::path game_root, user_data;
    bool loaded = false;
    std::string note;
  };
  std::function<SuitsImportInfo()> suits_import_info;
  std::filesystem::path save_dir;  // the ups.dat save package folder, or empty
  // Where the one-time save backup goes: outside the folder the game lists
  // its saves from.
  std::filesystem::path backup_root;
  ExtrasState* extras = nullptr;   // owned by the app: survives closing the menu
  // The Output size row's exclusive mode list (Task 10 review I1): owned by the app, so a list made while exclusive
  // fullscreen was off serves the openings made while it is on. Null: the menu keeps its own, per opening.
  display::ExclusiveModesCache* exclusive_modes = nullptr;
  // Netplay (Task 12 fix round I3): asked before an Extras action (an unlock, a tweak toggle); true = refused (in an
  // online session: *status says why, a log line is written). Empty: never refused.
  std::function<bool(std::string_view action, std::string* status)> refuse;
  // Netplay (Task 14): true while this process runs an online session or its lobby is starting one
  // (online_view::SessionLocks). Five controls read their disabled state from online_view::LockedInSession with it
  // (Extras, the Game tab's Restart now buttons, its Language dropdown and its "Change..."), and the refusal behind them reads the same
  // answer. Empty: never locked.
  std::function<bool()> session_locked;
  OnlineHost online;  // the Online tab's callbacks; empty `state` = the tab says it is unavailable
  // Game tab: the folder the game started from, whether --game_data_root
  // chose it, and the action that closes the menu and opens the setup screen
  // to choose another (used from the next start).
  std::filesystem::path game_files;
  bool game_files_from_command_line = false;
  std::filesystem::path user_data;  // saves and the shader cache (shown only)
  std::function<void()> change_game_files;
};

class MenuDialog : public rex::ui::ImGuiDialog, public rex::ui::WindowInputListener {
 public:
  // Input listener z-order: above the ImGui drawer (64), so a key or click
  // that answers "press a key" never reaches ImGui, the binds or the game.
  static constexpr size_t kInputZOrder = 65;

  MenuDialog(rex::ui::ImGuiDrawer* drawer, MenuHost host);
  ~MenuDialog() override;

  // Logs the Online tab's state and the session locks if they changed (the same call OnDraw makes every frame). The
  // app calls it from a timer when DOA4_MENU_OPEN=1: a minimized window never paints, so OnDraw alone would not log.
  void PollNetState();
  // Test switch DOA4_EXTRAS_REQUEST: what a click would do, without drawing. "wipe", "restart", "language N" (a pick of
  // text id N in the Language dropdown) or "tweak NB" (tweak N, B = 0 or 1).
  // "suits_import" starts the Esc import from DOA4_SUITS_IMPORT_TEST=<ASCII path> as a click on the button would.
  // "skip_intro B" (B = 0 or 1): the Game tab's "Skip the intro at start-up" box set to B.
  void TestAction(std::string_view action);

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void OnMouseDown(rex::ui::MouseEvent& e) override;

 private:
  enum class Listen { kNone, kPad, kKey };

  void DrawVideoSettings();
  // The widest row any tab draws without wrapping (the window's minimum width; display::MenuWindowWidth).
  float WidestRow();
  // The three display-mode radios, VSync and Output size (R60: never locked, memory only near a session).
  void DrawDisplayRows();
  // Keeps the exclusive mode list (ExclusiveModes()) as display::ExclusiveModesStep says: called at the top of every
  // frame the menu draws, before any radio; a build runs from host_.defer (never DXGI inside the draw).
  void EnsureExclusiveModes();
  // The app's cache (host_.exclusive_modes), or this menu's own when the app gave none.
  display::ExclusiveModesCache& ExclusiveModes() {
    return host_.exclusive_modes ? *host_.exclusive_modes : own_exclusive_modes_;
  }
  // Saves `name` then sets it; near a session (SessionLocked) only sets it in memory. The one choice every
  // presentation row and the start-up box share.
  bool SetPresentationSetting(std::string_view name, std::string_view value, bool quote);
  void DrawSpeakerSettings();
  void DrawLanguageSettings();
  // The Game tab's "Skip the intro at start-up" box (doa4_skip_intro, read at the next start; doa4_intro_skip.h).
  void DrawIntroSettings();
  void DrawDiscordSettings();
  // The box's change (and the test action "skip_intro B"): saved, then set; near a session only set, as Show FPS does.
  void SetSkipIntro(bool on);
  // The dropdown's pick (and the test action "language <id>"): refused when locked, else saved, then applied live.
  void PickLanguage(uint8_t id);
  void DrawControlsTab();
  void DrawPadSection();
  void DrawKeyboardSection();
  void DrawExtrasTab();
  void DrawPhotoTab();
  void DrawOnlineTab();
  void DrawSuitsImport();
  // The one answer behind the import button's disabled state and the test action's refusal; *why says what stops it.
  bool SuitsImportEnabled(std::string* why) const;
  // The button's work (and the test action's): Flow::Start on the game files and the user data.
  void StartSuitsImport();
  // host_.defer, but a call queued before this menu was destroyed does nothing. The import flow is destroyed only after the
  // menu (OnShutdown), so a call that passes this check still has its flow.
  std::function<void(std::function<void()>)> GuardedDefer();
  void DrawGameTab();
  bool SessionLocked() const;
  bool Locked(online_view::Control control) const;  // LockedInSession(control, SessionLocked())
  // Logs the Online tab's state and the locks once per change (every frame the menu draws, whichever tab is shown).
  void LogNetState(const online_view::In& in, const online_view::View& view);
  // Saves the tweak's cvar; on success sets it and, for a live tweak, writes
  // or reverts its bytes. Failures go to status_; false when the setting did not change.
  bool SetTweak(size_t tweak, bool on);
  // Saves `name` first; only on success sets the cvar. Failures go to
  // status_ and leave the running game as it was. Returns whether it saved.
  bool SaveThenSet(std::string_view name, std::string_view value, bool quote);
  void StartPadListen(size_t row, const rex::input::X_INPUT_GAMEPAD* pad);
  void PollPadListen(const rex::input::X_INPUT_GAMEPAD& pad);
  void AssignPad(size_t target, size_t source);
  void AssignKey(size_t row, const std::string& key_name);
  // `control` is the button's own (kVideoRestart / kAudioRestart / kUpdateRestart): refused in an online session.
  void Restart(online_view::Control control);
  // The Extras tab's wipe: a button, then a confirmation with the second press.
  void DrawWipeUnlocks(const std::optional<uint32_t>& profile, bool locked);
  void WipeNow(uint32_t profile);
  // Sets the cvar and saves it; `quote` writes the value as a TOML string.
  // Failures go to status_.
  void SetAndSave(std::string_view name, std::string_view value, bool quote);

  MenuHost host_;
  int slot_ = 0;  // controller slot shown in the Controls tab
  Listen listen_ = Listen::kNone;
  size_t listen_row_ = 0;
  uint32_t pad_held_ = 0;  // PadControl bits held since pad listening started
  bool pad_connected_ = false;  // last non-busy GetRawState answer for slot_
  double next_save_poll_ = 0.0;  // ImGui time of the next "has the game saved?" check
  bool wipe_armed_ = false;      // the first press of "Wipe unlocks..." happened
  display::ExclusiveModesCache own_exclusive_modes_;  // used only without host_.exclusive_modes
  bool exclusive_modes_first_frame_ = true;  // this opening's first drawn frame is still to come (the dialog is per opening)
  // A build was started in this opening (queued on host_.defer; whatever its outcome, the only one this opening: a failed
  // listing is tried again at the next opening, not on every frame).
  bool exclusive_modes_attempted_ = false;
  std::string status_;     // last error, shown in red
  OnlineTab online_tab_;
  std::string logged_tab_line_;  // the last "[doa4][NET] tab: ..." line written
  bool logged_locks_ = false;    // the "menu: locked in session" line was written
  std::optional<bool> drawn_large_;  // the font of the last frame drawn (true: the fullscreen one); none before the first
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // GuardedDefer's token; cleared by ~MenuDialog
};

}  // namespace doa4
