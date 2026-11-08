// Host test for src/doa4_suits_import_flow.cpp (the DOAX2 swimsuit import's screen logic, without ImGui). Built and run by
// run_suits_import_flow_test.bat. Links none of the converter: the import is a scripted fake Runner.

#include "../../src/doa4_suits_import_flow.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

#include "../../src/doa4_utf8.h"

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::suits_import;
namespace sc = doa4::suitconv;
namespace fs = std::filesystem;
using Lines = std::vector<std::string>;

static void CheckLinesAt(int line, const char* what, const Lines& actual, const Lines& expected) {
  ++g_checks;
  if (actual == expected) return;
  ++g_failures;
  std::printf("FAIL %s:%d: %s\n  actual (%zu):\n", __FILE__, line, what, actual.size());
  for (const auto& l : actual) std::printf("    [%s]\n", l.c_str());
  std::printf("  expected (%zu):\n", expected.size());
  for (const auto& l : expected) std::printf("    [%s]\n", l.c_str());
}
#define CHECK_LINES(actual, expected) CheckLinesAt(__LINE__, #actual, (actual), (expected))

static sc::Progress Prog(sc::Phase phase, int done, int total, double fraction = 0, uint64_t bd = 0, uint64_t bt = 0) {
  sc::Progress p;
  p.phase = phase;
  p.suits_done = done;
  p.suits_total = total;
  p.fraction = fraction;
  p.bytes_done = bd;
  p.bytes_total = bt;
  return p;
}

