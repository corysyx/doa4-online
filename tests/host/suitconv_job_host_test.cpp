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
// verify_import_folder; and the real sections, gated by DOA4_SUITCONV_DISC, DOA4_SUITCONV_GAME_DIR and DOA4_SUITCONV_ORACLE_DIR
// ("<group>: SKIPPED (...)" when unset), which use the player's disc and game files and the reference converter's folder
// (68 suit files and manifest.txt): real_import_equals_oracle (one production import, 69 files plus the marker equal the
// reference by size and SHA-1), real_import_worker_counts (1, 4 and 8 workers, equal folders), real_reverse_order_one_process
// (ConvertSuit over the 34 in reverse order), real_fma3_toggle (the C runtime's FMA3 switch off and on, on the converting
// threads), real_dirty_mxcsr (FTZ, DAZ and round-up on the calling thread and on direct ConvertSuit threads),
// real_time_and_memory (wall time, phases, each suit's conversion time against the Python converter's, the working set).
// The final review's groups (each runs alone as DOA4_SUITCONV_ONLY=final_guard, final_destroy, final_throwing_log, final_ownerless,
// final_held_folder, final_swap_retry or final_free_space): in_process_guard (a second job or a
// CompletePendingSwap on a folder where a job runs is refused, nothing touched), destroy_from_log_callback (the job destroyed
// from its own log callback, on the job's thread and on a worker), throwing_log_callback (a callback that throws on every call
// changes no outcome and ends no thread), ownerless_folder_recovery (an empty .new or .old, or one holding only the marker's
// temporary file, is deleted; a foreign one is refused or kept), held_folder_keeps_marker, swap_retries_blocked_rename (a file
// held for 300 ms, for the whole window, a cancel between tries), free_space_counts_leftover.
// DOA4_SUITCONV_ONLY=real runs only the real sections, =fake only the others, =<section name> only that section. Last lines: "skipped sections: K", "slowest
// cancel ...", then "PASSED: N checks, 0 failures" or "FAILED: N checks, M failures".

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "../../src/doa4_suitconv_job.h"
#include "../../src/doa4_costumes.h"
#include "../../src/doa4_suits.h"
#include "../../src/doa4_suits_manifest.h"
#include "doa4_test_image.h"
#include "../../src/doa4_suits_picks.h"
#include "suitconv_synth.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <immintrin.h>
#include <math.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

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
// the final review's fixes: the in-process guard, no terminate route, ownerless folders, the swap's retry, free space
// ---------------------------------------------------------------------------------------------------------------

static bool OnlyHas(const char* token);
// The final-fix groups run with the fake groups, or alone by their own name (DOA4_SUITCONV_ONLY=<name>).
static bool Want(const char* group) { return OnlyHas("fake") || OnlyHas(group); }

// Blocks the job's own thread at a phase until the test lets it go (20 s at most).
struct PhaseGate {
  std::atomic<bool> release{false}, reached{false};
  void Wait() {
    reached = true;
    const auto t0 = Clock::now();
    while (!release.load() && MsSince(t0) < 20000) SleepMs(1);
  }
  bool WaitReached(int ms) const {
    const auto t0 = Clock::now();
    while (!reached.load() && MsSince(t0) < ms) SleepMs(1);
    return reached.load();
  }
};

// A second job on a user data folder where a job runs is refused at once and touches nothing; CompletePendingSwap on that
// folder is refused too; another folder is not affected; the folder is free again once the job is done.
static void TestInProcessGuard() {
  Group("in_process_guard");
  World w("guard");
  World other("guard_other");
  w.WriteCurrent(w.Cur(), "cur", true);
  PhaseGate gate;
  w.phase_hook = [&gate](Phase p) {
    if (p == Phase::kConverting) gate.Wait();
  };
  auto a = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
  CK(gate.WaitReached(10000));
  // The running job's .new holds its marker and nothing else: a recovery would call it interrupted and delete it.
  const Snap running = SnapTree(w.ud);
  CKF(running.count("doax2_suits.new/.doa4-suitconv") == 1, "the running job's .new has %zu entries", running.size());
  {
    auto b = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
    CK(b->done());
    const ImportResult rb = b->result();
    CKF(Is(rb, Outcome::kFailed) && Has(rb.detail, "already running"), "StartForTest: outcome %d (%s)", int(rb.outcome), rb.detail.c_str());
    CK(b->progress().phase == Phase::kDone);
  }
  {
    ImportRequest req = w.Req(2);
    req.user_data = w.ud / ".." / "ud";  // the same folder, spelled another way
    auto b = ImportJob::Start(std::move(req));
    CK(b->done());
    const ImportResult rb = b->result();
    CKF(Is(rb, Outcome::kFailed) && Has(rb.detail, "already running"), "Start: outcome %d (%s)", int(rb.outcome), rb.detail.c_str());
  }
  CK_SAME(running, SnapTree(w.ud), "user data after the refused starts");
  const std::string why = CompletePendingSwap(w.ud);
  CKF(Has(why, "import is running"), "CompletePendingSwap: '%s'", why.c_str());
  CK_SAME(running, SnapTree(w.ud), "user data after the refused CompletePendingSwap");
  {
    const ImportResult ro = RunJob(other, other.MakeHooks(), 2);  // another folder: not blocked
    CKF(Is(ro, Outcome::kImported), "other folder: outcome %d (%s)", int(ro.outcome), ro.detail.c_str());
  }
  gate.release = true;
  CK(WaitDone(*a, 30000));
  const ImportResult ra = a->result();
  CKF(Is(ra, Outcome::kImported), "the first job: outcome %d (%s)", int(ra.outcome), ra.detail.c_str());
  CheckImportFolder(w, w.Cur(), {}, "the first job's import");
  // The folder is free again as soon as the job is done (the job object is still alive).
  CK(CompletePendingSwap(w.ud).empty());
  w.phase_hook = nullptr;
  const ImportResult rc = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(rc, Outcome::kImported), "a job after the first: outcome %d (%s)", int(rc.outcome), rc.detail.c_str());
  // CompletePendingSwap holds the folder while it works: a Start from inside its log callback is refused.
  World w2("guard_swap_callback");
  SetupRow(w2, 1);  // an interrupted .new: the recovery logs it
  int calls = 0, refused = 0;
  const Snap before = SnapTree(w2.ud);
  const std::string why2 = CompletePendingSwap(w2.ud, [&](std::string_view) {
    ++calls;
    auto b = ImportJob::StartForTest(w2.Req(2), w2.MakeHooks());
    if (b->done() && Is(b->result(), Outcome::kFailed) && Has(b->result().detail, "already running")) ++refused;
  });
  CKF(why2.empty(), "%s", why2.c_str());
  CKF(calls >= 1 && refused == calls, "%d log calls, %d refused starts", calls, refused);
  CK(!Exists(w2.New()));
}

