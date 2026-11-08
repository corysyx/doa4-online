// Host test for src/doa4_suits_labels.cpp: the extended sheet-6 sprite table (records 197-203 for the
// costume-select labels past COS. 10), the id rewrite while the costume list is drawn, and the labels
// COS. 11-17 composed into image 0 of spChrN.xpr / spChrW.xpr. Built and run by run_suits_labels_test.bat. The texture
// fixtures are synthetic (no game art). Optional real-data check: DOA4_SUITS_GAME_DIR=<the DOA4
// folder holding spr.afs> (read-only); DOA4_SUITS_LABEL_DUMP=<a folder outside the repo> also writes
// the composed image 0 there (row-major palette indices) for a preview. Prints
// "suits_labels: N checks run" before its verdict line: a run that ends a section early shows as a
// smaller N.

#include "../../src/doa4_suits_labels.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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

namespace lb = doa4::labels;

// Guest memory as a map; unwritten addresses read 0. Counts reads and records every write, so a
// check can say "nothing was read or written" and "every write landed inside the block".
struct FakeGuest {
  std::map<uint32_t, uint8_t> bytes;
  std::vector<uint32_t> writes;
  size_t reads = 0;
  doa4::GuestBytes guest{[this](uint32_t a) {
                           ++reads;
                           const auto it = bytes.find(a);
                           return it == bytes.end() ? uint8_t(0) : it->second;
                         },
                         [this](uint32_t a, uint8_t v) {
                           writes.push_back(a);
                           bytes[a] = v;
                         }};
  FakeGuest() = default;
  FakeGuest(const FakeGuest&) = delete;
  FakeGuest& operator=(const FakeGuest&) = delete;
  void Put16(uint32_t a, uint16_t v) {
    bytes[a] = uint8_t(v >> 8);
    bytes[a + 1] = uint8_t(v);
  }
  void Put32(uint32_t a, uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes[a + i] = uint8_t(v >> (24 - 8 * i));
  }
  uint32_t Get32(uint32_t a) const {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      const auto it = bytes.find(a + i);
      v = (v << 8) | (it == bytes.end() ? 0u : it->second);
    }
    return v;
  }
  std::vector<uint8_t> Range(uint32_t a, uint32_t n) const {
    std::vector<uint8_t> out;
    for (uint32_t i = 0; i < n; ++i) {
      const auto it = bytes.find(a + i);
      out.push_back(it == bytes.end() ? 0 : it->second);
    }
    return out;
  }
};

// The game's pointer word and sheet 6's table address, written here on their own (not taken from
// the module) so a changed constant in the module is a failure, not a shared mistake. From the
// image (the author's dump of the game's XEX, not in this repository): the word at 0x82C77D20 holds 0x82C706B8; sheet 7's table
// starts at 0x82C70FF4 = 0x82C706B8 + 197 * 12.
static constexpr uint32_t kWord = 0x82C77D20;
static constexpr uint32_t kTable = 0x82C706B8;
static constexpr uint32_t kCount = 197;

struct Rec {
  int16_t x0, y0, x1, y1;
  uint16_t sheet, image;
};
// Records 123..132 in the image (COS. 1 .. COS. 10).
static constexpr std::array<Rec, 10> kCos = {{
    {0, 37, 106, 69, 6, 0},   {106, 37, 212, 69, 6, 0},  {212, 37, 318, 69, 6, 0},
    {318, 37, 424, 69, 6, 0}, {0, 70, 106, 102, 6, 0},   {106, 70, 212, 102, 6, 0},
    {212, 70, 318, 102, 6, 0}, {318, 70, 424, 102, 6, 0}, {0, 103, 103, 135, 6, 0},
    {106, 103, 209, 135, 6, 0},
}};
// The blank cell (212,174) at COS. 10's size (103 x 32), on COS. 10's sheet and image.
static constexpr Rec kBlank = {212, 174, 315, 206, 6, 0};

static void PutRec(FakeGuest& g, uint32_t table, uint32_t i, const Rec& r) {
  const uint32_t a = table + 12 * i;
  g.Put16(a, uint16_t(r.x0));
  g.Put16(a + 2, uint16_t(r.y0));
  g.Put16(a + 4, uint16_t(r.x1));
  g.Put16(a + 6, uint16_t(r.y1));
  g.Put16(a + 8, r.sheet);
  g.Put16(a + 10, r.image);
}
static std::vector<uint8_t> RecBytes(const Rec& r) {
  FakeGuest g;
  PutRec(g, 0x1000, 0, r);
  return g.Range(0x1000, 12);
}

// A sheet-6 table at `table`: every record distinct (no two records, and no record and the blank
// one, share their bytes), records 123..132 the real labels, and the pointer word naming it.
static void Table(FakeGuest& g, uint32_t table) {
  for (uint32_t i = 0; i < kCount; ++i) {
    PutRec(g, table, i, {int16_t(i), int16_t(i + 1000), int16_t(-int(i)), int16_t(3 * i), 6, uint16_t(i % 30 + 1)});
  }
  for (uint32_t k = 0; k < 10; ++k) PutRec(g, table, 123 + k, kCos[k]);
  g.Put32(kWord, table);
}

// Hands out exact-size blocks with a 64-byte gap after each, records each request, or fails.
struct ExactAlloc {
  uint32_t next = 0x40001000;
  bool fail = false;
  std::vector<std::pair<uint32_t, uint32_t>> blocks;
  lb::AllocFn Fn() {
    return [this](uint32_t size) -> uint32_t {
      if (fail) {
        blocks.push_back({0, size});
        return 0;
      }
      const uint32_t a = next;
      blocks.push_back({a, size});
      next += size + 64;
      return a;
    };
  }
};

static void TestBlankRecord() {
  const lb::Record b = lb::BlankRecord();
  CHECK(b.x0 == kBlank.x0 && b.y0 == kBlank.y0 && b.x1 == kBlank.x1 && b.y1 == kBlank.y1 &&
        b.sheet == kBlank.sheet && b.image == kBlank.image);
}

// The extended table (here with the blank records): records 0..196 byte for byte the original
// (record 133, the real Record panel, included), 197..203 the given records, one exact-size block
// with every byte written, the original table untouched, and the pointer word now naming the block:
// the only write outside the block is that one word.
static constexpr uint32_t kExtended = 204;  // 197 + the seven new labels

