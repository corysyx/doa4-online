// In-game menu (Escape): Game, Controls, Photo, Extras and Online tabs. See doa4_menu.h.

#include "doa4_menu.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/audio/speaker_mode.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/input/pad_remap.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_presenter.h>
#include <rex/ui/display_mode.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_win.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "doa4_config.h"
#include "doa4_discord.h"
#include "doa4_display.h"
#include "doa4_gamefiles.h"
#include "doa4_graphics.h"
#include "doa4_language.h"
#include "doa4_photo_win.h"
#include "doa4_suits_import_view.h"
#include "doa4_speakers.h"
#include "doa4_utf8.h"
#include "net/np_session.h"
#include "doa4_unlocks.h"

namespace doa4 {

namespace {
ImFont* g_menu_font = nullptr;  // AddMenuFont's; set once at start-up on the UI thread, before any menu draws
}  // namespace

void AddMenuFont(ImFontAtlas* atlas) {
  if (!atlas) return;
  ImFontConfig config;
  config.OversampleH = config.OversampleV = 1;
  config.PixelSnapH = true;
  config.SizePixels = display::kMenuLargeFontPx;  // ProggyClean's own size, 13 px
  g_menu_font = atlas->AddFontDefault(&config);
  // The SDK's Japanese merge (rexglue imgui_drawer.cpp), at this font's size: paths and names can hold Japanese.
  const char* jp_font_path = "C:\\Windows\\Fonts\\msgothic.ttc";
  std::error_code ec;
  if (g_menu_font && std::filesystem::exists(jp_font_path, ec)) {
    ImFontConfig jp;
    jp.MergeMode = true;
    jp.OversampleH = jp.OversampleV = 1;
    jp.PixelSnapH = true;
    jp.FontNo = 0;
    atlas->AddFontFromFileTTF(jp_font_path, display::kMenuLargeFontPx, &jp, atlas->GetGlyphRangesJapanese());
  }
  REXLOG_INFO("[doa4][menu] menu font: {}", g_menu_font ? "ProggyClean 13 px" : "none (the default font, scaled)");
}

namespace {

using rex::input::PadControl;
using rex::input::PadRemap;
using rex::X_RESULT;  // X_ERROR_SUCCESS expands to an unqualified X_RESULT

constexpr std::array<std::string_view, 4> kPadRemapCvars = {"pad_remap_p1", "pad_remap_p2",
                                                            "pad_remap_p3", "pad_remap_p4"};

// Indexed by PadControl.
constexpr std::array<const char*, rex::input::kPadControlCount> kPadLabels = {
    "A",     "B",    "X",           "Y",           "LB",       "RB",         "LT",         "RT",
    "Back",  "Start", "L-stick click", "R-stick click", "D-pad up", "D-pad down", "D-pad left",
    "D-pad right"};

// What each button does in the game's Type A layout, its first Button Config choice: the action bits of the preset
// table at 0x821FA660 read through the button masks at 0x82E46B40 (sub_822F0098 ORs them per pressed button).
constexpr std::array<std::array<const char*, 2>, 8> kButtonNotes = {{
    {"A", "Throw (F+P)"},
    {"B", "Kick"},
    {"X", "Free/Hold"},
    {"Y", "Punch"},
    {"LB", "Taunt (F+P+K)"},
    {"RB", "Tag Change"},
    {"LT", "P+K"},
    {"RT", "F+K"},
}};

struct KeyRow {
  const char* cvar;
  const char* label;
};
// The SDK's keyboard-as-controller binds (src/input/mnk/mnk_input_driver.cpp).
constexpr std::array<KeyRow, 21> kKeyRows = {{
    {"keybind_a", "A"},
    {"keybind_b", "B"},
    {"keybind_x", "X"},
    {"keybind_y", "Y"},
    {"keybind_left_shoulder", "LB"},
    {"keybind_right_shoulder", "RB"},
    {"keybind_left_trigger", "LT"},
    {"keybind_right_trigger", "RT"},
    {"keybind_back", "Back"},
    {"keybind_start", "Start"},
    {"keybind_guide", "Guide"},
    {"keybind_lstick_press", "L-stick click"},
    {"keybind_rstick_press", "R-stick click"},
    {"keybind_lstick_up", "L-stick up"},
    {"keybind_lstick_down", "L-stick down"},
    {"keybind_lstick_left", "L-stick left"},
    {"keybind_lstick_right", "L-stick right"},
    {"keybind_dpad_up", "D-pad up"},
    {"keybind_dpad_down", "D-pad down"},
    {"keybind_dpad_left", "D-pad left"},
    {"keybind_dpad_right", "D-pad right"},
}};

constexpr ImVec4 kErrorColor(1.0f, 0.45f, 0.4f, 1.0f);
constexpr ImVec4 kNoteColor(1.0f, 0.85f, 0.4f, 1.0f);
constexpr ImVec4 kDoneColor(0.45f, 0.9f, 0.5f, 1.0f);

struct UnlockRow {
  unlocks::Unlock what;
  const char* label;
};
constexpr std::array<UnlockRow, 5> kUnlockRows = {{
    {unlocks::Unlock::kCharacters, "Unlock all characters"},
    {unlocks::Unlock::kCostumes, "Unlock all costumes"},
    {unlocks::Unlock::kVoices, "Unlock all system voices"},
    {unlocks::Unlock::kStages, "Unlock all stages"},
    {unlocks::Unlock::kEverything, "Unlock everything"},
}};

// Texts drawn without wrapping, named so the window width (MenuDialog::WidestRow) measures exactly what is drawn.
constexpr std::array<const char*, 3> kDisplayModes = {"Windowed", "Borderless fullscreen",
                                                      "Exclusive fullscreen (experimental)"};
constexpr const char* kDisplayModeNote = "Experimental: measured no lower input lag than borderless fullscreen.";
constexpr const char* kVsyncNote =
    "Removes tearing on monitors without G-Sync/FreeSync; can add up to one screen refresh of input lag.";
constexpr const char* kOutputSizeLabel = "Output size";
constexpr float kOutputSizeComboWidth = 260.0f;
constexpr const char* kOutputSizeNote = "The size the picture is shown at. Render scale sets how sharp the game draws.";
constexpr const char* kShowFpsLabel = "Show FPS over the game (F7)";
constexpr std::array<const char*, 2> kAntiAliasingNote = {
    "The game uses 2x MSAA; FXAA Extreme suits it best. FXAA also softens", "text a little."};
constexpr std::array<const char*, 2> kTextureFilteringNote = {
    "Applies to textures as the game draws them; you may see it after the", "next screen loads."};
constexpr std::array<const char*, 2> kSpeakersNote = {
    "Automatic follows Windows: 5.1 when the speakers are set to 5.1 or more,",
    "stereo otherwise. 5.1 surround always sends six channels."};
constexpr float kLanguageComboWidth = 220.0f;
constexpr const char* kSkipIntroLabel = "Skip the intro at start-up";
constexpr const char* kSkipIntroNote = "Starts at the title screen. Applies at the next start.";
constexpr const char* kDiscordLabel = "Show \"Playing Dead or Alive 4 Online\" on Discord";
constexpr const char* kDiscordNote = "Needs the Discord app running on this PC. Applies at once.";
constexpr const char* kDiscordNoteUnavailable = "Not available in this build yet.";
const char* DiscordNote() { return discord::Available() ? kDiscordNote : kDiscordNoteUnavailable; }
constexpr const char* kGameFilesNote = "A new location is used from the next start.";
constexpr const char* kButtonNotesNote = "The game's Type A layout. Its Button Config (Options) can change these.";
constexpr const char* kNoControllerNote = "No controller in this slot.";
constexpr const char* kKeyboardLabel = "Use keyboard as controller";
constexpr const char* kPadListenText = "Press a button... (Esc cancels)";
constexpr const char* kKeyListenText = "Press a key or mouse button... (Esc cancels)";
constexpr float kTableFirstColumnWidth = 140.0f;
constexpr float kExtrasButtonWidth = 220.0f;
constexpr const char* kLoadProfileNote = "Load a profile first (reach the main menu).";
constexpr const char* kWipeQuestion = "Lock everything again, as in a new save?";
constexpr const char* kBustLabel = "Full DOAX2 breast movement";
constexpr const char* kBustNote = "Up and down range of DOAX2 instead of the character's DOA4 range. Takes effect at once.";
constexpr const char* kPhotoMonitorNote = "Monitor size: at x4 the same clean, supersampled image you see on screen.";
constexpr const char* kPhotoCopyLabel = "Also save a full-quality copy when you save a snapshot to the in-game Album";
constexpr const char* kPhotoCopyNote = "The copy is the game's 3:2 photo, at the render scale's full quality.";

uint32_t PressedControls(const rex::input::X_INPUT_GAMEPAD& pad) {
  uint32_t mask = 0;
  for (size_t i = 0; i < rex::input::kPadControlCount; ++i) {
    if (rex::input::IsPadControlPressed(pad, static_cast<PadControl>(i))) mask |= 1u << i;
  }
  return mask;
}

PadRemap CurrentRemap(int slot) {
  PadRemap remap;
  rex::input::ParsePadRemap(rex::cvar::Query<std::string>(kPadRemapCvars[slot]), &remap);
  return remap;
}

}  // namespace

MenuDialog::MenuDialog(rex::ui::ImGuiDrawer* drawer, MenuHost host)
    : rex::ui::ImGuiDialog(drawer), host_(std::move(host)) {
  host_.window->AddInputListener(this, kInputZOrder);
}

MenuDialog::~MenuDialog() {
  *alive_ = false;
  host_.window->RemoveInputListener(this);
}

std::function<void(std::function<void()>)> MenuDialog::GuardedDefer() {
  return [this, alive = std::weak_ptr<bool>(alive_)](std::function<void()> fn) {
    host_.defer([alive, fn = std::move(fn)] {
      if (auto a = alive.lock(); a && *a) fn();
    });
  };
}

bool MenuDialog::SuitsImportEnabled(std::string* why) const {
  const std::string none;
  std::string reason;
  if (!host_.suits_import) {
    reason = "no import in this build";
  } else if (host_.suits_import->busy()) {
    reason = "an import is running";
  } else if (Locked(online_view::Control::kExtras)) {
    reason = online_view::kLockedText;
  } else if (host_.online.state) {
    const online_view::In in = host_.online.state(false);
    if (!online_view::Describe(in).suits_import_enabled) reason = "online play is on: disconnect first";
  }
  if (why) *why = reason;
  return reason.empty();
}

void MenuDialog::StartSuitsImport() {
  if (!host_.suits_import) return;
  std::string why;
  if (!SuitsImportEnabled(&why)) {
    REXLOG_INFO("[doa4][SUITS] import: start refused: {}", why);
    return;
  }
  const MenuHost::SuitsImportInfo info = host_.suits_import_info ? host_.suits_import_info() : MenuHost::SuitsImportInfo{};
  if (info.game_root.empty() || info.user_data.empty()) {
    REXLOG_INFO("[doa4][SUITS] import: start refused: the game files or the user data folder is not known yet");
    return;
  }
  REXLOG_INFO("[doa4][SUITS] import: game files {}, user data {}", Utf8(info.game_root), Utf8(info.user_data));
  host_.suits_import->Start(info.game_root, info.user_data, 2);
}

bool MenuDialog::SessionLocked() const { return host_.session_locked && host_.session_locked(); }

bool MenuDialog::Locked(online_view::Control control) const {
  return online_view::LockedInSession(control, SessionLocked());
}

void MenuDialog::LogNetState(const online_view::In& in, const online_view::View& view) {
  const std::string tab = online_view::TabLogLine(in, view);
  if (tab != logged_tab_line_) {
    logged_tab_line_ = tab;
    REXLOG_INFO("{}", tab);
  }
  const std::string locks = online_view::LockedLine(SessionLocked());
  if (!locks.empty() && !logged_locks_) {
    logged_locks_ = true;
    REXLOG_INFO("{}", locks);
  }
}

void MenuDialog::PollNetState() {
  if (!host_.online.state) return;
  const online_view::In online_in = host_.online.state(false);  // not the tab: no build id
  LogNetState(online_in, online_view::Describe(online_in));
}

void MenuDialog::OnDraw(ImGuiIO& io) {
  // The Online tab's state and the session locks are logged whichever tab is shown (the log is the test hook:
  // the session's shots never hold the menu).
  PollNetState();
  // The exclusive mode list, whichever tab is shown and before any radio (Task 10 review I1): made while exclusive
  // fullscreen is off, at this opening's first frame.
  EnsureExclusiveModes();
  // On every tab: the Game and Online tabs show the unsaved warning too.
  if (ImGui::GetTime() >= next_save_poll_) {
    if (host_.guest && host_.extras) PollSaved(*host_.guest, host_.save_dir, host_.extras);
    next_save_poll_ = ImGui::GetTime() + 1.0;
  }
  // The menu's text size (AddMenuFont; display::kMenuFontScale), pushed before WidestRow so the window's width is measured
  // with it.
  // The user (2026-11-11): the larger font in fullscreen (borderless or exclusive), the SDK's 10 px one in a window, from
  // the window's live state, so a switch while the menu is open changes it on the next frame.
  const bool large = host_.window && host_.window->IsFullscreen();
  if (!large) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase);
  } else if (g_menu_font) {
    ImGui::PushFont(g_menu_font, g_menu_font->LegacySize);
  } else {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * display::kMenuFontScale);
  }
  // The font changed since the last frame (a display mode switch): the window takes the new measured width once.
  const bool font_changed = drawn_large_.has_value() && *drawn_large_ != large;
  drawn_large_ = large;
  // centred again on that switch too (review2 M3), or a taller window from a large display can hang off a smaller one
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          font_changed ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  // Task 12 fix round 2: wide enough for the widest unwrapped row of every tab (measured, WidestRow), and never narrower
  // (a constraint, so a narrower size from an earlier opening or a drag cannot cut text off); clamped to the display.
  const ImGuiStyle& style = ImGui::GetStyle();
  const float width = display::MenuWindowWidth(WidestRow(), style.WindowPadding.x, style.ScrollbarSize, io.DisplaySize.x);
  ImGui::SetNextWindowSize(ImVec2(width, 0.0f), font_changed ? ImGuiCond_Always : ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::Begin("DOA4###doa4_menu", nullptr, ImGuiWindowFlags_NoCollapse)) {
    // Quit Game (the user, 2026-11-11): a red button at the top right that closes the game, the same way as closing its
    // window (the Restart buttons' path), in or out of an online session.
    const float quit_w = ImGui::CalcTextSize("Quit Game").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - quit_w));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.08f, 0.08f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.05f, 0.05f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
    if (ImGui::Button("Quit Game")) {
      REXLOG_INFO("[doa4][menu] Quit Game pressed: closing the game");
      rex::ui::Window* window = host_.window;
      host_.defer([window] { window->RequestClose(); });
    }
    ImGui::PopStyleColor(4);
    if (ImGui::BeginTabBar("tabs")) {
      if (ImGui::BeginTabItem("Game")) {
        DrawGameTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controls")) {
        DrawControlsTab();
        ImGui::EndTabItem();
      } else if (listen_ != Listen::kNone) {
        listen_ = Listen::kNone;  // left the tab while a row was listening
      }
      if (ImGui::BeginTabItem("Photo")) {
        DrawPhotoTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Extras")) {
        DrawExtrasTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Online")) {
        DrawOnlineTab();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    if (!status_.empty()) {
      ImGui::Separator();
      ImGui::TextColored(kErrorColor, "%s", status_.c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("Close (Esc)")) {
      host_.defer(host_.close_menu);
    }
  }
  ImGui::End();
  ImGui::PopFont();
}

// Task 12 fix round 2: the widest row any tab draws without wrapping, measured with the font in use (ImGui sizes a
// radio button or checkbox as square + inner spacing + label, a button as its label + frame padding). TextWrapped texts
// follow the window and are not measured. Dynamic texts are measured as they are now (the suits notes, the custom
// texture filter value; the Online tab's notes in OnlineTab::WidestRow), so one that appears widens the window then.
float MenuDialog::WidestRow() {
  const ImGuiStyle& st = ImGui::GetStyle();
  const float frame = ImGui::GetFrameHeight();
  const auto text = [](const std::string& t) { return ImGui::CalcTextSize(t.c_str(), nullptr, true).x; };
  const auto toggle = [&](const std::string& t) { return display::ToggleWidth(frame, st.ItemInnerSpacing.x, text(t)); };
  const auto button = [&](const std::string& t) { return text(t) + st.FramePadding.x * 2; };
  const auto row = [&](const std::vector<float>& items) { return display::RowWidth(items, st.ItemSpacing.x); };
  // a row of the two-column tables (first column kTableFirstColumnWidth): each column's cell padding and the 1-pixel inner
  // border (ImGuiTableFlags_BordersInnerV)
  const auto table = [&](float second) { return display::MenuPx(kTableFirstColumnWidth, ImGui::GetFontSize()) + second + st.CellPadding.x * 4 + 1.0f; };
  const auto toggles = [&](const auto& labels) {
    std::vector<float> items;
    for (const auto& l : labels) items.push_back(toggle(l));
    return row(items);
  };
  std::vector<float> w;
  // Game
  for (const char* m : kDisplayModes) w.push_back(toggle(m));
  for (const char* t : {kDisplayModeNote, kVsyncNote, kOutputSizeNote, kSkipIntroNote, kGameFilesNote}) w.push_back(text(t));
  w.push_back(toggle("VSync"));
  w.push_back(row({text(kOutputSizeLabel), display::MenuPx(kOutputSizeComboWidth, ImGui::GetFontSize())}));
  w.push_back(toggle(kShowFpsLabel));
  for (const char* s : graphics::kRenderScales) w.push_back(toggle(s));
  w.push_back(text(fmt::format("Applies after a restart (running at x{}).", graphics::kRenderScales.size())));
  w.push_back(button("Restart now"));
  {
    std::vector<std::string> aa, tf, sp;
    for (const auto& c : graphics::kAntiAliasing) aa.push_back(c.label);
    for (const auto& c : graphics::kTextureFiltering) tf.push_back(c.label);
    for (const auto& c : speakers::kChoices) {
      sp.push_back(c.label);
      w.push_back(text(fmt::format("Applies after a restart (running on {}).", c.label)));
    }
    w.push_back(toggles(aa));
    w.push_back(toggles(tf));
    w.push_back(toggles(sp));
  }
  for (const char* t : kAntiAliasingNote) w.push_back(text(t));
  for (const char* t : kTextureFilteringNote) w.push_back(text(t));
  for (const char* t : kSpeakersNote) w.push_back(text(t));
  w.push_back(text(fmt::format("Custom ({}) set in dead_or_alive_4.toml.", rex::cvar::Query<int32_t>("anisotropic_override"))));
  {
    ExtrasState unsaved;  // both restart warnings (UnsavedUnlocksWarning), whichever shows
    unsaved.waiting_for_save = true;
    w.push_back(text(UnsavedUnlocksWarning(unsaved)));
    unsaved.pending_wipe = true;
    w.push_back(text(UnsavedUnlocksWarning(unsaved)));
  }
  w.push_back(row({display::MenuPx(kLanguageComboWidth, ImGui::GetFontSize()), text(online_view::kLockedText)}));
  w.push_back(toggle(kSkipIntroLabel));
  w.push_back(toggle(kDiscordLabel));
  w.push_back(text(DiscordNote()));
  w.push_back(row({button("Change..."), text(online_view::kLockedText), button("Open folder")}));
  // Controls
  w.push_back(toggles(std::array<std::string, 4>{"Player 1", "Player 2", "Player 3", "Player 4"}));
  w.push_back(text(kNoControllerNote));
  w.push_back(text(kButtonNotesNote));
  for (const auto& note : kButtonNotes) w.push_back(table(text(note[1])));
  w.push_back(table(button(kPadListenText)));
  w.push_back(table(button(kKeyListenText)));
  w.push_back(button("Reset Player 4"));
  w.push_back(toggle(kKeyboardLabel));
  w.push_back(row({text("Keyboard is"), toggle("P1##kbd"), toggle("P2##kbd"), toggle("P3##kbd"), toggle("P4##kbd")}));
  w.push_back(button("Reset keyboard"));
  // Photo
  for (const auto& f : photo::kFormats) w.push_back(toggle(f.label));
  w.push_back(text(kPhotoMonitorNote));
  w.push_back(row({button("Choose folder..."), button("Open folder"), button("Use default")}));
  w.push_back(toggle(kPhotoCopyLabel));
  w.push_back(text(kPhotoCopyNote));
  // Extras
  w.push_back(text(online_view::kLockedText));
  w.push_back(text(kLoadProfileNote));
  w.push_back(row({display::MenuPx(kExtrasButtonWidth, ImGui::GetFontSize()), text("Unlocked")}));
  w.push_back(text(kWipeQuestion));
  w.push_back(row({button("Yes, wipe unlocks"), button("Cancel")}));
  w.push_back(text("Waiting for the game to save..."));
  for (const tweaks::Tweak& t : tweaks::AllTweaks()) w.push_back(toggle(t.label));
  w.push_back(toggle(kBustLabel));
  w.push_back(text(kBustNote));
  for (const std::string& note : host_.suits_notes) w.push_back(text(note));
  // Online
  online_view::In in;
  if (host_.online.state) in = host_.online.state(false);
  w.push_back(OnlineTab::WidestRow(in, online_view::Describe(in),
                                   host_.extras ? UnsavedUnlocksWarning(*host_.extras) : nullptr));
  float widest = 0;
  for (float x : w) widest = std::max(widest, x);
  return widest;
}

// The Game tab's first part: display mode, Show FPS, render scale, anti-aliasing, texture filtering.
void MenuDialog::DrawVideoSettings() {
  ImGui::SeparatorText("Display mode");
  DrawDisplayRows();

  bool show_fps = rex::cvar::Query<bool>("doa4_show_fps");
  if (ImGui::Checkbox(kShowFpsLabel, &show_fps)) {
    if (SetPresentationSetting("doa4_show_fps", show_fps ? "true" : "false", false) && host_.online.shown_changed) {
      host_.online.shown_changed();
    }
  }

  ImGui::SeparatorText("Render scale");
  int& saved_scale = *host_.saved_scale;
  for (int scale = 1; scale <= int(graphics::kRenderScales.size()); ++scale) {
    if (ImGui::RadioButton(graphics::kRenderScales[scale - 1], saved_scale == scale) &&
        saved_scale != scale) {
      // resolution_scale is read once at start-up: save it for the next start
      // without changing the running value.
      std::string error =
          SaveTomlKey(host_.config_path, "resolution_scale", std::to_string(scale));
      if (error.empty()) {
        saved_scale = scale;
        status_.clear();
      } else {
        status_ = "Could not save settings: " + error;
      }
    }
  }
  if (saved_scale != host_.running_scale) {
    ImGui::TextColored(kNoteColor, "Applies after a restart (running at x%d).",
                       host_.running_scale);
    ImGui::BeginDisabled(Locked(online_view::Control::kVideoRestart));
    if (ImGui::Button("Restart now")) {
      Restart(online_view::Control::kVideoRestart);
    }
    ImGui::EndDisabled();
    if (Locked(online_view::Control::kVideoRestart)) {
      ImGui::TextColored(kNoteColor, "%s", online_view::kLockedText);
    }
    if (const char* warning = UnsavedUnlocksWarning(*host_.extras)) {
      ImGui::TextColored(kErrorColor, "%s", warning);
    }
  }

  ImGui::SeparatorText("Anti-aliasing");
  ImGui::PushID("aa");
  const int aa = graphics::AntiAliasingIndex(rex::cvar::Query<std::string>("swap_post_effect"));
  for (int i = 0; i < int(graphics::kAntiAliasing.size()); ++i) {
    if (i) ImGui::SameLine();
    if (ImGui::RadioButton(graphics::kAntiAliasing[i].label, aa == i) && aa != i &&
        SaveThenSet("swap_post_effect", graphics::kAntiAliasing[i].value, true) &&
        host_.set_post_effect) {
      host_.set_post_effect(i);
    }
  }
  ImGui::PopID();
  for (const char* line : kAntiAliasingNote) ImGui::TextDisabled("%s", line);

  ImGui::SeparatorText("Texture filtering");
  ImGui::PushID("aniso");
  const int32_t aniso = rex::cvar::Query<int32_t>("anisotropic_override");
  const int row = graphics::TextureFilteringIndex(aniso);
  for (int i = 0; i < int(graphics::kTextureFiltering.size()); ++i) {
    if (i) ImGui::SameLine();
    if (ImGui::RadioButton(graphics::kTextureFiltering[i].label, row == i) && row != i) {
      // Read by the SDK each time a texture is bound: no further call needed.
      SaveThenSet("anisotropic_override", graphics::kTextureFiltering[i].value, false);
    }
  }
  ImGui::PopID();
  if (row < 0) {
    ImGui::TextColored(kNoteColor, "Custom (%d) set in dead_or_alive_4.toml.", aniso);
  }
  for (const char* line : kTextureFilteringNote) ImGui::TextDisabled("%s", line);
}

// The Game tab's speakers part: the Speakers choice (its own separator) and the Output device block.
void MenuDialog::DrawSpeakerSettings() {
  ImGui::SeparatorText("Speakers");
  if (!host_.saved_speakers) return;
  std::string& saved = *host_.saved_speakers;
  const int row = speakers::ChoiceIndex(saved);
  for (int i = 0; i < int(speakers::kChoices.size()); ++i) {
    if (i) ImGui::SameLine();
    if (ImGui::RadioButton(speakers::kChoices[i].label, row == i) && row != i) {
      // The audio driver reads audio_speakers once, when it opens: save it for
      // the next start without changing the running value.
      std::string error = SaveTomlKey(host_.config_path, "audio_speakers",
                                      TomlString(speakers::kChoices[i].value));
      if (error.empty()) {
        saved = speakers::kChoices[i].value;
        status_.clear();
      } else {
        status_ = "Could not save settings: " + error;
      }
    }
  }
  if (speakers::ChoiceIndex(saved) != speakers::ChoiceIndex(host_.running_speakers)) {
    ImGui::TextColored(kNoteColor, "Applies after a restart (running on %s).",
                       speakers::kChoices[speakers::ChoiceIndex(host_.running_speakers)].label);
    ImGui::BeginDisabled(Locked(online_view::Control::kAudioRestart));
    if (ImGui::Button("Restart now")) {
      Restart(online_view::Control::kAudioRestart);
    }
    ImGui::EndDisabled();
    if (Locked(online_view::Control::kAudioRestart)) {
      ImGui::TextColored(kNoteColor, "%s", online_view::kLockedText);
    }
    if (const char* warning = UnsavedUnlocksWarning(*host_.extras)) {
      ImGui::TextColored(kErrorColor, "%s", warning);
    }
  }
  for (const char* line : kSpeakersNote) ImGui::TextDisabled("%s", line);

  ImGui::SeparatorText("Output device");
  const rex::audio::OutputInfo info = rex::audio::GetOutputInfo();
  ImGui::TextWrapped("%s", speakers::DescribeOutput(info.device_channels, info.stream_channels).c_str());
  if (info.stream_channels != 0) {
    ImGui::TextWrapped("The game is sending: %s.", info.stream_channels == 2 ? "stereo" : "5.1");
  }
  const std::string hint =
      speakers::Hint(info.device_channels, info.stream_channels, host_.running_speakers);
  if (!hint.empty()) ImGui::TextWrapped("%s", hint.c_str());
}

void MenuDialog::DrawControlsTab() {
  DrawPadSection();
  ImGui::Spacing();
  ImGui::SeparatorText("What the buttons do");
  if (ImGui::BeginTable("button_notes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, display::MenuPx(kTableFirstColumnWidth, ImGui::GetFontSize()));
    ImGui::TableSetupColumn("Action");
    for (const auto& note : kButtonNotes) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(note[0]);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(note[1]);
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("%s", kButtonNotesNote);
  ImGui::Spacing();
  DrawKeyboardSection();
}

void MenuDialog::DrawPadSection() {
  ImGui::SeparatorText("Controller");
  for (int slot = 0; slot < 4; ++slot) {
    if (slot) ImGui::SameLine();
    std::string label = fmt::format("Player {}", slot + 1);
    if (ImGui::RadioButton(label.c_str(), slot_ == slot) && slot_ != slot) {
      slot_ = slot;
      pad_connected_ = false;
      if (listen_ == Listen::kPad) listen_ = Listen::kNone;
    }
  }
  if (rex::cvar::Query<std::string>("input_backend") != "sdl") {
    ImGui::TextWrapped("Controller remapping needs the SDL input backend (input_backend = sdl).");
    return;
  }

  rex::input::InputSystem* input = host_.input ? host_.input() : nullptr;
  rex::input::X_INPUT_STATE raw{};
  const X_RESULT result = input ? input->GetRawState(slot_, &raw) : X_ERROR_DEVICE_NOT_CONNECTED;
  if (result != X_ERROR_BUSY) {  // busy: keep last frame's answer
    pad_connected_ = result == X_ERROR_SUCCESS;
    if (pad_connected_ && listen_ == Listen::kPad) {
      PollPadListen(raw.gamepad);
    }
  }
  const bool connected = pad_connected_ && result == X_ERROR_SUCCESS;
  if (!pad_connected_) {
    ImGui::TextDisabled("%s", kNoControllerNote);
  }

  const PadRemap remap = CurrentRemap(slot_);
  if (ImGui::BeginTable("pad", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, display::MenuPx(kTableFirstColumnWidth, ImGui::GetFontSize()));
    ImGui::TableSetupColumn("Driven by");
    for (size_t row = 0; row < rex::input::kPadControlCount; ++row) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(kPadLabels[row]);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(row));
      const bool listening = listen_ == Listen::kPad && listen_row_ == row;
      const char* text = listening ? kPadListenText
                                   : kPadLabels[static_cast<size_t>(remap.source[row])];
      if (ImGui::Button(text, ImVec2(-FLT_MIN, 0.0f)) && !listening) {
        StartPadListen(row, connected ? &raw.gamepad : nullptr);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  std::string reset = fmt::format("Reset Player {}", slot_ + 1);
  if (ImGui::Button(reset.c_str())) {
    listen_ = Listen::kNone;
    SetAndSave(kPadRemapCvars[slot_], "", true);
  }
}

void MenuDialog::DrawKeyboardSection() {
  ImGui::SeparatorText("Keyboard");
  bool mnk_mode = rex::cvar::Query<bool>("mnk_mode");
  if (ImGui::Checkbox(kKeyboardLabel, &mnk_mode)) {
    SetAndSave("mnk_mode", mnk_mode ? "true" : "false", false);
  }
  const int32_t user = rex::cvar::Query<int32_t>("mnk_user_index");
  ImGui::TextUnformatted("Keyboard is");
  for (int32_t slot = 0; slot < 4; ++slot) {
    ImGui::SameLine();
    std::string label = fmt::format("P{}##kbd", slot + 1);
    if (ImGui::RadioButton(label.c_str(), user == slot) && user != slot) {
      SetAndSave("mnk_user_index", std::to_string(slot), false);
    }
  }

  if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, display::MenuPx(kTableFirstColumnWidth, ImGui::GetFontSize()));
    ImGui::TableSetupColumn("Key");
    for (size_t row = 0; row < kKeyRows.size(); ++row) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(kKeyRows[row].label);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(100 + row));
      const bool listening = listen_ == Listen::kKey && listen_row_ == row;
      std::string key = rex::cvar::Query<std::string>(kKeyRows[row].cvar);
      std::string text = listening ? kKeyListenText
                                   : (key.empty() ? "(none)" : key);
      if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0.0f)) && !listening) {
        listen_ = Listen::kKey;
        listen_row_ = row;
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (ImGui::Button("Reset keyboard")) {
    listen_ = Listen::kNone;
    for (size_t row = 0; row < kKeyRows.size(); ++row) {
      const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(kKeyRows[row].cvar);
      if (!info) continue;
      std::string value = info->default_value;
      if (value == "Escape") value = "Return";  // Escape opens this menu
      SetAndSave(kKeyRows[row].cvar, value, true);
    }
  }
}

