// Host test for src/doa4_suits.cpp: an import folder against a fake game folder and the image's
// real costume tables, read from your own image (DOA4_TEST_IMAGE, doa4_test_image.h; no game byte is in this
// repository). Without the image the sections that need the tables print SKIPPED; the manifest and folder checks that fail
// before the tables are read run on an empty guest. Built and run by run_suits_test.bat. Optional real-archive check:
// DOA4_SUITS_GAME_DIR=<the DOA4 folder holding chrone.afs, chrtwo.afs, chrthree.afs> (read-only).
// Prints "suits: N checks run" before its verdict line: a run that ends a section early shows
// as a smaller N.

#include "../../src/doa4_suits.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../src/doa4_costumes.h"
#include "../../src/doa4_gamefiles.h"
#include "../../src/doa4_suits_picks.h"
#include "../../src/doa4_utf8.h"
#include "doa4_test_image.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int g_failures = 0;
static int g_checks = 0;  // every CHECK that ran: a mutant that skips or ends a section shows here
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::suits;
namespace fs = std::filesystem;

// The image's costume ranges, loaded once in main; null when the image is not there (FakeGuest is then empty).
static const doa4_test::CostumeImage* g_image = nullptr;

struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  doa4::GuestBytes guest{[this](uint32_t a) {
                           auto it = bytes.find(a);
                           return it == bytes.end() ? uint8_t(0) : it->second;
                         },
                         [this](uint32_t a, uint8_t v) { bytes[a] = v; }};
  FakeGuest() {
    if (!g_image) return;
    Load(doa4_test::kCostumeTablesRange.address, g_image->tables);
    Load(doa4_test::kCostumeRecordsRange.address, g_image->records);
    Load(doa4_test::kCostumeFilesRange.address, g_image->files);
  }
  void Load(uint32_t start, const std::vector<uint8_t>& data) {
    for (size_t i = 0; i < data.size(); ++i) bytes[start + uint32_t(i)] = data[i];
  }
};

static void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i));
}

// What the game's empty costume slots hold: sixteen 0x77 bytes (68 of the real archives' 16-byte
// entries; the 69th, chrone entry 0 = file 204, is 00 00 00 01 and zeros). Written here on its own
// so a changed constant in the module under test is a failure, not a shared mistake.
static constexpr uint8_t kFiller = 0x77;

// An AFS archive of `count` entries, every one a 16-byte placeholder (16 x kFiller) except those
// in `sizes` (whose bytes are zeros, apart from a 16-byte one, which is filled like the rest).
static std::vector<uint8_t> Archive(uint32_t count, const std::map<uint32_t, uint32_t>& sizes = {}) {
  const uint64_t first = (8 + 8ull * count + 8 + 2047) / 2048 * 2048;
  std::vector<uint64_t> offsets;
  uint64_t pos = first;
  for (uint32_t i = 0; i < count; ++i) {
    offsets.push_back(pos);
    const auto it = sizes.find(i);
    pos = (pos + (it == sizes.end() ? 16 : it->second) + 2047) / 2048 * 2048;
  }
  std::vector<uint8_t> a(size_t((pos + 48ull * count + 2047) / 2048 * 2048), 0);
  std::memcpy(a.data(), "AFS\0", 4);
  Put32(a, 4, count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto it = sizes.find(i);
    const uint32_t size = it == sizes.end() ? 16 : it->second;
    Put32(a, 8 + 8 * i, uint32_t(offsets[i]));
    Put32(a, 12 + 8 * i, size);
    if (size == 16) std::memset(a.data() + offsets[i], kFiller, 16);
  }
  Put32(a, 8 + 8 * count, uint32_t(pos));
  Put32(a, 12 + 8 * count, 48 * count);
  return a;
}

static void Write(const fs::path& p, const std::vector<uint8_t>& bytes) {
  std::ofstream(p, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
static void WriteText(const fs::path& p, const std::string& text) {
  std::ofstream(p, std::ios::binary).write(text.data(), std::streamsize(text.size()));
}
static std::vector<uint8_t> Content(int seed, size_t n) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = uint8_t(seed * 31 + i * 7);
  return v;
}
static std::string Sha(const std::vector<uint8_t>& v) {
  return doa4::gamefiles::Sha1Hex(v.data(), v.size());
}

// A reason or label by index, or "" past the end: a mutant that loads too few or too many suits
// must fail a check, not read past a vector and end the run.
static std::string SkippedAt(const ImportStatus& s, size_t i) {
  return i < s.skipped.size() ? s.skipped[i] : std::string();
}

// 0 when `p` opens for writing (nobody holds it); else the Win32 error (ERROR_SHARING_VIOLATION
// while the loader holds the file open without write sharing).
static DWORD WriteOpenError(const fs::path& p) {
  HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return GetLastError();
  CloseHandle(h);
  return 0;
}

static std::vector<uint8_t> ReadAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Overwrites byte `index` of entry `entry` of the fake archive file `p` (found through its TOC).
static void PokeEntry(const fs::path& p, uint32_t entry, size_t index, uint8_t value) {
  std::vector<uint8_t> bytes = ReadAll(p);
  const size_t toc_at = 8 + 8 * size_t(entry);
  if (toc_at + 4 > bytes.size()) return;
  const size_t at = size_t(bytes[toc_at]) | size_t(bytes[toc_at + 1]) << 8 | size_t(bytes[toc_at + 2]) << 16 |
                    size_t(bytes[toc_at + 3]) << 24;
  if (at + index >= bytes.size()) return;
  bytes[at + index] = value;
  Write(p, bytes);
}

// A folder's files by name: size and SHA-1 of the bytes. "Nothing is written" is a comparison of
// two of these, taken before and after.
static std::map<std::string, std::string> Snapshot(const fs::path& dir) {
  std::map<std::string, std::string> out;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (!e.is_regular_file()) continue;
    const std::vector<uint8_t> bytes = ReadAll(e.path());
    out[doa4::Utf8(e.path().filename())] = std::to_string(bytes.size()) + " " + Sha(bytes);
  }
  return out;
}

// Whether a loaded suit's source holds exactly `want`.
static bool SourceHolds(const std::shared_ptr<const doa4::afs::Source>& source,
                        const std::vector<uint8_t>& want) {
  if (!source || source->size() != want.size()) return false;
  std::vector<uint8_t> got(want.size());
  return source->Read(0, got.data(), got.size()) && got == want;
}

using Overlays = std::map<std::string, std::shared_ptr<const doa4::afs::Overlay>>;

// Whether `entry` of the combined `archive` has exactly `want` as its bytes.
static bool EntryHolds(const Overlays& overlays, const char* archive, uint32_t entry,
                       const std::vector<uint8_t>& want) {
  const auto it = overlays.find(archive);
  if (it == overlays.end() || !it->second) return false;
  const doa4::afs::Overlay& o = *it->second;
  if (o.entry_size(entry) != want.size()) return false;
  std::vector<uint8_t> got(want.size());
  return o.Read(o.entry_offset(entry), got.data(), got.size()) && got == want;
}

struct World {
  fs::path root, game, import;
  std::map<std::string, std::vector<uint8_t>> files;
  explicit World(const std::u8string& import_name = u8"doax2_suits",
                 const std::u8string& game_name = u8"game") {
    // One folder per process and per World: two runs at once, or a test that leaks a handle, must
    // not take another test's files with them.
    static int counter = 0;
    root = fs::temp_directory_path() /
           ("doa4_suits_test_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(++counter));
    std::error_code ec;
    fs::remove_all(root, ec);
    game = root / fs::path(game_name);
    import = root / fs::path(import_name);
    fs::create_directories(game);
    fs::create_directories(import);
    Write(game / "chrone.afs", Archive(73));
    Write(game / "chrtwo.afs", Archive(128));
    Write(game / "chrthree.afs", Archive(188));
  }
  // Every status that holds a file open is destroyed before its World, so the folder must go. A
  // handle the code under test leaked keeps a file undeletable: that shows here as a failed check
  // (and not as an exception out of a destructor).
  ~World() {
    std::error_code ec;
    fs::remove_all(root, ec);
    std::error_code ec2;
    CHECK(!ec && !fs::exists(root, ec2));
  }
  std::string LineSized(const std::string& head, const std::string& name, int seed, size_t tpr_n,
                        size_t cat_n) {
    files[name + ".tpr"] = Content(seed, tpr_n);
    files[name + ".cat"] = Content(seed + 1, cat_n);
    Write(import / (name + ".tpr"), files[name + ".tpr"]);
    Write(import / (name + ".cat"), files[name + ".cat"]);
    return head + " " + name + ".tpr " + std::to_string(tpr_n) + " " + Sha(files[name + ".tpr"]) + " " +
           name + ".cat " + std::to_string(cat_n) + " " + Sha(files[name + ".cat"]) + "\n";
  }
  std::string Line(const std::string& head, const std::string& name, int seed) {
    return LineSized(head, name, seed, 5000, 700);
  }
  // Kasumi in her own m7 (chrone 65/66) and Ayane appended on chrone 71/72.
  std::string TwoSuits() {
    return "doax2-suits 1\nconverter test\n" +
           Line("suit 5 own:7 269 270 509 510 vks_ch_d 0", "ka", 1) +
           Line("suit 13 append 275 276 543 544 vay_cs_b 0", "ay", 3);
  }
};

static void TestNoFolder() {
  World w;
  FakeGuest g;
  const ImportStatus s = LoadImport(w.root / "nothing", w.game, g.guest);
  CHECK(!s.folder && s.suits.empty());
  CHECK(s.Summary() == "DOAX2 swimsuits: none imported");
  CHECK(s.path == w.root / "nothing" && s.problem.empty() && s.fingerprint.empty());
}

// A regular file where the folder should be is not an import folder.
static void TestImportIsAFile() {
  World w;
  FakeGuest g;
  WriteText(w.root / "afile", "doax2-suits 1\n");
  const ImportStatus s = LoadImport(w.root / "afile", w.game, g.guest);
  CHECK(!s.folder && s.problem.empty() && s.Summary() == "DOAX2 swimsuits: none imported");
}

static void TestNoManifest() {
  World w;
  FakeGuest g;
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.folder && s.problem == "manifest.txt is missing or unreadable");
  CHECK(s.Summary() == "DOAX2 swimsuits: none loaded (manifest.txt is missing or unreadable)");
  CHECK(s.path == w.import && s.fingerprint.empty() && s.suits.empty());
}

static void TestBadManifest() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", "hello\n");
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.problem.rfind("manifest.txt: line 1: not a doax2-suits 1 manifest", 0) == 0);
  CHECK(s.folder && s.fingerprint.empty() && s.suits.empty() && s.skipped.empty());
}

// The read limit: a manifest of exactly kManifestLimit bytes is read (and here refused by the
// parser, with the parser's reason); one byte more is unreadable. An empty file is read too.
static void TestManifestSizeLimit() {
  World w;
  FakeGuest g;
  std::string text = "hello\n";
  text.resize(size_t(kManifestLimit), ' ');
  WriteText(w.import / "manifest.txt", text);
  {
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.problem.rfind("manifest.txt: line 1: not a doax2-suits 1 manifest", 0) == 0);
  }
  text.push_back(' ');
  WriteText(w.import / "manifest.txt", text);
  {
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.problem == "manifest.txt is missing or unreadable");
  }
  WriteText(w.import / "manifest.txt", "");
  {
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.problem == "manifest.txt: empty manifest");
  }
}

