// Suit converter (Plan 2): the XPR2 container, the chunk tables and the .cat directory readers and writers. A name-for-name
// port of the recipe's tpr.py (physics_spike/recipe_m1/tpr.py, SHA-1 9d11ca4827cbc781c8833bd55045e43fb30d14c7) plus the
// three .cat helpers of convert.py (cat_dir, parse_t2, t4_records, lines 125-165). Python's name -> C++ name:
// snake_case -> CamelCase (Xpr.parse -> Xpr::Parse, world_mats -> WorldMats, dec_hend3n -> DecHend3n ...). The Python's
// asserts are ConvertError with the assert's text; every read goes through be::Reader, so a malformed file throws
// ConvertError and never reads out of bounds.
//
// Ported: u32, al, pad16, Xpr (parse, build, of_type), buf_span, tex_fc, tex_addrs, tex_set, chunk_head, make_head,
// build_table_chunk, Node, parse_hie, mat_floats, build_hie, world_mats, Decl (parse_decl, build, vb, stride), ObjGeo
// (parse_objgeo, build, node), parse_mdlgeo, build_mdlgeo, mesh_words, mesh_stage_base, mesh_textures, mesh_patch,
// ObjInfo (parse_objinfo, build), parse_mdlinfo, build_mdlinfo, DT, DT_BY_NAME, USAGE, half, dec_hend3n, decode_elem;
// cat_dir, parse_t2, t4_records. NOT ported (never reached by the 34 suits): buf_set, encode_elem, Node.raw_mat,
// ObjGeo.set_node, tex_info, Xpr.users.
//
// Differences a Python caller could see (all on inputs the real files never have):
//  - A Node whose matrix is 16 doubles must hold exactly 16 (Python lets a list be any length).
//  - Decl::Parse refuses a tail count above 1 << 20 (Python would slice empty bytes four billion times).
//  - WorldMats refuses a parent chain deeper than 900 or a cycle (Python raises RecursionError near 1000).
// Pure C++ (no SDK).

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "doa4_suitconv_be.h"
#include "doa4_suitconv_num.h"

namespace doa4::suitconv::tpr {

using Bytes = std::vector<uint8_t>;
using Fourcc = std::array<uint8_t, 4>;

constexpr Fourcc MakeFourcc(const char (&s)[5]) {
  return {uint8_t(s[0]), uint8_t(s[1]), uint8_t(s[2]), uint8_t(s[3])};
}

// al(x, a): x rounded up to a multiple of a. Throws ConvertError for a == 0 (ZeroDivisionError).
uint64_t Al(uint64_t x, uint64_t a);
// pad16(b): b followed by zeros up to a multiple of 16.
Bytes Pad16(std::span<const uint8_t> b);

// ---------------------------------------------------------------- XPR2 container
struct Res {
  Fourcc type;
  Bytes blob;
};

// res: USER blobs and descriptors in table order; data: the data section. hs is the header size Parse read.
struct Xpr {
  std::vector<Res> res;
  Bytes data;
  std::optional<uint32_t> hs;
  std::vector<std::pair<uint32_t, uint32_t>> orig_offs;  // per resource: (offset, name offset) as read

  static Xpr Parse(std::span<const uint8_t> d);
  // align: blob alignment in the header (DOA4 files 1, DOAX2 files 4).
  Bytes Build(int align = 1) const;
  std::vector<size_t> OfType(const Fourcc& fourcc) const;
};

// ---------------------------------------------------------------- fetch constants
// VBUF/IBUF descriptor -> (base, size) in the data section.
std::pair<uint32_t, uint32_t> BufSpan(std::span<const uint8_t> desc);
std::array<uint32_t, 6> TexFc(std::span<const uint8_t> desc);
std::pair<uint32_t, uint32_t> TexAddrs(std::span<const uint8_t> desc);
// tex_set: base and mip are Python ints; a result that does not fit a u32 is the struct.error. mip must be 0 when the
// descriptor has no mip address (assert).
Bytes TexSet(std::span<const uint8_t> desc, int64_t base, int64_t mip);

// ---------------------------------------------------------------- chunks
inline constexpr uint32_t kChunkVer = 0xFF000100;

struct ChunkHead {
  std::array<uint8_t, 8> magic;
  uint32_t size, n, n2, toff, xoff;
  std::array<uint32_t, 3> z;
  std::vector<uint32_t> table;
};
ChunkHead ParseChunkHead(std::span<const uint8_t> d, uint64_t off);
// True when magic.rstrip(b'\0') == name.
bool MagicIs(const std::array<uint8_t, 8>& magic, std::string_view name);

Bytes MakeHead(std::string_view magic, uint64_t size, uint64_t n, uint64_t toff, uint64_t xoff = 0,
               std::optional<uint64_t> n2 = std::nullopt);
// head + pre (bytes at 0x30) + table (padded 16) + children (each padded 16; Hie leaves the last unpadded).
Bytes BuildTableChunk(std::string_view magic, const std::vector<Bytes>& children, std::span<const uint8_t> pre = {},
                      std::optional<uint64_t> n2 = std::nullopt, bool pad_last = true);

// ---------------------------------------------------------------- Hie
// A node's matrix is kept as its 64 raw bytes (raw == true) or as 16 doubles.
struct Node {
  bool raw = true;
  Bytes mat_bytes;
  std::vector<double> mat_floats;  // 16 entries when !raw
  int64_t id = 0, parent = -1;
  std::vector<int64_t> children;
  Bytes tail;

