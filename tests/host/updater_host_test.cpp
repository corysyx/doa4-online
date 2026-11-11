// Host test for src/doa4_updater (netplay plan 1b, Task 4): the SHA-256 of a file, the SHA256SUMS.txt parser, the
// verify-then-swap with its rollback at every one of the six rename steps (simulated with fail_at, and two real
// failures: a leftover .old and a staged file another handle holds without delete sharing), the .dev backup of a
// self-built game, DeleteOldFiles and CleanUpAtStart, the host rule, the release decision (Choose) over fixed release
// JSON, and (fix round 1) the whole check through Env with a fake HTTP source and a temp game folder: Start/Busy with an
// injected check, the self-built confirm, may_swap, the caps, the swap latch (RestartPending) and Shutdown.
// Built and run by run_updater_test.bat. No network: GameEnv (WinHTTP) is never called; every Start gets an injected
// check. Files go under %TEMP%\doa4_updater_<pid>\ and only that folder is removed at the end.
// SHA-256 oracles (never this code): "abc" and the 448-bit message are the FIPS 180-2 examples; the 200,003-byte
// pattern file (byte i = (i*7+3) & 0xFF) was hashed by Windows' certutil -hashfile and by Python's hashlib (both
// aaa1137a...329d); the empty file by hashlib (certutil refuses an empty file).

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "../../src/doa4_updater.h"

namespace fs = std::filesystem;
namespace up = doa4::updater;

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)
#define CHECK_EQ(a, b)                                                                                    \
  do {                                                                                                    \
    ++g_checks;                                                                                           \
    const std::string va_ = (a), vb_ = (b);                                                               \
    if (va_ != vb_) {                                                                                     \
      std::printf("FAIL %s:%d: %s\n   got:  [%s]\n   want: [%s]\n", __FILE__, __LINE__, #a, va_.c_str(), \
                  vb_.c_str());                                                                           \
      ++g_failures;                                                                                       \
    }                                                                                                     \
  } while (0)

static bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
// Task 15: Result::found by name, so a failed check prints what it got
static const char* FoundName(up::Found f) {
  return f == up::Found::kNewer ? "newer" : f == up::Found::kUpToDate ? "up to date" : "failed";
}
#define CHECK_FOUND(r, want) CHECK_EQ(FoundName((r).found), FoundName(want))

// ---- files ----
static fs::path g_root;
static int g_dir_n = 0;
static fs::path NewDir(const std::string& tag) {
  const fs::path d = g_root / (tag + "_" + std::to_string(++g_dir_n));
  std::error_code ec;
  fs::create_directories(d, ec);
  return d;
}
static void Write(const fs::path& p, const std::string& bytes) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(bytes.data(), std::streamsize(bytes.size()));
}
static bool Read(const fs::path& p, std::string* out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}
// Every file and folder under `dir` (relative generic path -> bytes, "<dir>" for a folder, "<unreadable>" when a file
// cannot be read). Never throws.
static std::map<std::string, std::string> Snapshot(const fs::path& dir) {
  std::map<std::string, std::string> m;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    const std::string rel = fs::relative(it->path(), dir, e2).generic_string();
    if (it->is_directory(e2)) {
      m[rel] = "<dir>";
    } else {
      std::string bytes;
      m[rel] = Read(it->path(), &bytes) ? bytes : std::string("<unreadable>");
    }
  }
  if (ec) m["<iteration error>"] = ec.message();
  return m;
}
static std::string Describe(const std::map<std::string, std::string>& m) {
  std::string s;
  for (const auto& kv : m) s += kv.first + "(" + std::to_string(kv.second.size()) + ") ";
  return s;
}
static bool Exists(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p, ec);
}

// ---- the fixture ----
static std::string Pattern() {
  std::string s(200003, '\0');
  for (size_t i = 0; i < s.size(); ++i) s[i] = char((i * 7 + 3) & 0xFF);
  return s;
}
static const char* const kM448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const char* const kShaAbc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
static const char* const kShaPattern = "aaa1137a572394ab285096ae86aa5e48fac554adfe277fe868729e38f469329d";
static const char* const kShaM448 = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
static const char* const kShaEmpty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// The installed (old) and the staged (new) contents of kFiles[0..2]; the new ones have oracle hashes.
static const std::string kOld[3] = {"old exe bytes", "old runtime bytes", "old tracy bytes"};
static std::string NewContent(int i) { return i == 0 ? Pattern() : i == 1 ? std::string("abc") : std::string(kM448); }
static const char* const kNewSha[3] = {kShaPattern, kShaAbc, kShaM448};

struct Case {
  fs::path dir, staging;
};
// dir: the three installed files (+ a toml the swap must not touch); staging: the three new files.
static Case MakeCase(const std::string& tag, bool with_tracy = true) {
  Case c;
  c.dir = NewDir(tag);
  c.staging = c.dir / "update_new";
  std::error_code ec;
  fs::create_directories(c.staging, ec);
  for (int i = 0; i < 3; ++i) {
    if (i == 2 && !with_tracy) continue;
    Write(c.dir / up::kFiles[i], kOld[i]);
  }
  Write(c.dir / "dead_or_alive_4.toml", "[netplay]\nnetplay_delay = 2\n");
  for (int i = 0; i < 3; ++i) Write(c.staging / up::kFiles[i], NewContent(i));
  return c;
}
// sha256sum text mode lines for the three new files; `bad` (0..2) gets a wrong hash, `skip` (0..2) no line.
static std::string SumsText(int bad = -1, int skip = -1) {
  std::string t;
  for (int i = 0; i < 3; ++i) {
    if (i == skip) continue;
    std::string h = kNewSha[i];
    if (i == bad) h.back() = h.back() == '0' ? '1' : '0';
    t += h + "  " + up::kFiles[i] + "\n";
  }
  return t;
}

// =====================================================================================================

static void TestSha256() {
  const fs::path d = NewDir("sha");
  Write(d / "abc.bin", "abc");
  Write(d / "pattern.bin", Pattern());
  Write(d / "m448.bin", kM448);
  Write(d / "empty.bin", "");
  std::string e;
  CHECK_EQ(up::Sha256File(d / "abc.bin", &e), kShaAbc);
  CHECK_EQ(up::Sha256File(d / "pattern.bin", &e), kShaPattern);  // more than one read buffer
  CHECK_EQ(up::Sha256File(d / "m448.bin", &e), kShaM448);
  CHECK_EQ(up::Sha256File(d / "empty.bin", &e), kShaEmpty);
  e.clear();
  CHECK_EQ(up::Sha256File(d / "missing.bin", &e), "");
  CHECK(!e.empty());
}