static void TestLoadsTwo() {
  World w;
  FakeGuest g;
  const std::string text = w.TwoSuits();
  WriteText(w.import / "manifest.txt", text);
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.problem.empty() && s.skipped.empty());
  CHECK(s.suits.size() == 2);
  CHECK(s.Summary() == "DOAX2 swimsuits: 2 loaded");
  CHECK(s.fingerprint == Sha(std::vector<uint8_t>(text.begin(), text.end())));  // already canonical
  CHECK(SuitsOf(s).size() == 2 && SuitsOf(s)[1].design == "vay_cs_b");
  CHECK(s.folder && s.path == w.import);
  // Manifest order, and each suit holds its own two files (different sizes and contents, so a
  // swapped tpr/cat or a swapped suit shows).
  CHECK(SuitsOf(s).size() == 2 && SuitsOf(s)[0].design == "vks_ch_d");
  CHECK(s.suits.size() == 2 && SourceHolds(s.suits[0].tpr, w.files["ka.tpr"]) &&
        SourceHolds(s.suits[0].cat, w.files["ka.cat"]) && SourceHolds(s.suits[1].tpr, w.files["ay.tpr"]) &&
        SourceHolds(s.suits[1].cat, w.files["ay.cat"]));
}

// The fingerprint is the SHA-1 of the CANONICAL text (spec: it keys the saved state), so the same
// manifest with a BOM, CRLF line ends, comments and blank lines has the same fingerprint, and it
// is not the SHA-1 of the file's bytes.
static void TestFingerprintIsCanonical() {
  World w;
  FakeGuest g;
  const std::string canonical = w.TwoSuits();
  std::string raw = "\xEF\xBB\xBF# written by hand\r\n\r\n";
  for (char c : canonical) {
    if (c == '\n') raw += '\r';
    raw += c;
  }
  raw += "# trailing comment\r\n";
  WriteText(w.import / "manifest.txt", raw);
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 2);
  CHECK(s.fingerprint == Sha(std::vector<uint8_t>(canonical.begin(), canonical.end())));
  CHECK(s.fingerprint != Sha(std::vector<uint8_t>(raw.begin(), raw.end())));
}

// Known-answer SHA-1 vectors (FIPS 180: "abc" and the 56-byte message), and three sizes around
// the 1 MiB read chunk with SHA-1s computed by Python's hashlib, so the loader's chunked hashing
// is checked against values that did not come from the code under test.
static void TestKnownChecksums() {
  World w;
  FakeGuest g;
  const std::string abc = "abc";
  const std::string longer = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  WriteText(w.import / "k.tpr", abc);
  WriteText(w.import / "k.cat", longer);
  WriteText(w.import / "manifest.txt",
            "doax2-suits 1\nconverter test\n"
            "suit 5 own:7 269 270 509 510 vks_ch_d 0 k.tpr 3 a9993e364706816aba3e25717850c26c9cd0d89d "
            "k.cat 56 84983e441c3bd26ebaae4aa1f95129e5e54670f1\n");
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 1 && s.skipped.empty());
}

static void TestBigFiles() {
  struct Big {
    int seed;
    size_t n;
    const char* sha1;
  };
  const Big bigs[] = {{4, 1048576, "6f0b81475fb660da5c2dd138f031681028499ce5"},    // exactly one chunk
                      {6, 1048577, "270c4118f45caca6a2d9c4ebf7233f9b7ee73bb4"},    // one chunk and a byte
                      {2, 2097169, "deaec3d6c8552e642584ce50fcb18e2071e2020c"}};  // two chunks and 17 bytes
  for (const Big& b : bigs) {
    World w;
    FakeGuest g;
    w.files["big.tpr"] = Content(b.seed, b.n);
    w.files["big.cat"] = Content(b.seed + 1, 700);
    Write(w.import / "big.tpr", w.files["big.tpr"]);
    Write(w.import / "big.cat", w.files["big.cat"]);
    WriteText(w.import / "manifest.txt",
              "doax2-suits 1\nconverter test\nsuit 5 own:7 269 270 509 510 vks_ch_d 0 big.tpr " +
                  std::to_string(b.n) + " " + b.sha1 + " big.cat 700 " + Sha(w.files["big.cat"]) + "\n");
    {
      const ImportStatus s = LoadImport(w.import, w.game, g.guest);
      CHECK(s.suits.size() == 1 && s.skipped.empty());
      CHECK(s.suits.size() == 1 && SourceHolds(s.suits[0].tpr, w.files["big.tpr"]));
    }
    std::vector<uint8_t> changed = w.files["big.tpr"];
    changed.back() ^= 1;  // the very last byte: only a hash over every chunk sees it
    Write(w.import / "big.tpr", changed);
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.suits.empty() && s.skipped.size() == 1 &&
          SkippedAt(s, 0) == "vks_ch_d c0: big.tpr does not match its checksum");
  }
}

static void TestChecksumMismatch() {
  World w;
  FakeGuest g;
  const std::string text = w.TwoSuits();
  WriteText(w.import / "manifest.txt", text);
  std::vector<uint8_t> changed = w.files["ay.tpr"];
  changed[100] ^= 1;
  Write(w.import / "ay.tpr", changed);
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 1 && s.suits[0].suit.design == "vks_ch_d");
  CHECK(s.skipped.size() == 1 && s.skipped[0] == "vay_cs_b c0: ay.tpr does not match its checksum");
  CHECK(s.Summary() == "DOAX2 swimsuits: 1 loaded, 1 skipped");
  // The fingerprint is of the manifest, not of what loaded.
  CHECK(s.fingerprint == Sha(std::vector<uint8_t>(text.begin(), text.end())));
  CHECK(WriteOpenError(w.import / "ay.tpr") == 0);  // a suit left out holds nothing open
}

// Every way a file can be wrong is its own reason, and the .tpr is looked at before the .cat.
static void TestFileReasons() {
  World w;
  FakeGuest g;
  const std::string text = w.TwoSuits();
  WriteText(w.import / "manifest.txt", text);
  {  // a bit flipped in the .cat only
    std::vector<uint8_t> changed = w.files["ay.cat"];
    changed[10] ^= 1;
    Write(w.import / "ay.cat", changed);
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.suits.size() == 1 && s.skipped.size() == 1 &&
          SkippedAt(s, 0) == "vay_cs_b c0: ay.cat does not match its checksum");
    Write(w.import / "ay.cat", w.files["ay.cat"]);
  }
  {  // control: the restored files load again
    const ImportStatus again = LoadImport(w.import, w.game, g.guest);
    CHECK(again.suits.size() == 2 && again.skipped.empty());
  }
  {  // longer than the manifest says (the other direction from 4999)
    Write(w.import / "ay.tpr", Content(9, 5001));
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.suits.size() == 1 && s.skipped.size() == 1 &&
          SkippedAt(s, 0) == "vay_cs_b c0: ay.tpr is 5001 bytes, the manifest says 5000");
    Write(w.import / "ay.tpr", w.files["ay.tpr"]);
  }
  {  // the .cat the wrong size too (shorter)
    Write(w.import / "ay.cat", Content(9, 699));
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.suits.size() == 1 && s.skipped.size() == 1 &&
          SkippedAt(s, 0) == "vay_cs_b c0: ay.cat is 699 bytes, the manifest says 700");
    Write(w.import / "ay.cat", w.files["ay.cat"]);
  }
  {  // both files bad: the .tpr is named, and only it
    Write(w.import / "ka.tpr", Content(9, 4999));
    fs::remove(w.import / "ka.cat");
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.suits.size() == 1 && s.skipped.size() == 1 &&
          SkippedAt(s, 0) == "vks_ch_d c0: ka.tpr is 4999 bytes, the manifest says 5000");
  }
}

static void TestMissingAndWrongSize() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  fs::remove(w.import / "ka.cat");
  Write(w.import / "ay.tpr", Content(9, 4999));
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.empty() && s.skipped.size() == 2);
  CHECK(SkippedAt(s, 0) == "vks_ch_d c0: ka.cat is missing or in use");
  CHECK(SkippedAt(s, 1) == "vay_cs_b c0: ay.tpr is 4999 bytes, the manifest says 5000");
  CHECK(s.Summary() == "DOAX2 swimsuits: 0 loaded, 2 skipped");
  // ka.tpr was opened and checked before ka.cat turned out to be missing: it is let go.
  CHECK(WriteOpenError(w.import / "ka.tpr") == 0);
}

// A file another program holds open for writing cannot be read safely: the suit is left out.
static void TestFileInUse() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  HANDLE h = CreateFileW((w.import / "ay.tpr").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(h != INVALID_HANDLE_VALUE);
  {
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.suits.size() == 1 && s.skipped.size() == 1 &&
          SkippedAt(s, 0) == "vay_cs_b c0: ay.tpr is missing or in use");
  }
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static void TestNotAPlaceholder() {
  World w;
  FakeGuest g;
  Write(w.game / "chrone.afs", Archive(73, {{66, 100}}));
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 1 && s.suits[0].suit.design == "vay_cs_b");
  CHECK(s.skipped.size() == 1 &&
        SkippedAt(s, 0) ==
            "vks_ch_d c0: file 270 (entry 66 of chrone.afs) is not an empty costume slot (100 bytes)");
}

// The placeholder rule is "exactly 16 bytes", on BOTH files of the pair, and the first bad file
// is the one named.
static void TestPlaceholderEdges() {
  const auto Run = [](const std::map<uint32_t, uint32_t>& sizes, size_t* loaded, std::string* reason) {
    World w;
    FakeGuest g;
    Write(w.game / "chrone.afs", Archive(73, sizes));
    WriteText(w.import / "manifest.txt", w.TwoSuits());
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    *loaded = s.suits.size();
    *reason = s.skipped.size() == 1 ? s.skipped[0] : "<" + std::to_string(s.skipped.size()) + " skipped>";
  };
  size_t loaded = 0;
  std::string reason;
  Run({{65, 5000}}, &loaded, &reason);  // the .tpr slot of Kasumi's pair (the brief's case is the .cat)
  CHECK(loaded == 1 && reason == "vks_ch_d c0: file 269 (entry 65 of chrone.afs) is not an empty costume slot (5000 bytes)");
  Run({{66, 15}}, &loaded, &reason);  // one byte under
  CHECK(loaded == 1 && reason == "vks_ch_d c0: file 270 (entry 66 of chrone.afs) is not an empty costume slot (15 bytes)");
  Run({{66, 17}}, &loaded, &reason);  // one byte over
  CHECK(loaded == 1 && reason == "vks_ch_d c0: file 270 (entry 66 of chrone.afs) is not an empty costume slot (17 bytes)");
  Run({{65, 17}, {66, 17}}, &loaded, &reason);  // both bad: the first is named
  CHECK(loaded == 1 && reason == "vks_ch_d c0: file 269 (entry 65 of chrone.afs) is not an empty costume slot (17 bytes)");
  Run({{71, 100}}, &loaded, &reason);  // the second suit's slot: the first still loads
  CHECK(loaded == 1 && reason == "vay_cs_b c0: file 275 (entry 71 of chrone.afs) is not an empty costume slot (100 bytes)");
  Run({}, &loaded, &reason);  // control: all 16 bytes loads both
  CHECK(loaded == 2 && reason == "<0 skipped>");
}

// A file number whose entry is past the end of the archive the game has.
static void TestEntryBeyondArchive() {
  const auto Run = [](uint32_t count, std::string* reason) {
    World w;
    FakeGuest g;
    Write(w.game / "chrone.afs", Archive(count));
    WriteText(w.import / "manifest.txt", w.TwoSuits());
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    *reason = s.skipped.empty() ? std::string("<none skipped>") : s.skipped[0];
  };
  std::string reason;
  Run(65, &reason);  // entry 65 == the count: the first file is already outside
  CHECK(reason == "vks_ch_d c0: file 269 is entry 65 of chrone.afs, which has 65");
  Run(66, &reason);  // the .tpr (65) is inside, the .cat (66) is the first one outside
  CHECK(reason == "vks_ch_d c0: file 270 is entry 66 of chrone.afs, which has 66");
  Run(67, &reason);  // both inside: Kasumi loads; Ayane's pair (71/72) is outside
  CHECK(reason == "vay_cs_b c0: file 275 is entry 71 of chrone.afs, which has 67");
}

