// Host test for src/doa4_suitconv_be.h and src/doa4_suitconv_tpr.cpp (the XPR2, chunk-table and .cat readers and writers,
// a port of the recipe's tpr.py). Built and run by run_suitconv_tpr_test.bat with the converter's compile flags.
//
// Groups: reader_bounds, table_chunk_layout, xpr_round_trip_synthetic, fetch_constants, world_mats_pins,
// hend3n_all_edges, decode_elem, mesh_records, geo_info_round_trip, cat_helpers; and two groups gated by environment
// variables (each prints "<group>: SKIPPED (<VAR> unset)" when its variable is unset):
//   real_round_trip_doax2  DOA4_SUITCONV_DISC      the DOAX2 ISO or an extracted folder: the 43 .tpr (9 bodies + 34 suits)
//                                                  parse, then Build(4), to the original bytes
//   real_round_trip_doa4   DOA4_SUITCONV_GAME_DIR  the DOA4 game folder: the 10 .tpr parse, then Build(1), to the original
// Every synthetic byte string is made here, laid out by hand (the offsets in the checks are typed from the layout rules,
// not computed by the code under test); the pins (suitconv_tpr_pins.h) are outputs of the recipe's own dec_hend3n and
// world_mats. No byte of any game file is stored. Last lines: "skipped sections: K", then
// "PASSED: N checks, 0 failures" or "FAILED: N checks, M failures".

#include "../../src/doa4_suitconv_be.h"
#include "../../src/doa4_suitconv_inputs.h"
#include "../../src/doa4_suitconv_recipe.h"
#include "../../src/doa4_suitconv_tpr.h"
#include "suitconv_tpr_pins.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace doa4::suitconv;
namespace fs = std::filesystem;
namespace tp = doa4::suitconv::tpr;
using tp::Bytes;

static int g_checks = 0;
static int g_failures = 0;
static int g_skipped = 0;
static int g_printed = 0;
static constexpr int kPrintCap = 25;

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
}
#define CK(cond) Check((cond), __FILE__, __LINE__, #cond)
#define CKF(cond, ...) Check((cond), __FILE__, __LINE__, #cond, __VA_ARGS__)

static void Group(const char* name) {
  g_printed = 0;
  std::printf("-- %s\n", name);
}

// ---------------------------------------------------------------------------------------------------------------
// helpers: a byte builder that does not share code with be::Writer, and "this throws ConvertError"
// ---------------------------------------------------------------------------------------------------------------

struct TB {
  Bytes b;
  TB& u8(uint32_t v) { b.push_back(uint8_t(v)); return *this; }
  TB& u16(uint32_t v) { u8(v >> 8); return u8(v); }
  TB& u32(uint32_t v) { u16(v >> 16); return u16(v); }
  TB& zeros(size_t n) { b.insert(b.end(), n, 0); return *this; }
  TB& str(const char* s, size_t pad_to = 0) {
    size_t n = 0;
    for (; s[n]; ++n) b.push_back(uint8_t(s[n]));
    if (pad_to > n) zeros(pad_to - n);
    return *this;
  }
  TB& cat(const Bytes& o) { b.insert(b.end(), o.begin(), o.end()); return *this; }
  TB& fill(uint8_t v, size_t n) { b.insert(b.end(), n, v); return *this; }
};

// The message of the ConvertError fn throws; "<none>" when it does not throw, "<other>" for another exception.
static std::string ThrowsMsg(const std::function<void()>& fn) {
  try {
    fn();
  } catch (const ConvertError& e) {
    return e.what();
  } catch (...) {
    return "<other>";
  }
  return "<none>";
}
static bool Throws(const std::function<void()>& fn) {
  const std::string m = ThrowsMsg(fn);
  return m != "<none>" && m != "<other>";
}

static std::span<const uint8_t> Sp(const Bytes& b) { return std::span<const uint8_t>(b.data(), b.size()); }

static uint32_t FBits(float f) { return std::bit_cast<uint32_t>(f); }

// 64 bytes of 16 big-endian float32 from small exactly representable values.
static Bytes MatBytes(const float (&v)[16]) {
  TB t;
  for (float f : v) t.u32(FBits(f));
  return t.b;
}

// ---------------------------------------------------------------------------------------------------------------
// reader_bounds
// ---------------------------------------------------------------------------------------------------------------

static void TestReaderBounds() {
  Group("reader_bounds");
  const Bytes five = {1, 2, 3, 4, 5};
  const be::Reader r(Sp(five));
  CK(r.U32(0) == 0x01020304u);
  CK(r.U32(1) == 0x02030405u);
  CK(ThrowsMsg([&] { r.U32(2); }) == "read past end");
  CK(ThrowsMsg([&] { r.U16(4); }) == "read past end");
  CK(ThrowsMsg([&] { r.U8(5); }) == "read past end");
  CK(r.U8(4) == 5);
  CK(ThrowsMsg([&] { r.F32(2); }) == "read past end");
  CK(ThrowsMsg([&] { r.U32s(0, 2); }) == "read past end");
  CK(ThrowsMsg([&] { r.U32s(0, 1ull << 40); }) == "read past end");  // no allocation of a hostile count
  CK(ThrowsMsg([&] { r.I16s(0, 3); }) == "read past end");
  CK(r.U32s(0, 1).size() == 1 && r.U16s(0, 2).size() == 2);
  CK(r.U32s(5, 0).empty());  // n == 0 at the end is fine, as unpack_from('>0I') is
  // Python's negative offset rule (unpack_from counts from the end when the whole field fits).
  CK(r.U32(-4) == 0x02030405u);
  CK(ThrowsMsg([&] { r.U32(-3); }) == "read past end");
  CK(ThrowsMsg([&] { r.U32(-6); }) == "read past end");
  // slices clamp and never throw
  CK(r.Slice(1, 3).size() == 2 && r.Slice(3, 100).size() == 2 && r.Slice(9, 12).empty() && r.Slice(4, 2).empty());
  CK(r.From(5).empty() && r.From(6).empty() && r.From(0).size() == 5);
  // a read from an empty buffer
  const be::Reader e{std::span<const uint8_t>()};
  CK(Throws([&] { e.U8(0); }) && Throws([&] { e.U16(0); }) && Throws([&] { e.U32(-4); }));
  // Writer ranges (struct.pack)
  CK(Throws([] { be::Writer w; w.U16(65536); }));
  CK(Throws([] { be::Writer w; w.I16(32768); }));
  CK(Throws([] { be::Writer w; w.I16(-32769); }));
  CK(Throws([] { be::Writer w; w.U32(1ull << 32); }));
  CK(Throws([] { be::Writer w; w.U8(256); }));
  CK(ThrowsMsg([] { be::Writer w; w.F32(1e39); }) == "float too large to pack with f format");
  {
    be::Writer w;
    w.U16(0xBEEF);
    w.I16(-2);
    w.U32(0x01020304u);
    w.F32(1.5);
    w.U8(7);
    const Bytes want = {0xBE, 0xEF, 0xFF, 0xFE, 1, 2, 3, 4, 0x3F, 0xC0, 0, 0, 7};
    CK(w.bytes() == want);
  }
  {
    Bytes b(8, 0);
    be::PackIntoU32(b, 2, 0xAABBCCDDu);
    const Bytes want = {0, 0, 0xAA, 0xBB, 0xCC, 0xDD, 0, 0};
    CK(b == want);
    CK(Throws([&] { be::PackIntoU32(b, 5, 1); }) && Throws([&] { be::PackIntoU32(b, 0, 1ull << 32); }));
    be::PackIntoU32(b, -4, 0x11223344u);  // from the end
    CK(b[4] == 0x11 && b[7] == 0x44);
  }
  // The parsers on truncated input throw ConvertError and never read out of bounds.
  const Bytes tiny = {'X', 'P', 'R', '2'};
  CK(Throws([&] { tp::Xpr::Parse(Sp(tiny)); }));
  CK(Throws([&] { tp::ParseHie(Sp(five)); }));
  CK(Throws([&] { tp::ParseMdlGeo(Sp(five)); }));
  CK(Throws([&] { tp::ParseMdlInfo(Sp(five)); }));
  CK(Throws([&] { tp::BufSpan(Sp(five)); }));
  CK(Throws([&] { tp::CatDir(Sp(five)); }));
  CK(Throws([&] { tp::DecodeElem(Sp(five), 3, 0x2A23B9); }));  // F3 at 3 needs 12 bytes
  CK(Throws([&] { tp::Al(5, 0); }));
}

