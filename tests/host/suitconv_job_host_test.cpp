// Host test for src/doa4_suitconv_job.cpp (the import job: worker threads, progress, cancel, checked writes, the swap).
// Built and run by run_suitconv_job_test.bat with the converter's compile flags.
//
// Every test runs the real job over a FAKE world: two small synthetic archives (a "disc" chr and a "game" chrone, seeded bytes),
// tables that describe them, and a fake converter that returns seeded bytes per suit, all injected through Hooks. No disc or
// game file is read. A test's files live under %TEMP%\doa4_suitconv_job\<test>\ (the user data folder is its "ud"); the folder
// is deleted when the test STARTS, never at its end, so a failure leaves its evidence.
//
// Groups: bytes_needed, defaults_are_production, full_import, progress_monotonic, cancel_in_each_phase,
// cancel_during_stalled_read (the checking thread; a worker; destroying the job), one_suit_fails, converter_throws, all_fail,
// over_budget_skipped, reference_mismatch_kept, write_fault_mid_way, crash_leaves_no_partial_file, no_space_refused_before_reads,
// no_write_access, wrong_disc, game_files_differ, read_fails_mid_import, swap_blocked_then_completed,
// swap_second_rename_fails_undone, each_swap_state, new_without_marker_refused, python_import_kept,
// second_import_over_kept_python_old, writes_only_under_user_data, manifest_order_independent_of_finish, workers,
// verify_import_folder; and one group gated by DOA4_SUITCONV_DISC and DOA4_SUITCONV_GAME_DIR (real_import: one full import with
// the production defaults, "real_import: SKIPPED (...)" when unset). Last lines: "skipped sections: K", "slowest cancel ...",
// then "PASSED: N checks, 0 failures" or "FAILED: N checks, M failures".

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "../../src/doa4_suitconv_job.h"
#include "../../src/doa4_suits_manifest.h"
#include "../../src/doa4_suits_picks.h"
#include "suitconv_synth.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace doa4::suitconv;
namespace fs = std::filesystem;
namespace dsuits = doa4::suits;
using Clock = std::chrono::steady_clock;

static int g_checks = 0;
static int g_failures = 0;
static int g_skipped = 0;
static int g_printed = 0;
static constexpr int kPrintCap = 40;
static long long g_max_cancel_ms = 0;
static std::string g_max_cancel_name;

static void Check(bool ok, const char* file, int line, const char* expr, const char* fmt = nullptr, ...) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  if (g_printed++ >= kPrintCap) return;
  std::printf("FAIL %s:%d: %s", file, line, expr);
  if (fmt) {
    std::printf("  [");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("]");
  }
  std::printf("\n");
  std::fflush(stdout);
}
#define CK(cond) Check((cond), __FILE__, __LINE__, #cond)
#define CKF(cond, ...) Check((cond), __FILE__, __LINE__, #cond, __VA_ARGS__)

static void Group(const char* name) {
  g_printed = 0;
  std::printf("-- %s\n", name);
  std::fflush(stdout);
}

static void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static long long MsSince(Clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

// ---------------------------------------------------------------------------------------------------------------
// Files: snapshots and listings
// ---------------------------------------------------------------------------------------------------------------

// A folder tree as relative path -> content ("<dir>" for a folder). Missing root: empty.
using Snap = std::map<std::string, std::string>;

static Snap SnapTree(const fs::path& root) {
  Snap s;
  std::error_code ec;
  if (!fs::exists(root, ec)) return s;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string rel = fs::relative(it->path(), root).generic_string();
    std::error_code e2;
    if (it->is_directory(e2)) {
      s[rel] = "<dir>";
    } else {
      HANDLE h = CreateFileW(it->path().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
      std::string data;
      if (h != INVALID_HANDLE_VALUE) {
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0) data.append(buf, n);
        CloseHandle(h);
      } else {
        data = "<unreadable>";
      }
      s[rel] = data;
    }
  }
  return s;
}

static std::string SnapDiff(const Snap& a, const Snap& b) {
  std::string out;
  for (const auto& [k, v] : a) {
    const auto it = b.find(k);
    if (it == b.end()) out += " -" + k;
    else if (it->second != v) out += " ~" + k;
  }
  for (const auto& [k, v] : b)
    if (!a.count(k)) out += " +" + k;
  return out.size() > 300 ? out.substr(0, 300) + "..." : out;
}

#define CK_SAME(a, b, what)                                                                  \
  do {                                                                                       \
    const Snap sa_ = (a), sb_ = (b);                                                         \
    CKF(sa_ == sb_, "%s differs:%s", (what), SnapDiff(sa_, sb_).c_str());                    \
  } while (0)

static std::string ReadText(const fs::path& p) {
  const Snap s = SnapTree(p.parent_path());
  const auto it = s.find(p.filename().generic_string());
  return it == s.end() ? std::string() : it->second;
}

static void WriteText(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  synth::WriteFileBytes(p, std::vector<uint8_t>(text.begin(), text.end()));
}

// ---------------------------------------------------------------------------------------------------------------
// The fake world
// ---------------------------------------------------------------------------------------------------------------

constexpr int kSuits = 34;
static const char* kMarker = ".doa4-suitconv";

// What the fake sources share: read counters, and the two ways a read can go wrong after a test arms it. A stalled read
// is a REAL synchronous ReadFile on the read end of an anonymous pipe whose write end the test holds open.
struct Gate {
  std::atomic<int> reads{0}, disc_reads{0}, game_reads{0};
  std::atomic<bool> stall_armed{false}, fail_armed{false};
  std::atomic<int> stalled{0}, peak_stalled{0};
  HANDLE pipe_r = nullptr;
};

class StallPipe {
 public:
  StallPipe() {
    CreatePipe(&r_, &w_, nullptr, 0);
    ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    backstop_ = std::thread([this] {
      WaitForSingleObject(ev_, 10000);  // the backstop: after 10 s the write end closes and every blocked read fails
      CloseHandle(w_);
    });
  }
  ~StallPipe() {
    SetEvent(ev_);
    backstop_.join();
    CloseHandle(r_);
    CloseHandle(ev_);
  }
  HANDLE read_end() const { return r_; }

 private:
  HANDLE r_ = nullptr, w_ = nullptr, ev_ = nullptr;
  std::thread backstop_;
};

// Contract: Read gives the bytes of `inner`, counts itself, and, once armed, either fails or blocks in a synchronous ReadFile
// on the gate's pipe and then fails (a stalled read never succeeds).
class GatedSource final : public doa4::afs::Source {
 public:
  GatedSource(std::shared_ptr<synth::MemArchive> inner, Gate* gate, std::atomic<int>* side)
      : inner_(std::move(inner)), gate_(gate), side_(side) {}
  uint64_t size() const override { return inner_->size(); }
  bool Read(uint64_t offset, void* dst, size_t n) const override {
    ++gate_->reads;
    ++*side_;
    if (gate_->fail_armed.load()) return false;
    if (gate_->stall_armed.load() && gate_->pipe_r) {
      const int now = ++gate_->stalled;
      int peak = gate_->peak_stalled.load();
      while (now > peak && !gate_->peak_stalled.compare_exchange_weak(peak, now)) {}
      char c = 0;
      DWORD got = 0;
      ReadFile(gate_->pipe_r, &c, 1, &got, nullptr);  // blocks until the write end closes or the thread's I/O is cancelled
      --gate_->stalled;
      return false;
    }
    return inner_->Read(offset, dst, n);
  }

 private:
  std::shared_ptr<synth::MemArchive> inner_;
  Gate* gate_;
  std::atomic<int>* side_;
};

static uint64_t BigFree(const fs::path&) { return uint64_t(1) << 40; }

struct World {
  explicit World(const std::string& test) {
    root = fs::temp_directory_path() / L"doa4_suitconv_job" / fs::path(test);
    std::error_code ec;
    fs::remove_all(root, ec);  // a leftover of this test's own earlier run
    ud = root / "ud";
    doax2 = root / "disc";
    game = root / "game";
    fs::create_directories(ud);
    chr = synth::MakeArchive(ArcId::kChr, {70000, 80000, 90000}, 11);
    chrone = synth::MakeArchive(ArcId::kChrone, {60000, 50000}, 12);
    for (uint32_t i = 0; i < 3; ++i) disc.Add(chr, i);
    for (uint32_t i = 0; i < 2; ++i) gtab.Add(chrone, i);
    chr_mem = std::make_shared<synth::MemArchive>(chr.file);
    chrone_mem = std::make_shared<synth::MemArchive>(chrone.file);
    chr_src = std::make_shared<GatedSource>(chr_mem, &gate, &gate.disc_reads);
    chrone_src = std::make_shared<GatedSource>(chrone_mem, &gate, &gate.game_reads);
    suits.assign(Suits().begin(), Suits().end());
    girls.assign(Girls().begin(), Girls().end());
    for (GirlRecipe& g : girls) g.budget_tpr = g.budget_cat = uint64_t(1) << 20;
    for (int i = 0; i < kSuits; ++i) {
      SuitOutput o;
      o.tpr = synth::Seeded(5000 + uint32_t(i), size_t(2000 + 37 * i));
      o.cat = synth::Seeded(7000 + uint32_t(i), size_t(500 + 11 * i));
      shas.push_back(doa4::gamefiles::Sha1Hex(o.tpr.data(), o.tpr.size()));
      suits[i].out_tpr_size = o.tpr.size();
      suits[i].out_tpr_sha1 = shas.back().c_str();
      shas.push_back(doa4::gamefiles::Sha1Hex(o.cat.data(), o.cat.size()));
      suits[i].out_cat_size = o.cat.size();
      suits[i].out_cat_sha1 = shas.back().c_str();
      outs.push_back(std::move(o));
      delay_ms.push_back(0);
    }
  }

  // The job's request over this world.
  ImportRequest Req(int workers = 2) {
    ImportRequest r;
    r.doax2 = doax2;
    r.game_root = game;
    r.user_data = ud;
    r.workers = workers;
    r.log = [this](std::string_view line) {
      std::lock_guard<std::mutex> lk(log_mu);
      log.emplace_back(line);
    };
    return r;
  }

