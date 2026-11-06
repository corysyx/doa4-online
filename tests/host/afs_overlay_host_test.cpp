// Host test for src/doa4_afs_overlay.cpp. Built and run by run_afs_overlay_test.bat.
// Optional real-archive check: DOA4_AFS_REAL=<config file> with lines
//   original <path>   golden <path>   replace <entry> <path>
// (golden = doax2_work/stage0/scripts/afs_rebuild.py's output for the same replacements).

#include "../../src/doa4_afs_overlay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::afs;
namespace fs = std::filesystem;

static void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i));
}
static uint32_t Get32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 |
         uint32_t(b[at + 3]) << 24;
}
static std::vector<uint8_t> Bytes(int seed, size_t size) {
  std::vector<uint8_t> e(size);
  for (size_t i = 0; i < size; ++i) e[i] = uint8_t(seed * 37 + i * 11 + (i >> 8));
  return e;
}

// Three entries of 100, 3000 and 10 bytes, laid out as the game's archives are. The offsets are
// written out by hand (0x800, 0x1000, 0x2000; name table at 0x2800; end 0x3000), not computed
// with the rule under test.
static std::vector<uint8_t> ThreeEntryArchive() {
  std::vector<uint8_t> a(0x3000, 0);
  std::memcpy(a.data(), "AFS\0", 4);
  Put32(a, 4, 3);
  Put32(a, 8, 0x800);
  Put32(a, 12, 100);
  Put32(a, 16, 0x1000);
  Put32(a, 20, 3000);
  Put32(a, 24, 0x2000);
  Put32(a, 28, 10);
  Put32(a, 32, 0x2800);
  Put32(a, 36, 144);
  const auto put = [&](size_t at, const std::vector<uint8_t>& e) {
    std::memcpy(&a[at], e.data(), e.size());
  };
  put(0x800, Bytes(0, 100));
  put(0x1000, Bytes(1, 3000));
  put(0x2000, Bytes(2, 10));
  for (int i = 0; i < 3; ++i) {
    const std::string name = "e" + std::to_string(i) + ".bin";
    std::memcpy(&a[0x2800 + 48 * size_t(i)], name.data(), name.size());
  }
  // Name record i's last u32 = TOC word i: the count, entry 0's offset, entry 0's size.
  Put32(a, 0x2800 + 44, 3);
  Put32(a, 0x2800 + 48 + 44, 0x800);
  Put32(a, 0x2800 + 96 + 44, 100);
  return a;
}

static std::shared_ptr<const Source> Mem(std::vector<uint8_t> bytes) {
  return std::make_shared<MemorySource>(std::move(bytes));
}

static std::vector<uint8_t> ReadAll(const Overlay& o) {
  std::vector<uint8_t> out(size_t(o.size()));
  CHECK(o.Read(0, out.data(), out.size()));
  return out;
}

static void TestIdentity() {
  const std::vector<uint8_t> a = ThreeEntryArchive();
  std::string error;
  const auto o = Overlay::Build(Mem(a), {}, &error);
  CHECK(o != nullptr);
  if (!o) return;
  CHECK(o->size() == 0x3000);
  CHECK(o->original_size() == 0x3000);
  CHECK(ReadAll(*o) == a);
}

