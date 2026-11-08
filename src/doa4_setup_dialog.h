// The first-run setup screen (ImGui, in the game window): shown before the
// game starts when no game files are configured, and from the Esc menu's Game
// tab to choose another location. Draws a doa4::setup::Flow; owns its worker
// thread.
//
// Test switch DOA4_SETUP_TEST (no file dialogs are opened):
//   folder=<path>               choose that extracted folder
//   iso=<path>|target=<path>    choose that image, extract it to <path>
// and, beside either form or alone, the DOAX2 swimsuit part:
//   doax2=<path>                that folder (a directory) or image (anything else) is the swimsuit source;
//                               in the start-up offer the import starts at once
//   doax2=skip                  no swimsuits: the first-run section is left alone, the offer's
//                               "Start without swimsuits" is pressed
// A scripted import's result screen is left with "Start the game". Each step is logged with [doa4][SETUP].

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <rex/ui/imgui_dialog.h>

#include "doa4_setup_flow.h"
#include "doa4_suits_import_flow.h"

namespace doa4 {

struct SetupHost {
  // kSuitsOffer: the game files are known; draws only the DOAX2 swimsuit section (start-up offer after an unfinished import).
  enum class Mode { kFirstRun, kChange, kSuitsOffer };
  Mode mode = Mode::kFirstRun;
  // The swimsuit import's flow, owned by the app (never deleted here; the app destroys it, then joins its worker, when the
  // start-up section ends). Null: no swimsuit section (kChange).
  suits_import::Flow* suits = nullptr;
  std::filesystem::path user_data;  // where the import writes (Flow::Start)
  // "Start the game" / an automatic start after the import: end the section and resume the start. UI thread, deferred.
  std::function<void()> suits_finish;
  // "Start without swimsuits": clear the pending flag, then as suits_finish. UI thread, deferred.
  std::function<void()> suits_skip;
  void* hwnd = nullptr;   // owner of the file dialogs
  std::string reason;     // UTF-8; shown above the buttons when not empty
  std::filesystem::path current;  // kChange: the folder in use now
  // Runs `fn` on the UI thread after the current frame (file dialogs and
  // closing must not happen while the dialog is drawing).
  std::function<void(std::function<void()> fn)> defer;
  // Runs `fn` on the UI thread; callable from any thread.
  std::function<void(std::function<void()> fn)> post;
  // The chosen folder (checked or just extracted and verified). UI thread.
  // Returns "" or why it could not be saved (shown in kChange).
  std::function<std::string(const std::filesystem::path& folder)> done;
  // Quit (kFirstRun) or close the dialog (kChange). UI thread, deferred.
  std::function<void()> close;
  std::function<void(const std::string& line)> log;
};

class SetupDialog : public rex::ui::ImGuiDialog {
 public:
  SetupDialog(rex::ui::ImGuiDrawer* drawer, SetupHost host);
  // The scripted steps and logs that do not need a painted frame (OnDraw calls it every frame; the app's test timer calls it
  // while DOA4_SETUP_TEST is set, since a minimized window never paints). UI thread; safe to call twice in a frame.
  void TestTick();
  ~SetupDialog();  // cancels a running copy and joins the worker (held as SetupDialog)

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  setup::Effects MakeEffects();
  void RunTestScript();
  void LogStage();
  void DrawSuitsScreen(bool offer_title);  // kSuitsOffer, and kFirstRun after the DOA4 part when the section comes back
  std::function<void(std::function<void()>)> GuardedDefer();  // host_.defer that skips a call queued before ~SetupDialog
  void StartImport();                     // Flow::Start over the known game folder
  void FinishSuits(bool skip);             // once: end the section and resume the start

  SetupHost host_;
  // Guards posts: the destructor resets alive_ on the UI thread while the
  // worker may be posting, so the worker only ever copies alive_weak_, which
  // is written once here and never again.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  const std::weak_ptr<bool> alive_weak_ = alive_;
  setup::Flow flow_;
  std::thread worker_;
  setup::TestScript test_;
  bool test_started_ = false, test_extract_started_ = false;
  bool test_doax2_chosen_ = false, test_doax2_done_ = false;
  std::filesystem::path game_folder_;  // the folder the DOA4 part ended with (kFirstRun), or host_.current (kSuitsOffer)
  bool suits_seen_ = false;            // the import has left kChoose after the DOA4 part: the section stays on screen (not "Starting...")
  bool suits_result_logged_ = false, finish_requested_ = false;
  std::string save_error_;  // what `done` returned
  setup::Stage logged_stage_ = setup::Stage::kChoose;
  std::string logged_message_;
};

}  // namespace doa4