static void TestParseSums() {
  // text mode, binary mode ('*'), CRLF, upper-case hex, a BOM, a blank line, junk lines, a name with a space
  const std::string t = std::string("\xEF\xBB\xBF") + kShaAbc + "  rexruntime.dll\r\n" +
                        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD *TracyClient.dll\r\n" + "\r\n" +
                        "not a line\n" + "abc  short.dll\n" + kShaPattern + "  dead or alive 4.exe\n";
  const up::Sums s = up::ParseSums(t);
  CHECK(s.hex.count("rexruntime.dll") == 1);
  if (s.hex.count("rexruntime.dll")) CHECK_EQ(s.hex.find("rexruntime.dll")->second, kShaAbc);
  CHECK(s.hex.count("TracyClient.dll") == 1);
  if (s.hex.count("TracyClient.dll")) CHECK_EQ(s.hex.find("TracyClient.dll")->second, kShaAbc);  // lower-cased
  CHECK(s.hex.count("dead or alive 4.exe") == 1);
  CHECK(s.hex.count("short.dll") == 0);
  CHECK(s.hex.size() == 3);
  CHECK(s.count.count("rexruntime.dll") && s.count.find("rexruntime.dll")->second == 1);
  // a name given twice is counted twice
  const up::Sums d = up::ParseSums(std::string(kShaAbc) + "  a.dll\n" + kShaM448 + "  a.dll\n");
  CHECK(d.count.count("a.dll") && d.count.find("a.dll")->second == 2);
  // the release's own format (release_stage.py: "<sha256 lower hex>  <name>"), no trailing newline
  const up::Sums r = up::ParseSums(std::string(kShaM448) + "  TracyClient.dll");
  CHECK(r.hex.count("TracyClient.dll") && r.hex.find("TracyClient.dll")->second == kShaM448);
}

static void TestSwapOk() {
  const Case c = MakeCase("ok");
  const std::string why = up::VerifyAndSwap(c.dir, c.staging, SumsText());
  CHECK_EQ(why, "");
  for (int i = 0; i < 3; ++i) {
    std::string now, old;
    CHECK(Read(c.dir / up::kFiles[i], &now) && now == NewContent(i));
    CHECK(Read(c.dir / (std::string(up::kFiles[i]) + ".old"), &old) && old == kOld[i]);
  }
  // staging emptied: the three files left it, nothing else was in it
  CHECK(Snapshot(c.staging).empty());
  // dir: exactly the three, the three .old, the untouched toml and the (empty) staging folder (by name: the fixture
  // before the swap also has 8 entries)
  const auto snap = Snapshot(c.dir);
  std::string names;
  for (const auto& kv : snap) names += kv.first + " ";
  CHECK_EQ(names,
           "TracyClient.dll TracyClient.dll.old dead_or_alive_4.exe dead_or_alive_4.exe.old dead_or_alive_4.toml "
           "rexruntime.dll rexruntime.dll.old update_new ");
  CHECK(snap.count("dead_or_alive_4.toml") && snap.find("dead_or_alive_4.toml")->second == "[netplay]\nnetplay_delay = 2\n");
}

// A refusal before any rename leaves both folders byte-identical.
static void ExpectRefused(const char* what, const Case& c, const std::string& sums, const std::string& must_name) {
  const auto dir0 = Snapshot(c.dir);
  const std::string why = up::VerifyAndSwap(c.dir, c.staging, sums);
  ++g_checks;
  if (why.empty() || !Has(why, must_name)) {
    std::printf("FAIL %s: refusal '%s' does not name %s\n", what, why.c_str(), must_name.c_str());
    ++g_failures;
  }
  const auto dir1 = Snapshot(c.dir);
  ++g_checks;
  if (dir1 != dir0) {
    std::printf("FAIL %s: the folder changed\n  before: %s\n  after:  %s\n", what, Describe(dir0).c_str(),
                Describe(dir1).c_str());
    ++g_failures;
  }
}

static void TestRefusals() {
  // a wrong checksum for each file (the last one too: a check made inside the rename loop would have renamed two)
  for (int bad = 0; bad < 3; ++bad) {
    const Case c = MakeCase("wrong" + std::to_string(bad));
    ExpectRefused(("wrong sum " + std::to_string(bad)).c_str(), c, SumsText(bad), up::kFiles[bad]);
  }
  // a staged file that differs from the sum (a broken download): one byte of the last file flipped
  {
    const Case c = MakeCase("corrupt");
    std::string b = kM448;
    b[10] ^= 1;
    Write(c.staging / up::kFiles[2], b);
    ExpectRefused("corrupt staged file", c, SumsText(), up::kFiles[2]);
  }
  // a SHA256SUMS.txt with no line for one file
  for (int skip = 0; skip < 3; ++skip) {
    const Case c = MakeCase("noline" + std::to_string(skip));
    ExpectRefused(("no line " + std::to_string(skip)).c_str(), c, SumsText(-1, skip), up::kFiles[skip]);
  }
  // a file named twice (even with the same, right hash)
  {
    const Case c = MakeCase("twice");
    ExpectRefused("named twice", c, SumsText() + kShaAbc + "  rexruntime.dll\n", up::kFiles[1]);
  }
  // a staged file missing (the download did not happen)
  {
    const Case c = MakeCase("nostaged");
    std::error_code ec;
    fs::remove(c.staging / up::kFiles[2], ec);
    ExpectRefused("staged file missing", c, SumsText(), up::kFiles[2]);
  }
  // an empty SHA256SUMS.txt
  {
    const Case c = MakeCase("emptysums");
    ExpectRefused("empty sums", c, "", up::kFiles[0]);
  }
}

// Every rename step fails in turn: both folders end byte-identical to the start (every file compared).
static void TestRollbackEveryStep() {
  for (int n = 0; n < 6; ++n) {
    const Case c = MakeCase("failat" + std::to_string(n));
    const auto dir0 = Snapshot(c.dir);
    CHECK(dir0.size() == 8);  // the fixture: 3 files, the toml, the staging folder and its 3 files
    const std::string why = up::VerifyAndSwap(c.dir, c.staging, SumsText(), n);
    ++g_checks;
    if (why.empty() || !Has(why, up::kFiles[n / 2])) {
      std::printf("FAIL fail_at %d: '%s' does not name %s\n", n, why.c_str(), up::kFiles[n / 2]);
      ++g_failures;
    }
    const auto dir1 = Snapshot(c.dir);
    ++g_checks;
    if (dir1 != dir0) {
      std::printf("FAIL fail_at %d: the folder is not as it was\n  before: %s\n  after:  %s\n", n, Describe(dir0).c_str(),
                  Describe(dir1).c_str());
      ++g_failures;
    }
  }
  // fail_at past the last step, or negative: all six done
  {
    const Case c = MakeCase("failat6");
    CHECK_EQ(up::VerifyAndSwap(c.dir, c.staging, SumsText(), 6), "");
    CHECK(Snapshot(c.staging).empty());
  }
}

// Real failures (no fail_at): the rollback undoes renames that really happened.
static void TestRealFailures() {
  // a leftover rexruntime.dll.old: step 2 cannot rename onto it; steps 0 and 1 are undone
  {
    const Case c = MakeCase("leftover");
    Write(c.dir / "rexruntime.dll.old", "a leftover from an older update");
    const auto dir0 = Snapshot(c.dir);
    const std::string why = up::VerifyAndSwap(c.dir, c.staging, SumsText());
    CHECK(!why.empty() && Has(why, "rexruntime.dll"));
    // review M-5: the player is told what to do (CheckAndUpdate deletes the .old files nobody uses first)
    CHECK(Has(why, "an old copy of rexruntime.dll is still in use: close other copies of the game and try again"));
    const auto dir1 = Snapshot(c.dir);
    CHECK(dir1 == dir0);
    if (dir1 != dir0) std::printf("  before: %s\n  after:  %s\n", Describe(dir0).c_str(), Describe(dir1).c_str());
  }
  // the staged TracyClient.dll held open without delete sharing: step 5 fails; steps 0-4 are undone
  {
    const Case c = MakeCase("locked");
    const auto dir0 = Snapshot(c.dir);
    const fs::path locked = c.staging / up::kFiles[2];
    HANDLE h = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    const std::string why = up::VerifyAndSwap(c.dir, c.staging, SumsText());
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK(!why.empty() && Has(why, "TracyClient.dll"));
    const auto dir1 = Snapshot(c.dir);
    CHECK(dir1 == dir0);
    if (dir1 != dir0) std::printf("  before: %s\n  after:  %s\n", Describe(dir0).c_str(), Describe(dir1).c_str());
  }
}

