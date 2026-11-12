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
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <rex/system/xobject.h>
#include <rex/system/xthread.h>

#include <map>

#include "np_det.h"
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
// The D3D device: sub_82765790 allocates it with sub_827590E0(20608 bytes, 128-byte aligned) and sub_827640A0 /
// sub_8275A4F0 set it up. Its CPU fence counter is at +10396 (sub_82759CE8, the fence insert, adds 2 per fence and stamps
// the value into the resources it touches); +10384 points at the word the GPU writes each finished fence to.
constexpr uint32_t kDeviceSize = 20608;
constexpr uint32_t kDevStateBegin = 12, kDevStateEnd = 10380;  // the shader constants and render state (rounds 4-6)
constexpr uint32_t kFenceCounter = 10396;
constexpr uint32_t kFenceWriteback = 10384;

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

// A kernel object at the save. The kernel (events, semaphores, threads, files) lives in the host, outside guest
// memory, so the snapshot does not hold it: a guest dispatcher header (an event or a critical section's waiter) carries
// only a stash of its host object's handle ("REX\0" + handle at +8 / +12, xobject.cpp GetNativeObject).
struct KObj {
  const void* ptr = nullptr;
  uint32_t type = 0;
  int32_t state = -1;  // events: signalled 1 / not 0
};

// A guest thread's stack at the save. Stacks (0x70000000-0x7EFFFFFF) are not in the snapshot's areas, yet a thread
// parked at the frame boundary keeps locals there from one frame to the next (the job workers hand pointers into
// their own frames to the work they run): the main thread's and the job workers' stacks are saved with the snapshot.
struct StackSave {
  const void* thread = nullptr;
  uint32_t tid = 0, start = 0, low = 0, high = 0, r1 = 0;
  bool main = false;
  bool parked = false;           // another guest thread, held in a system call for the save / load
  uint32_t lr = 0;               // its guest lr while held
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> ctx;      // its PPC context (held threads and job workers)
};

// A guest thread other than the main thread and the job workers (the game's middleware threads, the kernel's
// callbacks): it runs on through a boundary, so the save and the load hold it while it waits in a system call -
// suspended with its host rip just after a syscall instruction, where it holds no user-mode lock and no guest state in
// flight. Round 12: with all of guest memory, the stacks and the kernel objects in the snapshot the frame 4096 crash
// stayed, and words kept changing during the save's 20 ms settle - something ran on.
struct Held {
  const void* thread = nullptr;
  HANDLE h = nullptr;
  uint32_t tid = 0, start = 0, lr = 0;
  bool held = false;
  uint64_t rip = 0;
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
  std::vector<uint8_t> dev_save;   // the device's bytes at the save
  bool fence_rewind = false;       // DOA4_NP_SNAPTEST_FENCE=rewind: the fence counter goes back with the snapshot
  uint32_t fence_at_save = 0;
  bool dev_restore = false;        // DOA4_NP_SNAPTEST_DEVICE=restore or state: the device goes back too (some of it)
  bool dev_state_only = false;     // =state: only its render-state shadow +12..+10380 goes back; the ring and fence
                                   // half after it stays live (round 4's stable handling), the fence counter goes back
  std::vector<uint32_t> dev_extra; // DOA4_NP_SNAPTEST_DEVLIVE: more device offsets kept live in restore mode
  std::vector<uint32_t> dev_live;  // restore mode: the device offsets kept live in this test
  bool dev_listed = false;         // the full list of changed device words was logged once
  std::map<uint32_t, struct KObj> kobj_save;  // the kernel's objects at the save, by handle
  std::vector<StackSave> stacks;  // the thread stacks at the save
  std::vector<Held> held;          // the other guest threads, held during a save / load
  bool hold = true;                // DOA4_NP_SNAPTEST_HOLD=off: leave the other threads running
  unsigned char phys_map[kPages] = {};  // physical pages (0xA0000000-0xDFFFFFFF) the game's code wrote, at the save
  std::vector<Chunk> phys_chunks;  // their contents at the save (outside the snapshot proper: reported at a load)
  std::vector<uint8_t> phys_buf;
  bool phys_restore = false;       // DOA4_NP_SNAPTEST_PHYS=restore: they go back with the snapshot too
  bool stacks_fix = true;          // DOA4_NP_SNAPTEST_STACKS=leave: only report the stacks, put none back
  bool kobj_fix = true;            // DOA4_NP_SNAPTEST_KOBJ=leave: only report the kernel objects, change nothing
  std::vector<Chunk> b1_chunks;    // the first pass after its first frame (the earliest point a difference can show)
  std::vector<uint8_t> b1_buf;
};

Snap& G() {
  static Snap* s = new Snap();  // ~4 MB of maps: made on first use (only in a SnapTest run)
  return *s;
}

// While other guest threads are held (HoldOthers), lines wait here: a held thread may have been stopped inside the
// logger's own write.
std::vector<std::string>* g_defer = nullptr;

