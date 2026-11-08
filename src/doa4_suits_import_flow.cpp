// The DOAX2 swimsuit import's screen logic. See doa4_suits_import_flow.h.

#include "doa4_suits_import_flow.h"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <thread>

#include "doa4_utf8.h"

namespace doa4::suits_import {

namespace fs = std::filesystem;
using suitconv::ImportResult;
using suitconv::Outcome;
using suitconv::Phase;
using suitconv::Progress;

// What a posted callback checks before it touches the Flow: ~Flow clears `flow` (UI thread) and drops the owning pointer.
struct Token {
  Flow* flow = nullptr;
};

namespace {

constexpr std::chrono::milliseconds kPollInterval{50};

// What the waiter holds instead of the Flow.
struct Shared {
  std::function<void(std::function<void()>)> post;
  std::weak_ptr<Token> token;
};

bool SameProgress(const Progress& a, const Progress& b) {
  return a.phase == b.phase && a.suits_done == b.suits_done && a.suits_total == b.suits_total &&
         a.bytes_done == b.bytes_done && a.bytes_total == b.bytes_total && a.fraction == b.fraction;
}

bool ClearsPending(Outcome o) {
  return o == Outcome::kImported || o == Outcome::kImportedWithSkips || o == Outcome::kSwapPending;
}

std::string Count(int n) { return std::to_string(n); }

}  // namespace

// The only door from a worker thread into a Flow: a callback posted to the UI thread that checks the guard token first.
struct FlowBridge {
  static void Post(const std::shared_ptr<Shared>& shared, std::function<void(Flow&)> fn) {
    if (!shared->post) return;
    shared->post([weak = shared->token, fn = std::move(fn)] {
      const std::shared_ptr<Token> token = weak.lock();
      if (!token || !token->flow) return;  // the Flow is gone
      fn(*token->flow);
    });
  }
  static void Log(const std::shared_ptr<Shared>& shared, std::string line) {
    Post(shared, [line = std::move(line)](Flow& flow) { flow.Log(line); });
  }
  static void OnProgress(Flow& flow, const Progress& p) { flow.OnProgress(p); }
  static void OnResult(Flow& flow, ImportResult r, const Progress& p) { flow.OnResult(std::move(r), p); }