// Another PROCESS is simulated by the test creating the folder's named kernel object itself: while it exists, CompletePendingSwap
// and a Start on that folder are refused and touch nothing (a planted interrupted .new is the thing a recovery would delete).
static void TestCrossProcessGuard() {
  Group("cross_process_guard");
  World w("xproc");
  World other("xproc_other");
  w.WriteCurrent(w.Cur(), "cur", true);
  fs::create_directories(w.New());
  WriteText(w.New() / kMarker, "doa4 suit importer\n");
  const std::wstring name = FolderClaimNameForTest(w.ud);
  // (d) the name: one folder, one name, whatever the spelling; another folder, another name.
  std::wstring upper = w.ud.wstring();
  for (wchar_t& c : upper) c = wchar_t(towupper(c));
  CK(!name.empty() && name.rfind(L"Local\\doa4-suitconv-", 0) == 0);
  CK(FolderClaimNameForTest(w.ud / ".." / "ud") == name);
  CK(FolderClaimNameForTest(fs::path(upper)) == name);
  CK(FolderClaimNameForTest(fs::path(w.ud.wstring() + L"\\")) == name);
  CK(FolderClaimNameForTest(other.ud) != name);
  const Snap before = SnapTree(w.ud);
  HANDLE held = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
  CK(held != nullptr && GetLastError() != ERROR_ALREADY_EXISTS);
  // (a) CompletePendingSwap: refused, listing unchanged, the .new with its marker and no manifest is still there.
  {
    const std::string why = CompletePendingSwap(w.ud);
    CKF(Has(why, "import is running"), "CompletePendingSwap: '%s'", why.c_str());
    CK_SAME(before, SnapTree(w.ud), "user data after the refused CompletePendingSwap");
    CK(Exists(w.New() / kMarker) && !Exists(w.New() / "manifest.txt"));
  }
  // (b) a job: ends kFailed "already running", touches nothing.
  {
    auto b = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
    CK(b->done());
    const ImportResult rb = b->result();
    CKF(Is(rb, Outcome::kFailed) && Has(rb.detail, "already running"), "StartForTest: outcome %d (%s)", int(rb.outcome), rb.detail.c_str());
    CK_SAME(before, SnapTree(w.ud), "user data after the refused StartForTest");
  }
  // (c) once the "other process" lets go, both work again.
  if (held) CloseHandle(held);
  held = nullptr;
  CK(CompletePendingSwap(w.ud).empty());
  CK(!Exists(w.New()));  // the interrupted .new was recovered now that nothing else holds the folder
  // (e) a claim taken and released in this process leaves no object behind.
  {
    SetLastError(0);
    HANDLE probe = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    CKF(probe != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "a released claim left its object (error %lu)", GetLastError());
    if (probe) CloseHandle(probe);
  }
  const ImportResult rc = RunJob(w, w.MakeHooks(), 2);
  CKF(Is(rc, Outcome::kImported), "a job after the other process let go: outcome %d (%s)", int(rc.outcome), rc.detail.c_str());
  {
    SetLastError(0);
    HANDLE probe = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    CKF(probe != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "a finished job left its object (error %lu)", GetLastError());
    if (probe) CloseHandle(probe);
  }
}

// The log callback destroys the job: on the job's own thread (a recovery line) and on a worker (a reference mismatch).
// The destructor must not join its own thread; the job ends in the background and the folder is free for the next job.
static void DestroyFromCallback(const char* name, bool on_worker) {
  World w(name);
  w.WriteCurrent(w.Cur(), "cur", true);
  if (on_worker) {
    w.shas.push_back(std::string(40, 'c'));
    w.suits[4].out_cat_sha1 = w.shas.back().c_str();  // suit 4 differs from the reference: logged by a worker
  } else {
    SetupRow(w, 1);  // an interrupted .new: its recovery is logged by the job's thread
  }
  std::atomic<ImportJob*> victim{nullptr};
  std::atomic<int> destroyed{0};
  ImportRequest req = w.Req(2);
  req.log = [&](std::string_view) {
    if (destroyed.load() > 0) return;
    const auto t0 = Clock::now();
    while (!victim.load() && MsSince(t0) < 5000) SleepMs(1);
    if (ImportJob* j = victim.exchange(nullptr)) {
      delete j;  // from inside the callback
      ++destroyed;
    }
  };
  ImportJob* raw = ImportJob::StartForTest(std::move(req), w.MakeHooks()).release();
  victim = raw;
  {
    const auto t_wait = Clock::now();  // the callback destroys the job: wait for it (without the fix this ends the process)
    while (destroyed.load() == 0 && MsSince(t_wait) < 10000) SleepMs(1);
  }
  // The destroyed job is cancelled and ends in the background: the folder is free again soon, then a fresh job imports.
  ImportResult r;
  const auto t0 = Clock::now();
  for (;;) {
    ImportRequest req2 = w.Req(2);
    req2.log = nullptr;
    auto job = ImportJob::StartForTest(std::move(req2), w.MakeHooks());
    if (job->done() && Has(job->result().detail, "already running")) {
      if (MsSince(t0) > 20000) break;
      SleepMs(10);
      continue;
    }
    CK(WaitDone(*job, 30000));
    r = job->result();
    break;
  }
  CKF(destroyed.load() == 1, "%s: the callback destroyed the job %d times", name, destroyed.load());
  CKF(Is(r, Outcome::kImported), "%s: the next job: outcome %d (%s)", name, int(r.outcome), r.detail.c_str());
  CheckImportFolder(w, w.Cur(), {}, name);
  CK(!Exists(w.New()) && !Exists(w.Old()));
}

static void TestDestroyFromLogCallback() {
  Group("destroy_from_log_callback");
  DestroyFromCallback("destroy_on_job_thread", false);
  DestroyFromCallback("destroy_on_worker", true);
}

// A log callback that throws on every call: the import ends as it would with a quiet callback. Without the swallow in Log
// the registration failures below end the process (the Log call sits outside any try on a thread's first line).
struct LogScenario {
  ImportResult r;
  Snap ud;
  int calls = 0;
};

// kind: 0 a leftover interrupted .new, a failing suit and a reference mismatch; 1 a cancel from inside the converter; 2 a write
// fault; 3 the job thread's registration fails; 4 a worker's registration fails. throw_mode: 0 quiet, 1 throws
// std::runtime_error, 2 throws an int.
static LogScenario RunLogScenario(int kind, int throw_mode) {
  World w("throwing_log_k" + std::to_string(kind));  // one name per kind: the details name the same paths in every mode
  w.WriteCurrent(w.Cur(), "cur", true);
  if (kind <= 2) {
    fs::create_directories(w.New());
    WriteText(w.New() / kMarker, "doa4 suit importer\n");
    w.shas.push_back(std::string(40, 'd'));
    w.suits[0].out_cat_sha1 = w.shas.back().c_str();  // a reference mismatch: a worker logs it
  }
  Hooks h = w.MakeHooks();
  if (kind == 0) w.fail_suits.insert(5);
  if (kind == 1) {
    w.cancel_after_finishes = 10;
    w.phase_hook = [](Phase p) {
      if (p == Phase::kCheckingSpace) SleepMs(30);  // the test stores the job's address first
    };
  }
  if (kind == 2) h.write_fault_at = 20;
  if (kind == 3) h.fail_register_at = 1;
  if (kind == 4) h.fail_register_at = 2;
  LogScenario s;
  std::atomic<int> calls{0};
  ImportRequest req = w.Req(2);
  req.log = [&calls, throw_mode](std::string_view) {
    ++calls;
    if (throw_mode == 1) throw std::runtime_error("the logger is broken");
    if (throw_mode == 2) throw 7;
  };
  auto job = ImportJob::StartForTest(std::move(req), std::move(h));
  w.job_ptr = job.get();
  const bool ok = WaitDone(*job, 60000);
  CK(ok);
  if (!ok) job->Cancel();
  WaitDone(*job, 15000);
  s.r = job->result();
  s.ud = SnapTree(w.ud);
  s.calls = calls.load();
  return s;
}

