// Suit converter (Plan 2): the import job. See doa4_suitconv_job.h.

#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_job.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>

#include "doa4_gamefiles.h"
#include "doa4_suits_manifest.h"
#include "doa4_suits_picks.h"
#include "doa4_suitconv_num.h"

namespace doa4::suitconv {

namespace fs = std::filesystem;
namespace dsuits = doa4::suits;

namespace {

constexpr wchar_t kMarkerName[] = L".doa4-suitconv";
constexpr char kMarkerText[] = "doa4 suit importer\n";
constexpr uint64_t kManifestLimit = uint64_t(1) << 20;
constexpr uint64_t kReserve = uint64_t(64) << 20;  // headroom on top of the outputs
constexpr double kCheckWeight = 0.15, kConvertWeight = 0.80;
constexpr auto kPoke = std::chrono::milliseconds(10);  // CancelSynchronousIo is repeated this often

using Logger = std::function<void(const std::string&)>;

std::string Utf8(const fs::path& p) {
  const std::wstring w = p.wstring();
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n : 0), '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string WinError(DWORD e) { return "Windows error " + std::to_string(e); }

bool Exists(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p, ec);
}

fs::path Cur(const fs::path& ud) { return ud / L"doax2_suits"; }
fs::path New(const fs::path& ud) { return ud / L"doax2_suits.new"; }
fs::path Old(const fs::path& ud) { return ud / L"doax2_suits.old"; }

bool HasMarker(const fs::path& dir) { return Exists(dir / kMarkerName); }

// A folder of ours is deleted with its marker last, so an interrupted deletion still leaves a folder that is recognised as ours.
bool RemoveOurs(const fs::path& dir) {
  std::error_code ec;
  std::vector<fs::path> entries;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(it->path());
  for (const fs::path& e : entries)
    if (e.filename() != kMarkerName) fs::remove_all(e, ec);
  fs::remove(dir / kMarkerName, ec);
  fs::remove(dir, ec);
  return !Exists(dir);
}

// The folder this import itself created (.new): everything in it goes.
bool RemoveAll(const fs::path& dir) {
  std::error_code ec;
  fs::remove_all(dir, ec);
  return !Exists(dir);
}

bool IsBlocked(DWORD e) { return e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED; }

bool Rename(const fs::path& from, const fs::path& to, DWORD* err) {
  if (MoveFileExW(from.c_str(), to.c_str(), 0)) return true;
  *err = GetLastError();
  return false;
}

// ---- the manifest and its verification ---------------------------------------------------------------------------

dsuits::Manifest ReferenceManifest(std::span<const SuitRecipe> suits) {
  dsuits::Manifest m;
  m.converter = std::string(kConverterWord);
  const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
  for (size_t i = 0; i < picks.size() && i < suits.size(); ++i) {
    dsuits::Suit s = picks[i];
    s.tpr.size = suits[i].out_tpr_size;
    s.tpr.sha1 = suits[i].out_tpr_sha1;
    s.cat.size = suits[i].out_cat_size;
    s.cat.sha1 = suits[i].out_cat_sha1;
    m.suits.push_back(std::move(s));
  }
  return m;
}

bool ReadWholeFile(const fs::path& p, uint64_t limit, std::string* out) {
  HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  out->clear();
  char buf[65536];
  DWORD n = 0;
  bool ok = true;
  for (;;) {
    if (!ReadFile(h, buf, sizeof buf, &n, nullptr)) {
      ok = false;
      break;
    }
    if (n == 0) break;
    out->append(buf, n);
    if (out->size() > limit) {
      ok = false;
      break;
    }
  }
  CloseHandle(h);
  return ok;
}

// A file's size and SHA-1 as it is on disk.
bool HashFile(const fs::path& p, uint64_t* size, std::string* sha1) {
  HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  gamefiles::Sha1 hash;
  std::vector<char> buf(size_t(1) << 20);
  uint64_t total = 0;
  bool ok = true;
  for (;;) {
    DWORD n = 0;
    if (!ReadFile(h, buf.data(), DWORD(buf.size()), &n, nullptr)) {
      ok = false;
      break;
    }
    if (n == 0) break;
    hash.Update(buf.data(), n);
    total += n;
  }
  CloseHandle(h);
  if (!ok) return false;
  *size = total;
  *sha1 = hash.HexDigest();
  return true;
}

std::string VerifyImpl(const fs::path& folder, const CancelToken* cancel) {
  std::string text;
  if (!ReadWholeFile(folder / L"manifest.txt", kManifestLimit, &text))
    return "no manifest.txt in " + Utf8(folder);
  const dsuits::ParseResult parsed = dsuits::ParseManifest(text);
  if (!parsed.manifest) return "manifest.txt: " + parsed.error;
  if (parsed.manifest->suits.empty()) return "manifest.txt lists no suits";
  for (const dsuits::Suit& s : parsed.manifest->suits) {
    if (!dsuits::IsPick(s)) return s.Label() + ": not one of the picks";
    for (const dsuits::SuitFile* f : {&s.tpr, &s.cat}) {
      if (cancel && cancel->cancelled()) return "cancelled";
      const fs::path p = folder / fs::path(f->name);
      uint64_t size = 0;
      std::string sha;
      if (!Exists(p)) return f->name + ": missing";
      if (!HashFile(p, &size, &sha)) return f->name + ": cannot be read";
      if (size != f->size)
        return f->name + ": size " + std::to_string(size) + ", the manifest says " + std::to_string(f->size);
      if (sha != f->sha1) return f->name + ": SHA-1 differs from the manifest";
    }
  }
  return {};
}

// ---- the swap ----------------------------------------------------------------------------------------------------

enum class SwapStatus { kDone, kBlocked, kFailed };

// The old rule: an old import that is ours is deleted; one that is not (a kept Python import) stays.
void ApplyOldRule(const fs::path& old, const Logger& log) {
  if (!Exists(old)) return;
  if (HasMarker(old)) {
    if (RemoveOurs(old)) log("old import removed: " + Utf8(old));
    else log("old import could not be removed (files open?): " + Utf8(old));
  } else {
    log("old import kept (not ours): " + Utf8(old));
  }
}

// .new (verified) becomes the current import.
SwapStatus SwapNewIn(const fs::path& ud, const Logger& log, std::string* why) {
  const fs::path cur = Cur(ud), nw = New(ud), old = Old(ud);
  const auto fail = [&](const std::string& what, DWORD e) {
    *why = what + ": " + WinError(e);
    return IsBlocked(e) ? SwapStatus::kBlocked : SwapStatus::kFailed;
  };
  DWORD err = 0;
  const bool had_cur = Exists(cur);
  if (had_cur) {
    if (Exists(old)) {
      if (HasMarker(old)) {
        if (!RemoveOurs(old)) {
          *why = "cannot clear " + Utf8(old) + " (files open?)";
          return SwapStatus::kBlocked;
        }
      } else {
        // Not ours (a kept Python import): never deleted, moved to the first free .old-<N>.
        fs::path kept;
        for (int n = 2;; ++n) {
          kept = ud / (L"doax2_suits.old-" + std::to_wstring(n));
          if (!Exists(kept)) break;
        }
        if (!Rename(old, kept, &err)) return fail("cannot rename " + Utf8(old) + " to " + Utf8(kept), err);
        log("old import kept (not ours): " + Utf8(old) + " moved to " + Utf8(kept));
      }
    }
    if (!Rename(cur, old, &err)) return fail("cannot rename " + Utf8(cur) + " to " + Utf8(old), err);
  }
  if (!Rename(nw, cur, &err)) {
    const DWORD first = err;
    std::string what = "cannot rename " + Utf8(nw) + " to " + Utf8(cur);
    if (had_cur && !Rename(old, cur, &err)) {
      *why = what + ": " + WinError(first) + "; and the first rename could not be undone: " + WinError(err);
      return SwapStatus::kFailed;
    }
    return fail(what, first);
  }
  ApplyOldRule(old, log);
  return SwapStatus::kDone;
}

enum class RecoverKind { kNothing, kRecovered, kPending, kNotOurs, kFailed };
struct Recovery {
  RecoverKind kind = RecoverKind::kNothing;
  std::string why;
};

// The swap states of the header. delete_blocked: a verified .new whose swap is blocked is deleted (Start) instead of left
// pending (CompletePendingSwap).
Recovery RecoverSwapState(const fs::path& ud, const Logger& log, bool delete_blocked) {
  Recovery r;
  const fs::path cur = Cur(ud), nw = New(ud), old = Old(ud);
  if (Exists(nw)) {
    if (!HasMarker(nw)) {
      r.kind = RecoverKind::kNotOurs;
      r.why = Utf8(nw) + " is not an import of ours (it has no " + "\".doa4-suitconv\" marker); it is left alone";
      return r;
    }
    std::string damaged;
    bool interrupted = !Exists(nw / L"manifest.txt");
    if (!interrupted) damaged = VerifyImpl(nw, nullptr);
    if (interrupted || !damaged.empty()) {
      log(interrupted ? "swap recovery: deleted an interrupted import (" + Utf8(nw) + ")"
                      : "swap recovery: deleted a damaged import (" + Utf8(nw) + "): " + damaged);
      if (!RemoveOurs(nw)) {
        r.kind = RecoverKind::kFailed;
        r.why = "cannot delete " + Utf8(nw);
        return r;
      }
      r.kind = RecoverKind::kRecovered;
    } else {
      std::string why;
      const SwapStatus st = SwapNewIn(ud, log, &why);
      if (st == SwapStatus::kDone) {
        log("swap recovery: completed a pending swap");
        r.kind = RecoverKind::kRecovered;
        return r;
      }
      if (st == SwapStatus::kFailed) {
        r.kind = RecoverKind::kFailed;
        r.why = why;
        return r;
      }
      if (!delete_blocked) {
        r.kind = RecoverKind::kPending;
        r.why = "swap blocked by open files: " + why;
        return r;
      }
      log("swap recovery: swap blocked by open files; deleted the leftover import (" + Utf8(nw) + ")");
      if (!RemoveOurs(nw)) {
        r.kind = RecoverKind::kFailed;
        r.why = "cannot delete " + Utf8(nw);
        return r;
      }
      r.kind = RecoverKind::kRecovered;
    }
  }
  if (Exists(cur) && Exists(old)) {
    ApplyOldRule(old, log);
    if (r.kind == RecoverKind::kNothing) r.kind = RecoverKind::kRecovered;
  }
  return r;
}

// ---- files -------------------------------------------------------------------------------------------------------

uint64_t DefaultFreeBytes(const fs::path& path) {
  fs::path p = path;
  std::error_code ec;
  while (!p.empty() && !fs::exists(p, ec)) {
    const fs::path parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
  ULARGE_INTEGER avail{};
  if (p.empty() || !GetDiskFreeSpaceExW(p.c_str(), &avail, nullptr, nullptr)) return 0;
  return avail.QuadPart;
}

// A probe file created and deleted in the user data folder (the folder is created when it does not exist).
bool WriteProbe(const fs::path& ud, std::string* why) {
  std::error_code ec;
  fs::create_directories(ud, ec);
  if (!fs::is_directory(ud, ec)) {
    *why = "cannot use " + Utf8(ud) + " as the user data folder";
    return false;
  }
  const fs::path probe = ud / (L".doa4-write-probe-" + std::to_wstring(GetCurrentProcessId()));
  HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    *why = "cannot create a file in " + Utf8(ud) + ": " + WinError(GetLastError());
    return false;
  }
  DWORD put = 0;
  const bool ok = WriteFile(h, "x", 1, &put, nullptr) && put == 1;
  CloseHandle(h);
  const bool gone = DeleteFileW(probe.c_str()) != 0;
  if (!ok || !gone) {
    *why = "cannot write and delete a file in " + Utf8(ud);
    return false;
  }
  return true;
}

struct SimulatedCrash {};

struct IoThread {
  HANDLE handle = nullptr;
  std::atomic<bool> in_read{false};
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The job
// ---------------------------------------------------------------------------------------------------------------

struct ImportJob::Impl {
  ImportRequest req;
  Hooks hooks;
  CancelToken cancel;