void Out(const std::string& line) {
  if (g_defer) g_defer->push_back(line);
  else np::LogLine("[doa4][RB] snaptest: " + line);
}

void FlushDeferred() {
  std::vector<std::string>* d = g_defer;
  g_defer = nullptr;
  if (!d) return;
  for (const std::string& l : *d) Out(l);
  delete d;
}

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
  if (s.dev_end > s.dev) s.keeps.push_back(Keep{s.dev, s.dev_end});  // restore mode writes it back after the load
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

// The device's bytes: [device, device + kDeviceSize) (the size its allocation asks for).
void FindDevice(Snap& s, uint8_t* base) {
  const uint32_t dev = g_device.load(std::memory_order_acquire);
  if (!dev || s.dev) return;
  s.dev = dev;
  s.dev_end = dev + kDeviceSize;
  const uint32_t raw = Be32(base + dev - 4);  // an aligned allocation keeps the block it came from just before it
  if (s.dev_restore) {
    std::string extra;
    for (uint32_t o : s.dev_extra) extra += fmt::format(" +{}", o);
    Out(fmt::format("the D3D device {:08X}-{:08X} ({} bytes; aligned block from {:08X}) goes back with the snapshot "
                    "(DOA4_NP_SNAPTEST_DEVICE={}) except {}{}{}; its fence counter goes back too",
                    dev, s.dev_end, kDeviceSize, raw, s.dev_state_only ? "state" : "restore",
                    s.dev_state_only ? fmt::format("outside its render state +{}..+{}", kDevStateBegin, kDevStateEnd)
                                     : std::string("its ring words (+0 +4 +8 and the words that moved with the ring)"),
                    extra.empty() ? "" : " and (DOA4_NP_SNAPTEST_DEVLIVE)", extra));
    return;
  }
  Out(fmt::format("the D3D device {:08X}-{:08X} ({} bytes; aligned block from {:08X}) keeps its current bytes at a "
                  "load{}",
                  dev, s.dev_end, kDeviceSize, raw,
                  s.fence_rewind ? " except its fence counter, which goes back with the snapshot "
                                   "(DOA4_NP_SNAPTEST_FENCE=rewind)"
                                 : ""));
}

// Restore mode, at a load: the device words that must stay as they are: the ring pointers (+0 write, +4 limit,
// +8 guarantee: the GPU reads the ring on from where it is) and every word that moved with the ring since the save
// (the same distance as +0, within 1 MB: copies of ring addresses in their 0xA/0xB/0xC/0xD forms), plus
// DOA4_NP_SNAPTEST_DEVLIVE. A ring address looks like a small negative float (0xBE...), so the value alone cannot tell.
void ClassifyDeviceLive(Snap& s, uint8_t* base) {
  s.dev_live.clear();
  if (!s.dev || !s.dev_restore || s.dev_save.size() != kDeviceSize) return;
  const uint32_t delta = Be32(base + s.dev) - Be32(s.dev_save.data());
  for (uint32_t o = 0; o + 4 <= kDeviceSize; o += 4) {
    const uint32_t was = Be32(s.dev_save.data() + o), now = Be32(base + s.dev + o);
    const int32_t off = int32_t(now - was - delta);
    const bool moved = was != now && delta != 0 && off > -(1 << 20) && off < (1 << 20);
    const bool outside_state = s.dev_state_only && (o < kDevStateBegin || o >= kDevStateEnd);
    if (o <= 8 || (!s.dev_state_only && moved) || outside_state || std::find(s.dev_extra.begin(), s.dev_extra.end(), o) != s.dev_extra.end()) {
      s.dev_live.push_back(o);
    }
  }
}

std::string DevLiveText(const Snap& s, uint8_t* base) {
  std::string t;
  for (uint32_t o : s.dev_live) t += fmt::format(" +{}={:08X}", o, Be32(base + s.dev + o));
  return t;
}

uint8_t* GuestPtr(uint8_t* base, uint32_t addr) { return base + addr + (addr >= 0xE0000000u ? 0x1000u : 0u); }

struct Fence {
  uint32_t issued = 0;     // the device's counter: the next fence (the last one issued is 2 below)
  uint32_t wb_addr = 0;    // where the GPU writes the last finished fence
  uint32_t finished = 0;
  bool idle() const { return !wb_addr || int32_t(issued - finished) <= 2; }
};
Fence ReadFence(const Snap& s, uint8_t* base) {
  Fence f;
  if (!s.dev) return f;
  f.issued = Be32(base + s.dev + kFenceCounter);
  f.wb_addr = Be32(base + s.dev + kFenceWriteback);
  if (f.wb_addr) f.finished = Be32(GuestPtr(base, f.wb_addr));
  return f;
}
// Waits (up to max_ms) until the GPU has finished every fence the device issued. Returns the ms waited, -1 on timeout.
double WaitGpu(const Snap& s, uint8_t* base, int max_ms) {
  const auto t0 = std::chrono::steady_clock::now();
  while (!ReadFence(s, base).idle()) {
    if (MsSince(t0) >= max_ms) return -1;
    Sleep(0);
  }
  return MsSince(t0);
}

