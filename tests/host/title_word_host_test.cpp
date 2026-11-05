// Host test for src/doa4_title_word.cpp. Built and run by run_title_word_test.bat. No game art: the logo file is made here
// (six k_8 images with palettes in the game's XPR2 layout, letters as plain slanted shapes where the real logo has them).
// Optional check on the real files: DOA4_TITLE_WORD_REAL=<game folder> composes from them and writes
// DOA4_TITLE_WORD_OUT (a .tga of the word) for looking at.

#include "../../src/doa4_title_word.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

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

using namespace doa4::title_word;

namespace {

void PutBe32(std::vector<uint8_t>* b, size_t at, uint32_t v) {
  (*b)[at] = uint8_t(v >> 24);
  (*b)[at + 1] = uint8_t(v >> 16);
  (*b)[at + 2] = uint8_t(v >> 8);
  (*b)[at + 3] = uint8_t(v);
}

struct Img {
  uint32_t w, h;
};
const Img kImages[6] = {{1024, 128}, {512, 64}, {512, 64}, {1024, 64}, {1024, 64}, {512, 64}};

// The layout of spLogoN.xpr: 12 TX2D resources of 40 bytes, 4096-aligned texel bases.
struct Made {
  std::vector<uint8_t> file;
  size_t data_start = 0;
  std::vector<uint32_t> base;  // per TX2D, relative to the data section
};

uint32_t FcWord0(bool tiled) { return tiled ? 0x80000000u : 0; }

Made MakeLogoFile(const std::vector<Rgba>& logo_palette_image, uint8_t seed) {
  Made m;
  const uint32_t count = 12;
  const size_t table = 16;
  const size_t descriptors = table + 16 * count;
  const size_t header_end = (descriptors + 40 * count + 4095) / 4096 * 4096;
  uint32_t cursor = 0;
  for (uint32_t i = 0; i < count; ++i) {
    m.base.push_back(cursor);
    // As far as the tiled addresses reach: a 256x1 palette spreads over tens of kilobytes.
    const bool palette = i % 2 == 1;
    const uint32_t w = palette ? 256 : kImages[i / 2].w, h = palette ? 1 : kImages[i / 2].h, log_bpp = palette ? 2 : 0;
    uint32_t bytes = 4096;
    for (uint32_t y = 0; y < h; ++y) {
      for (uint32_t x = 0; x < w; ++x) bytes = std::max(bytes, TiledOffset(x, y, w, log_bpp) + (1u << log_bpp));
    }
    cursor += (bytes + 4095) / 4096 * 4096;
  }
  m.file.assign(header_end + cursor, 0);
  m.data_start = header_end;
  std::memcpy(m.file.data(), "XPR2", 4);
  PutBe32(&m.file, 4, uint32_t(header_end - 12));
  PutBe32(&m.file, 8, cursor);
  PutBe32(&m.file, 12, count);
  for (uint32_t i = 0; i < count; ++i) {
    const size_t e = table + 16 * i;
    std::memcpy(m.file.data() + e, "TX2D", 4);
    const size_t d = descriptors + 40 * i;
    PutBe32(&m.file, e + 4, uint32_t(d - 12));
    PutBe32(&m.file, e + 8, 40);
    const size_t fc = d + 40 - 24;
    PutBe32(&m.file, fc, FcWord0(true));
    const bool palette = i % 2 == 1;
    const uint32_t w = palette ? 256 : kImages[i / 2].w, h = palette ? 1 : kImages[i / 2].h;
    PutBe32(&m.file, fc + 4, m.base[i] | (palette ? (6u | (2u << 6)) : 2u));
    PutBe32(&m.file, fc + 8, (w - 1) | ((h - 1) << 13));
  }
  // Noise in every image and palette except the logo's, so a stray write anywhere shows.
  uint32_t r = seed + 1;
  for (size_t i = header_end; i < m.file.size(); ++i) {
    r = r * 1664525u + 1013904223u;
    m.file[i] = uint8_t(r >> 24);
  }
  // Image 0 and palette 0: a palette holding the distinct colours of the logo picture, and its indices.
  std::map<uint32_t, uint8_t> index;
  std::vector<Rgba> pal;
  std::vector<uint8_t> linear(kLogoWidth * kLogoHeight);
  for (uint32_t k = 0; k < logo_palette_image.size(); ++k) {
    const Rgba& c = logo_palette_image[k];
    const uint32_t key = uint32_t(c.r) << 24 | c.g << 16 | c.b << 8 | c.a;
    auto it = index.find(key);
    if (it == index.end()) {
      it = index.emplace(key, uint8_t(pal.size())).first;
      pal.push_back(c);
    }
    linear[k] = it->second;
  }
  for (uint32_t y = 0; y < kLogoHeight; ++y) {
    for (uint32_t x = 0; x < kLogoWidth; ++x) {
      m.file[m.data_start + m.base[0] + TiledOffset(x, y, kLogoWidth, 0)] = linear[y * kLogoWidth + x];
    }
  }
  for (uint32_t i = 0; i < 256; ++i) {
    const Rgba c = i < pal.size() ? pal[i] : Rgba{};
    const size_t at = m.data_start + m.base[1] + TiledOffset(i, 0, 256, 2);
    m.file[at] = c.a;
    m.file[at + 1] = c.r;
    m.file[at + 2] = c.g;
    m.file[at + 3] = c.b;
  }
  return m;
}

// Stand-in letters: a plain chrome-like rim and dark fill in slanted shapes where the real logo has O, L, I and E. Their
// neighbours (A before L, V between I and E, R after O) are in another colour, so a piece of one in the word shows.
std::vector<Rgba> MakeLogo() {
  std::vector<Rgba> p(kLogoWidth * kLogoHeight);
  struct Shape {
    int left, right;
    Rgba c;
  };
  const Rgba letter{200, 220, 230, 255}, neighbour{255, 0, 0, 255};
  const Shape shapes[] = {{347, 417, letter}, {430, 505, neighbour}, {525, 575, neighbour}, {586, 640, letter},
                          {651, 668, letter}, {690, 745, neighbour}, {757, 830, letter}};
  for (int y = 41; y <= 93; ++y) {
    for (const Shape& s : shapes) {
      const int shift = int(0.27 * (92 - y));
      for (int x = s.left + shift; x < s.right + shift; ++x) p[size_t(y) * kLogoWidth + x] = s.c;
    }
  }
  return p;
}

std::vector<uint8_t> ImageBytes(const Made& m, uint32_t i) {
  const size_t from = m.data_start + m.base[i];
  const size_t to = i + 1 < m.base.size() ? m.data_start + m.base[i + 1] : m.file.size();
  return std::vector<uint8_t>(m.file.begin() + from, m.file.begin() + to);
}

struct FakeGuest {
  std::map<uint32_t, uint8_t> mem;
  doa4::GuestBytes guest;
  FakeGuest() {
    guest.read = [this](uint32_t a) { auto it = mem.find(a); return it == mem.end() ? uint8_t(0) : it->second; };
    guest.write = [this](uint32_t a, uint8_t v) { mem[a] = v; };
  }
  void Be16(uint32_t a, uint16_t v) {
    mem[a] = uint8_t(v >> 8);
    mem[a + 1] = uint8_t(v);
  }
  uint16_t ReadBe16(uint32_t a) { return uint16_t(mem[a] << 8 | mem[a + 1]); }
};

void PutRecord4(FakeGuest* g) {
  const uint32_t r = kSheet3Records + 12 * 4;
  const uint16_t v[6] = {170, 8, 854, 56, 3, 4};
  for (int i = 0; i < 6; ++i) g->Be16(r + 2 * i, v[i]);
}

}  // namespace