  // The function given to run_async. Holds the runner and the shared block, never the Flow.
  static void Wait(const std::shared_ptr<Runner>& runner, const std::shared_ptr<Shared>& shared) {
    std::optional<Progress> last;
    for (;;) {
      const Progress p = runner->progress();
      if (!last || !SameProgress(*last, p)) {
        last = p;
        Post(shared, [p](Flow& flow) { OnProgress(flow, p); });
      }
      if (runner->done()) break;
      std::this_thread::sleep_for(kPollInterval);
    }
    ImportResult result = runner->result();
    const Progress final_progress = runner->progress();
    Post(shared, [result = std::move(result), final_progress](Flow& flow) mutable {
      OnResult(flow, std::move(result), final_progress);
    });
  }
};

// ---- Free functions ----------------------------------------------------------------------------------------------------
std::string PhaseText(const Progress& progress) {
  switch (progress.phase) {
    case Phase::kCheckingSpace:
      return std::string(kPhaseCheckingSpace);
    case Phase::kCheckingDisc:
      return std::string(kPhaseCheckingDisc);
    case Phase::kCheckingGame:
      return std::string(kPhaseCheckingGame);
    case Phase::kConverting: {
      const int shown = std::min(progress.suits_done + 1, progress.suits_total);
      return "Building swimsuit " + Count(shown) + " of " + Count(progress.suits_total);
    }
    case Phase::kWriting:
      return std::string(kPhaseWriting);
    case Phase::kSwapping:
      return std::string(kPhaseSwapping);
    case Phase::kDone:
      return std::string(kPhaseDone);
  }
  return {};
}

std::string FormatMB(uint64_t bytes) {
  const uint64_t mb = (bytes >> 20) + ((bytes & ((1ull << 20) - 1)) ? 1 : 0);
  return std::to_string(mb) + " MB";
}

std::string ReimportNote(std::string_view converter) {
  if (converter.empty() || converter == suitconv::kConverterWord) return {};
  return std::string(kReimportLine);
}

std::string_view OutcomeName(Outcome outcome) {
  switch (outcome) {
    case Outcome::kRunning:
      return "kRunning";
    case Outcome::kImported:
      return "kImported";
    case Outcome::kImportedWithSkips:
      return "kImportedWithSkips";
    case Outcome::kNothingImported:
      return "kNothingImported";
    case Outcome::kCancelled:
      return "kCancelled";
    case Outcome::kWrongDisc:
      return "kWrongDisc";
    case Outcome::kGameFilesDiffer:
      return "kGameFilesDiffer";
    case Outcome::kNoSpace:
      return "kNoSpace";
    case Outcome::kNoWriteAccess:
      return "kNoWriteAccess";
    case Outcome::kReadError:
      return "kReadError";
    case Outcome::kSwapPending:
      return "kSwapPending";
    case Outcome::kFailed:
      return "kFailed";
  }
  return "kUnknown";
}

StartupSuits DecideStartup(bool session, bool pending, bool first_run_shown) {
  return {!session, !session && pending && !first_run_shown};
}

// ---- Flow --------------------------------------------------------------------------------------------------------------
Flow::Flow(Mode mode, Effects effects) : mode_(mode), fx_(std::move(effects)), token_(std::make_shared<Token>()) {
  token_->flow = this;
}

Flow::~Flow() {
  if (stage_ == Stage::kRunning && runner_ && !cancel_sent_) {
    cancel_sent_ = true;
    runner_->Cancel();
  }
  token_->flow = nullptr;
  token_.reset();  // a posted callback that runs from now on finds no token
}

void Flow::Log(const std::string& line) const {
  if (fx_.log) fx_.log(line);
}

void Flow::ChooseImage() {
  if (stage_ != Stage::kChoose || !fx_.pick_iso) return;
  const auto picked = fx_.pick_iso(last_dir_);
  if (!picked) return;
  source_ = *picked;
  last_dir_ = picked->parent_path();
}

void Flow::ChooseFolder() {
  if (stage_ != Stage::kChoose || !fx_.pick_folder) return;
  const auto picked = fx_.pick_folder(last_dir_);
  if (!picked) return;
  source_ = *picked;
  last_dir_ = *picked;
}

void Flow::Clear() {
  if (stage_ != Stage::kChoose) return;
  source_.clear();
}

void Flow::Back() {
  if (stage_ != Stage::kResult) return;
  stage_ = Stage::kChoose;
  result_.reset();
  progress_ = {};
  logged_phase_.reset();
}

bool Flow::Start(const fs::path& game_root, const fs::path& user_data, int workers) {
  if (source_.empty()) {
    Log("import: start refused: no DOAX2 source chosen");
    return false;
  }
  if (stage_ == Stage::kRunning) {
    Log("import: start refused: an import is running");
    return false;
  }
  result_.reset();
  progress_ = {};
  logged_phase_.reset();
  cancel_sent_ = false;
  user_data_ = user_data;
  Log("import: start " + Utf8(source_) + " -> " + Utf8(user_data) + " workers=" + Count(workers));

  std::error_code ec;
  fs::create_directories(user_data, ec);
  std::error_code ec2;
  if (ec || !fs::is_directory(user_data, ec2)) {
    Log("import: cannot create " + Utf8(user_data) + (ec ? ": " + ec.message() : std::string()));
    ImportResult r;
    r.outcome = Outcome::kNoWriteAccess;
    result_ = std::move(r);
    stage_ = Stage::kResult;
    Log("import: result " + std::string(OutcomeName(Outcome::kNoWriteAccess)));
    return true;
  }

  if (fx_.save_pending) {
    const std::string err = fx_.save_pending(true);
    if (!err.empty()) Log("import: could not save the pending flag (starting anyway): " + err);
  }

  auto shared = std::make_shared<Shared>();
  shared->post = fx_.post;
  shared->token = token_;

  suitconv::ImportRequest request;
  request.doax2 = source_;
  request.game_root = game_root;
  request.user_data = user_data;
  request.workers = workers;
  // Called from the job's threads: the line travels to the UI thread like everything else.
  request.log = [shared](std::string_view line) { FlowBridge::Log(shared, "import: job: " + std::string(line)); };

  std::shared_ptr<Runner> runner = fx_.start ? fx_.start(std::move(request)) : nullptr;
  if (!runner) {
    ImportResult r;
    r.outcome = Outcome::kFailed;
    r.detail = "the import could not be started";
    result_ = std::move(r);
    stage_ = Stage::kResult;
    Log("import: result " + std::string(OutcomeName(Outcome::kFailed)) + ": " + result_->detail);
    return true;
  }
  runner_ = runner;
  stage_ = Stage::kRunning;  // before run_async: an inline run_async finishes the whole import in there
  fx_.run_async([runner = std::move(runner), shared = std::move(shared)] { FlowBridge::Wait(runner, shared); });
  return true;
}

void Flow::Cancel() {
  if (stage_ != Stage::kRunning || !runner_ || cancel_sent_) return;
  cancel_sent_ = true;
  Log("import: cancel requested");
  runner_->Cancel();
}

void Flow::OnProgress(const Progress& p) {
  if (stage_ != Stage::kRunning) return;
  progress_ = p;
  if (!logged_phase_ || *logged_phase_ != p.phase) {
    logged_phase_ = p.phase;
    Log("import: phase " + PhaseText(p));
  }
}

void Flow::OnResult(ImportResult result, const Progress& p) {
  if (stage_ != Stage::kRunning) return;
  OnProgress(p);
  if (result.outcome == Outcome::kRunning) {  // a job that is done always has an outcome; do not show "running" as one
    result.outcome = Outcome::kFailed;
    result.detail = "the import ended without a result";
  }
  Log("import: result " + std::string(OutcomeName(result.outcome)));
  for (const auto& s : result.skipped) Log("import: skipped " + s.label + ": " + s.stage + ": " + s.why);
  for (const auto& d : result.differs_from_reference) Log("import: differs " + d);
  if (ClearsPending(result.outcome) && fx_.save_pending) {
    const std::string err = fx_.save_pending(false);
    if (!err.empty()) Log("import: could not clear the pending flag: " + err);
  }
  result_ = std::move(result);
  stage_ = Stage::kResult;
  runner_.reset();
}

bool Flow::restart_needed() const {
  return mode_ == Mode::kMenu && stage_ == Stage::kResult && result_ && ClearsPending(result_->outcome);
}

bool Flow::auto_start() const {
  return mode_ != Mode::kMenu && stage_ == Stage::kResult && result_ && result_->outcome == Outcome::kImported &&
         result_->differs_from_reference.empty();
}

bool Flow::can_retry() const {
  if (stage_ != Stage::kResult || !result_) return false;
  switch (result_->outcome) {
    case Outcome::kWrongDisc:
    case Outcome::kReadError:
    case Outcome::kNoSpace:
    case Outcome::kNoWriteAccess:
    case Outcome::kCancelled:
    case Outcome::kFailed:
      return true;
    default:
      return false;
  }
}

std::vector<std::string> Flow::ResultLines() const {
  std::vector<std::string> lines;
  if (stage_ != Stage::kResult || !result_) return lines;
  const ImportResult& r = *result_;
  const int total = progress_.suits_total;
  const int skipped = int(r.skipped.size());
  const auto skip_lines = [&] {
    for (const auto& s : r.skipped) lines.push_back(s.label + ": " + s.stage + ": " + s.why);
  };
  const auto with_detail = [&](const std::string& head) {
    return r.detail.empty() ? head + ". Nothing was changed." : head + " (" + r.detail + "). Nothing was changed.";
  };
  // (1) the outcome's own lines
  switch (r.outcome) {
    case Outcome::kRunning:
      break;
    case Outcome::kImported:
      lines.push_back(Count(total - skipped) + " swimsuits imported.");
      break;
    case Outcome::kImportedWithSkips:
      lines.push_back(Count(total - skipped) + " swimsuits imported, " + Count(skipped) + " skipped:");
      skip_lines();
      break;
    case Outcome::kNothingImported:
      lines.push_back("No swimsuit could be built:");
      skip_lines();
      break;
    case Outcome::kCancelled:
      lines.push_back(std::string(kCancelledLine));
      lines.push_back(std::string(kCancelledOfferedAgain));
      break;
    case Outcome::kWrongDisc:
      lines.push_back(std::string(kWrongDiscLine));
      break;
    case Outcome::kGameFilesDiffer:
      lines.push_back(std::string(kGameFilesDifferLine));
      break;
    case Outcome::kNoSpace:
      lines.push_back("Needs " + FormatMB(r.bytes_needed) + " free in " + Utf8(user_data_) + "; " + FormatMB(r.bytes_free) +
                      " free. Nothing was changed.");
      break;
    case Outcome::kNoWriteAccess:
      lines.push_back("Cannot write to " + Utf8(user_data_) + ". Nothing was changed.");
      break;
    case Outcome::kReadError:
      lines.push_back(with_detail("The disc could not be read"));
      break;
    case Outcome::kSwapPending:
      lines.push_back(std::string(kSwapPendingLine));
      break;
    case Outcome::kFailed:
      lines.push_back(with_detail("The import failed"));
      break;
  }
  // (2) kSwapPending's own line lists no skips
  if (r.outcome == Outcome::kSwapPending && skipped > 0) {
    lines.push_back(Count(skipped) + " skipped:");
    skip_lines();
  }
  // (3) after any outcome
  if (!r.differs_from_reference.empty()) {
    lines.push_back(Count(int(r.differs_from_reference.size())) +
                    " of them differ from the reference build on this PC: they work, and online they count as a different "
                    "import.");
  }
  // (4) last
  if (restart_needed()) lines.push_back(std::string(kRestartLine));
  return lines;
}

}  // namespace doa4::suits_import