void Put32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}

// The value a copy holds at addr (0 when the copy does not hold it).
uint32_t CopyValue(const std::vector<Chunk>& chunks, const std::vector<uint8_t>& buf, uint32_t addr) {
  auto it = std::upper_bound(chunks.begin(), chunks.end(), addr, [](uint32_t a, const Chunk& c) { return a < c.addr; });
  if (it == chunks.begin()) return 0;
  --it;
  if (addr < it->addr || addr + 4 > it->addr + it->len) return 0;
  return Be32(buf.data() + it->off + (addr - it->addr));
}

// "ADDR old->new, ..." for the first `max` words.
std::string WordsText(uint8_t* base, const std::vector<uint32_t>& words, const std::vector<Chunk>& chunks,
                      const std::vector<uint8_t>& buf, size_t max) {
  std::string t;
  for (size_t k = 0; k < words.size() && k < max; ++k) {
    t += fmt::format("{}{:08X} {:08X}->{:08X}", k ? ", " : "", words[k], CopyValue(chunks, buf, words[k]),
                     Be32(base + words[k]));
  }
  if (words.size() > max) t += fmt::format(", ... ({} more)", words.size() - max);
  return t;
}

// The snapshot's pages and the pages written so far in this test (in the areas the snapshot covers), copied now.
void CaptureTestPages(Snap& s, uint8_t* base, std::vector<Chunk>* chunks, std::vector<uint8_t>* buf, uint32_t* outside) {
  std::vector<unsigned char> map(s.saved, s.saved + kPages);
  uint32_t n = 0;
  for (uint32_t p = 0; p < kPages; ++p) {
    if (s.test[p] && !s.saved[p] && SnapPage(p, 0, 0)) {
      map[p] = 1;
      ++n;
    }
  }
  if (outside) *outside = n;
  Capture(base, map.data(), chunks, buf);
}

// The stacks of the main thread and the job workers: [stack end, stack base) from each thread's guest PCR.
bool InSyscall(uint64_t rip) {
  if (rip < 2) return false;
  MEMORY_BASIC_INFORMATION mbi = {};
  if (VirtualQuery(reinterpret_cast<const void*>(rip - 2), &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
  if (mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE))) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(rip - 2);
  return p[0] == 0x0F && p[1] == 0x05;
}

// Holds every guest thread but the main thread and the job workers, each once it waits in a system call (up to
// 300 ms in all); the ones that never do run on and are reported.
void HoldOthers(Snap& s, const char* when) {
  s.held.clear();
  if (!g_defer) g_defer = new std::vector<std::string>();
  auto* ks = rex::system::kernel_state();
  if (!ks || !s.hold) return;
  auto* me = rex::system::XThread::GetCurrentThread();
  for (auto& o : ks->object_table()->GetAllObjects()) {
    if (!o || o->type() != rex::system::XObject::Type::Thread) continue;
    auto* t = static_cast<rex::system::XThread*>(o.get());
    if (t == me || !t->is_guest_thread() || !t->thread() || !t->thread_state() || !t->thread_state()->context()) continue;
    const uint32_t start = t->creation_params()->start_address;
    if (np::det::IsWorkerEntry(start)) continue;
    bool dup = false;
    for (const auto& e : s.held) dup = dup || e.thread == t;
    if (dup) continue;
    Held h;
    h.thread = t;
    h.h = static_cast<HANDLE>(t->thread()->native_handle());
    h.tid = t->thread_id();
    h.start = start;
    s.held.push_back(h);
  }
  const auto t0 = std::chrono::steady_clock::now();
  for (;;) {
    bool all = true;
    for (Held& h : s.held) {
      if (h.held || !h.h) continue;
      if (SuspendThread(h.h) == DWORD(-1)) {
        h.h = nullptr;
        continue;
      }
      CONTEXT c = {};
      c.ContextFlags = CONTEXT_CONTROL;
      if (GetThreadContext(h.h, &c) && InSyscall(c.Rip)) {
        h.held = true;
        h.rip = c.Rip;
        h.lr = uint32_t(static_cast<rex::system::XThread*>(const_cast<void*>(h.thread))->thread_state()->context()->lr);
        continue;
      }
      h.rip = c.Rip;
      ResumeThread(h.h);
      all = false;
    }
    if (all || MsSince(t0) > 300.0) break;
    Sleep(1);
  }
  std::string text, running;
  uint32_t n = 0;
  const uint64_t exe = uint64_t(GetModuleHandleW(nullptr));
  for (const Held& h : s.held) {
    if (h.held) {
      ++n;
      text += fmt::format(" {}({:08X}) lr {:08X}", h.tid, h.start, h.lr);
    } else {
      running += fmt::format(" {}({:08X}) rip {}{:X}", h.tid, h.start, h.rip >= exe && h.rip < exe + 0x10000000 ? "exe+" : "",
                             h.rip >= exe && h.rip < exe + 0x10000000 ? h.rip - exe : h.rip);
    }
  }
  Out(fmt::format("other guest threads at the {}: {} held in a system call{}{}{}{}", when, n, n ? ":" : "", text,
                  running.empty() ? "" : "; still running after 300 ms:", running));
}