void MenuDialog::DrawExtrasTab() {
  ImGui::SeparatorText("Unlocks");
  // In an online session nothing here may touch the player's real save or the guest's tables (spec 6).
  const bool extras_locked = Locked(online_view::Control::kExtras);
  if (extras_locked) ImGui::TextColored(kNoteColor, "%s", online_view::kLockedText);
  const std::optional<uint32_t> profile =
      host_.guest ? unlocks::FindProfile(*host_.guest) : std::nullopt;
  if (!profile) {
    ImGui::TextDisabled("%s", kLoadProfileNote);
  }
  for (const UnlockRow& row : kUnlockRows) {
    const bool done = profile && unlocks::IsUnlocked(*host_.guest, *profile, row.what);
    ImGui::BeginDisabled(!profile || done || extras_locked);
    if (ImGui::Button(row.label, ImVec2(display::MenuPx(kExtrasButtonWidth, ImGui::GetFontSize()), 0.0f)) &&
        !(host_.refuse && host_.refuse(std::string("unlock ") + row.label, &status_))) {
      std::string error;
      if (UnlockWithBackup(*host_.guest, *profile, row.what, host_.save_dir, host_.backup_root,
                           BackupStamp(), host_.extras, &error)) {
        status_.clear();
        REXLOG_INFO("[doa4][EXTRAS] {}: {}", row.label, host_.extras->last_change);
      } else {
        status_ = error;
        REXLOG_ERROR("[doa4][EXTRAS] {}: {}", row.label, error);
      }
    }
    ImGui::EndDisabled();
    if (done) {
      ImGui::SameLine();
      ImGui::TextColored(kDoneColor, "Unlocked");
    }
    if (row.what == unlocks::Unlock::kStages) {
      ImGui::Indent();
      ImGui::TextWrapped(
          "Also unlocks characters, costumes and voices, and records one Story clear for each "
          "Story character (shown in Records).");
      ImGui::Unindent();
    }
  }

  DrawWipeUnlocks(profile, extras_locked);

  ExtrasState& state = *host_.extras;
  if (!state.last_change.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", state.last_change.c_str());
  }
  if (state.waiting_for_save) {
    ImGui::TextColored(kNoteColor, "Waiting for the game to save...");
    ImGui::TextWrapped(
        "Change any Options setting, or finish a mode, to save now. Re-enter character or "
        "stage select to see the change.");
  } else if (state.saved) {
    ImGui::TextColored(kDoneColor, "Saved.");
  }
  if (!state.backup.empty()) {
    ImGui::TextWrapped("Backup of your save from before the first unlock: %s",
                       Utf8(state.backup).c_str());
  } else if (!state.backup_note.empty()) {
    ImGui::TextWrapped("%s", state.backup_note.c_str());
  }
  if (!state.wipe_backup.empty()) {
    ImGui::TextWrapped("Backup of your save from just before the wipe: %s", Utf8(state.wipe_backup).c_str());
  }

  ImGui::SeparatorText("Game tweaks");
  for (size_t i = 0; i < tweaks::AllTweaks().size(); ++i) {
    const tweaks::Tweak& tweak = tweaks::AllTweaks()[i];
    if (i == tweaks::kHelenaOpera) {
      ImGui::SeparatorText("Extra costumes");
      ImGui::TextWrapped(
          "Added after each character's own costumes. Turning one on also unlocks that character's other costumes. "
          "Re-enter character select to see the change.");
    }
    bool on = rex::cvar::Query<bool>(tweak.cvar);
    ImGui::BeginDisabled(!host_.guest || extras_locked);  // a tweak is applied to guest memory
    if (ImGui::Checkbox(tweak.label, &on)) {
      SetTweak(i, on);
    }
    ImGui::EndDisabled();
  }

  ImGui::SeparatorText("DOAX2 swimsuits");
  ImGui::TextWrapped("%s", host_.suits_summary.empty() ? "DOAX2 swimsuits: none imported" : host_.suits_summary.c_str());
  for (const std::string& note : host_.suits_notes) ImGui::TextColored(kErrorColor, "%s", note.c_str());
  ImGui::TextWrapped("Imported suits come after each character's own and extra costumes.");
  bool full_bust = rex::cvar::Query<bool>("doa4_suits_full_bust");
  ImGui::BeginDisabled(extras_locked);  // its rescale writes guest memory
  if (ImGui::Checkbox(kBustLabel, &full_bust)) {
    SaveThenSet("doa4_suits_full_bust", full_bust ? "true" : "false", false);
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("%s", kBustNote);
  DrawSuitsImport();
}

// The import (Plan 3, Task 6): choose, import with progress and cancel, the result, and "Restart now" after one that
// changed the folder. The flow is the app's (it keeps running with the menu closed); this only draws it.
void MenuDialog::DrawSuitsImport() {
  if (!host_.suits_import) return;
  suits_import::Flow& flow = *host_.suits_import;
  const MenuHost::SuitsImportInfo info = host_.suits_import_info ? host_.suits_import_info() : MenuHost::SuitsImportInfo{};
  if (!info.note.empty() && !flow.restart_needed()) {
    ImGui::TextColored(kNoteColor, "%s", info.note.c_str());
  }
  suits_import::DrawHooks hooks;
  hooks.defer = GuardedDefer();
  hooks.start_game = [] {};
  hooks.restart = [this] { Restart(online_view::Control::kUpdateRestart); };  // the session lock applies as for the update's
  hooks.start_import = [this] { StartSuitsImport(); };
  hooks.import_label = info.loaded ? "Import again..." : "Import DOAX2 swimsuits...";
  std::string why;
  const bool choosing = flow.stage() == suits_import::Stage::kChoose;
  const bool enabled = SuitsImportEnabled(&why);
  ImGui::BeginDisabled(choosing && !enabled);
  suits_import::DrawSuitImport(flow, hooks);
  ImGui::EndDisabled();
  if (choosing && !enabled && !why.empty()) ImGui::TextDisabled("%s", why.c_str());
}

void MenuDialog::DrawWipeUnlocks(const std::optional<uint32_t>& profile, bool locked) {
  ImGui::Spacing();
  ImGui::BeginDisabled(!profile || locked);
  if (!wipe_armed_) {
    if (ImGui::Button("Wipe unlocks...", ImVec2(display::MenuPx(kExtrasButtonWidth, ImGui::GetFontSize()), 0.0f))) wipe_armed_ = true;
  } else {
    ImGui::TextColored(kErrorColor, "%s", kWipeQuestion);
    ImGui::TextWrapped(
        "Characters, costumes, voices, stages and Story clears go back to a new save's, including ones you earned, and "
        "the extra costumes turn off. A backup is made first. Album, settings and other records stay.");
    if (ImGui::Button("Yes, wipe unlocks") && profile) {
      wipe_armed_ = false;
      WipeNow(*profile);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) wipe_armed_ = false;
  }
  ImGui::EndDisabled();
}

void MenuDialog::WipeNow(uint32_t profile) {
  if (host_.refuse && host_.refuse("wipe unlocks", &status_)) return;
  // First: while a costume box is on its character is held unlocked at her maximum, which would undo the wipe for her.
  // A box that cannot go off (the settings file cannot be written) stops the wipe, with SetTweak's reason showing.
  for (const size_t i : {tweaks::kHelenaOpera, tweaks::kKokoroKimonos, tweaks::kChristieCostume}) {
    if (rex::cvar::Query<bool>(tweaks::AllTweaks()[i].cvar) && !SetTweak(i, false)) {
      REXLOG_ERROR("[doa4][EXTRAS] wipe unlocks: not done, {} stays on: {}", tweaks::AllTweaks()[i].cvar, status_);
      return;
    }
  }
  std::string error;
  if (!WipeWithBackup(*host_.guest, profile, host_.save_dir, host_.backup_root, BackupStamp(), host_.extras, &error)) {
    status_ = error;
    REXLOG_ERROR("[doa4][EXTRAS] wipe unlocks: {}", error);
    return;
  }
  status_.clear();
  REXLOG_INFO("[doa4][EXTRAS] wipe unlocks: {}", host_.extras->last_change);
}

void MenuDialog::TestAction(std::string_view action) {
  REXLOG_INFO("[doa4][EXTRAS] test action: {}", action);
  if (action == "wipe") {
    if (const std::optional<uint32_t> profile = host_.guest ? unlocks::FindProfile(*host_.guest) : std::nullopt) {
      WipeNow(*profile);
    }
  } else if (action == "restart") {
    Restart(online_view::Control::kUpdateRestart);
  } else if (action == "suits_import") {
    // What a click does: the source is set through the flow's pick effects (the app's return DOA4_SUITS_IMPORT_TEST without a
    // dialog), then the button's own work. Refused exactly when the button is disabled.
    const char* env = np::TestSwitch("DOA4_SUITS_IMPORT_TEST");  // starts an import that changes doax2_suits: guarded like the other test switches
    std::string why;
    if (!env || !env[0]) {
      REXLOG_INFO("[doa4][SUITS] test action suits_import: refused: DOA4_SUITS_IMPORT_TEST is not set");
    } else if (!host_.suits_import) {
      REXLOG_INFO("[doa4][SUITS] test action suits_import: refused: no import section");
    } else if (!SuitsImportEnabled(&why)) {
      REXLOG_INFO("[doa4][SUITS] test action suits_import: refused: {}", why);
    } else if (host_.suits_import->stage() != suits_import::Stage::kChoose) {
      REXLOG_INFO("[doa4][SUITS] test action suits_import: refused: the import screen is not at the choose step");
    } else {
      std::error_code ec;
      const bool folder = std::filesystem::is_directory(std::filesystem::path(env), ec);
      REXLOG_INFO("[doa4][SUITS] test action suits_import: source ({})", folder ? "folder" : "image");
      if (folder) {
        host_.suits_import->ChooseFolder();
      } else {
        host_.suits_import->ChooseImage();
      }
      StartSuitsImport();
    }
  } else if (action.rfind("language ", 0) == 0 && action.size() == 10 && action[9] >= '0' && action[9] <= '9') {
    PickLanguage(uint8_t(action[9] - '0'));  // as a pick in the dropdown: "language 2"
  } else if (action == "skip_intro 0" || action == "skip_intro 1") {
    SetSkipIntro(action.back() == '1');  // as a click on the Game tab's box
  } else if (action.rfind("tweak", 0) == 0 && action.size() == 8 && host_.guest) {  // tweak<index><0|1>, as "tweak 21"
    const size_t index = size_t(action[6] - '0');
    if (index < tweaks::AllTweaks().size()) SetTweak(index, action[7] == '1');
  }
}

void MenuDialog::DrawOnlineTab() {
  online_view::In in;
  online_view::View view;
  if (host_.online.state) {
    in = host_.online.state(true);
    view = online_view::Describe(in);
  }
  online_tab_.Draw(
      host_.online, in, view,
      [this](std::string_view name, std::string_view value, bool quote) { return SaveThenSet(name, value, quote); },
      [this] {
        // After this frame, with the menu closed first (Explorer in front stalls the UI-thread repaint).
        host_.defer([close = host_.close_menu, dir = host_.user_data / "netplay" / "sessions"] {
          if (close) close();
          std::error_code ec;
          std::filesystem::create_directories(dir, ec);
          photo::OpenInExplorer(dir);
        });
      },
      [this] { Restart(online_view::Control::kUpdateRestart); }, UnsavedUnlocksWarning(*host_.extras));
}

// The Language dropdown: the game's text language. The selected row is the saved choice when it is a text id, else what the
// game is showing now (a never-saved game shows its actual language).
void MenuDialog::DrawLanguageSettings() {
  const std::optional<uint8_t> saved = language::ValidText(rex::cvar::Query<int32_t>("doa4_language"));
  std::optional<uint8_t> shown = saved;
  if (!shown && host_.guest) shown = language::ValidText(language::Read(*host_.guest).text);
  const char* preview = "";
  for (const language::Choice& choice : language::kTextChoices) {
    if (shown && choice.id == *shown) preview = choice.label;
  }
  const bool locked = Locked(online_view::Control::kLanguage);
  ImGui::BeginDisabled(locked);
  ImGui::SetNextItemWidth(display::MenuPx(kLanguageComboWidth, ImGui::GetFontSize()));
  if (ImGui::BeginCombo("##language", preview)) {
    for (const language::Choice& choice : language::kTextChoices) {
      const bool selected = shown && choice.id == *shown;
      if (ImGui::Selectable(choice.label, selected) && !selected) PickLanguage(uint8_t(choice.id));
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  if (locked) {
    ImGui::SameLine();
    ImGui::TextColored(kNoteColor, "%s", online_view::kLockedText);
  }
}

// A pick in the dropdown (and the test action's): refused exactly when the dropdown is disabled; saves the choice, then asks the
// game thread to apply the text (language::RequestLiveText).
void MenuDialog::PickLanguage(uint8_t id) {
  if (online_view::Refuse(online_view::Control::kLanguage, SessionLocked(), "change language", &status_,
                          [](const std::string& line) { REXLOG_INFO("{}", line); })) {
    return;
  }
  if (!language::ValidText(id)) return;
  if (SaveThenSet("doa4_language", std::to_string(id), false)) language::RequestLiveText(id);
}

// Not locked in a session: a session skips every movie on both PCs whatever the box says (doa4_intro_skip.h).
void MenuDialog::DrawIntroSettings() {
  bool skip = rex::cvar::Query<bool>("doa4_skip_intro");
  if (ImGui::Checkbox(kSkipIntroLabel, &skip)) SetSkipIntro(skip);
  ImGui::TextDisabled("%s", kSkipIntroNote);
}

// The Discord status (doa4_discord.h): host-only, so not locked in a session; near one it is set in memory only, as the
// other presentation settings are (SetPresentationSetting).
void MenuDialog::DrawDiscordSettings() {
  bool on = rex::cvar::Query<bool>("doa4_discord_status");
  ImGui::BeginDisabled(!discord::Available());  // review2 M2: a build without the application id has no worker
  if (ImGui::Checkbox(kDiscordLabel, &on) && SetPresentationSetting("doa4_discord_status", on ? "true" : "false", false)) {
    discord::SetEnabled(on);
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("%s", DiscordNote());
}

void MenuDialog::SetSkipIntro(bool on) {
  SetPresentationSetting("doa4_skip_intro", on ? "true" : "false", false);
}

// R60. Near a session nothing is written to the config (the session restores it at its end): only in memory, as F7 does.
bool MenuDialog::SetPresentationSetting(std::string_view name, std::string_view value, bool quote) {
  if (!SessionLocked()) return SaveThenSet(name, value, quote);
  if (rex::cvar::SetFlagByName(name, value)) return true;
  status_ = fmt::format("Could not set {} to {}.", name, value);
  return false;
}

// The display rows of the Game tab. None is locked in a session (presentation only). Every Window call goes through
// host_.defer (the UI thread, after the frame), never from this draw.
void MenuDialog::DrawDisplayRows() {
  rex::ui::Window* window = host_.window;
  enum Mode { kWindowed, kBorderless, kExclusive };
  // The live state: IsFullscreenExclusiveActive is true only while DXGI exclusive is on, so a lost one shows as Borderless.
  const bool fullscreen = window->IsFullscreen();
  const Mode mode = !fullscreen ? kWindowed : window->IsFullscreenExclusiveActive() ? kExclusive : kBorderless;

  ImGui::PushID("display_mode");
  Mode want = mode;
  if (ImGui::RadioButton(kDisplayModes[0], mode == kWindowed)) want = kWindowed;
  if (ImGui::RadioButton(kDisplayModes[1], mode == kBorderless)) want = kBorderless;
  if (ImGui::RadioButton(kDisplayModes[2], mode == kExclusive)) want = kExclusive;
  // A click on Borderless while the wish is still exclusive (entry refused or lost) also clears the wish.
  if (want != mode || (want == kBorderless && window->IsFullscreenExclusive())) {
    const bool fs = want != kWindowed;
    const bool exclusive = want == kExclusive;
    if (exclusive) {
      // Fullscreen first, then the wish: the window is in fullscreen when the exclusive entry is posted.
      host_.defer([window] {
        window->SetFullscreen(true);
        window->SetFullscreenExclusive(true);
      });
    } else {
      // The wish first, so leaving exclusive (SetFullscreen(false) leaves it itself) or entering fullscreen is as asked.
      host_.defer([window, fs] {
        window->SetFullscreenExclusive(false);
        window->SetFullscreen(fs);
      });
    }
    SetPresentationSetting("fullscreen", fs ? "true" : "false", false);
    SetPresentationSetting("fullscreen_exclusive", exclusive ? "true" : "false", false);
  }
  ImGui::TextDisabled("%s", kDisplayModeNote);
  ImGui::PopID();

  ImGui::PushID("vsync");
  bool vsync = rex::cvar::Query<bool>("present_vsync");
  if (ImGui::Checkbox("VSync", &vsync)) SetPresentationSetting("present_vsync", vsync ? "true" : "false", false);
  ImGui::TextDisabled("%s", kVsyncNote);
  ImGui::PopID();

  ImGui::PushID("output_size");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(kOutputSizeLabel);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(display::MenuPx(kOutputSizeComboWidth, ImGui::GetFontSize()));
  if (mode == kBorderless) {
    ImGui::BeginDisabled();
    if (ImGui::BeginCombo("##size", "Desktop size")) ImGui::EndCombo();
    ImGui::EndDisabled();
  } else {
    display::OutputSizeList list;
    if (mode == kWindowed) {
      // The window's live client size is the current size (a drag or a maximize shows truthfully); the saved one only
      // before the window has a size.
      uint32_t w = window->GetActualPhysicalWidth(), h = window->GetActualPhysicalHeight();
      if (!w || !h) {
        unsigned sw = 0, sh = 0;
        const std::string saved = rex::cvar::Query<std::string>("window_client_size");
        if (std::sscanf(saved.c_str(), "%ux%u", &sw, &sh) == 2) w = sw, h = sh;
      }
      list = display::WindowedSizeList(window->ListWindowedClientSizes(), w, h);
    } else {
      list = display::ExclusiveSizeList(ExclusiveModes().modes,
                                        rex::cvar::Query<std::string>("fullscreen_exclusive_mode"));
    }
    const char* preview = list.selected >= 0 ? list.entries[size_t(list.selected)].label.c_str() : "";
    if (ImGui::BeginCombo("##size", preview)) {
      for (size_t i = 0; i < list.entries.size(); ++i) {
        const display::OutputSizeEntry& e = list.entries[i];
        ImGui::PushID(int(i));
        if (ImGui::Selectable(e.label.c_str(), int(i) == list.selected) && int(i) != list.selected) {
          if (mode == kWindowed) {
            SetPresentationSetting("window_client_size", e.value, true);
            const uint32_t w = e.width, h = e.height;
            host_.defer([window, w, h] { window->SetWindowedClientSize(w, h); });
          } else {
            // While exclusive the mode changes in place through the reapply call: SetFullscreenExclusive(false) then (true)
            // would coalesce into nothing.
            SetPresentationSetting("fullscreen_exclusive_mode", e.value, true);
            host_.defer([window] { window->ReapplyFullscreenExclusiveMode(); });
          }
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
  }
  ImGui::TextDisabled("%s", kOutputSizeNote);
  ImGui::PopID();
}

// The monitor's exclusive modes (Task 10 review I1). The listing builds a DXGI factory, so it runs from host_.defer, never
// inside the draw, and at most once per opening while exclusive is off (display::ExclusiveModesStep). The SDK merges the
// output's live mode in as the desktop entry: made while exclusive is off that is the desktop mode; made while it is on
// (only when the game started in exclusive and no list exists yet) it is the mode exclusive set, so it is dropped.
void MenuDialog::EnsureExclusiveModes() {
  const bool first_frame = exclusive_modes_first_frame_;
  exclusive_modes_first_frame_ = false;
  if (!host_.window) return;
  const display::ExclusiveModesCache& cache = ExclusiveModes();
  if (display::ExclusiveModesStep(first_frame, exclusive_modes_attempted_, cache.built, cache.from_active,
                                  host_.window->IsFullscreenExclusiveActive()) != display::ModesStep::kBuild) {
    return;
  }
  exclusive_modes_attempted_ = true;
  GuardedDefer()([this] {
    std::vector<rex::ui::display_mode::DisplayMode> modes;
    rex::ui::display_mode::DisplayMode desktop;
    if (!rex::ui::d3d12::D3D12Presenter::ListExclusiveFullscreenModes(host_.window->GetNativeWindowHandle(), modes,
                                                                     desktop)) {
      // No output for the window now: the cache stays as it was. Not tried again in this opening (attempted), only at
      // the next one.
      return;
    }
    const bool active_now = host_.window->IsFullscreenExclusiveActive();
    display::ExclusiveModesCache& cache = ExclusiveModes();
    if (active_now && cache.built) return;  // exclusive came on before this ran: keep the list made with it off
    cache.modes.clear();
    for (const auto& m : modes) {
      cache.modes.push_back({m.width, m.height, rex::ui::display_mode::RefreshHz(m), rex::ui::display_mode::ModeText(m)});
    }
    if (active_now) display::DropModeText(cache.modes, rex::ui::display_mode::ModeText(desktop));
    cache.built = true;
    cache.from_active = active_now;
  });
}

void MenuDialog::DrawGameTab() {
  // Each section has its own ID scope: the video and speakers parts both have a "Restart now" button, and both show when a
  // render scale and a speakers choice are pending (ImGui's ID-conflict highlight). Wrapping at the call keeps every return
  // path of a section balanced.
  ImGui::PushID("video");
  DrawVideoSettings();
  ImGui::PopID();
  ImGui::Spacing();
  ImGui::PushID("speakers");
  DrawSpeakerSettings();
  ImGui::PopID();
  ImGui::SeparatorText("Language");
  ImGui::PushID("language");
  DrawLanguageSettings();
  ImGui::PopID();
  ImGui::SeparatorText("Start-up");
  ImGui::PushID("intro");
  DrawIntroSettings();
  ImGui::PopID();
  ImGui::SeparatorText("Discord");
  ImGui::PushID("discord");
  DrawDiscordSettings();
  ImGui::PopID();
  const gamefiles::GameTab tab = gamefiles::DescribeGameTab(
      host_.game_files, host_.game_files_from_command_line,
      PathFromUtf8(rex::cvar::Query<std::string>("doa4_game_files")));
  ImGui::SeparatorText("Game files");
  ImGui::TextWrapped("%s", Utf8(tab.now).c_str());
  if (!tab.note.empty()) ImGui::TextWrapped("%s", tab.note.c_str());
  const bool change_locked = Locked(online_view::Control::kGameChange);
  ImGui::BeginDisabled(!tab.can_change || !host_.change_game_files || change_locked);
  if (ImGui::Button("Change...") &&
      !online_view::Refuse(online_view::Control::kGameChange, SessionLocked(), "change game files", &status_,
                           [](const std::string& line) { REXLOG_INFO("{}", line); })) {
    // After this frame, with the menu closed first (the setup screen takes
    // its place; the menu must not be destroyed while it is drawing).
    host_.defer([close = host_.close_menu, change = host_.change_game_files] {
      close();
      change();
    });
  }
  ImGui::EndDisabled();
  if (change_locked) {
    ImGui::SameLine();
    ImGui::TextColored(kNoteColor, "%s", online_view::kLockedText);
  }
  ImGui::SameLine();
  if (ImGui::Button("Open folder")) {
    host_.defer([close = host_.close_menu, dir = tab.now] {
      close();
      photo::OpenInExplorer(dir);
    });
  }
  ImGui::TextDisabled("%s", kGameFilesNote);
  ImGui::SeparatorText("Saves");
  ImGui::TextWrapped("%s", Utf8(host_.user_data).c_str());
}

void MenuDialog::DrawPhotoTab() {
  ImGui::SeparatorText("Save a photo");
  ImGui::TextWrapped(
      "Press F12 anywhere in the game. The photo is the game's own frame at the render scale, "
      "without this menu or any other overlay.");

  ImGui::SeparatorText("Format");
  ImGui::PushID("photo_format");
  const int format =
      photo::ChoiceIndex(photo::kFormats, rex::cvar::Query<std::string>("doa4_photo_format"));
  for (int i = 0; i < int(photo::kFormats.size()); ++i) {
    if (ImGui::RadioButton(photo::kFormats[i].label, format == i) && format != i) {
      SaveThenSet("doa4_photo_format", photo::kFormats[i].value, true);
    }
  }
  ImGui::PopID();
  ImGui::TextDisabled("%s", kPhotoMonitorNote);

  ImGui::SeparatorText("Folder");
  const std::string custom = rex::cvar::Query<std::string>("doa4_photo_dir");
  const std::filesystem::path folder =
      custom.empty() ? host_.photo_default_dir
                     : std::filesystem::path(std::u8string(custom.begin(), custom.end()));
  const std::u8string shown = folder.u8string();
  ImGui::TextWrapped("%s%s", reinterpret_cast<const char*>(shown.c_str()),
                     custom.empty() ? "  (default)" : "");
  if (ImGui::Button("Choose folder...")) {
    // After this frame: the folder dialog is modal and must not run inside
    // the menu's drawing.
    auto* win32 = dynamic_cast<rex::ui::Win32Window*>(host_.window);
    void* hwnd = win32 ? win32->hwnd() : nullptr;
    host_.defer([this, hwnd, folder] {
      if (std::optional<std::filesystem::path> chosen = photo::PickFolder(hwnd, folder)) {
        const std::u8string text = chosen->u8string();
        SaveThenSet("doa4_photo_dir", std::string(text.begin(), text.end()), true);
      }
    });
  }
  ImGui::SameLine();
  if (ImGui::Button("Open folder")) {
    // Close the menu first: while it is open the game is drawn from the UI
    // thread, and with Explorer in front the picture stopped until the menu
    // closed. After this frame, not inside the menu's drawing.
    host_.defer([close = host_.close_menu, folder] {
      if (close) close();
      photo::OpenInExplorer(folder);
    });
  }
  if (!custom.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Use default")) {
      SaveThenSet("doa4_photo_dir", "", true);
    }
  }

  ImGui::SeparatorText("In-game snapshots");
  bool auto_copy = rex::cvar::Query<bool>("doa4_photo_auto");
  if (ImGui::Checkbox(kPhotoCopyLabel, &auto_copy)) {
    SaveThenSet("doa4_photo_auto", auto_copy ? "true" : "false", false);
  }
  ImGui::TextDisabled("%s", kPhotoCopyNote);

  if (host_.photo_result) {
    const photo::Result result = host_.photo_result();
    if (result.serial) {
      ImGui::Spacing();
      if (result.error) {
        ImGui::TextColored(kErrorColor, "%s", result.text.c_str());
      } else {
        ImGui::TextColored(kDoneColor, "%s", result.text.c_str());
      }
    }
  }
}

bool MenuDialog::SetTweak(size_t index, bool on) {
  const tweaks::Tweak& tweak = tweaks::AllTweaks()[index];
  if (host_.refuse && host_.refuse(std::string("tweak ") + tweak.cvar, &status_)) return false;
  const char* value = on ? "true" : "false";
  // A costume box changes the save: the first backup comes before anything else.
  if (on && !tweak.additions.empty()) {
    std::string backup_error;
    if (!BackupBeforeCostumeBox(host_.save_dir, host_.backup_root, BackupStamp(), host_.extras, &backup_error)) {
      status_ = backup_error;
      REXLOG_ERROR("[doa4][EXTRAS] {}: {}", tweak.cvar, backup_error);
      return false;
    }
  }
  // Saved first: when the save fails the running game keeps its state.
  std::string error = SaveTomlKey(host_.config_path, tweak.cvar, value);
  if (!error.empty()) {
    status_ = "Could not save settings: " + error;
    return false;
  }
  status_.clear();
  if (on && !tweak.additions.empty()) CostumeBoxOn(host_.extras);
  rex::cvar::SetFlagByName(tweak.cvar, value);
  std::string apply_error = host_.patcher->Apply(*host_.guest, index, on, host_.protect);
  if (!apply_error.empty()) {
    status_ = fmt::format("{}: {}", tweak.label, apply_error);
  }
  REXLOG_INFO("[doa4][EXTRAS] {} = {}: {}", tweak.cvar, value,
              apply_error.empty() ? "applied" : apply_error);
  return true;
}

void MenuDialog::StartPadListen(size_t row, const rex::input::X_INPUT_GAMEPAD* pad) {
  listen_ = Listen::kPad;
  listen_row_ = row;
  // Inputs already held count only after they are released.
  pad_held_ = pad ? PressedControls(*pad) : 0;
}

void MenuDialog::PollPadListen(const rex::input::X_INPUT_GAMEPAD& pad) {
  const uint32_t pressed = PressedControls(pad);
  const uint32_t fresh = pressed & ~pad_held_;
  pad_held_ &= pressed;
  if (!fresh) return;
  size_t source = 0;
  while (!(fresh & (1u << source))) ++source;
  listen_ = Listen::kNone;
  AssignPad(listen_row_, source);
}

void MenuDialog::AssignPad(size_t target, size_t source) {
  PadRemap remap = CurrentRemap(slot_);
  rex::input::AssignPadControl(&remap, static_cast<PadControl>(target),
                               static_cast<PadControl>(source));
  SetAndSave(kPadRemapCvars[slot_], rex::input::FormatPadRemap(remap), true);
}

void MenuDialog::AssignKey(size_t row, const std::string& key_name) {
  const char* cvar = kKeyRows[row].cvar;
  const std::string previous = rex::cvar::Query<std::string>(cvar);
  if (previous == key_name) return;
  // A key already bound elsewhere swaps, so no key drives two controls.
  for (size_t other = 0; other < kKeyRows.size(); ++other) {
    if (other != row && rex::cvar::Query<std::string>(kKeyRows[other].cvar) == key_name) {
      SetAndSave(kKeyRows[other].cvar, previous, true);
    }
  }
  SetAndSave(cvar, key_name, true);
}

void MenuDialog::OnKeyDown(rex::ui::KeyEvent& e) {
  if (listen_ == Listen::kNone) return;
  if (e.virtual_key() == rex::ui::VirtualKey::kEscape) {
    listen_ = Listen::kNone;  // cancels the row; the menu stays open
    e.set_handled(true);
    return;
  }
  if (listen_ != Listen::kKey) return;
  std::string name = rex::ui::VirtualKeyToString(e.virtual_key());
  e.set_handled(true);
  if (name.empty()) return;  // a key the SDK cannot name: keep listening
  listen_ = Listen::kNone;
  AssignKey(listen_row_, name);
}

void MenuDialog::OnMouseDown(rex::ui::MouseEvent& e) {
  if (listen_ == Listen::kPad) {
    listen_ = Listen::kNone;  // a click elsewhere cancels; the click itself goes on to ImGui
    return;
  }
  if (listen_ != Listen::kKey) return;
  const char* name = nullptr;
  switch (e.button()) {
    case rex::ui::MouseEvent::Button::kLeft:
      name = "LMB";
      break;
    case rex::ui::MouseEvent::Button::kRight:
      name = "RMB";
      break;
    case rex::ui::MouseEvent::Button::kMiddle:
      name = "MMB";
      break;
    default:
      return;
  }
  e.set_handled(true);
  listen_ = Listen::kNone;
  AssignKey(listen_row_, name);
}

void MenuDialog::Restart(online_view::Control control) {
  if (online_view::Refuse(control, SessionLocked(), "restart", &status_,
                          [](const std::string& line) { REXLOG_INFO("{}", line); })) {
    return;
  }
  wchar_t exe[MAX_PATH];
  DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    status_ = "Restart failed: cannot find the game's executable.";
    return;
  }
  std::wstring command_line = GetCommandLineW();
  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  STARTUPINFOW parent{};  // the new instance starts the way this one did (a test's hidden game stays hidden)
  parent.cb = sizeof(parent);
  GetStartupInfoW(&parent);
  if (parent.dwFlags & STARTF_USESHOWWINDOW) {
    startup_info.dwFlags = STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = parent.wShowWindow;
  }
  PROCESS_INFORMATION process{};
  // The new instance waits for this one to exit (DeadOrAlive4App::OnPreSetup)
  // before it opens the save data and the shader cache.
  SetEnvironmentVariableW(L"DOA4_WAIT_PID", std::to_wstring(GetCurrentProcessId()).c_str());
  BOOL created = CreateProcessW(exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                nullptr, &startup_info, &process);
  DWORD error = GetLastError();
  SetEnvironmentVariableW(L"DOA4_WAIT_PID", nullptr);
  if (!created) {
    status_ = fmt::format("Restart failed (Windows error {}).", error);
    REXLOG_ERROR("[doa4][menu] restart: CreateProcessW failed, error {}", error);
    return;
  }
  REXLOG_INFO("[doa4][menu] restarting: new process {}", process.dwProcessId);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  rex::ui::Window* window = host_.window;
  host_.defer([window] { window->RequestClose(); });
}

bool MenuDialog::SaveThenSet(std::string_view name, std::string_view value, bool quote) {
  std::string error =
      SaveTomlKey(host_.config_path, name, quote ? TomlString(value) : std::string(value));
  if (!error.empty()) {
    status_ = "Could not save settings: " + error;
    return false;
  }
  status_.clear();
  if (!rex::cvar::SetFlagByName(name, value)) {
    status_ = fmt::format("Could not set {} to {}.", name, value);
    return false;
  }
  REXLOG_INFO("[doa4][UI] {} = {} saved", name, value);
  return true;
}

void MenuDialog::SetAndSave(std::string_view name, std::string_view value, bool quote) {
  if (!rex::cvar::SetFlagByName(name, value)) {
    status_ = fmt::format("Could not set {} to {}.", name, value);
    return;
  }
  std::string error =
      SaveTomlKey(host_.config_path, name, quote ? TomlString(value) : std::string(value));
  status_ = error.empty() ? std::string() : "Could not save settings: " + error;
}

}  // namespace doa4