static fs::path TempRoot() {
  static const fs::path root =
      fs::temp_directory_path() / ("p3_t3_flow_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  return root;
}

// ---- The fakes ---------------------------------------------------------------------------------------------------------
struct RunState {
  std::atomic<int> cancels{0};
  std::atomic<int> polls{0};
};

// Scripted: progress() shows steps[0], steps[1], ... one per done() call; done() is true once the last step has been shown.
// until_cancel: shows steps[0] and is done only after Cancel().
struct FakeRunner : Runner {
  std::shared_ptr<RunState> st;
  std::vector<sc::Progress> steps;
  sc::ImportResult res;
  bool until_cancel = false;
  sc::Progress progress() const override {
    if (until_cancel) return steps.front();
    const size_t i = std::min<size_t>(size_t(st->polls.load()), steps.size() - 1);
    return steps[i];
  }
  bool done() const override {
    if (until_cancel) return st->cancels.load() > 0;
    return st->polls++ >= int(steps.size()) - 1;
  }
  void Cancel() override { ++st->cancels; }
  sc::ImportResult result() const override { return res; }
};

struct Fake {
  std::deque<std::optional<fs::path>> isos, folders;
  std::vector<fs::path> dialog_starts;
  std::vector<bool> pending_calls;
  std::string pending_error;
  std::vector<std::string> logs;
  std::vector<sc::ImportRequest> requests;
  int starts = 0;
  bool start_null = false;
  std::function<void(sc::ImportRequest&)> on_start;
  // the scripted import
  std::vector<sc::Progress> steps{Prog(sc::Phase::kDone, 34, 34, 1.0)};
  sc::ImportResult res;
  bool until_cancel = false;
  std::shared_ptr<RunState> state = std::make_shared<RunState>();
  // threading
  bool defer = false;  // run_async queues the work instead of running it
  std::vector<std::function<void()>> queued;
  bool real_thread = false;  // run_async starts a std::thread (kept in `thread`)
  std::thread thread;
  bool defer_posts = false;  // post queues
  std::mutex posts_mutex;
  std::deque<std::function<void()>> posts;
  Flow* flow = nullptr;
  std::vector<double> fractions_after_post;  // flow->progress().fraction after each inline post (when `flow` is set)

  Effects Make() {
    Effects e;
    e.pick_iso = [this](const fs::path& start) {
      dialog_starts.push_back(start);
      if (isos.empty()) return std::optional<fs::path>();
      auto r = isos.front();
      isos.pop_front();
      return r;
    };
    e.pick_folder = [this](const fs::path& start) {
      dialog_starts.push_back(start);
      if (folders.empty()) return std::optional<fs::path>();
      auto r = folders.front();
      folders.pop_front();
      return r;
    };
    e.start = [this](sc::ImportRequest req) -> std::unique_ptr<Runner> {
      ++starts;
      if (on_start) on_start(req);
      requests.push_back(req);
      if (start_null) return nullptr;
      auto r = std::make_unique<FakeRunner>();
      r->st = state;
      r->steps = steps;
      r->res = res;
      r->until_cancel = until_cancel;
      return r;
    };
    e.run_async = [this](std::function<void()> work) {
      if (real_thread) {
        thread = std::thread(std::move(work));
      } else if (defer) {
        queued.push_back(std::move(work));
      } else {
        work();
      }
    };
    e.post = [this](std::function<void()> fn) {
      if (defer_posts) {
        std::lock_guard<std::mutex> lock(posts_mutex);
        posts.push_back(std::move(fn));
        return;
      }
      fn();
      if (flow) fractions_after_post.push_back(flow->progress().fraction);
    };
    e.save_pending = [this](bool v) {
      pending_calls.push_back(v);
      return pending_error;
    };
    e.log = [this](const std::string& s) { logs.push_back(s); };
    return e;
  }
  void RunQueued() {
    auto q = std::move(queued);
    queued.clear();
    for (auto& w : q) w();
  }
  bool LogHas(const std::string& part) const {
    for (const auto& l : logs)
      if (l.find(part) != std::string::npos) return true;
    return false;
  }
};

static const fs::path kIso = "C:/disc/doax2.iso";
static const fs::path kGame = "C:/game";
static fs::path Ud() { return TempRoot() / "ud"; }

static sc::SkippedSuit Skip(const char* label, const char* stage, const char* why) { return {label, stage, why}; }

// Picks kIso and starts; returns Start's answer.
static bool PickAndStart(Fake& f, Flow& flow, int workers = 2) {
  f.isos.push_back(kIso);
  flow.ChooseImage();
  return flow.Start(kGame, Ud(), workers);
}

// ---- Tests -------------------------------------------------------------------------------------------------------------
static void TestStartRefusedWithoutSource() {
  Fake f;
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(!flow.Start(kGame, Ud(), 2));
  CHECK(f.starts == 0 && f.pending_calls.empty());
  CHECK(flow.stage() == Stage::kChoose && !flow.busy());
  CHECK(f.LogHas("import:"));  // the refusal is logged
  CHECK(!fs::exists(Ud()));    // and nothing was created
}

static void TestStartRefusedWhileRunning() {
  Fake f;
  f.defer = true;
  f.until_cancel = true;
  f.res.outcome = sc::Outcome::kCancelled;
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, flow));
  CHECK(flow.busy() && flow.stage() == Stage::kRunning);
  const size_t logs_before = f.logs.size();
  CHECK(!flow.Start(kGame, Ud(), 2));
  CHECK(f.starts == 1 && f.pending_calls == std::vector<bool>{true});
  CHECK(f.logs.size() > logs_before);
  // The other buttons do nothing while running.
  const size_t dialogs = f.dialog_starts.size();
  f.isos.push_back("C:/other.iso");
  f.folders.push_back("C:/otherdir");
  flow.ChooseImage();
  flow.ChooseFolder();
  flow.Clear();
  flow.Back();
  CHECK(f.dialog_starts.size() == dialogs);
  CHECK(flow.source() == kIso && flow.stage() == Stage::kRunning);
  flow.Cancel();
  f.RunQueued();
  CHECK(flow.stage() == Stage::kResult);
}

static void TestFirstRunSuccessAutoStarts() {
  Fake f;
  f.res.outcome = sc::Outcome::kImported;
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, flow));
  CHECK(flow.stage() == Stage::kResult && !flow.busy());
  CHECK(f.pending_calls == (std::vector<bool>{true, false}));
  CHECK_LINES(flow.ResultLines(), (Lines{"34 swimsuits imported."}));
  CHECK(flow.auto_start() && !flow.restart_needed() && !flow.can_retry());
  CHECK(flow.mode() == Mode::kFirstRun);
  // The same in the start offer.
  Fake g;
  g.res.outcome = sc::Outcome::kImported;
  Flow offer(Mode::kStartOffer, g.Make());
  CHECK(PickAndStart(g, offer));
  CHECK(offer.auto_start());
}

static void TestDiffersBlocksAutoStart() {
  Fake f;
  f.res.outcome = sc::Outcome::kImported;
  f.res.differs_from_reference = {"a c0", "b c1"};
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, flow));
  CHECK_LINES(flow.ResultLines(),
              (Lines{"34 swimsuits imported.",
                     "2 of them differ from the reference build on this PC: they work, and online they count as a different import."}));
  CHECK(!flow.auto_start());
  CHECK(f.pending_calls == (std::vector<bool>{true, false}));
}

