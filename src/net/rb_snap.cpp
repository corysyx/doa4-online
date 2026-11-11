// DOA4 rollback, milestone 2: the save / restore test (rb_snap.h says what it does).
#include "rb_snap.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef DOA4_RB_HOST_TEST
#include <atomic>
#include <chrono>

#include <fmt/format.h>
#include <rex/hash.h>
#include <rex/logging.h>

#include "np_session.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace doa4::rb {

namespace {

bool ParseU32(std::string_view t, uint32_t* out) {
  if (t.empty() || t.size() > 9) return false;
  uint32_t v = 0;
  for (char c : t) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + uint32_t(c - '0');
  }
  *out = v;
  return true;
}

std::string_view Trim(std::string_view t) {
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.remove_suffix(1);
  return t;
}

}  // namespace

SnapConfig ParseSnapConfig(std::string_view text, std::string* error) {
  SnapConfig c;
  text = Trim(text);
  if (text.empty() || text == "0" || text == "off") return c;
  if (text == "1" || text == "on") {
    c.on = true;
    return c;
  }
  uint32_t v[4] = {c.at, c.frames, c.every, c.count};
  size_t n = 0;
  while (true) {
    const size_t comma = text.find(',');
    const std::string_view part = Trim(text.substr(0, comma));
    if (n == 4 || !ParseU32(part, &v[n])) {
      if (error) *error = "DOA4_NP_SNAPTEST: expected at,frames[,every[,count]] (whole numbers), got '" + std::string(text) + "'";
      return SnapConfig{};
    }
    ++n;
    if (comma == std::string_view::npos) break;
    text.remove_prefix(comma + 1);
  }
  if (n < 2 || v[1] < 1 || v[1] > 120 || v[3] < 1 || v[3] > 1000) {
    if (error) *error = "DOA4_NP_SNAPTEST: needs at,frames with frames 1-120 (and count 1-1000)";
    return SnapConfig{};
  }
  c.on = true;
  c.at = v[0];
  c.frames = v[1];
  c.every = v[2];
  c.count = v[3];
  return c;
}

bool SnapPage(uint32_t page, uint32_t skip_lo, uint32_t skip_hi) {
  if (page >= skip_lo && page < skip_hi) return false;
  switch (AreaOf(page)) {
    case Area::kHeap4K:
    case Area::kHeap64K:
    case Area::kXexData:
    case Area::kXexOther:
    case Area::kHeap90:
      return page != 0;  // (page 0 is never game memory)
    default:
      return false;  // thread stacks, physical memory
  }
}

std::vector<Range> Runs(const unsigned char* map, size_t pages) {
  std::vector<Range> out;
  size_t i = 0;
  while (i < pages) {
    if (!map[i]) {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < pages && map[j]) ++j;
    out.push_back(Range{uint32_t(i), uint32_t(j - i)});
    i = j;
  }
  return out;
}

std::string EqualText(const std::vector<bool>& equal) {
  std::string text = std::to_string(equal.size()) + " boundaries:";
  size_t i = 0;
  while (i < equal.size()) {
    size_t j = i;
    while (j < equal.size() && equal[j] == equal[i]) ++j;
    text += (i ? ", " : " ") + std::to_string(i) + (j - i > 1 ? "-" + std::to_string(j - 1) : "") +
            (equal[i] ? " equal" : " differ");
    i = j;
  }
  return text;
}

#ifndef DOA4_RB_HOST_TEST

extern "C" size_t doa4_fiber_table(uint32_t* out, size_t max);  // doa4_app_hooks.cpp