// An install without TracyClient.dll: placed without a backup; a failure at its step leaves it absent again.
static void TestMissingOriginal() {
  {
    const Case c = MakeCase("noorig", false);
    CHECK_EQ(up::VerifyAndSwap(c.dir, c.staging, SumsText()), "");
    std::string now;
    CHECK(Read(c.dir / up::kFiles[2], &now) && now == kM448);
    CHECK(!Exists(c.dir / "TracyClient.dll.old"));
    CHECK(Exists(c.dir / "rexruntime.dll.old") && Exists(c.dir / "dead_or_alive_4.exe.old"));
  }
  for (int n = 4; n < 6; ++n) {
    const Case c = MakeCase("noorig_fail" + std::to_string(n), false);
    const auto dir0 = Snapshot(c.dir);
    CHECK(!up::VerifyAndSwap(c.dir, c.staging, SumsText(), n).empty());
    CHECK(Snapshot(c.dir) == dir0);
  }
}

static void TestDeleteOldFiles() {
  const fs::path d = NewDir("delold");
  for (int i = 0; i < 3; ++i) {
    Write(d / up::kFiles[i], kOld[i]);
    Write(d / (std::string(up::kFiles[i]) + ".old"), "old " + std::to_string(i));
  }
  // never touched: other .old names, near-miss names, the config, the files themselves, an .old in a subfolder, and the
  // .dev copies of a self-built game (review I-2)
  for (int i = 0; i < 3; ++i) Write(d / (std::string(up::kFiles[i]) + ".dev"), "dev " + std::to_string(i));
  Write(d / "other.dll.old", "x");
  Write(d / "dead_or_alive_4.exe.old2", "y");
  Write(d / "rexruntime.old", "z");
  Write(d / "dead_or_alive_4.toml", "t");
  std::error_code ec;
  fs::create_directories(d / "logs", ec);
  Write(d / "logs" / "dead_or_alive_4.exe.old", "in a subfolder");
  auto expect = Snapshot(d);
  for (int i = 0; i < 3; ++i) expect.erase(std::string(up::kFiles[i]) + ".old");
  CHECK(expect.size() == 12);
  CHECK(expect.count("rexruntime.dll.dev") == 1);
  up::DeleteOldFiles(d);
  const auto after = Snapshot(d);
  CHECK(after == expect);
  if (after != expect) std::printf("  want: %s\n  got:  %s\n", Describe(expect).c_str(), Describe(after).c_str());
  // an .old whose file is missing is kept (it may be the only copy); the others still go
  const fs::path m = NewDir("delold_missing");
  Write(m / "TracyClient.dll.old", "the only copy");
  Write(m / "rexruntime.dll", "r");
  Write(m / "rexruntime.dll.old", "r old");
  up::DeleteOldFiles(m);
  CHECK(Exists(m / "TracyClient.dll.old"));
  CHECK(!Exists(m / "rexruntime.dll.old"));
  CHECK(Exists(m / "rexruntime.dll"));
  // a folder that does not exist: nothing happens, nothing throws
  up::DeleteOldFiles(g_root / "no_such_folder");
  CHECK(!Exists(g_root / "no_such_folder"));
}

// ---- the release decision ----
static std::string Asset(const std::string& name, const std::string& tag) {
  return "{\"name\":\"" + name + "\",\"browser_download_url\":\"https://github.com/o/r/releases/download/" + tag + "/" +
         name + "\"}";
}
static std::string Release(const std::string& tag, const std::vector<std::string>& names, bool draft = false) {
  std::string a;
  for (size_t i = 0; i < names.size(); ++i) a += (i ? "," : "") + Asset(names[i], tag);
  return "{\"tag_name\":\"" + tag + "\",\"draft\":" + (draft ? "true" : "false") + ",\"prerelease\":true,\"assets\":[" + a +
         "]}";
}
static const std::vector<std::string> kAll = {"dead_or_alive_4.exe", "rexruntime.dll", "TracyClient.dll",
                                              "SHA256SUMS.txt", "DOA4-x-win64.zip"};

static void TestChoose() {
  // 1.0.7 lacks SHA256SUMS.txt, 1.0.8 lacks TracyClient.dll, 2.0.0 is a draft, other-v9 is another channel:
  // the pick is 1.0.6 (the list is in no order)
  const std::string json =
      "[" + Release("netplay-v1.0.5", kAll) + "," +
      Release("netplay-v1.0.7", {"dead_or_alive_4.exe", "rexruntime.dll", "TracyClient.dll"}) + "," +
      Release("netplay-v1.0.8", {"dead_or_alive_4.exe", "rexruntime.dll", "SHA256SUMS.txt"}) + "," +
      Release("netplay-v2.0.0", kAll, true) + "," + Release("other-v9.9.9", kAll) + "," +
      Release("netplay-v1.0.6", kAll) + "]";
  {
    const up::Choice c = up::Choose(json, "netplay", "netplay-v1.0.5");
    CHECK(c.update && !c.replaces_dev);
    CHECK_EQ(c.tag, "netplay-v1.0.6");
    CHECK(c.urls.size() == 4);
    CHECK(c.urls.count("SHA256SUMS.txt") && c.urls.find("SHA256SUMS.txt")->second ==
                                                 "https://github.com/o/r/releases/download/netplay-v1.0.6/SHA256SUMS.txt");
    for (int i = 0; i < 3; ++i) CHECK(c.urls.count(up::kFiles[i]));
  }
  {
    const up::Choice c = up::Choose(json, "netplay", "netplay-v1.0.6");  // the same
    CHECK(!c.update);
    CHECK_EQ(c.message, "Already on the latest netplay release (netplay-v1.0.6)");
  }
  {
    const up::Choice c = up::Choose(json, "netplay", "netplay-v1.0.9");  // newer than any (a local release tag)
    CHECK(!c.update);
    CHECK_EQ(c.message, "Already on the latest netplay release (netplay-v1.0.9)");
  }
  for (const char* dev : {"dev", ""}) {  // a self-built game (an unparsable tag): any release replaces it
    const up::Choice c = up::Choose(json, "netplay", dev);
    CHECK(c.update && c.replaces_dev);
    CHECK_EQ(c.tag, "netplay-v1.0.6");
  }
  {  // P3D-2: x.y.0 is a real release now, not a self-built game: it updates, but nothing is kept as .dev
    const up::Choice c = up::Choose(json, "netplay", "netplay-v1.0.0");
    CHECK(c.update && !c.replaces_dev);
    CHECK_EQ(c.tag, "netplay-v1.0.6");
  }
  {
    const up::Choice c = up::Choose("[]", "netplay", "netplay-v1.0.5");
    CHECK(!c.update);
    CHECK_EQ(c.message, "No netplay release found yet");
  }
  {
    const up::Choice c = up::Choose("{\"message\":\"Not Found\"}", "netplay", "dev");  // an error object
    CHECK(!c.update);
    CHECK_EQ(c.message, "No netplay release found yet");
  }
  CHECK_EQ(up::UpdatedText("netplay-v1.0.6", false), "Updated to netplay-v1.0.6. Restart the game to use it.");
  CHECK_EQ(up::UpdatedText("netplay-v1.0.6", true),
           "Updated to netplay-v1.0.6 (your self-built files are kept as .dev). Restart the game to use it.");
  CHECK_EQ(up::OfferText("netplay-v1.0.6"),
           "Release netplay-v1.0.6 replaces your self-built game. Press again to install.");
}