  static Node FromBytes(std::span<const uint8_t> mat, int64_t id, int64_t parent, std::vector<int64_t> children,
                        Bytes tail = {});
  static Node FromFloats(std::span<const double> mat, int64_t id, int64_t parent, std::vector<int64_t> children,
                         Bytes tail = {});
};

std::vector<Node> ParseHie(std::span<const uint8_t> blob);
// mat_floats(m): the 16 float32 of raw bytes (big-endian) widened to double, or the node's doubles.
std::vector<double> MatFloats(const Node& n);
Bytes BuildHie(const std::vector<Node>& nodes);
// row-vector convention: world = local * world(parent); each product sum is builtin sum() (num::PySum).
std::vector<std::vector<double>> WorldMats(const std::vector<Node>& nodes);

// ---------------------------------------------------------------- MdlGeo
struct DeclElem {
  uint16_t a, b;
  uint32_t c;
  uint8_t d, e, f, g;
  bool operator==(const DeclElem&) const = default;
};

// One GeoDecl entry: head (12 u32), elements, then head[1] tails of 16 bytes.
struct Decl {
  std::array<uint32_t, 12> head{};
  std::vector<DeclElem> elems;
  std::vector<Bytes> tails;

  uint32_t Vb() const;      // u32(tails[0], 0)
  uint32_t Stride() const;  // u32(tails[0], 4)
  Bytes Build() const;
  // parse_decl: returns the decl and the offset just past its tails.
  static std::pair<Decl, uint64_t> Parse(std::span<const uint8_t> blob, uint64_t o);
};

struct ObjGeo {
  Bytes pre;  // 16 bytes at 0x30: 0x00020000, node index, 0, 0
  std::vector<Decl> decls;
  std::vector<Bytes> meshes;  // raw mesh records

  uint32_t NodeIndex() const;  // u32(pre, 4)
  Bytes Build() const;
  // parse_objgeo: returns the object and the size its chunk head says.
  static std::pair<ObjGeo, uint32_t> Parse(std::span<const uint8_t> blob, uint64_t o);
};

std::vector<ObjGeo> ParseMdlGeo(std::span<const uint8_t> blob);
Bytes BuildMdlGeo(const std::vector<ObjGeo>& objs);

// mesh record fields (u32 words): [0] mesh index, [2] decl index, [4] index start, [5] index count, [6] texture stage
// count; stages are 0x40 bytes at the end: {stage, kind, texture index, ...}
std::vector<uint32_t> MeshWords(std::span<const uint8_t> m);
// (len(m) - 0x40 * nst, nst); the base can be negative, as in Python.
std::pair<int64_t, uint32_t> MeshStageBase(std::span<const uint8_t> m);
struct MeshTexture {
  uint32_t stage, kind, texture;
};
std::vector<MeshTexture> MeshTextures(std::span<const uint8_t> m);
// mesh_patch: texmap maps a texture index that is not 0xFFFFFFFF; its result must fit a u32.
Bytes MeshPatch(std::span<const uint8_t> m, std::optional<uint64_t> index = std::nullopt,
                std::optional<uint64_t> decl = std::nullopt, std::optional<uint64_t> istart = std::nullopt,
                std::optional<uint64_t> icount = std::nullopt,
                const std::function<uint64_t(uint32_t)>& texmap = nullptr);

// ---------------------------------------------------------------- MdlInfo
struct ObjInfo {
  std::array<uint32_t, 12> words{};  // 12 u32 at 0x30: [0] 0x00010001, [1] node index, [3] palette count, ...
  Bytes bounds;                      // 48 bytes at 0x60
  std::vector<uint16_t> palette;
  std::vector<Bytes> infos;  // per-mesh 0x50-byte records

  Bytes Build() const;
  static ObjInfo Parse(std::span<const uint8_t> blob, uint64_t o);
};

std::vector<ObjInfo> ParseMdlInfo(std::span<const uint8_t> blob);
Bytes BuildMdlInfo(const std::vector<ObjInfo>& objs);

// ---------------------------------------------------------------- vertex formats
struct DtEntry {
  uint32_t typ;
  std::string_view name;
  int size;
};
std::span<const DtEntry> DT();                     // the DT table
const DtEntry* DtFind(uint32_t typ);               // DT[typ] or null
std::optional<uint32_t> DtByName(std::string_view name);  // DT_BY_NAME
std::string_view UsageName(uint32_t usage);       // USAGE[usage], empty when absent

// struct.unpack('>e', struct.pack('>H', h))
double Half(uint16_t h);
// dec_hend3n(v): three signed-normalized fields, 11 + 11 + 10 bits.
std::array<double, 3> DecHend3n(uint32_t v);

// decode_elem's tuple: n values (floats, or the integers of UB4N / COLOR / UB4 as exact doubles).
struct ElemValue {
  int n = 0;
  std::array<double, 4> v{};
};
// Throws ConvertError for a type that is not in DT (KeyError) or is DEC3N (ValueError).
ElemValue DecodeElem(std::span<const uint8_t> d, int64_t o, uint32_t typ);

// ---------------------------------------------------------------- .cat directory (convert.py)
// cat_dir(c): the (type, value) pairs of the directory at u32(c, 4), up to the first zero type.
std::vector<std::pair<uint8_t, uint32_t>> CatDir(std::span<const uint8_t> c);

struct T2Group {
  bool is_main = true;  // the first group's kind is 'main'
  uint8_t kind = 0;     // otherwise the byte that started the group
  std::vector<std::pair<uint16_t, uint8_t>> items;
};
// parse_t2(c, off): the groups and the offset just past the terminating zero.
std::pair<std::vector<T2Group>, uint64_t> ParseT2(std::span<const uint8_t> c, uint64_t off);

// t4_records(c, off): 0x40-byte records until one starts with 0xFFFF.
std::vector<Bytes> T4Records(std::span<const uint8_t> c, uint64_t off);

}  // namespace doa4::suitconv::tpr