static void TestArchiveMissing() {
  World w;
  FakeGuest g;
  fs::remove(w.game / "chrtwo.afs");
  const std::string text = "doax2-suits 1\nconverter test\n" +
                           w.Line("suit 15 append 355 356 545 546 vls_cs_h 0", "li", 5);
  WriteText(w.import / "manifest.txt", text);
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.empty() && s.skipped.size() == 1 &&
        SkippedAt(s, 0) == "vls_cs_h c0: chrtwo.afs is missing or not an AFS archive");
  // Present but not an archive: garbage, and too small to hold a header.
  Write(w.game / "chrtwo.afs", Content(3, 100));
  const ImportStatus garbage = LoadImport(w.import, w.game, g.guest);
  CHECK(garbage.suits.empty() && garbage.skipped.size() == 1 &&
        SkippedAt(garbage, 0) == "vls_cs_h c0: chrtwo.afs is missing or not an AFS archive");
  Write(w.game / "chrtwo.afs", Content(3, 10));
  const ImportStatus tiny = LoadImport(w.import, w.game, g.guest);
  CHECK(tiny.suits.empty() && tiny.skipped.size() == 1 &&
        SkippedAt(tiny, 0) == "vls_cs_h c0: chrtwo.afs is missing or not an AFS archive");
  // Control: a good archive in its place loads the suit.
  Write(w.game / "chrtwo.afs", Archive(128));
  const ImportStatus good = LoadImport(w.import, w.game, g.guest);
  CHECK(good.suits.size() == 1 && good.skipped.empty());
}

static void TestTableReasonPropagates() {
  World w;
  FakeGuest g;
  const std::string text = "doax2-suits 1\nconverter test\n" +
                           w.Line("suit 5 own:7 271 272 509 510 vks_cs_a 0", "kw", 7);
  WriteText(w.import / "manifest.txt", text);
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.skipped.size() == 1 &&
        SkippedAt(s, 0) == "vks_cs_a c0: model record m7 holds files 269/270, not the suit's 271/272");
}

// The game's tables are asked first: a suit they refuse is refused for that reason even when its
// slots are not empty and its files are gone, and nothing about its files is looked at.
static void TestTableReasonComesFirst() {
  World w;
  FakeGuest g;
  Write(w.game / "chrone.afs", Archive(73, {{67, 100}, {68, 100}}));
  const std::string text = "doax2-suits 1\nconverter test\n" +
                           w.Line("suit 5 own:7 271 272 509 510 vks_cs_a 0", "kw", 7);
  WriteText(w.import / "manifest.txt", text);
  fs::remove(w.import / "kw.tpr");
  fs::remove(w.import / "kw.cat");
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.empty() && s.skipped.size() == 1 &&
        SkippedAt(s, 0) == "vks_cs_a c0: model record m7 holds files 269/270, not the suit's 271/272");
  // The same suit with its table right (own:7 is 269/270) and the slots not empty is refused for
  // the slots, and files that are gone do not change that (slots are looked at before files).
  Write(w.game / "chrone.afs", Archive(73, {{65, 100}}));
  const std::string fixed = "doax2-suits 1\nconverter test\n"
                            "suit 5 own:7 269 270 509 510 vks_cs_a 0 kw.tpr 5000 " +
                            Sha(w.files["kw.tpr"]) + " kw.cat 700 " + Sha(w.files["kw.cat"]) + "\n";
  WriteText(w.import / "manifest.txt", fixed);
  const ImportStatus t = LoadImport(w.import, w.game, g.guest);
  CHECK(t.suits.empty() && t.skipped.size() == 1 &&
        SkippedAt(t, 0) == "vks_cs_a c0: file 269 (entry 65 of chrone.afs) is not an empty costume slot (100 bytes)");
}

static void TestUnicodeFolder() {
  // UTF-8 bytes in hex escapes, split so a hex escape ends where the next letter starts:
  // "Donn" e-acute "es_" and two CJK characters.
  World w(u8"Donn\xC3\xA9" u8"es_\xE6\x97\xA5\xE6\x9C\xAC");
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 2);
}

// The game folder can be somewhere with such a name too.
static void TestUnicodeGameFolder() {
  World w(u8"doax2_suits", u8"G\xC3\xA4" u8"me_\xE6\x97\xA5\xE6\x9C\xAC");
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 2 && s.skipped.empty());
  std::string error;
  const auto overlays = BuildOverlays(s, w.game, g.guest, &error);
  CHECK(error.empty() && overlays.size() == 1 && EntryHolds(overlays, "chrone.afs", 65, w.files["ka.tpr"]));
}

static void TestBuildOverlays() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  const auto game_before = Snapshot(w.game);
  const auto import_before = Snapshot(w.import);
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  std::string error;
  const auto overlays = BuildOverlays(s, w.game, g.guest, &error);
  CHECK(error.empty() && overlays.size() == 1 && overlays.count("chrone.afs"));
  if (!overlays.count("chrone.afs")) return;
  const doa4::afs::Overlay& o = *overlays.at("chrone.afs");
  CHECK(o.entry_size(65) == 5000 && o.entry_size(66) == 700 && o.entry_size(71) == 5000);
  std::vector<uint8_t> got(5000);
  CHECK(o.Read(o.entry_offset(65), got.data(), got.size()) && got == w.files["ka.tpr"]);
  CHECK(o.Read(o.entry_offset(71), got.data(), got.size()) && got == w.files["ay.tpr"]);
  CHECK(o.entry_offset(66) % 2048 == 0 && o.entry_offset(72) % 2048 == 0);
  // All four replaced entries, byte for byte, and an entry nobody replaced still its 16 bytes.
  CHECK(EntryHolds(overlays, "chrone.afs", 66, w.files["ka.cat"]));
  CHECK(EntryHolds(overlays, "chrone.afs", 72, w.files["ay.cat"]));
  CHECK(o.entry_size(64) == 16 && o.entry_size(67) == 16 && o.entry_size(72) == 700);
  const auto labels = EntryLabels(s, g.guest);
  const auto ArchiveLabels = [&](const char* name) {
    const auto it = labels.find(name);
    return it == labels.end() ? std::map<uint32_t, std::string>() : it->second;
  };
  const auto LabelAt = [&](const char* name, uint32_t entry) {
    const auto m = ArchiveLabels(name);
    const auto it = m.find(entry);
    return it == m.end() ? std::string("<none>") : it->second;
  };
  CHECK(LabelAt("chrone.afs", 65) == "vks_ch_d c0 (tpr)");
  CHECK(LabelAt("chrone.afs", 72) == "vay_cs_b c0 (cat)");
  // The whole map: no other archive, no other entry.
  const std::map<std::string, std::map<uint32_t, std::string>> want = {
      {"chrone.afs",
       {{65, "vks_ch_d c0 (tpr)"}, {66, "vks_ch_d c0 (cat)"}, {71, "vay_cs_b c0 (tpr)"}, {72, "vay_cs_b c0 (cat)"}}}};
  CHECK(labels == want);
  // Spec 3.1 / 3.2: nothing is written to the game folder, and the import folder is only read.
  CHECK(!game_before.empty() && Snapshot(w.game) == game_before);
  CHECK(!import_before.empty() && Snapshot(w.import) == import_before);
}

// Suits in all three archives, and a pair that straddles two: the combined archives are grouped
// by the archive each FILE is in, one overlay each, every replaced entry right.
static void TestThreeArchives() {
  World w;
  FakeGuest g;
  const std::string text = "doax2-suits 1\nconverter test\n" +
                           w.Line("suit 5 own:7 269 270 509 510 vks_ch_d 0", "ka", 1) +    // chrone 65/66
                           w.Line("suit 15 append 355 356 545 546 vls_cs_h 0", "li", 3) +   // chrtwo 78/79
                           w.Line("suit 13 append 547 548 543 544 vay_cs_c 0", "ax", 5) +   // chrthree 142/143
                           w.Line("suit 1 append 276 277 - - vxx_cs_a 0", "xa", 7);          // chrone 72 + chrtwo 0
  WriteText(w.import / "manifest.txt", text);
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 4 && s.skipped.empty());
  {  // the overlays hold the game's archives open, so they go before an archive is removed
    std::string error;
    const auto overlays = BuildOverlays(s, w.game, g.guest, &error);
    CHECK(error.empty() && overlays.size() == 3);
    CHECK(EntryHolds(overlays, "chrone.afs", 65, w.files["ka.tpr"]));
    CHECK(EntryHolds(overlays, "chrone.afs", 66, w.files["ka.cat"]));
    CHECK(EntryHolds(overlays, "chrone.afs", 72, w.files["xa.tpr"]));
    CHECK(EntryHolds(overlays, "chrtwo.afs", 0, w.files["xa.cat"]));
    CHECK(EntryHolds(overlays, "chrtwo.afs", 78, w.files["li.tpr"]));
    CHECK(EntryHolds(overlays, "chrtwo.afs", 79, w.files["li.cat"]));
    CHECK(EntryHolds(overlays, "chrthree.afs", 142, w.files["ax.tpr"]));
    CHECK(EntryHolds(overlays, "chrthree.afs", 143, w.files["ax.cat"]));
  }
  const std::map<std::string, std::map<uint32_t, std::string>> want = {
      {"chrone.afs", {{65, "vks_ch_d c0 (tpr)"}, {66, "vks_ch_d c0 (cat)"}, {72, "vxx_cs_a c0 (tpr)"}}},
      {"chrtwo.afs", {{0, "vxx_cs_a c0 (cat)"}, {78, "vls_cs_h c0 (tpr)"}, {79, "vls_cs_h c0 (cat)"}}},
      {"chrthree.afs", {{142, "vay_cs_c c0 (tpr)"}, {143, "vay_cs_c c0 (cat)"}}}};
  CHECK(EntryLabels(s, g.guest) == want);
  // SuitsOf keeps the manifest order of all four.
  const std::vector<Suit> suits = SuitsOf(s);
  CHECK(suits.size() == 4 && suits[0].design == "vks_ch_d" && suits[1].design == "vls_cs_h" &&
        suits[2].design == "vay_cs_c" && suits[3].design == "vxx_cs_a");

  // One archive that cannot be opened ends the whole build: no partial map.
  std::error_code ec;
  fs::remove(w.game / "chrtwo.afs", ec);
  CHECK(!ec);
  std::string error2;
  const auto none = BuildOverlays(s, w.game, g.guest, &error2);
  CHECK(none.empty() && error2 == "cannot open chrtwo.afs in the game folder");
}