static void TestCancelKeepsPending() {
  Fake f;
  f.defer = true;
  f.until_cancel = true;
  f.res.outcome = sc::Outcome::kCancelled;
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, flow));
  flow.Cancel();
  flow.Cancel();  // asked once only
  CHECK(f.state->cancels == 1);
  f.RunQueued();
  CHECK(flow.stage() == Stage::kResult);
  CHECK(f.pending_calls == std::vector<bool>{true});
  CHECK_LINES(flow.ResultLines(),
              (Lines{"Import cancelled. Nothing was changed.", "The import is offered again at the next start."}));
  CHECK(flow.can_retry() && !flow.auto_start() && !flow.restart_needed());
  // Cancel with nothing running does nothing.
  flow.Cancel();
  CHECK(f.state->cancels == 1);
}

struct OutcomeRow {
  sc::Outcome outcome;
  std::string detail;
  size_t skips;
  Lines lines;
  bool retry;
  bool clears_pending;
};

static void TestEachOutcomeLines() {
  const std::string ud = doa4::Utf8(Ud());
  const Lines skip_lines{"vks_ch_d c0: budget: too big", "vks_ch_e c1: tpr: no", "vks_ch_f c2: cat: bad"};
  const std::vector<OutcomeRow> rows = {
      {sc::Outcome::kImported, "", 0, {"34 swimsuits imported."}, false, true},
      {sc::Outcome::kImportedWithSkips, "", 3,
       {"31 swimsuits imported, 3 skipped:", skip_lines[0], skip_lines[1], skip_lines[2]}, false, true},
      {sc::Outcome::kNothingImported, "", 3,
       {"No swimsuit could be built:", skip_lines[0], skip_lines[1], skip_lines[2]}, false, false},
      {sc::Outcome::kCancelled, "", 0,
       {"Import cancelled. Nothing was changed.", "The import is offered again at the next start."}, true, false},
      {sc::Outcome::kWrongDisc, "", 0,
       {"Needs your own USA copy of DOAX2: this disc's files are not the ones the swimsuits are built from. Nothing was "
        "changed."},
       true, false},
      {sc::Outcome::kGameFilesDiffer, "", 0,
       {"Your DOA4 game files are not the Rev 1 files the swimsuits are built from. Nothing was changed."}, false, false},
      {sc::Outcome::kNoSpace, "", 0, {"Needs 364 MB free in " + ud + "; 100 MB free. Nothing was changed."}, true, false},
      {sc::Outcome::kNoWriteAccess, "", 0, {"Cannot write to " + ud + ". Nothing was changed."}, true, false},
      {sc::Outcome::kReadError, "bad sector", 0, {"The disc could not be read (bad sector). Nothing was changed."}, true, false},
      {sc::Outcome::kSwapPending, "", 0,
       {"The swimsuits are ready. They are put in place when the game next starts."}, false, true},
      {sc::Outcome::kFailed, "no room", 0, {"The import failed (no room). Nothing was changed."}, true, false},
  };
  for (const auto& row : rows) {
    Fake f;
    f.res.outcome = row.outcome;
    f.res.detail = row.detail;
    f.res.bytes_needed = 381096088;
    f.res.bytes_free = 104857600;
    for (size_t i = 0; i < row.skips; ++i) {
      static const sc::SkippedSuit all[] = {{"vks_ch_d c0", "budget", "too big"}, {"vks_ch_e c1", "tpr", "no"},
                                            {"vks_ch_f c2", "cat", "bad"}};
      f.res.skipped.push_back(all[i]);
    }
    Flow flow(Mode::kFirstRun, f.Make());
    CHECK(PickAndStart(f, flow));
    CHECK(flow.stage() == Stage::kResult);
    CHECK_LINES(flow.ResultLines(), row.lines);
    CHECK(flow.can_retry() == row.retry);
    const std::vector<bool> want = row.clears_pending ? std::vector<bool>{true, false} : std::vector<bool>{true};
    CHECK(f.pending_calls == want);
    CHECK(flow.result() && flow.result()->outcome == row.outcome);
    // auto_start only after a clean kImported; never restart_needed outside the menu.
    CHECK(flow.auto_start() == (row.outcome == sc::Outcome::kImported));
    CHECK(!flow.restart_needed());
  }
  // Every Outcome except kRunning has a row.
  CHECK(rows.size() == 11);
}

static void TestSkipsListedInOrder() {
  Fake f;
  f.res.outcome = sc::Outcome::kImportedWithSkips;
  f.res.skipped = {Skip("z last c3", "s3", "w3"), Skip("a first c0", "s0", "w0"), Skip("m mid c1", "s1", "w1")};
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, flow));
  CHECK_LINES(flow.ResultLines(), (Lines{"31 swimsuits imported, 3 skipped:", "z last c3: s3: w3", "a first c0: s0: w0",
                                         "m mid c1: s1: w1"}));
}