// The formula the tools and the suit labels use, pinned on values worked by hand from it at a tile boundary.
static void TestTiling() {
  CHECK(TiledOffset(0, 0, 1024, 0) == 0);
  // From another implementation of the formula (tex_png.py's tiled_offset, which decoded the real logo correctly), not this one.
  CHECK(TiledOffset(1, 0, 1024, 0) == 1);
  CHECK(TiledOffset(7, 3, 1024, 0) == 31);
  CHECK(TiledOffset(100, 9, 1024, 0) == 1940);
  CHECK(TiledOffset(33, 17, 1024, 0) == 2577);
  CHECK(TiledOffset(300, 40, 1024, 0) == 41924);
  CHECK(TiledOffset(1023, 63, 1024, 0) == 65407);
  CHECK(TiledOffset(1, 0, 256, 2) == 4);
  CHECK(TiledOffset(9, 0, 256, 2) == 68);
  CHECK(TiledOffset(128, 0, 256, 2) == 16384);
  CHECK(TiledOffset(255, 0, 256, 2) == 28908);
  std::set<uint32_t> seen;
  for (uint32_t y = 0; y < 64; ++y) {
    for (uint32_t x = 0; x < 1024; ++x) seen.insert(TiledOffset(x, y, 1024, 0));
  }
  CHECK(seen.size() == 1024u * 64u);  // one to one: no two texels share a byte
  std::set<uint32_t> palette;
  for (uint32_t x = 0; x < 256; ++x) palette.insert(TiledOffset(x, 0, 256, 2));
  CHECK(palette.size() == 256);
}