static void TestExtendTable() {
  FakeGuest g;
  Table(g, kTable);
  const std::vector<uint8_t> original = g.Range(kTable, kCount * 12);
  ExactAlloc alloc;
  const lb::Extended e = lb::ExtendTable(g.guest, alloc.Fn(), lb::NewRecords(false));
  CHECK(e.note.empty());
  CHECK(e.original == kTable);
  CHECK(alloc.blocks.size() == 1 && alloc.blocks[0].second == kExtended * 12);
  CHECK(e.table != 0 && !alloc.blocks.empty() && e.table == alloc.blocks[0].first);
  CHECK(g.Get32(kWord) == e.table && e.table != 0);  // the pointer word names the extended table
  CHECK(g.Range(kTable, kCount * 12) == original);   // the original table is untouched
  int same = 0, blank = 0;
  for (uint32_t i = 0; i < kExtended; ++i) {
    const std::vector<uint8_t> rec = g.Range(e.table + 12 * i, 12);
    if (i < kCount) {
      same += rec == g.Range(kTable + 12 * i, 12);
    } else {
      blank += rec == RecBytes(kBlank);
    }
  }
  CHECK(same == 197);
  CHECK(blank == 7);
  // The last original record and the Record panel are kept, not replaced by a label.
  CHECK(g.Range(e.table + 12 * 196, 12) == g.Range(kTable + 12 * 196, 12) &&
        g.Range(e.table + 12 * 196, 12) != RecBytes(kBlank));
  CHECK(g.Range(e.table + 12 * 133, 12) == g.Range(kTable + 12 * 133, 12));
  std::set<uint32_t> in_block, outside;
  for (uint32_t a : g.writes) {
    if (e.table != 0 && a >= e.table && a < e.table + kExtended * 12) {
      in_block.insert(a);
    } else {
      outside.insert(a);
    }
  }
  CHECK(in_block.size() == kExtended * 12);  // every byte of the block was written
  CHECK(outside == std::set<uint32_t>({kWord, kWord + 1, kWord + 2, kWord + 3}));
}

// The original table has 197 records, the extended one 204: both edges of each.
static void TestRecordBounds() {
  FakeGuest g;
  const lb::Record r{1, 2, 3, 4, 6, 9};
  CHECK(lb::WriteRecord(g.guest, 0x40001000, kCount, 196, r));
  CHECK(g.writes.size() == 12 && g.Range(0x40001000 + 12 * 196, 12) == RecBytes({1, 2, 3, 4, 6, 9}));
  size_t before = g.writes.size();
  CHECK(!lb::WriteRecord(g.guest, 0x40001000, kCount, 197, r));
  CHECK(g.writes.size() == before);
  CHECK(lb::WriteRecord(g.guest, 0x40001000, kExtended, 203, r));
  before = g.writes.size();
  CHECK(!lb::WriteRecord(g.guest, 0x40001000, kExtended, 204, r));
  CHECK(!lb::WriteRecord(g.guest, 0x40001000, kExtended, 0xFFFFFFFFu, r));
  CHECK(g.writes.size() == before);
  CHECK(lb::ReadRecord(g.guest, 0x40001000, kCount, 196).has_value());
  CHECK(!lb::ReadRecord(g.guest, 0x40001000, kCount, 197).has_value());
  CHECK(lb::ReadRecord(g.guest, 0x40001000, kExtended, 203).has_value());
  CHECK(!lb::ReadRecord(g.guest, 0x40001000, kExtended, 204).has_value());
  // Negative coordinates survive the round trip (the fields are signed).
  CHECK(lb::WriteRecord(g.guest, 0x40002000, kCount, 0, {-5, -1, 300, 400, 6, 0}));
  const std::optional<lb::Record> back = lb::ReadRecord(g.guest, 0x40002000, kCount, 0);
  CHECK(back && back->x0 == -5 && back->y0 == -1 && back->x1 == 300 && back->y1 == 400);
}

// The original table must lie inside the image with all 197 records: both edges of both ends. A
// refusal allocates nothing, writes nothing and leaves the pointer word as it was.
static void TestPointerBounds() {
  struct Case {
    uint32_t table;
    bool extended;
  };
  const Case cases[] = {
      {0x82000000, true},                     // the first byte of the image
      {0x81FFFFFC, false},                    // one word below it
      {0x83000000 - kCount * 12, true},       // the last record ends at the image's end
      {0x83000000 - kCount * 12 + 1, false},  // one byte past it
      {0xFFFFFFF0, false},                    // the end would wrap a 32-bit sum
  };
  for (const Case& t : cases) {
    FakeGuest g;
    Table(g, t.table);
    ExactAlloc alloc;
    const lb::Extended e = lb::ExtendTable(g.guest, alloc.Fn(), lb::NewRecords(false));
    CHECK((e.table != 0) == t.extended);
    CHECK(e.note.empty() == t.extended);
    CHECK(e.original == t.table);
    CHECK(alloc.blocks.size() == (t.extended ? 1u : 0u));  // refused before anything is allocated
    CHECK(t.extended || (g.writes.empty() && g.Get32(kWord) == t.table));
  }
  FakeGuest g;
  Table(g, 0x81FFFFFC);
  ExactAlloc alloc;
  CHECK(lb::ExtendTable(g.guest, alloc.Fn(), lb::NewRecords(false)).note ==
        "the sheet-6 sprite table pointer at 0x82c77d20 is 0x81fffffc, not a 197-record table inside the image");
  FakeGuest zero;  // no pointer at all
  const lb::Extended z = lb::ExtendTable(zero.guest, alloc.Fn(), lb::NewRecords(false));
  CHECK(z.table == 0 && !z.note.empty() && zero.writes.empty());
}

// Records 123..132 must be the ten labels this fix knows: both ends of that range are checked,
// their neighbours are not.
static void TestLayoutCheck() {
  struct Case {
    uint32_t record;
    bool extended;
  };
  for (const Case& t : {Case{122, true}, Case{123, false}, Case{132, false}, Case{133, true}}) {
    FakeGuest g;
    Table(g, kTable);
    // One field changed: the width of a label, or a non-label record.
    PutRec(g, kTable, t.record, {0, 37, 107, 69, 6, 0});
    ExactAlloc alloc;
    const lb::Extended e = lb::ExtendTable(g.guest, alloc.Fn(), lb::NewRecords(false));
    CHECK((e.table != 0) == t.extended);
    CHECK(t.extended || e.note == "records 123-132 of the sheet-6 sprite table are not the COS. 1-10 labels this "
                                  "build knows (record " + std::to_string(t.record) + " differs)");
    CHECK(t.extended || (g.writes.empty() && alloc.blocks.empty() && g.Get32(kWord) == kTable));
  }
  // The image word of COS. 1 (the label's texture) is part of the check too.
  FakeGuest g;
  Table(g, kTable);
  PutRec(g, kTable, 123, {0, 37, 106, 69, 6, 1});
  ExactAlloc alloc;
  const lb::Extended e = lb::ExtendTable(g.guest, alloc.Fn(), lb::NewRecords(false));
  CHECK(e.table == 0 && e.note == "records 123-132 of the sheet-6 sprite table are not the COS. 1-10 labels this "
                                   "build knows (record 123 differs)");
}