static void TestNoSpaceMegabytes() {
  Fake f;
  f.res.outcome = sc::Outcome::kNoSpace;
  f.res.bytes_needed = 381096088;
  f.res.bytes_free = 104857600;
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, flow));
  const auto lines = flow.ResultLines();
  CHECK(lines.size() == 1);
  CHECK(!lines.empty() &&
        lines[0] == "Needs 364 MB free in " + doa4::Utf8(Ud()) + "; 100 MB free. Nothing was changed.");
}

static void TestFormatMB() {
  CHECK(FormatMB(0) == "0 MB");
  CHECK(FormatMB(1) == "1 MB");
  CHECK(FormatMB((1ull << 20) - 1) == "1 MB");
  CHECK(FormatMB(1ull << 20) == "1 MB");
  CHECK(FormatMB((1ull << 20) + 1) == "2 MB");
  CHECK(FormatMB(104857600) == "100 MB");
  CHECK(FormatMB(381096088) == "364 MB");
  CHECK(FormatMB(381096088 - 1) == "364 MB");
  CHECK(FormatMB(364ull << 20) == "364 MB");
}

static void TestMenuRestartNeeded() {
  const sc::Outcome need[] = {sc::Outcome::kImported, sc::Outcome::kImportedWithSkips, sc::Outcome::kSwapPending};
  const sc::Outcome other[] = {sc::Outcome::kNothingImported, sc::Outcome::kCancelled, sc::Outcome::kWrongDisc,
                               sc::Outcome::kGameFilesDiffer, sc::Outcome::kNoSpace, sc::Outcome::kNoWriteAccess,
                               sc::Outcome::kReadError, sc::Outcome::kFailed};
  for (Mode mode : {Mode::kMenu, Mode::kFirstRun, Mode::kStartOffer}) {
    for (sc::Outcome o : need) {
      Fake f;
      f.res.outcome = o;
      Flow flow(mode, f.Make());
      CHECK(PickAndStart(f, flow));
      const auto lines = flow.ResultLines();
      const bool has_restart = !lines.empty() && lines.back() == "Restart the game to use them.";
      CHECK(flow.restart_needed() == (mode == Mode::kMenu));
      CHECK(has_restart == (mode == Mode::kMenu));
      // Only the menu ever has the restart line, and only once.
      int count = 0;
      for (const auto& l : lines) count += l == "Restart the game to use them.";
      CHECK(count == (mode == Mode::kMenu ? 1 : 0));
      // The menu never starts the game by itself.
      CHECK(!(mode == Mode::kMenu && flow.auto_start()));
    }
  }
  for (sc::Outcome o : other) {
    Fake f;
    f.res.outcome = o;
    Flow flow(Mode::kMenu, f.Make());
    CHECK(PickAndStart(f, flow));
    CHECK(!flow.restart_needed());
    for (const auto& l : flow.ResultLines()) CHECK(l != "Restart the game to use them.");
  }
}

static void TestMenuFullOrder() {
  const std::string differs =
      "2 of them differ from the reference build on this PC: they work, and online they count as a different import.";
  {  // kSwapPending: its own line lists no skips, so "<m> skipped:" and the skips follow it.
    Fake f;
    f.res.outcome = sc::Outcome::kSwapPending;
    f.res.skipped = {Skip("a c0", "s0", "w0"), Skip("b c1", "s1", "w1")};
    f.res.differs_from_reference = {"x c0", "y c1"};
    Flow flow(Mode::kMenu, f.Make());
    CHECK(PickAndStart(f, flow));
    CHECK_LINES(flow.ResultLines(),
                (Lines{"The swimsuits are ready. They are put in place when the game next starts.", "2 skipped:",
                       "a c0: s0: w0", "b c1: s1: w1", differs, "Restart the game to use them."}));
  }
  {  // kImportedWithSkips: the outcome's own lines carry the skips.
    Fake f;
    f.res.outcome = sc::Outcome::kImportedWithSkips;
    f.res.skipped = {Skip("a c0", "s0", "w0"), Skip("b c1", "s1", "w1")};
    f.res.differs_from_reference = {"x c0", "y c1"};
    Flow flow(Mode::kMenu, f.Make());
    CHECK(PickAndStart(f, flow));
    CHECK_LINES(flow.ResultLines(), (Lines{"32 swimsuits imported, 2 skipped:", "a c0: s0: w0", "b c1: s1: w1", differs,
                                           "Restart the game to use them."}));
  }
  {  // kSwapPending without skips: no "skipped" line.
    Fake f;
    f.res.outcome = sc::Outcome::kSwapPending;
    Flow flow(Mode::kMenu, f.Make());
    CHECK(PickAndStart(f, flow));
    CHECK_LINES(flow.ResultLines(),
                (Lines{"The swimsuits are ready. They are put in place when the game next starts.",
                       "Restart the game to use them."}));
  }
  {  // The differs line comes after any outcome, and nothing restart-ish after a failure.
    Fake f;
    f.res.outcome = sc::Outcome::kFailed;
    f.res.detail = "boom";
    f.res.differs_from_reference = {"x c0", "y c1"};
    Flow flow(Mode::kMenu, f.Make());
    CHECK(PickAndStart(f, flow));
    CHECK_LINES(flow.ResultLines(), (Lines{"The import failed (boom). Nothing was changed.", differs}));
  }
}