  mutable std::mutex mu;  // prog and res
  Progress prog;
  ImportResult res;
  std::atomic<bool> done{false};
  std::atomic<bool> finished{false};  // the job thread has ended (the watchdog stops)

  std::mutex log_mu;
  std::mutex io_mu;  // the registered threads
  std::vector<IoThread*> io;
  std::mutex wd_mu;
  std::condition_variable wd_cv;
  std::thread job_thread, watchdog;

  std::atomic<int> write_count{0};
  std::atomic<bool> abort_all{false}, crashed{false};
  int fail_kind = 0;  // under mu: 0 none, 1 an input could not be read or changed, 2 a write failed
  std::string fail_detail;

  // A thread that reads registers itself (a duplicate of its own handle) so Cancel can reach its blocked read.
  class Registration {
   public:
    explicit Registration(Impl* impl) : impl_(impl) {
      DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &t_.handle, 0, FALSE,
                      DUPLICATE_SAME_ACCESS);
      std::lock_guard<std::mutex> lk(impl_->io_mu);
      impl_->io.push_back(&t_);
    }
    ~Registration() {
      {
        std::lock_guard<std::mutex> lk(impl_->io_mu);
        impl_->io.erase(std::find(impl_->io.begin(), impl_->io.end(), &t_));
      }
      if (t_.handle) CloseHandle(t_.handle);
    }
    IoThread& thread() { return t_; }