static void TestBuildOverlaysEdges() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  {  // nothing loaded: nothing to build, nothing wrong
    ImportStatus empty;
    std::string error;
    const auto overlays = BuildOverlays(empty, w.game, g.guest, &error);
    CHECK(overlays.empty() && error.empty());
    CHECK(EntryLabels(empty, g.guest).empty() && SuitsOf(empty).empty());
  }
  {  // the game's archive is not an archive: the reason names the file and what is wrong with it
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.suits.size() == 2);
    Write(w.game / "chrone.afs", Content(3, 100));
    std::string error;
    const auto overlays = BuildOverlays(s, w.game, g.guest, &error);
    CHECK(overlays.empty() && error == "chrone.afs: not an AFS archive");
    fs::remove(w.game / "chrone.afs");
    std::string error2;
    const auto gone = BuildOverlays(s, w.game, g.guest, &error2);
    CHECK(gone.empty() && error2 == "cannot open chrone.afs in the game folder");
  }
  {  // a loaded suit whose files the table does not place in a character archive (the tables
     // cannot change under LoadImport, so this is built by hand): either of the two files
    std::string error;
    ImportStatus hand;
    LoadedSuit l;
    l.suit.design = "vks_ch_d";
    l.suit.body_tpr = 0;  // the image's file 0 is not in chrone/chrtwo/chrthree
    l.suit.body_cat = 269;
    hand.suits.push_back(l);
    CHECK(!doa4::costumes::FileLocation(g.guest, 0).has_value());
    auto overlays = BuildOverlays(hand, w.game, g.guest, &error);
    CHECK(overlays.empty() && error == "vks_ch_d c0: its files are not character archive files");
    hand.suits[0].suit.body_tpr = 269;
    hand.suits[0].suit.body_cat = 0;
    error.clear();
    overlays = BuildOverlays(hand, w.game, g.guest, &error);
    CHECK(overlays.empty() && error == "vks_ch_d c0: its files are not character archive files");
  }
}

// What Fail does: the reason is kept, the suits go, and so do the files they held.
static void TestFailDropsSuits() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 2 && s.problem.empty());
  CHECK(WriteOpenError(w.import / "ka.tpr") == ERROR_SHARING_VIOLATION);
  s.Fail("the game tables do not fit");
  CHECK(s.problem == "the game tables do not fit" && s.suits.empty() && SuitsOf(s).empty());
  CHECK(s.Summary() == "DOAX2 swimsuits: none loaded (the game tables do not fit)");
  CHECK(s.folder);
  CHECK(WriteOpenError(w.import / "ka.tpr") == 0 && WriteOpenError(w.import / "ay.cat") == 0);
}

// The files of a loaded suit stay open without write sharing while the status lives; they are
// let go with it. The positive control (the same open succeeds afterwards) shows the refusal is
// the loader's hold and not something else about the file.
static void TestFileHeldOpen() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  CHECK(WriteOpenError(w.import / "ka.tpr") == 0);  // before: free
  {
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    CHECK(s.suits.size() == 2);
    HANDLE h = CreateFileW((w.import / "ka.tpr").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(h == INVALID_HANDLE_VALUE);  // a checked file cannot be rewritten while loaded
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK(WriteOpenError(w.import / "ka.tpr") == ERROR_SHARING_VIOLATION);
    CHECK(WriteOpenError(w.import / "ka.cat") == ERROR_SHARING_VIOLATION);
    CHECK(WriteOpenError(w.import / "ay.tpr") == ERROR_SHARING_VIOLATION);
    CHECK(WriteOpenError(w.import / "ay.cat") == ERROR_SHARING_VIOLATION);
  }
  CHECK(WriteOpenError(w.import / "ka.tpr") == 0 && WriteOpenError(w.import / "ay.cat") == 0);  // after
}

// An empty slot is sixteen 0x77 bytes, not just sixteen bytes: any other 16-byte entry is data
// (the real chrone entry 0, file 204, is 00 00 00 01 and zeros). Every byte position of the slot,
// with two different wrong values (0x00 and 0x77 + 1, so neither a "below" nor an "above" test of
// the byte passes), then the data shapes the real archive has and the old fakes had.
static void TestPlaceholderContent() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  const fs::path archive = w.game / "chrone.afs";
  const std::string tpr65 =
      "vks_ch_d c0: file 269 (entry 65 of chrone.afs) is not an empty costume slot (it holds data)";
  const std::string cat66 =
      "vks_ch_d c0: file 270 (entry 66 of chrone.afs) is not an empty costume slot (it holds data)";
  const std::string tpr71 =
      "vay_cs_b c0: file 275 (entry 71 of chrone.afs) is not an empty costume slot (it holds data)";
  struct Result {
    size_t loaded;
    size_t skipped;
    std::string first;
  };
  const auto Load = [&]() {
    const ImportStatus s = LoadImport(w.import, w.game, g.guest);
    return Result{s.suits.size(), s.skipped.size(), SkippedAt(s, 0)};
  };
  const auto Fresh = [&]() { Write(archive, Archive(73)); };
  Fresh();
  {  // control: the fake's slots are what the real ones are, and both suits load
    const Result r = Load();
    CHECK(r.loaded == 2 && r.skipped == 0);
  }
  for (size_t at = 0; at < 16; ++at) {
    for (uint8_t v : {uint8_t(0x00), uint8_t(kFiller + 1)}) {
      Fresh();
      PokeEntry(archive, 65, at, v);
      const Result r = Load();
      CHECK(r.loaded == 1 && r.skipped == 1 && r.first == tpr65);
    }
  }
  Fresh();  // the .cat slot of the same pair, first and last byte
  PokeEntry(archive, 66, 0, 0x00);
  {
    const Result r = Load();
    CHECK(r.loaded == 1 && r.skipped == 1 && r.first == cat66);
  }
  Fresh();
  PokeEntry(archive, 66, 15, 0x00);
  {
    const Result r = Load();
    CHECK(r.loaded == 1 && r.skipped == 1 && r.first == cat66);
  }
  Fresh();  // all zeros: what the fakes of the first version held
  for (size_t at = 0; at < 16; ++at) PokeEntry(archive, 65, at, 0x00);
  {
    const Result r = Load();
    CHECK(r.loaded == 1 && r.skipped == 1 && r.first == tpr65);
  }
  Fresh();  // 00 00 00 01 and zeros: the real chrone entry 0
  for (size_t at = 0; at < 16; ++at) PokeEntry(archive, 65, at, at == 3 ? 0x01 : 0x00);
  {
    const Result r = Load();
    CHECK(r.loaded == 1 && r.skipped == 1 && r.first == tpr65);
  }
  Fresh();  // both slots of the pair hold data: the first file is the one named
  PokeEntry(archive, 65, 4, 0x00);
  PokeEntry(archive, 66, 4, 0x00);
  {
    const Result r = Load();
    CHECK(r.loaded == 1 && r.skipped == 1 && r.first == tpr65);
  }
  Fresh();  // the second suit's slot: the first suit still loads
  PokeEntry(archive, 71, 9, 0x00);
  {
    const Result r = Load();
    CHECK(r.loaded == 1 && r.skipped == 1 && r.first == tpr71);
  }
}

// BuildOverlays opens the game's archive again after LoadImport checked it, so what it is about
// to replace is checked on that open, entry by entry in replacement order (Kasumi's 65 and 66,
// then Ayane's 71 and 72), and the reason names the archive and the first entry that changed.
static void TestOverlayRecheck() {
  World w;
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  CHECK(s.suits.size() == 2);
  const fs::path archive = w.game / "chrone.afs";
  struct Built {
    size_t overlays;
    std::string error;
  };
  // The overlays hold the archive open, so they are gone before the next rewrite.
  const auto Build = [&]() {
    std::string error;
    const auto overlays = BuildOverlays(s, w.game, g.guest, &error);
    return Built{overlays.size(), error};
  };
  const auto Changed = [](uint32_t entry) {
    return "chrone.afs changed since the import was checked (entry " + std::to_string(entry) + ")";
  };
  {  // control: nothing changed
    const Built b = Build();
    CHECK(b.overlays == 1 && b.error.empty());
  }
  Write(archive, Archive(73, {{66, 100}}));  // a replaced entry grew
  {
    const Built b = Build();
    CHECK(b.overlays == 0 && b.error == Changed(66));
  }
  Write(archive, Archive(73));  // a replaced entry's bytes changed
  PokeEntry(archive, 65, 9, 0x00);
  {
    const Built b = Build();
    CHECK(b.overlays == 0 && b.error == Changed(65));
  }
  Write(archive, Archive(73));  // only the last replaced entry changed
  PokeEntry(archive, 72, 15, 0x00);
  {
    const Built b = Build();
    CHECK(b.overlays == 0 && b.error == Changed(72));
  }
  Write(archive, Archive(73, {{66, 100}, {72, 100}}));  // two changed: the first in replacement order
  {
    const Built b = Build();
    CHECK(b.overlays == 0 && b.error == Changed(66));
  }
  Write(archive, Archive(60));  // fewer entries than the one it replaces
  {
    const Built b = Build();
    CHECK(b.overlays == 0 && b.error == Changed(65));
  }
  // Entries it does not replace are not its business, however odd they are.
  Write(archive, Archive(73, {{3, 5000}}));
  PokeEntry(archive, 4, 0, 0x00);
  {
    const Built b = Build();
    CHECK(b.overlays == 1 && b.error.empty());
  }
}

// ---- Fix round 1 (F1): MarkEmptySlots, the load-request guard's empty-slot marks ----

using Marks = std::set<std::pair<uint32_t, uint32_t>>;     // (character, model)
using Slots = std::set<std::pair<std::string, uint32_t>>;  // (archive name, entry)

static Marks MarksOf(const doa4::costumes::GuardState& s) {
  Marks out;
  for (uint32_t c = 0; c < doa4::costumes::kCharCount; ++c) {
    for (uint32_t m = 0; m < 256; ++m) {
      if (s.empty_slot[c].contains(m)) out.insert({c, m});
    }
  }
  return out;
}

// An independent reading of the rule on the guest's tables, written from the table layout and not
// from the module: model m (below `counts`) of character c is marked when its body .tpr or its
// body .cat is a file of one of the three character archives (file table 0x82C3B2E8: {u8 archive
// 3/4/5, u8, be16 entry}) whose entry is in `empty`. Records: be32 at 0x82C5E738 + 8c + 4, 8 bytes
// per model {be16 body tpr, be16 body cat, ...}.
static Marks Expected(const FakeGuest& g, const Slots& empty, const doa4::costumes::GuardState& counts) {
  const auto b8 = [&](uint32_t a) {
    const auto it = g.bytes.find(a);
    return it == g.bytes.end() ? 0u : unsigned(it->second);
  };
  const auto be16 = [&](uint32_t a) { return (b8(a) << 8) | b8(a + 1); };
  const auto be32 = [&](uint32_t a) { return (be16(a) << 16) | be16(a + 2); };
  const auto in_empty = [&](uint32_t file) {
    if (file >= 1640) return false;
    const uint32_t at = 0x82C3B2E8 + 4 * file;
    const char* name = b8(at) == 3 ? "chrone.afs" : b8(at) == 4 ? "chrtwo.afs" : b8(at) == 5 ? "chrthree.afs" : nullptr;
    return name != nullptr && empty.count({name, be16(at + 2)}) != 0;
  };
  Marks out;
  for (uint32_t c = 0; c < 25; ++c) {
    const uint32_t records = be32(0x82C5E738 + 8 * c + 4);
    for (uint32_t m = 0; m < counts.records[c] && m < 256; ++m) {
      if (in_empty(be16(records + 8 * m)) || in_empty(be16(records + 8 * m + 2))) out.insert({c, m});
    }
  }
  return out;
}
static Marks Expected(const FakeGuest& g, const Slots& empty) {
  return Expected(g, empty, doa4::costumes::ReadGuardState(g.guest));
}

// An archive of `count` entries: every entry is 32 bytes of data, except those in `empty` (empty
// slots: 16 x 0x77) and those in `data16` (16 bytes, which the caller then pokes to non-0x77).
static std::vector<uint8_t> SlotArchive(uint32_t count, const std::set<uint32_t>& empty,
                                        const std::set<uint32_t>& data16 = {}) {
  std::map<uint32_t, uint32_t> sizes;
  for (uint32_t i = 0; i < count; ++i) {
    if (!empty.count(i) && !data16.count(i)) sizes[i] = 32;
  }
  return Archive(count, sizes);
}