// ---------------------------------------------------------------------------------------------------------------
// table_chunk_layout
// ---------------------------------------------------------------------------------------------------------------

static Bytes Pattern(size_t n, uint8_t seed) {
  Bytes b(n);
  for (size_t i = 0; i < n; ++i) b[i] = uint8_t(seed + 7 * i);
  return b;
}

// A Hie of three nodes: node 0 (id 0, root) has children 1 and 2.
static std::vector<tp::Node> ThreeNodeHie(Bytes* m0, Bytes* m1, Bytes* m2) {
  *m0 = Pattern(64, 0x10);
  *m1 = Pattern(64, 0x40);
  *m2 = Pattern(64, 0x70);
  std::vector<tp::Node> nodes;
  nodes.push_back(tp::Node::FromBytes(Sp(*m0), 0, -1, {1, 2}));
  nodes.push_back(tp::Node::FromBytes(Sp(*m1), 1, 0, {}));
  nodes.push_back(tp::Node::FromBytes(Sp(*m2), 2, 0, {}));
  return nodes;
}

static void TestTableChunkLayout() {
  Group("table_chunk_layout");
  Bytes m0, m1, m2;
  const auto nodes = ThreeNodeHie(&m0, &m1, &m2);
  // Records: node 0 = 64 + 6 + 2*2 = 74 bytes (padded to 80), node 1 and node 2 = 70 bytes. The head is 0x30 bytes, the
  // table (3 words, padded to 16) follows at 0x30, the first record at 0x40, the second at 0x40 + 80 = 0x90, the third
  // at 0x90 + 80 = 0xE0; the LAST record of a Hie is not padded, so the chunk is 0xE0 + 70 = 0x126 bytes.
  TB want;
  want.str("Hie", 8).u32(0xFF000100).u32(0x30).u32(0x126).u32(3).u32(3).u32(0).u32(0x30).u32(0).u32(0).u32(0);
  want.u32(0x40).u32(0x90).u32(0xE0).u32(0);
  want.cat(m0).u16(0).u16(0xFFFF).u16(2).u16(1).u16(2).zeros(6);
  want.cat(m1).u16(1).u16(0).u16(0).zeros(10);
  want.cat(m2).u16(2).u16(0).u16(0);
  CK(want.b.size() == 0x126);
  const Bytes got = tp::BuildHie(nodes);
  CKF(got == want.b, "BuildHie %zu bytes, expected %zu", got.size(), want.b.size());
  // Parse reads it back: ids, parents, children and the raw matrices.
  std::vector<tp::Node> back;
  CK(!Throws([&] { back = tp::ParseHie(Sp(want.b)); }));
  CK(back.size() == 3 && back[0].raw && back[0].mat_bytes == m0 && back[2].mat_bytes == m2);
  CK(back.size() == 3 && back[0].id == 0 && back[0].parent == -1 && back[0].children == std::vector<int64_t>({1, 2}));
  CK(back.size() == 3 && back[1].id == 1 && back[1].parent == 0 && back[1].children.empty());
  CK(back.size() == 3 && back[2].id == 2 && back[2].parent == 0 && back[2].children.empty());
  CK(tp::BuildHie(back) == want.b);
  // With the last record padded like the others (pad_last = true) the chunk is 0x126 -> 0x130 and nothing else moves.
  {
    std::vector<Bytes> recs;
    for (int i = 0; i < 3; ++i) {
      TB r;
      r.cat(i == 0 ? m0 : i == 1 ? m1 : m2).u16(i).u16(i == 0 ? 0xFFFF : 0).u16(i == 0 ? 2 : 0);
      if (i == 0) r.u16(1).u16(2);
      recs.push_back(r.b);
    }
    const Bytes padded = tp::BuildTableChunk("Hie", recs);
    CK(padded.size() == 0x130);
    CK(padded.size() == 0x130 && padded[8 + 8] == 0 && padded[8 + 9] == 0 && padded[8 + 10] == 1 && padded[8 + 11] == 0x30);  // size word 0x130
    CK(padded.size() == 0x130 && padded[0x30] == 0 && padded[0x33] == 0x40 && padded[0x37] == 0x90 && padded[0x3B] == 0xE0);
    const Bytes unpadded = tp::BuildTableChunk("Hie", recs, {}, std::nullopt, false);
    CK(unpadded.size() == 0x126);
    CK(padded.size() == 0x130 && std::equal(unpadded.begin() + 0x30, unpadded.end(), padded.begin() + 0x30));
  }
  // A float-matrix node builds the same bytes as its raw twin when the floats are float32 values.
  {
    const float f0[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1.5f, -2.25f, 3, 1};
    const Bytes raw = MatBytes(f0);
    std::vector<double> dv(f0, f0 + 16);
    std::vector<tp::Node> a, b;
    a.push_back(tp::Node::FromBytes(Sp(raw), 5, -1, {}));
    b.push_back(tp::Node::FromFloats(dv, 5, -1, {}));
    CK(tp::BuildHie(a) == tp::BuildHie(b));
    CK(tp::MatFloats(b[0]) == dv && tp::MatFloats(a[0]) == dv);
    // a float32 overflow while building is the struct.error
    std::vector<double> big = dv;
    big[3] = 1e39;
    CK(Throws([&] { tp::BuildHie({tp::Node::FromFloats(big, 0, -1, {})}); }));
    CK(Throws([&] { tp::Node::FromFloats(std::vector<double>(15), 0, -1, {}); }));
  }
  // pre bytes, an explicit n2, two children: head 0x30 + pre 48 -> table at 0x60 (2 words, padded to 16) -> bodies at
  // 0x70 and 0x80, chunk 0x90.
  {
    const Bytes c0 = Pattern(16, 1), c1 = Pattern(16, 2), pre = Pattern(48, 3);
    TB w;
    w.str("ObjInfo", 8).u32(0xFF000100).u32(0x30).u32(0x90).u32(2).u32(7).u32(0).u32(0x60).u32(0).u32(0).u32(0);
    w.cat(pre).u32(0x70).u32(0x80).zeros(8).cat(c0).cat(c1);
    CK(tp::BuildTableChunk("ObjInfo", {c0, c1}, Sp(pre), 7) == w.b);
    // a pre that is not a multiple of 16 is the assert
    CK(Throws([&] { tp::BuildTableChunk("X", {c0}, Sp(Pattern(40, 1))); }));
  }
  // no children: no table, the chunk is its head
  {
    const Bytes e = tp::BuildTableChunk("MdlGeo", {});
    TB w;
    w.str("MdlGeo", 8).u32(0xFF000100).u32(0x30).u32(0x30).u32(0).u32(0).u32(0).u32(0x30).u32(0).u32(0).u32(0);
    CK(e == w.b);
    CK(tp::ParseMdlGeo(Sp(e)).empty());
  }
  // Hie parse refuses a record whose tail is not zero and one whose length is not the padded length.
  {
    Bytes bad = want.b;
    bad[0x40 + 74 + 2] = 1;  // inside node 0's padding
    CK(Throws([&] { tp::ParseHie(Sp(bad)); }));
    Bytes bad2 = want.b;
    bad2.push_back(0);  // the last record grows: size word still says 0x126, so the last record ends at 0x126 (not at the end)
    CK(tp::ParseHie(Sp(bad2)).size() == 3);
    Bytes bad3 = want.b;
    bad3[0] = 'X';  // magic
    CK(Throws([&] { tp::ParseHie(Sp(bad3)); }));
    Bytes bad4 = want.b;
    bad4[11] = 1;  // version word
    CK(Throws([&] { tp::ParseHie(Sp(bad4)); }));
  }
}