// P3D / the bridge: what the DEPLOYED updaters (netplay-v1.0.7, np1-v109 = v1.0.9) do is the code of Choose/PickRelease at
// those refs. ParseVersion, CompareVersions and PickRelease are unchanged since then; IsDevBuild is not (a patch of 0 no
// longer counts as self-built), but these pins use inputs with a non-zero patch (1.0.7, 1.0.9, 1.0.10), where the old and
// the new IsDevBuild agree. So they run today's code on the old updaters' behalf; and the new alpha channel's own picks.
static void TestChannels() {
  const std::string json = "[" + Release("netplay-v1.0.9", kAll) + "," + Release("netplay-v1.0.10", kAll) + "," +
                           Release("alpha-v0.3.0", kAll) + "]";
  for (const char* cur : {"netplay-v1.0.9", "netplay-v1.0.7"}) {  // old copies on the old channel take the bridge
    const up::Choice c = up::Choose(json, "netplay", cur);
    CHECK(c.update && !c.replaces_dev);
    CHECK_EQ(c.tag, "netplay-v1.0.10");
    CHECK(c.urls.size() == 4);
  }
  {  // ... and, once on the bridge's build, nothing newer is on their channel (alpha-v0.3.0 is invisible to them)
    const up::Choice c = up::Choose(json, "netplay", "netplay-v1.0.10");
    CHECK(!c.update);
    CHECK_EQ(c.message, "Already on the latest netplay release (netplay-v1.0.10)");
  }
  const std::string alpha = "[" + Release("netplay-v1.0.9", kAll) + "," + Release("netplay-v1.0.10", kAll) + "," +
                            Release("alpha-v0.3.0", kAll) + "," + Release("alpha-v0.3.1", kAll) + "]";
  {  // the alpha channel ignores every netplay-v1.0.x and takes the higher alpha number
    const up::Choice c = up::Choose(alpha, "alpha", "alpha-v0.3.0");
    CHECK(c.update && !c.replaces_dev);
    CHECK_EQ(c.tag, "alpha-v0.3.1");
  }
  {
    const up::Choice c = up::Choose(alpha, "alpha", "alpha-v0.3.1");
    CHECK(!c.update);
    CHECK_EQ(c.message, "Already on the latest alpha release (alpha-v0.3.1)");
  }
  {  // only old-channel releases: the alpha channel has none
    const std::string only_old = "[" + Release("netplay-v1.0.9", kAll) + "," + Release("netplay-v1.0.10", kAll) + "]";
    const up::Choice c = up::Choose(only_old, "alpha", "alpha-v0.3.0");
    CHECK(!c.update);
    CHECK_EQ(c.message, "No alpha release found yet");
  }
  {  // a local build ("dev") on the alpha channel is offered alpha-v0.3.1, never a netplay tag
    const up::Choice c = up::Choose(alpha, "alpha", "dev");
    CHECK(c.update && c.replaces_dev);
    CHECK_EQ(c.tag, "alpha-v0.3.1");
  }
}

static void TestMessages() {
  // no check has run in this process: Refuse sets the message, Busy stays false
  CHECK(!up::Busy());
  up::Refuse(up::kOnlineText);
  CHECK_EQ(up::Message(), "Disconnect from online play first.");
  CHECK(!up::Busy());
}

static void TestExeDir() {
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  CHECK(up::ExeDir() == fs::path(buf).parent_path());
  CHECK(Exists(up::ExeDir() / "updater_host_test.exe"));
}

// =====================================================================================================
// Fix round 1
// =====================================================================================================

template <class F>
static bool WaitFor(F f, int ms) {
  const auto t0 = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms)) {
    if (f()) return true;
    Sleep(5);
  }
  return f();
}
static long long MsSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

// I-2: a self-built game's files are kept as <name>.dev; an existing .dev is never replaced.
static void TestDevBackup() {
  {
    const Case c = MakeCase("dev");
    CHECK_EQ(up::VerifyAndSwap(c.dir, c.staging, SumsText(), -1, ".dev"), "");
    for (int i = 0; i < 3; ++i) {
      std::string now, kept;
      CHECK(Read(c.dir / up::kFiles[i], &now) && now == NewContent(i));
      CHECK(Read(c.dir / (std::string(up::kFiles[i]) + ".dev"), &kept) && kept == kOld[i]);
      CHECK(!Exists(c.dir / (std::string(up::kFiles[i]) + ".old")));
    }
  }
  {
    const Case c = MakeCase("dev_inway");
    Write(c.dir / "TracyClient.dll.dev", "an earlier self-built copy");
    const auto dir0 = Snapshot(c.dir);
    const std::string why = up::VerifyAndSwap(c.dir, c.staging, SumsText(), -1, ".dev");
    CHECK(Has(why, "TracyClient.dll.dev") && Has(why, "move it out of the game's folder"));
    CHECK(Snapshot(c.dir) == dir0);
  }
  for (int n : {1, 3, 5}) {  // the rollback puts .dev renames back too
    const Case c = MakeCase("dev_fail" + std::to_string(n));
    const auto dir0 = Snapshot(c.dir);
    CHECK(!up::VerifyAndSwap(c.dir, c.staging, SumsText(), n, ".dev").empty());
    CHECK(Snapshot(c.dir) == dir0);
  }
}

// M-2: at start, a leftover update_new goes with the .old files; .dev files and everything else stay.
static void TestCleanUpAtStart() {
  const fs::path d = NewDir("cleanup");
  for (int i = 0; i < 3; ++i) {
    Write(d / up::kFiles[i], kOld[i]);
    Write(d / (std::string(up::kFiles[i]) + ".old"), "old");
    Write(d / (std::string(up::kFiles[i]) + ".dev"), "dev");
  }
  std::error_code ec;
  fs::create_directories(d / "update_new" / "sub", ec);
  Write(d / "update_new" / "dead_or_alive_4.exe", "half a download");
  Write(d / "update_new" / "sub" / "x", "x");
  Write(d / "dead_or_alive_4.toml", "t");
  up::CleanUpAtStart(d);
  CHECK(!Exists(d / "update_new"));
  for (int i = 0; i < 3; ++i) {
    CHECK(!Exists(d / (std::string(up::kFiles[i]) + ".old")));
    CHECK(Exists(d / (std::string(up::kFiles[i]) + ".dev")));
    CHECK(Exists(d / up::kFiles[i]));
  }
  CHECK(Exists(d / "dead_or_alive_4.toml"));
  // an empty or relative folder: nothing happens (never the current directory's update_new)
  const fs::path here = NewDir("cleanup_cwd");
  fs::create_directories(here / "update_new", ec);
  Write(here / "update_new" / "keep", "k");
  wchar_t old_cwd[MAX_PATH];
  GetCurrentDirectoryW(MAX_PATH, old_cwd);
  CHECK(SetCurrentDirectoryW(here.c_str()) != 0);
  up::CleanUpAtStart(fs::path());
  up::CleanUpAtStart(fs::path("."));
  SetCurrentDirectoryW(old_cwd);
  CHECK(Exists(here / "update_new" / "keep"));
}