// The game folder's three archives with exactly the entries of `empty` as empty slots (every
// other entry data); entry counts as in World's archives unless given.
static void WriteSlotArchives(World& w, const Slots& empty, uint32_t one = 73, uint32_t two = 128,
                              uint32_t three = 188, const std::set<uint32_t>& one_data16 = {}) {
  std::set<uint32_t> e1, e2, e3;
  for (const auto& [name, entry] : empty) {
    (name == "chrone.afs" ? e1 : name == "chrtwo.afs" ? e2 : e3).insert(entry);
  }
  Write(w.game / "chrone.afs", SlotArchive(one, e1, one_data16));
  Write(w.game / "chrtwo.afs", SlotArchive(two, e2));
  Write(w.game / "chrthree.afs", SlotArchive(three, e3));
}

static Slots AllEntries(uint32_t one, uint32_t two, uint32_t three) {
  Slots all;
  for (uint32_t i = 0; i < one; ++i) all.insert({"chrone.afs", i});
  for (uint32_t i = 0; i < two; ++i) all.insert({"chrtwo.afs", i});
  for (uint32_t i = 0; i < three; ++i) all.insert({"chrthree.afs", i});
  return all;
}

struct Marked {
  Marks marks;
  std::string note;
};
static Marked Mark(const fs::path& game, const FakeGuest& g, const Overlays& overlays = {}) {
  doa4::costumes::GuardState s = doa4::costumes::ReadGuardState(g.guest);
  Marked out;
  out.note = MarkEmptySlots(&s, g.guest, game, overlays);
  out.marks = MarksOf(s);
  return out;
}

static std::shared_ptr<const doa4::afs::Overlay> MakeOverlay(const fs::path& game, const char* name,
                                                             std::vector<doa4::afs::Replacement> replacements) {
  std::string error;
  const auto original = doa4::afs::FileSource::Open(game / name);
  std::shared_ptr<const doa4::afs::Overlay> o =
      original ? doa4::afs::Overlay::Build(original, std::move(replacements), &error) : nullptr;
  CHECK(o != nullptr && error.empty());
  return o;
}

static std::shared_ptr<const doa4::afs::Source> Bytes(std::vector<uint8_t> v) {
  return std::make_shared<doa4::afs::MemorySource>(std::move(v));
}

// Which entries are empty slots decides it, per FILE and per ARCHIVE: a .tpr-only slot, a
// .cat-only slot, a file shared by several models, a 16-byte entry that is data, an empty entry of
// the same number in ANOTHER archive (decoy), and a size-32 entry.
static void TestMarkSingleFiles() {
  World w;
  FakeGuest g;
  // chrone 65 = Kasumi m7's .tpr; 52 = the .cat of m0 and m10 (they share body 255/256 = 51/52);
  // chrtwo 127 = Ayane m5's .cat; chrthree 14 = the .tpr of Lisa m2 and m7 (shared 419/420);
  // chrtwo 53 is the DECOY: Kasumi m1's .tpr is chrone 53, which is data here.
  const Slots empty = {{"chrone.afs", 65}, {"chrone.afs", 52}, {"chrtwo.afs", 127},
                       {"chrtwo.afs", 53}, {"chrthree.afs", 14}};
  // chrone 67 (Kasumi m8's .tpr) is 16 bytes that are NOT a slot (one byte differs): data.
  WriteSlotArchives(w, empty, 73, 128, 188, {67});
  PokeEntry(w.game / "chrone.afs", 67, 3, 0x00);
  const Marks want = Expected(g, empty);
  CHECK(want.count({5, 7}) && want.count({5, 0}) && want.count({5, 10}) && want.count({13, 5}) &&
        want.count({15, 2}) && want.count({15, 7}));  // the fixture does produce these
  CHECK(!want.count({5, 8}) && !want.count({5, 1}));  // 16-byte data; the decoy's archive differs
  const Marked got = Mark(w.game, g);
  CHECK(got.note.empty());
  CHECK(got.marks == want);
  CHECK(got.marks.count({5, 7}) && got.marks.count({5, 0}) && got.marks.count({5, 10}));  // .tpr-only, .cat-only, shared
  CHECK(got.marks.count({13, 5}) && got.marks.count({15, 2}) && got.marks.count({15, 7}));
  CHECK(!got.marks.count({5, 8}));  // a size-16 entry that is not 0x77 is data, not a slot
  CHECK(!got.marks.count({5, 1}));  // the same entry number empty in chrtwo does not count for chrone
  CHECK(got.marks.size() == want.size() && want.size() == 14);  // the count an independent script gave
}

// Every entry an empty slot: every model whose body files have a location is marked (the whole
// population, against the independent reading).
static void TestMarkEverything() {
  World w;  // World's archives: every entry a placeholder
  FakeGuest g;
  const Marks want = Expected(g, AllEntries(73, 128, 188));
  CHECK(want.size() == 240);  // the count an independent script gave (counts are the image's: 31 for Kasumi)
  const Marked got = Mark(w.game, g);
  CHECK(got.note.empty() && got.marks == want);
  // Characters whose count is unknown (0) have no models to mark.
  bool any_unknown = false;
  for (const auto& mark : got.marks) any_unknown = any_unknown || mark.first == 24;
  CHECK(!any_unknown);
  CHECK(got.marks.count({5, 0}) && got.marks.count({5, 30}) && !got.marks.count({5, 31}));  // 31 records
}

// The slot is judged in the archive AS THE GAME READS IT: through the overlay when the archive has
// one. An overlay that serves real data for the entry unmarks the model; one that serves an empty
// slot (16 x 0x77) does not; one replacing only the .tpr leaves the empty .cat; an overlay of
// another archive changes nothing here.
static void TestMarkThroughOverlay() {
  World w;
  FakeGuest g;
  WriteSlotArchives(w, {{"chrone.afs", 65}, {"chrone.afs", 66}, {"chrtwo.afs", 120}, {"chrtwo.afs", 121}});
  const Slots empty = {{"chrone.afs", 65}, {"chrone.afs", 66}, {"chrtwo.afs", 120}, {"chrtwo.afs", 121}};
  const Marks want = Expected(g, empty);
  CHECK(want.count({5, 7}) && want.count({13, 2}));
  {
    const Marked none = Mark(w.game, g);  // no overlays: the originals
    CHECK(none.note.empty() && none.marks == want);
  }
  const auto data = [] { return Bytes(Content(7, 5000)); };
  const auto slot = [] { return Bytes(std::vector<uint8_t>(16, kFiller)); };
  const auto Minus = [&](std::initializer_list<std::pair<const char*, uint32_t>> served) {
    Slots left = empty;
    for (const auto& [name, entry] : served) left.erase({name, entry});
    return left;
  };
  {
    // Real data in both entries Kasumi m7 reads (chrone 65/66; character 23 shares them): unmarked.
    const Overlays both = {{"chrone.afs", MakeOverlay(w.game, "chrone.afs", {{65, data()}, {66, data()}})}};
    const Marked got = Mark(w.game, g, both);
    const Marks served = Expected(g, Minus({{"chrone.afs", 65}, {"chrone.afs", 66}}));
    CHECK(got.note.empty() && got.marks == served);
    CHECK(!got.marks.count({5, 7}) && !got.marks.count({23, 7}));
    CHECK(got.marks.count({13, 2}));  // chrtwo has no overlay: its original's empty slots
  }
  {
    const Overlays tpr_only = {{"chrone.afs", MakeOverlay(w.game, "chrone.afs", {{65, data()}})}};
    const Marked got = Mark(w.game, g, tpr_only);
    CHECK(got.marks == Expected(g, Minus({{"chrone.afs", 65}})));
    CHECK(got.marks.count({5, 7}));  // the .cat entry 66 is still an empty slot
  }
  {
    const Overlays cat_only = {{"chrone.afs", MakeOverlay(w.game, "chrone.afs", {{66, data()}})}};
    const Marked got = Mark(w.game, g, cat_only);
    CHECK(got.marks == Expected(g, Minus({{"chrone.afs", 66}})));
    CHECK(got.marks.count({5, 7}));  // the .tpr entry 65 is still an empty slot
  }
  {
    const Overlays slots = {{"chrone.afs", MakeOverlay(w.game, "chrone.afs", {{65, slot()}, {66, slot()}})}};
    CHECK(Mark(w.game, g, slots).marks == want);  // replaced by empty slots: still empty
  }
  {
    const Overlays other = {{"chrtwo.afs", MakeOverlay(w.game, "chrtwo.afs", {{120, data()}, {121, data()}})}};
    const Marked got = Mark(w.game, g, other);
    CHECK(got.marks == Expected(g, Minus({{"chrtwo.afs", 120}, {"chrtwo.afs", 121}})));
    CHECK(got.marks.count({5, 7}) && !got.marks.count({13, 2}));  // chrone's original still marks Kasumi m7
  }
}

// A body file with no location (0xFFFF, or a file of another archive such as mot.afs = 23) marks
// nothing, even in archives full of empty slots; the other file of the record still can.
static void TestMarkNoLocation() {
  World w;  // every entry an empty slot
  FakeGuest g;
  const uint32_t rec = g.guest.ReadBe32(0x82C5E738 + 8 * 5 + 4);
  const auto put16 = [&](uint32_t at, uint16_t v) {
    g.guest.Write(at, uint8_t(v >> 8));
    g.guest.Write(at + 1, uint8_t(v));
  };
  put16(rec + 8 * 7, 0xFFFF);      // m7: neither file has a location
  put16(rec + 8 * 7 + 2, 0xFFFF);
  put16(rec + 8 * 8, 23);          // m8: .tpr is in mot.afs, .cat has no location
  put16(rec + 8 * 8 + 2, 0xFFFF);
  put16(rec + 8 * 9, 0xFFFF);      // m9: no .tpr; its .cat (274, chrone 70) is an empty slot
  const Marks want = Expected(g, AllEntries(73, 128, 188));
  CHECK(!want.count({5, 7}) && !want.count({5, 8}) && want.count({5, 9}) && want.count({5, 6}));
  const Marked got = Mark(w.game, g);
  CHECK(got.note.empty() && got.marks == want);
  CHECK(!got.marks.count({5, 7}) && !got.marks.count({5, 8}) && got.marks.count({5, 9}));
}

// An entry past the archive's end marks nothing (the model's files are not there to be empty).
static void TestMarkEntryPastEnd() {
  World w;
  FakeGuest g;
  const Slots empty = AllEntries(60, 128, 188);  // chrone has 60 entries; Kasumi m7 is entry 65/66
  WriteSlotArchives(w, empty, 60, 128, 188);
  const Marks want = Expected(g, empty);
  CHECK(want.count({5, 0}) && !want.count({5, 7}));
  const Marked got = Mark(w.game, g);
  CHECK(got.note.empty() && got.marks == want);
  CHECK(got.marks.count({5, 0}) && !got.marks.count({5, 7}));
}