   private:
    Impl* impl_;
    IoThread t_;
  };

  struct ReadScope {
    explicit ReadScope(IoThread& t) : t_(t) { t_.in_read = true; }
    ~ReadScope() { t_.in_read = false; }
    IoThread& t_;
  };

  void Log(const std::string& line) {
    std::lock_guard<std::mutex> lk(log_mu);
    if (req.log) req.log(line);
  }
  Logger Logging() {
    return [this](const std::string& s) { Log(s); };
  }

  void Raise(double f) {
    std::lock_guard<std::mutex> lk(mu);
    prog.fraction = std::max(prog.fraction, std::min(f, 1.0));
  }
  void SetPhase(Phase p) {
    {
      std::lock_guard<std::mutex> lk(mu);
      prog.phase = p;
    }
    if (hooks.on_phase) hooks.on_phase(p);
  }

  // CancelSynchronousIo on every registered thread that is inside a read, again and again until the job ends: a call made
  // before the thread has entered ReadFile cancels nothing.
  void Watchdog() {
    std::unique_lock<std::mutex> lk(wd_mu);
    while (!finished.load()) {
      wd_cv.wait_for(lk, kPoke);
      if (finished.load()) break;
      if (!cancel.cancelled()) continue;
      std::lock_guard<std::mutex> io_lk(io_mu);
      for (IoThread* t : io)
        if (t->in_read.load()) CancelSynchronousIo(t->handle);
    }
  }