  // The hooks that put the fake world in the job.
  Hooks MakeHooks() {
    Hooks h;
    h.suits = suits;
    h.girls = girls;
    h.disc = disc.refs;
    h.game = gtab.refs;
    h.read_inputs = [this](InputSet& set, const SuitRecipe&, const GirlRecipe&) {
      SuitInputs in;
      if (read_delay_ms > 0) SleepMs(read_delay_ms);
      in.body_tpr = set.Read(disc.refs[0]);  // a real checked read through the InputSet
      return in;
    };
    h.convert = [this](const SuitRecipe& s, const GirlRecipe&, const SuitInputs&, const CancelToken&,
                       ConvertFailure* f) -> std::optional<SuitOutput> {
      const int idx = int(&s - suits.data());
      const int now = ++converting;
      int peak_now = peak.load();
      while (now > peak_now && !peak.compare_exchange_weak(peak_now, now)) {}
      if (delay_ms[idx] > 0) SleepMs(delay_ms[idx]);
      --converting;
      size_t finished_n = 0;
      {
        std::lock_guard<std::mutex> lk(order_mu);
        finish_order.push_back(idx);
        finished_n = finish_order.size();
      }
      if (cancel_after_finishes > 0 && finished_n == cancel_after_finishes) {
        if (ImportJob* j = job_ptr.load()) j->Cancel();  // a cancel from inside the work
      }
      if (throw_bad_alloc.count(idx)) throw std::bad_alloc();
      if (throw_runtime.count(idx)) throw std::runtime_error("synthetic converter failure");
      if (fail_all || fail_suits.count(idx)) {
        if (f) *f = {"cloth", "synthetic failure of suit " + std::to_string(idx)};
        return std::nullopt;
      }
      return outs[size_t(idx)];
    };
    h.open_inputs = [this](const ImportRequest&, std::span<const EntryRef> d, std::span<const EntryRef> g,
                           std::string* error) {
      InputSet::ArchiveSources da{{ArcId::kChr, chr_src}};
      InputSet::ArchiveSources ga{{ArcId::kChrone, chrone_src}};
      return InputSet::OpenSources(std::move(da), std::move(ga), d, g, error);
    };
    h.free_bytes = [this](const fs::path& p) {
      free_bytes_arg = p;
      return free_value;
    };
    h.on_phase = [this](Phase p) {
      if (phase_hook) phase_hook(p);
    };
    h.on_write = [this](int n) {
      if (on_write_hook) on_write_hook(n);
    };
    return h;
  }

  // The 34 picks as a manifest: the converted suits only (not in `skip`), with the fake outputs' sizes and hashes.
  std::string ExpectedManifest(const std::set<int>& skip = {}) const {
    dsuits::Manifest m;
    m.converter = std::string(kConverterWord);
    const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
    for (int i = 0; i < kSuits; ++i) {
      if (skip.count(i)) continue;
      dsuits::Suit s = picks[size_t(i)];
      s.tpr.size = outs[size_t(i)].tpr.size();
      s.tpr.sha1 = doa4::gamefiles::Sha1Hex(outs[size_t(i)].tpr.data(), outs[size_t(i)].tpr.size());
      s.cat.size = outs[size_t(i)].cat.size();
      s.cat.sha1 = doa4::gamefiles::Sha1Hex(outs[size_t(i)].cat.data(), outs[size_t(i)].cat.size());
      m.suits.push_back(std::move(s));
    }
    return dsuits::FormatManifest(m);
  }

  // The folder `dir` as a complete, verified import of the suits in `which` (picks order), written without the job. With
  // the marker unless `marker` is false.
  void WriteImport(const fs::path& dir, const std::vector<int>& which, bool marker = true) {
    fs::create_directories(dir);
    if (marker) WriteText(dir / kMarker, "doa4 suit importer\n");
    dsuits::Manifest m;
    m.converter = std::string(kConverterWord);
    const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
    for (int i : which) {
      dsuits::Suit s = picks[size_t(i)];
      const SuitOutput& o = outs[size_t(i)];
      synth::WriteFileBytes(dir / s.tpr.name, o.tpr);
      synth::WriteFileBytes(dir / s.cat.name, o.cat);
      s.tpr.size = o.tpr.size();
      s.tpr.sha1 = doa4::gamefiles::Sha1Hex(o.tpr.data(), o.tpr.size());
      s.cat.size = o.cat.size();
      s.cat.sha1 = doa4::gamefiles::Sha1Hex(o.cat.data(), o.cat.size());
      m.suits.push_back(std::move(s));
    }
    WriteText(dir / "manifest.txt", dsuits::FormatManifest(m));
  }

  // A stand-in for a current import (not verified by anything): a marker if ours, two files, a manifest-looking file.
  void WriteCurrent(const fs::path& dir, const std::string& tag, bool marker) {
    fs::create_directories(dir);
    if (marker) WriteText(dir / kMarker, "doa4 suit importer\n");
    WriteText(dir / "manifest.txt", "doax2-suits 1\nconverter " + tag + "\n");
    synth::WriteFileBytes(dir / (tag + "_a.bin"), synth::Seeded(900 + uint32_t(tag.size()), 3000));
    synth::WriteFileBytes(dir / (tag + "_b.bin"), synth::Seeded(950 + uint32_t(tag.size()), 777));
  }

  fs::path Cur() const { return ud / "doax2_suits"; }
  fs::path New() const { return ud / "doax2_suits.new"; }
  fs::path Old() const { return ud / "doax2_suits.old"; }

  std::vector<std::string> LogCopy() {
    std::lock_guard<std::mutex> lk(log_mu);
    return log;
  }
  int LogCount(const std::string& needle) {
    int n = 0;
    for (const std::string& l : LogCopy())
      if (l.find(needle) != std::string::npos) ++n;
    return n;
  }

  fs::path root, ud, doax2, game;
  Gate gate;
  synth::Archive chr, chrone;
  synth::Table disc, gtab;
  std::shared_ptr<synth::MemArchive> chr_mem, chrone_mem;
  std::shared_ptr<GatedSource> chr_src, chrone_src;
  std::vector<SuitRecipe> suits;
  std::vector<GirlRecipe> girls;
  std::deque<std::string> shas;
  std::vector<SuitOutput> outs;
  // behaviour of the fake converter and the other hooks
  std::vector<int> delay_ms;
  int read_delay_ms = 0;  // the fake read_inputs waits this long before its read
  std::set<int> fail_suits, throw_bad_alloc, throw_runtime;
  bool fail_all = false;
  std::atomic<int> converting{0}, peak{0};
  std::mutex order_mu;
  std::vector<int> finish_order;
  std::mutex log_mu;
  std::vector<std::string> log;
  std::function<void(Phase)> phase_hook;
  std::function<void(int)> on_write_hook;
  std::atomic<ImportJob*> job_ptr{nullptr};  // set by a test that cancels from inside the fake converter
  size_t cancel_after_finishes = 0;
  uint64_t free_value = uint64_t(1) << 40;
  fs::path free_bytes_arg;
};

static bool WaitDone(ImportJob& job, int ms) {
  const auto t0 = Clock::now();
  while (!job.done()) {
    if (MsSince(t0) > ms) return false;
    SleepMs(1);
  }
  return true;
}

// Runs a job to its end (60 s limit; on a timeout it cancels and records a failed check).
static ImportResult RunJob(World& w, Hooks h, int workers = 2) {
  auto job = ImportJob::StartForTest(w.Req(workers), std::move(h));
  const bool ok = WaitDone(*job, 60000);
  CK(ok);
  if (!ok) job->Cancel();
  WaitDone(*job, 15000);
  return job->result();
}

static bool Is(const ImportResult& r, Outcome o) { return r.outcome == o; }
static bool Has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

static std::vector<std::string> Lines(const std::string& text) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < text.size()) {
    const size_t e = text.find('\n', i);
    out.push_back(text.substr(i, e == std::string::npos ? std::string::npos : e - i));
    if (e == std::string::npos) break;
    i = e + 1;
  }
  return out;
}

static int SuitLines(const std::string& manifest) {
  int n = 0;
  for (const std::string& l : Lines(manifest))
    if (l.rfind("suit ", 0) == 0) ++n;
  return n;
}

// `dir` holds exactly the converted suits (all but `skip`), the manifest, the marker, and nothing else; every file has the
// fake converter's bytes. Reports one failed check naming the first problem.
static void CheckImportFolder(const World& w, const fs::path& dir, const std::set<int>& skip, const char* what) {
  const Snap s = SnapTree(dir);
  const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
  std::set<std::string> want = {"manifest.txt", kMarker};
  std::string bad;
  size_t suits_expected = 0;
  for (int i = 0; i < kSuits; ++i) {
    if (skip.count(i)) continue;
    ++suits_expected;
    const dsuits::Suit& p = picks[size_t(i)];
    want.insert(p.tpr.name);
    want.insert(p.cat.name);
    const auto t = s.find(p.tpr.name);
    const auto c = s.find(p.cat.name);
    if (t == s.end() || t->second != std::string(w.outs[size_t(i)].tpr.begin(), w.outs[size_t(i)].tpr.end())) bad += " tpr" + std::to_string(i);
    if (c == s.end() || c->second != std::string(w.outs[size_t(i)].cat.begin(), w.outs[size_t(i)].cat.end())) bad += " cat" + std::to_string(i);
  }
  CKF(suits_expected > 0, "%s: nothing expected", what);
  std::string extra;
  for (const auto& [k, v] : s)
    if (!want.count(k)) extra += " " + k;
  CKF(bad.empty(), "%s: wrong or missing files:%s", what, bad.c_str());
  CKF(extra.empty(), "%s: unexpected entries:%s", what, extra.c_str());
  CKF(s.count(kMarker) == 1, "%s: no marker", what);
  CKF(ReadText(dir / "manifest.txt") == w.ExpectedManifest(skip), "%s: manifest text differs", what);
}

static bool Exists(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p, ec);
}

static bool HasTmp(const fs::path& root) {
  for (const auto& [k, v] : SnapTree(root))
    if (k.size() >= 4 && k.compare(k.size() - 4, 4, ".tmp") == 0) return true;
  return false;
}

// ---------------------------------------------------------------------------------------------------------------
// bytes_needed, defaults_are_production
// ---------------------------------------------------------------------------------------------------------------

static std::string ReferenceManifestText(std::span<const SuitRecipe> suits) {
  dsuits::Manifest m;
  m.converter = std::string(kConverterWord);
  const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
  for (size_t i = 0; i < picks.size(); ++i) {
    dsuits::Suit s = picks[i];
    s.tpr.size = suits[i].out_tpr_size;
    s.tpr.sha1 = suits[i].out_tpr_sha1;
    s.cat.size = suits[i].out_cat_size;
    s.cat.sha1 = suits[i].out_cat_sha1;
    m.suits.push_back(std::move(s));
  }
  return dsuits::FormatManifest(m);
}