// M-3: the hosts a request may go to or end at.
static void ExpectHost(const char* host, bool want) {
  ++g_checks;
  if (up::HostAllowed(host) != want) {
    std::printf("FAIL host '%s': HostAllowed %s, want %s\n", host, want ? "false" : "true", want ? "true" : "false");
    ++g_failures;
  }
}
static void TestHostAllowed() {
  for (const char* h : {"github.com", "api.github.com", "GitHub.com", "API.GITHUB.COM", "objects.githubusercontent.com",
                        "release-assets.githubusercontent.com", "Objects.GitHubUserContent.COM"})
    ExpectHost(h, true);
  for (const char* h : {"", "githubusercontent.com", ".githubusercontent.com", "evilgithubusercontent.com",
                        "objects.githubusercontent.com.evil.net", "github.com.", "objects.githubusercontent.com.",
                        "evilgithub.com", "github.com.evil.com", "uploads.github.com", "codeload.github.com",
                        "raw.github.io", "a.b.githubusercontent.com", "ob jects.githubusercontent.com",
                        "objects_x.githubusercontent.com", "api.github.com.evil.com", "xgithub.com"})
    ExpectHost(h, false);
}

// I-3: one check at a time; Busy clears afterwards; the log comes from the check's own thread; an exception ends it.
static void TestStartBusy() {
  up::ResetForTest();
  std::atomic<bool> release{false}, saw_confirm{false}, saw_predicate{false};
  std::atomic<int> runs{0};
  std::atomic<DWORD> tid{0};
  std::mutex lmu;
  std::vector<std::pair<DWORD, std::string>> lines;
  up::Request req;
  req.log = [&](const std::string& l) {
    std::lock_guard<std::mutex> g(lmu);
    lines.emplace_back(GetCurrentThreadId(), l);
  };
  req.confirm = true;
  req.may_swap = [] { return false; };
  auto check = [&](const up::Request& r) {
    ++runs;
    tid = GetCurrentThreadId();
    saw_confirm = r.confirm;
    saw_predicate = r.may_swap && !r.may_swap();
    for (int i = 0; i < 500 && !release.load(); ++i) Sleep(10);
    up::Result res;
    res.message = "fake check done";
    return res;
  };
  CHECK(up::Start(req, check));
  CHECK(up::Busy());
  CHECK_EQ(up::Message(), "Checking for updates...");
  CHECK(!up::Start(req, check));  // one at a time
  up::Refuse(up::kOnlineText);     // a refusal does not overwrite a running check's message
  CHECK_EQ(up::Message(), "Checking for updates...");
  release = true;
  CHECK(WaitFor([] { return !up::Busy(); }, 3000));
  CHECK(runs.load() == 1);
  CHECK_EQ(up::Message(), "fake check done");
  CHECK(saw_confirm.load() && saw_predicate.load());  // the request reaches the check as given
  {
    std::lock_guard<std::mutex> g(lmu);
    CHECK(lines.size() == 2);
    if (lines.size() == 2) {
      CHECK_EQ(lines[0].second, "[doa4][UPDATE] Checking for updates...");
      CHECK_EQ(lines[1].second, "[doa4][UPDATE] fake check done");
      CHECK(lines[0].first == tid.load() && lines[1].first == tid.load() && tid.load() != GetCurrentThreadId());
    }
  }
  // an exception out of the check: Busy clears and the message says so; the next check starts
  CHECK(up::Start(up::Request{}, [](const up::Request&) -> up::Result { throw std::runtime_error("boom"); }));
  CHECK(WaitFor([] { return !up::Busy(); }, 3000));
  CHECK_EQ(up::Message(), "The update check stopped unexpectedly");
  CHECK(up::Start(up::Request{}, [](const up::Request&) {
    up::Result r;
    r.message = "again";
    return r;
  }));
  CHECK(WaitFor([] { return !up::Busy(); }, 3000));
  CHECK_EQ(up::Message(), "again");
}

// ---- the whole check through Env: a fake HTTP source and a temp game folder ----
struct FakeHttp {
  std::map<std::string, std::string> bodies;
  std::map<std::string, std::string> fail;  // url -> the error text
  std::vector<std::string> asked;
  std::function<void(const std::string& url, size_t chunk)> on_chunk;  // called before each 4 KiB chunk
  bool Get(const std::string& url, bool, const up::Sink& sink, std::string* error) {
    asked.push_back(url);
    const auto f = fail.find(url);
    if (f != fail.end()) {
      *error = f->second;
      return false;
    }
    const auto it = bodies.find(url);
    if (it == bodies.end()) {
      *error = "HTTP 404";
      return false;
    }
    const std::string& b = it->second;
    for (size_t at = 0, n = 0; at < b.size(); at += 4096, ++n) {
      if (on_chunk) on_chunk(url, n);
      if (!sink(b.data() + at, std::min<size_t>(4096, b.size() - at))) {
        *error = "stopped by the sink";
        return false;
      }
    }
    return true;
  }
};
static const std::string kApi = "https://api.github.com/repos/o/r/releases?per_page=30";
static const std::string kTag = "netplay-v1.0.6";
static std::string Url(const std::string& name) { return "https://github.com/o/r/releases/download/" + kTag + "/" + name; }
static void Serve(FakeHttp& h, const std::string& sums) {
  h.bodies[kApi] = "[" + Release(kTag, kAll) + "]";
  for (int i = 0; i < 3; ++i) h.bodies[Url(up::kFiles[i])] = NewContent(i);
  h.bodies[Url("SHA256SUMS.txt")] = sums;
}
static up::Env MakeEnv(FakeHttp& h, const fs::path& dir, const std::string& current) {
  up::Env e;
  e.get = [&h](const std::string& u, bool api, const up::Sink& s, std::string* err) { return h.Get(u, api, s, err); };
  e.dir = dir;
  e.repo = "o/r";
  e.channel = "netplay";
  e.current = current;
  return e;
}
static fs::path GameDir(const std::string& tag) {  // the three old files and a toml
  const fs::path d = NewDir(tag);
  for (int i = 0; i < 3; ++i) Write(d / up::kFiles[i], kOld[i]);
  Write(d / "dead_or_alive_4.toml", "t");
  return d;
}
// A check that must change nothing: the folder (no update_new left either) and the latch.
static void ExpectUnchanged(const char* what, const fs::path& d, const std::map<std::string, std::string>& d0) {
  const auto d1 = Snapshot(d);
  ++g_checks;
  if (d1 != d0 || up::RestartPending()) {
    std::printf("FAIL %s: changed (restart pending %d)\n  before: %s\n  after:  %s\n", what, int(up::RestartPending()),
                Describe(d0).c_str(), Describe(d1).c_str());
    ++g_failures;
  }
}