namespace {

constexpr uint32_t kPage = 1u << kPageShift;
constexpr int kSettleMs = 20;  // how long a save / load watches the stopped game for memory that changes by itself

std::atomic<uint32_t> g_device{0};

// The fibers' parked positions (doa4_app_hooks.cpp): 4 words each - buffer, lr, r1, host fiber generation.
struct FiberPos {
  uint32_t buf = 0, lr = 0, r1 = 0, gen = 0;
};
std::vector<FiberPos> Fibers() {
  std::vector<uint32_t> w(4 * 256);
  const size_t n = std::min<size_t>(doa4_fiber_table(w.data(), 256), 256);
  std::vector<FiberPos> out;
  for (size_t k = 0; k < n; ++k) out.push_back(FiberPos{w[k * 4], w[k * 4 + 1], w[k * 4 + 2], w[k * 4 + 3]});
  std::sort(out.begin(), out.end(), [](const FiberPos& a, const FiberPos& b) { return a.buf < b.buf; });
  return out;
}
// "N fibers, M moved: buf lr r1 gen -> lr r1 gen, ..." between two tables.
std::string FiberText(const std::vector<FiberPos>& was, const std::vector<FiberPos>& now, uint32_t* moved_out) {
  std::string moved;
  uint32_t n = 0, added = 0, gone = 0;
  for (const FiberPos& f : now) {
    auto it = std::find_if(was.begin(), was.end(), [&](const FiberPos& x) { return x.buf == f.buf; });
    if (it == was.end()) {
      ++added;
      continue;
    }
    if (it->lr == f.lr && it->r1 == f.r1 && it->gen == f.gen) continue;
    if (++n <= 12) {
      moved += fmt::format("{}{:08X} lr {:08X} r1 {:08X} #{} -> lr {:08X} r1 {:08X} #{}", moved.empty() ? "" : "; ",
                           f.buf, it->lr, it->r1, it->gen, f.lr, f.r1, f.gen);
    }
  }
  for (const FiberPos& f : was) {
    if (std::none_of(now.begin(), now.end(), [&](const FiberPos& x) { return x.buf == f.buf; })) ++gone;
  }
  if (moved_out) *moved_out = n + added + gone;
  return fmt::format("{} fibers, {} parked elsewhere, {} new, {} gone{}{}", now.size(), n, added, gone,
                     moved.empty() ? "" : ": ", moved);
}

struct Chunk {
  uint32_t addr = 0;  // guest address (page aligned)
  uint32_t len = 0;   // bytes (whole pages)
  size_t off = 0;     // in the buffer
};

struct Keep {  // guest bytes [lo, hi) a load leaves as they are
  uint32_t lo = 0, hi = 0;
};

struct Snap {
  SnapConfig cfg;
  unsigned char seen[kPages] = {};   // pages the game's code wrote since the session started
  unsigned char test[kPages] = {};   // pages written during the current test (both passes)
  unsigned char saved[kPages] = {};  // pages in the snapshot
  unsigned char keep_page[kPages] = {};  // pages with kept bytes in them
  int phase = 0;                     // 0 waiting, 1 first pass, 2 second pass, 3 done
  uint32_t next_at = 0;
  uint32_t tests_done = 0;
  uint32_t at = 0;
  uint32_t i = 0;  // boundaries since `at` in this pass
  uint32_t dev = 0, dev_end = 0;   // the D3D device's bytes (its heap block)
  std::vector<uint32_t> live;      // words seen changing while the game stood at a boundary (sorted, unique)
  std::vector<uint32_t> live_new;  // found at a load: used from the next test on
  std::vector<Keep> keeps;         // this test's kept bytes: the device and the live words
  std::vector<Chunk> chunks;       // the snapshot
  std::vector<uint8_t> buf;
  std::vector<Chunk> end_chunks;   // the first pass's end (snapshot pages + pages first written in the test)
  std::vector<uint8_t> end_buf;
  std::vector<uint64_t> h1, h2;    // per boundary: hash of the snapshot's pages (kept bytes left out)
  std::vector<np::FrameInput> inputs;
  uint32_t outside = 0;            // pages written in the first pass that the snapshot does not hold
  bool full = false;               // DOA4_NP_SNAPTEST_MODE=full: every committed page of the areas, written or not
  std::vector<FiberPos> fib_save, fib_end1;
  uint32_t equal_tests = 0;
};

Snap& G() {
  static Snap* s = new Snap();  // ~4 MB of maps: made on first use (only in a SnapTest run)
  return *s;
}

void Out(const std::string& line) { np::LogLine("[doa4][RB] snaptest: " + line); }

double MsSince(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

uint32_t Be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

// Calls fn(addr, len) for the committed, writable parts of [addr, addr + len) of guest memory.
template <typename Fn>
void ForCommitted(uint8_t* base, uint32_t addr, uint32_t len, Fn&& fn) {
  const uint64_t end = uint64_t(addr) + len;
  uint64_t a = addr;
  while (a < end) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(base + a, &mbi, sizeof mbi)) return;
    const uint64_t region_end =
        uint64_t(reinterpret_cast<uint8_t*>(mbi.BaseAddress) - base) + uint64_t(mbi.RegionSize);
    const uint64_t b = std::min(end, region_end);
    if (b <= a) return;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (mbi.State == MEM_COMMIT && (mbi.Protect & writable) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
      fn(uint32_t(a), uint32_t(b - a));
    }
    a = b;
  }
}