// The word: the letters are there, in order, at the size the design says; nothing of the neighbours came along.
static void TestWord() {
  const Word w = ComposeWord(MakeLogo());
  CHECK(w.width >= 400 && w.width <= 460);  // the approved mock-up is 429 wide
  CHECK(w.height >= 56 && w.height <= kSlotHeight);
  CHECK(w.pixels.size() == size_t(w.width) * w.height);
  if (w.pixels.empty()) return;
  int red = 0, solid = 0;
  for (const Rgba& p : w.pixels) {
    if (p.r > 200 && p.g < 60 && p.a > 40) ++red;
    if (p.a > 200) ++solid;
  }
  CHECK(red == 0);
  CHECK(solid > int(w.width) * int(w.height) / 4);
  // Six letters: walking along a row just above the baseline, solid runs separated by see-through gaps. The stems of the
  // Ns and the foot of the L all reach this row, the I and the E too.
  const uint32_t row = w.height * 3 / 4;
  int runs = 0;
  bool in = false;
  for (uint32_t x = 0; x < w.width; ++x) {
    const bool on = w.pixels[size_t(row) * w.width + x].a > 200;
    if (on && !in) ++runs;
    in = on;
  }
  CHECK(runs >= 6);
  // Every N is as wide as the letters around it: built, so it is the composer's own number that could drift.
  CHECK(ComposeWord(MakeLogo()).pixels.size() == w.pixels.size());
}

// Two players must get the same bytes.
static void TestDeterministic() {
  const Made a = MakeLogoFile(MakeLogo(), 7);
  std::vector<uint8_t> one = a.file, two = a.file;
  uint32_t w1 = 0, h1 = 0, w2 = 0, h2 = 0;
  CHECK(PutWordInLogoFile(&one, &w1, &h1).empty());
  CHECK(PutWordInLogoFile(&two, &w2, &h2).empty());
  CHECK(one == two);
  CHECK(w1 == w2 && h1 == h2 && w1 > 0);
  CHECK(one != a.file);
}