static void TestAllocFails() {
  FakeGuest g;
  Table(g, kTable);
  ExactAlloc alloc;
  alloc.fail = true;
  const lb::Extended e = lb::ExtendTable(g.guest, alloc.Fn(), lb::NewRecords(false));
  CHECK(e.table == 0 && e.original == kTable);
  CHECK(e.note == "no guest memory for the extended sheet-6 sprite table (2448 bytes)");
  CHECK(alloc.blocks.size() == 1 && alloc.blocks[0].second == 2448);
  CHECK(g.writes.empty() && g.Get32(kWord) == kTable);
}

// The id rewrite, written here on its own: while the costume list is drawn, sheet 6's ids 133..139
// (0x60085..0x6008B, the Record panel, its edge strip and the cursor frames) become 197..203
// (0x600C5..0x600CB). Everything else, and everything while the list is not being drawn, passes.
static void TestRewriteId() {
  int mapped = 0;
  for (uint32_t k = 0; k < 7; ++k) mapped += lb::RewriteId(0x60085 + k, true) == 0x600C5 + k;
  CHECK(mapped == 7);
  CHECK(lb::RewriteId(0x60084, true) == 0x60084);  // COS. 10, just below the range
  CHECK(lb::RewriteId(0x6008C, true) == 0x6008C);  // just above it
  CHECK(lb::RewriteId(0x70085, true) == 0x70085);  // the same index on sheet 7
  CHECK(lb::RewriteId(0x160085, true) == 0x160085);  // sheet 0x16
  CHECK(lb::RewriteId(0x600C5, true) == 0x600C5);  // already a new id
  CHECK(lb::RewriteId(0x60085, false) == 0x60085);  // not drawing the list
  CHECK(lb::RewriteId(0x6008B, false) == 0x6008B);
}

// The draw struct the resolver reads its id from (+4): only that word changes, and only for an id
// in the range.
static void TestRewriteDrawStruct() {
  FakeGuest g;
  const uint32_t at = 0x7018F000;
  for (uint32_t i = 0; i < 48; ++i) g.bytes[at + i] = uint8_t(0xA0 + i);
  g.Put32(at + 4, 0x00060087);
  const std::vector<uint8_t> before = g.Range(at, 48);
  CHECK(lb::RewriteDrawStruct(g.guest, at));
  CHECK(g.Get32(at + 4) == 0x000600C7);
  std::vector<uint8_t> after = g.Range(at, 48), expect = before;
  expect[4] = 0x00, expect[5] = 0x06, expect[6] = 0x00, expect[7] = 0xC7;
  CHECK(after == expect);
  CHECK(std::set<uint32_t>(g.writes.begin(), g.writes.end()) == std::set<uint32_t>({at + 4, at + 5, at + 6, at + 7}));
  g.writes.clear();
  g.Put32(at + 4, 0x00060084);
  CHECK(!lb::RewriteDrawStruct(g.guest, at));
  CHECK(g.writes.empty() && g.Get32(at + 4) == 0x00060084);
}

// The "drawing the costume list" flag: set inside a scope, cleared after it (also on an exception),
// kept by a nested scope, and per thread.
static void TestListingScope() {
  CHECK(!lb::Listing());
  {
    lb::ListingScope outer;
    CHECK(lb::Listing());
    {
      lb::ListingScope inner;
      CHECK(lb::Listing());
    }
    CHECK(lb::Listing());  // the nested scope gives back what it found
    bool other = true;
    std::thread t([&] { other = lb::Listing(); });
    t.join();
    CHECK(!other);  // another thread is not drawing the list
  }
  CHECK(!lb::Listing());
  try {
    lb::ListingScope scope;
    throw 1;
  } catch (int) {
  }
  CHECK(!lb::Listing());
}

// ======================================================================== the art: COS. 11-17

namespace fs = std::filesystem;

// The records that show COS. 11..17, written here on their own: COS. 10's size (103 x 32), sheet 6,
// image 0, at the seven cells the research note lists as empty (costume_select_ui.md, 4(b)).
static constexpr std::array<Rec, 7> kLabels = {{
    {212, 103, 315, 135, 6, 0}, {318, 103, 421, 135, 6, 0}, {128, 136, 231, 168, 6, 0},
    {234, 136, 337, 168, 6, 0}, {340, 136, 443, 168, 6, 0}, {0, 174, 103, 206, 6, 0},
    {106, 174, 209, 206, 6, 0},
}};

static bool Same(const lb::Record& a, const Rec& b) {
  return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1 && a.sheet == b.sheet && a.image == b.image;
}

static void TestLabelRecords() {
  int same = 0;
  for (uint32_t k = 1; k <= 7; ++k) {
    const std::optional<lb::Record> r = lb::LabelRecord(k);
    same += r && Same(*r, kLabels[k - 1]);
  }
  CHECK(same == 7);
  CHECK(!lb::LabelRecord(0).has_value());
  CHECK(!lb::LabelRecord(8).has_value());
}

// The seven new records: the label cells when the art was composed, else the blank cell seven times.
static void TestNewRecords() {
  const std::array<lb::Record, 7> art = lb::NewRecords(true), blank = lb::NewRecords(false);
  int labels = 0, blanks = 0;
  for (uint32_t k = 0; k < 7; ++k) {
    labels += Same(art[k], kLabels[k]);
    blanks += Same(blank[k], kBlank);
  }
  CHECK(labels == 7);
  CHECK(blanks == 7);
}