  // One checked file write: "<dest>.tmp", flushed, renamed over dest.
  bool WriteChecked(const fs::path& dest, const uint8_t* data, size_t n, std::string* why) {
    const int idx = ++write_count;
    if (hooks.write_fault_at == idx) {
      *why = "cannot write " + Utf8(dest) + " (injected write fault #" + std::to_string(idx) + ")";
      return false;
    }
    const bool crash = hooks.crash_at_write == idx;
    fs::path tmp = dest;
    tmp += L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      *why = "cannot create " + Utf8(tmp) + ": " + WinError(GetLastError());
      return false;
    }
    const size_t want = crash ? n / 2 : n;
    bool ok = true;
    for (size_t at = 0; ok && at < want;) {
      const DWORD chunk = DWORD(std::min<size_t>(want - at, size_t(1) << 20));
      DWORD put = 0;
      ok = WriteFile(h, data + at, chunk, &put, nullptr) && put == chunk;
      at += chunk;
    }
    if (crash) {
      CloseHandle(h);
      throw SimulatedCrash{};  // the disk stays as a crash would leave it
    }
    ok = ok && FlushFileBuffers(h);
    const DWORD werr = GetLastError();
    ok = CloseHandle(h) && ok;
    if (!ok) {
      DeleteFileW(tmp.c_str());
      *why = "cannot write " + Utf8(dest) + ": " + WinError(werr);
      return false;
    }
    if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      *why = "cannot rename " + Utf8(tmp) + ": " + WinError(GetLastError());
      DeleteFileW(tmp.c_str());
      return false;
    }
    return true;
  }

  void Fail(int kind, const std::string& detail) {
    {
      std::lock_guard<std::mutex> lk(mu);
      if (fail_kind == 0) {
        fail_kind = kind;
        fail_detail = detail;
      }
    }
    abort_all = true;
  }

  struct Slot {
    bool converted = false, skipped = false, differs = false;
    SkippedSuit skip;
    uint64_t tpr_size = 0, cat_size = 0;
    std::string tpr_sha, cat_sha;
  };
  std::atomic<int> suits_finished{0};

  void Finished(int n_suits) {
    const int d = ++suits_finished;
    {
      std::lock_guard<std::mutex> lk(mu);
      prog.suits_done = std::max(prog.suits_done, d);
    }
    Raise(kCheckWeight + kConvertWeight * double(d) / double(std::max(n_suits, 1)));
  }

  void ProcessSuit(IoThread& me, InputSet& set, size_t i, const dsuits::Suit& pick, Slot& slot, int n_suits) {
    const SuitRecipe& suit = hooks.suits[i];
    const auto skip = [&](const std::string& stage, const std::string& why) {
      slot.skipped = true;
      slot.skip = SkippedSuit{pick.Label(), stage, why};
      Finished(n_suits);
    };
    const GirlRecipe* girl = nullptr;
    for (const GirlRecipe& g : hooks.girls)
      if (std::string(g.girl) == suit.girl) girl = &g;
    if (!girl) return skip("recipe", std::string("the recipe tables have no girl ") + suit.girl);

    SuitInputs in;
    try {
      ReadScope scope(me);
      in = hooks.read_inputs ? hooks.read_inputs(set, suit, *girl) : ReadSuitInputs(set, suit, *girl);
    } catch (const InputChanged& e) {
      return Fail(1, e.what());
    } catch (const std::bad_alloc&) {
      return skip("inputs", "out of memory");
    } catch (const std::exception& e) {
      return skip("inputs", e.what());
    }
    if (cancel.cancelled() || abort_all.load()) return;

    ConvertFailure failure;
    std::optional<SuitOutput> out;
    try {
      out = hooks.convert ? hooks.convert(suit, *girl, in, cancel, &failure)
                          : ConvertSuit(suit, *girl, in, Level::kM1, &cancel, nullptr, &failure);
    } catch (const InputChanged& e) {
      return Fail(1, e.what());
    } catch (const std::bad_alloc&) {
      return skip("convert", "out of memory");
    } catch (const std::exception& e) {
      return skip("convert", e.what());
    } catch (...) {
      return skip("convert", "unknown exception");
    }
    if (cancel.cancelled() || abort_all.load()) return;
    if (!out) return skip(failure.stage.empty() ? "convert" : failure.stage, failure.what);

    // R7: a file over its budget fails the suit.
    if (out->tpr.size() > girl->budget_tpr)
      return skip("budget", pick.tpr.name + " is " + std::to_string(out->tpr.size()) + " bytes, over the limit of " +
                                std::to_string(girl->budget_tpr));
    if (out->cat.size() > girl->budget_cat)
      return skip("budget", pick.cat.name + " is " + std::to_string(out->cat.size()) + " bytes, over the limit of " +
                                std::to_string(girl->budget_cat));

    slot.tpr_size = out->tpr.size();
    slot.cat_size = out->cat.size();
    slot.tpr_sha = gamefiles::Sha1Hex(out->tpr.data(), out->tpr.size());
    slot.cat_sha = gamefiles::Sha1Hex(out->cat.data(), out->cat.size());
    const bool tpr_differs = slot.tpr_sha != suit.out_tpr_sha1;
    const bool cat_differs = slot.cat_sha != suit.out_cat_sha1;
    if (tpr_differs || cat_differs) {
      slot.differs = true;
      Log("suit " + pick.Label() + ": output differs from the reference build (" +
          (tpr_differs ? slot.tpr_sha : slot.cat_sha) + ")");
    }

    const fs::path nw = New(req.user_data);
    std::string why;
    if (!WriteChecked(nw / fs::path(pick.tpr.name), out->tpr.data(), out->tpr.size(), &why) ||
        !WriteChecked(nw / fs::path(pick.cat.name), out->cat.data(), out->cat.size(), &why)) {
      return Fail(2, why);
    }
    slot.converted = true;
    Finished(n_suits);
  }

  // The outcome of a failed check as the job reports it.
  bool CheckPassed(const InputCheck& c, bool disc, ImportResult& r) {
    if (c.status == InputStatus::kOk) return true;
    r.mismatched_inputs = c.mismatched;
    r.detail = c.detail;
    if (cancel.cancelled() || c.status == InputStatus::kCancelled) r.outcome = Outcome::kCancelled;
    else if (c.status == InputStatus::kReadError) r.outcome = Outcome::kReadError;
    else r.outcome = disc ? Outcome::kWrongDisc : Outcome::kGameFilesDiffer;
    return false;
  }

  ImportResult Execute(IoThread& me) {
    ImportResult r;
    const auto fin = [&](Outcome o, std::string detail = {}) {
      r.outcome = o;
      if (!detail.empty()) r.detail = std::move(detail);
      return r;
    };
    const fs::path ud = req.user_data;
    const fs::path nw = New(ud);
    const std::vector<dsuits::Suit> picks = dsuits::PickSuits();
    const int n_suits = int(picks.size());

    if (hooks.suits.size() != picks.size()) return fin(Outcome::kFailed, "the recipe tables do not match the picks");
    for (size_t i = 0; i < picks.size(); ++i)
      if (picks[i].design != hooks.suits[i].design || picks[i].colour != hooks.suits[i].colour)
        return fin(Outcome::kFailed, "the recipe tables do not match the picks");
    uint64_t check_total = 0;
    for (const EntryRef& e : hooks.disc) check_total += e.size;
    for (const EntryRef& e : hooks.game) check_total += e.size;
    {
      std::lock_guard<std::mutex> lk(mu);
      prog.suits_total = n_suits;
      prog.bytes_total = check_total;
    }

    // 1. Space and write access.
    SetPhase(Phase::kCheckingSpace);
    if (cancel.cancelled()) return fin(Outcome::kCancelled);
    r.bytes_needed = BytesNeeded(hooks.suits);
    r.bytes_free = hooks.free_bytes ? hooks.free_bytes(ud) : DefaultFreeBytes(ud);
    if (r.bytes_free < r.bytes_needed)
      return fin(Outcome::kNoSpace, "an import needs " + std::to_string(r.bytes_needed) + " bytes free in " + Utf8(ud) +
                                        ", " + std::to_string(r.bytes_free) + " are");
    {
      std::string why;
      if (!WriteProbe(ud, &why)) return fin(Outcome::kNoWriteAccess, why);
    }

    // 2. The inputs are opened (no entry bytes are read yet), then the disc is checked, then the game files.
    std::string error;
    std::unique_ptr<InputSet> set;
    {
      Impl::ReadScope scope(me);
      set = hooks.open_inputs ? hooks.open_inputs(req, hooks.disc, hooks.game, &error)
                              : InputSet::Open(req.doax2, req.game_root, hooks.disc, hooks.game, &error);
    }
    if (!set) return fin(cancel.cancelled() ? Outcome::kCancelled : Outcome::kFailed, "the recipe tables do not fit: " + error);
    SetPhase(Phase::kCheckingDisc);
    if (cancel.cancelled()) return fin(Outcome::kCancelled);
    const auto progress_of = [this, check_total](uint64_t base) {
      return [this, check_total, base](uint64_t b) {
        {
          std::lock_guard<std::mutex> lk(mu);
          prog.bytes_done = base + b;
        }
        if (check_total > 0) Raise(kCheckWeight * double(base + b) / double(check_total));
      };
    };
    InputCheck dc;
    {
      ReadScope scope(me);
      dc = set->CheckDisc(cancel, progress_of(0));
    }
    if (!CheckPassed(dc, true, r)) return r;
    if (cancel.cancelled()) return fin(Outcome::kCancelled);
    SetPhase(Phase::kCheckingGame);
    if (cancel.cancelled()) return fin(Outcome::kCancelled);
    InputCheck gc;
    {
      ReadScope scope(me);
      gc = set->CheckGame(cancel, progress_of(dc.bytes_checked));
    }
    if (!CheckPassed(gc, false, r)) return r;
    Raise(kCheckWeight);
    if (cancel.cancelled()) return fin(Outcome::kCancelled);

    // 3. Only now: what an earlier import left behind, then a fresh .new.
    {
      const Recovery rc = RecoverSwapState(ud, Logging(), true);
      if (rc.kind == RecoverKind::kNotOurs || rc.kind == RecoverKind::kFailed || rc.kind == RecoverKind::kPending)
        return fin(Outcome::kFailed, rc.why);
    }
    if (Exists(nw) && !RemoveOurs(nw)) return fin(Outcome::kFailed, "cannot delete the leftover " + Utf8(nw));
    std::error_code ec;
    fs::create_directories(nw, ec);
    std::string why;
    if (!fs::is_directory(nw, ec)) return fin(Outcome::kFailed, "cannot create " + Utf8(nw));
    if (!WriteChecked(nw / kMarkerName, reinterpret_cast<const uint8_t*>(kMarkerText), sizeof(kMarkerText) - 1, &why)) {
      RemoveAll(nw);
      return fin(Outcome::kFailed, why);
    }

    // 4. Conversion and the per-suit writes.
    SetPhase(Phase::kConverting);
    std::vector<Slot> slots(picks.size());
    std::atomic<size_t> next{0};
    int workers = req.workers;
    if (workers <= 0) workers = int(std::clamp<int>(int(std::thread::hardware_concurrency()) - 1, 1, 4));
    workers = std::min(workers, n_suits);
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w) {
      pool.emplace_back([this, &set, &next, &slots, &picks, n_suits] {
        Registration reg(this);
        try {
          while (!abort_all.load() && !cancel.cancelled()) {
            const size_t i = next++;
            if (i >= picks.size()) break;
            ProcessSuit(reg.thread(), *set, i, picks[i], slots[i], n_suits);
          }
        } catch (const SimulatedCrash&) {
          crashed = true;
          abort_all = true;
        } catch (const std::exception& e) {
          Fail(2, std::string("worker failed: ") + e.what());
        } catch (...) {
          Fail(2, "worker failed");
        }
      });
    }
    for (std::thread& t : pool) t.join();

    if (crashed.load()) return fin(Outcome::kFailed, "simulated crash");  // nothing is cleaned up, as after a crash
    int kind = 0;
    std::string detail;
    {
      std::lock_guard<std::mutex> lk(mu);
      kind = fail_kind;
      detail = fail_detail;
    }
    const auto abandon = [&](Outcome o, std::string d) {
      RemoveAll(nw);
      return fin(cancel.cancelled() ? Outcome::kCancelled : o, std::move(d));
    };
    if (kind == 1) return abandon(Outcome::kReadError, detail);
    if (kind == 2) return abandon(Outcome::kFailed, detail);
    if (cancel.cancelled()) return abandon(Outcome::kCancelled, "cancelled");

    int converted = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
      if (slots[i].skipped) r.skipped.push_back(slots[i].skip);
      if (slots[i].converted) ++converted;
      if (slots[i].differs) r.differs_from_reference.push_back(picks[i].Label());
    }
    if (converted == 0) {
      RemoveAll(nw);
      return fin(Outcome::kNothingImported, "no suit could be converted");
    }

    // 5. The manifest (last), re-parsed before it is renamed in, and the verification of .new.
    SetPhase(Phase::kWriting);
    Raise(kCheckWeight + kConvertWeight);
    if (cancel.cancelled()) return abandon(Outcome::kCancelled, "cancelled");
    dsuits::Manifest m;
    m.converter = std::string(kConverterWord);
    for (size_t i = 0; i < slots.size(); ++i) {
      if (!slots[i].converted) continue;
      dsuits::Suit s = picks[i];
      s.tpr.size = slots[i].tpr_size;
      s.tpr.sha1 = slots[i].tpr_sha;
      s.cat.size = slots[i].cat_size;
      s.cat.sha1 = slots[i].cat_sha;
      m.suits.push_back(std::move(s));
    }
    const std::string text = dsuits::FormatManifest(m);
    const dsuits::ParseResult back = dsuits::ParseManifest(text);
    if (!back.manifest || !(*back.manifest == m)) return abandon(Outcome::kFailed, "the manifest does not read back: " + back.error);
    try {
      if (!WriteChecked(nw / L"manifest.txt", reinterpret_cast<const uint8_t*>(text.data()), text.size(), &why))
        return abandon(Outcome::kFailed, why);
    } catch (const SimulatedCrash&) {
      return fin(Outcome::kFailed, "simulated crash");
    }
    Raise(kCheckWeight + kConvertWeight + 0.02);
    const std::string verdict = VerifyImpl(nw, &cancel);
    if (!verdict.empty()) return abandon(Outcome::kFailed, "the new import failed its verification: " + verdict);
    Raise(kCheckWeight + kConvertWeight + 0.03);

    // 6. The swap.
    SetPhase(Phase::kSwapping);
    if (cancel.cancelled()) return abandon(Outcome::kCancelled, "cancelled");
    const SwapStatus st = SwapNewIn(ud, Logging(), &why);
    if (st == SwapStatus::kBlocked) return fin(Outcome::kSwapPending, why);
    if (st == SwapStatus::kFailed) return fin(Outcome::kFailed, why);
    return fin(r.skipped.empty() ? Outcome::kImported : Outcome::kImportedWithSkips);
  }

  void Run() {
    Registration reg(this);
    ImportResult r;
    try {
      r = Execute(reg.thread());
    } catch (const SimulatedCrash&) {
      r.outcome = Outcome::kFailed;
      r.detail = "simulated crash";
    } catch (const std::exception& e) {
      RemoveAll(New(req.user_data));
      r.outcome = Outcome::kFailed;
      r.detail = std::string("the import failed: ") + e.what();
    }
    {
      std::lock_guard<std::mutex> lk(mu);
      res = std::move(r);
      prog.phase = Phase::kDone;
      prog.fraction = 1.0;
    }
    done.store(true);
    finished.store(true);
    wd_cv.notify_all();
  }
};