static void TestEmptyDetail() {
  {
    Fake f;
    f.res.outcome = sc::Outcome::kReadError;
    Flow flow(Mode::kFirstRun, f.Make());
    CHECK(PickAndStart(f, flow));
    CHECK_LINES(flow.ResultLines(), (Lines{"The disc could not be read. Nothing was changed."}));
  }
  {
    Fake f;
    f.res.outcome = sc::Outcome::kFailed;
    Flow flow(Mode::kFirstRun, f.Make());
    CHECK(PickAndStart(f, flow));
    CHECK_LINES(flow.ResultLines(), (Lines{"The import failed. Nothing was changed."}));
  }
}

static void TestPhaseTexts() {
  using P = sc::Phase;
  CHECK(PhaseText(Prog(P::kCheckingSpace, 0, 0)) == "Checking free space");
  CHECK(PhaseText(Prog(P::kCheckingDisc, 0, 0)) == "Checking the DOAX2 disc");
  CHECK(PhaseText(Prog(P::kCheckingGame, 0, 0)) == "Checking the DOA4 game files");
  CHECK(PhaseText(Prog(P::kConverting, 0, 34)) == "Building swimsuit 1 of 34");
  CHECK(PhaseText(Prog(P::kConverting, 2, 34)) == "Building swimsuit 3 of 34");
  CHECK(PhaseText(Prog(P::kConverting, 33, 34)) == "Building swimsuit 34 of 34");
  CHECK(PhaseText(Prog(P::kConverting, 34, 34)) == "Building swimsuit 34 of 34");  // never 35 of 34
  CHECK(PhaseText(Prog(P::kWriting, 34, 34)) == "Writing the swimsuits");
  CHECK(PhaseText(Prog(P::kSwapping, 34, 34)) == "Putting the swimsuits in place");
  CHECK(PhaseText(Prog(P::kDone, 34, 34)) == "Done");
}

static void TestProgressPassthrough() {
  Fake f;
  f.steps = {Prog(sc::Phase::kCheckingDisc, 0, 0, 0.1, 5, 10), Prog(sc::Phase::kConverting, 7, 34, 0.5, 10, 10),
             Prog(sc::Phase::kDone, 34, 34, 0.9, 10, 10)};
  f.res.outcome = sc::Outcome::kImported;
  Flow flow(Mode::kFirstRun, f.Make());
  f.flow = &flow;
  CHECK(flow.progress().phase == sc::Phase::kCheckingSpace && flow.progress().fraction == 0);
  CHECK(PickAndStart(f, flow));
  // Each step reached the UI in order (the result's own post repeats the last one).
  CHECK((f.fractions_after_post == std::vector<double>{0.1, 0.5, 0.9, 0.9}));
  const auto p = flow.progress();
  CHECK(p.phase == sc::Phase::kDone && p.suits_done == 34 && p.suits_total == 34 && p.bytes_done == 10 &&
        p.bytes_total == 10 && p.fraction == 0.9);
}

static void TestUserDataCreatedBeforeStart() {
  Fake f;
  f.res.outcome = sc::Outcome::kImported;
  const fs::path ud = TempRoot() / "fresh" / "deeper";
  bool existed = false;
  std::vector<bool> pending_then;
  fs::path got_ud, got_game, got_doax2;
  int got_workers = -1;
  f.on_start = [&](sc::ImportRequest& r) {
    existed = fs::is_directory(ud);
    pending_then = f.pending_calls;
    got_ud = r.user_data;
    got_game = r.game_root;
    got_doax2 = r.doax2;
    got_workers = r.workers;
  };
  CHECK(!fs::exists(ud));
  Flow flow(Mode::kFirstRun, f.Make());
  f.isos.push_back(kIso);
  flow.ChooseImage();
  CHECK(flow.Start(kGame, ud, 3));
  CHECK(existed);
  CHECK(pending_then == std::vector<bool>{true});  // save_pending(true) came before start
  CHECK(got_ud == ud && got_game == kGame && got_doax2 == kIso && got_workers == 3);
}