static void TestBytesNeeded() {
  Group("bytes_needed");
  uint64_t refs = 0;
  for (const SuitRecipe& s : Suits()) refs += s.out_tpr_size + s.out_cat_size;
  const std::string manifest = ReferenceManifestText(Suits());
  // The two pins: the reference manifest is 5,752 bytes, and the space an import needs is 381,096,088 (the 68 reference
  // sizes, that manifest once, and 64 MiB).
  CKF(manifest.size() == 5752, "manifest %zu", manifest.size());
  CKF(BytesNeeded() == 381096088ull, "BytesNeeded %llu", (unsigned long long)BytesNeeded());
  CK(BytesNeeded() == refs + manifest.size() + (uint64_t(64) << 20));
  CK(BytesNeeded() != 381101840ull);
  // The table overload follows the table it is given (a fake table has other sizes, hence another manifest length).
  World w("bytes_needed");
  uint64_t fake_refs = 0;
  for (const SuitRecipe& s : w.suits) fake_refs += s.out_tpr_size + s.out_cat_size;
  const std::string fake_manifest = ReferenceManifestText(w.suits);
  CK(fake_manifest.size() != manifest.size());
  CKF(BytesNeeded(w.suits) == fake_refs + fake_manifest.size() + (uint64_t(64) << 20), "fake %llu",
      (unsigned long long)BytesNeeded(w.suits));
  CK(BytesNeeded(w.suits) != BytesNeeded());
}