static void TestCheckAndUpdate() {
  up::ResetForTest();
  {  // no release yet: only the list is asked for
    FakeHttp h;
    h.bodies[kApi] = "[]";
    const fs::path d = GameDir("cau_none");
    const auto d0 = Snapshot(d);
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(!r.ok);
    CHECK_EQ(r.message, "No netplay release found yet");
    CHECK(h.asked.size() == 1 && h.asked[0] == kApi);
    ExpectUnchanged("no release", d, d0);
  }
  {  // GitHub not reachable
    FakeHttp h;
    h.fail[kApi] = "no answer (error 12007)";
    const fs::path d = GameDir("cau_unreachable");
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK_EQ(r.message, "Could not reach GitHub (no answer (error 12007))");
  }
  {  // a wrong sum for the last file: all four downloaded, nothing changed, update_new removed
    FakeHttp h;
    Serve(h, SumsText(2));
    const fs::path d = GameDir("cau_badsum");
    const auto d0 = Snapshot(d);
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(!r.ok && Has(r.message, "TracyClient.dll") && Has(r.message, "does not match"));
    CHECK(h.asked.size() == 5);
    ExpectUnchanged("bad sum", d, d0);
  }
  {  // may_swap says no (online play began during the download): asked once, nothing changed
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("cau_mayswap");
    const auto d0 = Snapshot(d);
    std::atomic<int> asked{0};
    up::Request q;
    q.may_swap = [&] {
      ++asked;
      return false;
    };
    const up::Result r = up::CheckAndUpdate(q, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(!r.ok && Has(r.message, "online play"));
    CHECK(asked.load() == 1);
    ExpectUnchanged("may_swap no", d, d0);
  }
  {  // a game file over the cap
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("cau_cap");
    const auto d0 = Snapshot(d);
    up::Env e = MakeEnv(h, d, "netplay-v1.0.5");
    e.max_file_bytes = 100000;  // the exe fixture has 200,003 bytes, the DLL fixtures 3 and 56
    const up::Result r = up::CheckAndUpdate(up::Request{}, e);
    CHECK(!r.ok && Has(r.message, "dead_or_alive_4.exe") && Has(r.message, "larger than"));
    ExpectUnchanged("cap", d, d0);
  }
  {  // a download that fails
    FakeHttp h;
    Serve(h, SumsText());
    h.fail[Url("rexruntime.dll")] = "HTTP 503";
    const fs::path d = GameDir("cau_dlfail");
    const auto d0 = Snapshot(d);
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(!r.ok && Has(r.message, "rexruntime.dll") && Has(r.message, "HTTP 503"));
    ExpectUnchanged("download fails", d, d0);
  }
  {  // a SHA256SUMS.txt over 1 MB
    FakeHttp h;
    Serve(h, SumsText() + std::string(size_t(1) << 20, '#'));
    const fs::path d = GameDir("cau_bigsums");
    const auto d0 = Snapshot(d);
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(!r.ok && Has(r.message, "SHA256SUMS.txt"));
    ExpectUnchanged("big sums", d, d0);
  }
  {  // I-2: a self-built game; confirm with nothing offered yet only offers
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("cau_devconfirm0");
    const auto d0 = Snapshot(d);
    up::Request c;
    c.confirm = true;
    const up::Result r = up::CheckAndUpdate(c, MakeEnv(h, d, "dev"));
    CHECK(!r.ok);
    CHECK_EQ(r.message, up::OfferText(kTag));
    CHECK(h.asked.size() == 1);
    ExpectUnchanged("dev confirm without an offer", d, d0);
    CHECK(up::OfferPending());
  }
  up::ResetForTest();
  CHECK(!up::OfferPending());
  {  // I-2: the first press only offers; the confirmed second press installs and keeps the self-built files as .dev
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("cau_dev");
    const auto d0 = Snapshot(d);
    const up::Result r1 = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "dev"));
    CHECK(!r1.ok);
    CHECK_EQ(r1.message, up::OfferText(kTag));
    CHECK(h.asked.size() == 1 && up::OfferPending());
    ExpectUnchanged("dev offer", d, d0);
    up::Request c;
    c.confirm = true;
    const up::Result r2 = up::CheckAndUpdate(c, MakeEnv(h, d, "dev"));
    CHECK(r2.ok);
    CHECK_EQ(r2.message, up::UpdatedText(kTag, true));
    for (int i = 0; i < 3; ++i) {
      std::string now, kept;
      CHECK(Read(d / up::kFiles[i], &now) && now == NewContent(i));
      CHECK(Read(d / (std::string(up::kFiles[i]) + ".dev"), &kept) && kept == kOld[i]);
      CHECK(!Exists(d / (std::string(up::kFiles[i]) + ".old")));
    }
    CHECK(!Exists(d / "update_new"));
    CHECK(up::RestartPending() && !up::OfferPending());
    // the latch: a later check downloads nothing
    const size_t n = h.asked.size();
    const up::Result r3 = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "dev"));
    CHECK(!r3.ok && h.asked.size() == n);
    CHECK_EQ(r3.message, up::UpdatedText(kTag, false));
  }
  up::ResetForTest();
  CHECK(!up::RestartPending());
  {  // a release game installs at the first press: .old kept; a stale .old nobody uses is deleted first; a leftover
     // update_new of an interrupted check is replaced and removed
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("cau_release");
    Write(d / "rexruntime.dll.old", "stale, not in use");
    std::error_code ec;
    fs::create_directories(d / "update_new", ec);
    Write(d / "update_new" / "junk", "j");
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(r.ok);
    CHECK_EQ(r.message, up::UpdatedText(kTag, false));
    for (int i = 0; i < 3; ++i) {
      std::string now, old;
      CHECK(Read(d / up::kFiles[i], &now) && now == NewContent(i));
      CHECK(Read(d / (std::string(up::kFiles[i]) + ".old"), &old) && old == kOld[i]);
      CHECK(!Exists(d / (std::string(up::kFiles[i]) + ".dev")));
    }
    CHECK(!Exists(d / "update_new"));
    CHECK(up::RestartPending());
  }
  up::ResetForTest();
}