// An archive that cannot be opened or read marks nothing for its files, is named once in the
// note, and the other archives are still marked.
static void TestMarkUnreadableArchive() {
  World w;
  FakeGuest g;
  std::error_code ec;
  fs::remove(w.game / "chrone.afs", ec);
  CHECK(!ec);
  {
    const Marks want = Expected(g, AllEntries(0, 128, 188));
    const Marked got = Mark(w.game, g);
    CHECK(got.marks == want);
    CHECK(want.count({13, 0}) && want.count({15, 0}) && !want.count({5, 0}));
    CHECK(!got.note.empty() && got.note.find("chrone.afs") != std::string::npos);
    CHECK(got.note.find("chrtwo.afs") == std::string::npos && got.note.find("chrthree.afs") == std::string::npos);
    CHECK(got.note.find("chrone.afs") == got.note.rfind("chrone.afs"));  // named once, not once per model
  }
  // Present but not an AFS archive (and one too short to hold a TOC): the TOC check says no.
  Write(w.game / "chrtwo.afs", std::vector<uint8_t>(4096, 0));
  Write(w.game / "chrthree.afs", std::vector<uint8_t>(5, 'A'));
  {
    const Marked got = Mark(w.game, g);
    CHECK(got.marks.empty());
    CHECK(got.note.find("chrone.afs") != std::string::npos && got.note.find("chrtwo.afs") != std::string::npos &&
          got.note.find("chrthree.afs") != std::string::npos);
  }
  // The reasons differ for a missing and a bad archive, so the note says which.
  {
    const Marked got = Mark(w.game, g);
    CHECK(got.note.find("chrone.afs: ") != std::string::npos && got.note.find("chrtwo.afs: ") != std::string::npos);
  }
}

// Marks are recomputed from nothing: an earlier mark does not survive.
static void TestMarkClearsOldMarks() {
  World w;
  FakeGuest g;
  WriteSlotArchives(w, {});
  doa4::costumes::GuardState s = doa4::costumes::ReadGuardState(g.guest);
  s.empty_slot[3].insert(1);
  s.empty_slot[24].insert(200);
  CHECK(MarkEmptySlots(&s, g.guest, w.game, {}).empty());
  CHECK(MarksOf(s).empty());
}

// The pipeline SetUpSuits runs: import, overlays, Apply, then the marks on Apply's guard. Kasumi's
// own model 7 (chrone 65/66) and Ayane's appended model 6 (chrone 71/72) are served by overlays,
// so neither is marked; without the overlays both are (the appended record exists in the heap copy
// and in the raised count).
static void TestMarkAppliedTables() {
  World w;  // every entry an empty slot
  FakeGuest g;
  WriteText(w.import / "manifest.txt", w.TwoSuits());
  const ImportStatus s = LoadImport(w.import, w.game, g.guest);
  std::string error;
  const Overlays overlays = BuildOverlays(s, w.game, g.guest, &error);
  CHECK(s.suits.size() == 2 && error.empty() && overlays.size() == 1);
  uint32_t next_block = 0x40000000;
  const doa4::costumes::ApplyResult applied = doa4::costumes::Apply(
      g.guest,
      [&](uint32_t size) {
        const uint32_t block = next_block;
        next_block += (size + 31) / 32 * 32;
        return block;
      },
      SuitsOf(s));
  CHECK(applied.applied.size() == 2 && applied.errors.empty());
  CHECK(applied.guard.records[13] == 7 && applied.guard.records[5] == 31);
  const Slots all = AllEntries(73, 128, 188);
  {
    doa4::costumes::GuardState state = applied.guard;
    CHECK(MarkEmptySlots(&state, g.guest, w.game, {}).empty());
    const Marks want = Expected(g, all, applied.guard);
    CHECK(MarksOf(state) == want);
    CHECK(want.count({5, 7}) && want.count({13, 6}) && want.count({13, 5}));  // appended model 6 exists
    CHECK(!want.count({13, 7}));                                                // ... and only up to the raised count
  }
  {
    doa4::costumes::GuardState state = applied.guard;
    CHECK(MarkEmptySlots(&state, g.guest, w.game, overlays).empty());
    Slots served = all;
    for (uint32_t e : {65u, 66u, 71u, 72u}) served.erase({"chrone.afs", e});
    const Marks want = Expected(g, served, applied.guard);
    CHECK(MarksOf(state) == want);
    CHECK(!want.count({5, 7}) && !want.count({13, 6}) && want.count({13, 5}) && want.count({5, 6}));
  }
}

// The 34 picks' body pairs: file numbers, and where the image's file table puts them (archive
// and the .tpr's entry; the .cat is the next entry). From the slot strings in
// the author's picks catalogue (a JSON file, not in this repository); numbers only, no game bytes.
struct Pick {
  uint16_t tpr, cat;
  const char* archive;
  uint32_t entry;
};
static const Pick kPicks[] = {
    {269, 270, "chrone.afs", 65},   {271, 272, "chrone.afs", 67},   {273, 274, "chrone.afs", 69},
    {349, 350, "chrtwo.afs", 72},   {275, 276, "chrone.afs", 71},   {211, 212, "chrone.afs", 7},
    {235, 236, "chrone.afs", 31},   {481, 482, "chrthree.afs", 76}, {347, 348, "chrtwo.afs", 70},
    {345, 346, "chrtwo.afs", 68},   {245, 246, "chrone.afs", 41},   {321, 322, "chrtwo.afs", 44},
    {319, 320, "chrtwo.afs", 42},   {335, 336, "chrtwo.afs", 58},   {343, 344, "chrtwo.afs", 66},
    {381, 382, "chrtwo.afs", 104},  {367, 368, "chrtwo.afs", 90},   {373, 374, "chrtwo.afs", 96},
    {375, 376, "chrtwo.afs", 98},   {295, 296, "chrtwo.afs", 18},   {285, 286, "chrtwo.afs", 8},
    {365, 366, "chrtwo.afs", 88},   {383, 384, "chrtwo.afs", 106},  {355, 356, "chrtwo.afs", 78},
    {353, 354, "chrtwo.afs", 76},   {351, 352, "chrtwo.afs", 74},   {463, 464, "chrthree.afs", 58},
    {465, 466, "chrthree.afs", 60}, {385, 386, "chrtwo.afs", 108},  {407, 408, "chrthree.afs", 2},
    {225, 226, "chrone.afs", 21},   {431, 432, "chrthree.afs", 26}, {449, 450, "chrthree.afs", 44},
    {451, 452, "chrthree.afs", 46},
};

// Against the real archives: every one of the 34 picks' body files is an empty slot (16 bytes of
// 0x77) where the file table says, no two picks share an entry, and the 16-byte entries of the
// three archives are exactly the 68 + 1 the filler rule was derived from.
static void CheckRealPicks(const fs::path& game, const doa4::GuestBytes& guest) {
  struct Real {
    std::shared_ptr<doa4::afs::FileSource> source;
    std::optional<doa4::afs::Toc> toc;
  };
  std::map<std::string, Real> real;
  for (const char* name : {"chrone.afs", "chrtwo.afs", "chrthree.afs"}) {
    Real r;
    r.source = doa4::afs::FileSource::Open(game / name);
    if (r.source) r.toc = doa4::afs::ReadToc(*r.source, nullptr);
    real[name] = r;
  }
  // Whether entry `entry` of `name` is 16 bytes, all kFiller (false when anything is unreadable).
  const auto IsEmptySlot = [&](const std::string& name, uint32_t entry) {
    const Real& r = real[name];
    if (!r.source || !r.toc || entry >= r.toc->count || r.toc->sizes[entry] != 16) return false;
    uint8_t bytes[16];
    if (!r.source->Read(r.toc->offsets[entry], bytes, sizeof bytes)) return false;
    for (uint8_t b : bytes) {
      if (b != kFiller) return false;
    }
    return true;
  };
  CHECK(std::size(kPicks) == 34);
  std::set<std::pair<std::string, uint32_t>> seen;
  for (const Pick& p : kPicks) {
    const auto t = doa4::costumes::FileLocation(guest, p.tpr);
    const auto c = doa4::costumes::FileLocation(guest, p.cat);
    const bool placed = t && c && std::string(t->archive) == p.archive && std::string(c->archive) == p.archive &&
                        t->entry == p.entry && c->entry == p.entry + 1;
    const bool empty = IsEmptySlot(p.archive, p.entry) && IsEmptySlot(p.archive, p.entry + 1);
    const bool fresh = seen.insert({p.archive, p.entry}).second & seen.insert({p.archive, p.entry + 1}).second;
    if (!placed || !empty || !fresh) std::printf("  (pick %u/%u in %s entry %u)\n", p.tpr, p.cat, p.archive, p.entry);
    CHECK(placed);
    CHECK(empty);
    CHECK(fresh);
  }
  CHECK(seen.size() == 68);
  // The 16-byte entries: 17 in chrone (16 filler + entry 0), 38 in chrtwo, 14 in chrthree, all
  // filler but chrone's entry 0.
  const struct {
    const char* name;
    uint32_t sixteen, filler;
  } want[] = {{"chrone.afs", 17, 16}, {"chrtwo.afs", 38, 38}, {"chrthree.afs", 14, 14}};
  for (const auto& wnt : want) {
    uint32_t sixteen = 0, filler = 0;
    const Real& r = real[wnt.name];
    if (r.toc) {
      for (uint32_t i = 0; i < r.toc->count; ++i) {
        if (r.toc->sizes[i] != 16) continue;
        ++sixteen;
        filler += IsEmptySlot(wnt.name, i) ? 1u : 0u;
      }
    }
    CHECK(sixteen == wnt.sixteen && filler == wnt.filler);
  }
  CHECK(!IsEmptySlot("chrone.afs", 0) && real["chrone.afs"].toc && real["chrone.afs"].toc->sizes[0] == 16);
}

// ---- Fix round 1 (F1), against the real archives ----

// The game's lookup sub_82526B78 (the same helper as costumes_host_test.cpp's GameLookup, read from
// .19.cpp:39404-39472): the variant is raised to at least 0 and lowered to var[costume] - 1; index
// = sum of var[0..costume) + variant; model 0 when the index is outside [0, len - 1].
static uint32_t GameLookup(const FakeGuest& g, uint32_t c, uint32_t costume, uint32_t variant) {
  const uint32_t var = g.guest.ReadBe32(0x82C34928 + 4 * c), list = g.guest.ReadBe32(0x82C348C0 + 4 * c);
  const int last = int(g.guest.Read(var + costume)) - 1;
  int v = int(variant);
  if (v < 0) v = 0;
  if (last < v) v = last;
  int sum = 0;
  for (uint32_t k = 0; k < costume; ++k) sum += g.guest.Read(var + k);
  const int index = sum + v;
  if (index < 0 || index > int(g.guest.Read(0x82C348A4 + c)) - 1) return 0;
  return g.guest.Read(list + uint32_t(index));
}

// The 15 own suits of the all34 import (the `suit <char> own:<model>` lines of its manifest):
// (character, model).
static const std::pair<uint32_t, uint32_t> kOwnSuits[] = {
    {5, 7}, {5, 8}, {5, 9}, {21, 4}, {10, 2}, {10, 9}, {10, 13}, {7, 4},
    {20, 2}, {20, 3}, {12, 7}, {12, 0}, {12, 3}, {12, 4}, {1, 5}};

// The census: every (character, model) of the UNPATCHED image whose body .tpr or .cat is an empty
// slot in the game's own archives (68 slots of 0x77: 16 in chrone, 38 in chrtwo, 14 in chrthree).
// Taken from the run below and agreed by an independent script that reads the archives' TOCs and
// the image's tables with its own parser. 15 are the own suits' models; the other 21 are dummy
// models that share a slot with a suit (Kasumi's (23, 7..9) share (5, 7..9)'s files, and so on).
static const std::pair<uint32_t, uint32_t> kCensus[] = {
    {0, 3},   {1, 5},   {2, 4},   {3, 4},   {5, 7},   {5, 8},   {5, 9},   {6, 4},   {7, 4},
    {10, 1},  {10, 2},  {10, 9},  {10, 13}, {10, 14}, {10, 15}, {10, 16}, {10, 17}, {10, 18},
    {10, 19}, {11, 4},  {12, 0},  {12, 3},  {12, 4},  {12, 7},  {12, 8},  {12, 9},  {14, 1},
    {17, 1},  {18, 7},  {19, 0},  {20, 2},  {20, 3},  {21, 4},  {23, 7},  {23, 8},  {23, 9}};