static void TestDefaultsAreProduction() {
  Group("defaults_are_production");
  World w("defaults_are_production");
  // 1. Everything defaulted, through Start: the real tables, the real disk, InputSet::Open over paths that do not exist.
  const Snap before = SnapTree(w.root);
  auto job = ImportJob::Start(w.Req());
  CK(WaitDone(*job, 30000));
  const ImportResult r = job->result();
  CKF(Is(r, Outcome::kWrongDisc), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(!r.detail.empty());
  CKF(r.bytes_needed == 381096088ull, "bytes_needed %llu", (unsigned long long)r.bytes_needed);
  CK(r.bytes_free > 0);  // the production free_bytes ran
  CK_SAME(before, SnapTree(w.root), "the root after a refused default import");
  // 2. The fake tables with the production read and convert hooks: the real ReadSuitInputs cannot read the real entries out of
  // a fake archive, which ends the import (an unreadable input ends the whole import): kReadError, nothing left behind.
  World v("defaults_are_production_reads");
  Hooks h = v.MakeHooks();
  h.read_inputs = nullptr;
  h.convert = nullptr;
  const Snap before2 = SnapTree(v.root);
  const ImportResult r2 = RunJob(v, std::move(h));
  CKF(Is(r2, Outcome::kReadError), "outcome %d (%s)", int(r2.outcome), r2.detail.c_str());
  CK(!r2.detail.empty());
  CK_SAME(before2, SnapTree(v.root), "the root after the production read hook failed");
}

// ---------------------------------------------------------------------------------------------------------------
// full_import, progress_monotonic
// ---------------------------------------------------------------------------------------------------------------

static void TestFullImport() {
  Group("full_import");
  World w("full_import");
  const Snap before = SnapTree(w.ud);
  CK(before.empty());
  const ImportResult r = RunJob(w, w.MakeHooks(), 3);
  CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(r.skipped.empty());
  CK(r.differs_from_reference.empty());
  CK(r.mismatched_inputs.empty());
  CK(r.bytes_needed == BytesNeeded(w.suits));
  CK(w.free_bytes_arg == w.ud);
  CheckImportFolder(w, w.Cur(), {}, "the import");
  CK(!Exists(w.New()) && !Exists(w.Old()));
  CK(!HasTmp(w.ud));
  CK(SuitLines(ReadText(w.Cur() / "manifest.txt")) == 34);
  // A healthy run logs no recovery line and no reference difference (the needles of the later tests).
  CK(w.LogCount("swap recovery") == 0);
  CK(w.LogCount("differs from the reference") == 0);
  CK(w.LogCount("old import") == 0);
  CK(w.gate.disc_reads.load() > 0 && w.gate.game_reads.load() > 0);
  // The import's own verification agrees with the test's.
  CK(VerifyImportFolder(w.Cur()).empty());
}

static void TestProgressMonotonic() {
  Group("progress_monotonic");
  World w("progress_monotonic");
  // Each phase lasts at least 20 ms, so the sampler sees every one of them; conversion takes 3 ms a suit.
  w.phase_hook = [](Phase) { SleepMs(20); };
  for (int& d : w.delay_ms) d = 3;
  auto job = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
  std::vector<Progress> samples;
  while (!job->done()) {
    samples.push_back(job->progress());
    SleepMs(1);
  }
  samples.push_back(job->progress());
  const ImportResult r = job->result();
  CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(samples.size() >= 30, "%zu samples", samples.size());  // the fixture is vacuous without samples
  bool mono = true, phases_ordered = true, counts_ordered = true, proportional = true;
  int late_samples = 0;
  constexpr double kCheckW = 0.15, kConvW = 0.80;
  std::set<int> phases;
  double first_converting = -1, max_during_converting = 0, min_during_converting = 2;
  for (size_t i = 0; i < samples.size(); ++i) {
    phases.insert(int(samples[i].phase));
    if (i > 0) {
      if (samples[i].fraction < samples[i - 1].fraction) mono = false;
      if (int(samples[i].phase) < int(samples[i - 1].phase)) phases_ordered = false;
      if (samples[i].suits_done < samples[i - 1].suits_done || samples[i].bytes_done < samples[i - 1].bytes_done)
        counts_ordered = false;
    }
    if (samples[i].phase == Phase::kConverting) {
      // Conversion is 80% of the bar after the checks' 15%: with d suits done the fraction is at least 0.15 + 0.80 (d-1)/34
      // (a sample may fall between a suit's count and its fraction). A fraction that restarts per phase and is only kept
      // from falling by a running maximum would sit at 0.15 until its own 0.80 x d/34 overtook it.
      const double floor_f = kCheckW + kConvW * double((std::max)(samples[i].suits_done - 1, 0)) / 34.0;
      if (samples[i].fraction < floor_f - 1e-9) proportional = false;
      if (samples[i].suits_done >= 10) ++late_samples;
      if (first_converting < 0) first_converting = samples[i].fraction;
      max_during_converting = (std::max)(max_during_converting, samples[i].fraction);
      min_during_converting = (std::min)(min_during_converting, samples[i].fraction);
    }
  }
  CK(mono);
  CK(proportional);
  CKF(late_samples >= 5, "%d samples with 10 or more suits done", late_samples);
  CK(phases_ordered);
  CK(counts_ordered);
  CKF(phases.size() == 7, "%zu distinct phases seen", phases.size());
  // The weights: checks 15%, so conversion starts at 0.15 and not before; it ends below 0.95; writing and the swap last.
  // (NOT a per-phase fraction: that would restart near 0 when conversion begins.)
  CKF(first_converting >= 0.15 - 1e-9, "first converting fraction %.4f", first_converting);
  CKF(max_during_converting < 0.95 + 1e-9 && max_during_converting > min_during_converting, "converting %.4f..%.4f",
      min_during_converting, max_during_converting);
  const Progress last = samples.back();
  CK(last.phase == Phase::kDone);
  CK(last.fraction == 1.0);
  CK(last.suits_done == 34 && last.suits_total == 34);
  CK(last.bytes_total > 0 && last.bytes_done == last.bytes_total);
  CK(samples.front().fraction < 0.15);
}

// ---------------------------------------------------------------------------------------------------------------
// cancel
// ---------------------------------------------------------------------------------------------------------------

static const char* PhaseName(Phase p) {
  switch (p) {
    case Phase::kCheckingSpace: return "space";
    case Phase::kCheckingDisc: return "disc";
    case Phase::kCheckingGame: return "game";
    case Phase::kConverting: return "converting";
    case Phase::kWriting: return "writing";
    case Phase::kSwapping: return "swapping";
    case Phase::kDone: return "done";
  }
  return "?";
}

static void NoteCancel(long long ms, const std::string& name) {
  std::printf("cancel latency %-34s %lld ms\n", name.c_str(), ms);
  if (ms > g_max_cancel_ms) {
    g_max_cancel_ms = ms;
    g_max_cancel_name = name;
  }
}

static void TestCancelInEachPhase() {
  Group("cancel_in_each_phase");
  const Phase phases[] = {Phase::kCheckingSpace, Phase::kCheckingDisc, Phase::kCheckingGame,
                          Phase::kConverting,    Phase::kWriting,      Phase::kSwapping};
  for (Phase p : phases) {
    World w(std::string("cancel_in_") + PhaseName(p));
    w.WriteCurrent(w.Cur(), "cur", true);
    for (int& d : w.delay_ms) d = 50;  // a converter that sleeps 50 ms per suit
    w.phase_hook = [p](Phase now) {
      if (now == p) SleepMs(50);  // the phase lasts long enough to be hit
    };
    const Snap before = SnapTree(w.ud);
    CK(!before.empty());
    auto job = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
    const auto t_wait = Clock::now();
    while (job->progress().phase != p && !job->done() && MsSince(t_wait) < 10000) SleepMs(1);
    CKF(job->progress().phase == p, "phase %s was not reached", PhaseName(p));
    const auto t0 = Clock::now();
    job->Cancel();
    const bool ok = WaitDone(*job, 15000);
    const long long ms = MsSince(t0);
    NoteCancel(ms, std::string("in phase ") + PhaseName(p));
    CKF(ok && ms <= 1000, "phase %s: done within 1 s: %lld ms", PhaseName(p), ms);
    const ImportResult r = job->result();
    CKF(Is(r, Outcome::kCancelled), "phase %s: outcome %d (%s)", PhaseName(p), int(r.outcome), r.detail.c_str());
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after the cancel (the current import byte-identical, nothing else)");
  }
}

// Arms a stalled read when `at` begins; waits until `want` threads are blocked in the real ReadFile; cancels (or destroys the
// job); the cancel must finish within a second and give kCancelled. variant: 0 the checking thread, 1 workers, 2 destroy.
static void StalledRead(const char* name, Phase at, int workers, int want, bool destroy) {
  StallPipe pipe;  // declared before the world and the job: they go first on a failure, the backstop closes the pipe
  World w(name);
  w.gate.pipe_r = pipe.read_end();
  w.WriteCurrent(w.Cur(), "cur", true);
  w.phase_hook = [&w, at](Phase now) {
    if (now == at) w.gate.stall_armed = true;
  };
  const Snap before = SnapTree(w.ud);
  auto job = ImportJob::StartForTest(w.Req(workers), w.MakeHooks());
  const auto t_wait = Clock::now();
  while (w.gate.stalled.load() < want && MsSince(t_wait) < 8000) SleepMs(1);
  CKF(w.gate.stalled.load() == want, "%s: %d thread(s) blocked in ReadFile, wanted %d", name, w.gate.stalled.load(), want);
  CK(!job->done());
  const auto t0 = Clock::now();
  if (destroy) {
    job.reset();
    const long long ms = MsSince(t0);
    NoteCancel(ms, std::string(name) + " (destroy)");
    CKF(ms <= 1000, "%s: the destructor returned within 1 s: %lld ms", name, ms);
  } else {
    job->Cancel();
    const bool ok = WaitDone(*job, 15000);
    const long long ms = MsSince(t0);
    NoteCancel(ms, name);
    CKF(ok && ms <= 1000, "%s: done within 1 s: %lld ms", name, ms);
    const ImportResult r = job->result();
    CKF(Is(r, Outcome::kCancelled), "%s: outcome %d (%s), never kReadError", name, int(r.outcome), r.detail.c_str());
  }
  CKF(w.gate.stalled.load() == 0, "%s: %d still blocked", name, w.gate.stalled.load());
  CK(!Exists(w.New()));
  CK_SAME(before, SnapTree(w.ud), name);
}

// The cancel arrives BEFORE the workers' reads start (they are still in the wait ahead of the read), so the read blocks after
// the cancel: a single CancelSynchronousIo made at Cancel time would hit nothing. The call has to be repeated.
static void StalledReadStartsAfterCancel() {
  StallPipe pipe;
  World w("stalled_read_starts_after_cancel");
  w.gate.pipe_r = pipe.read_end();
  w.read_delay_ms = 250;
  w.phase_hook = [&w](Phase now) {
    if (now == Phase::kConverting) w.gate.stall_armed = true;
  };
  w.WriteCurrent(w.Cur(), "cur", true);
  const Snap before = SnapTree(w.ud);
  auto job = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
  const auto t_wait = Clock::now();
  while (job->progress().phase != Phase::kConverting && MsSince(t_wait) < 8000) SleepMs(1);
  SleepMs(40);
  CK(w.gate.peak_stalled.load() == 0);  // nobody is blocked yet: the workers are still waiting ahead of their reads
  const auto t0 = Clock::now();
  job->Cancel();
  const bool ok = WaitDone(*job, 15000);
  const long long ms = MsSince(t0);
  NoteCancel(ms, "stalled read starting after Cancel");
  CKF(ok && ms <= 1000, "done within 1 s: %lld ms", ms);
  CK(w.gate.peak_stalled.load() >= 1);  // the read did start, after the cancel, and was cancelled
  CKF(Is(job->result(), Outcome::kCancelled), "outcome %d", int(job->result().outcome));
  CK(!Exists(w.New()));
  CK_SAME(before, SnapTree(w.ud), "user data");
}

static void TestCancelDuringStalledRead() {
  Group("cancel_during_stalled_read");
  StalledReadStartsAfterCancel();
  StalledRead("stalled_in_check_disc", Phase::kCheckingDisc, 2, 1, false);
  StalledRead("stalled_in_check_game", Phase::kCheckingGame, 2, 1, false);
  StalledRead("stalled_in_workers", Phase::kConverting, 3, 3, false);  // every worker is inside its ReadSuitInputs
  StalledRead("stalled_then_destroyed", Phase::kCheckingDisc, 2, 1, true);
  StalledRead("stalled_workers_destroyed", Phase::kConverting, 2, 2, true);
}

// ---------------------------------------------------------------------------------------------------------------
// per-suit outcomes
// ---------------------------------------------------------------------------------------------------------------

static std::string LabelOf(int i) { return dsuits::PickSuits()[size_t(i)].Label(); }

static void TestOneSuitFails() {
  Group("one_suit_fails");
  World w("one_suit_fails");
  w.fail_suits = {7};
  const ImportResult r = RunJob(w, w.MakeHooks(), 3);
  CKF(Is(r, Outcome::kImportedWithSkips), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(r.skipped.size() == 1, "%zu skipped", r.skipped.size());
  if (r.skipped.size() == 1) {
    CK(r.skipped[0].label == LabelOf(7));
    CK(r.skipped[0].stage == "cloth");
    CK(r.skipped[0].why == "synthetic failure of suit 7");
  }
  CK(SuitLines(ReadText(w.Cur() / "manifest.txt")) == 33);
  CheckImportFolder(w, w.Cur(), {7}, "33 suits");
  CK(!Exists(w.Cur() / dsuits::PickSuits()[7].tpr.name));
  CK(!Exists(w.New()) && !HasTmp(w.ud));
}

static void TestConverterThrows() {
  Group("converter_throws");
  World w("converter_throws");
  w.throw_bad_alloc = {3};
  w.throw_runtime = {5};
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r, Outcome::kImportedWithSkips), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(r.skipped.size() == 2, "%zu skipped", r.skipped.size());
  if (r.skipped.size() == 2) {
    CK(r.skipped[0].label == LabelOf(3) && r.skipped[1].label == LabelOf(5));  // picks order
    CK(r.skipped[0].why == "out of memory");
    CK(r.skipped[1].why == "synthetic converter failure");
    CK(r.skipped[0].stage == "convert" && r.skipped[1].stage == "convert");
  }
  CheckImportFolder(w, w.Cur(), {3, 5}, "32 suits");
}

static void TestAllFail() {
  Group("all_fail");
  World w("all_fail");
  w.WriteCurrent(w.Cur(), "cur", true);
  w.fail_all = true;
  const Snap before = SnapTree(w.ud);
  const ImportResult r = RunJob(w, w.MakeHooks(), 3);
  CKF(Is(r, Outcome::kNothingImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(r.skipped.size() == 34, "%zu skipped", r.skipped.size());
  if (r.skipped.size() == 34) CK(r.skipped.front().label == LabelOf(0) && r.skipped.back().label == LabelOf(33));
  CK(!Exists(w.New()) && !Exists(w.Old()));
  CK_SAME(before, SnapTree(w.ud), "user data after an import that converted nothing (nothing renamed)");
}

static void TestOverBudgetSkipped() {
  Group("over_budget_skipped");
  World w("over_budget_skipped");
  // vks is suits 0-3 (.tpr sizes 2000, 2037, 2074, 2111); vay is suits 4-6 (.cat sizes 544, 555, 566). A file is over budget
  // when it is LARGER than the budget: suit 2 (== budget) is kept, suit 3 skipped; suit 5 kept, suit 6 skipped.
  CK(w.outs[2].tpr.size() == 2074 && w.outs[3].tpr.size() == 2111 && w.outs[5].cat.size() == 555 &&
     w.outs[6].cat.size() == 566);
  CK(std::string(w.suits[3].girl) == "vks" && std::string(w.suits[6].girl) == "vay");
  for (GirlRecipe& g : w.girls) {
    if (std::string(g.girl) == "vks") g.budget_tpr = 2074;
    if (std::string(g.girl) == "vay") g.budget_cat = 555;
  }
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r, Outcome::kImportedWithSkips), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(r.skipped.size() == 2, "%zu skipped", r.skipped.size());
  if (r.skipped.size() == 2) {
    CK(r.skipped[0].label == LabelOf(3) && r.skipped[1].label == LabelOf(6));
    CK(r.skipped[0].stage == "budget" && r.skipped[1].stage == "budget");
    CK(r.skipped[0].why.find("2111") != std::string::npos);
    CK(r.skipped[1].why.find("566") != std::string::npos);
  }
  CheckImportFolder(w, w.Cur(), {3, 6}, "32 suits");
}

static void TestReferenceMismatchKept() {
  Group("reference_mismatch_kept");
  World w("reference_mismatch_kept");
  // Suit 11: the .cat differs from the reference; suit 12: the .tpr differs; every other suit equals it.
  w.shas.push_back(std::string(40, 'a'));
  w.suits[11].out_cat_sha1 = w.shas.back().c_str();
  w.shas.push_back(std::string(40, 'b'));
  w.suits[12].out_tpr_sha1 = w.shas.back().c_str();
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(r.skipped.empty());
  CKF(r.differs_from_reference.size() == 2, "%zu differ", r.differs_from_reference.size());
  if (r.differs_from_reference.size() == 2) {
    CK(r.differs_from_reference[0] == LabelOf(11) && r.differs_from_reference[1] == LabelOf(12));
  }
  const std::string cat_sha = doa4::gamefiles::Sha1Hex(w.outs[11].cat.data(), w.outs[11].cat.size());
  const std::string tpr_sha = doa4::gamefiles::Sha1Hex(w.outs[12].tpr.data(), w.outs[12].tpr.size());
  CK(w.LogCount("suit " + LabelOf(11) + ": output differs from the reference build (" + cat_sha + ")") == 1);
  CK(w.LogCount("suit " + LabelOf(12) + ": output differs from the reference build (" + tpr_sha + ")") == 1);
  CKF(w.LogCount("differs from the reference") == 2, "%d log lines", w.LogCount("differs from the reference"));
  CheckImportFolder(w, w.Cur(), {}, "all 34 kept");  // the suits are in the folder and the manifest, with their own hashes
}

// ---------------------------------------------------------------------------------------------------------------
// writes
// ---------------------------------------------------------------------------------------------------------------

static void TestWriteFaultMidWay() {
  Group("write_fault_mid_way");
  // The writes of a full import, counted: the marker (1), two per suit (68), the manifest (70). "The 20th file" is the 20th of
  // these; the marker counts.
  {
    World w("write_fault_20");
    w.WriteCurrent(w.Cur(), "cur", true);
    Hooks h = w.MakeHooks();
    h.write_fault_at = 20;
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, std::move(h), 3);
    CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CKF(r.detail.find("#20") != std::string::npos, "detail: %s", r.detail.c_str());
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after a failed write (old import intact, .new gone)");
  }
  {
    World w("write_fault_70");  // the 70th write is the manifest: the last one
    Hooks h = w.MakeHooks();
    h.write_fault_at = 70;
    const ImportResult r = RunJob(w, std::move(h), 3);
    CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(!Exists(w.New()) && !Exists(w.Cur()));
  }
  {
    World w("write_fault_71");  // an import has 70 writes: a fault on the 71st never fires
    Hooks h = w.MakeHooks();
    h.write_fault_at = 71;
    const ImportResult r = RunJob(w, std::move(h), 3);
    CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  }
}

static void TestCrashLeavesNoPartialFile() {
  Group("crash_leaves_no_partial_file");
  for (int n : {20, 70}) {
    World w("crash_at_" + std::to_string(n));
    w.WriteCurrent(w.Cur(), "cur", true);
    const Snap cur_before = SnapTree(w.Cur());
    Hooks h = w.MakeHooks();
    h.crash_at_write = n;
    const ImportResult r = RunJob(w, std::move(h), 2);
    CKF(Is(r, Outcome::kFailed), "n=%d: outcome %d (%s)", n, int(r.outcome), r.detail.c_str());
    // What a crash leaves in .new: the marker, no manifest (it is written last), every suit file under its final name
    // complete, and the interrupted write only under a .tmp name.
    const Snap s = SnapTree(w.New());
    CKF(!s.empty() && s.count(kMarker) == 1, "n=%d: .new has %zu entries", n, s.size());
    CK(s.count("manifest.txt") == 0);
    const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
    int complete = 0;
    for (int i = 0; i < kSuits; ++i) {
      for (int part = 0; part < 2; ++part) {
        const std::string& name = part == 0 ? picks[size_t(i)].tpr.name : picks[size_t(i)].cat.name;
        const std::vector<uint8_t>& want = part == 0 ? w.outs[size_t(i)].tpr : w.outs[size_t(i)].cat;
        const auto it = s.find(name);
        if (it == s.end()) continue;
        ++complete;
        CKF(it->second == std::string(want.begin(), want.end()), "n=%d: %s is truncated under its final name", n, name.c_str());
      }
    }
    CKF(complete >= (n == 70 ? 68 : 8), "n=%d: %d complete files", n, complete);
    int halves = 0;
    for (const auto& [k, v] : s)
      if (k.size() > 4 && k.compare(k.size() - 4, 4, ".tmp") == 0) ++halves;
    CKF(halves >= 1, "n=%d: %d .tmp files (the crash truncated none)", n, halves);
    if (n == 70) CK(s.count("manifest.txt.tmp") == 1 && s.at("manifest.txt.tmp").size() > 0);
    CK_SAME(cur_before, SnapTree(w.Cur()), "the current import");
    // Recovery from this state: .new has no manifest, so it is deleted; the current import stays.
    const std::string why = CompletePendingSwap(w.ud);
    CKF(why.empty(), "n=%d: %s", n, why.c_str());
    CK(!Exists(w.New()));
    CK_SAME(cur_before, SnapTree(w.Cur()), "the current import after recovery");
  }
}

static void TestNoSpaceRefusedBeforeReads() {
  Group("no_space_refused_before_reads");
  {
    World w("no_space");
    w.WriteCurrent(w.Cur(), "cur", true);
    const uint64_t need = BytesNeeded(w.suits);
    w.free_value = need - 1;  // one byte short
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, w.MakeHooks());
    CKF(Is(r, Outcome::kNoSpace), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(r.bytes_needed == need && r.bytes_free == need - 1);
    CKF(w.gate.reads.load() == 0, "%d reads", w.gate.reads.load());  // counted by the fake sources
    CK(w.free_bytes_arg == w.ud);
    CK_SAME(before, SnapTree(w.ud), "user data after the refusal");
  }
  {
    World w("no_space_exact");  // exactly enough is enough
    w.free_value = BytesNeeded(w.suits);
    const ImportResult r = RunJob(w, w.MakeHooks());
    CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(r.bytes_free == w.free_value);
  }
}

static void TestNoWriteAccess() {
  Group("no_write_access");
  World w("no_write_access");
  std::error_code ec;
  fs::remove_all(w.ud, ec);
  WriteText(w.ud, "this is a file, so nothing can be created inside it\n");  // user data that cannot be written
  const Snap before = SnapTree(w.root);
  const ImportResult r = RunJob(w, w.MakeHooks());
  CKF(Is(r, Outcome::kNoWriteAccess), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(!r.detail.empty());
  CKF(w.gate.reads.load() == 0, "%d reads", w.gate.reads.load());
  CK_SAME(before, SnapTree(w.root), "the root after the refusal");
}

// ---------------------------------------------------------------------------------------------------------------
// input checks
// ---------------------------------------------------------------------------------------------------------------

// The reads opening the game side costs (its archive's table of contents): what a world's game sources have served before
// any entry is checked. Measured, not assumed.
static int GameReadsToOpen() {
  World v("open_only");
  std::string error;
  auto set = v.MakeHooks().open_inputs(ImportRequest{}, v.disc.refs, v.gtab.refs, &error);
  CKF(set != nullptr, "%s", error.c_str());
  return v.gate.game_reads.load();
}

static void TestWrongDisc() {
  Group("wrong_disc");
  const int toc_reads = GameReadsToOpen();
  CK(toc_reads > 0);
  World w("wrong_disc");
  w.WriteCurrent(w.Cur(), "cur", true);
  w.WriteImport(w.New(), {0, 1, 2, 3, 4});  // a leftover verified import: nothing may touch it before the checks pass
  CK(VerifyImportFolder(w.New()).empty());
  uint64_t off1 = 0;
  {
    std::string toc_error;
    const auto toc = doa4::afs::ReadToc(*w.chr_mem, &toc_error);
    CKF(toc.has_value(), "%s", toc_error.c_str());
    if (toc) off1 = toc->offsets[1];
  }
  w.chr_mem->bytes()[off1 + 3] ^= 0x01;  // entry "chr 1"
  const Snap before = SnapTree(w.ud);
  const ImportResult r = RunJob(w, w.MakeHooks());
  CKF(Is(r, Outcome::kWrongDisc), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(r.mismatched_inputs.size() == 1 && r.mismatched_inputs[0] == "chr 1", "mismatched %zu", r.mismatched_inputs.size());
  CKF(w.gate.game_reads.load() == toc_reads, "the game files were read (%d reads, %d to open)", w.gate.game_reads.load(),
      toc_reads);  // CheckDisc stops the import: no entry of the game files is read
  CK(w.gate.disc_reads.load() > 0);
  CK_SAME(before, SnapTree(w.ud), "user data after a refusal (the leftover .new untouched)");
}

static void TestGameFilesDiffer() {
  Group("game_files_differ");
  World w("game_files_differ");
  w.WriteCurrent(w.Cur(), "cur", true);
  w.WriteImport(w.New(), {0, 1, 2, 3, 4});
  std::string toc_error;
  const auto toc = doa4::afs::ReadToc(*w.chrone_mem, &toc_error);
  CKF(toc.has_value(), "%s", toc_error.c_str());
  if (toc) w.chrone_mem->bytes()[toc->offsets[0] + 9] ^= 0x10;
  const Snap before = SnapTree(w.ud);
  const ImportResult r = RunJob(w, w.MakeHooks());
  CKF(Is(r, Outcome::kGameFilesDiffer), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(r.mismatched_inputs.size() == 1 && r.mismatched_inputs[0] == "chrone 0", "mismatched %zu", r.mismatched_inputs.size());
  CK(w.gate.disc_reads.load() > 0 && w.gate.game_reads.load() > GameReadsToOpen());  // the disc was checked first, and was fine
  CK_SAME(before, SnapTree(w.ud), "user data after a refusal (the leftover .new untouched)");
}

static void TestReadFailsMidImport() {
  Group("read_fails_mid_import");
  {
    World w("read_fails");
    w.WriteCurrent(w.Cur(), "cur", true);
    w.phase_hook = [&w](Phase p) {
      if (p == Phase::kConverting) w.gate.fail_armed = true;  // every read fails from here on
    };
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, w.MakeHooks(), 3);
    CKF(Is(r, Outcome::kReadError), "outcome %d (%s)", int(r.outcome), r.detail.c_str());  // not a per-suit skip
    CK(r.skipped.size() < 34);
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after a failed read");
  }
  {
    World w("read_changes");
    w.WriteCurrent(w.Cur(), "cur", true);
    w.phase_hook = [&w](Phase p) {
      if (p == Phase::kConverting) w.chr_mem->bytes()[0x800 + 17] ^= 0x01;  // entry 0 changes after it was checked
    };
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, w.MakeHooks(), 3);
    CKF(Is(r, Outcome::kReadError), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after changed input bytes");
  }
}

// ---------------------------------------------------------------------------------------------------------------
// the swap
// ---------------------------------------------------------------------------------------------------------------

static void TestSwapBlockedThenCompleted() {
  Group("swap_blocked_then_completed");
  World w("swap_blocked");
  w.WriteCurrent(w.Cur(), "cur", true);
  // The game holds the current import's files open as LoadImport does: shared for reading only.
  std::shared_ptr<doa4::afs::FileSource> held = doa4::afs::FileSource::Open(w.Cur() / "cur_a.bin");
  CK(held != nullptr);
  const Snap cur_before = SnapTree(w.Cur());
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r, Outcome::kSwapPending), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(!r.detail.empty());
  CK_SAME(cur_before, SnapTree(w.Cur()), "the current import while blocked");
  CK(!Exists(w.Old()));
  CK(Exists(w.New()));
  CK(VerifyImportFolder(w.New()).empty());  // .new is complete and verified
  CK(SuitLines(ReadText(w.New() / "manifest.txt")) == 34);
  // While the file is still open the swap is still blocked, and nothing changes.
  const Snap all_before = SnapTree(w.ud);
  const std::string still = CompletePendingSwap(w.ud);
  CK(!still.empty());
  CK_SAME(all_before, SnapTree(w.ud), "user data after a blocked CompletePendingSwap");
  held.reset();  // the game lets go
  const std::string why = CompletePendingSwap(w.ud);
  CKF(why.empty(), "%s", why.c_str());
  CheckImportFolder(w, w.Cur(), {}, "the new import in place");
  CK(!Exists(w.New()) && !Exists(w.Old()));
}

static void TestSwapSecondRenameFailsUndone() {
  Group("swap_second_rename_fails_undone");
  World w("swap_second_fails");
  w.WriteCurrent(w.Cur(), "cur", true);
  std::shared_ptr<doa4::afs::FileSource> held;
  // At the start of the swap a file inside .new is opened for reading: the first rename (current -> .old) succeeds, the
  // second (.new -> current) is refused because of the open file.
  w.phase_hook = [&w, &held](Phase p) {
    if (p == Phase::kSwapping) held = doa4::afs::FileSource::Open(w.New() / "manifest.txt");
  };
  const Snap cur_before = SnapTree(w.Cur());
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CK(held != nullptr);
  CKF(Is(r, Outcome::kSwapPending), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK_SAME(cur_before, SnapTree(w.Cur()), "the current import (the first rename undone)");
  CK(!Exists(w.Old()));
  CK(Exists(w.New()));
  CK(VerifyImportFolder(w.New()).empty());
  held.reset();
  const std::string why = CompletePendingSwap(w.ud);
  CKF(why.empty(), "%s", why.c_str());
  CheckImportFolder(w, w.Cur(), {}, "the new import in place");
  CK(!Exists(w.New()) && !Exists(w.Old()));
}

// One row of the swap table (the five rows of the header's table), set up in `w`.
static void SetupRow(World& w, int row) {
  switch (row) {
    case 1:  // .new ours, no manifest: interrupted
      w.WriteCurrent(w.Cur(), "cur", true);
      fs::create_directories(w.New());
      WriteText(w.New() / kMarker, "doa4 suit importer\n");
      synth::WriteFileBytes(w.New() / "vks_ch_d_0.tpr.tmp", synth::Seeded(5, 100));
      break;
    case 2:  // .new verified, current present
      w.WriteCurrent(w.Cur(), "cur", true);
      w.WriteImport(w.New(), {0, 1, 2, 3, 4});
      break;
    case 3:  // .new verified, no current, .old present: a crash between the renames
      w.WriteCurrent(w.Old(), "old", true);
      w.WriteImport(w.New(), {0, 1, 2, 3, 4});
      break;
    case 4:  // current and .old, no .new: a crash after both renames
      w.WriteCurrent(w.Cur(), "cur", true);
      w.WriteCurrent(w.Old(), "old", true);
      break;
    case 5: {  // .new with a manifest that fails verification: one file's bytes changed
      w.WriteCurrent(w.Cur(), "cur", true);
      w.WriteImport(w.New(), {0, 1, 2, 3, 4});
      const std::string name = dsuits::PickSuits()[2].cat.name;
      std::vector<uint8_t> bytes = w.outs[2].cat;
      bytes[7] ^= 0x01;
      synth::WriteFileBytes(w.New() / name, bytes);
      break;
    }
  }
}

static void TestEachSwapState() {
  Group("each_swap_state");
  const char* needle[] = {"", "interrupted", "pending swap", "pending swap", "old import removed", "damaged"};
  for (int row = 1; row <= 5; ++row) {
    // (a) CompletePendingSwap: what the table says.
    {
      World w("swap_row" + std::to_string(row) + "_complete");
      SetupRow(w, row);
      const Snap cur_before = SnapTree(w.Cur());
      const Snap new_before = SnapTree(w.New());
      CK(!new_before.empty() || row == 4);
      std::vector<std::string> lines;
      const std::string why = CompletePendingSwap(w.ud, [&lines](std::string_view l) { lines.emplace_back(l); });
      CKF(why.empty(), "row %d: %s", row, why.c_str());
      CKF(!Exists(w.New()), "row %d: .new remains", row);
      CKF(!Exists(w.Old()), "row %d: .old remains", row);
      bool logged = false;
      for (const std::string& l : lines)
        if (l.find(needle[row]) != std::string::npos) logged = true;
      CKF(logged, "row %d: no log line with '%s'", row, needle[row]);
      if (row == 1 || row == 4 || row == 5) {
        CK(!cur_before.empty());
        CK_SAME(cur_before, SnapTree(w.Cur()), "the current import");  // kept
      } else {
        CK(!new_before.empty());
        CK_SAME(new_before, SnapTree(w.Cur()), "the swapped-in import");  // .new is now the current import
      }
    }
    // (b) Start: recovers first, then imports; the new import is in place and nothing else is left.
    {
      World w("swap_row" + std::to_string(row) + "_start");
      SetupRow(w, row);
      const ImportResult r = RunJob(w, w.MakeHooks(), 2);
      CKF(Is(r, Outcome::kImported), "row %d: outcome %d (%s)", row, int(r.outcome), r.detail.c_str());
      CheckImportFolder(w, w.Cur(), {}, "the fresh import");
      CK(!Exists(w.New()) && !Exists(w.Old()));
      CKF(w.LogCount(needle[row]) >= 1, "row %d: no log line with '%s'", row, needle[row]);
      CKF(w.LogCount("swap recovery") + w.LogCount("old import") >= 1, "row %d: no recovery line", row);
    }
  }
  // A leftover verified .new that is still blocked is deleted and replaced by the new import.
  {
    World w("swap_row2_blocked_leftover");
    SetupRow(w, 2);
    std::shared_ptr<doa4::afs::FileSource> held = doa4::afs::FileSource::Open(w.Cur() / "cur_a.bin");
    CK(held != nullptr);
    const ImportResult r = RunJob(w, w.MakeHooks(), 2);
    CKF(Is(r, Outcome::kSwapPending), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    // .new now holds the FRESH import (34 suits), not the leftover (5).
    CK(SuitLines(ReadText(w.New() / "manifest.txt")) == 34);
    CK(w.LogCount("blocked") >= 1);
    held.reset();
    CK(CompletePendingSwap(w.ud).empty());
    CheckImportFolder(w, w.Cur(), {}, "the fresh import after the pending swap");
  }
  // Row 4 with a .old that is NOT ours (a kept Python import) is kept, never deleted.
  {
    World w("swap_row4_python_old");
    w.WriteCurrent(w.Cur(), "cur", true);
    w.WriteCurrent(w.Old(), "py", false);
    const Snap py = SnapTree(w.Old());
    std::vector<std::string> lines;
    const std::string why = CompletePendingSwap(w.ud, [&lines](std::string_view l) { lines.emplace_back(l); });
    CKF(why.empty(), "%s", why.c_str());
    CK(!py.empty());
    CK_SAME(py, SnapTree(w.Old()), "the kept .old");
    bool logged = false;
    for (const std::string& l : lines)
      if (l.find("old import kept") != std::string::npos && l.find(w.Old().string()) != std::string::npos) logged = true;
    CK(logged);
  }
}

// The files in .new are checked once more before the swap: one changed after it was written (here, while the job is in its
// writing phase) ends the import, and nothing is swapped.
static void TestVerificationCatchesDamage() {
  Group("verification_catches_damage");
  World w("verification_damage");
  w.WriteCurrent(w.Cur(), "cur", true);
  const std::string name = dsuits::PickSuits()[9].cat.name;
  w.phase_hook = [&w, name](Phase p) {
    if (p != Phase::kWriting) return;
    std::vector<uint8_t> b = w.outs[9].cat;
    b[3] ^= 0x01;  // the same size, one bit different
    synth::WriteFileBytes(w.New() / name, b);
  };
  const Snap before = SnapTree(w.ud);
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(Has(r.detail, "verification") && Has(r.detail, name.c_str()), "detail: %s", r.detail.c_str());
  CK(!Exists(w.New()));
  CK_SAME(before, SnapTree(w.ud), "user data (nothing swapped, the current import intact)");
}

static void TestNewWithoutMarkerRefused() {
  Group("new_without_marker_refused");
  World w("new_without_marker");
  w.WriteCurrent(w.Cur(), "cur", true);
  fs::create_directories(w.New());
  WriteText(w.New() / "stray.txt", "someone else's folder\n");
  const Snap before = SnapTree(w.ud);
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CKF(r.detail.find(w.New().string()) != std::string::npos, "detail: %s", r.detail.c_str());
  CK_SAME(before, SnapTree(w.ud), "user data (the folder that is not ours, never deleted)");
  const std::string why = CompletePendingSwap(w.ud);
  CKF(why.find(w.New().string()) != std::string::npos, "CompletePendingSwap: %s", why.c_str());
  CK_SAME(before, SnapTree(w.ud), "user data after CompletePendingSwap");
}

static void TestPythonImportKept() {
  Group("python_import_kept");
  World w("python_import_kept");
  w.WriteCurrent(w.Cur(), "py", false);  // a Python import: no marker
  const Snap py = SnapTree(w.Cur());
  CK(py.count(kMarker) == 0 && py.size() >= 3);
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK_SAME(py, SnapTree(w.Old()), "the Python import kept as .old");
  CheckImportFolder(w, w.Cur(), {}, "the new import");
  CKF(w.LogCount("old import kept") == 1 && w.LogCount(w.Old().string()) >= 1, "log");
}

static void TestSecondImportOverKeptPythonOld() {
  Group("second_import_over_kept_python_old");
  World w("second_over_python");
  w.WriteCurrent(w.Cur(), "py", false);
  const Snap py = SnapTree(w.Cur());
  CK(!py.empty());
  const ImportResult r1 = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r1, Outcome::kImported), "first: outcome %d (%s)", int(r1.outcome), r1.detail.c_str());
  CK_SAME(py, SnapTree(w.Old()), ".old after the first import");
  const ImportResult r2 = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r2, Outcome::kImported), "second: outcome %d (%s)", int(r2.outcome), r2.detail.c_str());
  // After the second import: doax2_suits.old holds nothing (it was our own first import and was deleted); doax2_suits.old-2
  // holds exactly the Python files, byte for byte. The Python import is never deleted.
  CK(!Exists(w.Old()));
  CK_SAME(py, SnapTree(w.ud / "doax2_suits.old-2"), "the kept Python import (.old-2)");
  CK(!Exists(w.ud / "doax2_suits.old-3"));
  CheckImportFolder(w, w.Cur(), {}, "the second import");
  CK(!Exists(w.New()));
  CK(w.LogCount("old-2") >= 1);
  // A third import leaves .old-2 alone as well.
  const ImportResult r3 = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r3, Outcome::kImported), "third: outcome %d (%s)", int(r3.outcome), r3.detail.c_str());
  CK_SAME(py, SnapTree(w.ud / "doax2_suits.old-2"), ".old-2 after the third import");
  CK(!Exists(w.Old()) && !Exists(w.ud / "doax2_suits.old-3"));
}