// The installed table, described from guest memory (the pointer word and the table it names), so a
// table that was never installed, or records that never landed, show in the start-up line.
static void TestDescribeInstalled() {
  FakeGuest g;
  Table(g, kTable);
  CHECK(lb::DescribeInstalled(g.guest, kTable) ==
        "the sheet-6 sprite table pointer still names the original table (0x82c706b8)");
  ExactAlloc alloc;
  const lb::Extended e = lb::ExtendTable(g.guest, alloc.Fn(), lb::NewRecords(true));
  CHECK(e.table == 0x40001000);
  int labels = 0;
  for (uint32_t k = 0; k < 7; ++k) labels += g.Range(e.table + 12 * (197 + k), 12) == RecBytes(kLabels[k]);
  CHECK(labels == 7);
  CHECK(lb::DescribeInstalled(g.guest, kTable) ==
        "the sheet-6 sprite table pointer names 0x40001000: records 0-196 equal the original; records 197-203 "
        "show the COS. 11-17 cells");
  PutRec(g, e.table, 0, {9, 9, 9, 9, 6, 0});
  PutRec(g, e.table, 196, {9, 9, 9, 9, 6, 0});
  CHECK(lb::DescribeInstalled(g.guest, kTable) ==
        "the sheet-6 sprite table pointer names 0x40001000: 2 of records 0-196 differ from the original; records "
        "197-203 show the COS. 11-17 cells");
  PutRec(g, e.table, 203, kCos[0]);
  CHECK(lb::DescribeInstalled(g.guest, kTable) ==
        "the sheet-6 sprite table pointer names 0x40001000: 2 of records 0-196 differ from the original; records "
        "197-203 hold something else");
  FakeGuest b;
  Table(b, kTable);
  ExactAlloc balloc;
  const lb::Extended be = lb::ExtendTable(b.guest, balloc.Fn(), lb::NewRecords(false));
  CHECK(be.table != 0 && lb::DescribeInstalled(b.guest, kTable) ==
                             "the sheet-6 sprite table pointer names 0x40001000: records 0-196 equal the original; "
                             "records 197-203 show the blank cell (212,174)");
}

// tex_png.py's tiled_offset for log_bpp 0 (one byte per texel), written here on its own: the texture
// fixtures are tiled with it, so a changed formula in the module does not change the fixtures too.
static uint32_t OwnTiled(uint32_t x, uint32_t y, uint32_t width) {
  const uint32_t aw = (width + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aw >> 5)) << 7;
  const uint32_t micro = (x & 7) + ((y & 6) << 2);
  const uint32_t off = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << 3) + ((y & 1) << 4);
  return ((off & ~511u) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

// tex_png.py's tiled_offset(x, y, 512, 0), printed by running the script's function (not derived by
// hand): both sides of the 8- and 32-texel steps in x and of the 1/2/8/16/32-row steps in y, the
// last texel, and points inside the cells.
struct Pin {
  uint32_t x, y, offset;
};
static constexpr Pin kPins[] = {
    {0, 0, 0},     {1, 0, 1},         {7, 0, 7},       {8, 0, 64},          {31, 0, 199},        {32, 0, 512},
    {0, 1, 16},    {0, 2, 8},         {0, 8, 384},     {0, 16, 2048},       {0, 31, 2488},       {0, 32, 16384},
    {511, 255, 130943}, {69, 103, 50237}, {212, 174, 87340}, {300, 200, 107460}, {5, 9, 405}, {17, 23, 2233},
};

static std::vector<uint8_t> OwnUntile(const std::vector<uint8_t>& file, size_t at) {
  std::vector<uint8_t> out(512 * 256, 0);
  if (at + 512 * 256 > file.size()) return out;
  for (uint32_t y = 0; y < 256; ++y) {
    for (uint32_t x = 0; x < 512; ++x) out[y * 512 + x] = file[at + OwnTiled(x, y, 512)];
  }
  return out;
}

static void TestTiling() {
  int module_ok = 0, own_ok = 0;
  for (const Pin& p : kPins) {
    module_ok += lb::TiledOffset8(p.x, p.y, 512) == p.offset;
    own_ok += OwnTiled(p.x, p.y, 512) == p.offset;
  }
  CHECK(module_ok == int(std::size(kPins)));
  CHECK(own_ok == int(std::size(kPins)));
  // A permutation of the texture's bytes: every texel has its own byte, none past the end.
  std::vector<uint8_t> hits(512 * 256, 0);
  bool in_range = true;
  int twice = 0;
  for (uint32_t y = 0; y < 256; ++y) {
    for (uint32_t x = 0; x < 512; ++x) {
      const uint32_t o = lb::TiledOffset8(x, y, 512);
      if (o >= 512 * 256) {
        in_range = false;
      } else {
        twice += hits[o]++ != 0;
      }
    }
  }
  CHECK(in_range && twice == 0);
  std::vector<uint8_t> tiled(512 * 256);
  uint32_t seed = 12345;
  for (uint8_t& b : tiled) {
    seed = seed * 1103515245u + 12345u;
    b = uint8_t(seed >> 16);
  }
  const std::vector<uint8_t> linear = lb::Untile8(tiled.data(), 512, 256);
  CHECK(linear == OwnUntile(tiled, 0));
  CHECK(lb::Retile8(linear, 512, 256) == tiled);  // untile then retile is the identity
  CHECK(lb::Untile8(lb::Retile8(linear, 512, 256).data(), 512, 256) == linear);
}

// A synthetic label atlas (row-major palette indices, 512 x 256), in place of the game's art: each
// of COS. 1..10's record rectangles holds a "button" whose background differs from cell to cell
// (so a block taken from the wrong rows or columns shows), COS. 1..9 a digit at columns 64..71 and
// COS. 10 a "1" at 64..68 and a "0" at 71..79 (rows 5..19), each in its own values. Columns 209..211,
// outside COS. 10's 103-wide record but inside its 106-wide grid cell, carry a marker standing for the
// button's shadow there (the game's atlas has it at 209..210): the copy of the whole cell takes it
// along, a copy of only the record would not. PLAYER 1 and COM. stand for the rest of the atlas.
static std::vector<uint8_t> Atlas() {
  std::vector<uint8_t> a(512 * 256, 0);
  for (int k = 0; k < 10; ++k) {
    const Rec& r = kCos[k];
    for (int y = r.y0; y < r.y1; ++y) {
      for (int x = r.x0; x < r.x1; ++x) {
        const int cx = x - r.x0, cy = y - r.y0;
        uint8_t v = uint8_t(40 + (cx * 13 + cy * 7 + k * 5) % 37);
        if (cy >= 5 && cy <= 19) {
          if (k < 9 && cx >= 64 && cx <= 71) v = uint8_t(100 + 10 * k + (cx + 2 * cy) % 7);
          if (k == 9 && cx >= 64 && cx <= 68) v = uint8_t(200 + (cx + cy) % 5);
          if (k == 9 && cx >= 71 && cx <= 79) v = uint8_t(220 + (3 * cx + cy) % 7);
        }
        a[y * 512 + x] = v;
      }
    }
  }
  for (int y = 103; y < 135; ++y) {
    for (int x = 209; x < 212; ++x) a[y * 512 + x] = 250;
  }
  for (int y = 0; y < 37; ++y) {
    for (int x = 0; x < 127; ++x) a[y * 512 + x] = uint8_t(1 + (x + y) % 9);
  }
  for (int y = 136; y < 173; ++y) {
    for (int x = 0; x < 127; ++x) a[y * 512 + x] = uint8_t(10 + (x * y) % 11);
  }
  return a;
}

// The atlas with COS. 11..17 composed, by the rule as the research note states it: COS. 10's 106x32
// grid cell copied to each cell, then rows 5..19 of COS. k's columns 62..74 over columns 69..81.
static std::vector<uint8_t> Expected(const std::vector<uint8_t>& a) {
  std::vector<uint8_t> e = a;
  const Rec& ten = kCos[9];
  for (int k = 1; k <= 7; ++k) {
    const Rec& t = kLabels[k - 1];
    const Rec& src = kCos[k - 1];
    for (int y = 0; y < 32; ++y) {
      for (int x = 0; x < 106; ++x) {
        const bool digit = y >= 5 && y <= 19 && x >= 69 && x <= 81;
        e[(t.y0 + y) * 512 + t.x0 + x] =
            digit ? a[(src.y0 + y) * 512 + src.x0 + x - 69 + 62] : a[(ten.y0 + y) * 512 + ten.x0 + x];
      }
    }
  }
  return e;
}

static void PutBe32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (24 - 8 * i));
}