static void TestReplaceGrows() {
  const std::vector<uint8_t> a = ThreeEntryArchive();
  const std::vector<uint8_t> big = Bytes(9, 5000);
  std::string error;
  const auto o = Overlay::Build(Mem(a), {{1, Mem(big)}}, &error);
  CHECK(o != nullptr);
  if (!o) return;
  // e0 0x800/100, e1 0x1000/5000, e2 align(0x1000 + 5000) = 0x2800/10,
  // names align(0x280A) = 0x3000 (144 bytes), end align(0x3090) = 0x3800.
  CHECK(o->size() == 0x3800);
  const std::vector<uint8_t> b = ReadAll(*o);
  CHECK(std::memcmp(b.data(), "AFS\0", 4) == 0);
  CHECK(Get32(b, 4) == 3);
  CHECK(Get32(b, 8) == 0x800 && Get32(b, 12) == 100);
  CHECK(Get32(b, 16) == 0x1000 && Get32(b, 20) == 5000);
  CHECK(Get32(b, 24) == 0x2800 && Get32(b, 28) == 10);
  CHECK(Get32(b, 32) == 0x3000 && Get32(b, 36) == 144);
  CHECK(std::memcmp(&b[0x800], Bytes(0, 100).data(), 100) == 0);
  CHECK(std::memcmp(&b[0x1000], big.data(), big.size()) == 0);
  bool pad_zero = true;
  for (size_t i = 0x1000 + 5000; i < 0x2800; ++i) pad_zero = pad_zero && b[i] == 0;
  CHECK(pad_zero);
  CHECK(std::memcmp(&b[0x2800], Bytes(2, 10).data(), 10) == 0);
  CHECK(std::memcmp(&b[0x3000], "e0.bin", 6) == 0);
  CHECK(std::memcmp(&b[0x3030], "e1.bin", 6) == 0);
  CHECK(Get32(b, 0x3000 + 44) == 3);
  CHECK(Get32(b, 0x3000 + 48 + 44) == 0x800);
  CHECK(Get32(b, 0x3000 + 96 + 44) == 100);
  bool tail_zero = true;
  for (size_t i = 0x3090; i < 0x3800; ++i) tail_zero = tail_zero && b[i] == 0;
  CHECK(tail_zero);
  CHECK(o->entry_offset(2) == 0x2800 && o->entry_size(1) == 5000);
  CHECK(o->ReplacedEntryAt(0x1000) == 1);
  CHECK(o->ReplacedEntryAt(0x1000 + 4999) == 1);
  CHECK(o->ReplacedEntryAt(0x800) == -1);
  CHECK(o->ReplacedEntryAt(0x2800) == -1);
}

static void TestReplaceEntry0() {
  const auto o = Overlay::Build(Mem(ThreeEntryArchive()), {{0, Mem(Bytes(7, 3000))}}, nullptr);
  CHECK(o != nullptr);
  if (!o) return;
  // e0 stays at 0x800 (3000 bytes), e1 align(0x13B8) = 0x1800, e2 align(0x23B8) = 0x2800,
  // names 0x3000, end 0x3800; name record 2 holds entry 0's new size.
  const std::vector<uint8_t> b = ReadAll(*o);
  CHECK(Get32(b, 8) == 0x800 && Get32(b, 12) == 3000);
  CHECK(Get32(b, 16) == 0x1800 && Get32(b, 24) == 0x2800 && Get32(b, 32) == 0x3000);
  CHECK(Get32(b, 0x3000 + 96 + 44) == 3000);
  CHECK(o->size() == 0x3800);
}

static void TestReadSlices() {
  const auto o = Overlay::Build(Mem(ThreeEntryArchive()), {{1, Mem(Bytes(9, 5000))}}, nullptr);
  CHECK(o != nullptr);
  if (!o) return;
  const std::vector<uint8_t> all = ReadAll(*o);
  std::mt19937 rng(1234);
  bool all_equal = true;
  for (int k = 0; k < 500; ++k) {
    const size_t off = rng() % all.size();
    const size_t n = rng() % (all.size() - off + 1);
    std::vector<uint8_t> part(n);
    all_equal = all_equal && o->Read(off, part.data(), n) &&
                std::memcmp(part.data(), all.data() + off, n) == 0;
  }
  CHECK(all_equal);
  uint8_t two[2];
  CHECK(!o->Read(o->size() - 1, two, 2));
  CHECK(o->Read(o->size(), two, 0));
}

static void ExpectBuildError(std::vector<uint8_t> archive, std::vector<Replacement> repl,
                             const std::string& want) {
  std::string error;
  const auto o = Overlay::Build(Mem(std::move(archive)), std::move(repl), &error);
  const bool ok = !o && error.find(want) != std::string::npos;
  if (!ok) std::printf("  want an error containing '%s', got '%s'\n", want.c_str(), error.c_str());
  CHECK(ok);
}