void ReleaseOthers(Snap& s) {
  for (Held& h : s.held) {
    if (h.held && h.h) ResumeThread(h.h);
    h.held = false;
  }
  FlushDeferred();
}

std::vector<StackSave> GuestStacks(Snap& s, uint8_t* base) {
  std::vector<StackSave> out;
  auto* ks = rex::system::kernel_state();
  if (!ks) return out;
  auto* me = rex::system::XThread::GetCurrentThread();
  for (auto& o : ks->object_table()->GetAllObjects()) {
    if (!o || o->type() != rex::system::XObject::Type::Thread) continue;
    auto* t = static_cast<rex::system::XThread*>(o.get());
    if (!t->is_guest_thread() || !t->pcr_ptr()) continue;
    const bool main = t == me;
    const uint32_t start = t->creation_params()->start_address;
    const Held* held = nullptr;
    for (const Held& h : s.held) held = h.thread == t && h.held ? &h : held;
    if (!main && !held && !np::det::IsWorkerEntry(start)) continue;
    bool dup = false;
    for (const auto& e : out) dup = dup || e.thread == t;
    if (dup) continue;
    auto* pcr = reinterpret_cast<const rex::system::X_KPCR*>(base + t->pcr_ptr());
    StackSave st;
    st.thread = t;
    st.tid = t->thread_id();
    st.start = start;
    st.main = main;
    st.parked = held != nullptr;
    st.high = pcr->stack_base_ptr;
    st.low = pcr->stack_end_ptr;
    if (st.low >= st.high || st.high - st.low > 16u << 20 || st.low < 0x70000000u || st.high > 0x7F000000u) continue;
    if (t->thread_state() && t->thread_state()->context()) {
      st.r1 = uint32_t(t->thread_state()->context()->r1.u64);
      st.lr = uint32_t(t->thread_state()->context()->lr);
    }
    out.push_back(std::move(st));
  }
  return out;
}

void SaveStacks(Snap& s, uint8_t* base) {
  s.stacks = GuestStacks(s, base);
  for (auto& st : s.stacks) {
    st.bytes.assign(base + st.low, base + st.high);
    if (!st.main) {
      auto* c = static_cast<rex::system::XThread*>(const_cast<void*>(st.thread))->thread_state()->context();
      st.ctx.assign(reinterpret_cast<const uint8_t*>(c), reinterpret_cast<const uint8_t*>(c) + sizeof(*c));
    }
  }
}

// At a load: how many words of each saved stack changed since the save; unless DOA4_NP_SNAPTEST_STACKS=leave, the
// stacks go back. The main thread's stack goes back only if its r1 is where it was at the save (the same call
// path into this hook); a worker's only if it still exists.
void LoadStacks(Snap& s, uint8_t* base) {
  const std::vector<StackSave> now = GuestStacks(s, base);
  uint32_t threads = 0, words = 0, put_back = 0;
  std::string text;
  for (const auto& st : s.stacks) {
    const StackSave* cur = nullptr;
    for (const auto& n : now) cur = n.thread == st.thread ? &n : cur;
    if (!cur || cur->low != st.low || cur->high != st.high) {
      text += fmt::format(" tid {} gone", st.tid);
      continue;
    }
    uint32_t changed = 0, first = 0, deepest = 0;
    for (uint32_t o = 0; o + 4 <= st.bytes.size(); o += 4) {
      if (std::memcmp(base + st.low + o, st.bytes.data() + o, 4) == 0) continue;
      if (!changed) first = st.low + o;
      deepest = st.low + o;
      ++changed;
    }
    if (changed) {
      ++threads;
      words += changed;
      if (threads <= 12) {
        text += fmt::format(" tid {}{} ({:08X}) {} words {:08X}-{:08X} r1 {:08X}{}", st.tid, st.main ? " main" : "",
                            st.start, changed, first, deepest, cur->r1,
                            cur->r1 != st.r1 ? fmt::format(" (was {:08X})", st.r1) : std::string());
      }
    }
    if (!s.stacks_fix) continue;
    if (st.main && cur->r1 != st.r1) {
      text += " (main left: r1 moved)";
      continue;
    }
    if (st.parked && (!cur->parked || cur->lr != st.lr)) {
      // held somewhere else than at the save: its host call chain is another one, so its guest state cannot go back
      text += fmt::format(" (tid {} left: {})", st.tid,
                          cur->parked ? fmt::format("lr {:08X}, was {:08X}", cur->lr, st.lr) : std::string("not held"));
      continue;
    }
    std::memcpy(base + st.low, st.bytes.data(), st.bytes.size());
    if (!st.main && !st.ctx.empty() && cur->lr == st.lr) {
      auto* c = static_cast<rex::system::XThread*>(const_cast<void*>(st.thread))->thread_state()->context();
      std::memcpy(reinterpret_cast<uint8_t*>(c), st.ctx.data(), std::min(st.ctx.size(), sizeof(*c)));
    }
    ++put_back;
  }
  Out(fmt::format("thread stacks since the save: {} of {} changed, {} words{}{}{}", threads, s.stacks.size(), words,
                  text.empty() ? "" : ":", text, put_back ? fmt::format("; {} put back", put_back) : std::string()));
}