static void PrintMarks(const char* what, const Marks& marks) {
  std::printf("%s (%zu):", what, marks.size());
  for (const auto& [c, m] : marks) std::printf(" (%u,%u)", c, m);
  std::printf("\n");
}

// MarkEmptySlots on the real archives and the image's real tables, read-only.
static void CheckRealMarks(const fs::path& game) {
  FakeGuest g;
  // (a) No overlays: the game's own archives.
  doa4::costumes::GuardState state = doa4::costumes::ReadGuardState(g.guest);
  const std::string note = MarkEmptySlots(&state, g.guest, game, {});
  const Marks census = MarksOf(state);
  PrintMarks("real census, no overlays", census);
  CHECK(note.empty());
  const Marks pinned(std::begin(kCensus), std::end(kCensus));
  CHECK(pinned.size() == 36 && census == pinned);
  CHECK(census.count({5, 7}));  // Kasumi model 7: her own suit's slot
  for (const auto& own : kOwnSuits) CHECK(census.count(own));
  CHECK(Marks(std::begin(kOwnSuits), std::end(kOwnSuits)).size() == 15);
  // NO model a player can reach through the game's own lookup (any costume below max, any variant)
  // is marked: marking one would change the unmodified game. Characters 22 and 23 keep their list
  // outside the image (0x831A649F, not a costume list in the image: costumes::CheckSuit refuses
  // it too), so what their lookup returns cannot be read here and they are not walked.
  Marks reachable;
  for (uint32_t c = 0; c < 25; ++c) {
    const uint32_t var = g.guest.ReadBe32(0x82C34928 + 4 * c), list = g.guest.ReadBe32(0x82C348C0 + 4 * c);
    const auto in_image = [](uint32_t a) { return a >= 0x82000000 && a < 0x83000000; };
    if (!in_image(var) || !in_image(list)) continue;
    for (uint32_t costume = 0; costume < g.guest.Read(0x82C34888 + c); ++costume) {
      for (uint32_t variant = 0; variant < std::max<uint32_t>(g.guest.Read(var + costume), 1); ++variant) {
        reachable.insert({c, GameLookup(g, c, costume, variant)});
      }
    }
  }
  CHECK(reachable.size() == 168);  // (the same population an independent script walked)
  Marks reachable_and_marked;
  for (const auto& r : reachable) {
    if (census.count(r)) reachable_and_marked.insert(r);
  }
  PrintMarks("reachable and marked (must be none)", reachable_and_marked);
  CHECK(reachable_and_marked.empty());
}

// With the all34 import (DOA4_SUITS_IMPORT_DIR=<that folder>): the overlays serve the 34 suits'
// files, so the models that read those slots are no longer empty. None of the 15 own suits' models
// stays marked; every census model whose body file is one of the 68 replaced entries goes (the 15
// own models and the 21 dummies that share their slots); the guard on Apply's tables marks none,
// and marks the 19 appended models when the overlays are left out.
static void CheckRealMarksWithImport(const fs::path& game, const fs::path& import) {
  FakeGuest g;
  const ImportStatus s = LoadImport(import, game, g.guest);
  CHECK(s.problem.empty() && s.skipped.empty() && s.suits.size() == 34);
  Marks own;
  Slots replaced;
  for (const LoadedSuit& l : s.suits) {
    if (l.suit.own_model >= 0) own.insert({l.suit.character, uint32_t(l.suit.own_model)});
    for (uint32_t file : {uint32_t(l.suit.body_tpr), uint32_t(l.suit.body_cat)}) {
      if (const auto loc = doa4::costumes::FileLocation(g.guest, file)) replaced.insert({loc->archive, loc->entry});
    }
  }
  CHECK(own == Marks(std::begin(kOwnSuits), std::end(kOwnSuits)));  // the literal above is the manifest's
  CHECK(replaced.size() == 68);
  std::string error;
  const Overlays overlays = BuildOverlays(s, game, g.guest, &error);
  CHECK(error.empty() && overlays.size() == 3);
  doa4::costumes::GuardState state = doa4::costumes::ReadGuardState(g.guest);
  CHECK(MarkEmptySlots(&state, g.guest, game, overlays).empty());
  const Marks after = MarksOf(state);
  const Marks census(std::begin(kCensus), std::end(kCensus));
  const Marks served = Expected(g, replaced);  // the models that read a replaced entry
  Marks want;
  for (const auto& m : census) {
    if (!served.count(m)) want.insert(m);
  }
  PrintMarks("real census, all34 overlays", after);
  CHECK(after == want);
  for (const auto& m : own) CHECK(!after.count(m));
  CHECK(census.size() - after.size() == 36 && after.empty());  // every census model reads a slot a suit fills
  // On the patched tables (what SetUpSuits marks): the appended models are served too.
  FakeGuest h;
  const ImportStatus s2 = LoadImport(import, game, h.guest);
  std::string error2;
  const Overlays overlays2 = BuildOverlays(s2, game, h.guest, &error2);
  uint32_t next_block = 0x40000000;
  const doa4::costumes::ApplyResult applied = doa4::costumes::Apply(
      h.guest,
      [&](uint32_t size) {
        const uint32_t block = next_block;
        next_block += (size + 31) / 32 * 32;
        return block;
      },
      SuitsOf(s2));
  CHECK(error2.empty() && applied.applied.size() == 34 && applied.errors.empty());
  doa4::costumes::GuardState with = applied.guard, without = applied.guard;
  CHECK(MarkEmptySlots(&with, h.guest, game, overlays2).empty());
  CHECK(MarkEmptySlots(&without, h.guest, game, {}).empty());
  PrintMarks("patched tables, all34 overlays", MarksOf(with));
  CHECK(MarksOf(with).empty());
  CHECK(MarksOf(without).size() == 36 + 19);  // the census plus the 19 appended models (no overlay: still empty slots)
}

// Optional: the same checks against the REAL archives (DOA4_SUITS_GAME_DIR=<the DOA4 folder with
// chrone.afs, chrtwo.afs and chrthree.afs>, opened read-only). The fake archives above say the
// image's file table points at 16-byte placeholders of 0x77; this says the game's own archives
// agree.
static void TestRealGameFolder() {
  wchar_t dir[1024];
  const DWORD len = GetEnvironmentVariableW(L"DOA4_SUITS_GAME_DIR", dir, DWORD(std::size(dir)));
  if (len == 0 || len >= std::size(dir)) {
    std::printf("real game folder: SKIPPED (DOA4_SUITS_GAME_DIR unset)\n");
    return;
  }
  const fs::path game(dir);
  World w;
  FakeGuest g;
  const std::string text = "doax2-suits 1\nconverter test\n" +
                           w.Line("suit 5 own:7 269 270 509 510 vks_ch_d 0", "ka", 1) +
                           w.Line("suit 13 append 275 276 543 544 vay_cs_b 0", "ay", 3) +
                           w.Line("suit 15 append 355 356 545 546 vls_cs_h 0", "li", 5);
  WriteText(w.import / "manifest.txt", text);
  {
    const ImportStatus s = LoadImport(w.import, game, g.guest);
    CHECK(s.problem.empty() && s.skipped.empty() && s.suits.size() == 3);
    std::string error;
    const auto overlays = BuildOverlays(s, game, g.guest, &error);
    CHECK(error.empty() && overlays.size() == 2);
    CHECK(EntryHolds(overlays, "chrone.afs", 65, w.files["ka.tpr"]) &&
          EntryHolds(overlays, "chrone.afs", 72, w.files["ay.cat"]) &&
          EntryHolds(overlays, "chrtwo.afs", 79, w.files["li.cat"]));
  }
  // A pair that is real data in the game (Ayane's head-part files, chrthree 138/139) is refused
  // as a body. Its head part is her own (543/544, one of her records' pairs), so the head-part
  // check passes and it is the slot check that speaks.
  WriteText(w.import / "manifest.txt",
            "doax2-suits 1\nconverter test\n" + w.Line("suit 13 append 543 544 543 544 vay_cs_x 0", "az", 9));
  {
    const ImportStatus real = LoadImport(w.import, game, g.guest);
    CHECK(real.suits.empty() && real.skipped.size() == 1 &&
          SkippedAt(real, 0) ==
              "vay_cs_x c0: file 543 (entry 138 of chrthree.afs) is not an empty costume slot (2394112 bytes)");
  }
  // File 204 (chrone entry 0) is exactly 16 bytes, but of data (00 00 00 01 and zeros): not a slot.
  WriteText(w.import / "manifest.txt",
            "doax2-suits 1\nconverter test\n" + w.Line("suit 1 append 204 205 - - vxx_cs_z 0", "d4", 11));
  {
    const ImportStatus data = LoadImport(w.import, game, g.guest);
    CHECK(data.suits.empty() && data.skipped.size() == 1 &&
          SkippedAt(data, 0) ==
              "vxx_cs_z c0: file 204 (entry 0 of chrone.afs) is not an empty costume slot (it holds data)");
  }
  CheckRealPicks(game, g.guest);
  CheckRealMarks(game);
  wchar_t import_dir[1024];
  const DWORD import_len = GetEnvironmentVariableW(L"DOA4_SUITS_IMPORT_DIR", import_dir, DWORD(std::size(import_dir)));
  if (import_len == 0 || import_len >= std::size(import_dir)) {
    std::printf("real import folder: SKIPPED (DOA4_SUITS_IMPORT_DIR unset)\n");
  } else {
    CheckRealMarksWithImport(game, fs::path(import_dir));
  }
}

// ---- Task 5: the compiled picks, what makes two imports the same, and the files this PC holds ----

// "" when `suits` (a manifest's, in its order) name the compiled table: every field but the file sizes and hashes.
static std::string DifferenceFromPicks(const std::vector<Suit>& suits) {
  const std::vector<Suit> picks = PickSuits();
  if (suits.size() != picks.size()) return "count " + std::to_string(suits.size());
  for (size_t i = 0; i < picks.size(); ++i) {
    const Suit& a = suits[i];
    const Suit& p = picks[i];
    const std::string at = "line " + std::to_string(i) + " (" + a.Label() + "): ";
    if (a.character != p.character) return at + "character";
    if (a.own_model != p.own_model) return at + "own_model";
    if (a.body_tpr != p.body_tpr || a.body_cat != p.body_cat) return at + "body";
    if (a.part_tpr != p.part_tpr || a.part_cat != p.part_cat) return at + "part";
    if (a.design != p.design) return at + "design";
    if (a.colour != p.colour) return at + "colour";
    if (a.tpr.name != p.tpr.name || a.cat.name != p.cat.name) return at + "file name";
  }
  return "";
}