static void TestRejects() {
  std::vector<uint8_t> a = ThreeEntryArchive();
  a[0] = 'X';
  ExpectBuildError(a, {}, "not an AFS archive");
  a = ThreeEntryArchive();
  Put32(a, 4, 0);
  ExpectBuildError(a, {}, "bad entry count");
  a = ThreeEntryArchive();
  Put32(a, 28, 0x10000);
  ExpectBuildError(a, {}, "entry 2 runs past the end");
  a = ThreeEntryArchive();
  Put32(a, 36, 100);
  ExpectBuildError(a, {}, "name table is not 48 bytes per entry");
  ExpectBuildError(ThreeEntryArchive(), {{3, Mem(Bytes(1, 10))}}, "replacement for entry 3 of 3");
  ExpectBuildError(ThreeEntryArchive(), {{1, Mem(Bytes(1, 10))}, {1, Mem(Bytes(2, 10))}},
                   "entry 1 replaced twice");
  ExpectBuildError(ThreeEntryArchive(), {{1, Mem({})}}, "replacement for entry 1 is empty");
}

static fs::path WriteTemp(const std::string& name, const std::vector<uint8_t>& bytes) {
  const fs::path p = fs::temp_directory_path() / name;
  std::ofstream(p, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  return p;
}

// Temp file names carry this run's process id, so two runs at once never share a file.
static std::string RunTag() { return "doa4_afs_test_" + std::to_string(GetCurrentProcessId()) + "_"; }

// Deletes its file when it goes out of scope. Declared before the sources that hold the file open,
// so those close first: FileSource shares for read only, by design, and a delete fails while one
// is open.
struct TempFile {
  fs::path path;
  ~TempFile() {
    std::error_code ec;
    fs::remove(path, ec);
    CHECK(!ec);
  }
};

static void TestConcurrentReads() {
  const fs::path arch = WriteTemp(RunTag() + "archive.afs", ThreeEntryArchive());
  const TempFile arch_cleanup{arch};
  const fs::path repl = WriteTemp(RunTag() + "entry.bin", Bytes(9, 5000));
  const TempFile repl_cleanup{repl};
  auto original = FileSource::Open(arch);
  auto data = FileSource::Open(repl);
  CHECK(original && data);
  if (!original || !data) return;
  CHECK(original->size() == 0x3000);
  const auto o = Overlay::Build(original, {{1, data}}, nullptr);
  const auto ref = Overlay::Build(Mem(ThreeEntryArchive()), {{1, Mem(Bytes(9, 5000))}}, nullptr);
  CHECK(o && ref);
  if (!o || !ref) return;
  const std::vector<uint8_t> all = ReadAll(*ref);
  std::vector<int> bad(8, 0);
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&, t] {
      std::mt19937 rng(77 + t);
      for (int k = 0; k < 500; ++k) {
        const size_t off = rng() % all.size();
        const size_t n = 1 + rng() % std::min<size_t>(3000, all.size() - off);
        std::vector<uint8_t> part(n);
        if (!o->Read(off, part.data(), n) || std::memcmp(part.data(), all.data() + off, n) != 0) {
          ++bad[size_t(t)];
        }
      }
    });
  }
  for (auto& th : threads) th.join();
  int total = 0;
  for (int b : bad) total += b;
  CHECK(total == 0);
  CHECK(FileSource::Open(fs::temp_directory_path() / (RunTag() + "missing.afs")) == nullptr);
}

// ---- Edge checks, added after the mutation probe: each one kills a named mutant that the checks
// above let through (the probe's survivors f1 f2 g1 g2 i k2 m2 r w x y1 y2 z, plus the keep-alive
// mutants v1 v2). Every limit is pinned on both sides.

// A source of zeros that takes no memory, for sizes near the 4 GiB limit of the 32-bit TOC.
class ZeroSource final : public Source {
 public:
  explicit ZeroSource(uint64_t size) : size_(size) {}
  uint64_t size() const override { return size_; }
  bool Read(uint64_t offset, void* dst, size_t n) const override {
    if (offset > size_ || n > size_ - offset) return false;
    if (n) std::memset(dst, 0, n);
    return true;
  }

 private:
  uint64_t size_;
};