const char* KTypeName(uint32_t t) {
  using T = rex::system::XObject::Type;
  switch (T(t)) {
    case T::Event: return "event";
    case T::Semaphore: return "semaphore";
    case T::Mutant: return "mutant";
    case T::Thread: return "thread";
    case T::Timer: return "timer";
    case T::File: return "file";
    default: return "other";
  }
}

std::map<uint32_t, KObj> KernelObjects() {
  std::map<uint32_t, KObj> out;
  auto* ks = rex::system::kernel_state();
  if (!ks) return out;
  for (auto& o : ks->object_table()->GetAllObjects()) {
    if (!o) continue;
    KObj k;
    k.ptr = o.get();
    k.type = uint32_t(o->type());
    if (o->type() == rex::system::XObject::Type::Event) {
      uint32_t st = 0;
      static_cast<rex::system::XEvent*>(o.get())->Query(nullptr, &st);
      k.state = int32_t(st);
    }
    for (uint32_t h : o->handles()) out[h] = k;
  }
  return out;
}

// At a load, before the memory goes back: the kernel objects made, gone and (events) set or reset since the save.
// Unless DOA4_NP_SNAPTEST_KOBJ=leave, the events that still exist go back to their state at the save.
void KernelObjectsAtLoad(Snap& s) {
  const std::map<uint32_t, KObj> now = KernelObjects();
  std::map<std::string, uint32_t> made, gone;
  std::string made_text, gone_text, changed_text;
  uint32_t changed = 0, put_back = 0;
  for (const auto& [h, k] : now) {
    auto it = s.kobj_save.find(h);
    if (it == s.kobj_save.end() || it->second.ptr != k.ptr) {
      if (++made[KTypeName(k.type)] <= 6) made_text += fmt::format(" {} {:08X}", KTypeName(k.type), h);
      continue;
    }
    if (k.type == uint32_t(rex::system::XObject::Type::Event) && it->second.state >= 0 && it->second.state != k.state) {
      if (++changed <= 16) changed_text += fmt::format(" {:08X} {}->{}", h, it->second.state, k.state);
      if (s.kobj_fix) {
        auto* ev = static_cast<rex::system::XEvent*>(const_cast<void*>(k.ptr));
        if (it->second.state) ev->Set(0, false);
        else ev->Reset();
        ++put_back;
      }
    }
  }
  for (const auto& [h, k] : s.kobj_save) {
    auto it = now.find(h);
    if (it == now.end() || it->second.ptr != k.ptr) {
      if (++gone[KTypeName(k.type)] <= 6) gone_text += fmt::format(" {} {:08X}", KTypeName(k.type), h);
    }
  }
  auto counts = [](const std::map<std::string, uint32_t>& m) {
    std::string t;
    for (const auto& [n, c] : m) t += fmt::format("{}{} {}", t.empty() ? "" : ", ", c, n);
    return t.empty() ? std::string("none") : t;
  };
  Out(fmt::format("kernel objects since the save: made {}{}{}; gone {}{}{}; {} events set or reset{}{}{}",
                  counts(made), made_text.empty() ? "" : " (e.g.", made_text.empty() ? "" : made_text + ")",
                  counts(gone), gone_text.empty() ? "" : " (e.g.", gone_text.empty() ? "" : gone_text + ")", changed,
                  changed ? ":" : "", changed_text,
                  put_back ? fmt::format(" ({} put back to the save)", put_back) : std::string()));
}