static void TestUserDataUncreatable() {
  fs::create_directories(TempRoot());
  const fs::path file = TempRoot() / "afile";
  { std::ofstream(file) << "x"; }
  const fs::path ud = file / "ud";
  Fake f;
  Flow flow(Mode::kFirstRun, f.Make());
  f.isos.push_back(kIso);
  flow.ChooseImage();
  CHECK(flow.Start(kGame, ud, 2));
  CHECK(f.starts == 0 && f.pending_calls.empty());
  CHECK(flow.stage() == Stage::kResult && !flow.busy());
  CHECK(flow.result() && flow.result()->outcome == sc::Outcome::kNoWriteAccess);
  CHECK_LINES(flow.ResultLines(), (Lines{"Cannot write to " + doa4::Utf8(ud) + ". Nothing was changed."}));
  CHECK(flow.can_retry() && !flow.auto_start());
  CHECK(f.LogHas("import:"));
}

static void TestSavePendingErrorStartsAnyway() {
  Fake f;
  f.pending_error = "disk full";
  f.res.outcome = sc::Outcome::kImported;
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, flow));
  CHECK(f.starts == 1);
  CHECK(f.LogHas("disk full"));
  CHECK(flow.stage() == Stage::kResult && flow.result() && flow.result()->outcome == sc::Outcome::kImported);
}

static void TestStartReturnsNoRunner() {
  Fake f;
  f.start_null = true;
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, flow));
  CHECK(flow.stage() == Stage::kResult && flow.result() && flow.result()->outcome == sc::Outcome::kFailed);
  CHECK_LINES(flow.ResultLines(), (Lines{"The import failed (the import could not be started). Nothing was changed."}));
  CHECK(flow.can_retry());
  CHECK(f.pending_calls == std::vector<bool>{true});
}

static void TestChoosingAndStages() {
  Fake f;
  f.res.outcome = sc::Outcome::kWrongDisc;
  Flow flow(Mode::kFirstRun, f.Make());
  CHECK(flow.source().empty() && flow.stage() == Stage::kChoose && !flow.busy());
  // A cancelled dialog changes nothing.
  flow.ChooseImage();
  CHECK(flow.source().empty());
  CHECK(f.dialog_starts.size() == 1 && f.dialog_starts[0].empty());
  // An image: the next dialog starts in its folder.
  f.isos.push_back(kIso);
  flow.ChooseImage();
  CHECK(flow.source() == kIso);
  f.folders.push_back("C:/extracted/DOAX2");
  flow.ChooseFolder();
  CHECK(!f.dialog_starts.empty() && f.dialog_starts.back() == kIso.parent_path());
  CHECK(flow.source() == fs::path("C:/extracted/DOAX2"));
  // A folder: the next dialog starts in that folder.
  flow.ChooseImage();
  CHECK(!f.dialog_starts.empty() && f.dialog_starts.back() == fs::path("C:/extracted/DOAX2"));
  CHECK(flow.source() == fs::path("C:/extracted/DOAX2"));  // the dialog was cancelled
  // Clear forgets the source; Start is refused again.
  flow.Clear();
  CHECK(flow.source().empty() && !flow.Start(kGame, Ud(), 2));
  // A result: Back returns to the choice with the source kept and the result gone.
  f.isos.push_back(kIso);
  flow.ChooseImage();
  CHECK(flow.Start(kGame, Ud(), 2));
  CHECK(flow.stage() == Stage::kResult && flow.result());
  const size_t dialogs = f.dialog_starts.size();
  f.isos.push_back("C:/other.iso");
  flow.ChooseImage();
  flow.ChooseFolder();
  flow.Clear();
  CHECK(f.dialog_starts.size() == dialogs && flow.source() == kIso);  // refused in kResult
  flow.Back();
  CHECK(flow.stage() == Stage::kChoose && !flow.result() && flow.source() == kIso);
  CHECK(flow.progress().fraction == 0 && flow.ResultLines().empty());
  CHECK(!flow.can_retry() && !flow.auto_start() && !flow.restart_needed());
  flow.Back();  // nothing to go back to
  CHECK(flow.stage() == Stage::kChoose);
  // Try again from the result without going back.
  CHECK(flow.Start(kGame, Ud(), 2));
  CHECK(flow.stage() == Stage::kResult && f.starts == 2);
  CHECK(flow.Start(kGame, Ud(), 2));
  CHECK(f.starts == 3);
}

