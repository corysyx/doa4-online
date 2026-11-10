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

namespace {

constexpr uint32_t kDeviceBytes = 0x6000;  // excluded from the device's start (Tekken 6's device block: 0x6280)

std::atomic<uint32_t> g_device{0};

struct Chunk {
  uint32_t addr = 0;  // guest address (page aligned)
  uint32_t len = 0;   // bytes (whole pages)
  size_t off = 0;     // in the buffer
};

struct Snap {
  SnapConfig cfg;
  unsigned char seen[kPages] = {};   // pages the game's code wrote since the session started
  unsigned char test[kPages] = {};   // pages written during the current test (both passes)
  unsigned char saved[kPages] = {};  // pages in the snapshot
  int phase = 0;                     // 0 waiting, 1 first pass, 2 second pass, 3 done
  uint32_t next_at = 0;
  uint32_t tests_done = 0;
  uint32_t at = 0;
  uint32_t i = 0;  // boundaries since `at` in this pass
  uint32_t dev_lo = 0, dev_hi = 0;
  std::vector<Chunk> chunks;       // the snapshot
  std::vector<uint8_t> buf;
  std::vector<Chunk> end_chunks;   // the first pass's end (snapshot pages + pages first written in the test)
  std::vector<uint8_t> end_buf;
  std::vector<uint64_t> h1, h2;    // per boundary: hash of the snapshot's pages
  std::vector<np::FrameInput> inputs;
  uint32_t outside = 0;            // pages written in the first pass that the snapshot does not hold
  uint32_t equal_tests = 0;
};

Snap& G() {
  static Snap* s = new Snap();  // ~3 MB of maps: made on first use (only in a SnapTest run)
  return *s;
}

void Out(const std::string& line) { np::LogLine("[doa4][RB] snaptest: " + line); }

double MsSince(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

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

uint64_t Hash(uint8_t* base, const std::vector<Chunk>& chunks) {
  uint64_t h = 0;
  for (const Chunk& c : chunks) h = XXH3_64bits_withSeed(base + c.addr, c.len, h);
  return h;
}

std::string Hex(const uint8_t* p, size_t n) {
  std::string t;
  for (size_t i = 0; i < n; ++i) t += fmt::format("{:02X}", p[i]);
  return t;
}

void Save(Snap& s, uint8_t* base, uint32_t frame, uint8_t fight_state) {
  const auto t0 = std::chrono::steady_clock::now();
  if (const uint32_t dev = g_device.load(std::memory_order_acquire); dev && !s.dev_hi) {
    s.dev_lo = dev >> kPageShift;
    s.dev_hi = (dev + kDeviceBytes + (1u << kPageShift) - 1) >> kPageShift;
    Out(fmt::format("the D3D device {:08X}: pages {:08X}-{:08X} stay out of the snapshot", dev, s.dev_lo << kPageShift,
                    s.dev_hi << kPageShift));
  }
  uint32_t pages = 0;
  for (uint32_t p = 0; p < kPages; ++p) {
    s.saved[p] = s.seen[p] && SnapPage(p, s.dev_lo, s.dev_hi);
    pages += s.saved[p];
  }
  const size_t bytes = Capture(base, s.saved, &s.chunks, &s.buf);
  std::memset(s.test, 0, sizeof s.test);
  s.h1.assign(1, Hash(base, s.chunks));
  s.h2.clear();
  s.inputs.clear();
  Census c = Count(s.saved, kPages);
  std::string areas;
  for (size_t a = 0; a < size_t(Area::kCount); ++a) {
    if (c.by_area[a]) areas += fmt::format("{}{} {} KB", areas.empty() ? "" : ", ", AreaName(Area(a)), c.by_area[a] * 4u);
  }
  Out(fmt::format("test {} of {}: saved the state entering frame {} (fight state {}): {} pages, {:.1f} MB in {} chunks, "
                  "{:.1f} ms ({})",
                  s.tests_done + 1, s.cfg.count, frame, fight_state, pages, double(bytes) / (1024.0 * 1024.0),
                  s.chunks.size(), MsSince(t0), areas));
}

void EndFirstPass(Snap& s, uint8_t* base) {
  // the pages the first pass wrote outside the snapshot (in the areas it covers): the snapshot cannot put them back
  std::vector<unsigned char> end(s.saved, s.saved + kPages);
  std::vector<unsigned char> outside(kPages, 0);
  s.outside = 0;
  for (uint32_t p = 0; p < kPages; ++p) {
    if (s.test[p] && !s.saved[p] && SnapPage(p, s.dev_lo, s.dev_hi)) {
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
  const auto t0 = std::chrono::steady_clock::now();
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
  Out(fmt::format("restored the state of frame {} ({:.1f} MB) in {:.1f} ms{}", s.at, double(restored) / (1024.0 * 1024.0),
                  MsSince(t0), skipped ? fmt::format("; {} KB no longer committed, not restored", skipped / 1024) : ""));
}

void Report(Snap& s, uint8_t* base) {
  std::vector<bool> equal;
  bool all = s.h1.size() == s.h2.size();
  for (size_t k = 0; k < s.h1.size() && k < s.h2.size(); ++k) {
    equal.push_back(s.h1[k] == s.h2[k]);
    all = all && equal.back();
  }
  // page by page against the first pass's end
  std::vector<unsigned char> differs(kPages, 0);
  struct First {
    uint32_t addr;
    std::string was, now;
  };
  std::vector<First> firsts;
  uint32_t pages = 0;
  for (const Chunk& c : s.end_chunks) {
    for (uint32_t o = 0; o < c.len; o += 1u << kPageShift) {
      const uint8_t* was = s.end_buf.data() + c.off + o;
      const uint8_t* now = base + c.addr + o;
      if (std::memcmp(was, now, 1u << kPageShift) == 0) continue;
      differs[(c.addr + o) >> kPageShift] = 1;
      ++pages;
      if (firsts.size() < 16) {
        uint32_t b = 0;
        while (b < (1u << kPageShift) && was[b] == now[b]) ++b;
        const uint32_t from = b & ~7u;
        const uint32_t n = std::min<uint32_t>(16, (1u << kPageShift) - from);
        firsts.push_back(First{c.addr + o + b, Hex(was + from, n), Hex(now + from, n)});
      }
    }
  }
  const bool pass = all && pages == 0;
  s.equal_tests += pass;
  Out(fmt::format("test {} of {} (frames {}-{}): {} - hashes {}; {} pages differ at the end{}", s.tests_done + 1,
                  s.cfg.count, s.at, s.at + s.cfg.frames - 1, pass ? "PASS" : "FAIL", EqualText(equal), pages,
                  s.outside ? fmt::format(" ({} pages were outside the snapshot)", s.outside) : ""));
  if (pages) {
    const Census c = Count(differs.data(), kPages);
    std::string areas;
    for (size_t a = 0; a < size_t(Area::kCount); ++a) {
      if (c.by_area[a]) areas += fmt::format("{}{} {}", areas.empty() ? "" : ", ", AreaName(Area(a)), c.by_area[a]);
    }
    Out("  differing pages by area: " + areas);
    const std::vector<Range> r = Ranges(differs.data(), kPages, 40);
    std::string line;
    for (size_t k = 0; k < r.size(); ++k) {
      line += (line.empty() ? "" : ", ") + RangeText(r[k]);
      if ((k + 1) % 8 == 0 || k + 1 == r.size()) {
        Out("  differing runs: " + line);
        line.clear();
      }
    }
    for (const First& f : firsts) {
      Out(fmt::format("  first difference in page {:08X} at {:08X}: first pass {} second {}", f.addr & ~0xFFFu, f.addr,
                      f.was, f.now));
    }
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
    std::memset(doa4_wmap, 0, sizeof doa4_wmap);  // what boot wrote before the session is not known page by page
    np::LogLine(fmt::format("[doa4][RB] snaptest: on - {} tests of {} frames, the first entering frame {}, then every {} "
                            "frames",
                            c.count, c.frames, c.at, c.every));
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
    s.h1.push_back(Hash(base, s.chunks));
    if (s.i < s.cfg.frames) {
      ++s.i;
      step.record = true;
      return step;
    }
    // the end of the first pass: keep it, put the snapshot back, run the same frames again
    EndFirstPass(s, base);
    Load(s, base);
    s.h2.assign(1, Hash(base, s.chunks));
    s.phase = 2;
    s.i = 1;
    step.rewind = true;
    step.frame = s.at;
    step.replay = true;
    step.input = &s.inputs[0];
    return step;
  }
  if (s.phase == 2) {
    s.h2.push_back(Hash(base, s.chunks));
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