static void TestWritesOnlyUnderUserData() {
  Group("writes_only_under_user_data");
  World w("writes_only");
  w.WriteCurrent(w.Cur(), "cur", true);
  const Snap root_before = SnapTree(w.root);
  const ImportResult r = RunJob(w, w.MakeHooks(), 3);
  CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  const Snap root_after = SnapTree(w.root);
  // Everything outside user data is as before (the world's root holds nothing but ud), and inside it only the import is
  // left: no probe file, no .new, no .old, no .tmp.
  Snap outside_before, outside_after;
  for (const auto& [k, v] : root_before)
    if (k != "ud" && k.rfind("ud/", 0) != 0) outside_before[k] = v;
  for (const auto& [k, v] : root_after)
    if (k != "ud" && k.rfind("ud/", 0) != 0) outside_after[k] = v;
  CK(root_before.count("ud") == 1);
  CK_SAME(outside_before, outside_after, "the world's root outside user data");
  int under_ud = 0;
  for (const auto& [k, v] : root_after) {
    if (k.rfind("ud/", 0) != 0) continue;
    ++under_ud;
    CKF(k == "ud/doax2_suits" || k.rfind("ud/doax2_suits/", 0) == 0, "unexpected entry %s", k.c_str());
  }
  CKF(under_ud == 71, "%d entries under ud", under_ud);  // the folder and its 70 files
  CheckImportFolder(w, w.Cur(), {}, "the import");
}