// Calls fn(addr, len) for the parts of [addr, addr + len) outside the kept bytes.
template <typename Fn>
void ForUnkept(const Snap& s, uint32_t addr, uint32_t len, Fn&& fn) {
  uint64_t a = addr;
  const uint64_t end = uint64_t(addr) + len;
  auto it = std::lower_bound(s.keeps.begin(), s.keeps.end(), addr, [](const Keep& k, uint32_t x) { return k.hi <= x; });
  while (a < end) {
    if (it == s.keeps.end() || it->lo >= end) {
      fn(uint32_t(a), uint32_t(end - a));
      return;
    }
    if (it->lo > a) fn(uint32_t(a), uint32_t(it->lo - a));
    a = std::max<uint64_t>(a, it->hi);
    ++it;
  }
}

bool Kept(const Snap& s, uint32_t addr) {
  if (!s.keep_page[addr >> kPageShift]) return false;
  auto it = std::lower_bound(s.keeps.begin(), s.keeps.end(), addr, [](const Keep& k, uint32_t x) { return k.hi <= x; });
  return it != s.keeps.end() && it->lo <= addr;
}

void BuildKeeps(Snap& s) {
  s.keeps.clear();
  if (s.dev_end > s.dev) s.keeps.push_back(Keep{s.dev, s.dev_end});
  for (uint32_t w : s.live) s.keeps.push_back(Keep{w, w + 4});
  std::sort(s.keeps.begin(), s.keeps.end(), [](const Keep& a, const Keep& b) { return a.lo < b.lo; });
  std::vector<Keep> merged;
  for (const Keep& k : s.keeps) {
    if (!merged.empty() && k.lo <= merged.back().hi) merged.back().hi = std::max(merged.back().hi, k.hi);
    else merged.push_back(k);
  }
  s.keeps = std::move(merged);
  std::memset(s.keep_page, 0, sizeof s.keep_page);
  for (const Keep& k : s.keeps) {
    for (uint32_t p = k.lo >> kPageShift; p <= (k.hi - 1) >> kPageShift; ++p) s.keep_page[p] = 1;
  }
}

// Folds the pages marked since the last boundary into seen / test and clears the map.
void FoldMarks(Snap& s) {
  const uint64_t* words = reinterpret_cast<const uint64_t*>(doa4_wmap);
  for (uint32_t w = 0; w < kPages / 8; ++w) {
    if (!words[w]) continue;
    for (uint32_t k = 0; k < 8; ++k) {
      const uint32_t page = w * 8 + k;
      if (!doa4_wmap[page]) continue;
      doa4_wmap[page] = 0;
      s.seen[page] = 1;
      s.test[page] = 1;
    }
  }
}

// Copies the committed parts of the marked pages into chunks / buf.
size_t Capture(uint8_t* base, const unsigned char* map, std::vector<Chunk>* chunks, std::vector<uint8_t>* buf) {
  chunks->clear();
  size_t bytes = 0;
  for (const Range& r : Runs(map, kPages)) {
    ForCommitted(base, r.first_page << kPageShift, r.pages << kPageShift, [&](uint32_t a, uint32_t n) {
      chunks->push_back(Chunk{a, n, bytes});
      bytes += n;
    });
  }
  if (buf->size() < bytes) buf->resize(bytes);
  for (const Chunk& c : *chunks) std::memcpy(buf->data() + c.off, base + c.addr, c.len);
  return bytes;
}

uint64_t Hash(const Snap& s, uint8_t* base) {
  uint64_t h = 0;
  for (const Chunk& c : s.chunks) {
    ForUnkept(s, c.addr, c.len, [&](uint32_t a, uint32_t n) { h = XXH3_64bits_withSeed(base + a, n, h); });
  }
  return h;
}