static void TestThrowingLogCallback() {
  Group("throwing_log_callback");
  for (int kind = 0; kind <= 4; ++kind) {
    const LogScenario quiet = RunLogScenario(kind, 0);
    for (int mode : {1, 2}) {
      const LogScenario loud = RunLogScenario(kind, mode);
      CKF(quiet.calls >= 1, "kind %d: the quiet run never logged (%d)", kind, quiet.calls);
      CKF(loud.calls >= 1, "kind %d/%d: the throwing callback was never called", kind, mode);
      CKF(loud.r.outcome == quiet.r.outcome, "kind %d/%d: outcome %d, quiet %d (%s)", kind, mode, int(loud.r.outcome),
          int(quiet.r.outcome), loud.r.detail.c_str());
      // (Which file the 20th write is depends on how the two workers interleave: the write fault's detail is compared by its
      // number, every other detail whole.)
      if (kind == 2) CKF(Has(loud.r.detail, "injected write fault #20"), "kind 2/%d: detail '%s'", mode, loud.r.detail.c_str());
      else CKF(loud.r.detail == quiet.r.detail, "kind %d/%d: detail '%s', quiet '%s'", kind, mode, loud.r.detail.c_str(), quiet.r.detail.c_str());
      CKF(loud.r.skipped.size() == quiet.r.skipped.size(), "kind %d/%d: %zu skipped, quiet %zu", kind, mode,
          loud.r.skipped.size(), quiet.r.skipped.size());
      CKF(loud.r.differs_from_reference == quiet.r.differs_from_reference, "kind %d/%d: differs list", kind, mode);
      // The user data folders hold the same files (the fake bytes are seeded: the comparison is exact).
      CKF(loud.ud == quiet.ud, "kind %d/%d: user data differs:%s", kind, mode, SnapDiff(quiet.ud, loud.ud).c_str());
    }
    if (kind == 0) CK(Is(quiet.r, Outcome::kImportedWithSkips) && quiet.r.skipped.size() == 1 && quiet.r.differs_from_reference.size() == 1);
    if (kind == 1) CK(Is(quiet.r, Outcome::kCancelled));
    if (kind == 2) CK(Is(quiet.r, Outcome::kFailed) && Has(quiet.r.detail, "#20"));
    if (kind >= 3) CK(Is(quiet.r, Outcome::kFailed) && Has(quiet.r.detail, "Cancel"));
  }
  // An exception that gets past ProcessSuit's own handlers (here, a hook called after a file write) ends the worker's body: its
  // outermost handler records the failure; the job ends kFailed, the .new is deleted, no thread ends the process.
  for (int mode : {1, 2}) {
    World w("worker_body_throws_" + std::to_string(mode));
    w.WriteCurrent(w.Cur(), "cur", true);
    Hooks h = w.MakeHooks();
    h.on_write = [mode](int n) {
      if (n != 10) return;
      if (mode == 1) throw std::runtime_error("synthetic writer failure");
      throw 7;
    };
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, std::move(h), 3);
    CKF(Is(r, Outcome::kFailed) && Has(r.detail, "worker failed"), "mode %d: outcome %d (%s)", mode, int(r.outcome), r.detail.c_str());
    if (mode == 1) CKF(Has(r.detail, "synthetic writer failure"), "detail: %s", r.detail.c_str());
    CK(!Exists(w.New()));
    CK_SAME(before, SnapTree(w.ud), "user data after a worker body threw");
  }
}

// A folder of ours that lost its marker: empty, or holding only the marker's temporary file.
static void MakeOwnerless(const fs::path& dir, bool tmp) {
  fs::create_directories(dir);
  if (tmp) synth::WriteFileBytes(dir / ".doa4-suitconv.tmp", synth::Seeded(31, 19));
}

