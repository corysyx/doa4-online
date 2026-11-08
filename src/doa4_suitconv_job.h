// Suit converter (Plan 2): the import job. Checks the player's DOAX2 disc and DOA4's game files, converts the 34 swimsuits
// on worker threads, writes them into <user data>/doax2_suits.new, and swaps that folder in as <user data>/doax2_suits only
// when it is complete and verified. Progress, cancel and the swap states are described below. Nothing in the game calls this
// yet; plain C++ plus Win32 (threads, files, CancelSynchronousIo), no SDK.
//
// Order of an import: the recipe tables are checked against the picks; space and write access (free bytes against
// BytesNeeded, then a probe file created and deleted in <user data>); CheckDisc; CheckGame; only then the swap-state recovery
// (below) and a fresh .new; conversion with a marker file first (<user data>/doax2_suits.new/.doa4-suitconv); the manifest,
// re-parsed, and the verification of .new; the swap. A refusal before conversion leaves <user data>'s listing unchanged.
//
// Conversion. Each worker reads one suit's inputs, converts it at Level::kM1 (no trace), checks the two files against the
// girl's budgets (a file over budget skips the suit, stage "budget"), hashes them, and writes both into .new at once, each as
// "<name>.tmp" then a rename; at most `workers` outputs are in memory. A suit that ConvertSuit refuses, or whose converter
// throws (std::bad_alloc included), is skipped with its stage and reason. A SHA-1 other than the reference build's keeps the
// suit, logs "suit <label>: output differs from the reference build (<sha1>)" (the .tpr's hash if that differs, else the
// .cat's) and lists it in differs_from_reference. An input that cannot be read, or whose bytes changed since the check
// (InputChanged), ends the whole import: kReadError, or kCancelled after Cancel; .new is deleted, the current import stays.
// The manifest lists the converted suits in picks order, whatever order they finish in.
//
// Cancel. Cancel() sets the token; the token is looked at between entries, 4 MiB hash chunks and suits. A read that is
// blocked inside the file system (a OneDrive placeholder being fetched, a sleeping disc) is not interrupted by the token, so
// the job also calls CancelSynchronousIo on every job thread that is inside a read (the checking thread, each worker), again
// every 10 ms until it has left it. A read that fails after Cancel gives kCancelled, never kReadError. Cancel never reaches
// the swap's renames once the first one has been made.
//
// Swap states (what is on disk under <user data>; cur = doax2_suits, new = doax2_suits.new, old = doax2_suits.old). A
// folder is "ours" when it holds the marker file .doa4-suitconv.
//   new without the marker                       not ours: never touched; Start fails (kFailed, the folder in detail)
//   new ours, no manifest.txt                    interrupted before the end: deleted
//   new ours, manifest fails VerifyImportFolder  damaged: deleted, cur kept
//   new ours and verified, cur present           cur is renamed to old, new to cur; old is then deleted if it is ours, else
//                                                kept and its path logged
//   new ours and verified, no cur, old present   new is renamed to cur; the old rule
//   cur and old, no new                          the old rule
// Before cur becomes old, an existing old that is ours is deleted; one that is not (a Python import) is renamed to the first
// free doax2_suits.old-<N> (N from 2), logged, never deleted. A rename that fails with a sharing violation or access denied
// means files are open: the swap is "blocked", new is left complete and the outcome is kSwapPending; if the first rename was
// made and the second failed, the first is undone. Start deletes a leftover verified new that is still blocked and replaces
// it by the new import.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "doa4_suitconv.h"
#include "doa4_suitconv_inputs.h"
#include "doa4_suitconv_recipe.h"

