// The DOAX2 swimsuit import's screen logic, without ImGui: choose the player's DOAX2 disc image or extracted folder, run the
// import with progress and cancel, show the result, and say whether the game can go on by itself. Every outside effect (file
// dialogs, the worker thread, the import job, the pending flag, the log) is injected, so the host test drives it synchronously.
// Task 4 (start-up) uses DecideStartup; Task 5 (first-run screen, start-up offer) and Task 6 (Esc menu) draw a Flow.
// Includes doa4_suitconv_job.h for its types only: this file links none of the converter.
//
// Threading (as setup::Flow): every Flow method and every `post`ed callback runs on the UI thread. `run_async` runs one
// function, the waiter, on a worker. The waiter never holds the Flow: it holds the Runner (shared) and a small shared block
// (the `post` function and a weak pointer to the Flow's guard token), polls Runner::done() every 50 ms and `post`s each change
// of progress and the final result. Every posted callback checks the guard token first; ~Flow drops the token, so a callback
// that runs after the Flow is gone does nothing, even when the owner forgot to drop pending posts. effects.log is called on the
// UI thread only (the job's own log lines travel through `post` as well). ~Flow calls Runner::Cancel() once on a running
// import and does not join: the owner joins the worker it started in run_async (as SetupDialog does) after the Flow is gone.
//
// Pending flag. Start sets it (save_pending(true)) before the job starts; the result clears it (save_pending(false)) for
// kImported, kImportedWithSkips and kSwapPending only. Any other ending leaves it set, so the import is offered again at the
// next start (DecideStartup).

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "doa4_suitconv_job.h"

namespace doa4::suits_import {

// ---- Fixed copy (tests pin each text) ---------------------------------------------------------------------------------
inline constexpr std::string_view kSectionTitle =
    "DOAX2 swimsuits (optional): needs your own USA copy of Dead or Alive Xtreme 2";
inline constexpr std::string_view kUnlockNote =
    "Importing the swimsuits also unlocks all the DOA4 costumes for the nine girls who have them. Your save isn't changed.";
inline constexpr std::string_view kNotChosen = "Not chosen: the game runs with its normal costumes.";
inline constexpr std::string_view kChosenPrefix = "Swimsuits from: ";
inline constexpr std::string_view kOfferTitle = "The DOAX2 swimsuit import did not finish last time.";
inline constexpr std::string_view kPhaseCheckingSpace = "Checking free space";
inline constexpr std::string_view kPhaseCheckingDisc = "Checking the DOAX2 disc";
inline constexpr std::string_view kPhaseCheckingGame = "Checking the DOA4 game files";
inline constexpr std::string_view kPhaseWriting = "Writing the swimsuits";
inline constexpr std::string_view kPhaseSwapping = "Putting the swimsuits in place";
inline constexpr std::string_view kPhaseDone = "Done";
inline constexpr std::string_view kCancelledLine = "Import cancelled. Nothing was changed.";
inline constexpr std::string_view kCancelledOfferedAgain = "The import is offered again at the next start.";
inline constexpr std::string_view kWrongDiscLine =
    "Needs your own USA copy of DOAX2: this disc's files are not the ones the swimsuits are built from. Nothing was changed.";
inline constexpr std::string_view kGameFilesDifferLine =
    "Your DOA4 game files are not the Rev 1 files the swimsuits are built from. Nothing was changed.";
inline constexpr std::string_view kSwapPendingLine = "The swimsuits are ready. They are put in place when the game next starts.";
inline constexpr std::string_view kRestartLine = "Restart the game to use them.";
inline constexpr std::string_view kReimportLine =
    "These swimsuits were built by an older importer. Import them again to match players who have the new ones.";

// ---- The import, as the flow sees it -----------------------------------------------------------------------------------
// What suitconv::ImportJob offers (ImportJob implements the same four calls; Task 4 wraps it). All four are safe from any thread.
struct Runner {
  virtual ~Runner() = default;
  virtual suitconv::Progress progress() const = 0;
  virtual bool done() const = 0;
  virtual void Cancel() = 0;
  virtual suitconv::ImportResult result() const = 0;  // valid once done()
};

enum class Mode { kFirstRun, kStartOffer, kMenu };
enum class Stage { kChoose, kRunning, kResult };

struct Effects {
  // File dialogs (UI thread); the argument is the folder to start in (empty: the dialog's own default); nullopt = cancelled.
  std::function<std::optional<std::filesystem::path>(const std::filesystem::path&)> pick_iso, pick_folder;
  // Starts the import and returns at once. Called on the UI thread.
  std::function<std::unique_ptr<Runner>(suitconv::ImportRequest)> start;
  std::function<void(std::function<void()>)> run_async;  // runs the waiter on a worker
  std::function<void(std::function<void()>)> post;       // back to the UI thread
  // Sets (true) or clears (false) the "import pending" flag in the settings; returns an error text, "" when saved.
  std::function<std::string(bool)> save_pending;
  std::function<void(const std::string&)> log;  // UI thread only
};

struct FlowBridge;  // the waiter's and the posted callbacks' way into a Flow (flow.cpp)
struct Token;       // the guard a posted callback checks

class Flow {
 public:
  Flow(Mode mode, Effects effects);
  ~Flow();  // cancels a running import once; does not join
  Flow(const Flow&) = delete;
  Flow& operator=(const Flow&) = delete;