// ---------------------------------------------------------------------------------------------------------------
// xpr_round_trip_synthetic
// ---------------------------------------------------------------------------------------------------------------

// Four resources in this order: TX2D (7 bytes), USER (5), USER (3), VBUF (8); data section of 10 bytes. n = 4: the
// resource table is 16 + 16*4 = 80 bytes long, so blobs start at 80 - 12 = 68 relative to byte 12.
//  align 1: TX2D 68..75, USER 75..80, USER 80..83, VBUF 83..91; first USER at 75; 12 + 91 = 103 -> hs = 0x1000 - 12.
//  align 4: TX2D 68..75, USER 76..81, USER 84..87, VBUF 88..96; first USER at 76.
static Bytes SynthXpr(int align) {
  const Bytes tx = Pattern(7, 1), u0 = Pattern(5, 2), u1 = Pattern(3, 3), vb = Pattern(8, 4), data = Pattern(10, 9);
  struct R {
    const char* t;
    uint32_t off;
    const Bytes* b;
  };
  const bool a1 = align == 1;
  const R rs[4] = {{"TX2D", 68, &tx}, {"USER", a1 ? 75u : 76u, &u0}, {"USER", a1 ? 80u : 84u, &u1}, {"VBUF", a1 ? 83u : 88u, &vb}};
  const uint32_t user0 = a1 ? 75 : 76;
  const uint32_t hs = 0x1000 - 12;
  TB t;
  t.str("XPR2").u32(hs).u32(10).u32(4);
  for (const R& r : rs) t.str(r.t).u32(r.off).u32(uint32_t(r.b->size())).u32(user0);
  t.zeros(12 + hs - t.b.size());
  for (const R& r : rs) std::copy(r.b->begin(), r.b->end(), t.b.begin() + 12 + r.off);
  t.cat(data);
  return t.b;
}

static void TestXprRoundTrip() {
  Group("xpr_round_trip_synthetic");
  const Bytes f1 = SynthXpr(1), f4 = SynthXpr(4);
  CK(f1.size() == 12 + 0x1000 - 12 + 10 && f4.size() == f1.size());
  CK(f1 != f4);
  const tp::Xpr x1 = tp::Xpr::Parse(Sp(f1));
  const tp::Xpr x4 = tp::Xpr::Parse(Sp(f4));
  CK(x1.res.size() == 4 && x1.res[0].type == tp::MakeFourcc("TX2D") && x1.res[3].type == tp::MakeFourcc("VBUF"));
  CK(x1.res.size() == 4 && x1.res[0].blob == Pattern(7, 1) && x1.res[1].blob == Pattern(5, 2) && x1.res[2].blob == Pattern(3, 3) && x1.res[3].blob == Pattern(8, 4));
  CK(x1.data == Pattern(10, 9) && x1.hs && *x1.hs == 0x1000 - 12);
  CK(x1.orig_offs.size() == 4 && x1.orig_offs[1] == std::make_pair(75u, 75u) && x4.orig_offs[3] == std::make_pair(88u, 76u));
  CK(x4.res.size() == 4 && x4.res[1].blob == x1.res[1].blob);
  CK((x1.OfType(tp::MakeFourcc("USER")) == std::vector<size_t>({1, 2})));
  CK(x1.OfType(tp::MakeFourcc("IBUF")).empty());
  // the rebuilt bytes equal the input at the file's own alignment ...
  CK(x1.Build(1) == f1);
  CK(x4.Build(4) == f4);
  CK(x1.Build() == f1);
  // ... and a deliberately misaligned variant does not: the other alignment moves the blobs.
  CK(x1.Build(4) != f1 && x1.Build(4) == f4);
  CK(x4.Build(1) != f4 && x4.Build(1) == f1);
  // an alignment of 0 is the ZeroDivisionError; the header's offsets follow the alignment
  CK(Throws([&] { x1.Build(0); }));
  // no USER at all: the name offset has no value (struct.error)
  {
    tp::Xpr nouser;
    nouser.res.push_back({tp::MakeFourcc("TX2D"), Pattern(4, 1)});
    CK(Throws([&] { nouser.Build(1); }));
    tp::Xpr empty;
    empty.data = Pattern(3, 1);
    const Bytes e = empty.Build(1);
    CK(e.size() == 0x1000 + 3 && e[0] == 'X' && e[7] == 0xF4 && e[6] == 0x0F && e[11] == 3 && e[15] == 0);  // hs = 0xFF4, ds = 3, n = 0
  }
  // malformed files throw and never read out of bounds
  {
    Bytes bad = f1;
    bad[0] = 'Y';
    CK(Throws([&] { tp::Xpr::Parse(Sp(bad)); }));
    Bytes shortd(f1.begin(), f1.end() - 1);  // data one byte short: the length assert
    CK(Throws([&] { tp::Xpr::Parse(Sp(shortd)); }));
    Bytes longd = f1;
    longd.push_back(0);
    CK(Throws([&] { tp::Xpr::Parse(Sp(longd)); }));
    Bytes few(f1.begin(), f1.begin() + 40);  // header claims 4 resources, 24 bytes of table present
    CK(Throws([&] { tp::Xpr::Parse(Sp(few)); }));
    Bytes manyres = f1;
    manyres[13] = 1;  // n = 0x10004 resources: the table runs off the end
    CK(Throws([&] { tp::Xpr::Parse(Sp(manyres)); }));
    // a blob that points past the end is clamped, as Python's slice is (then the build is simply different)
    Bytes wild = f1;
    wild[16 + 16 + 4 + 3] = 0xFF;  // resource 1's offset = 0xFF (still inside)... and its size 5
    wild[16 + 16 + 4] = 0x7F;      // offset 0x7F0000FF: far past the end
    const tp::Xpr xw = tp::Xpr::Parse(Sp(wild));
    CK(xw.res[1].blob.empty());
  }
}