// M-1 (last: Shutdown is for good): a download stops at its next chunk; Shutdown waits at most its limit for the swap
// step and a cancel there swaps nothing; after Shutdown nothing starts.
// The folder's own SHA256SUMS.txt (the retired launcher's damage check reads exactly this file): after an in-game update it
// is the release's verified one, so the launcher sees intact files; absent stays absent; a refused swap leaves the old one.
static void TestSumsRefresh() {
  up::ResetForTest();
  {
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("sums_refresh");
    Write(d / "SHA256SUMS.txt", "the bridge's old sums\n");
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(r.ok);
    std::string now;
    CHECK(Read(d / "SHA256SUMS.txt", &now) && now == SumsText());
    CHECK(!Exists(d / "SHA256SUMS.txt.tmp"));
  }
  up::ResetForTest();
  {  // no SHA256SUMS.txt in the folder: none is created
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("sums_absent");
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(r.ok);
    CHECK(!Exists(d / "SHA256SUMS.txt") && !Exists(d / "SHA256SUMS.txt.tmp"));
  }
  up::ResetForTest();
  {  // a swap that is refused (wrong sum for the last file): the old SHA256SUMS.txt stays
    FakeHttp h;
    Serve(h, SumsText(2));
    const fs::path d = GameDir("sums_refused");
    Write(d / "SHA256SUMS.txt", "old sums\n");
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(!r.ok);
    std::string now;
    CHECK(Read(d / "SHA256SUMS.txt", &now) && now == "old sums\n");
  }
  up::ResetForTest();
  {  // the refresh cannot be written (a folder holds that name): a WARN line, the update still succeeded
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("sums_warn");
    std::error_code ec;
    fs::create_directories(d / "SHA256SUMS.txt", ec);
    std::vector<std::string> lines;
    up::Request q;
    q.log = [&](const std::string& l) { lines.push_back(l); };
    const up::Result r = up::CheckAndUpdate(q, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(r.ok && r.message == up::UpdatedText(kTag, false));
    bool warned = false;
    for (const auto& l : lines) warned = warned || Has(l, "WARN") && Has(l, "SHA256SUMS.txt");
    CHECK(warned);
    std::string now;
    CHECK(Read(d / up::kFiles[0], &now) && now == NewContent(0));
  }
  up::ResetForTest();
}

static void TestShutdown() {
  up::ResetForTest();
  {
    FakeHttp h;
    Serve(h, SumsText());
    h.on_chunk = [](const std::string& url, size_t n) {
      if (url.find("dead_or_alive_4.exe") != std::string::npos && n == 1) up::Shutdown(0);
    };
    const fs::path d = GameDir("shut_download");
    const auto d0 = Snapshot(d);
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK(!r.ok);
    ExpectUnchanged("shutdown during a download", d, d0);
    // the download stopped there: nothing after the exe was asked for (the swap step's own cancel check would leave the
    // folder unchanged even if the downloads had gone on)
    CHECK(h.asked.size() == 2);
    CHECK(!up::Start(up::Request{}, [](const up::Request&) { return up::Result{}; }));
    CHECK(!up::Busy());
    // Task 15: after Shutdown the check-only entry asks nothing, and its thread does not start (done never called)
    FakeHttp h2;
    Serve(h2, SumsText());
    const up::Result co = up::CheckOnly(MakeEnv(h2, GameDir("shut_checkonly"), "netplay-v1.0.5"));
    CHECK_FOUND(co, up::Found::kFailed);
    CHECK(h2.asked.empty());
    std::atomic<bool> called{false};
    CHECK(!up::StartCheckOnly(MakeEnv(h2, GameDir("shut_checkonly2"), "netplay-v1.0.5"),
                              [&](const up::Result&) { called = true; }));
    Sleep(200);
    CHECK(!called.load() && h2.asked.empty());
  }
  up::ResetForTest();
  {
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("shut_swap");
    const auto d0 = Snapshot(d);
    std::atomic<bool> in_predicate{false};
    up::Request q;
    q.may_swap = [&] {
      in_predicate = true;
      Sleep(3000);
      return true;
    };
    const up::Env env = MakeEnv(h, d, "netplay-v1.0.5");
    CHECK(up::Start(q, [env](const up::Request& r) { return up::CheckAndUpdate(r, env); }));
    CHECK(WaitFor([&] { return in_predicate.load(); }, 5000));
    const auto t0 = std::chrono::steady_clock::now();
    up::Shutdown(1000);
    const long long ms = MsSince(t0);
    std::printf("info: Shutdown(1000) during the swap step returned after %lld ms\n", ms);
    CHECK(ms >= 900 && ms < 2500);
    CHECK(WaitFor([] { return !up::Busy(); }, 6000));
    ExpectUnchanged("shutdown during the swap step", d, d0);
  }
  up::ResetForTest();
  {
    const auto t0 = std::chrono::steady_clock::now();
    up::Shutdown(1000);  // nothing in progress: at once
    CHECK(MsSince(t0) < 300);
  }
}

// ---- Task 15: what the list said (Result::found), the check-only entry and the done callback ----
// CheckAndUpdate classifies the list for the button's memory refresh: newer even when the download or the swap fails
// afterwards (the release exists), up to date, and failed for no answer / no release of the channel.
static void TestFound() {
  up::ResetForTest();
  {
    FakeHttp h;
    h.bodies[kApi] = "[]";
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, GameDir("found_none"), "netplay-v1.0.5"));
    CHECK_FOUND(r, up::Found::kFailed);
  }
  {
    FakeHttp h;
    h.fail[kApi] = "no answer (error 12007)";
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, GameDir("found_unreachable"), "netplay-v1.0.5"));
    CHECK_FOUND(r, up::Found::kFailed);
  }
  {  // the same release: up to date; a running tag past every release: up to date too (not "newer", not "failed")
    FakeHttp h;
    Serve(h, SumsText());
    const up::Result same = up::CheckAndUpdate(up::Request{}, MakeEnv(h, GameDir("found_same"), kTag));
    CHECK_FOUND(same, up::Found::kUpToDate);
    CHECK_EQ(same.tag, kTag);
    const up::Result past = up::CheckAndUpdate(up::Request{}, MakeEnv(h, GameDir("found_past"), "netplay-v1.0.7"));
    CHECK_FOUND(past, up::Found::kUpToDate);
  }
  {  // a newer release whose download fails: still newer (the release exists), nothing swapped
    FakeHttp h;
    Serve(h, SumsText(2));
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, GameDir("found_badsum"), "netplay-v1.0.5"));
    CHECK(!r.ok);
    CHECK_FOUND(r, up::Found::kNewer);
    CHECK_EQ(r.tag, kTag);
  }
  {  // a self-built game's first press (the offer): newer
    FakeHttp h;
    Serve(h, SumsText());
    const up::Result r = up::CheckAndUpdate(up::Request{}, MakeEnv(h, GameDir("found_dev"), "dev"));
    CHECK_FOUND(r, up::Found::kNewer);
  }
  up::ResetForTest();
}

