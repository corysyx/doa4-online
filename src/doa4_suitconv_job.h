// Suit converter (Plan 2): the import job. Checks the player's DOAX2 disc and DOA4's game files, converts the 34 swimsuits
// on worker threads, writes them into <user data>/doax2_suits.new, and swaps that folder in as <user data>/doax2_suits only
// when it is complete and verified. Progress, cancel and the swap states are described below. Nothing in the game calls this
// yet; plain C++ plus Win32 (threads, files, CancelSynchronousIo), no SDK.
//
// Order of an import: the recipe tables are checked against the picks; space and write access (free bytes, plus the
// bytes of a leftover .new that the recovery will delete, against BytesNeeded; then a probe file created and deleted in
// <user data>); CheckDisc; CheckGame; only then the swap-state recovery
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
// folder is "ours" when it holds the marker file .doa4-suitconv, or when it has lost the marker and is empty or holds only the
// marker's temporary file (.doa4-suitconv.tmp): the marker is written first and deleted last, so those are the only marker-less
// states this job's writer and deleter leave, and an owner-less new or old is deleted like any folder of ours.
//   new without the marker, not empty            not ours: never touched; Start fails (kFailed, the folder in detail)
//   new ours, no manifest.txt                    interrupted before the end: deleted
//   new ours, manifest fails VerifyImportFolder  damaged: deleted, cur kept
//   new ours and verified, cur present           cur is renamed to old, new to cur; old is then deleted if it is ours, else
//                                                kept and its path logged
//   new ours and verified, no cur, old present   new is renamed to cur; the old rule
//   cur and old, no new                          the old rule
// Before cur becomes old, an existing old that is ours is deleted; one that is not (a Python import) is renamed to the first
// free doax2_suits.old-<N> (N from 2), logged, never deleted. A rename that fails with a sharing violation or access denied
// means files are open: the swap is "blocked", new is left complete and the outcome is kSwapPending; if the first rename was
// made and the second failed, the first is undone. A blocked rename is retried first: 10 tries over about a second, Cancel
// looked at between tries while nothing has been moved yet (then the import is abandoned, kCancelled). Start deletes a
// leftover verified new that is still blocked and replaces it by the new import.
//
// For the caller (Plan 3):
//   - Call CompletePendingSwap(<user data>) at startup, before suits::LoadImport opens the import: LoadImport keeps the current
//     import's files open without write sharing for the whole session, and that is what blocks a swap. Mid-session (the Esc
//     menu) every import that finds at least one suit loaded ends kSwapPending; the swap is finished at the next start.
//   - CompletePendingSwap is synchronous and cannot be cancelled: with a verified .new waiting it hashes about 314 MB on the
//     calling thread. Call it before the game opens the import, off the frame loop, never on a frame.
//   - <user data> is the folder that holds doax2_suits. Never run CompletePendingSwap, or a second ImportJob, on a user data
//     folder while an ImportJob runs there: both enter the swap-state recovery, which would delete the running job's .new under
//     its writer. In one process this is enforced: a second Start or StartForTest on the same folder (canonical path, case
//     folded) while a job runs there returns a job that is already done with kFailed ("an import is already running"), and
//     CompletePendingSwap returns "an import is running ..."; both touch nothing. A folder is held from Start until the job's
//     thread is done (done() true), and CompletePendingSwap holds it while it works. A second PROCESS is detected too: the
//     hold includes a named kernel event per folder ("Local\doa4-suitconv-" + SHA-1 of the folded canonical path), so the same
//     answers come back when another game process on the same user data runs an import or a swap; the event's existence is
//     the claim (the OS drops it when that process dies) and nothing is written to the user data folder. Only processes of
//     one Windows session (the "Local\" namespace) see each other.
//   - The log callback is called from worker threads (calls are serialised) and may throw: every exception from it is
//     swallowed, so a failing logger cannot end the game or change an import's outcome.
//   - The job may be destroyed from inside the log callback (or any hook): the destructor sees that it runs on the job's own
//     thread and detaches instead of joining. The import is cancelled and finishes in the background, the folder is held until
//     then, and nothing of the destroyed job is reachable any more. Destroying it from any other thread cancels and joins.
//   - progress(), done(), Cancel() and result() are safe from any thread; Start returns at once.

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
  std::function<void(std::string_view)> log;  // may be empty; called from worker threads (calls are serialised); may throw
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
  // Called after each file write that completed (its rename done), with the write's number; from any job thread.
  std::function<void(int)> on_write;
  // Failure seams: the Nth worker thread (1-based) cannot be started; the job's own thread cannot be started; the Nth thread
  // registration for Cancel (the job's thread is the first, the workers follow) fails as DuplicateHandle failing would. 0 = none.
  int fail_worker_start_at = 0;
  bool fail_job_thread_start = false;
  int fail_register_at = 0;
  // Called between the swap's two renames (the current import is .old, .new not yet the current import), on the job's thread.
  std::function<void()> between_renames;
  // The Nth file write writes half of its bytes and the job ends at once, leaving the disk as a crash would; 0 = none.
  int crash_at_write = 0;
};

class ImportJob {
 public:
  // Returns at once; the work runs on its own thread.
  static std::unique_ptr<ImportJob> Start(ImportRequest request);
  static std::unique_ptr<ImportJob> StartForTest(ImportRequest request, Hooks hooks);
  ~ImportJob();  // cancels and joins (from inside the job's own callbacks: cancels and detaches)
  ImportJob(const ImportJob&) = delete;
  ImportJob& operator=(const ImportJob&) = delete;

  Progress progress() const;
  bool done() const;
  void Cancel();
  ImportResult result() const;  // valid once done()

  struct Impl;

 private:
  explicit ImportJob(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;  // shared with the job's threads, which outlive the object when it is destroyed from a callback
};

// The free space an import needs: the 68 reference sizes, the reference manifest's length, and 64 MiB.
uint64_t BytesNeeded();
uint64_t BytesNeeded(std::span<const SuitRecipe> suits);

// Finishes or clears what an earlier import left under <user data> (the table above). "" when there was nothing to do or the
// swap is done, otherwise why not (an import is running there, blocked by open files, a folder that is not ours, a failed
// rename). log may be empty; it is called on the calling thread. See "For the caller" above.
std::string CompletePendingSwap(const std::filesystem::path& user_data,
                                const std::function<void(std::string_view)>& log = {});

// Test seam: the name of the kernel object that stands for a user data folder across processes (same name for two spellings of one folder).
std::wstring FolderClaimNameForTest(const std::filesystem::path& user_data);

// "" when `folder` is a good import: manifest.txt parses, every line is a pick, every file exists with the listed size and
// SHA-1. Otherwise the first reason.
std::string VerifyImportFolder(const std::filesystem::path& folder);

}  // namespace doa4::suitconv