// An XPR2 file shaped like spChrN.xpr: the header (12 + hs = 0x1000 bytes) holds two resources of
// 40 bytes (16 zero bytes, then the 24-byte fetch constant): image 0 and its 256x1 palette; the data
// section holds the palette at 0x20000 and image 0's texels, tiled, at `base`.
struct XprSpec {
  const char* magic = "XPR2";
  const char* type = "TX2D";
  uint32_t fmt = 2, width = 512, height = 256, endian = 0, mip = 0, base = 0;
  bool tiled = true;
  uint32_t desc = 36;  // image 0's descriptor, relative to byte 12
  uint32_t data_size = 0x21000;
};
static constexpr size_t kData = 0x1000;  // where the data section starts in MakeXpr's files

static std::vector<uint8_t> MakeXpr(const std::vector<uint8_t>& linear, const XprSpec& s = {}) {
  const uint32_t hs = 0x1000 - 12;
  std::vector<uint8_t> x(12 + hs + s.data_size, 0);
  std::memcpy(x.data(), s.magic, 4);
  PutBe32(x, 4, hs);
  PutBe32(x, 8, s.data_size);
  PutBe32(x, 12, 2);
  const uint32_t descs[2] = {s.desc, 76};
  for (int i = 0; i < 2; ++i) {
    std::memcpy(&x[16 + 16 * i], s.type, 4);
    PutBe32(x, 20 + 16 * i, descs[i]);
    PutBe32(x, 24 + 16 * i, 40);
    PutBe32(x, 28 + 16 * i, 36);
  }
  const uint32_t fc[2][6] = {
      {(s.tiled ? 0x80000000u : 0u) | 0x04000002u, s.base | (s.endian << 6) | s.fmt,
       ((s.height - 1) << 13) | (s.width - 1), 0x1400, 0, s.mip | 0x200},
      {0x82000002u, 0x20000u | 0x86u, 0xFF, 0xC14, 0, 0x200},
  };
  for (int i = 0; i < 2; ++i) {
    const size_t at = 12 + descs[i] + 16;
    for (int w = 0; w < 6 && at + 4 * w + 4 <= 0x1000; ++w) PutBe32(x, at + 4 * w, fc[i][w]);
  }
  for (size_t i = 0; i < 0x400 && kData + 0x20000 + i < x.size(); ++i) x[kData + 0x20000 + i] = uint8_t(i * 3 + 1);
  if (linear.size() == 512 * 256 && s.base + 0x20000 <= s.data_size) {
    for (uint32_t y = 0; y < 256; ++y) {
      for (uint32_t c = 0; c < 512; ++c) x[kData + s.base + OwnTiled(c, y, 512)] = linear[y * 512 + c];
    }
  }
  return x;
}

static void TestComposeSynthetic() {
  const std::vector<uint8_t> atlas = Atlas();
  const std::vector<uint8_t> in = MakeXpr(atlas);
  std::vector<uint8_t> out = in;
  CHECK(lb::ComposeLabels(&out) == "");
  CHECK(out.size() == in.size());
  const std::vector<uint8_t> want = Expected(atlas);
  const std::vector<uint8_t> got = OwnUntile(out, kData);
  CHECK(got == want);
  // Each cell's columns 103..105 (beyond the drawn record, inside the cell) carry COS. 10's marker.
  int tail = 0;
  for (const Rec& t : kLabels) {
    for (int y = 0; y < 32; ++y) {
      for (int x = 103; x < 106; ++x) tail += got[(t.y0 + y) * 512 + t.x0 + x] == 250;
    }
  }
  CHECK(tail == 7 * 32 * 3);
  // Only texel bytes changed: the header, the palette and everything else are as they were.
  CHECK(std::equal(in.begin(), in.begin() + kData, out.begin()));
  CHECK(std::equal(in.begin() + kData + 0x20000, in.end(), out.begin() + kData + 0x20000));
  size_t changed = 0, want_changed = 0;
  for (size_t i = 0; i < in.size(); ++i) changed += in[i] != out[i];
  for (size_t i = 0; i < atlas.size(); ++i) want_changed += atlas[i] != want[i];
  CHECK(want_changed == 7 * 106 * 32 && changed == want_changed);
  // Composing again is refused (the cells are not empty now) and changes nothing.
  std::vector<uint8_t> again = out;
  CHECK(lb::ComposeLabels(&again) == "cell 1 (212,103) is not empty");
  CHECK(again == out);
  // The texels may end exactly at the end of the data section.
  XprSpec edge;
  edge.base = 0x1000;
  std::vector<uint8_t> at_end = MakeXpr(atlas, edge);
  CHECK(lb::ComposeLabels(&at_end) == "");
  CHECK(OwnUntile(at_end, kData + 0x1000) == want);
}