// The words of the chunks that differ between the copy and memory now (kept bytes left out when skip_kept).
std::vector<uint32_t> ChangedWords(const Snap& s, uint8_t* base, const std::vector<Chunk>& chunks,
                                   const std::vector<uint8_t>& copy, bool skip_kept) {
  std::vector<uint32_t> out;
  for (const Chunk& c : chunks) {
    for (uint32_t o = 0; o < c.len; o += kPage) {
      const uint8_t* was = copy.data() + c.off + o;
      const uint8_t* now = base + c.addr + o;
      if (std::memcmp(was, now, kPage) == 0) continue;
      for (uint32_t w = 0; w < kPage; w += 4) {
        if (std::memcmp(was + w, now + w, 4) == 0) continue;
        const uint32_t addr = c.addr + o + w;
        if (skip_kept && Kept(s, addr)) continue;
        out.push_back(addr);
      }
    }
  }
  return out;
}

std::string PagesText(const std::vector<uint32_t>& words, size_t max) {
  std::vector<unsigned char> map(kPages, 0);
  for (uint32_t w : words) map[w >> kPageShift] = 1;
  std::string text;
  for (const Range& r : Ranges(map.data(), kPages, max)) text += (text.empty() ? "" : ", ") + RangeText(r);
  return text;
}

void AddLive(std::vector<uint32_t>* live, const std::vector<uint32_t>& words) {
  live->insert(live->end(), words.begin(), words.end());
  std::sort(live->begin(), live->end());
  live->erase(std::unique(live->begin(), live->end()), live->end());
}

uint32_t Be16(const uint8_t* p) { return (uint32_t(p[0]) << 8) | p[1]; }

// The device's heap block. The guest heap's entries (sub_82794AC0, the free path) hold the block's size at +0 and the
// previous block's at +2, in 16-byte units; the entry is 8 or 16 bytes before the device. The block is the device's
// when the next entry's previous size is this one's size.
void FindDevice(Snap& s, uint8_t* base) {
  const uint32_t dev = g_device.load(std::memory_order_acquire);
  if (!dev || s.dev) return;
  uint32_t end = 0;
  std::string how;
  for (uint32_t back : {8u, 16u}) {
    const uint32_t entry = dev - back;
    const uint32_t units = Be16(base + entry);
    const uint32_t next = entry + units * 16;
    if (units * 16 >= 0x1000 && units * 16 <= 0x40000 && Be16(base + next + 2) == units) {
      end = next;
      how = fmt::format("heap entry {:08X}: {} x 16 bytes", entry, units);
      break;
    }
  }
  if (!end) {
    end = dev + 0x4000;
    std::string t;
    for (int k = -16; k < 0; ++k) t += fmt::format("{:02X}", base[dev + k]);
    how = "a guess: no heap entry found before it (" + t + ")";
  }
  s.dev = dev;
  s.dev_end = end;
  Out(fmt::format("the D3D device {:08X}-{:08X} ({} bytes, {}) keeps its current bytes at a load", dev, end, end - dev,
                  how));
}