static void TestOwnerlessFolderRecovery() {
  Group("ownerless_folder_recovery");
  struct Case {
    const char* name;
    bool on_new, tmp;
  };
  const Case cases[] = {{"empty_new", true, false}, {"tmp_only_new", true, true}, {"empty_old", false, false}, {"tmp_only_old", false, true}};
  for (const Case& c : cases) {
    // (a) CompletePendingSwap: the folder is deleted, the current import untouched.
    {
      World w(std::string("ownerless_") + c.name + "_complete");
      w.WriteCurrent(w.Cur(), "cur", true);
      MakeOwnerless(c.on_new ? w.New() : w.Old(), c.tmp);
      const Snap cur_before = SnapTree(w.Cur());
      const std::string why = CompletePendingSwap(w.ud);
      CKF(why.empty(), "%s: %s", c.name, why.c_str());
      CKF(!Exists(w.New()) && !Exists(w.Old()), "%s: a folder remains", c.name);
      CKF(!Exists(w.ud / "doax2_suits.old-2"), "%s: renamed to .old-2", c.name);
      CK_SAME(cur_before, SnapTree(w.Cur()), "the current import");
    }
    // (b) Start: the same cleanup, then the import completes.
    {
      World w(std::string("ownerless_") + c.name + "_start");
      w.WriteCurrent(w.Cur(), "cur", true);
      MakeOwnerless(c.on_new ? w.New() : w.Old(), c.tmp);
      const ImportResult r = RunJob(w, w.MakeHooks(), 2);
      CKF(Is(r, Outcome::kImported), "%s: outcome %d (%s)", c.name, int(r.outcome), r.detail.c_str());
      CheckImportFolder(w, w.Cur(), {}, c.name);
      CKF(!Exists(w.New()) && !Exists(w.Old()) && !Exists(w.ud / "doax2_suits.old-2"), "%s: a folder remains", c.name);
    }
  }
  // An owner-less .old in the way of a pending swap (a verified .new, a current import): the swap's own clearing of .old
  // deletes it, it is not kept as .old-2.
  for (bool tmp : {false, true}) {
    World w(std::string("ownerless_old_pending_new") + (tmp ? "_tmp" : "_empty"));
    SetupRow(w, 2);  // the current import and a verified .new
    MakeOwnerless(w.Old(), tmp);
    const Snap new_before = SnapTree(w.New());
    const std::string why = CompletePendingSwap(w.ud);
    CKF(why.empty(), "%s", why.c_str());
    CK_SAME(new_before, SnapTree(w.Cur()), "the swapped-in import");
    CKF(!Exists(w.New()) && !Exists(w.Old()) && !Exists(w.ud / "doax2_suits.old-2"), "tmp=%d: a folder remains", int(tmp));
  }
  // Not ours, still refused and untouched: one foreign file; the temp file plus a foreign file; only a sub-folder.
  for (int variant = 0; variant < 3; ++variant) {
    World w("ownerless_foreign_" + std::to_string(variant));
    w.WriteCurrent(w.Cur(), "cur", true);
    fs::create_directories(w.New());
    if (variant == 0) WriteText(w.New() / "stray.txt", "someone else's folder\n");
    if (variant == 1) {
      synth::WriteFileBytes(w.New() / ".doa4-suitconv.tmp", synth::Seeded(32, 19));
      WriteText(w.New() / "stray.txt", "someone else's folder\n");
    }
    if (variant == 2) fs::create_directories(w.New() / "sub");
    const Snap before = SnapTree(w.ud);
    const ImportResult r = RunJob(w, w.MakeHooks(), 2);
    CKF(Is(r, Outcome::kFailed) && Has(r.detail, w.New().string().c_str()), "variant %d: outcome %d (%s)", variant, int(r.outcome), r.detail.c_str());
    CK_SAME(before, SnapTree(w.ud), "user data (a folder that is not ours)");
    const std::string why = CompletePendingSwap(w.ud);
    CKF(Has(why, w.New().string().c_str()), "variant %d: CompletePendingSwap: %s", variant, why.c_str());
    CK_SAME(before, SnapTree(w.ud), "user data after CompletePendingSwap");
  }
  // A .old that is a foreign folder with only a sub-folder is kept (renamed .old-2), as any Python import is.
  {
    World w("ownerless_old_subfolder");
    w.WriteCurrent(w.Cur(), "cur", true);
    fs::create_directories(w.Old() / "sub");
    const ImportResult r = RunJob(w, w.MakeHooks(), 2);
    CKF(Is(r, Outcome::kImported), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CK(Exists(w.ud / "doax2_suits.old-2" / "sub"));
    CK(!Exists(w.Old()));
  }
}

// RemoveOurs deletes the marker before the folder: when the folder itself cannot go (another program holds a handle on it),
// the marker is put back, so the next try still knows the folder. (Without it the folder would be an empty stranger.)
static void TestHeldFolderKeepsMarker() {
  Group("held_folder_keeps_marker");
  World w("held_folder");
  w.WriteCurrent(w.Cur(), "cur", true);
  fs::create_directories(w.New());
  WriteText(w.New() / kMarker, "doa4 suit importer\n");
  synth::WriteFileBytes(w.New() / "a_suit.tpr", synth::Seeded(3, 500));  // no manifest: an interrupted import
  HANDLE dir = CreateFileW(w.New().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  CK(dir != INVALID_HANDLE_VALUE);
  const std::string why1 = CompletePendingSwap(w.ud);
  CKF(!why1.empty(), "the folder is held: a failed cleanup says why");
  CK(Exists(w.New()));
  CKF(Exists(w.New() / kMarker), "the folder lost its marker while it could not be deleted");
  CK(!Exists(w.New() / "a_suit.tpr"));
  CloseHandle(dir);
  const std::string why2 = CompletePendingSwap(w.ud);
  CKF(why2.empty(), "%s", why2.c_str());
  CK(!Exists(w.New()));
}

// A swap whose rename is blocked by an open file is retried (10 tries over about a second, the token looked at between tries).
static void TestSwapRetriesBlockedRename() {
  Group("swap_retries_blocked_rename");
  // (a) The game holds a file of the current import for 300 ms of the swap: the first rename waits and the import completes.
  // (b) The same for a file of .new: the second rename waits.
  for (int which = 0; which < 2; ++which) {
    World w(which == 0 ? "retry_first_rename" : "retry_second_rename");
    w.WriteCurrent(w.Cur(), "cur", true);
    std::shared_ptr<doa4::afs::FileSource> held;
    std::thread releaser;
    Clock::time_point t_swap;
    w.phase_hook = [&](Phase p) {
      if (p != Phase::kSwapping) return;
      t_swap = Clock::now();
      held = doa4::afs::FileSource::Open(which == 0 ? w.Cur() / "cur_a.bin" : w.New() / "manifest.txt");
      releaser = std::thread([&held] {
        SleepMs(300);
        held.reset();
      });
    };
    const ImportResult r = RunJob(w, w.MakeHooks(), 2);
    const long long ms = MsSince(t_swap);
    if (releaser.joinable()) releaser.join();
    CKF(Is(r, Outcome::kImported), "%s: outcome %d (%s)", which == 0 ? "first" : "second", int(r.outcome), r.detail.c_str());
    CKF(ms >= 250, "%s: the swap took %lld ms: it did not wait for the open file", which == 0 ? "first" : "second", ms);
    CheckImportFolder(w, w.Cur(), {}, "the import after a short block");
    CK(!Exists(w.New()) && !Exists(w.Old()));
  }
  // (c) A file held for the whole retry window: kSwapPending as before, after about a second of tries.
  {
    World w("retry_window_exhausted");
    w.WriteCurrent(w.Cur(), "cur", true);
    std::shared_ptr<doa4::afs::FileSource> held = doa4::afs::FileSource::Open(w.Cur() / "cur_a.bin");
    CK(held != nullptr);
    Clock::time_point t_swap;
    w.phase_hook = [&](Phase p) {
      if (p == Phase::kSwapping) t_swap = Clock::now();
    };
    const ImportResult r = RunJob(w, w.MakeHooks(), 2);
    const long long ms = MsSince(t_swap);
    CKF(Is(r, Outcome::kSwapPending), "outcome %d (%s)", int(r.outcome), r.detail.c_str());
    CKF(ms >= 800 && ms <= 6000, "the retries took %lld ms (wanted about 1000)", ms);
    CK(Exists(w.New()) && VerifyImportFolder(w.New()).empty());
  }
  // (d) A cancel during the retries is seen between tries: the import is abandoned at once (nothing was moved).
  {
    World w("retry_cancelled");
    w.WriteCurrent(w.Cur(), "cur", true);
    std::shared_ptr<doa4::afs::FileSource> held = doa4::afs::FileSource::Open(w.Cur() / "cur_a.bin");
    CK(held != nullptr);
    const Snap cur_before = SnapTree(w.Cur());
    std::atomic<ImportJob*> job_ptr{nullptr};
    std::atomic<long long> cancelled_at{0};
    std::thread canceller;
    Clock::time_point t0;
    w.phase_hook = [&](Phase p) {
      if (p == Phase::kCheckingSpace) SleepMs(30);  // the test stores the job's address first
      if (p != Phase::kSwapping) return;
      t0 = Clock::now();
      canceller = std::thread([&] {
        SleepMs(100);
        cancelled_at = MsSince(t0);
        if (ImportJob* j = job_ptr.load()) j->Cancel();
      });
    };
    auto job = ImportJob::StartForTest(w.Req(2), w.MakeHooks());
    job_ptr = job.get();
    CK(WaitDone(*job, 30000));
    const long long done_ms = MsSince(t0);
    if (canceller.joinable()) canceller.join();
    CKF(Is(job->result(), Outcome::kCancelled), "outcome %d (%s)", int(job->result().outcome), job->result().detail.c_str());
    CKF(done_ms - cancelled_at.load() <= 400, "done %lld ms after the cancel", done_ms - cancelled_at.load());
    CK(!Exists(w.New()) && !Exists(w.Old()));
    CK_SAME(cur_before, SnapTree(w.Cur()), "the current import");
  }
}

// The free-space check counts the bytes of a leftover .new that the recovery will delete: one with our marker, an empty one, one
// holding only the marker's temporary file. A foreign .new frees nothing.
static void TestFreeSpaceCountsLeftover() {
  Group("free_space_counts_leftover");
  const auto Sum = [](const fs::path& dir) {
    uint64_t total = 0;
    for (const auto& [k, v] : Sizes(dir))
      if (v != uint64_t(-1)) total += v;
    return total;
  };
  // kind: 0 marked interrupted, 1 temp-marker only, 2 foreign (no marker), 3 nothing, 4 empty
  for (int kind = 0; kind <= 4; ++kind) {
    for (int edge = 0; edge < 2; ++edge) {  // free = needed - leftover (proceeds), one byte less (refused)
      if (kind >= 2 && edge > 0) continue;
      World w("free_space_" + std::to_string(kind) + "_" + std::to_string(edge));
      w.WriteCurrent(w.Cur(), "cur", true);
      fs::create_directories(w.New());
      if (kind == 0) {
        WriteText(w.New() / kMarker, "doa4 suit importer\n");
        WriteSparse(w.New() / "leftover.tpr", 10u << 20);
      } else if (kind == 1) {
        WriteSparse(w.New() / ".doa4-suitconv.tmp", 10u << 20);
      } else if (kind == 2) {
        WriteSparse(w.New() / "stray.bin", 10u << 20);
      } else {
        fs::remove_all(w.New());
        if (kind == 4) fs::create_directories(w.New());
      }
      const uint64_t need = BytesNeeded(w.suits);
      const uint64_t counted = (kind == 0 || kind == 1) ? Sum(w.New()) : 0;
      const bool proceeds = (kind == 0 || kind == 1) ? edge == 0 : false;
      w.free_value = need - counted - (edge == 0 ? 0 : 1);
      if (kind >= 2) w.free_value = need - 1000;
      const Snap before = SnapTree(w.ud);
      const ImportResult r = RunJob(w, w.MakeHooks(), 2);
      if (proceeds) {
        CKF(Is(r, Outcome::kImported), "kind %d edge %d: outcome %d (%s)", kind, edge, int(r.outcome), r.detail.c_str());
        CheckImportFolder(w, w.Cur(), {}, "the import over a leftover");
      } else {
        CKF(Is(r, Outcome::kNoSpace), "kind %d edge %d: outcome %d (%s)", kind, edge, int(r.outcome), r.detail.c_str());
        CK_SAME(before, SnapTree(w.ud), "user data after kNoSpace");
      }
    }
  }
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
// The real sections (gated by DOA4_SUITCONV_DISC, DOA4_SUITCONV_GAME_DIR and DOA4_SUITCONV_ORACLE_DIR)
// ---------------------------------------------------------------------------------------------------------------
// They run the production job and the converter on the player's own disc and DOA4 game files and compare the files with the
// folder the Python converter wrote (ORACLE_DIR: the 68 suit files and manifest.txt), by size and SHA-1. Every import goes to
// %TEMP%\doa4_suitconv_real\<case>\ud, deleted when its case starts. Nothing under the disc, the game folder or the oracle
// folder is written. A section whose variables are unset prints "<group>: SKIPPED (...)" and counts as skipped.
// DOA4_SUITCONV_ONLY=real runs only these sections (a measurement that must not share the process with the fake-world groups),
// =fake only the fake-world groups, =<section name> only that section.

struct FileInfo {
  uint64_t size = 0;
  std::string sha1;
  bool operator==(const FileInfo& o) const { return size == o.size && sha1 == o.sha1; }
};
using Files = std::map<std::string, FileInfo>;

struct RealEnv {
  std::string disc, game, oracle;
};

// DOA4_SUITCONV_ONLY: unset runs everything; otherwise a comma separated list of "real" (every real section), "fake" (every
// fake-world group) and real section names (real_time_and_memory alone, for a clean memory figure).
static bool OnlyHas(const char* token) {
  const char* only = std::getenv("DOA4_SUITCONV_ONLY");
  if (!only || !*only) return true;
  const std::string list = std::string(",") + only + ",", want = std::string(",") + token + ",";
  return list.find(want) != std::string::npos;
}

static bool GetRealEnv(const char* group, RealEnv* e) {
  if (!OnlyHas("real") && !OnlyHas(group)) return false;
  const char* d = std::getenv("DOA4_SUITCONV_DISC");
  const char* g = std::getenv("DOA4_SUITCONV_GAME_DIR");
  const char* o = std::getenv("DOA4_SUITCONV_ORACLE_DIR");
  if (!d || !*d || !g || !*g || !o || !*o) {
    std::printf("%s: SKIPPED (DOA4_SUITCONV_DISC, DOA4_SUITCONV_GAME_DIR or DOA4_SUITCONV_ORACLE_DIR unset)\n", group);
    ++g_skipped;
    return false;
  }
  e->disc = d;
  e->game = g;
  e->oracle = o;
  Group(group);
  std::printf("%s: disc %s\n", group, e->disc.c_str());
  return true;
}

static bool ReadAll(const fs::path& p, std::vector<uint8_t>* out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

// Every file directly inside `dir` with its size and SHA-1; a sub-folder or an unreadable file is named in *problem.
static Files ListFiles(const fs::path& dir, std::string* problem) {
  Files m;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    std::error_code e2;
    std::vector<uint8_t> bytes;
    if (!it->is_regular_file(e2) || !ReadAll(it->path(), &bytes)) {
      *problem += " unreadable-or-not-a-file:" + name;
      continue;
    }
    m[name] = FileInfo{uint64_t(bytes.size()), doa4::gamefiles::Sha1Hex(bytes.data(), bytes.size())};
  }
  if (ec) *problem += " cannot-list:" + dir.string();
  return m;
}

static Files LoadOracle(const RealEnv& env) {
  std::string problem;
  Files f = ListFiles(fs::path(env.oracle), &problem);
  CKF(problem.empty(), "oracle folder:%s", problem.c_str());
  CKF(f.size() == 69 && f.count("manifest.txt") == 1, "oracle folder holds %zu files (69 expected: 68 suit files and manifest.txt)",
      f.size());
  return f;
}

// `got` equals `want` file by file (size and SHA-1), plus `extra` names in got only, and nothing else: the first differences
// are named.
static void CompareFiles(const std::string& what, const Files& got, const Files& want, const std::set<std::string>& extra) {
  std::string diff;
  int n = 0;
  for (const auto& [name, w] : want) {
    const auto it = got.find(name);
    std::string d;
    if (it == got.end()) d = " missing:" + name;
    else if (it->second.size != w.size) d = " size:" + name + "(" + std::to_string(it->second.size) + " vs " + std::to_string(w.size) + ")";
    else if (it->second.sha1 != w.sha1) d = " sha1:" + name;
    if (!d.empty() && n++ < 6) diff += d;
  }
  for (const auto& [name, g] : got)
    if (!want.count(name) && !extra.count(name) && n++ < 6) diff += " extra:" + name;
  CKF(diff.empty(), "%s differs from the reference:%s (%d file(s))", what.c_str(), diff.c_str(), n);
  CKF(got.size() == want.size() + extra.size(), "%s: %zu files, expected %zu", what.c_str(), got.size(), want.size() + extra.size());
}

static fs::path RealUserData(const std::string& case_name) {
  const fs::path base = fs::temp_directory_path() / "doa4_suitconv_real" / case_name;
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base / "ud");
  return base / "ud";
}

struct RealRun {
  ImportResult result;
  bool finished = false;
  Files files;  // what doax2_suits holds afterwards
  double wall_s = 0;
  fs::path ud;
  std::string verify;                                  // VerifyImportFolder of the new import
  std::vector<double> suit_ms;                          // per suit, picks order, when timed
  std::map<int, Clock::time_point> phase_at;            // when each phase began (Phase as int)
  Clock::time_point start, end;
  uint64_t ws_before = 0, ws_peak_sampled = 0, peak_before = 0, peak_after = 0;
};

static uint64_t WorkingSet(bool peak) {
  PROCESS_MEMORY_COUNTERS pmc{};
  pmc.cb = sizeof pmc;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return 0;
  return peak ? uint64_t(pmc.PeakWorkingSetSize) : uint64_t(pmc.WorkingSetSize);
}

// What a real import needs besides the request: a converter wrapper (timing, FMA3 switch), recorded phases, memory samples.
struct RealOptions {
  int workers = 0;
  bool timed = false;                // record per-suit conversion times, phase times and the working set
  int fma3 = -1;                     // when 0 or 1: _set_FMA3_enable(fma3) on the converting thread before each conversion
};

// One production import (the job's own hooks, a wrapper around ConvertSuit when timing or switching FMA3) into
// %TEMP%\doa4_suitconv_real\<case>\ud.
static RealRun RunRealImport(const RealEnv& env, const std::string& case_name, const RealOptions& opt) {
  RealRun run;
  run.ud = RealUserData(case_name);
  ImportRequest req;
  req.doax2 = fs::path(env.disc);
  req.game_root = fs::path(env.game);
  req.user_data = run.ud;
  req.workers = opt.workers;
  run.suit_ms.assign(Suits().size(), 0.0);
  std::mutex mu;
  Hooks h;
  const bool wrap = opt.timed || opt.fma3 >= 0;
  if (wrap) {
    h.convert = [&](const SuitRecipe& s, const GirlRecipe& g, const SuitInputs& in, const CancelToken& tok,
                    ConvertFailure* f) -> std::optional<SuitOutput> {
      if (opt.fma3 >= 0) _set_FMA3_enable(opt.fma3);
      const auto t0 = Clock::now();
      auto r = ConvertSuit(s, g, in, Level::kM1, &tok, nullptr, f);
      const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
      run.suit_ms[size_t(&s - Suits().data())] = ms;
      return r;
    };
  }
  if (opt.timed) {
    h.on_phase = [&](Phase p) {
      std::lock_guard<std::mutex> lk(mu);
      run.phase_at[int(p)] = Clock::now();
    };
  }
  std::atomic<bool> sampling{true};
  std::thread sampler;
  if (opt.timed) {
    run.ws_before = WorkingSet(false);
    run.peak_before = WorkingSet(true);
    run.ws_peak_sampled = run.ws_before;
    sampler = std::thread([&] {
      while (sampling.load()) {
        run.ws_peak_sampled = (std::max)(run.ws_peak_sampled, WorkingSet(false));
        SleepMs(5);
      }
    });
  }
  run.start = Clock::now();
  auto job = wrap ? ImportJob::StartForTest(req, std::move(h)) : ImportJob::Start(req);
  run.finished = WaitDone(*job, 20 * 60 * 1000);
  run.end = Clock::now();
  if (!run.finished) job->Cancel();
  WaitDone(*job, 30000);
  sampling = false;
  if (sampler.joinable()) sampler.join();
  if (opt.timed) {
    run.ws_peak_sampled = (std::max)(run.ws_peak_sampled, WorkingSet(false));
    run.peak_after = WorkingSet(true);
  }
  run.wall_s = std::chrono::duration<double>(run.end - run.start).count();
  run.result = job->result();
  job.reset();
  const fs::path cur = run.ud / "doax2_suits";
  std::string problem;
  run.files = ListFiles(cur, &problem);
  CKF(problem.empty(), "%s: import folder:%s", case_name.c_str(), problem.c_str());
  run.verify = VerifyImportFolder(cur);
  return run;
}

// An import ended well and its folder holds the 69 files of the reference and the marker, every one equal.
static void CheckRealImport(const char* what, const RealRun& run, const Files& oracle) {
  const ImportResult& r = run.result;
  CKF(run.finished, "%s: the import did not finish in 20 minutes", what);
  CKF(Is(r, Outcome::kImported), "%s: outcome %d (%s)", what, int(r.outcome), r.detail.c_str());
  CKF(r.differs_from_reference.empty() && r.skipped.empty(), "%s: %zu differ from the reference build, %zu skipped", what,
      r.differs_from_reference.size(), r.skipped.size());
  CKF(run.verify.empty(), "%s: VerifyImportFolder: %s", what, run.verify.c_str());
  CompareFiles(what, run.files, oracle, {kMarker});
  CKF(run.files.count(kMarker) == 1, "%s: no marker file", what);
}

// real_import_equals_oracle: one production import, the folder equals the reference byte for byte.
// The game's costume tables as the game holds them in memory (read from your own image, DOA4_TEST_IMAGE, as the loader's own
// host test does), for doa4::suits::LoadImport.
struct FixtureGuest {
  std::map<uint32_t, uint8_t> bytes;
  doa4::GuestBytes guest{[this](uint32_t a) {
                           const auto it = bytes.find(a);
                           return it == bytes.end() ? uint8_t(0) : it->second;
                         },
                         [this](uint32_t a, uint8_t v) { bytes[a] = v; }};
  explicit FixtureGuest(const doa4_test::CostumeImage& image) {
    Load(doa4_test::kCostumeTablesRange.address, image.tables);
    Load(doa4_test::kCostumeRecordsRange.address, image.records);
    Load(doa4_test::kCostumeFilesRange.address, image.files);
  }
  void Load(uint32_t start, const std::vector<uint8_t>& data) {
    for (size_t i = 0; i < data.size(); ++i) bytes[start + uint32_t(i)] = data[i];
  }
};

static void TestRealImportEqualsOracle() {
  RealEnv env;
  if (!GetRealEnv("real_import_equals_oracle", &env)) return;
  const Files oracle = LoadOracle(env);
  const RealRun run = RunRealImport(env, "equals_oracle", {});
  CheckRealImport("real import", run, oracle);
  CK(run.files.size() == 70);  // the 68 suit files, manifest.txt and the marker
  // The folder the job wrote is accepted by the game's own loader: all 34 suits pass every check, none skipped. (The loader
  // keeps the files open for the rest of the process.)
  {
    std::string why;
    if (const std::optional<doa4_test::CostumeImage> image = doa4_test::LoadCostumeImage(&why)) {
      FixtureGuest guest(*image);
      const dsuits::ImportStatus st = dsuits::LoadImport(run.ud / "doax2_suits", fs::path(env.game), guest.guest);
      CKF(st.problem.empty(), "LoadImport: %s", st.problem.c_str());
      CKF(st.suits.size() == 34 && st.skipped.empty(), "LoadImport accepted %zu suits, skipped %zu (%s)", st.suits.size(),
          st.skipped.size(), st.skipped.empty() ? "" : st.skipped[0].c_str());
      std::printf("real_import_equals_oracle: LoadImport accepted %zu of 34\n", st.suits.size());
    } else if (doa4_test::ImageIsUnset(why)) {
      std::printf("real_import_equals_oracle: LoadImport check: SKIPPED (%s)\n", why.c_str());
      ++g_skipped;
    } else {
      CKF(false, "LoadImport check: DOA4_TEST_IMAGE is set but the image tier cannot run: %s", why.c_str());
    }
  }
  std::printf("real_import_equals_oracle: outcome %d, %zu files compared with %zu of the reference, %.1f s\n",
              int(run.result.outcome), run.files.size(), oracle.size(), run.wall_s);
}

// real_import_worker_counts: 1, 4 and 8 workers give the same folder, file by file, and each equals the reference.
static void TestRealImportWorkerCounts() {
  RealEnv env;
  if (!GetRealEnv("real_import_worker_counts", &env)) return;
  const Files oracle = LoadOracle(env);
  std::vector<RealRun> runs;
  for (int w : {1, 4, 8}) {
    runs.push_back(RunRealImport(env, "workers_" + std::to_string(w), {w}));
    CheckRealImport(("import with " + std::to_string(w) + " worker(s)").c_str(), runs.back(), oracle);
    std::printf("real_import_worker_counts: %d worker(s): %.1f s\n", w, runs.back().wall_s);
  }
  CompareFiles("4 workers against 1", runs[1].files, runs[0].files, {});
  CompareFiles("8 workers against 1", runs[2].files, runs[0].files, {});
}

// Every suit read and converted straight through ConvertSuit (no job), in the given picks order, on the calling thread unless
// `threads` > 1 (then suit i goes to thread i % threads); each thread first runs `setup`. Compared with the reference by size and
// SHA-1; returns how many of the 68 files differ.
static int ConvertAllDirect(const RealEnv& env, const Files& oracle, const std::vector<int>& order, int threads,
                            const std::function<void()>& setup, const std::function<void()>& after_each, const char* what) {
  std::string err;
  auto set = InputSet::Open(fs::path(env.disc), fs::path(env.game), DiscEntries(), GameEntries(), &err);
  CKF(set != nullptr, "%s: cannot open the inputs: %s", what, err.c_str());
  if (!set) return 68;
  const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
  std::atomic<int> bad{0};
  std::mutex mu;
  std::string diff;
  auto work = [&](int tid) {
    setup();
    for (size_t k = size_t(tid); k < order.size(); k += size_t(threads)) {
      const size_t i = size_t(order[k]);
      const SuitRecipe& s = Suits()[i];
      const GirlRecipe* girl = FindGirl(s.girl);
      std::optional<SuitOutput> out;
      ConvertFailure f;
      try {
        out = ConvertSuit(s, *girl, ReadSuitInputs(*set, s, *girl), Level::kM1, nullptr, nullptr, &f);
      } catch (const std::exception& e) {
        f.what = e.what();
      }
      after_each();
      std::lock_guard<std::mutex> lk(mu);
      if (!out) {
        bad += 2;
        diff += " failed:" + std::string(s.design) + "(" + f.stage + ": " + f.what + ")";
        continue;
      }
      const auto check = [&](const std::string& name, const std::vector<uint8_t>& b) {
        const auto it = oracle.find(name);
        if (it == oracle.end() || it->second.size != b.size() ||
            it->second.sha1 != doa4::gamefiles::Sha1Hex(b.data(), b.size())) {
          ++bad;
          if (diff.size() < 300) diff += " " + name;
        }
      };
      check(picks[i].tpr.name, out->tpr);
      check(picks[i].cat.name, out->cat);
    }
  };
  if (threads <= 1) {
    work(0);
  } else {
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t) ts.emplace_back(work, t);
    for (std::thread& t : ts) t.join();
  }
  CKF(bad == 0, "%s: differs from the reference:%s", what, diff.c_str());
  return bad;
}

// real_reverse_order_one_process: ConvertSuit over the 34 in reverse picks order, in one process, one thread: equal files.
static void TestRealReverseOrder() {
  RealEnv env;
  if (!GetRealEnv("real_reverse_order_one_process", &env)) return;
  const Files oracle = LoadOracle(env);
  std::vector<int> order;
  for (int i = int(Suits().size()) - 1; i >= 0; --i) order.push_back(i);
  CK(order.size() == 34 && order.front() == 33 && order.back() == 0);
  const auto t0 = Clock::now();
  const int bad = ConvertAllDirect(env, oracle, order, 1, [] {}, [] {}, "reverse order");
  std::printf("real_reverse_order_one_process: 34 suits, %d of 68 files differ, %.1f s\n", bad,
              std::chrono::duration<double>(Clock::now() - t0).count());
}

// real_fma3_toggle: the C runtime's FMA3 switch off and on, set on each converting thread before each conversion, through the
// production job; equal to the reference both times.
static void TestRealFma3Toggle() {
  RealEnv env;
  if (!GetRealEnv("real_fma3_toggle", &env)) return;
  const Files oracle = LoadOracle(env);
  for (int mode : {0, 1}) {
    const int back = _set_FMA3_enable(mode);
    std::printf("real_fma3_toggle: _set_FMA3_enable(%d) returned %d\n", mode, back);
    const RealRun run = RunRealImport(env, "fma3_" + std::to_string(mode), {0, false, mode});
    CheckRealImport(mode ? "FMA3 on" : "FMA3 off", run, oracle);
    std::printf("real_fma3_toggle: FMA3 %s: outcome %d, %.1f s\n", mode ? "on" : "off", int(run.result.outcome), run.wall_s);
  }
  _set_FMA3_enable(1);
}

// real_dirty_mxcsr: FTZ, DAZ and round-up set on the calling thread before the job starts, and on four threads that call
// ConvertSuit directly (each must find its own control word as it left it afterwards); equal to the reference.
static constexpr unsigned kDirtyBits = 0x8000u | 0x0040u | 0x4000u;  // FTZ, DAZ, rounding control 01 = up (bit 13)
static void TestRealDirtyMxcsr() {
  RealEnv env;
  if (!GetRealEnv("real_dirty_mxcsr", &env)) return;
  const Files oracle = LoadOracle(env);
  const unsigned saved = _mm_getcsr();
  _mm_setcsr(saved | kDirtyBits);
  CKF((_mm_getcsr() & kDirtyBits) == kDirtyBits, "the dirty control word did not stick: %08x", _mm_getcsr());
  const RealRun run = RunRealImport(env, "dirty_mxcsr", {});
  CKF((_mm_getcsr() & kDirtyBits) == kDirtyBits, "the job changed the calling thread's control word: %08x", _mm_getcsr());
  _mm_setcsr(saved);
  CheckRealImport("import started from a thread with FTZ, DAZ and round-up", run, oracle);
  std::atomic<int> clobbered{0};
  std::vector<int> order;
  for (int i = 0; i < int(Suits().size()); ++i) order.push_back(i);
  ConvertAllDirect(
      env, oracle, order, 4, [] { _mm_setcsr(_mm_getcsr() | kDirtyBits); },
      [&] {
        if ((_mm_getcsr() & kDirtyBits) != kDirtyBits) ++clobbered;
      },
      "dirty control word on the converting threads");
  CKF(clobbered == 0, "ConvertSuit left %d thread(s) with a changed control word", clobbered.load());
  std::printf("real_dirty_mxcsr: job and 4 direct threads under FTZ, DAZ and round-up, %d control word change(s)\n", clobbered.load());
}

// ---- timing and memory ----
// The Python converter's time per suit at m1 (convert.main only, seconds, one suit at a time, below-normal priority, no trace)
// on the author's PC, in picks order: the C++ must not be slower on any suit.
struct PyTime {
  const char* design;
  double seconds;
};
static const PyTime kPythonSeconds[] = {
    {"vks_ch_d", 0.821}, {"vks_cs_a", 1.74},  {"vks_on_o", 0.844}, {"vks_hn_i", 1.453}, {"vay_cs_b", 1.086},
    {"vay_on_k", 0.838}, {"vay_sk_d", 0.827}, {"vht_cs_f", 1.004}, {"vht_sp_h", 0.793}, {"vht_on_e", 0.83},
    {"vks_wi_b", 0.826}, {"vco_cs_i", 1.295}, {"vco_sk_c", 2.001}, {"vhe_on_c", 0.877}, {"vco_ch_f", 0.77},
    {"vle_cs_c", 0.82},  {"vle_on_i", 0.996}, {"vle_sk_h", 0.878}, {"vle_dl_a", 0.767}, {"vhe_cm_a", 0.805},
    {"vhe_on_d", 0.775}, {"vhe_sk_a", 1.824}, {"vhe_dl_l", 0.848}, {"vls_cs_h", 1.256}, {"vls_on_j", 0.726},
    {"vls_sp_a", 0.803}, {"vcr_cs_g", 1.077}, {"vcr_on_n", 0.751}, {"vcr_ch_e", 0.796}, {"vcr_cm_c", 2.067},
    {"vtn_cs_e", 2.974}, {"vtn_ch_i", 0.83},  {"vtn_sh_c", 2.048}, {"vls_on_b", 0.889},
};
static constexpr double kTotalGateSeconds = 60.0;  // an import (default workers) over this fails
static constexpr double kPhaseReportSeconds = 30.0;  // over this the report names the phase that holds the time
static constexpr double kMemoryTargetMiB = 1024.0;   // reported against, not gated

static const char* PhaseName(int p) {
  switch (Phase(p)) {
    case Phase::kCheckingSpace: return "space";
    case Phase::kCheckingDisc: return "check disc";
    case Phase::kCheckingGame: return "check game";
    case Phase::kConverting: return "convert";
    case Phase::kWriting: return "write";
    case Phase::kSwapping: return "swap";
    case Phase::kDone: return "done";
  }
  return "?";
}

// Prints each phase's duration (from its start to the next phase's, the last to the end); returns the longest phase's name
// and seconds.
static std::pair<std::string, double> PrintPhases(const char* label, const RealRun& run) {
  std::string longest;
  double longest_s = 0;
  std::string line;
  for (auto it = run.phase_at.begin(); it != run.phase_at.end(); ++it) {
    if (it->first == int(Phase::kDone)) continue;
    const auto next = std::next(it);
    const Clock::time_point to = next == run.phase_at.end() ? run.end : next->second;
    const double s = std::chrono::duration<double>(to - it->second).count();
    char buf[96];
    std::snprintf(buf, sizeof buf, " %s %.2f s;", PhaseName(it->first), s);
    line += buf;
    if (s > longest_s) {
      longest_s = s;
      longest = PhaseName(it->first);
    }
  }
  std::printf("  %s phases:%s\n", label, line.c_str());
  return {longest, longest_s};
}

static void PrintMemory(const char* label, const RealRun& run) {
  const double mib = 1024.0 * 1024.0;
  const double rise_sampled = (double(run.ws_peak_sampled) - double(run.ws_before)) / mib;
  std::printf("  %s memory: working set %.0f MiB before, sampled maximum %.0f MiB (rise %.0f MiB, sampled every 5 ms)", label,
              double(run.ws_before) / mib, double(run.ws_peak_sampled) / mib, rise_sampled);
  if (run.peak_after > run.peak_before)  // the process peak moved during this import: it is the import's own peak
    std::printf(", process peak %.0f MiB (rise %.0f MiB)", double(run.peak_after) / mib,
                (double(run.peak_after) - double(run.ws_before)) / mib);
  else
    std::printf(", process peak %.0f MiB was reached earlier in the process", double(run.peak_after) / mib);
  std::printf("; target %.0f MiB\n", kMemoryTargetMiB);
}

static void TestRealTimeAndMemory() {
  RealEnv env;
  if (!GetRealEnv("real_time_and_memory", &env)) return;
  const Files oracle = LoadOracle(env);
  // one worker: each suit's conversion time
  const RealRun one = RunRealImport(env, "time_1_worker", {1, true});
  CheckRealImport("timed import, 1 worker", one, oracle);
  // the default worker count: the wall time
  const RealRun def = RunRealImport(env, "time_default_workers", {0, true});
  CheckRealImport("timed import, default workers", def, oracle);

  std::printf("real_time_and_memory: wall time, 1 worker %.2f s, default workers %.2f s\n", one.wall_s, def.wall_s);
  const auto longest1 = PrintPhases("1 worker", one);
  const auto longest_def = PrintPhases("default workers", def);
  PrintMemory("1 worker", one);
  PrintMemory("default workers", def);
  std::printf("  %-10s %9s %9s %7s\n", "suit", "C++ (s)", "Python (s)", "ratio");
  double cpp_total = 0, py_total = 0;
  int slower = 0, found = 0;
  for (size_t i = 0; i < Suits().size(); ++i) {
    const double cpp = one.suit_ms[i] / 1000.0;
    double py = -1;
    for (const PyTime& t : kPythonSeconds)
      if (std::string(t.design) == Suits()[i].design) py = t.seconds;
    found += py > 0;
    cpp_total += cpp;
    py_total += py > 0 ? py : 0;
    const bool bad = py > 0 && cpp > py;
    slower += bad;
    std::printf("  %-10s %9.3f %9.3f %7.2f%s\n", Suits()[i].design, cpp, py, py > 0 ? cpp / py : 0.0, bad ? "  SLOWER" : "");
    CKF(py > 0 && cpp > 0 && !bad, "%s: C++ %.3f s against Python %.3f s", Suits()[i].design, cpp, py);
  }
  CKF(found == 34, "%d of 34 suits have a Python time", found);
  std::printf("  total conversion: C++ %.2f s, Python %.2f s; %d suit(s) slower than Python\n", cpp_total, py_total, slower);
  if (def.wall_s > kPhaseReportSeconds)
    std::printf("  the import takes %.1f s (over %.0f s): the longest phase is '%s' with %.1f s\n", def.wall_s, kPhaseReportSeconds,
                longest_def.first.c_str(), longest_def.second);
  if (one.wall_s > kPhaseReportSeconds)
    std::printf("  the 1-worker import takes %.1f s (over %.0f s): the longest phase is '%s' with %.1f s\n", one.wall_s,
                kPhaseReportSeconds, longest1.first.c_str(), longest1.second);
  CKF(def.wall_s <= kTotalGateSeconds, "the import takes %.1f s, over the %.0f s gate (longest phase '%s' %.1f s)", def.wall_s,
      kTotalGateSeconds, longest_def.first.c_str(), longest_def.second);
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash must still show how far the run got
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);  // a crash under test ends the process without a dialog
  const bool run_fake = OnlyHas("fake");
  const bool run_real = true;  // each real section decides for itself (GetRealEnv)
  try {
    if (run_fake) {
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
    }
    if (Want("final_guard")) TestInProcessGuard();
    if (Want("final_cross_process")) TestCrossProcessGuard();
    if (Want("final_ownerless")) TestOwnerlessFolderRecovery();
    if (Want("final_held_folder")) TestHeldFolderKeepsMarker();
    if (Want("final_swap_retry")) TestSwapRetriesBlockedRename();
    if (Want("final_free_space")) TestFreeSpaceCountsLeftover();
    if (Want("final_throwing_log")) TestThrowingLogCallback();
    if (Want("final_destroy")) TestDestroyFromLogCallback();
    if (run_real) {
      TestRealImportEqualsOracle();
      TestRealImportWorkerCounts();
      TestRealReverseOrder();
      TestRealFma3Toggle();
      TestRealDirtyMxcsr();
      TestRealTimeAndMemory();
    }
    if (run_fake) TestHookThrowsNonStdException();
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