// The check-only entry: one GET of the list, never a download, never update_new, never a swap or an offer, never the
// shared Busy/Message.
static void TestCheckOnly() {
  up::ResetForTest();
  up::Refuse("before");  // Message() must stay this
  {  // newer: only the list asked for (the download addresses are served and never fetched), the folder unchanged
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("co_newer");
    const auto d0 = Snapshot(d);
    const up::Result r = up::CheckOnly(MakeEnv(h, d, "netplay-v1.0.5"));
    CHECK_FOUND(r, up::Found::kNewer);
    CHECK_EQ(r.tag, kTag);
    CHECK(!r.ok);
    CHECK_EQ(r.message, "Release netplay-v1.0.6 is available");
    CHECK(h.asked.size() == 1 && h.asked[0] == kApi);  // NOT the four downloads CheckAndUpdate would ask for
    ExpectUnchanged("check only, newer", d, d0);
  }
  {  // up to date (same tag, and a running tag past it)
    FakeHttp h;
    Serve(h, SumsText());
    const up::Result same = up::CheckOnly(MakeEnv(h, GameDir("co_same"), kTag));
    CHECK_FOUND(same, up::Found::kUpToDate);
    CHECK_EQ(same.message, "Already on the latest netplay release (netplay-v1.0.6)");
    const up::Result past = up::CheckOnly(MakeEnv(h, GameDir("co_past"), "netplay-v1.0.7"));
    CHECK_FOUND(past, up::Found::kUpToDate);
    CHECK(h.asked.size() == 2);
  }
  {  // no answer
    FakeHttp h;
    h.fail[kApi] = "no answer (error 12007)";
    const up::Result r = up::CheckOnly(MakeEnv(h, GameDir("co_fail"), "netplay-v1.0.5"));
    CHECK_FOUND(r, up::Found::kFailed);
    CHECK_EQ(r.message, "Could not reach GitHub (no answer (error 12007))");
  }
  {  // no release of the channel
    FakeHttp h;
    h.bodies[kApi] = "[]";
    const up::Result r = up::CheckOnly(MakeEnv(h, GameDir("co_none"), "netplay-v1.0.5"));
    CHECK_FOUND(r, up::Found::kFailed);
    CHECK_EQ(r.message, "No netplay release found yet");
  }
  {  // a self-built game: newer, and no offer is recorded (the button's two-press confirm is not touched)
    FakeHttp h;
    Serve(h, SumsText());
    const fs::path d = GameDir("co_dev");
    const auto d0 = Snapshot(d);
    const up::Result r = up::CheckOnly(MakeEnv(h, d, "dev"));
    CHECK_FOUND(r, up::Found::kNewer);
    CHECK(!up::OfferPending());
    CHECK(h.asked.size() == 1);
    ExpectUnchanged("check only, dev", d, d0);
  }
  CHECK_EQ(up::Message(), "before");
  CHECK(!up::Busy());
  // StartCheckOnly: its own thread, done gets the result there; Busy stays false meanwhile (a button check can start)
  {
    FakeHttp h;
    Serve(h, SumsText());
    std::atomic<bool> release{false}, called{false};
    std::atomic<DWORD> tid{0};
    up::Result got;
    std::mutex gmu;
    up::Env e = MakeEnv(h, GameDir("co_thread"), "netplay-v1.0.5");
    auto inner = e.get;
    e.get = [&release, inner](const std::string& u, bool api, const up::Sink& s, std::string* err) {
      for (int i = 0; i < 500 && !release.load(); ++i) Sleep(10);
      return inner(u, api, s, err);
    };
    CHECK(up::StartCheckOnly(e, [&](const up::Result& r) {
      std::lock_guard<std::mutex> g(gmu);
      got = r;
      tid = GetCurrentThreadId();
      called = true;
    }));
    CHECK(!up::Busy());
    CHECK(up::Start(up::Request{}, [](const up::Request&) { return up::Result{}; }));  // not blocked by the start-up check
    CHECK(WaitFor([] { return !up::Busy(); }, 3000));
    release = true;
    CHECK(WaitFor([&] { return called.load(); }, 5000));
    std::lock_guard<std::mutex> g(gmu);
    CHECK_FOUND(got, up::Found::kNewer);
    CHECK(tid.load() != 0 && tid.load() != GetCurrentThreadId());
  }
  up::ResetForTest();
}

// Start's done: once, with the check's result, on the check's thread, after the last log line.
static void TestStartDone() {
  up::ResetForTest();
  std::mutex mu;
  std::vector<std::string> order;
  std::atomic<int> calls{0};
  up::Request q;
  q.log = [&](const std::string& l) {
    std::lock_guard<std::mutex> g(mu);
    order.push_back("log " + l);
  };
  q.done = [&](const up::Result& r) {
    std::lock_guard<std::mutex> g(mu);
    order.push_back(std::string("done ") + FoundName(r.found) + " " + r.tag);
    ++calls;
  };
  CHECK(up::Start(q, [](const up::Request&) {
    up::Result r;
    r.found = up::Found::kNewer;
    r.tag = "netplay-v9.9.9";
    r.message = "fake";
    return r;
  }));
  CHECK(WaitFor([&] { return calls.load() == 1; }, 3000));
  CHECK(WaitFor([] { return !up::Busy(); }, 3000));
  std::lock_guard<std::mutex> g(mu);
  CHECK(order.size() == 3);
  if (order.size() == 3) {
    CHECK_EQ(order[1], "log [doa4][UPDATE] fake");
    CHECK_EQ(order[2], "done newer netplay-v9.9.9");
  }
  up::ResetForTest();
}

// R107 (review M1): once Shutdown has begun, a check still running neither logs its result line nor calls done (so nothing
// reaches the app or the notice's mailbox while the process is ending).
static void TestCancelSkipsDone() {
  up::ResetForTest();
  CHECK(!up::Cancelling());
  {  // the button's Start thread: race-free, Busy clears only after the done decision
    std::atomic<bool> release{false}, entered{false}, called{false};
    std::mutex mu;
    std::vector<std::string> lines;
    up::Request q;
    q.log = [&](const std::string& l) {
      std::lock_guard<std::mutex> g(mu);
      lines.push_back(l);
    };
    q.done = [&](const up::Result&) { called = true; };
    CHECK(up::Start(q, [&](const up::Request&) {
      entered = true;
      for (int i = 0; i < 500 && !release.load(); ++i) Sleep(10);
      up::Result r;
      r.found = up::Found::kNewer;
      r.message = "late answer";
      return r;
    }));
    CHECK(WaitFor([&] { return entered.load(); }, 3000));
    up::Shutdown(0);
    CHECK(up::Cancelling());
    release = true;
    CHECK(WaitFor([] { return !up::Busy(); }, 6000));
    CHECK(!called.load());
    std::lock_guard<std::mutex> g(mu);
    CHECK(std::find(lines.begin(), lines.end(), "[doa4][UPDATE] late answer") == lines.end());
    CHECK(lines.size() == 1);  // only "Checking for updates..." (proof the thread ran and logged before Shutdown)
  }
  up::ResetForTest();
  {  // the start-up check's thread: the GET is held, Shutdown, then released; done must not come. This thread has no
     // completion signal, so the check waits 300 ms after the GET returned (a bounded wait, not a proof of absence).
    FakeHttp h;
    Serve(h, SumsText());
    std::atomic<bool> release{false}, entered{false}, returned{false}, called{false};
    up::Env e = MakeEnv(h, GameDir("cancel_checkonly"), "netplay-v1.0.5");
    auto inner = e.get;
    e.get = [&release, &entered, &returned, inner](const std::string& u, bool api, const up::Sink& s, std::string* err) {
      entered = true;
      for (int i = 0; i < 500 && !release.load(); ++i) Sleep(10);
      const bool ok = inner(u, api, s, err);
      returned = true;
      return ok;
    };
    CHECK(up::StartCheckOnly(e, [&](const up::Result&) { called = true; }));
    CHECK(WaitFor([&] { return entered.load(); }, 3000));
    up::Shutdown(0);
    release = true;
    CHECK(WaitFor([&] { return returned.load(); }, 3000));
    Sleep(300);
    CHECK(!called.load());
  }
  up::ResetForTest();
  CHECK(!up::Cancelling());
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  wchar_t tmp[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  g_root = fs::path(tmp) / ("doa4_updater_" + std::to_string(GetCurrentProcessId()));
  std::error_code ec;
  fs::create_directories(g_root, ec);
  TestSha256();
  TestParseSums();
  TestSwapOk();
  TestRefusals();
  TestRollbackEveryStep();
  TestRealFailures();
  TestMissingOriginal();
  TestDeleteOldFiles();
  TestChoose();
  TestChannels();
  TestMessages();
  TestExeDir();
  TestDevBackup();
  TestCleanUpAtStart();
  TestHostAllowed();
  TestStartBusy();
  TestCheckAndUpdate();
  TestFound();
  TestCheckOnly();
  TestStartDone();
  TestCancelSkipsDone();
  TestSumsRefresh();
  TestShutdown();  // last: the cancel flag stays set
  fs::remove_all(g_root, ec);
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