void Save(Snap& s, uint8_t* base, uint32_t frame, uint8_t fight_state) {
  const auto t0 = std::chrono::steady_clock::now();
  FindDevice(s, base);
  AddLive(&s.live, s.live_new);
  s.live_new.clear();
  BuildKeeps(s);
  uint32_t pages = 0;
  if (s.full) {
    // every committed, writable page of the areas (at most 768 MB), whether the game's code wrote it or not
    std::memset(s.saved, 0, sizeof s.saved);
    uint64_t total = 0;
    for (const auto& [lo, hi] : {std::pair<uint32_t, uint32_t>{0x00010000u, 0x70000000u},
                                 std::pair<uint32_t, uint32_t>{0x80000000u, 0xA0000000u}}) {
      ForCommitted(base, lo, hi - lo, [&](uint32_t a, uint32_t n) {
        for (uint32_t p = a >> kPageShift; p < (a + n) >> kPageShift; ++p) {
          if (!SnapPage(p, 0, 0) || total >= (768ull << 20)) continue;
          s.saved[p] = 1;
          total += kPage;
          ++pages;
        }
      });
    }
    if (total >= (768ull << 20)) Out("the full snapshot reached 768 MB: the rest of the committed memory is left out");
  } else {
    for (uint32_t p = 0; p < kPages; ++p) {
      s.saved[p] = s.seen[p] && SnapPage(p, 0, 0);
      pages += s.saved[p];
    }
  }
  const size_t bytes = Capture(base, s.saved, &s.chunks, &s.buf);
  const double copy_ms = MsSince(t0);
  // memory that changes while the game stands at the boundary: written by the GPU or by threads that run on (kept)
  Sleep(kSettleMs);
  const std::vector<uint32_t> moved = ChangedWords(s, base, s.chunks, s.buf, true);
  if (!moved.empty()) {
    AddLive(&s.live, moved);
    BuildKeeps(s);
    Out(fmt::format("{} words changed by themselves in {} ms at the save (kept from now on): {}", moved.size(),
                    kSettleMs, PagesText(moved, 12)));
  }
  std::memset(s.test, 0, sizeof s.test);
  s.fib_save = Fibers();
  s.h1.assign(1, Hash(s, base));
  s.h2.clear();
  s.inputs.clear();
  Census c = Count(s.saved, kPages);
  std::string areas;
  for (size_t a = 0; a < size_t(Area::kCount); ++a) {
    if (c.by_area[a]) areas += fmt::format("{}{} {} KB", areas.empty() ? "" : ", ", AreaName(Area(a)), c.by_area[a] * 4u);
  }
  Out(fmt::format("test {} of {}: saved the state entering frame {} (fight state {}): {} pages, {:.1f} MB in {} chunks, "
                  "{:.1f} ms ({}); {} kept words",
                  s.tests_done + 1, s.cfg.count, frame, fight_state, pages, double(bytes) / (1024.0 * 1024.0),
                  s.chunks.size(), copy_ms, areas, s.live.size()));
}

void EndFirstPass(Snap& s, uint8_t* base) {
  // the pages the first pass wrote outside the snapshot (in the areas it covers): the snapshot cannot put them back
  std::vector<unsigned char> end(s.saved, s.saved + kPages);
  std::vector<unsigned char> outside(kPages, 0);
  s.outside = 0;
  for (uint32_t p = 0; p < kPages; ++p) {
    if (s.test[p] && !s.saved[p] && SnapPage(p, 0, 0)) {
      outside[p] = 1;
      end[p] = 1;
      ++s.outside;
    }
  }
  Capture(base, end.data(), &s.end_chunks, &s.end_buf);
  if (s.outside) {
    std::string runs;
    const std::vector<Range> r = Ranges(outside.data(), kPages, 12);
    for (const Range& x : r) runs += (runs.empty() ? "" : ", ") + RangeText(x);
    Out(fmt::format("{} pages were first written during the first pass (not in the snapshot, not restored): {}",
                    s.outside, runs));
  }
}