ImportJob::ImportJob(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ImportJob::~ImportJob() {
  if (!impl_) return;
  Cancel();
  impl_->job_thread.join();
  impl_->finished.store(true);
  impl_->wd_cv.notify_all();
  impl_->watchdog.join();
}

std::unique_ptr<ImportJob> ImportJob::Start(ImportRequest request) { return StartForTest(std::move(request), Hooks{}); }

std::unique_ptr<ImportJob> ImportJob::StartForTest(ImportRequest request, Hooks hooks) {
  auto impl = std::make_unique<Impl>();
  impl->req = std::move(request);
  impl->hooks = std::move(hooks);
  Impl* raw = impl.get();
  std::unique_ptr<ImportJob> job(new ImportJob(std::move(impl)));
  raw->watchdog = std::thread([raw] { raw->Watchdog(); });
  raw->job_thread = std::thread([raw] { raw->Run(); });
  return job;
}

Progress ImportJob::progress() const {
  std::lock_guard<std::mutex> lk(impl_->mu);
  return impl_->prog;
}
bool ImportJob::done() const { return impl_->done.load(); }
void ImportJob::Cancel() {
  impl_->cancel.Cancel();
  impl_->wd_cv.notify_all();
}
ImportResult ImportJob::result() const {
  std::lock_guard<std::mutex> lk(impl_->mu);
  return impl_->res;
}

uint64_t BytesNeeded(std::span<const SuitRecipe> suits) {
  uint64_t total = kReserve;
  for (const SuitRecipe& s : suits) total += s.out_tpr_size + s.out_cat_size;
  total += dsuits::FormatManifest(ReferenceManifest(suits)).size();
  return total;
}
uint64_t BytesNeeded() { return BytesNeeded(Suits()); }

std::string CompletePendingSwap(const fs::path& user_data, const std::function<void(std::string_view)>& log) {
  const Logger logger = [&log](const std::string& s) {
    if (log) log(s);
  };
  const Recovery r = RecoverSwapState(user_data, logger, false);
  switch (r.kind) {
    case RecoverKind::kNothing:
    case RecoverKind::kRecovered: return {};
    default: return r.why;
  }
}

std::string VerifyImportFolder(const fs::path& folder) { return VerifyImpl(folder, nullptr); }

}  // namespace doa4::suitconv