// Only image 4 and its palette change; the other five images and everything in the header stay byte for byte.
static void TestOnlyTheSlotChanges() {
  const Made m = MakeLogoFile(MakeLogo(), 11);
  Made out = m;
  uint32_t w = 0, h = 0;
  CHECK(PutWordInLogoFile(&out.file, &w, &h).empty());
  CHECK(out.file.size() == m.file.size());
  CHECK(std::equal(m.file.begin(), m.file.begin() + m.data_start, out.file.begin()));
  for (uint32_t i = 0; i < 12; ++i) {
    if (i == 8 || i == 9) continue;
    CHECK(ImageBytes(m, i) == ImageBytes(out, i));
  }
  CHECK(ImageBytes(m, 8) != ImageBytes(out, 8));
  CHECK(ImageBytes(m, 9) != ImageBytes(out, 9));
}

// Reading the slot back as the game does gives the word; outside the word the image is see-through; the palette's alpha
// order is ascending so a filter that blends neighbouring indices stays between neighbouring looks.
static void TestSlotReadsBack() {
  const Made m = MakeLogoFile(MakeLogo(), 13);
  Made out = m;
  uint32_t w = 0, h = 0;
  CHECK(PutWordInLogoFile(&out.file, &w, &h).empty());
  const Word word = ComposeWord(MakeLogo());
  CHECK(w == word.width && h == word.height);
  Rgba pal[256];
  for (uint32_t i = 0; i < 256; ++i) {
    const size_t at = out.data_start + out.base[9] + TiledOffset(i, 0, 256, 2);
    pal[i] = Rgba{out.file[at + 1], out.file[at + 2], out.file[at + 3], out.file[at]};
  }
  CHECK(pal[0].a == 0);
  int outside = 0, far_off = 0;
  for (uint32_t y = 0; y < kSlotHeight; ++y) {
    for (uint32_t x = 0; x < kSlotWidth; ++x) {
      const Rgba c = pal[out.file[out.data_start + out.base[8] + TiledOffset(x, y, kSlotWidth, 0)]];
      if (x >= w || y >= h) {
        if (c.a != 0) ++outside;
      } else {
        const Rgba& want = word.pixels[size_t(y) * w + x];
        if (want.a > 16 && (std::abs(int(c.r) - want.r) > 40 || std::abs(int(c.g) - want.g) > 40 ||
                            std::abs(int(c.b) - want.b) > 40 || std::abs(int(c.a) - want.a) > 40)) {
          ++far_off;
        }
      }
    }
  }
  CHECK(outside == 0);
  CHECK(far_off == 0);
}

// Anything unexpected: a reason, and the file exactly as it was.
static void TestRefusals() {
  const Made good = MakeLogoFile(MakeLogo(), 3);
  const auto refused = [](std::vector<uint8_t> file, const char* what) {
    const std::vector<uint8_t> before = file;
    uint32_t w = 0, h = 0;
    const std::string why = PutWordInLogoFile(&file, &w, &h);
    if (why.empty()) std::printf("FAIL not refused: %s\n", what);
    CHECK(!why.empty());
    CHECK(file == before);
  };
  refused({}, "empty");
  refused({'X', 'P', 'R', '2'}, "short");
  std::vector<uint8_t> f = good.file;
  f[0] = 'Y';
  refused(f, "magic");
  f = good.file;
  f.pop_back();
  refused(f, "sizes do not add up");
  const size_t descriptors = 16 + 16 * 12;
  const auto fc = [&](uint32_t i, uint32_t word) { return descriptors + 40 * i + 16 + 4 * word; };
  f = good.file;
  PutBe32(&f, 12, 10);
  refused(f, "too few resources");
  f = good.file;
  PutBe32(&f, fc(0, 2), 511u | (127u << 13));
  refused(f, "logo not 1024x128");
  f = good.file;
  PutBe32(&f, fc(8, 2), 511u | (63u << 13));
  refused(f, "slot not 1024x64");
  f = good.file;
  f[fc(8, 1) + 3] = uint8_t((f[fc(8, 1) + 3] & 0xC0) | 6);
  refused(f, "slot not k_8");
  f = good.file;
  f[fc(9, 1) + 3] = uint8_t((f[fc(9, 1) + 3] & 0xC0) | 2);
  refused(f, "palette not 8888");
  f = good.file;
  f[fc(8, 1) + 3] = uint8_t(f[fc(8, 1) + 3] | (1 << 6));
  refused(f, "slot byte swapped");
  f = good.file;
  f[fc(9, 1) + 3] = uint8_t((f[fc(9, 1) + 3] & 0x3F) | (1 << 6));
  refused(f, "palette with another byte swap");
  f = good.file;
  PutBe32(&f, fc(8, 0), 0);
  refused(f, "slot not tiled");
  f = good.file;
  PutBe32(&f, fc(8, 1), 0xFFFFF000u | 2u);
  refused(f, "slot past the data");
  f = good.file;
  PutBe32(&f, fc(8, 5), 0x1000);
  refused(f, "mip levels");
  // A logo with no picture: nothing to cut letters from.
  std::vector<Rgba> empty(kLogoWidth * kLogoHeight);
  refused(MakeLogoFile(empty, 5).file, "empty logo");
}