// After the memory went back: guest dispatcher headers in the restored pages whose stash names a host object that is
// gone, or whose handle now belongs to another object, would make the game wait on (or crash in) the wrong object.
// Unless DOA4_NP_SNAPTEST_KOBJ=leave, such a stash is cleared, so the next use makes a new host object from the
// header's restored state (as at the header's first use).
void RepairStashes(Snap& s, uint8_t* base) {
  if (!rex::system::kernel_state()) return;
  const std::map<uint32_t, KObj> now = KernelObjects();
  uint32_t stashes = 0, stale = 0;
  std::string text;
  for (const Chunk& c : s.chunks) {
    for (uint32_t o = 0; o + 16 <= c.len; o += 4) {
      const uint32_t a = c.addr + o;
      if (Kept(s, a + 8)) continue;
      auto* h = reinterpret_cast<rex::system::X_DISPATCH_HEADER*>(base + a);
      if (h->wait_list_flink != rex::system::kXObjSignature) continue;
      ++stashes;
      const uint32_t handle = h->wait_list_blink;
      auto saved = s.kobj_save.find(handle);
      auto obj = now.find(handle);
      const bool ok = obj != now.end() && saved != s.kobj_save.end() && saved->second.ptr == obj->second.ptr;
      if (ok) continue;
      if (++stale <= 12) {
        text += fmt::format(" {:08X}->{:08X}{}", a, handle,
                            obj == now.end() ? " (gone)" : saved == s.kobj_save.end() ? " (made since)" : " (another object)");
      }
      if (s.kobj_fix) {
        h->wait_list_flink = 0;
        h->wait_list_blink = 0;
      }
    }
  }
  Out(fmt::format("{} kernel-object stashes in the restored memory, {} naming an object made or gone since the save{}{}{}",
                  stashes, stale, stale ? ":" : "", text,
                  stale && s.kobj_fix ? " (cleared)" : ""));
}

void Save(Snap& s, uint8_t* base, uint32_t frame, uint8_t fight_state) {
  FindDevice(s, base);
  const double gpu_ms = s.fence_rewind ? WaitGpu(s, base, 100) : 0;
  HoldOthers(s, "save");
  const auto t0 = std::chrono::steady_clock::now();
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
  // physical memory the game's code wrote: not in the snapshot (the GPU reads and writes it), copied to report
  std::memset(s.phys_map, 0, sizeof s.phys_map);
  for (uint32_t p = 0xA0000000u >> kPageShift; p < (0xE0000000u >> kPageShift); ++p) s.phys_map[p] = s.seen[p];
  const size_t phys_bytes = Capture(base, s.phys_map, &s.phys_chunks, &s.phys_buf);
  Out(fmt::format("physical memory the game's code wrote: {:.1f} MB in {} chunks copied for the load's report{}",
                  double(phys_bytes) / (1024.0 * 1024.0), s.phys_chunks.size(),
                  s.phys_restore ? " (and put back: DOA4_NP_SNAPTEST_PHYS=restore)" : ""));
  const double copy_ms = MsSince(t0);
  // memory that changes while the game stands at the boundary: written by the GPU or by threads that run on (kept)
  Sleep(kSettleMs);
  const std::vector<uint32_t> moved = ChangedWords(s, base, s.chunks, s.buf, true);
  if (!moved.empty()) {
    AddLive(&s.live, moved);
    BuildKeeps(s);
    Out(fmt::format("{} words changed by themselves in {} ms at the save (kept from now on): {}", moved.size(),
                    kSettleMs, WordsText(base, moved, s.chunks, s.buf, 16)));
  }
  if (s.dev) s.dev_save.assign(base + s.dev, base + s.dev_end);
  const Fence fence = ReadFence(s, base);
  s.fence_at_save = fence.issued;
  const uint32_t clocks = np::det::SaveClocks();
  s.kobj_save = KernelObjects();
  SaveStacks(s, base);
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
                  "{:.1f} ms ({}); {} kept words; {} thread clocks saved; fence {:08X}, the GPU finished {:08X} "
                  "(at {:08X}){}",
                  s.tests_done + 1, s.cfg.count, frame, fight_state, pages, double(bytes) / (1024.0 * 1024.0),
                  s.chunks.size(), copy_ms, areas, s.live.size(), clocks, fence.issued, fence.finished, fence.wb_addr,
                  !s.fence_rewind  ? std::string()
                  : gpu_ms < 0     ? std::string("; the GPU was still busy after 100 ms")
                                   : fmt::format("; waited {:.1f} ms for the GPU", gpu_ms)));
  ReleaseOthers(s);
}

void EndFirstPass(Snap& s, uint8_t* base) {
  // the pages the first pass wrote outside the snapshot (in the areas it covers): the snapshot cannot put them back
  CaptureTestPages(s, base, &s.end_chunks, &s.end_buf, &s.outside);
  if (s.outside) {
    std::vector<unsigned char> outside(kPages, 0);
    for (uint32_t p = 0; p < kPages; ++p) outside[p] = s.test[p] && !s.saved[p] && SnapPage(p, 0, 0);
    std::string runs;
    for (const Range& x : Ranges(outside.data(), kPages, 12)) runs += (runs.empty() ? "" : ", ") + RangeText(x);
    Out(fmt::format("{} pages were first written during the first pass (not in the snapshot, not restored): {}",
                    s.outside, runs));
  }
}