// A memory source that counts how many are alive.
class CountedSource final : public Source {
 public:
  CountedSource(std::vector<uint8_t> bytes, int* alive) : inner_(std::move(bytes)), alive_(alive) {
    ++*alive_;
  }
  ~CountedSource() override { --*alive_; }
  uint64_t size() const override { return inner_.size(); }
  bool Read(uint64_t offset, void* dst, size_t n) const override {
    return inner_.Read(offset, dst, n);
  }

 private:
  MemorySource inner_;
  int* alive_;
};

// The Source contract: Read fills all n bytes or returns false; a read may end exactly at the end
// but not pass it, whatever the offset. `want` holds the source's bytes (at least 16).
static void CheckSourceContract(const Source& s, const std::vector<uint8_t>& want) {
  const uint64_t size = want.size();
  uint8_t buf[16];
  CHECK(s.size() == size);
  CHECK(s.Read(size - 2, buf, 2) && buf[0] == want[size - 2] && buf[1] == want[size - 1]);
  CHECK(!s.Read(size - 1, buf, 2));
  CHECK(s.Read(size, buf, 0));
  CHECK(!s.Read(size + 1, buf, 0));
  CHECK(!s.Read(~uint64_t(0), buf, 1));
  CHECK(s.Read(0, buf, 16) && std::memcmp(buf, want.data(), 16) == 0);
}