static void TestManifestOrderIndependentOfFinish() {
  Group("manifest_order_independent_of_finish");
  World w("manifest_order");
  std::mt19937 rng(20261107);
  for (int i = 0; i < kSuits; ++i) w.delay_ms[size_t(i)] = i < 2 ? 100 : int(rng() % 12);
  const ImportResult r = RunJob(w, w.MakeHooks(), 4);
  CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  // The fixture's precondition: suits finished out of picks order. Suits 0 and 1 take 100 ms each while the other two
  // workers get through the remaining 32 in a few ms apiece, so both finish after almost all of them (about 64 inversions).
  int inversions = 0;
  CK(w.finish_order.size() == 34);
  for (size_t i = 0; i < w.finish_order.size(); ++i)
    for (size_t j = i + 1; j < w.finish_order.size(); ++j)
      if (w.finish_order[i] > w.finish_order[j]) ++inversions;
  CKF(inversions >= 20, "%d inversions", inversions);
  CK(!std::is_sorted(w.finish_order.begin(), w.finish_order.end()));
  CK(ReadText(w.Cur() / "manifest.txt") == w.ExpectedManifest());
  CheckImportFolder(w, w.Cur(), {}, "the import");
}

static void TestWorkers() {
  Group("workers");
  const unsigned hw = std::thread::hardware_concurrency();
  const int cap = int(std::clamp<int>(int(hw) - 1, 1, 4));
  for (int req : {0, 1, 3}) {
    World w("workers_" + std::to_string(req));
    for (int& d : w.delay_ms) d = 12;
    const ImportResult r = RunJob(w, w.MakeHooks(), req);
    CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    const int want = req == 0 ? cap : req;
    CKF(w.peak.load() == want, "workers=%d: %d converting at once, wanted %d (hardware %u)", req, w.peak.load(), want, hw);
  }
}