  // Each is refused (nothing happens) unless the stage is kChoose. The dialog starts in the folder of
  // the last pick (the pick's folder for a folder, the file's folder for an image); empty before the first pick.
  void ChooseImage();
  void ChooseFolder();
  void Clear();  // forget the chosen source (kChoose only)
  // Starts the import from kChoose or, to try again, from kResult. False (logged, nothing changed) with no source or while
  // running. Creates `user_data` first; when that fails the flow goes to kResult with kNoWriteAccess and returns true.
  bool Start(const std::filesystem::path& game_root, const std::filesystem::path& user_data, int workers);
  void Cancel();  // while running: asks the import to stop, once
  void Back();    // from kResult to kChoose (the source stays chosen); refused while running

  Stage stage() const { return stage_; }
  Mode mode() const { return mode_; }
  const std::filesystem::path& source() const { return source_; }
  suitconv::Progress progress() const { return progress_; }  // the latest posted snapshot
  std::optional<suitconv::ImportResult> result() const { return result_; }
  // The text for the result (kResult): outcome lines, skips, the differs line, the restart line (kMenu).
  std::vector<std::string> ResultLines() const;
  bool busy() const { return stage_ == Stage::kRunning; }
  bool restart_needed() const;  // kMenu, after kImported, kImportedWithSkips or kSwapPending
  bool auto_start() const;      // kFirstRun / kStartOffer, after kImported with nothing differing: start the game
  bool can_retry() const;       // after kWrongDisc, kReadError, kNoSpace, kNoWriteAccess, kCancelled or kFailed

 private:
  friend struct FlowBridge;
  void Log(const std::string& line) const;
  void OnProgress(const suitconv::Progress& progress);
  void OnResult(suitconv::ImportResult result, const suitconv::Progress& progress);

  Mode mode_;
  Effects fx_;
  Stage stage_ = Stage::kChoose;
  std::filesystem::path source_, last_dir_, user_data_;
  suitconv::Progress progress_;
  std::optional<suitconv::Phase> logged_phase_;
  std::optional<suitconv::ImportResult> result_;
  std::shared_ptr<Runner> runner_;
  bool cancel_sent_ = false;
  std::shared_ptr<Token> token_;
};

// ---- Free functions ----------------------------------------------------------------------------------------------------
std::string PhaseText(const suitconv::Progress& progress);  // "Building swimsuit 3 of 34" (never total + 1)
std::string FormatMB(uint64_t bytes);                       // ceil(bytes / 2^20), "<n> MB"
std::string ReimportNote(std::string_view converter);       // kReimportLine unless the word is empty or kConverterWord
std::string_view OutcomeName(suitconv::Outcome outcome);    // the enumerator's spelling, "kImported"

struct StartupSuits {
  bool complete_swap, offer;
};
// complete_swap = !session; offer = !session && pending && !first_run_shown.
StartupSuits DecideStartup(bool session, bool pending, bool first_run_shown);

}  // namespace doa4::suits_import