static void TestEdges() {
  const std::vector<uint8_t> base = ThreeEntryArchive();
  std::string error;

  // An entry that is an exact multiple of 2048 bytes gets no padding (the real .tpr is 2688
  // sectors). e0 0x800/100, e1 align(0x864) = 0x1000/2048, e2 align(0x1800) = 0x1800/10,
  // names align(0x180A) = 0x2000, end align(0x2090) = 0x2800.
  {
    const auto o = Overlay::Build(Mem(base), {{1, Mem(Bytes(3, 2048))}}, &error);
    CHECK(o != nullptr);
    if (o) {
      CHECK(o->entry_offset(2) == 0x1800 && o->size() == 0x2800);
      const std::vector<uint8_t> b = ReadAll(*o);
      CHECK(Get32(b, 24) == 0x1800 && Get32(b, 32) == 0x2000);
      CHECK(std::memcmp(&b[0x1800], Bytes(2, 10).data(), 10) == 0);
      CHECK(std::memcmp(&b[0x2000], "e0.bin", 6) == 0);
    }
  }

  // The last entry can be replaced: e2 0x2000/3000, names align(0x2BB8) = 0x3000, end 0x3800.
  {
    const std::vector<uint8_t> last = Bytes(4, 3000);
    const auto o = Overlay::Build(Mem(base), {{2, Mem(last)}}, &error);
    CHECK(o != nullptr);
    if (o) {
      const std::vector<uint8_t> b = ReadAll(*o);
      CHECK(o->size() == 0x3800);
      CHECK(Get32(b, 24) == 0x2000 && Get32(b, 28) == 3000 && Get32(b, 32) == 0x3000);
      CHECK(std::memcmp(&b[0x2000], last.data(), last.size()) == 0);
      CHECK(o->ReplacedEntryAt(0x2000) == 2 && o->ReplacedEntryAt(0x3000) == -1);
      CHECK(std::memcmp(&b[0x3000 + 96], "e2.bin", 6) == 0);
    }
  }

  // Bytes the rewrite does not own are kept: the gap between the TOC and entry 0, and the parts of
  // a name record other than its last word.
  {
    std::vector<uint8_t> a = base;
    a[40] = 0xAB;
    a[0x7FF] = 0xCD;
    a[0x2800 + 48 + 33] = 0x5A;
    const auto same = Overlay::Build(Mem(a), {}, &error);
    CHECK(same != nullptr && ReadAll(*same) == a);
    const auto grown = Overlay::Build(Mem(a), {{1, Mem(Bytes(9, 5000))}}, &error);
    CHECK(grown != nullptr);
    if (grown) {
      const std::vector<uint8_t> b = ReadAll(*grown);
      CHECK(b[40] == 0xAB && b[0x7FF] == 0xCD && b[0x3000 + 48 + 33] == 0x5A);
    }
  }

  // Entry 0 may start right at the end of the TOC (8 + 8 * 3 + 8 = 40) but not before it.
  {
    std::vector<uint8_t> a = base;
    Put32(a, 8, 39);
    ExpectBuildError(a, {}, "entry 0 overlaps the TOC");
    Put32(a, 8, 40);
    const auto o = Overlay::Build(Mem(a), {}, &error);
    CHECK(o != nullptr);
    if (o) {
      // e0 40/100, e1 align(140) = 0x800/3000, e2 align(0x13B8) = 0x1800/10, names 0x2000, end 0x2800.
      CHECK(o->entry_offset(0) == 40 && o->entry_offset(1) == 0x800 && o->entry_offset(2) == 0x1800);
      CHECK(o->size() == 0x2800);
      const std::vector<uint8_t> b = ReadAll(*o);
      CHECK(Get32(b, 8) == 40 && Get32(b, 16) == 0x800 && Get32(b, 24) == 0x1800);
      CHECK(std::memcmp(&b[0x800], Bytes(1, 3000).data(), 3000) == 0);
    }
  }

  // The entry-count limit is 65536: 65537 is refused, 65536 is served. The big archive has zero-size
  // entries (all at `first`) and a 48-bytes-per-entry name table right after.
  {
    std::vector<uint8_t> a = base;
    Put32(a, 4, 0x10001);
    ExpectBuildError(a, {}, "bad entry count 65537");
    const uint32_t n = 0x10000;
    const uint32_t first = 0x80800;  // align(8 + 8 * n + 8)
    const uint32_t names_size = 48 * n;
    std::vector<uint8_t> big(size_t(first) + names_size, 0);
    std::memcpy(big.data(), "AFS\0", 4);
    Put32(big, 4, n);
    for (uint32_t i = 0; i < n; ++i) Put32(big, 8 + 8 * size_t(i), first);
    Put32(big, 8 + 8 * size_t(n), first);
    Put32(big, 12 + 8 * size_t(n), names_size);
    const auto o = Overlay::Build(Mem(std::move(big)), {}, &error);
    CHECK(o != nullptr);
    if (o) CHECK(o->size() == first + uint64_t(names_size) && o->entry_offset(n - 1) == first);
  }

  // The name table must be exactly 48 bytes per entry: a long one is refused as well as a short one.
  {
    std::vector<uint8_t> a = base;
    Put32(a, 36, 192);
    ExpectBuildError(a, {}, "name table is not 48 bytes per entry");
  }

  // A name table that ends exactly at the end of the file is fine.
  {
    std::vector<uint8_t> a = base;
    a.resize(0x2890);
    const auto o = Overlay::Build(Mem(a), {}, &error);
    CHECK(o != nullptr);
    if (o) {
      CHECK(o->original_size() == 0x2890 && o->size() == 0x3000);
      CHECK(ReadAll(*o) == base);
    }
  }

  // 4 GiB: the TOC holds 32-bit offsets and sizes. One source over the limit and a total over it
  // are refused with their own messages; a total just under it is served, 64-bit offsets and all.
  {
    ExpectBuildError(base, {{1, std::make_shared<ZeroSource>(0x100000000ull)}},
                     "replacement for entry 1 is over 4 GiB");
    const uint64_t one_and_a_half_gib = 0x60000000ull;
    const auto z = [&] { return std::make_shared<ZeroSource>(one_and_a_half_gib); };
    ExpectBuildError(base, {{0, z()}, {1, z()}, {2, z()}}, "the combined archive would be over 4 GiB");
    // e0 0x800, e1 0x60000800, e2 0xC0000800, names align(0xC000080A) = 0xC0001000, end 0xC0001800.
    const auto o = Overlay::Build(Mem(base), {{0, z()}, {1, z()}}, &error);
    CHECK(o != nullptr);
    if (o) {
      CHECK(o->entry_offset(1) == 0x60000800 && o->entry_offset(2) == 0xC0000800);
      CHECK(o->size() == 0xC0001800ull);
      uint8_t e2[10], name[6];
      CHECK(o->Read(0xC0000800, e2, 10) && std::memcmp(e2, Bytes(2, 10).data(), 10) == 0);
      CHECK(o->Read(0xC0001000, name, 6) && std::memcmp(name, "e0.bin", 6) == 0);
    }
  }

  // The overlay keeps its sources alive once the caller lets go, and lets them go with itself.
  {
    int alive = 0;
    auto o = Overlay::Build(std::make_shared<CountedSource>(base, &alive),
                            {{1, std::make_shared<CountedSource>(Bytes(9, 5000), &alive)}}, &error);
    CHECK(o != nullptr && alive == 2);
    if (o) {
      const std::vector<uint8_t> b = ReadAll(*o);
      CHECK(std::memcmp(&b[0x1000], Bytes(9, 5000).data(), 5000) == 0);
      o.reset();
      CHECK(alive == 0);
    }
  }
}