// Every refusal names its reason and leaves the file as it was.
static void TestComposeRefusals() {
  const std::vector<uint8_t> atlas = Atlas();
  struct Case {
    XprSpec spec;
    std::string reason;
  };
  std::vector<Case> cases;
  const auto add = [&](auto&& edit, const std::string& reason) {
    XprSpec s;
    edit(s);
    cases.push_back({s, reason});
  };
  add([](XprSpec& s) { s.magic = "XPR1"; }, "not an XPR2 file");
  add([](XprSpec& s) { s.type = "USER"; }, "it has no TX2D texture");
  add([](XprSpec& s) { s.desc = 0x1000 - 12 - 39; }, "its first texture's descriptor runs past the header");
  add([](XprSpec& s) { s.fmt = 6; }, "image 0 is format 6, not k_8 (2)");
  add([](XprSpec& s) { s.height = 512; }, "image 0 is 512x512, not 512x256");
  add([](XprSpec& s) { s.width = 256; }, "image 0 is 256x256, not 512x256");
  add([](XprSpec& s) { s.tiled = false; }, "image 0 is not tiled");
  add([](XprSpec& s) { s.endian = 1; }, "image 0 has byte swap 1, not 0");
  add([](XprSpec& s) { s.mip = 0x1000; }, "image 0 has mip levels stored");
  add([](XprSpec& s) { s.base = 0x2000; }, "image 0's texels run past the data section");
  for (const Case& c : cases) {
    const std::vector<uint8_t> in = MakeXpr(atlas, c.spec);
    std::vector<uint8_t> out = in;
    const std::string why = lb::ComposeLabels(&out);
    CHECK(why == c.reason);
    if (why != c.reason) std::printf("  got \"%s\", expected \"%s\"\n", why.c_str(), c.reason.c_str());
    CHECK(out == in);
  }
  // A file one byte longer than its header and data say.
  std::vector<uint8_t> longer = MakeXpr(atlas);
  longer.push_back(0);
  std::vector<uint8_t> out = longer;
  CHECK(lb::ComposeLabels(&out) == "its header and data sizes do not add up to the file's size");
  CHECK(out == longer);
  std::vector<uint8_t> tiny = {'X', 'P', 'R', '2', 0, 0, 0, 0};
  out = tiny;
  CHECK(lb::ComposeLabels(&out) == "not an XPR2 file");
  CHECK(out == tiny);
}

// The cells must be empty to the last texel, and the texels just outside them do not count; the
// sources must hold something.
static void TestComposeEdges() {
  struct Poke {
    int x, y;
    std::string reason;  // "" = composes
  };
  const Poke pokes[] = {
      {212, 103, "cell 1 (212,103) is not empty"},  // the first texel of the first cell
      {317, 134, "cell 1 (212,103) is not empty"},  // its last texel
      {211, 205, "cell 7 (106,174) is not empty"},  // the last texel of the last cell
      {212, 174, ""},                               // just right of it: the blank cell, not a target
      {424, 103, ""},                               // just right of cell 2
      {340, 168, ""},                               // just below cell 5
  };
  for (const Poke& p : pokes) {
    std::vector<uint8_t> atlas = Atlas();
    atlas[p.y * 512 + p.x] = 7;
    const std::vector<uint8_t> in = MakeXpr(atlas);
    std::vector<uint8_t> out = in;
    const std::string why = lb::ComposeLabels(&out);
    CHECK(why == p.reason);
    if (p.reason.empty()) {
      CHECK(OwnUntile(out, kData) == Expected(atlas));  // the poked texel is still there
    } else {
      CHECK(out == in);
    }
  }
  std::vector<uint8_t> no_ten = Atlas();
  for (int y = 103; y < 135; ++y) {
    for (int x = 106; x < 209; ++x) no_ten[y * 512 + x] = 0;
  }
  std::vector<uint8_t> in = MakeXpr(no_ten), out = in;
  CHECK(lb::ComposeLabels(&out) == "the COS. 10 label is empty");
  CHECK(out == in);
  std::vector<uint8_t> no_three = Atlas();
  for (int y = 37 + 5; y <= 37 + 19; ++y) {
    for (int x = 212 + 62; x <= 212 + 74; ++x) no_three[y * 512 + x] = 0;
  }
  in = MakeXpr(no_three);
  out = in;
  CHECK(lb::ComposeLabels(&out) == "the digit of COS. 3 is empty");
  CHECK(out == in);
}

// A per-run folder (process id and a counter), removed with non-throwing calls.
struct TempDir {
  fs::path path;
  TempDir() {
    static int counter = 0;
    path = fs::temp_directory_path() /
           ("doa4_labels_test_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(counter++));
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

static void PutLe32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i));
}

// An AFS archive laid out the game's way: entry 0 at 0x800, each next entry at the previous end
// rounded up to 2048, the name table (48 bytes per entry, record i's last word = TOC word i) after
// the last one, zero padding; the file ends on a 2048 boundary.
static std::vector<uint8_t> Afs(const std::vector<std::vector<uint8_t>>& entries, uint32_t gap_before = 0xFFFFFFFF) {
  const auto align = [](uint64_t v) { return (v + 2047) / 2048 * 2048; };
  const uint32_t n = uint32_t(entries.size());
  std::vector<uint32_t> words{n};
  uint64_t pos = 0x800;
  for (uint32_t i = 0; i < n; ++i) {
    if (i == gap_before) pos += 2048;  // a layout the game would not derive
    words.push_back(uint32_t(pos));
    words.push_back(uint32_t(entries[i].size()));
    pos = align(pos + entries[i].size());
  }
  const uint64_t names = pos;
  std::vector<uint8_t> a(size_t(align(names + 48 * n)), 0);
  std::memcpy(a.data(), "AFS\0", 4);
  PutLe32(a, 4, n);
  for (uint32_t i = 0; i < n; ++i) {
    PutLe32(a, 8 + 8 * i, words[1 + 2 * i]);
    PutLe32(a, 12 + 8 * i, words[2 + 2 * i]);
    std::memcpy(a.data() + words[1 + 2 * i], entries[i].data(), entries[i].size());
    const std::string name = "e" + std::to_string(i) + ".bin";
    std::memcpy(a.data() + names + 48 * i, name.data(), name.size());
    PutLe32(a, size_t(names + 48 * i + 44), words[i]);
  }
  PutLe32(a, 8 + 8 * n, uint32_t(names));
  PutLe32(a, 12 + 8 * n, 48 * n);
  return a;
}