// ---------------------------------------------------------------------------------------------------------------
// fix round 1
// ---------------------------------------------------------------------------------------------------------------

// A hook that throws before anything was created by this run: the folders an earlier import (or somebody else) left are not
// this job's to delete. A marker-less .new is never touched; a pending verified .new waits for the recovery that follows the
// checks. A .new this run created itself is still deleted.
static void TestHookThrowsLeavesForeignNew() {
  Group("hook_throws_leaves_foreign_new");
  for (Phase at : {Phase::kCheckingSpace, Phase::kCheckingDisc, Phase::kCheckingGame}) {
    for (int variant = 0; variant < 2; ++variant) {
      World w(std::string("throw_") + PhaseName(at) + (variant == 0 ? "_stray" : "_pending"));
      w.WriteCurrent(w.Cur(), "cur", true);
      if (variant == 0) {
        fs::create_directories(w.New());
        WriteText(w.New() / "stray.txt", "someone else's folder\n");  // no marker
      } else {
        w.WriteImport(w.New(), {0, 1, 2, 3, 4});  // a verified import waiting for its swap
      }
      w.phase_hook = [at](Phase p) {
        if (p == at) throw std::runtime_error("synthetic hook failure");
      };
      const Snap before = SnapTree(w.ud);
      CK(Exists(w.New()));
      const ImportResult r = RunJob(w, w.MakeHooks(), 2);
      CKF(Is(r, Outcome::kFailed), "%s/%d: outcome %d (%s)", PhaseName(at), variant, int(r.outcome), r.detail.c_str());
      CK(Has(r.detail, "synthetic hook failure"));
      CK_SAME(before, SnapTree(w.ud), "user data after a throwing hook (the .new that was not ours or not yet decided stays)");
    }
  }
  {
    World w("throw_converting");  // the .new this run created is its own: deleted
    w.WriteCurrent(w.Cur(), "cur", true);
    w.phase_hook = [](Phase p) {
      if (p == Phase::kConverting) throw std::runtime_error("synthetic hook failure");
    };
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, w.MakeHooks(), 2);
    CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after a throw while converting");
  }
}

// An exception between the swap's two renames leaves the state exactly as it is (the previous import as .old, .new complete,
// no current import): nothing is deleted on the way out, and the recovery finishes the swap.
static void TestThrowBetweenSwapRenames() {
  Group("throw_between_swap_renames");
  World w("throw_between_renames");
  w.WriteCurrent(w.Cur(), "cur", true);
  const Snap cur_before = SnapTree(w.Cur());
  Hooks h = w.MakeHooks();
  h.between_renames = [] { throw std::runtime_error("synthetic failure between the renames"); };
  const ImportResult r = RunJob(w, std::move(h), 2);
  CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(Has(r.detail, "between the renames"));
  CK(!Exists(w.Cur()));
  CK(Exists(w.New()));
  CK(VerifyImportFolder(w.New()).empty());  // .new is complete and verified
  CK_SAME(cur_before, SnapTree(w.Old()), "the previous import, now .old");
  const std::string why = CompletePendingSwap(w.ud);
  CKF(why.empty(), "%s", why.c_str());
  CheckImportFolder(w, w.Cur(), {}, "the new import after the recovery");
  CK(!Exists(w.New()) && !Exists(w.Old()));
}

static std::string ReadSmall(const fs::path& p) {
  std::string data;
  HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return data;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0) data.append(buf, n);
  CloseHandle(h);
  return data;
}

// A cleanup that cannot finish (a file in the folder is held open) keeps the folder marked as ours, so the next import or
// CompletePendingSwap still recognises and deletes it; the marker goes last.
static void TestBlockedCleanupKeepsMarker() {
  Group("blocked_cleanup_keeps_marker");
  const std::string name = dsuits::PickSuits()[0].tpr.name;
  for (int variant = 0; variant < 3; ++variant) {
    World w("blocked_cleanup_" + std::to_string(variant));
    w.WriteCurrent(w.Cur(), "cur", true);
    const Snap cur_before = SnapTree(w.Cur());
    std::shared_ptr<doa4::afs::FileSource> held;
    std::atomic<bool> opened{false};
    if (variant < 2) {
      // The job's own cleanup after a cancel in the writing phase, with one written suit file held open.
      w.phase_hook = [&](Phase p) {
        if (p != Phase::kWriting) return;
        held = doa4::afs::FileSource::Open(w.New() / name);
        opened = true;
        SleepMs(60);
      };
      auto job = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
      const auto t_wait = Clock::now();
      while (!opened.load() && MsSince(t_wait) < 10000) SleepMs(1);
      CK(opened.load() && held != nullptr);
      job->Cancel();
      CK(WaitDone(*job, 15000));
      CKF(Is(job->result(), Outcome::kCancelled), "outcome %d", int(job->result().outcome));
    } else {
      // The recovery's own deletion of an interrupted .new, with a file held open.
      fs::create_directories(w.New());
      WriteText(w.New() / kMarker, "doa4 suit importer\n");
      synth::WriteFileBytes(w.New() / "a_suit.tpr", synth::Seeded(3, 500));
      held = doa4::afs::FileSource::Open(w.New() / "a_suit.tpr");
      CK(held != nullptr);
    }
    // The folder is still there, still ours, and says it is unfinished (no manifest).
    CK(Exists(w.New()));
    CKF(Exists(w.New() / kMarker), "variant %d: the marker was deleted while a file could not be", variant);
    CK(!Exists(w.New() / "manifest.txt"));
    const std::string why1 = CompletePendingSwap(w.ud);
    CKF(!why1.empty(), "variant %d: a cleanup that cannot delete the held file says why", variant);
    CKF(Exists(w.New() / kMarker), "variant %d: the marker survives the second failed cleanup", variant);
    held.reset();
    if (variant == 1) {
      // The next import recovers the same way and goes on (the hook that held the file open is gone).
      w.phase_hook = nullptr;
      const ImportResult r = RunJob(w, w.MakeHooks(), 2);
      CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
      CheckImportFolder(w, w.Cur(), {}, "the import after a blocked cleanup");
      CK(w.LogCount("interrupted") >= 1);
    } else {
      const std::string why2 = CompletePendingSwap(w.ud);
      CKF(why2.empty(), "variant %d: %s", variant, why2.c_str());
      CK(!Exists(w.New()));
      CK_SAME(cur_before, SnapTree(w.Cur()), "the current import");
    }
  }
}

static void WriteSparse(const fs::path& p, uint64_t size) {
  HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create " + p.string());
  DWORD ret = 0;
  DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ret, nullptr);
  LARGE_INTEGER at;
  at.QuadPart = LONGLONG(size);
  const bool ok = SetFilePointerEx(h, at, nullptr, FILE_BEGIN) && SetEndOfFile(h);
  CloseHandle(h);
  if (!ok) throw std::runtime_error("cannot size " + p.string());
}

static std::map<std::string, uint64_t> Sizes(const fs::path& root) {
  std::map<std::string, uint64_t> m;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    m[fs::relative(it->path(), root).generic_string()] = it->is_directory(e2) ? uint64_t(-1) : fs::file_size(it->path(), e2);
  }
  return m;
}