void Load(Snap& s, uint8_t* base) {
  // the fibers: a fiber parked somewhere else than at the save resumes its host stack where it is now, with the
  // guest state of the save (rb_snap.h)
  s.fib_end1 = Fibers();
  uint32_t moved_fibers = 0;
  Out("fibers at the load against the save: " + FiberText(s.fib_save, s.fib_end1, &moved_fibers));
  if (s.full) {
    // pages that changed since the save although the game's code did not write them (file reads, sound, the GPU)
    const std::vector<uint32_t> words = ChangedWords(s, base, s.chunks, s.buf, true);
    std::vector<uint32_t> unmarked;
    for (uint32_t w : words) {
      if (!s.test[w >> kPageShift]) unmarked.push_back(w);
    }
    Out(fmt::format("{} words in pages the game's code did not write changed since the save{}{}", unmarked.size(),
                    unmarked.empty() ? "" : ": ", PagesText(unmarked, 16)));
  }
  const auto t0 = std::chrono::steady_clock::now();
  // the kept bytes as they are now
  std::vector<uint8_t> kept;
  for (const Keep& k : s.keeps) kept.insert(kept.end(), base + k.lo, base + k.hi);
  size_t restored = 0, skipped = 0;
  for (const Chunk& c : s.chunks) {
    size_t done = 0;
    ForCommitted(base, c.addr, c.len, [&](uint32_t a, uint32_t n) {
      std::memcpy(base + a, s.buf.data() + c.off + (a - c.addr), n);
      done += n;
    });
    restored += done;
    skipped += c.len - done;
  }
  size_t off = 0;
  for (const Keep& k : s.keeps) {
    std::memcpy(base + k.lo, kept.data() + off, k.hi - k.lo);
    off += k.hi - k.lo;
  }
  const double ms = MsSince(t0);
  Sleep(kSettleMs);
  const std::vector<uint32_t> moved = ChangedWords(s, base, s.chunks, s.buf, true);
  Out(fmt::format("restored the state of frame {} ({:.1f} MB, {} kept ranges) in {:.1f} ms{}{}", s.at,
                  double(restored) / (1024.0 * 1024.0), s.keeps.size(), ms,
                  skipped ? fmt::format("; {} KB no longer committed, not restored", skipped / 1024) : "",
                  moved.empty() ? std::string()
                                : fmt::format("; {} words changed by themselves in {} ms after it (kept from the next "
                                              "test on): {}",
                                              moved.size(), kSettleMs, PagesText(moved, 12))));
  AddLive(&s.live_new, moved);
}

void Report(Snap& s, uint8_t* base) {
  std::vector<bool> equal;
  bool all = s.h1.size() == s.h2.size();
  for (size_t k = 0; k < s.h1.size() && k < s.h2.size(); ++k) {
    equal.push_back(s.h1[k] == s.h2[k]);
    all = all && equal.back();
  }
  const std::vector<uint32_t> words = ChangedWords(s, base, s.end_chunks, s.end_buf, true);
  Out("fibers at the end of the second pass against the first: " + FiberText(s.fib_end1, Fibers(), nullptr));
  const bool pass = all && words.empty();
  s.equal_tests += pass;
  std::vector<unsigned char> differs(kPages, 0);
  for (uint32_t w : words) differs[w >> kPageShift] = 1;
  const Census c = Count(differs.data(), kPages);
  Out(fmt::format("test {} of {} (frames {}-{}): {} - hashes {}; {} words in {} pages differ at the end{}",
                  s.tests_done + 1, s.cfg.count, s.at, s.at + s.cfg.frames - 1, pass ? "PASS" : "FAIL",
                  EqualText(equal), words.size(), c.total,
                  s.outside ? fmt::format(" ({} pages were outside the snapshot)", s.outside) : ""));
  if (words.empty()) return;
  std::string areas;
  for (size_t a = 0; a < size_t(Area::kCount); ++a) {
    if (c.by_area[a]) areas += fmt::format("{}{} {}", areas.empty() ? "" : ", ", AreaName(Area(a)), c.by_area[a]);
  }
  Out("  differing pages by area: " + areas);
  Out("  differing runs: " + PagesText(words, 24));
  // the differing words by how much the second pass's value is above the first's (big-endian u32)
  struct Delta {
    uint32_t delta = 0;
    std::vector<uint32_t> addrs;
    uint32_t max_now = 0;
  };
  std::vector<Delta> deltas;
  auto value = [&](uint32_t addr, bool now) -> uint32_t {
    if (now) return Be32(base + addr);
    for (const Chunk& ch : s.end_chunks) {
      if (addr >= ch.addr && addr < ch.addr + ch.len) return Be32(s.end_buf.data() + ch.off + (addr - ch.addr));
    }
    return 0;
  };
  for (uint32_t w : words) {
    const uint32_t was = value(w, false), now = value(w, true);
    const uint32_t d = now - was;
    auto it = std::find_if(deltas.begin(), deltas.end(), [&](const Delta& x) { return x.delta == d; });
    if (it == deltas.end()) {
      deltas.push_back(Delta{d, {}, 0});
      it = deltas.end() - 1;
    }
    it->addrs.push_back(w);
    it->max_now = std::max(it->max_now, now);
  }
  std::stable_sort(deltas.begin(), deltas.end(), [](const Delta& a, const Delta& b) { return a.addrs.size() > b.addrs.size(); });
  for (size_t k = 0; k < deltas.size() && k < 10; ++k) {
    const Delta& d = deltas[k];
    std::string ex;
    for (size_t j = 0; j < d.addrs.size() && j < 6; ++j) {
      ex += fmt::format("{}{:08X} {:08X}->{:08X}", j ? ", " : "", d.addrs[j], value(d.addrs[j], false),
                        value(d.addrs[j], true));
    }
    Out(fmt::format("  {} words +{:X} (e.g. {})", d.addrs.size(), d.delta, ex));
  }
  if (deltas.size() > 10) Out(fmt::format("  ... and {} other differences", deltas.size() - 10));
  // a counter behind the largest group: device words a little above its values (the D3D fence counter?)
  if (s.dev && deltas.front().addrs.size() >= 4) {
    std::string at;
    for (uint32_t a = s.dev; a + 4 <= s.dev_end; a += 4) {
      const uint32_t v = Be32(base + a);
      if (v >= deltas.front().max_now && v - deltas.front().max_now <= 64) {
        at += fmt::format("{}+{:X}={:08X}", at.empty() ? "" : ", ", a - s.dev, v);
      }
    }
    if (!at.empty()) Out(fmt::format("  device words just above the largest group ({:08X}): {}", deltas.front().max_now, at));
  }
}

}  // namespace