// The Source contract on both sources, and FileSource's share mode: while one is open nobody can
// write to the file or delete it (a checked file cannot change under the game); once it is closed
// both are possible again, which is the control for the first half.
static void TestSources() {
  const std::vector<uint8_t> content = Bytes(5, 4096);
  CheckSourceContract(MemorySource(content), content);

  const fs::path path = WriteTemp(RunTag() + "share.bin", content);
  const TempFile cleanup{path};
  auto src = FileSource::Open(path);
  CHECK(src != nullptr);
  if (!src) return;
  CheckSourceContract(*src, content);
  const auto can_open = [&](DWORD access) {
    HANDLE h = CreateFileW(path.c_str(), access,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
  };
  CHECK(can_open(GENERIC_READ));
  CHECK(!can_open(GENERIC_WRITE));
  CHECK(!can_open(DELETE));
  src.reset();
  CHECK(can_open(GENERIC_WRITE));
  CHECK(can_open(DELETE));
}

// Byte-for-byte against afs_rebuild.py's output, when a config is given.
static void RealArchive() {
  const char* cfg = std::getenv("DOA4_AFS_REAL");
  if (!cfg || !cfg[0]) {
    std::printf("real archive: SKIPPED (DOA4_AFS_REAL unset)\n");
    return;
  }
  std::ifstream in(cfg);
  std::string line, original_path, golden_path;
  std::vector<Replacement> repl;
  while (std::getline(in, line)) {
    std::istringstream words(line);
    std::string key;
    words >> key;
    std::string rest;
    std::getline(words >> std::ws, rest);
    if (key == "original") original_path = rest;
    if (key == "golden") golden_path = rest;
    if (key == "replace") {
      std::istringstream r(rest);
      uint32_t entry = 0;
      std::string path;
      r >> entry;
      std::getline(r >> std::ws, path);
      repl.push_back({entry, FileSource::Open(fs::path(path))});
    }
  }
  auto original = FileSource::Open(fs::path(original_path));
  auto golden = FileSource::Open(fs::path(golden_path));
  CHECK(original && golden && !repl.empty());
  if (!original || !golden) return;
  const auto same = Overlay::Build(original, {}, nullptr);
  const auto o = Overlay::Build(original, repl, nullptr);
  CHECK(same && o);
  if (!same || !o) return;
  const auto equal_to = [](const Overlay& ov, const Source& want) {
    if (ov.size() != want.size()) return false;
    std::vector<uint8_t> x(1 << 22), y(1 << 22);
    for (uint64_t at = 0; at < want.size(); at += x.size()) {
      const size_t n = size_t(std::min<uint64_t>(x.size(), want.size() - at));
      if (!ov.Read(at, x.data(), n) || !want.Read(at, y.data(), n)) return false;
      if (std::memcmp(x.data(), y.data(), n) != 0) return false;
    }
    return true;
  };
  const bool identical = equal_to(*same, *original);
  const bool matches_golden = equal_to(*o, *golden);
  CHECK(identical);
  CHECK(matches_golden);
  std::printf("real archive: no replacement %s (%llu bytes); replaced %s afs_rebuild.py\n",
              identical ? "identical" : "DIFFERS", (unsigned long long)original->size(),
              matches_golden ? "identical to" : "DIFFERS from");
}

int main() {
  TestIdentity();
  TestReplaceGrows();
  TestReplaceEntry0();
  TestReadSlices();
  TestRejects();
  TestConcurrentReads();
  TestEdges();
  TestSources();
  RealArchive();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("afs_overlay: all checks passed\n");
  return 0;
}