// The recovery hashes a leftover .new before deciding anything: a cancel during it returns within a second, reads the
// cancelled hash as "cancelled" (never as "damaged"), and the leftover is untouched.
static void TestRecoveryVerificationCancellable() {
  Group("recovery_verification_is_cancellable");
  World w("recovery_cancel");
  w.WriteCurrent(w.Cur(), "cur", true);
  // A leftover whose first file is a 4 GiB (less one byte) sparse file (its SHA-1 takes seconds to compute) and whose manifest lists a wrong
  // SHA-1: left alone to finish, the recovery would call it damaged and delete it.
  fs::create_directories(w.New());
  WriteText(w.New() / kMarker, "doa4 suit importer\n");
  dsuits::Manifest m;
  m.converter = std::string(kConverterWord);
  const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
  for (int i : {0, 1}) {
    dsuits::Suit s = picks[size_t(i)];
    s.tpr.size = 0xFFFFFFFFull;  // the largest size a manifest line takes
    s.tpr.sha1 = std::string(40, 'a');
    s.cat.size = 100;
    s.cat.sha1 = std::string(40, 'b');
    WriteSparse(w.New() / s.tpr.name, s.tpr.size);
    synth::WriteFileBytes(w.New() / s.cat.name, synth::Seeded(40 + uint32_t(i), 100));
    m.suits.push_back(std::move(s));
  }
  WriteText(w.New() / "manifest.txt", dsuits::FormatManifest(m));
  const auto sizes_before = Sizes(w.New());
  const std::string manifest_before = ReadSmall(w.New() / "manifest.txt");
  const Snap cur_before = SnapTree(w.Cur());
  CK(sizes_before.size() == 6 && !manifest_before.empty());
  auto job = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
  const auto t_wait = Clock::now();
  while (job->progress().phase != Phase::kCheckingGame && MsSince(t_wait) < 10000) SleepMs(1);
  SleepMs(300);  // the game check is a few ms: the recovery is hashing the first leftover file now
  CK(!job->done());
  CK(job->progress().phase == Phase::kCheckingGame);
  const auto t0 = Clock::now();
  job->Cancel();
  const bool ok = WaitDone(*job, 30000);
  const long long ms = MsSince(t0);
  NoteCancel(ms, "cancel during the recovery's verification");
  CKF(ok && ms <= 1000, "done within 1 s: %lld ms", ms);
  const ImportResult r = job->result();
  CKF(Is(r, Outcome::kCancelled), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(Exists(w.New()) && Exists(w.New() / kMarker));
  CK(Sizes(w.New()) == sizes_before);
  CK(ReadSmall(w.New() / "manifest.txt") == manifest_before);
  CK(!Exists(w.Old()));
  CK_SAME(cur_before, SnapTree(w.Cur()), "the current import");
  CK(w.LogCount("damaged") == 0);
}

// A cancel that comes from inside the work, not at a phase's entry: from the converter after ten suits, and between the
// manifest's write and the end of the verification (the verification must stop: the swap phase is never reached).
static void TestCancelFromInsideTheWork() {
  Group("cancel_from_inside_the_work");
  {
    World w("cancel_in_converter");
    w.WriteCurrent(w.Cur(), "cur", true);
    w.cancel_after_finishes = 10;
    w.phase_hook = [](Phase p) {
      if (p == Phase::kCheckingSpace) SleepMs(30);  // the test stores the job's address first
    };
    const Snap before = SnapTree(w.ud);
    auto job = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
    w.job_ptr = job.get();
    CK(WaitDone(*job, 30000));
    const ImportResult r = job->result();
    CKF(Is(r, Outcome::kCancelled), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(job->progress().suits_done >= 5 && job->progress().suits_done < 34);  // it stopped part-way (the tenth conversion finished, its write never started)
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after a cancel from the converter");
  }
  {
    World w("cancel_in_verification");
    w.WriteCurrent(w.Cur(), "cur", true);
    std::atomic<bool> swap_reached{false};
    w.phase_hook = [&](Phase p) {
      if (p == Phase::kSwapping) swap_reached = true;
      if (p == Phase::kCheckingSpace) SleepMs(30);
    };
    std::atomic<ImportJob*> job_ptr{nullptr};
    w.on_write_hook = [&](int n) {
      if (n == 70) {  // the manifest is in place: the verification of .new is next
        if (ImportJob* j = job_ptr.load()) j->Cancel();
      }
    };
    const Snap before = SnapTree(w.ud);
    auto job = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
    job_ptr = job.get();
    CK(WaitDone(*job, 30000));
    const ImportResult r = job->result();
    CKF(Is(r, Outcome::kCancelled), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CKF(!swap_reached.load(), "the swap phase began although the cancel came before the verification ended");
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after a cancel between the manifest and the verification");
  }
}

// The routes to std::terminate: a thread that cannot be started, a registration for Cancel that fails.
static void TestThreadAndRegistrationFailures() {
  Group("thread_and_registration_failures");
  {
    World w("worker_start_fails");
    w.WriteCurrent(w.Cur(), "cur", true);
    Hooks h = w.MakeHooks();
    h.fail_worker_start_at = 2;  // the first worker runs, the second cannot be started
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, std::move(h), 3);
    CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(Has(r.detail, "worker"));
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after a worker thread could not be started");
  }
  {
    World w("job_thread_start_fails");
    Hooks h = w.MakeHooks();
    h.fail_job_thread_start = true;
    auto job = ImportJob::StartForTest(w.Req(2), std::move(h));
    CK(job->done());  // nothing is running: the failure is the result
    const ImportResult r = job->result();
    CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(Has(r.detail, "thread"));
    CK(job->progress().phase == Phase::kDone);
    job.reset();  // the destructor must not join a thread that never started
    CK(SnapTree(w.ud).empty());
  }
  for (int at : {1, 2}) {
    World w("register_fails_" + std::to_string(at));
    w.WriteCurrent(w.Cur(), "cur", true);
    Hooks h = w.MakeHooks();
    h.fail_register_at = at;  // 1: the job's thread; 2: a worker (the first registration after the job's)
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, std::move(h), 2);
    CKF(Is(r, Outcome::kFailed), "at %d: outcome %d (%s)", at, int(r.outcome), r.detail.c_str());
    CKF(Has(r.detail, "Cancel"), "at %d: detail: %s", at, r.detail.c_str());
    CKF(w.LogCount("Cancel") >= 1, "at %d: logged", at);
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after a failed registration");
  }
}

// Last: without the catch-all in the job's thread this ends the process.
static void TestHookThrowsNonStdException() {
  Group("hook_throws_non_std_exception");
  World w("throw_int");
  w.WriteCurrent(w.Cur(), "cur", true);
  w.phase_hook = [](Phase p) {
    if (p == Phase::kCheckingDisc) throw 42;
  };
  const Snap before = SnapTree(w.ud);
  const ImportResult r = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(r, Outcome::kFailed), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(!r.detail.empty());
  CK_SAME(before, SnapTree(w.ud), "user data");
}

// ---------------------------------------------------------------------------------------------------------------
// VerifyImportFolder
// ---------------------------------------------------------------------------------------------------------------

static void TestVerifyImportFolder() {
  Group("verify_import_folder");
  World w("verify_import");
  const fs::path good = w.root / "good";
  w.WriteImport(good, {0, 1, 2, 3, 4});
  CK(VerifyImportFolder(good).empty());
  const std::string name = dsuits::PickSuits()[2].tpr.name;
  const std::string label = LabelOf(2);
  const auto Copy = [&](const char* tag) {
    const fs::path p = w.root / tag;
    fs::copy(good, p, fs::copy_options::recursive);
    return p;
  };
  {
    const fs::path p = Copy("no_manifest");
    fs::remove(p / "manifest.txt");
    const std::string why = VerifyImportFolder(p);
    CKF(Has(why, "manifest.txt"), "%s", why.c_str());
  }
  {
    const fs::path p = Copy("bad_manifest");
    WriteText(p / "manifest.txt", "doax2-suits 2\n");
    const std::string why = VerifyImportFolder(p);
    CKF(Has(why, "manifest"), "%s", why.c_str());
  }
  {
    const fs::path p = Copy("empty_manifest");
    WriteText(p / "manifest.txt", dsuits::FormatManifest(dsuits::Manifest{std::string(kConverterWord), {}}));
    CK(!VerifyImportFolder(p).empty());
  }
  {
    const fs::path p = Copy("not_a_pick");
    std::string text = ReadText(p / "manifest.txt");
    const size_t at = text.find("vks_ch_d");
    CK(at != std::string::npos);
    text.replace(at, 8, "vks_zz_z");  // a design that is not a pick (the file names stay)
    WriteText(p / "manifest.txt", text);
    const std::string why = VerifyImportFolder(p);
    CK(!why.empty());
  }
  {
    const fs::path p = Copy("missing_file");
    fs::remove(p / name);
    const std::string why = VerifyImportFolder(p);
    CKF(Has(why, name.c_str()) && Has(why, "missing"), "%s", why.c_str());
  }
  {
    const fs::path p = Copy("wrong_size");
    std::vector<uint8_t> b = w.outs[2].tpr;
    b.push_back(0);
    synth::WriteFileBytes(p / name, b);
    const std::string why = VerifyImportFolder(p);
    CKF(Has(why, name.c_str()) && Has(why, "size"), "%s", why.c_str());
  }
  {
    const fs::path p = Copy("wrong_sha");
    std::vector<uint8_t> b = w.outs[2].tpr;
    b[100] ^= 0x01;  // the same size
    synth::WriteFileBytes(p / name, b);
    const std::string why = VerifyImportFolder(p);
    CKF(Has(why, name.c_str()) && Has(why, "SHA-1"), "%s", why.c_str());
  }
  (void)label;
}

// ---------------------------------------------------------------------------------------------------------------
// real_import (gated)
// ---------------------------------------------------------------------------------------------------------------

static void TestRealImport() {
  const char* disc = std::getenv("DOA4_SUITCONV_DISC");
  const char* game = std::getenv("DOA4_SUITCONV_GAME_DIR");
  if (!disc || !*disc || !game || !*game) {
    std::printf("real_import: SKIPPED (DOA4_SUITCONV_DISC or DOA4_SUITCONV_GAME_DIR unset)\n");
    ++g_skipped;
    return;
  }
  Group("real_import");
  World w("real_import");
  ImportRequest req;
  req.doax2 = fs::path(disc);
  req.game_root = fs::path(game);
  req.user_data = w.ud;
  auto job = ImportJob::Start(req);
  CK(WaitDone(*job, 20 * 60 * 1000));
  const ImportResult r = job->result();
  CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
  CK(r.differs_from_reference.empty() && r.skipped.empty());
  CK(VerifyImportFolder(w.Cur()).empty());
  CK(SuitLines(ReadText(w.Cur() / "manifest.txt")) == 34);
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash must still show how far the run got
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);  // a crash under test ends the process without a dialog
  try {
    TestBytesNeeded();
    TestDefaultsAreProduction();
    TestFullImport();
    TestProgressMonotonic();
    TestCancelInEachPhase();
    TestCancelDuringStalledRead();
    TestOneSuitFails();
    TestConverterThrows();
    TestAllFail();
    TestOverBudgetSkipped();
    TestReferenceMismatchKept();
    TestWriteFaultMidWay();
    TestCrashLeavesNoPartialFile();
    TestNoSpaceRefusedBeforeReads();
    TestNoWriteAccess();
    TestWrongDisc();
    TestGameFilesDiffer();
    TestReadFailsMidImport();
    TestSwapBlockedThenCompleted();
    TestSwapSecondRenameFailsUndone();
    TestEachSwapState();
    TestVerificationCatchesDamage();
    TestNewWithoutMarkerRefused();
    TestPythonImportKept();
    TestSecondImportOverKeptPythonOld();
    TestWritesOnlyUnderUserData();
    TestManifestOrderIndependentOfFinish();
    TestWorkers();
    TestVerifyImportFolder();
    TestHookThrowsLeavesForeignNew();
    TestBlockedCleanupKeepsMarker();
    TestRecoveryVerificationCancellable();
    TestCancelFromInsideTheWork();
    TestThreadAndRegistrationFailures();
    TestThrowBetweenSwapRenames();
    TestRealImport();
    TestHookThrowsNonStdException();
  } catch (const std::exception& e) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: uncaught exception: %s\n", e.what());
  } catch (...) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL: uncaught non-standard exception\n");
  }
  std::printf("skipped sections: %d\n", g_skipped);
  std::printf("slowest cancel: %lld ms (%s)\n", g_max_cancel_ms, g_max_cancel_name.c_str());
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