static void WriteFile(const fs::path& p, const std::vector<uint8_t>& b) {
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

static std::vector<uint8_t> ReadOverlay(const doa4::afs::Overlay& o) {
  std::vector<uint8_t> b(size_t(o.size()));
  if (!o.Read(0, b.data(), b.size())) b.clear();
  return b;
}

static std::vector<std::vector<uint8_t>> SprEntries() {
  std::vector<std::vector<uint8_t>> e;
  for (uint32_t i = 0; i < 12; ++i) {
    std::vector<uint8_t> v(100 + 700 * i);
    for (size_t j = 0; j < v.size(); ++j) v[j] = uint8_t(i * 31 + j * 7 + 1);
    e.push_back(v);
  }
  e[8] = MakeXpr(Atlas());
  e[9] = MakeXpr(Atlas());
  for (size_t i = 0; i < 0x400; ++i) e[9][kData + 0x20000 + i] ^= 0x5A;  // the two files differ elsewhere
  return e;
}

// spr.afs in a game folder: entries 8 and 9 come back composed, everything else byte for byte.
static void TestBuildLabelOverlay() {
  fs::path folder;
  {
    TempDir game;
    folder = game.path;
    const std::vector<std::vector<uint8_t>> entries = SprEntries();
    const std::vector<uint8_t> original = Afs(entries);
    WriteFile(game.path / "spr.afs", original);
    // The expected entries come from the rule's own model (Expected()), not from the composer under
    // test, so a wrong composition fails here too.
    std::vector<std::vector<uint8_t>> composed = entries;
    composed[8] = MakeXpr(Expected(Atlas()));
    composed[9] = MakeXpr(Expected(Atlas()));
    for (size_t i = 0; i < 0x400; ++i) composed[9][kData + 0x20000 + i] ^= 0x5A;
    CHECK(composed[8] != entries[8] && composed[9] != entries[9]);
    const std::vector<uint8_t> want = Afs(composed);
    {
      std::string error;
      const std::shared_ptr<const doa4::afs::Overlay> o = lb::BuildLabelOverlay(game.path, &error);
      CHECK(o && error.empty());
      if (o) {
        CHECK(o->size() == original.size());
        CHECK(ReadOverlay(*o) == want);
      }
    }
    // Failures: nothing is returned and the reason names the archive and the entry.
    const auto refused = [&](const std::vector<uint8_t>& archive, const std::string& reason) {
      WriteFile(game.path / "spr.afs", archive);
      std::string error;
      const bool none = lb::BuildLabelOverlay(game.path, &error) == nullptr;
      if (error != reason) std::printf("  got \"%s\", expected \"%s\"\n", error.c_str(), reason.c_str());
      return none && error == reason;
    };
    std::vector<std::vector<uint8_t>> bad = entries;
    bad[8] = bad[0];
    CHECK(refused(Afs(bad), "spr.afs entry 8 (spChrN.xpr): not an XPR2 file"));
    bad = entries;
    std::vector<uint8_t> busy = Atlas();
    busy[103 * 512 + 212] = 9;
    bad[9] = MakeXpr(busy);
    CHECK(refused(Afs(bad), "spr.afs entry 9 (spChrW.xpr): cell 1 (212,103) is not empty"));
    CHECK(refused(Afs(std::vector<std::vector<uint8_t>>(entries.begin(), entries.begin() + 9)),
                  "spr.afs has 9 entries; spChrN.xpr and spChrW.xpr are entries 8 and 9"));
    {
      // Ten entries are enough (8 and 9 are the last two).
      WriteFile(game.path / "spr.afs", Afs(std::vector<std::vector<uint8_t>>(entries.begin(), entries.begin() + 10)));
      std::string error;
      const std::shared_ptr<const doa4::afs::Overlay> o = lb::BuildLabelOverlay(game.path, &error);
      CHECK(o && error.empty());
      CHECK(o && ReadOverlay(*o) ==
                     Afs(std::vector<std::vector<uint8_t>>(composed.begin(), composed.begin() + 10)));
    }
    {
      // On top of another overlay of spr.afs (the title word's): that one's entry stays, and the game folder's file is not
      // what gets served for it. The stand-in is the same size as the entry it replaces, as the word's files are.
      WriteFile(game.path / "spr.afs", original);
      std::vector<uint8_t> word(entries[10].size(), 0xEE);
      std::string why;
      const std::shared_ptr<const doa4::afs::Overlay> title = doa4::afs::Overlay::Build(
          doa4::afs::FileSource::Open(game.path / "spr.afs"), {{10, std::make_shared<doa4::afs::MemorySource>(word)}}, &why);
      CHECK(title != nullptr);
      std::vector<std::vector<uint8_t>> both = composed;
      both[10] = word;
      std::string error;
      const std::shared_ptr<const doa4::afs::Overlay> o = lb::BuildLabelOverlay(game.path, &error, title);
      CHECK(o && error.empty());
      CHECK(o && o->original_size() == title->size());  // what InstallOverlays compares with the served entry's size
      CHECK(o && ReadOverlay(*o) == Afs(both));
      CHECK(o && ReadOverlay(*o) != want);  // NOT the labels alone, which is what ignoring `over` serves
    }
    CHECK(refused(Afs(entries, 5), "spr.afs: the combined archive would not keep the original layout"));
    std::vector<uint8_t> not_afs = original;
    std::memcpy(not_afs.data(), "XYZ\0", 4);
    WriteFile(game.path / "spr.afs", not_afs);
    std::string error;
    CHECK(lb::BuildLabelOverlay(game.path, &error) == nullptr && error.rfind("spr.afs: ", 0) == 0);
    std::error_code ec;
    fs::remove(game.path / "spr.afs", ec);
    error.clear();
    CHECK(lb::BuildLabelOverlay(game.path, &error) == nullptr && error == "cannot open spr.afs in the game folder");
  }
  CHECK(!fs::exists(folder));  // the folder went (nothing held the archive open)
}

static uint64_t Fnv(const std::vector<uint8_t>& v) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (uint8_t b : v) {
    h ^= b;
    h *= 0x100000001b3ull;
  }
  return h;
}

static uint32_t Be32At(const std::vector<uint8_t>& b, size_t at) {
  return at + 4 <= b.size() ? uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3]
                            : 0;
}