namespace doa4::suitconv {

struct ImportRequest {
  std::filesystem::path doax2, game_root, user_data;
  int workers = 0;  // 0: clamp(hardware_concurrency - 1, 1, 4)
  std::function<void(std::string_view)> log;  // may be empty; called from worker threads (calls are serialised)
};

enum class Phase { kCheckingSpace, kCheckingDisc, kCheckingGame, kConverting, kWriting, kSwapping, kDone };

struct Progress {
  Phase phase = Phase::kCheckingSpace;
  int suits_done = 0, suits_total = 0;  // suits converted and written, or skipped
  uint64_t bytes_done = 0, bytes_total = 0;  // input bytes hashed by the two checks, and the checks' total
  double fraction = 0;  // never decreases: checks 15%, conversion 80%, writing and the swap 5%
};

enum class Outcome {
  kRunning, kImported, kImportedWithSkips, kNothingImported, kCancelled, kWrongDisc, kGameFilesDiffer, kNoSpace,
  kNoWriteAccess, kReadError, kSwapPending, kFailed
};

struct SkippedSuit {
  std::string label, stage, why;  // "vks_ch_d c0", the converter's stage (or "budget"), the reason
};

struct ImportResult {
  Outcome outcome = Outcome::kRunning;
  std::vector<SkippedSuit> skipped;                  // picks order
  std::vector<std::string> differs_from_reference;  // labels, picks order
  std::vector<std::string> mismatched_inputs;       // entry labels from the failed check
  uint64_t bytes_needed = 0, bytes_free = 0;
  std::string detail;
};

// What a test can replace. Every member is defaulted, and the default is production: an empty function means the
// production behaviour, the tables default to the recipe tables.
struct Hooks {
  // One suit's inputs. Default: ReadSuitInputs.
  std::function<SuitInputs(InputSet&, const SuitRecipe&, const GirlRecipe&)> read_inputs;
  // The converter. Default: ConvertSuit at Level::kM1, no trace.
  std::function<std::optional<SuitOutput>(const SuitRecipe&, const GirlRecipe&, const SuitInputs&, const CancelToken&,
                                          ConvertFailure*)>
      convert;
  // The tables: the 34 suits in picks order (reference sizes and SHA-1s), the girls (budgets), the entries the two
  // checks verify.
  std::span<const SuitRecipe> suits = Suits();
  std::span<const GirlRecipe> girls = Girls();
  std::span<const EntryRef> disc = DiscEntries();
  std::span<const EntryRef> game = GameEntries();
  // The InputSet over the request's disc and game folder. Default: InputSet::Open.
  std::function<std::unique_ptr<InputSet>(const ImportRequest&, std::span<const EntryRef>, std::span<const EntryRef>,
                                          std::string*)>
      open_inputs;
  // Free bytes where `path` (the user data folder) lives. Default: GetDiskFreeSpaceEx.
  std::function<uint64_t(const std::filesystem::path&)> free_bytes;
  // Called on the job's thread when a phase begins, before its work.
  std::function<void(Phase)> on_phase;
  // The Nth file write of the import (1-based; the marker is the first, the manifest the last) fails; 0 = none.
  int write_fault_at = 0;
  // The Nth file write writes half of its bytes and the job ends at once, leaving the disk as a crash would; 0 = none.
  int crash_at_write = 0;
};

class ImportJob {
 public:
  // Returns at once; the work runs on its own thread.
  static std::unique_ptr<ImportJob> Start(ImportRequest request);
  static std::unique_ptr<ImportJob> StartForTest(ImportRequest request, Hooks hooks);
  ~ImportJob();  // cancels and joins
  ImportJob(const ImportJob&) = delete;
  ImportJob& operator=(const ImportJob&) = delete;

  Progress progress() const;
  bool done() const;
  void Cancel();
  ImportResult result() const;  // valid once done()

  struct Impl;

 private:
  explicit ImportJob(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// The free space an import needs: the 68 reference sizes, the reference manifest's length, and 64 MiB.
uint64_t BytesNeeded();
uint64_t BytesNeeded(std::span<const SuitRecipe> suits);

// Finishes or clears what an earlier import left under <user data> (the table above). "" when there was nothing to do or the
// swap is done, otherwise why not (blocked by open files, a folder that is not ours, a failed rename). log may be empty.
std::string CompletePendingSwap(const std::filesystem::path& user_data,
                                const std::function<void(std::string_view)>& log = {});

// "" when `folder` is a good import: manifest.txt parses, every line is a pick, every file exists with the listed size and
// SHA-1. Otherwise the first reason.
std::string VerifyImportFolder(const std::filesystem::path& folder);

}  // namespace doa4::suitconv