static void TestRecord() {
  FakeGuest g;
  PutRecord4(&g);
  CHECK(InstallRecord(g.guest, 429, 63).empty());
  const uint32_t r = kSheet3Records + 12 * 4;
  CHECK(g.ReadBe16(r) == 0 && g.ReadBe16(r + 2) == 0 && g.ReadBe16(r + 4) == 429 && g.ReadBe16(r + 6) == 63);
  CHECK(g.ReadBe16(r + 8) == 3 && g.ReadBe16(r + 10) == 4);
  // Any other record: refused, nothing written. Rect and sheet/image each.
  for (uint32_t off : {0u, 2u, 4u, 6u, 8u, 10u}) {
    FakeGuest bad;
    PutRecord4(&bad);
    bad.Be16(r + off, 999);
    const auto before = bad.mem;
    CHECK(!InstallRecord(bad.guest, 429, 63).empty());
    CHECK(bad.mem == before);
  }
  // Records 0..3 and 5 stay.
  FakeGuest other;
  PutRecord4(&other);
  other.Be16(kSheet3Records + 12 * 3, 77);
  other.Be16(kSheet3Records + 12 * 5, 88);
  CHECK(InstallRecord(other.guest, 429, 63).empty());
  CHECK(other.ReadBe16(kSheet3Records + 12 * 3) == 77 && other.ReadBe16(kSheet3Records + 12 * 5) == 88);
  // A word the record cannot name: refused.
  FakeGuest big;
  PutRecord4(&big);
  CHECK(!InstallRecord(big.guest, kSlotWidth + 1, 63).empty());
  CHECK(!InstallRecord(big.guest, 429, kSlotHeight + 1).empty());
  CHECK(!InstallRecord(big.guest, 0, 63).empty());
}

// The check that runs before anything is installed: the same verdict as InstallRecord's, and it writes nothing.
static void TestCheckRecord() {
  FakeGuest g;
  PutRecord4(&g);
  const auto before = g.mem;
  CHECK(CheckRecord(g.guest).empty());
  CHECK(g.mem == before);
  const uint32_t r = kSheet3Records + 12 * 4;
  for (uint32_t off : {0u, 2u, 4u, 6u, 8u, 10u}) {
    FakeGuest bad;
    PutRecord4(&bad);
    bad.Be16(r + off, 999);
    const auto was = bad.mem;
    CHECK(!CheckRecord(bad.guest).empty());
    CHECK(bad.mem == was);
  }
}