static void TestDestroyWhileRunning() {
  Fake f;
  f.real_thread = true;
  f.defer_posts = true;
  f.until_cancel = true;
  f.res.outcome = sc::Outcome::kCancelled;
  auto flow = std::make_unique<Flow>(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, *flow));
  CHECK(flow->busy());
  // Wait until the waiter has posted its first snapshot.
  for (int i = 0; i < 200; ++i) {
    {
      std::lock_guard<std::mutex> lock(f.posts_mutex);
      if (!f.posts.empty()) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const size_t logs_before = f.logs.size();
  const auto t0 = std::chrono::steady_clock::now();
  flow.reset();  // the owner forgot to drop the pending posts
  const auto took = std::chrono::steady_clock::now() - t0;
  CHECK(took < std::chrono::seconds(1));
  CHECK(f.state->cancels == 1);
  if (f.thread.joinable()) f.thread.join();  // the owner joins
  const size_t logs_after = f.logs.size();
  CHECK(logs_after == logs_before);  // the destructor logs nothing (nothing was posted into it either)
  // Now the posts that outlived the Flow run: they must do nothing.
  std::deque<std::function<void()>> late;
  {
    std::lock_guard<std::mutex> lock(f.posts_mutex);
    late = std::move(f.posts);
  }
  CHECK(!late.empty());
  CHECK(late.size() >= 2);  // the first snapshot and the result
  for (auto& fn : late) fn();
  CHECK(f.logs.size() == logs_after);
  CHECK(f.pending_calls == std::vector<bool>{true});
  CHECK(f.state->cancels == 1);
}

static void TestDestroyAfterUserCancelCancelsOnce() {
  Fake f;
  f.defer = true;
  f.until_cancel = true;
  auto flow = std::make_unique<Flow>(Mode::kFirstRun, f.Make());
  CHECK(PickAndStart(f, *flow));
  flow->Cancel();
  flow.reset();
  CHECK(f.state->cancels == 1);
  // A Flow that is not running cancels nothing.
  Fake g;
  g.res.outcome = sc::Outcome::kImported;
  {
    Flow done(Mode::kFirstRun, g.Make());
    CHECK(PickAndStart(g, done));
  }
  CHECK(g.state->cancels == 0);
}

static void TestReimportNote() {
  CHECK(ReimportNote("proto-1d").empty());
  CHECK(ReimportNote("") .empty());
  CHECK(ReimportNote("proto-1c") ==
        "These swimsuits were built by an older importer. Import them again to match players who have the new ones.");
  CHECK(ReimportNote("zzz") == std::string(kReimportLine));
}

static void TestDecideStartup() {
  struct Row {
    bool session, pending, shown, swap, offer;
  };
  const Row rows[] = {
      {false, false, false, true, false}, {false, false, true, true, false}, {false, true, false, true, true},
      {false, true, true, true, false},   {true, false, false, false, false}, {true, false, true, false, false},
      {true, true, false, false, false},  {true, true, true, false, false},
  };
  for (const Row& r : rows) {
    const StartupSuits s = DecideStartup(r.session, r.pending, r.shown);
    CHECK(s.complete_swap == r.swap);
    CHECK(s.offer == r.offer);
  }
}

static void TestLogLines() {
  Fake f;
  f.steps = {Prog(sc::Phase::kCheckingSpace, 0, 0), Prog(sc::Phase::kCheckingDisc, 0, 0),
             Prog(sc::Phase::kConverting, 0, 34), Prog(sc::Phase::kConverting, 1, 34), Prog(sc::Phase::kDone, 34, 34, 1.0)};
  f.res.outcome = sc::Outcome::kImportedWithSkips;
  f.res.skipped = {Skip("a c0", "s0", "w0"), Skip("b c1", "s1", "w1")};
  f.res.differs_from_reference = {"x c0"};
  f.on_start = [](sc::ImportRequest& r) { r.log("suit x c0: output differs from the reference build (abc)"); };
  const fs::path src = fs::path(u8"C:/disc/d\u00e9j\u00e0.iso");
  Flow flow(Mode::kFirstRun, f.Make());
  f.isos.push_back(src);
  flow.ChooseImage();
  // The job's own log is only reachable through the request: the fake start calls it after the request was built.
  CHECK(flow.Start(kGame, Ud(), 2));
  Lines want = {"import: start " + doa4::Utf8(src) + " -> " + doa4::Utf8(Ud()) + " workers=2",
                "import: job: suit x c0: output differs from the reference build (abc)",
                "import: phase Checking free space",
                "import: phase Checking the DOAX2 disc",
                "import: phase Building swimsuit 1 of 34",
                "import: phase Done",
                "import: result kImportedWithSkips",
                "import: skipped a c0: s0: w0",
                "import: skipped b c1: s1: w1",
                "import: differs x c0"};
  CHECK_LINES(f.logs, want);
  for (const auto& l : f.logs) CHECK(l.rfind("import: ", 0) == 0);
}

static void TestConstantsAndNames() {
  CHECK(kSectionTitle == "DOAX2 swimsuits (optional): needs your own USA copy of Dead or Alive Xtreme 2");
  CHECK(kUnlockNote ==
        "Importing the swimsuits also unlocks all the DOA4 costumes for the nine girls who have them. Your save isn't changed.");
  CHECK(kNotChosen == "Not chosen: the game runs with its normal costumes.");
  CHECK(kChosenPrefix == "Swimsuits from: ");
  CHECK(kOfferTitle == "The DOAX2 swimsuit import did not finish last time.");
  CHECK(kPhaseCheckingSpace == "Checking free space");
  CHECK(kPhaseCheckingDisc == "Checking the DOAX2 disc");
  CHECK(kPhaseCheckingGame == "Checking the DOA4 game files");
  CHECK(kPhaseWriting == "Writing the swimsuits");
  CHECK(kPhaseSwapping == "Putting the swimsuits in place");
  CHECK(kPhaseDone == "Done");
  CHECK(kCancelledLine == "Import cancelled. Nothing was changed.");
  CHECK(kCancelledOfferedAgain == "The import is offered again at the next start.");
  CHECK(kWrongDiscLine ==
        "Needs your own USA copy of DOAX2: this disc's files are not the ones the swimsuits are built from. Nothing was changed.");
  CHECK(kGameFilesDifferLine == "Your DOA4 game files are not the Rev 1 files the swimsuits are built from. Nothing was changed.");
  CHECK(kSwapPendingLine == "The swimsuits are ready. They are put in place when the game next starts.");
  CHECK(kRestartLine == "Restart the game to use them.");
  CHECK(kReimportLine ==
        "These swimsuits were built by an older importer. Import them again to match players who have the new ones.");
  using O = sc::Outcome;
  CHECK(OutcomeName(O::kRunning) == "kRunning");
  CHECK(OutcomeName(O::kImported) == "kImported");
  CHECK(OutcomeName(O::kImportedWithSkips) == "kImportedWithSkips");
  CHECK(OutcomeName(O::kNothingImported) == "kNothingImported");
  CHECK(OutcomeName(O::kCancelled) == "kCancelled");
  CHECK(OutcomeName(O::kWrongDisc) == "kWrongDisc");
  CHECK(OutcomeName(O::kGameFilesDiffer) == "kGameFilesDiffer");
  CHECK(OutcomeName(O::kNoSpace) == "kNoSpace");
  CHECK(OutcomeName(O::kNoWriteAccess) == "kNoWriteAccess");
  CHECK(OutcomeName(O::kReadError) == "kReadError");
  CHECK(OutcomeName(O::kSwapPending) == "kSwapPending");
  CHECK(OutcomeName(O::kFailed) == "kFailed");
}

int main() {
  TestStartRefusedWithoutSource();
  TestStartRefusedWhileRunning();
  TestFirstRunSuccessAutoStarts();
  TestDiffersBlocksAutoStart();
  TestCancelKeepsPending();
  TestEachOutcomeLines();
  TestSkipsListedInOrder();
  TestNoSpaceMegabytes();
  TestFormatMB();
  TestMenuRestartNeeded();
  TestMenuFullOrder();
  TestEmptyDetail();
  TestPhaseTexts();
  TestProgressPassthrough();
  TestUserDataCreatedBeforeStart();
  TestUserDataUncreatable();
  TestSavePendingErrorStartsAnyway();
  TestStartReturnsNoRunner();
  TestChoosingAndStages();
  TestDestroyWhileRunning();
  TestDestroyAfterUserCancelCancelsOnce();
  TestReimportNote();
  TestDecideStartup();
  TestLogLines();
  TestConstantsAndNames();
  std::error_code ec;
  fs::remove_all(TempRoot(), ec);
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