// Optional: the game's own spr.afs (DOA4_SUITS_GAME_DIR, read-only). The values pinned here come
// from an independent Python model run over the same file (tex_png.py's tiled_offset and the rule
// above): FNV-1a 64 of image 0 untiled, of the composed image and of each composed file.
static void TestRealSprAfs() {
  wchar_t dir[1024];
  const DWORD len = GetEnvironmentVariableW(L"DOA4_SUITS_GAME_DIR", dir, DWORD(std::size(dir)));
  if (len == 0 || len >= std::size(dir)) {
    std::printf("real spr.afs: SKIPPED (DOA4_SUITS_GAME_DIR unset)\n");
    return;
  }
  const fs::path game(dir);
  const std::shared_ptr<doa4::afs::FileSource> src = doa4::afs::FileSource::Open(game / "spr.afs");
  CHECK(src != nullptr);
  if (!src) return;
  std::string why;
  const std::optional<doa4::afs::Toc> toc = doa4::afs::ReadToc(*src, &why);
  CHECK(toc && toc->count == 53);
  if (!toc || toc->count < 10) return;
  const uint64_t fnv_files[2] = {0xcdfc4d1e3e3b3ca5ull, 0x7fe6f325751c71a5ull};
  std::vector<uint8_t> composed[2], before_image;
  size_t at_image = 0;
  for (int j = 0; j < 2; ++j) {
    const uint32_t e = 8 + j;
    std::vector<uint8_t> f(toc->sizes[e]);
    CHECK(src->Read(toc->offsets[e], f.data(), f.size()));
    CHECK(f.size() == 4378624 && std::memcmp(f.data(), "XPR2", 4) == 0);
    // Image 0 by its own reading of the header: the first resource is a 40-byte TX2D.
    const uint32_t hs = Be32At(f, 4), desc = 12 + Be32At(f, 20), size = Be32At(f, 24);
    CHECK(std::memcmp(f.data() + 16, "TX2D", 4) == 0 && size == 40);
    uint32_t fc[6];
    for (int w = 0; w < 6; ++w) fc[w] = Be32At(f, desc + size - 24 + 4 * w);
    CHECK((fc[1] & 0x3F) == 2 && (fc[2] & 0x1FFF) + 1 == 512 && ((fc[2] >> 13) & 0x1FFF) + 1 == 256);
    CHECK((fc[0] >> 31) == 1 && ((fc[1] >> 6) & 3) == 0 && (fc[5] & 0xFFFFF000u) == 0);
    const size_t at = 12 + size_t(hs) + (fc[1] & 0xFFFFF000u);
    CHECK(at == 0x2000);
    const std::vector<uint8_t> before = OwnUntile(f, at);
    CHECK(Fnv(before) == 0x9eb13fa087ba932full);
    // The seven cells, and the blank cell of the stopgap, hold palette index 0 throughout.
    int busy = 0;
    for (const Rec& t : kLabels) {
      for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 106; ++x) busy += before[(t.y0 + y) * 512 + t.x0 + x] != 0;
      }
    }
    for (int y = 174; y < 206; ++y) {
      for (int x = 212; x < 318; ++x) busy += before[y * 512 + x] != 0;
    }
    CHECK(busy == 0);
    // The copy is seamless: outside rows 5..19, COS. k's columns 62..74 equal COS. 10's 69..81.
    int seams = 0;
    for (int k = 0; k < 7; ++k) {
      for (int y = 0; y < 32; ++y) {
        if (y >= 5 && y <= 19) continue;
        for (int i = 0; i < 13; ++i) {
          seams += before[(kCos[k].y0 + y) * 512 + kCos[k].x0 + 62 + i] !=
                   before[(kCos[9].y0 + y) * 512 + kCos[9].x0 + 69 + i];
        }
      }
    }
    CHECK(seams == 0);
    // COS. 10's button shadow runs past its 103-wide record into columns 103..104 of its grid cell
    // (59 texels), and column 105 is empty: copying the whole cell takes all of the art and nothing
    // of the next cell.
    int shadow = 0, last_column = 0;
    for (int y = 0; y < 32; ++y) {
      for (int x = 103; x < 105; ++x) shadow += before[(kCos[9].y0 + y) * 512 + kCos[9].x0 + x] != 0;
      last_column += before[(kCos[9].y0 + y) * 512 + kCos[9].x0 + 105] != 0;
    }
    CHECK(shadow == 59 && last_column == 0);
    std::vector<uint8_t> out = f;
    CHECK(lb::ComposeLabels(&out) == "");
    const std::vector<uint8_t> after = OwnUntile(out, at);
    CHECK(after == Expected(before));
    CHECK(Fnv(after) == 0xf551e38e66fee70cull);
    CHECK(Fnv(out) == fnv_files[j]);
    // Only image 0's texels changed: 22533 of them (the model's count), nothing outside the texture.
    size_t changed = 0, outside = 0;
    for (size_t i = 0; i < f.size() && i < out.size(); ++i) {
      if (f[i] == out[i]) continue;
      ++changed;
      outside += i < at || i >= at + 512 * 256;
    }
    CHECK(out.size() == f.size() && changed == 22533 && outside == 0);
    composed[j] = out;
    if (j == 0) {
      before_image = before;
      at_image = at;
    }
  }
  // Both files compose identically: the same texels, to the same values.
  CHECK(composed[0].size() > at_image + 512 * 256 && composed[1].size() > at_image + 512 * 256 &&
        std::equal(composed[0].begin() + at_image, composed[0].begin() + at_image + 512 * 256,
                   composed[1].begin() + at_image));
  // The combined archive: the original byte for byte, apart from those texels of entries 8 and 9.
  std::string error;
  {
    const std::shared_ptr<const doa4::afs::Overlay> o = lb::BuildLabelOverlay(game, &error);
    CHECK(o && error.empty());
    if (o) {
      CHECK(o->size() == src->size());
      std::vector<uint8_t> a(1 << 20), b(1 << 20);
      uint64_t differ = 0, outside = 0;
      bool read = true;
      for (uint64_t pos = 0; pos < src->size() && read; pos += a.size()) {
        const size_t n = size_t(std::min<uint64_t>(a.size(), src->size() - pos));
        read = src->Read(pos, a.data(), n) && o->Read(pos, b.data(), n);
        for (size_t i = 0; i < n && read; ++i) {
          if (a[i] == b[i]) continue;
          ++differ;
          const uint64_t p = pos + i;
          bool in8 = p >= toc->offsets[8] && p < toc->offsets[8] + toc->sizes[8];
          bool in9 = p >= toc->offsets[9] && p < toc->offsets[9] + toc->sizes[9];
          outside += !in8 && !in9;
        }
      }
      CHECK(read);
      CHECK(differ == 2 * 22533 && outside == 0);
      std::vector<uint8_t> e8(toc->sizes[8]), e9(toc->sizes[9]);
      CHECK(o->Read(toc->offsets[8], e8.data(), e8.size()) && e8 == composed[0]);
      CHECK(o->Read(toc->offsets[9], e9.data(), e9.size()) && e9 == composed[1]);
    }
  }
  wchar_t dump[1024];
  const DWORD dump_len = GetEnvironmentVariableW(L"DOA4_SUITS_LABEL_DUMP", dump, DWORD(std::size(dump)));
  if (dump_len > 0 && dump_len < std::size(dump)) {
    WriteFile(fs::path(dump) / "image0_composed_cpp.bin", OwnUntile(composed[0], at_image));
    std::printf("real spr.afs: composed image 0 written to the dump folder\n");
  }
}

int main() {
  // Unbuffered: a run that dies mid-way still shows the FAIL lines printed before it.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  TestBlankRecord();
  TestExtendTable();
  TestRecordBounds();
  TestPointerBounds();
  TestLayoutCheck();
  TestAllocFails();
  TestRewriteId();
  TestRewriteDrawStruct();
  TestListingScope();
  TestLabelRecords();
  TestNewRecords();
  TestDescribeInstalled();
  TestTiling();
  TestComposeSynthetic();
  TestComposeRefusals();
  TestComposeEdges();
  TestBuildLabelOverlay();
  TestRealSprAfs();
  std::printf("suits_labels: %d checks run\n", g_checks);
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("suits_labels: all checks passed\n");
  return 0;
}