// A spr.afs with 20 entries in the game's layout (entries contiguous on 2048-byte boundaries, the name table last).
static std::vector<uint8_t> MakeArchive(const std::vector<uint8_t>& logo_n, const std::vector<uint8_t>& logo_w) {
  const uint32_t count = 20;
  std::vector<std::vector<uint8_t>> entries(count);
  for (uint32_t i = 0; i < count; ++i) entries[i].assign(3000 + i * 7, uint8_t(i + 1));
  entries[kSprLogoN] = logo_n;
  entries[kSprLogoW] = logo_w;
  const auto align = [](size_t v) { return (v + 2047) / 2048 * 2048; };
  std::vector<uint8_t> file(0x800, 0);
  const auto le = [&](size_t at, uint32_t v) {
    for (int k = 0; k < 4; ++k) file[at + k] = uint8_t(v >> (8 * k));
  };
  std::memcpy(file.data(), "AFS", 4);
  le(4, count);
  for (uint32_t i = 0; i < count; ++i) {
    const size_t at = file.size();
    le(8 + 8 * i, uint32_t(at));
    le(12 + 8 * i, uint32_t(entries[i].size()));
    file.insert(file.end(), entries[i].begin(), entries[i].end());
    file.resize(align(file.size()), 0);
  }
  const size_t names = file.size();
  le(8 + 8 * count, uint32_t(names));
  le(12 + 8 * count, 48 * count);
  file.resize(names + 48 * count, 0);
  for (uint32_t i = 0; i < count; ++i) {
    const std::string name = "entry" + std::to_string(i) + ".bin";
    std::memcpy(file.data() + names + 48 * i, name.data(), name.size());
    // The game's archives: a name record's last word holds TOC word i, word 0 being the entry count.
    const uint32_t word = i == 0 ? count : (i % 2 == 1 ? (uint32_t(file[8 + 8 * (i / 2)]) | uint32_t(file[9 + 8 * (i / 2)]) << 8 | uint32_t(file[10 + 8 * (i / 2)]) << 16 | uint32_t(file[11 + 8 * (i / 2)]) << 24)
                                                       : (uint32_t(file[4 + 8 * (i / 2)]) | uint32_t(file[5 + 8 * (i / 2)]) << 8 | uint32_t(file[6 + 8 * (i / 2)]) << 16 | uint32_t(file[7 + 8 * (i / 2)]) << 24));
    le(names + 48 * i + 44, word);
  }
  file.resize(align(file.size()), 0);
  return file;
}

static std::filesystem::path PutArchive(const char* folder, const std::vector<uint8_t>& archive) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "doa4_title_word_test" / folder;
  std::filesystem::create_directories(dir);
  std::ofstream f(dir / kSprArchive, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(archive.data()), std::streamsize(archive.size()));
  return dir;
}