static void TestPicksTable() {
  const std::vector<Suit> picks = PickSuits();
  CHECK(picks.size() == 34);
  // A real manifest's rules hold for the table: ParseManifest refuses a repeated file, placeholder or label.
  Manifest m;
  m.converter = "picks";
  for (Suit s : picks) {
    s.tpr.size = 1;
    s.tpr.sha1 = std::string(40, 'a');
    s.cat.size = 1;
    s.cat.sha1 = std::string(40, 'b');
    m.suits.push_back(s);
  }
  const ParseResult parsed = ParseManifest(FormatManifest(m));
  CHECK(parsed.manifest.has_value());
  if (parsed.manifest) CHECK(DifferenceFromPicks(parsed.manifest->suits).empty());
  // Nothing is claimed about a file the table does not know.
  for (const Suit& s : picks) CHECK(s.tpr.size == 0 && s.cat.size == 0 && s.tpr.sha1.empty() && s.cat.sha1.empty());
  // The fixture list above (kPicks, from picks.json's slot strings) agrees on the body pairs.
  std::map<std::pair<uint16_t, uint16_t>, int> slots;
  for (const Pick& p : kPicks) ++slots[{p.tpr, p.cat}];
  size_t found = 0;
  for (const Suit& s : picks) found += slots.count({s.body_tpr, s.body_cat});
  CHECK(found == 34);
  // Per girl, as the catalogue has them.
  std::map<int, int> per;
  for (const Suit& s : picks) ++per[s.character];
  CHECK(per[5] == 4 && per[13] == 3 && per[21] == 4 && per[10] == 4 && per[7] == 4 && per[15] == 3 && per[20] == 4 && per[12] == 4 &&
        per[1] == 4);
  // Pinned for a run with no real manifest: the SHA-1 of the table's own lines, derived by a script from the real manifest
  // (imports/all34_r18/manifest.txt: each line's first nine words, its file names, size 0, no hash), not from this table.
  std::string lines;
  for (const Suit& s : picks) lines += FormatSuitLine(s);
  CHECK(doa4::gamefiles::Sha1Hex(lines.data(), lines.size()) == "a2eff1921139fa78fb21f38a0e40b40efb2e1289");
  // The instrument can see each field: one change each, every one named.
  const std::vector<Suit> base = picks;
  CHECK(DifferenceFromPicks(base).empty());
  const auto seen = [&](auto&& change) {
    std::vector<Suit> v = base;
    change(v[2]);
    return !DifferenceFromPicks(v).empty();
  };
  CHECK(seen([](Suit& s) { s.character = 6; }));
  CHECK(seen([](Suit& s) { s.own_model = 9; }));
  CHECK(seen([](Suit& s) { s.body_tpr = 1; }));
  CHECK(seen([](Suit& s) { s.body_cat = 1; }));
  CHECK(seen([](Suit& s) { s.part_tpr = 1; }));
  CHECK(seen([](Suit& s) { s.part_cat = 1; }));
  CHECK(seen([](Suit& s) { s.design = "x"; }));
  CHECK(seen([](Suit& s) { s.colour = 1; }));
  CHECK(seen([](Suit& s) { s.tpr.name = "x"; }));
  CHECK(seen([](Suit& s) { s.cat.name = "x"; }));
}

// Env-gated, as the disc's checks are: DOA4_SUITS_MANIFEST_REAL=<a real import's manifest.txt>.
static void TestPicksAgainstRealManifest() {
  const char* path = std::getenv("DOA4_SUITS_MANIFEST_REAL");
  if (!path) {
    std::printf("real manifest: SKIPPED (DOA4_SUITS_MANIFEST_REAL unset)\n");
    return;
  }
  const std::vector<uint8_t> bytes = ReadAll(path);
  const std::string text(bytes.begin(), bytes.end());
  const ParseResult parsed = ParseManifest(text);
  CHECK(parsed.manifest.has_value());
  if (!parsed.manifest) return;
  const std::string diff = DifferenceFromPicks(parsed.manifest->suits);
  if (!diff.empty()) std::printf("the manifest differs from the picks: %s\n", diff.c_str());
  CHECK(diff.empty());
  std::printf("real manifest: %zu suits compared with the picks\n", parsed.manifest->suits.size());
}

// A real manifest line for pick `i`, with these file contents.
static Suit PickWithFiles(size_t i, const std::vector<uint8_t>& tpr, const std::vector<uint8_t>& cat) {
  Suit s = PickSuits()[i];
  s.tpr.size = tpr.size();
  s.tpr.sha1 = Sha(tpr);
  s.cat.size = cat.size();
  s.cat.sha1 = Sha(cat);
  return s;
}

static ImportStatus StatusOf(std::vector<Suit> suits) {
  ImportStatus st;
  st.folder = true;
  for (Suit& s : suits) st.suits.push_back({std::move(s), nullptr, nullptr});
  return st;
}

static void TestImportChecksum() {
  const std::vector<uint8_t> a = Content(1, 64), b = Content(2, 64), c = Content(3, 64);
  const Suit s0 = PickWithFiles(0, a, b), s1 = PickWithFiles(1, a, c), s2 = PickWithFiles(2, b, c);
  const std::string whole = ImportChecksum(StatusOf({s0, s1, s2}));
  CHECK(whole.size() == 40);
  CHECK(ImportChecksum(StatusOf({})).empty());  // no import is the empty text SuitSessionOf reads as none
  // The same: the line order and the converter are not part of it (the session's order comes from the picks).
  CHECK(ImportChecksum(StatusOf({s2, s0, s1})) == whole);
  // Not the same: any suit's bytes, or a suit this PC could not load (the spec: a changed or missing file counts as a different import).
  CHECK(ImportChecksum(StatusOf({s0, s1})) != whole);
  Suit other = s2;
  other.cat.sha1 = std::string(40, 'f');
  CHECK(ImportChecksum(StatusOf({s0, s1, other})) != whole);
  other = s2;
  other.tpr.size += 1;
  CHECK(ImportChecksum(StatusOf({s0, s1, other})) != whole);
  // Its input is the suit lines, whatever else the manifest holds: a suit the picks do not list is not in a session, so not in the hash.
  Suit stranger = s2;
  stranger.design = "vzz_xx_x";
  CHECK(!IsPick(stranger));
  CHECK(ImportChecksum(StatusOf({s0, s1, s2, stranger})) == whole);
  CHECK(ImportChecksum(StatusOf({stranger})).empty());
  // The right label on other numbers is not that pick either.
  for (const auto& change : std::vector<std::function<void(Suit&)>>{[](Suit& s) { s.own_model = 3; }, [](Suit& s) { s.body_tpr = 5; },
                                                                    [](Suit& s) { s.body_cat = 5; }, [](Suit& s) { s.part_tpr = 5; },
                                                                    [](Suit& s) { s.part_cat = 5; }, [](Suit& s) { s.character = 6; },
                                                                    [](Suit& s) { s.colour = 3; }}) {
    Suit bent = s0;
    change(bent);
    CHECK(!IsPick(bent));
  }
  CHECK(IsPick(s0));
  CHECK(IsPick(s2));
}

// A session: SessionFiles leaves the loaded suits that are picks; the tables come from the picks whatever this PC holds; a model
// whose file this PC lacks is an empty slot to the guard, so the load request goes to her first costume.
static void TestSessionFilesAndGuard() {
  World w;  // every entry of the fake archives an empty slot
  FakeGuest with_files, no_files;
  const std::vector<uint8_t> a = Content(21, 4096), b = Content(22, 2048);
  // the import: one real pick (Ayane's vay_on_k, append) and one suit the picks do not list
  const size_t pick_index = 5;
  Suit in_picks = PickWithFiles(pick_index, a, b);
  CHECK(in_picks.design == "vay_on_k" && in_picks.character == 13 && in_picks.own_model == -1);
  Manifest m;
  m.converter = "t";
  m.suits.push_back(in_picks);
  Suit outsider = PickWithFiles(0, b, a);
  outsider.design = "vzz_xx_x";
  outsider.tpr.name = "vzz_xx_x_0.tpr";
  outsider.cat.name = "vzz_xx_x_0.cat";
  m.suits.push_back(outsider);
  Write(w.import / in_picks.tpr.name, a);
  Write(w.import / in_picks.cat.name, b);
  Write(w.import / outsider.tpr.name, b);
  Write(w.import / outsider.cat.name, a);
  WriteText(w.import / "manifest.txt", FormatManifest(m));
  ImportStatus s = LoadImport(w.import, w.game, with_files.guest);
  CHECK(s.suits.size() == 2);
  const std::vector<LoadedSuit> files = SessionFiles(s);
  CHECK(files.size() == 1);
  if (files.size() == 1) CHECK(files[0].suit.Label() == "vay_on_k c0");
  s.suits = files;
  std::string error;
  const Overlays overlays = BuildOverlays(s, w.game, with_files.guest, &error);
  CHECK(error.empty() && overlays.size() == 1);

  const auto build = [](FakeGuest& g) {
    uint32_t next_block = 0x40000000;
    return doa4::costumes::Apply(
        g.guest,
        [&](uint32_t size) {
          const uint32_t block = next_block;
          next_block += (size + 31) / 32 * 32;
          return block;
        },
        doa4::costumes::Fitting(g.guest, PickSuits(), nullptr));
  };
  const doa4::costumes::ApplyResult ra = build(with_files), rb = build(no_files);
  CHECK(with_files.bytes == no_files.bytes);  // the tables, byte for byte, whatever each PC holds
  CHECK(ra.applied.size() == 34 && rb.applied.size() == 34);

  doa4::costumes::GuardState ga = ra.guard, gb = rb.guard;
  CHECK(MarkEmptySlots(&ga, with_files.guest, w.game, overlays).empty());
  CHECK(MarkEmptySlots(&gb, no_files.guest, w.game, {}).empty());
  uint32_t ayane_model = 0xFF;
  for (const auto& ap : ra.applied) {
    if (ap.label == "vay_on_k c0") ayane_model = ap.model;
  }
  CHECK(ayane_model != 0xFF);
  // The PC with the file loads the suit; the PC without sends that request to her first costume.
  CHECK(!ga.empty_slot[13].contains(ayane_model));
  CHECK(gb.empty_slot[13].contains(ayane_model));
  const auto first = [](FakeGuest& g, uint32_t c) { return uint32_t(g.guest.Read(g.guest.ReadBe32(doa4::costumes::kListTable + 4 * c))); };
  CHECK(doa4::costumes::DecideLoad(&ga, with_files.guest, 13, ayane_model).use == ayane_model);
  CHECK(doa4::costumes::DecideLoad(&gb, no_files.guest, 13, ayane_model).use == first(no_files, 13));
  CHECK(doa4::costumes::DecideLoad(&gb, no_files.guest, 13, ayane_model).rule == doa4::costumes::GuardRule::kEmptySlot);
}

int main() {
  // Unbuffered: a run that dies mid-way still shows the FAIL lines printed before it.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  // Fail before the tables are read: an empty guest is enough, with or without the image.
  TestNoFolder();
  TestImportIsAFile();
  TestNoManifest();
  TestBadManifest();
  TestManifestSizeLimit();
  TestPicksTable();
  TestPicksAgainstRealManifest();
  TestImportChecksum();
  std::string why;
  if (const std::optional<doa4_test::CostumeImage> image = doa4_test::LoadCostumeImage(&why)) {
    g_image = &*image;
    TestLoadsTwo();
    TestFingerprintIsCanonical();
    TestKnownChecksums();
    TestBigFiles();
    TestChecksumMismatch();
    TestFileReasons();
    TestMissingAndWrongSize();
    TestFileInUse();
    TestNotAPlaceholder();
    TestPlaceholderEdges();
    TestPlaceholderContent();
    TestEntryBeyondArchive();
    TestArchiveMissing();
    TestTableReasonPropagates();
    TestTableReasonComesFirst();
    TestUnicodeFolder();
    TestUnicodeGameFolder();
    TestBuildOverlays();
    TestThreeArchives();
    TestBuildOverlaysEdges();
    TestOverlayRecheck();
    TestFailDropsSuits();
    TestFileHeldOpen();
    TestMarkSingleFiles();
    TestMarkEverything();
    TestMarkThroughOverlay();
    TestMarkNoLocation();
    TestMarkEntryPastEnd();
    TestMarkUnreadableArchive();
    TestMarkClearsOldMarks();
    TestMarkAppliedTables();
    TestRealGameFolder();
    TestSessionFilesAndGuard();
  } else if (doa4_test::ReportNoImage("suits: the sections on the image's costume tables", why)) {
    ++g_checks;
    ++g_failures;
  }
  std::printf("suits: %d checks run\n", g_checks);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("suits: all checks passed\n");
  return 0;
}