// ---------------------------------------------------------------------------------------------------------------
// fetch_constants
// ---------------------------------------------------------------------------------------------------------------

static void TestFetchConstants() {
  Group("fetch_constants");
  // BufSpan: the last 8 bytes, base and size with the low two bits cleared.
  {
    TB d;
    d.fill(0xEE, 5).u32(0x12345679).u32(0x0000100F);
    const auto [base, size] = tp::BufSpan(Sp(d.b));
    CK(base == 0x12345678u && size == 0x100Cu);
    CK(Throws([&] { tp::BufSpan(Sp(Bytes(7, 0))); }));
    CK(Throws([&] { tp::BufSpan(Sp(Bytes())); }));
  }
  // TexFc / TexAddrs / TexSet: six dwords at the end of the descriptor.
  {
    TB d;
    d.fill(0x99, 9).u32(0xAAAAAAAA).u32(0x00ABC0FF).u32(0x11111111).u32(0x22222222).u32(0x33333333).u32(0x00DEF0AB);
    const auto fc = tp::TexFc(Sp(d.b));
    CK(fc[0] == 0xAAAAAAAAu && fc[1] == 0x00ABC0FFu && fc[5] == 0x00DEF0ABu);
    const auto [b, m] = tp::TexAddrs(Sp(d.b));
    CK(b == 0x00ABC000u && m == 0x00DEF000u);
    const Bytes set = tp::TexSet(Sp(d.b), 0x00123000, 0x00456000);
    TB want;
    want.fill(0x99, 9).u32(0xAAAAAAAA).u32(0x001230FF).u32(0x11111111).u32(0x22222222).u32(0x33333333).u32(0x004560AB);
    CK(set == want.b);
    CK(Throws([&] { tp::TexSet(Sp(d.b), 0x1'0000'0000, 0); }));
    CK(Throws([&] { tp::TexSet(Sp(d.b), -4096, 0); }));
    CK(Throws([&] { tp::TexFc(Sp(Bytes(23, 0))); }));
    // no mip address: mip must be 0
    TB e;
    e.u32(0).u32(0x00ABC0FF).u32(0).u32(0).u32(0).u32(0x00000AB0);
    CK(tp::TexSet(Sp(e.b), 0x1000, 0).size() == 24);
    CK(Throws([&] { tp::TexSet(Sp(e.b), 0x1000, 0x2000); }));
    const auto [b2, m2] = tp::TexAddrs(Sp(e.b));
    CK(b2 == 0xABC000u && m2 == 0);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// world_mats_pins, hend3n_all_edges
// ---------------------------------------------------------------------------------------------------------------

static uint64_t Bits(double d) { return std::bit_cast<uint64_t>(d); }

static void TestWorldMatsPins() {
  Group("world_mats_pins");
  using namespace suitconv_tpr_pins;
  size_t node = 0;
  size_t bad_raw = 0, bad_float = 0, entries = 0;
  uint64_t first_bad = 0;
  for (size_t c = 0; c < kWmCountCount; ++c) {
    const size_t n = kWmCount[c];
    std::vector<tp::Node> raw, flt;
    for (size_t i = 0; i < n; ++i) {
      const size_t at = (node + i) * 64;
      const Bytes mat(kWmMat + at, kWmMat + at + 64);
      raw.push_back(tp::Node::FromBytes(Sp(mat), int64_t(i), kWmParent[node + i], {}));
      flt.push_back(tp::Node::FromFloats(tp::MatFloats(raw.back()), int64_t(i), kWmParent[node + i], {}));
    }
    const auto a = tp::WorldMats(raw), b = tp::WorldMats(flt);
    for (size_t i = 0; i < n; ++i)
      for (size_t k = 0; k < 16; ++k) {
        const uint64_t want = kWmOut[(node + i) * 16 + k];
        ++entries;
        if (Bits(a[i][k]) != want) {
          if (!bad_raw++) first_bad = (c << 8) | (i << 4) | k;
        }
        if (Bits(b[i][k]) != want) ++bad_float;
      }
    node += n;
  }
  CKF(bad_raw == 0, "%zu of %zu entries differ from the recipe's world_mats (first: case %llu node %llu entry %llu)", bad_raw,
      entries, (unsigned long long)(first_bad >> 8), (unsigned long long)((first_bad >> 4) & 15), (unsigned long long)(first_bad & 15));
  CKF(bad_float == 0, "float-matrix nodes: %zu entries differ", bad_float);
  CK(node == kWmParentCount && entries == kWmOutCount / 1 * 1 && entries == node * 16);
  // Precondition: these pins can tell builtin sum() from a naive sum (many entries differ), and some parent comes
  // after its child, and the cases are not all trivial.
  size_t differs = 0;
  for (size_t i = 0; i < kWmOutCount; ++i) differs += kWmOut[i] != kWmNaive[i];
  CKF(differs > 20, "only %zu pinned entries differ from the naive sum", differs);
  size_t forward = 0;
  node = 0;
  for (size_t c = 0; c < kWmCountCount; ++c) {
    for (size_t i = 0; i < kWmCount[c]; ++i) forward += kWmParent[node + i] > int64_t(i);
    node += kWmCount[c];
  }
  CK(forward > 10 && kWmCountCount > 100);
  // errors: a parent index outside the list, and a cycle
  {
    const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Bytes m = MatBytes(id);
    CK(Throws([&] { tp::WorldMats({tp::Node::FromBytes(Sp(m), 0, 3, {})}); }));
    CK(Throws([&] { tp::WorldMats({tp::Node::FromBytes(Sp(m), 0, 1, {}), tp::Node::FromBytes(Sp(m), 1, 0, {})}); }));
    CK(Throws([&] { tp::WorldMats({tp::Node::FromBytes(Sp(Bytes(63, 0)), 0, -1, {})}); }));
    CK(tp::WorldMats({}).empty());
  }
}

static void TestHend3nAllEdges() {
  Group("hend3n_all_edges");
  using namespace suitconv_tpr_pins;
  size_t bad = 0, bad_elem = 0, wrong_differs = 0;
  uint64_t first_bad = 0;
  for (size_t i = 0; i < kHendWordCount; ++i) {
    const uint32_t v = kHendWord[i];
    const auto d = tp::DecHend3n(v);
    for (int k = 0; k < 3; ++k)
      if (Bits(d[k]) != kHendOut[i * 3 + k]) {
        if (!bad++) first_bad = v;
      }
    // the same through DecodeElem on the four bytes
    TB t;
    t.u32(v);
    const tp::ElemValue ev = tp::DecodeElem(Sp(t.b), 0, 0x2A2190);
    if (ev.n != 3) ++bad_elem;
    for (int k = 0; k < 3; ++k)
      if (Bits(ev.v[k]) != kHendOut[i * 3 + k]) ++bad_elem;
    for (int k = 0; k < 3; ++k) wrong_differs += kHendOut[i * 3 + k] != kHendWrong[i * 3 + k];
  }
  CKF(bad == 0, "%zu lane values differ (first word 0x%08llx)", bad, (unsigned long long)first_bad);
  CKF(bad_elem == 0, "%zu DecodeElem values differ", bad_elem);
  CK(kHendWordCount >= 216 + 6);
  CKF(wrong_differs > 100, "only %zu pinned values differ from the divisor 1 << (bits-1)", wrong_differs);
  // typed by hand: the sign bit decodes to -1024/1023 (11-bit) and -512/511 (10-bit); max positive to exactly 1
  CK(Bits(tp::DecHend3n(0x400)[0]) == Bits(-1024.0 / 1023.0));
  CK(Bits(tp::DecHend3n(0x200u << 22)[2]) == Bits(-512.0 / 511.0));
  CK(tp::DecHend3n(0x3FF)[0] == 1.0 && tp::DecHend3n(0x7FF)[0] == -1.0 / 1023.0 && tp::DecHend3n(0x1FFu << 22)[2] == 1.0);
}

// ---------------------------------------------------------------------------------------------------------------
// decode_elem
// ---------------------------------------------------------------------------------------------------------------

static void TestDecodeElem() {
  Group("decode_elem");
  {
    TB d;
    d.u32(0x3F800000).u32(0xC0200000).u32(0x3E000000).u32(0x40490FDB);  // 1.0, -2.5, 0.125, pi as float32
    const auto f1 = tp::DecodeElem(Sp(d.b), 0, 0x2C83A4);
    CK(f1.n == 1 && f1.v[0] == 1.0);
    const auto f2 = tp::DecodeElem(Sp(d.b), 4, 0x2C23A5);
    CK(f2.n == 2 && f2.v[0] == -2.5 && f2.v[1] == 0.125);
    const auto f3 = tp::DecodeElem(Sp(d.b), 0, 0x2A23B9);
    CK(f3.n == 3 && f3.v[0] == 1.0 && f3.v[1] == -2.5 && f3.v[2] == 0.125);
    const auto f4 = tp::DecodeElem(Sp(d.b), 0, 0x1A23A6);
    CK(f4.n == 4 && Bits(f4.v[3]) == Bits(double(std::bit_cast<float>(0x40490FDBu))));
    CK(Throws([&] { tp::DecodeElem(Sp(d.b), 4, 0x1A23A6); }));  // F4 at 4 needs 16 bytes
  }
  {
    TB d;
    d.u16(0x3C00).u16(0xC000).u16(0x3800).u16(0x0001);  // 1, -2, 0.5, 2^-24
    const auto h2 = tp::DecodeElem(Sp(d.b), 0, 0x2C235F);
    CK(h2.n == 2 && h2.v[0] == 1.0 && h2.v[1] == -2.0);
    const auto h4 = tp::DecodeElem(Sp(d.b), 0, 0x1A2360);
    CK(h4.n == 4 && h4.v[2] == 0.5 && h4.v[3] == std::ldexp(1.0, -24));
    CK(Throws([&] { tp::DecodeElem(Sp(d.b), 2, 0x1A2360); }));
  }
  {
    const Bytes d = {1, 2, 255, 0, 9, 8};
    for (uint32_t typ : {0x1A2086u, 0x182886u, 0x1A2286u}) {
      const auto v = tp::DecodeElem(Sp(d), 0, typ);
      CK(v.n == 4 && v.v[0] == 1 && v.v[1] == 2 && v.v[2] == 255 && v.v[3] == 0);
      const auto w = tp::DecodeElem(Sp(d), 4, typ);  // two bytes left: d[o:o+4] is a 2-tuple, no error
      CK(w.n == 2 && w.v[0] == 9 && w.v[1] == 8);
      CK(tp::DecodeElem(Sp(d), 6, typ).n == 0);
    }
  }
  CK(Throws([&] { tp::DecodeElem(Sp(Bytes(16, 0)), 0, 0x12345); }));    // KeyError
  CK(Throws([&] { tp::DecodeElem(Sp(Bytes(16, 0)), 0, 0x2A2187); }));   // DEC3N: ValueError
  CK(tp::DT().size() == 11 && tp::DtFind(0x2A2190) && tp::DtFind(0x2A2190)->name == "HEND3N" && tp::DtFind(0x2A2190)->size == 4);
  CK(tp::DtByName("COLOR") == 0x182886u && !tp::DtByName("NOPE") && tp::UsageName(10) == "COL" && tp::UsageName(4).empty());
  CK(tp::Half(0x3555) == 0.333251953125);
}

// ---------------------------------------------------------------------------------------------------------------
// mesh_records
// ---------------------------------------------------------------------------------------------------------------

// A mesh record: 0x18 bytes of words, the stage count at 0x18, 0x1C-0x20 spare, then nst stages of 0x40 bytes.
static Bytes SynthMesh() {
  TB m;
  m.u32(7).u32(0).u32(3).u32(0).u32(100).u32(200).u32(0).u32(0);  // words 0..7; word 6 (0x18) is the stage count below
  m.b[0x18] = 0, m.b[0x19] = 0, m.b[0x1A] = 0, m.b[0x1B] = 2;
  // stage 0: stage 0, kind 1, texture 5; stage 1: stage 1, kind 2, texture 0xFFFFFFFF
  m.u32(0).u32(1).u32(5).zeros(0x34);
  m.u32(1).u32(2).u32(0xFFFFFFFF).zeros(0x34);
  return m.b;
}

static void TestMeshRecords() {
  Group("mesh_records");
  const Bytes m = SynthMesh();
  CK(m.size() == 0x20 + 0x80);
  const auto w = tp::MeshWords(Sp(m));
  CK(w.size() == 0x28 && w[0] == 7 && w[2] == 3 && w[4] == 100 && w[5] == 200 && w[6] == 2 && w[8] == 0 && w[9] == 1 && w[10] == 5);
  const auto [base, nst] = tp::MeshStageBase(Sp(m));
  CK(base == 0x20 && nst == 2);
  const auto tex = tp::MeshTextures(Sp(m));
  CK(tex.size() == 2 && tex[0].stage == 0 && tex[0].kind == 1 && tex[0].texture == 5 && tex[1].stage == 1 && tex[1].kind == 2 &&
     tex[1].texture == 0xFFFFFFFFu);
  // patch the four fields and the texture map; 0xFFFFFFFF is not passed to the map
  std::vector<uint32_t> seen;
  const Bytes p = tp::MeshPatch(Sp(m), 9, 4, 11, 22, [&](uint32_t t) { seen.push_back(t); return uint64_t(t) + 100; });
  const auto pw = tp::MeshWords(Sp(p));
  CK(pw[0] == 9 && pw[2] == 4 && pw[4] == 11 && pw[5] == 22 && pw[6] == 2 && pw[10] == 105 && pw[0x20 - 8 + 2] == 0xFFFFFFFFu);
  CK(seen == std::vector<uint32_t>({5}));
  // nothing given: a copy
  CK(tp::MeshPatch(Sp(m)) == m);
  CK(tp::MeshPatch(Sp(m), 9) != m && tp::MeshPatch(Sp(m), std::nullopt, 4)[11] == 4);
  // a texture map that returns a value past 32 bits is the struct.error
  CK(Throws([&] { tp::MeshPatch(Sp(m), std::nullopt, std::nullopt, std::nullopt, std::nullopt, [](uint32_t) { return uint64_t(1) << 32; }); }));
  // a stage count the record cannot hold: the texture read leaves the record
  Bytes big = m;
  big[0x1B] = 9;
  CK(Throws([&] { tp::MeshTextures(Sp(big)); }));
  CK(!Throws([&] { tp::MeshWords(Sp(Bytes(2, 0))); }));  // fewer than 4 bytes: zero words, no error
  CK(Throws([&] { tp::MeshStageBase(Sp(Bytes(0x18, 0))); }));
}

// ---------------------------------------------------------------------------------------------------------------
// geo_info_round_trip
// ---------------------------------------------------------------------------------------------------------------

static tp::Decl SynthDecl(uint32_t seed) {
  tp::Decl d;
  d.head = {0, 0, 0, 36, 0, 4, 1, 0, 0, 0, 0, 0};
  d.head[2] = seed;
  d.elems = {{0, 0, 0x2A23B9, 0, 0, 0, 0}, {0, 12, 0x2C23A5, 0, 5, 0, 0}, {0xFF, 0, 0xFFFFFF, 0, 0, 0, 0}};
  TB t;
  t.u32(1).u32(20).u32(0).u32(0);  // vb index 1, stride 20
  d.tails = {t.b};
  return d;
}

static void TestGeoInfoRoundTrip() {
  Group("geo_info_round_trip");
  // One decl: head 0x30, elements 3 * 12 = 36 bytes, padded so the tails start at 0x60 (0x30 + 48): the entry is 0x60 + 16
  // = 112 bytes. head[0] = 0x60, head[1] = 1, head[4] = 3.
  const tp::Decl d = SynthDecl(2);
  const Bytes db = d.Build();
  CK(db.size() == 112);
  TB want;
  want.u32(0x60).u32(1).u32(2).u32(36).u32(3).u32(4).u32(1).zeros(20);
  want.u16(0).u16(0).u32(0x2A23B9).u8(0).u8(0).u8(0).u8(0);
  want.u16(0).u16(12).u32(0x2C23A5).u8(0).u8(5).u8(0).u8(0);
  want.u16(0xFF).u16(0).u32(0xFFFFFF).u8(0).u8(0).u8(0).u8(0);
  want.zeros(12);
  want.u32(1).u32(20).u32(0).u32(0);
  CK(db == want.b);
  CK(d.Vb() == 1 && d.Stride() == 20);
  {
    const auto [back, end] = tp::Decl::Parse(Sp(db), 0);
    CK(end == 112 && back.elems == d.elems && back.tails == d.tails && back.head[0] == 0x60 && back.head[4] == 3);
    CK(back.Build() == db);
  }
  // ObjGeo: the chunk has a 0x40 table offset (the 16-byte pre at 0x30), the mesh table (2 words, padded) at 0x40, the
  // GeoDecl chunk at 0x50: head 0x30 + table 16 + the entry (112, a multiple of 16) = 0xB0 bytes, so the first mesh is
  // at 0x50 + 0xB0 = 0x100, the second 64 bytes later, and the chunk is 0x100 + 64 + 96 = 0x1A0 bytes.
  tp::ObjGeo og;
  og.pre = TB().u32(0x00020000).u32(5).u32(0).u32(0).b;
  og.decls = {d};
  og.meshes = {Pattern(64, 1), Pattern(96, 2)};
  const Bytes ob = og.Build();
  CK(ob.size() == 0x1A0);
  CK(ob.size() == 0x1A0 && ob[15] == 0x30 && ob[18] == 1 && ob[19] == 0xA0);  // hsz 0x30, size 0x1A0
  CK(ob.size() == 0x1A0 && ob[0x23] == 0x40 && ob[0x2B] == 0x50);              // toff 0x40, xoff 0x50
  {
    const tp::ChunkHead h = tp::ParseChunkHead(Sp(ob), 0);
    CK(h.n == 2 && h.n2 == 2 && h.toff == 0x40 && h.xoff == 0x50 && h.size == 0x1A0);
    CK(h.table == std::vector<uint32_t>({0x100, 0x140}));
    CK(tp::MagicIs(h.magic, "ObjGeo") && !tp::MagicIs(h.magic, "ObjGe") && !tp::MagicIs(h.magic, "ObjGeoX"));
    const tp::ChunkHead gh = tp::ParseChunkHead(Sp(ob), 0x50);
    CK(tp::MagicIs(gh.magic, "GeoDecl") && gh.n == 1 && gh.table == std::vector<uint32_t>({0x40}) && gh.size == 0xB0);
  }
  CK(og.NodeIndex() == 5);
  {
    const auto [back, size] = tp::ObjGeo::Parse(Sp(ob), 0);
    CK(size == 0x1A0 && back.pre == og.pre && back.decls.size() == 1 && back.meshes == og.meshes);
    CK(back.Build() == ob);
  }
  // MdlGeo wraps two objects; the second sits after the first (offsets in the outer table)
  {
    tp::ObjGeo og2 = og;
    og2.meshes = {Pattern(32, 4)};
    const Bytes mg = tp::BuildMdlGeo({og, og2});
    const tp::ChunkHead h = tp::ParseChunkHead(Sp(mg), 0);
    CK(h.n == 2 && h.table[0] == 0x40 && h.table[1] == 0x40 + 0x1A0);
    const auto objs = tp::ParseMdlGeo(Sp(mg));
    CK(objs.size() == 2 && objs[1].meshes.size() == 1 && objs[1].meshes[0] == Pattern(32, 4));
    CK(tp::BuildMdlGeo(objs) == mg);
  }
  // a mesh that is not a multiple of 16 is the assert; so is an element list not ending in 0xFF
  {
    tp::ObjGeo bad = og;
    bad.meshes = {Pattern(65, 1)};
    CK(Throws([&] { bad.Build(); }));
    tp::Decl nd = d;
    nd.elems.back().a = 0x7F;
    const Bytes nb = nd.Build();
    CK(Throws([&] { tp::Decl::Parse(Sp(nb), 0); }));
    Bytes wrongtoff = ob;
    wrongtoff[0x20 + 3] = 0x50;  // toff word of the ObjGeo head
    CK(Throws([&] { tp::ObjGeo::Parse(Sp(wrongtoff), 0); }));
    Bytes cut(ob.begin(), ob.begin() + 0x60);
    CK(Throws([&] { tp::ObjGeo::Parse(Sp(cut), 0); }));
    // a tail count that the file cannot hold, and one that is absurd
    Bytes db2 = db;
    db2[7] = 200;  // head[1] = 200 tails: slices clamp, so this parses with empty tails
    CK(tp::Decl::Parse(Sp(db2), 0).first.tails.size() == 200);
    db2[4] = 0x7F;  // head[1] = 0x7F0000C8: refused
    CK(Throws([&] { tp::Decl::Parse(Sp(db2), 0); }));
  }
  // ObjInfo: pre = 12 words (48) + bounds (48) + a palette of 3 halves padded to 16 = 112, so the table starts at 0x30 + 112 = 0xA0
  // (0x90 + al(6, 16)), the two 0x50-byte records at 0xB0 and 0x100, and the chunk is 0x150 bytes.
  tp::ObjInfo oi;
  oi.words = {0x00010001, 5, 0, 99, 0, 0, 0, 0, 0, 0, 0, 0};
  oi.bounds = Pattern(48, 5);
  oi.palette = {1, 2, 3};
  oi.infos = {Pattern(0x50, 6), Pattern(0x50, 7)};
  const Bytes ib = oi.Build();
  CK(ib.size() == 0x150);
  {
    const tp::ChunkHead h = tp::ParseChunkHead(Sp(ib), 0);
    CK(h.n == 2 && h.toff == 0xA0 && h.table == std::vector<uint32_t>({0xB0, 0x100}) && h.size == 0x150);
    CK(ib.size() == 0x150 && ib[0x30 + 15] == 3);  // words[3] was rewritten to the palette length (3)
    const tp::ObjInfo back = tp::ObjInfo::Parse(Sp(ib), 0);
    CK(back.palette == oi.palette && back.bounds == oi.bounds && back.infos == oi.infos && back.words[3] == 3 && back.words[1] == 5);
    CK(back.Build() == ib);
    // no palette: pre is 96 bytes, the table at 0x90
    tp::ObjInfo np = oi;
    np.palette.clear();
    const Bytes nb = np.Build();
    CK(tp::ParseChunkHead(Sp(nb), 0).toff == 0x90 && tp::ObjInfo::Parse(Sp(nb), 0).Build() == nb);
    Bytes badpad = ib;
    badpad[0x90 + 6] = 1;  // inside the palette padding
    CK(Throws([&] { tp::ObjInfo::Parse(Sp(badpad), 0); }));
  }
  {
    const Bytes mi = tp::BuildMdlInfo({oi, oi});
    const auto objs = tp::ParseMdlInfo(Sp(mi));
    CK(objs.size() == 2 && tp::BuildMdlInfo(objs) == mi);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// cat_helpers
// ---------------------------------------------------------------------------------------------------------------

static void TestCatHelpers() {
  Group("cat_helpers");
  // directory at 8: {a, b, type, z, value} 8-byte entries until a == 0
  {
    TB c;
    c.u32(0x43415400).u32(8);
    c.u8(1).u8(0).u8(2).u8(0).u32(0x100);
    c.u8(1).u8(9).u8(4).u8(7).u32(0x200);
    c.u8(1).u8(0).u8(5).u8(0).u32(0x300);
    c.u8(0).u8(0).u8(6).u8(0).u32(0x400);  // a == 0 ends it
    c.u8(1).u8(0).u8(7).u8(0).u32(0x500);
    const auto dir = tp::CatDir(Sp(c.b));
    CK((dir == std::vector<std::pair<uint8_t, uint32_t>>({{2, 0x100}, {4, 0x200}, {5, 0x300}})));
    // an entry cut short by the end of the file stops the walk (q + 8 <= len)
    Bytes cut(c.b.begin(), c.b.begin() + 8 + 8 + 5);
    CK(tp::CatDir(Sp(cut)).size() == 1);
    // the directory offset itself must be readable
    CK(Throws([&] { tp::CatDir(Sp(Bytes(7, 1))); }));
    // an offset past the end: an empty directory
    TB far;
    far.u32(0).u32(0x1000);
    CK(tp::CatDir(Sp(far.b)).empty());
  }
  // parse_t2: 3-byte entries (0x90/0x91 first), anything else starts a group (0 ends)
  {
    TB c;
    c.zeros(4);                       // offset 4: the groups
    c.u8(0x90).u8(0x01).u8(0x05);     // (0x9001, 5)
    c.u8(0x91).u8(0x02).u8(0x06);     // (0x9102, 6)
    c.u8(0x03);                       // closes 'main', starts group 3
    c.u8(0x90).u8(0x00).u8(0x09);     // (0x9000, 9)
    c.u8(0x07);                       // closes group 3, starts group 7 (empty)
    c.u8(0x00);                       // closes group 7 and ends
    c.u8(0xAA);
    const auto [groups, next] = tp::ParseT2(Sp(c.b), 4);
    CK(groups.size() == 3 && next == 16);
    CK(groups.size() == 3 && groups[0].is_main && groups[0].items.size() == 2 && groups[0].items[0] == std::make_pair(uint16_t(0x9001), uint8_t(5)) &&
       groups[0].items[1] == std::make_pair(uint16_t(0x9102), uint8_t(6)));
    CK(groups.size() == 3 && !groups[1].is_main && groups[1].kind == 3 && groups[1].items.size() == 1 && groups[1].items[0].first == 0x9000);
    CK(groups.size() == 3 && !groups[2].is_main && groups[2].kind == 7 && groups[2].items.empty());
    // never terminated: the read leaves the buffer
    Bytes open(c.b.begin(), c.b.begin() + 4 + 9);
    CK(Throws([&] { tp::ParseT2(Sp(open), 4); }));
    // a 0x90 entry cut after its first byte
    Bytes cut2 = {0x90, 0x01};
    CK(Throws([&] { tp::ParseT2(Sp(cut2), 0); }));
    CK(Throws([&] { tp::ParseT2(Sp(cut2), 9); }));
  }
  // t4_records: 0x40-byte records until one starts with 0xFFFF
  {
    TB c;
    c.zeros(2);
    c.cat(Pattern(0x40, 1)).cat(Pattern(0x40, 2));
    c.b[2] = 0x00;  // first record starts 00 01... (not FFFF)
    c.u16(0xFFFF).fill(3, 6);
    const auto recs = tp::T4Records(Sp(c.b), 2);
    CK(recs.size() == 2 && recs[0].size() == 0x40 && recs[1] == Pattern(0x40, 2));
    CK(recs.size() == 2 && recs[0][0] == 0x00);
    Bytes none = {0xFF, 0xFF, 1, 2};
    CK(tp::T4Records(Sp(none), 0).empty());
    CK(Throws([&] { tp::T4Records(Sp(Bytes(0x40 + 1, 1)), 0); }));  // no terminator
  }
}

// ---------------------------------------------------------------------------------------------------------------
// real_round_trip (read only)
// ---------------------------------------------------------------------------------------------------------------

static const char* Env(const char* name) {
  static std::map<std::string, std::string> cache;  // _dupenv_s copies; keep the copy alive
  char* buf = nullptr;
  size_t len = 0;
  if (_dupenv_s(&buf, &len, name) != 0 || !buf) return nullptr;
  cache[name] = buf;
  free(buf);
  return cache[name].empty() ? nullptr : cache[name].c_str();
}

static void SkipGroup(const char* group, const char* var) {
  ++g_skipped;
  std::printf("%s: SKIPPED (%s unset)\n", group, var);
}

// Files known not to round trip through the Python either (label -> why); empty unless the report lists one.
static const std::map<std::string, std::string>& KnownNoRoundTrip() {
  static const std::map<std::string, std::string> k = {};
  return k;
}

// Parse then Build(align) for each entry; returns how many equal the original. Prints every other one (label, size, first
// differing offset) and counts it as a failed check unless it is in KnownNoRoundTrip.
// If other_align is non-zero, *other_differs gets how many files Build(other_align) does NOT reproduce (a check that the
// section is sensitive to the alignment: real DOAX2 blobs are all multiples of 4, so there align 1 and 4 agree).
static int RoundTripAll(InputSet& set, const std::vector<EntryRef>& entries, int align, const char* group,
                        int other_align = 0, int* other_differs = nullptr) {
  int ok = 0;
  for (const EntryRef& e : entries) {
    const std::string label = EntryLabel(e);
    std::shared_ptr<const std::vector<uint8_t>> bytes;
    try {
      bytes = set.Read(e);
    } catch (const std::exception& ex) {
      CKF(false, "%s: Read %s: %s", group, label.c_str(), ex.what());
      continue;
    }
    try {
      const tp::Xpr x = tp::Xpr::Parse(Sp(*bytes));
      const Bytes rebuilt = x.Build(align);
      if (other_align && other_differs && x.Build(other_align) != *bytes) ++*other_differs;
      if (rebuilt == *bytes) {
        ++ok;
        CK(true);
        continue;
      }
      size_t at = 0;
      while (at < rebuilt.size() && at < bytes->size() && rebuilt[at] == (*bytes)[at]) ++at;
      std::printf("  no round trip: %s (%zu bytes, rebuilt %zu, first difference at %zu)\n", label.c_str(), bytes->size(),
                  rebuilt.size(), at);
    } catch (const std::exception& ex) {
      std::printf("  no round trip: %s (%zu bytes): %s\n", label.c_str(), bytes->size(), ex.what());
    }
    CKF(KnownNoRoundTrip().count(label) != 0, "%s: %s does not round trip", group, label.c_str());
  }
  return ok;
}

static void TestRealDoax2() {
  const char* disc = Env("DOA4_SUITCONV_DISC");
  if (!disc) return SkipGroup("real_round_trip_doax2", "DOA4_SUITCONV_DISC");
  Group("real_round_trip_doax2");
  std::printf("disc: %s\n", disc);
  std::string error;
  auto set = InputSet::Open(fs::path(disc), fs::path(), DiscEntries(), {}, &error);
  CKF(set != nullptr, "Open: %s", error.c_str());
  if (!set) return;
  // The table holds 18 body entries (tpr, cat, ...) and then the 34 suits' (tpr, cat) pairs: every even position is a .tpr.
  std::vector<EntryRef> tprs;
  for (size_t i = 0; i < DiscEntries().size(); i += 2) tprs.push_back(DiscEntries()[i]);
  const auto t0 = std::chrono::steady_clock::now();
  const int ok = RoundTripAll(*set, tprs, 4, "doax2");
  std::printf("real_round_trip_doax2: %d/%zu .tpr parse and rebuild (align 4) to the original bytes, %.1f s\n", ok, tprs.size(),
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  CK(tprs.size() == 43);
}

static void TestRealDoa4() {
  const char* dir = Env("DOA4_SUITCONV_GAME_DIR");
  if (!dir) return SkipGroup("real_round_trip_doa4", "DOA4_SUITCONV_GAME_DIR");
  Group("real_round_trip_doa4");
  std::printf("game: %s\n", dir);
  std::string error;
  auto set = InputSet::Open(fs::path(), fs::path(dir), {}, GameEntries(), &error);
  CKF(set != nullptr, "Open: %s", error.c_str());
  if (!set) return;
  // The table holds ten (tpr, cat) pairs (the nine references and Tina's hand donor), then rtm01.cat alone: every even
  // position below 20 is a .tpr.
  std::vector<EntryRef> tprs;
  const auto g = GameEntries();
  for (size_t i = 0; i + 1 < g.size(); i += 2) tprs.push_back(g[i]);
  const auto t0 = std::chrono::steady_clock::now();
  int differs4 = 0;
  const int ok = RoundTripAll(*set, tprs, 1, "doa4", 4, &differs4);
  std::printf("real_round_trip_doa4: %d/%zu .tpr parse and rebuild (align 1) to the original bytes, %.1f s\n", ok, tprs.size(),
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  std::printf("real_round_trip_doa4: %d/%zu rebuild differently with Build(4)\n", differs4, tprs.size());
  CKF(differs4 >= 1, "no DOA4 file tells align 1 from align 4");
  CK(tprs.size() == 10);
}

// A group that throws (a defect that makes a parse throw) loses its remaining checks, never the other groups'.
static void RunGroup(void (*fn)(), const char* name) {
  try {
    fn();
  } catch (const std::exception& e) {
    ++g_checks;
    ++g_failures;
    std::printf("FAIL %s threw: %s\n", name, e.what());
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash (a mutated bounds check) must not swallow what was printed before it
  doa4::suitconv::num::FpEnvGuard fp;        // the converter's floating-point environment (MXCSR 0x1F80)
  RunGroup(TestReaderBounds, "reader_bounds");
  RunGroup(TestTableChunkLayout, "table_chunk_layout");
  RunGroup(TestXprRoundTrip, "xpr_round_trip_synthetic");
  RunGroup(TestFetchConstants, "fetch_constants");
  RunGroup(TestWorldMatsPins, "world_mats_pins");
  RunGroup(TestHend3nAllEdges, "hend3n_all_edges");
  RunGroup(TestDecodeElem, "decode_elem");
  RunGroup(TestMeshRecords, "mesh_records");
  RunGroup(TestGeoInfoRoundTrip, "geo_info_round_trip");
  RunGroup(TestCatHelpers, "cat_helpers");
  RunGroup(TestRealDoax2, "real_round_trip_doax2");
  RunGroup(TestRealDoa4, "real_round_trip_doa4");
  std::printf("skipped sections: %d\n", g_skipped);
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