// The whole archive route, on every run; and the line the players compare.
static void TestArchiveAndFilesLine() {
  const Made n = MakeLogoFile(MakeLogo(), 21), w = MakeLogoFile(MakeLogo(), 22);
  const std::vector<uint8_t> archive = MakeArchive(n.file, w.file);
  uint32_t width = 0, height = 0;
  std::string error;
  const auto overlay = BuildOverlay(PutArchive("good", archive), &width, &height, &error);
  CHECK(overlay != nullptr);
  if (!overlay) {
    std::printf("BuildOverlay: %s\n", error.c_str());
    return;
  }
  const Word word = ComposeWord(MakeLogo());
  CHECK(width == word.width && height == word.height);
  CHECK(overlay->size() == archive.size());
  // Same layout: an entry outside the two reads as it did; the two read as the composed files.
  std::vector<uint8_t> read(archive.size());
  CHECK(overlay->Read(0, read.data(), read.size()));
  std::vector<uint8_t> expect_n = n.file, expect_w = w.file;
  uint32_t a = 0, b = 0;
  CHECK(PutWordInLogoFile(&expect_n, &a, &b).empty());
  CHECK(PutWordInLogoFile(&expect_w, &a, &b).empty());
  CHECK(overlay->entry_size(kSprLogoN) == expect_n.size() && overlay->entry_size(kSprLogoW) == expect_w.size());
  CHECK(std::equal(expect_n.begin(), expect_n.end(), read.begin() + long(overlay->entry_offset(kSprLogoN))));
  CHECK(std::equal(expect_w.begin(), expect_w.end(), read.begin() + long(overlay->entry_offset(kSprLogoW))));
  size_t differing_elsewhere = 0;
  for (size_t i = 0; i < read.size(); ++i) {
    const bool in_n = i >= overlay->entry_offset(kSprLogoN) && i < overlay->entry_offset(kSprLogoN) + expect_n.size();
    const bool in_w = i >= overlay->entry_offset(kSprLogoW) && i < overlay->entry_offset(kSprLogoW) + expect_w.size();
    if (!in_n && !in_w && read[i] != archive[i]) ++differing_elsewhere;
  }
  CHECK(differing_elsewhere == 0);

  const std::string line = FilesLine(overlay.get());
  CHECK(line.size() == std::string("title-word=").size() + 16 + 1);
  CHECK(line.rfind("title-word=", 0) == 0 && line.back() == '\n');
  CHECK(line.find_first_not_of("0123456789abcdef", 11) == line.size() - 1);
  CHECK(FilesLine(nullptr) == "title-word=none\n");
  CHECK(line != FilesLine(nullptr));
  // The same files give the same line; a change in either logo file gives another: the two-player check depends on both.
  uint32_t ww = 0, hh = 0;
  const auto same = BuildOverlay(PutArchive("same", archive), &ww, &hh, &error);
  CHECK(same != nullptr && FilesLine(same.get()) == line);
  for (const bool change_n : {true, false}) {
    std::vector<Rgba> other = MakeLogo();
    for (Rgba& p : other) {
      if (p.a != 0) p.g = uint8_t(p.g - 40);
    }
    const Made changed = MakeLogoFile(other, change_n ? 21 : 22);
    const auto differs = BuildOverlay(PutArchive("other", MakeArchive(change_n ? changed.file : n.file, change_n ? w.file : changed.file)), &ww, &hh, &error);
    CHECK(differs != nullptr && FilesLine(differs.get()) != line);
  }

  // Refused, with a reason: no archive at all, too few entries, and each logo file unusable in turn.
  std::string why;
  CHECK(BuildOverlay(std::filesystem::temp_directory_path() / "doa4_title_word_test" / "none_here", &ww, &hh, &why) == nullptr);
  CHECK(!why.empty());
  std::vector<uint8_t> junk(5000, 7);
  for (const bool bad_n : {true, false}) {
    why.clear();
    CHECK(BuildOverlay(PutArchive("bad", MakeArchive(bad_n ? junk : n.file, bad_n ? w.file : junk)), &ww, &hh, &why) == nullptr);
    CHECK(why.find(std::string("entry ") + std::to_string(bad_n ? kSprLogoN : kSprLogoW)) != std::string::npos);
  }
}

// Below the logo by the design's distance, and it follows the logo: moved or scaled, the word goes with it.
static void TestDrawFollowsTheLogo() {
  WordDraw d = WordDrawFor(640.0f, 300.0f, 1.0f, 1.0f);
  CHECK(d.id == 0x30004);
  CHECK(d.x == 642.0f && d.y == 381.5f);
  d = WordDrawFor(100.0f, 50.0f, 1.0f, 1.0f);
  CHECK(d.x == 102.0f && d.y == 131.5f);
  d = WordDrawFor(640.0f, 300.0f, 2.0f, 0.5f);
  CHECK(d.x == 644.0f && d.y == 340.75f);
}