void NoteD3DDevice(uint32_t device) {
  uint32_t none = 0;
  if (device) g_device.compare_exchange_strong(none, device, std::memory_order_acq_rel);
}

bool SnapActive() {
  static const bool on = [] {
    const char* v = std::getenv("DOA4_NP_SNAPTEST");
    std::string error;
    const SnapConfig c = ParseSnapConfig(v ? v : "", &error);
    if (!error.empty()) np::LogLine("[doa4][RB] snaptest: " + error + "; off");
    if (!c.on) return false;
    Snap& s = G();
    s.cfg = c;
    s.next_at = c.at;
    const char* mode = std::getenv("DOA4_NP_SNAPTEST_MODE");
    s.full = mode && std::string(mode) == "full";
    std::memset(doa4_wmap, 0, sizeof doa4_wmap);  // what boot wrote before the session is not known page by page
    np::LogLine(fmt::format("[doa4][RB] snaptest: on - {} tests of {} frames, the first entering frame {}, then every {} "
                            "frames; snapshot of {}",
                            c.count, c.frames, c.at, c.every,
                            s.full ? "every committed page (DOA4_NP_SNAPTEST_MODE=full)" : "the pages the game's code wrote"));
    return true;
  }();
  return on;
}

SnapStep SnapBoundary(uint32_t frame, uint8_t* base, uint8_t fight_state) {
  Snap& s = G();
  FoldMarks(s);
  SnapStep step;
  if (s.phase == 0 && s.tests_done < s.cfg.count && frame == s.next_at) {
    s.at = frame;
    Save(s, base, frame, fight_state);
    s.phase = 1;
    s.i = 1;
    step.record = true;
    return step;
  }
  if (s.phase == 1) {
    s.h1.push_back(Hash(s, base));
    if (s.i < s.cfg.frames) {
      ++s.i;
      step.record = true;
      return step;
    }
    // the end of the first pass: keep it, put the snapshot back, run the same frames again
    EndFirstPass(s, base);
    Load(s, base);
    s.h2.assign(1, Hash(s, base));
    s.phase = 2;
    s.i = 1;
    step.rewind = true;
    step.frame = s.at;
    step.replay = true;
    step.input = &s.inputs[0];
    return step;
  }
  if (s.phase == 2) {
    s.h2.push_back(Hash(s, base));
    if (s.i < s.cfg.frames) {
      step.replay = true;
      step.input = &s.inputs[s.i];
      ++s.i;
      return step;
    }
    Report(s, base);
    ++s.tests_done;
    s.phase = 0;
    s.next_at = frame + s.cfg.every;
    if (s.tests_done == s.cfg.count) {
      Out(fmt::format("done: {} of {} tests passed", s.equal_tests, s.cfg.count));
      s.phase = 3;
    }
  }
  return step;
}

void SnapRecordInput(const np::FrameInput& input) { G().inputs.push_back(input); }

#endif  // DOA4_RB_HOST_TEST

}  // namespace doa4::rb