void Load(Snap& s, uint8_t* base) {
  HoldOthers(s, "load");
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
  // the device words the first pass changed (they keep their new values)
  if (s.dev && s.dev_save.size() == s.dev_end - s.dev) {
    std::string t;
    uint32_t n = 0, top = 0;
    for (uint32_t o = 0; o + 4 <= kDeviceSize; o += 4) {
      const uint32_t was = Be32(s.dev_save.data() + o), now = Be32(base + s.dev + o);
      if (was == now) continue;
      top = o;
      if (++n <= (s.dev_listed ? 48u : 100000u)) t += fmt::format("{}+{} {:08X}->{:08X}", n > 1 ? ", " : "", o, was, now);
    }
    s.dev_listed = true;
    Out(fmt::format("{} device words changed since the save (highest at +{}){}{}", n, top, n ? ": " : "", t));
  }
  const double gpu_ms = s.fence_rewind ? WaitGpu(s, base, 100) : 0;
  ClassifyDeviceLive(s, base);
  if (s.dev_restore && !s.dev_state_only) {
    Out(fmt::format("device words left as they are (they moved with the ring, or DOA4_NP_SNAPTEST_DEVLIVE): {}:{}",
                    s.dev_live.size(), DevLiveText(s, base)));
  }
  {
    // the physical pages the game's code wrote: how many words changed since the save, by page run
    std::vector<uint32_t> changed;
    for (const Chunk& c : s.phys_chunks) {
      for (uint32_t o = 0; o + 4 <= c.len; o += 4) {
        if (std::memcmp(base + c.addr + o, s.phys_buf.data() + c.off + o, 4) != 0) changed.push_back(c.addr + o);
      }
    }
    Out(fmt::format("physical memory since the save: {} words changed{}{}", changed.size(), changed.empty() ? "" : ": ",
                    PagesText(changed, 24)));
  }
  KernelObjectsAtLoad(s);
  const auto t0 = std::chrono::steady_clock::now();
  const Fence fence_before = ReadFence(s, base);  // the device's counter before the restore (restore mode puts it back)
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
  if (s.dev_restore && s.dev_save.size() == kDeviceSize) {
    // the device goes back to the save, except the words that follow the ring
    std::vector<uint32_t> live_now;
    for (uint32_t o : s.dev_live) live_now.push_back(Be32(base + s.dev + o));
    std::memcpy(base + s.dev, s.dev_save.data(), kDeviceSize);
    for (size_t k = 0; k < s.dev_live.size(); ++k) Put32(base + s.dev + s.dev_live[k], live_now[k]);
  }
  std::string fence_text;
  if (s.fence_rewind && s.dev) {
    const Fence f = fence_before;
    Put32(base + s.dev + kFenceCounter, s.fence_at_save);
    if (f.wb_addr) Put32(GuestPtr(base, f.wb_addr), s.fence_at_save - 2);
    fence_text = fmt::format("; fence {:08X} (GPU finished {:08X}{}) put back to {:08X}", f.issued, f.finished,
                             gpu_ms < 0 ? ", still busy after 100 ms" : fmt::format(" after {:.1f} ms", gpu_ms),
                             s.fence_at_save);
  }
  if (s.phys_restore) {
    for (const Chunk& c : s.phys_chunks) std::memcpy(base + c.addr, s.phys_buf.data() + c.off, c.len);
  }
  const uint32_t clocks = np::det::RestoreClocks();
  const double ms = MsSince(t0);
  RepairStashes(s, base);
  LoadStacks(s, base);
  Sleep(kSettleMs);
  const std::vector<uint32_t> moved = ChangedWords(s, base, s.chunks, s.buf, true);
  Out(fmt::format("restored the state of frame {} ({:.1f} MB, {} kept ranges, {} thread clocks) in {:.1f} ms{}{}{}",
                  s.at, double(restored) / (1024.0 * 1024.0), s.keeps.size(), clocks, ms, fence_text,
                  skipped ? fmt::format("; {} KB no longer committed, not restored", skipped / 1024) : "",
                  moved.empty() ? std::string()
                                : fmt::format("; {} words changed by themselves in {} ms after it (kept from the next "
                                              "test on): {}",
                                              moved.size(), kSettleMs, WordsText(base, moved, s.chunks, s.buf, 16))));
  AddLive(&s.live_new, moved);
  ReleaseOthers(s);
}