static void TestRealFiles() {
  const char* root = std::getenv("DOA4_TITLE_WORD_REAL");
  if (!root || !root[0]) {
    std::printf("real files: SKIPPED (DOA4_TITLE_WORD_REAL unset)\n");
    return;
  }
  uint32_t w = 0, h = 0;
  std::string error;
  const auto overlay = BuildOverlay(root, &w, &h, &error);
  if (!overlay) std::printf("real files: %s\n", error.c_str());
  CHECK(overlay != nullptr);
  std::printf("real files: word %ux%u, %s", w, h, overlay ? FilesLine(overlay.get()).c_str() : "no overlay\n");
  if (!overlay) return;
  // Read the logo file back through the overlay, as the game will.
  std::vector<uint8_t> xpr(size_t(overlay->entry_size(kSprLogoW)));
  CHECK(overlay->Read(overlay->entry_offset(kSprLogoW), xpr.data(), xpr.size()));
  const auto be = [&](size_t a) { return uint32_t(xpr[a]) << 24 | xpr[a + 1] << 16 | xpr[a + 2] << 8 | xpr[a + 3]; };
  const size_t header_end = 12 + size_t(be(4));
  const auto base = [&](uint32_t i) {
    const size_t e = 16 + 16 * i;
    const size_t fc = 12 + be(e + 4) + be(e + 8) - 24;
    return header_end + (be(fc + 4) & 0xFFFFF000u);
  };
  // In the player's files the changed bytes are the slot's texels and palette entries and nothing else, in both logo files;
  // the archive is the original everywhere outside the two entries.
  {
    const auto source = doa4::afs::FileSource::Open(std::filesystem::path(root) / kSprArchive);
    CHECK(source != nullptr);
    if (source) {
      std::string why;
      const auto toc = doa4::afs::ReadToc(*source, &why);
      CHECK(toc.has_value());
      std::set<uint64_t> allowed;
      for (const uint32_t entry : {kSprLogoN, kSprLogoW}) {
        for (uint32_t y = 0; y < kSlotHeight; ++y) {
          for (uint32_t x = 0; x < kSlotWidth; ++x) allowed.insert(toc->offsets[entry] + base(8) + TiledOffset(x, y, kSlotWidth, 0));
        }
        for (uint32_t i = 0; i < 256; ++i) {
          for (uint32_t k = 0; k < 4; ++k) allowed.insert(toc->offsets[entry] + base(9) + TiledOffset(i, 0, 256, 2) + k);
        }
      }
      std::vector<uint8_t> a(1 << 20), b(1 << 20);
      uint64_t strays = 0, changed = 0;
      for (uint64_t at = 0; at < source->size(); at += a.size()) {
        const size_t n = size_t(std::min<uint64_t>(a.size(), source->size() - at));
        CHECK(source->Read(at, a.data(), n));
        CHECK(overlay->Read(at, b.data(), n));
        for (size_t i = 0; i < n; ++i) {
          if (a[i] == b[i]) continue;
          ++changed;
          if (!allowed.count(at + i)) ++strays;
        }
      }
      std::printf("real files: %llu bytes changed, %llu outside the slot\n", (unsigned long long)changed, (unsigned long long)strays);
      CHECK(changed > 0);
      CHECK(strays == 0);
      CHECK(overlay->size() == source->size());
    }
  }
  const char* out = std::getenv("DOA4_TITLE_WORD_OUT");
  if (!out) return;
  // The slot as a picture, for looking at.
  std::ofstream f(out, std::ios::binary);
  const uint8_t tga[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, uint8_t(w), uint8_t(w >> 8), uint8_t(h), uint8_t(h >> 8), 32, 0x28};
  f.write(reinterpret_cast<const char*>(tga), 18);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const uint8_t i = xpr[base(8) + TiledOffset(x, y, kSlotWidth, 0)];
      const size_t at = base(9) + TiledOffset(i, 0, 256, 2);
      const char c[4] = {char(xpr[at + 3]), char(xpr[at + 2]), char(xpr[at + 1]), char(xpr[at])};
      f.write(c, 4);
    }
  }
}

int main() {
  TestTiling();
  TestWord();
  TestDeterministic();
  TestOnlyTheSlotChanges();
  TestSlotReadsBack();
  TestRefusals();
  TestRecord();
  TestCheckRecord();
  TestArchiveAndFilesLine();
  TestDrawFollowsTheLogo();
  TestRealFiles();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