// Logs how memory now differs from a copy of the first pass at the same point: by area, by run and by how much the
// values moved. Returns the number of differing words.
size_t Differences(const Snap& s, uint8_t* base, const std::vector<Chunk>& chunks, const std::vector<uint8_t>& buf,
                   const std::string& when) {
  const std::vector<uint32_t> words = ChangedWords(s, base, chunks, buf, true);
  std::vector<unsigned char> differs(kPages, 0);
  for (uint32_t w : words) differs[w >> kPageShift] = 1;
  const Census c = Count(differs.data(), kPages);
  Out(fmt::format("  {}: {} words in {} pages differ", when, words.size(), c.total));
  if (words.empty()) return 0;
  std::string areas;
  for (size_t a = 0; a < size_t(Area::kCount); ++a) {
    if (c.by_area[a]) areas += fmt::format("{}{} {}", areas.empty() ? "" : ", ", AreaName(Area(a)), c.by_area[a]);
  }
  Out("    by area: " + areas);
  Out("    runs: " + PagesText(words, 24));
  // the differing words by how much the second pass's value is above the first's (big-endian u32)
  struct Delta {
    uint32_t delta = 0;
    std::vector<uint32_t> addrs;
  };
  std::vector<Delta> deltas;
  for (uint32_t w : words) {
    const uint32_t d = Be32(base + w) - CopyValue(chunks, buf, w);
    auto it = std::find_if(deltas.begin(), deltas.end(), [&](const Delta& x) { return x.delta == d; });
    if (it == deltas.end()) {
      deltas.push_back(Delta{d, {}});
      it = deltas.end() - 1;
    }
    it->addrs.push_back(w);
  }
  std::stable_sort(deltas.begin(), deltas.end(), [](const Delta& a, const Delta& b) { return a.addrs.size() > b.addrs.size(); });
  for (size_t k = 0; k < deltas.size() && k < 10; ++k) {
    const Delta& d = deltas[k];
    Out(fmt::format("    {} words +{:X} (e.g. {})", d.addrs.size(), d.delta, WordsText(base, d.addrs, chunks, buf, 6)));
  }
  if (deltas.size() > 10) Out(fmt::format("    ... and {} other differences", deltas.size() - 10));
  // every differing word in address order (the first 64): the earliest point is where a cause is still visible
  if (words.size() <= 400) Out("    words: " + WordsText(base, words, chunks, buf, 64));
  return words.size();
}

void Report(Snap& s, uint8_t* base) {
  std::vector<bool> equal;
  bool all = s.h1.size() == s.h2.size();
  for (size_t k = 0; k < s.h1.size() && k < s.h2.size(); ++k) {
    equal.push_back(s.h1[k] == s.h2[k]);
    all = all && equal.back();
  }
  Out("fibers at the end of the second pass against the first: " + FiberText(s.fib_end1, Fibers(), nullptr));
  const std::vector<uint32_t> words = ChangedWords(s, base, s.end_chunks, s.end_buf, true);
  const bool pass = all && words.empty();
  s.equal_tests += pass;
  Out(fmt::format("test {} of {} (frames {}-{}): {} - hashes {}{}", s.tests_done + 1, s.cfg.count, s.at,
                  s.at + s.cfg.frames - 1, pass ? "PASS" : "FAIL", EqualText(equal),
                  s.outside ? fmt::format(" ({} pages were outside the snapshot)", s.outside) : ""));
  if (!words.empty()) Differences(s, base, s.end_chunks, s.end_buf, fmt::format("after {} frames", s.cfg.frames));
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
    const char* fence = std::getenv("DOA4_NP_SNAPTEST_FENCE");
    s.fence_rewind = fence && std::string(fence) == "rewind";
    const char* device = std::getenv("DOA4_NP_SNAPTEST_DEVICE");
    s.dev_restore = device && (std::string(device) == "restore" || std::string(device) == "state");
    s.dev_state_only = device && std::string(device) == "state";
    if (s.dev_restore) s.fence_rewind = true;  // the counter goes back with the device: the GPU's writeback must too
    if (const char* extra = std::getenv("DOA4_NP_SNAPTEST_DEVLIVE")) {
      std::string_view rest = extra;
      while (!rest.empty()) {
        const size_t comma = rest.find(',');
        uint32_t o = 0;
        if (ParseU32(Trim(rest.substr(0, comma)), &o) && o % 4 == 0 && o < kDeviceSize) s.dev_extra.push_back(o);
        rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
      }
    }
    const char* kobj = std::getenv("DOA4_NP_SNAPTEST_KOBJ");
    s.kobj_fix = !(kobj && std::string(kobj) == "leave");
    const char* phys = std::getenv("DOA4_NP_SNAPTEST_PHYS");
    s.phys_restore = phys && std::string(phys) == "restore";
    const char* hold = std::getenv("DOA4_NP_SNAPTEST_HOLD");
    s.hold = !(hold && std::string(hold) == "off");
    const char* stk = std::getenv("DOA4_NP_SNAPTEST_STACKS");
    s.stacks_fix = !(stk && std::string(stk) == "leave");
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
    if (s.i == 1 && s.cfg.frames > 1) CaptureTestPages(s, base, &s.b1_chunks, &s.b1_buf, nullptr);
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
    if (s.i == 1 && s.cfg.frames > 1) {
      Differences(s, base, s.b1_chunks, s.b1_buf,
                  fmt::format("test {}, after the first replayed frame (frame {})", s.tests_done + 1, s.at));
    }
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
