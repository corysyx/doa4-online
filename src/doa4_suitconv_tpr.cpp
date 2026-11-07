// Suit converter (Plan 2): the XPR2 container, the chunk tables and the .cat directory readers and writers. See
// doa4_suitconv_tpr.h. Each function names the tpr.py / convert.py function it ports; the line numbers are those of
// tpr.py with SHA-1 9d11ca4827cbc781c8833bd55045e43fb30d14c7 (convert.py for the cat helpers).

#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_tpr.h"

#include <algorithm>
#include <cstring>

namespace doa4::suitconv::tpr {

using be::Reader;
using be::Writer;

namespace {

[[noreturn]] void Fail(const char* what) { throw ConvertError(what); }

void Assert(bool ok, const char* text) {
  if (!ok) throw ConvertError(std::string("assert ") + text);
}

bool AllZero(std::span<const uint8_t> s) {
  for (uint8_t b : s)
    if (b) return false;
  return true;
}

Bytes Cat(std::initializer_list<std::span<const uint8_t>> parts) {
  Bytes out;
  for (auto p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

void PutBytes(Bytes& out, std::span<const uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); }

std::span<const uint8_t> S(const Bytes& b) { return std::span<const uint8_t>(b.data(), b.size()); }

}  // namespace

// tpr.py al (line 24)
uint64_t Al(uint64_t x, uint64_t a) {
  if (a == 0) Fail("integer division or modulo by zero");
  return (x + a - 1) / a * a;
}

// tpr.py pad16 (line 28)
Bytes Pad16(std::span<const uint8_t> b) {
  Bytes out(b.begin(), b.end());
  out.resize(static_cast<size_t>(Al(b.size(), 16)), 0);
  return out;
}

// ---------------------------------------------------------------- XPR2 container
// Xpr.parse (line 42)
Xpr Xpr::Parse(std::span<const uint8_t> d) {
  const Reader r(d);
  const auto head = r.Slice(0, 4);
  Assert(head.size() == 4 && std::memcmp(head.data(), "XPR2", 4) == 0, "d[:4] == b'XPR2'");
  const uint32_t hs = r.U32(4), ds = r.U32(8), n = r.U32(12);
  Xpr x;
  for (uint32_t i = 0; i < n; ++i) {
    const int64_t at = 16 + 16 * static_cast<int64_t>(i);
    Fourcc t;
    for (int k = 0; k < 4; ++k) t[k] = r.U8(at + k);
    be::ResolveOffset(at, d.size(), 16);  // the whole 16-byte record must be present, as unpack_from('>4sIII') requires
    const uint32_t o = r.U32(at + 4), s = r.U32(at + 8), no = r.U32(at + 12);
    const auto blob = r.Slice(12ull + o, 12ull + o + s);
    x.res.push_back(Res{t, Bytes(blob.begin(), blob.end())});
    x.orig_offs.emplace_back(o, no);
  }
  Assert(static_cast<uint64_t>(d.size()) == 12ull + hs + ds, "len(d) == 12 + hs + ds");
  const auto data = r.From(12ull + hs);
  x.data.assign(data.begin(), data.end());
  x.hs = hs;
  return x;
}

// Xpr.build (line 54)
Bytes Xpr::Build(int align) const {
  if (align < 1) Fail("alignment must be at least 1");
  const uint64_t n = res.size();
  uint64_t pos = 16 + 16 * n - 12;  // relative to byte 12
  std::optional<uint64_t> user0;
  std::vector<uint64_t> offs;
  offs.reserve(res.size());
  static constexpr Fourcc kUser = MakeFourcc("USER");
  for (const Res& e : res) {
    pos = Al(pos, static_cast<uint64_t>(align));
    if (e.type == kUser && !user0) user0 = pos;
    offs.push_back(pos);
    pos += e.blob.size();
  }
  const uint64_t hs_new = Al(12 + pos, 0x1000) - 12;
  Bytes out(static_cast<size_t>(12 + hs_new), 0);
  {
    Writer w;
    static constexpr uint8_t kMagic[4] = {'X', 'P', 'R', '2'};
    w.Bytes(kMagic);
    w.U32(hs_new);
    w.U32(data.size());
    w.U32(n);
    std::memcpy(out.data(), w.bytes().data(), 16);
  }
  for (size_t i = 0; i < res.size(); ++i) {
    if (!user0) Fail("required argument is not an integer");
    Writer w;
    w.Bytes(res[i].type);
    w.U32(offs[i]);
    w.U32(res[i].blob.size());
    w.U32(*user0);
    std::memcpy(out.data() + 16 + 16 * i, w.bytes().data(), 16);
    if (!res[i].blob.empty()) std::memcpy(out.data() + 12 + offs[i], res[i].blob.data(), res[i].blob.size());
  }
  PutBytes(out, S(data));
  return out;
}

// Xpr.of_type (line 77)
std::vector<size_t> Xpr::OfType(const Fourcc& fourcc) const {
  std::vector<size_t> out;
  for (size_t i = 0; i < res.size(); ++i)
    if (res[i].type == fourcc) out.push_back(i);
  return out;
}

// ---------------------------------------------------------------- fetch constants
// buf_span (line 82)
std::pair<uint32_t, uint32_t> BufSpan(std::span<const uint8_t> desc) {
  const Reader r(desc);
  const int64_t at = static_cast<int64_t>(be::ResolveOffset(static_cast<int64_t>(desc.size()) - 8, desc.size(), 8));
  const uint32_t w0 = r.U32(at), w1 = r.U32(at + 4);
  return {w0 & ~3u, w1 & ~3u};
}

// tex_fc (line 94)
std::array<uint32_t, 6> TexFc(std::span<const uint8_t> desc) {
  const Reader r(desc);
  const int64_t at = static_cast<int64_t>(be::ResolveOffset(static_cast<int64_t>(desc.size()) - 24, desc.size(), 24));
  std::array<uint32_t, 6> fc;
  for (int i = 0; i < 6; ++i) fc[i] = r.U32(at + 4 * i);
  return fc;
}

// tex_addrs (line 98)
std::pair<uint32_t, uint32_t> TexAddrs(std::span<const uint8_t> desc) {
  const auto fc = TexFc(desc);
  return {fc[1] & 0xFFFFF000u, fc[5] & 0xFFFFF000u};
}

// tex_set (line 103)
Bytes TexSet(std::span<const uint8_t> desc, int64_t base, int64_t mip) {
  auto fc = TexFc(desc);
  fc[1] = be::CheckU32S(static_cast<int64_t>(fc[1] & 0xFFF) | base);
  if (fc[5] & 0xFFFFF000u) {
    fc[5] = be::CheckU32S(static_cast<int64_t>(fc[5] & 0xFFF) | mip);
  } else {
    Assert(mip == 0, "mip == 0");
  }
  Bytes out(desc.begin(), desc.end() - 24);
  Writer w;
  for (uint32_t v : fc) w.U32(v);
  PutBytes(out, S(w.bytes()));
  return out;
}

// ---------------------------------------------------------------- chunks
// chunk_head (line 125)
ChunkHead ParseChunkHead(std::span<const uint8_t> d, uint64_t off) {
  const Reader r(d);
  ChunkHead h{};
  const auto m = r.Slice(off, off + 8);
  std::copy(m.begin(), m.end(), h.magic.begin());
  const int64_t b = static_cast<int64_t>(be::ResolveOffset(static_cast<int64_t>(off) + 8, d.size(), 40));  // ten words, one field
  const uint32_t ver = r.U32(b), hsz = r.U32(b + 4);
  h.size = r.U32(b + 8);
  h.n = r.U32(b + 12);
  h.n2 = r.U32(b + 16);
  h.z[0] = r.U32(b + 20);
  h.toff = r.U32(b + 24);
  h.z[1] = r.U32(b + 28);
  h.xoff = r.U32(b + 32);
  h.z[2] = r.U32(b + 36);
  Assert(ver == kChunkVer && hsz == 0x30, "ver == CHUNK_VER and hsz == 0x30");
  if (h.n) h.table = r.U32s(static_cast<int64_t>(off) + h.toff, h.n);
  return h;
}

bool MagicIs(const std::array<uint8_t, 8>& magic, std::string_view name) {
  size_t len = magic.size();
  while (len > 0 && magic[len - 1] == 0) --len;
  return len == name.size() && std::memcmp(magic.data(), name.data(), len) == 0;
}

// make_head (line 133): magic.ljust(8, b'\0') (never truncated) + the ten words.
Bytes MakeHead(std::string_view magic, uint64_t size, uint64_t n, uint64_t toff, uint64_t xoff,
               std::optional<uint64_t> n2) {
  Bytes out(magic.begin(), magic.end());
  if (out.size() < 8) out.resize(8, 0);
  Writer w;
  w.U32(kChunkVer);
  w.U32(0x30);
  w.U32(size);
  w.U32(n);
  w.U32(n2 ? *n2 : n);
  w.U32(0);
  w.U32(toff);
  w.U32(0);
  w.U32(xoff);
  w.U32(0);
  PutBytes(out, S(w.bytes()));
  return out;
}

// build_table_chunk (line 138)
Bytes BuildTableChunk(std::string_view magic, const std::vector<Bytes>& children, std::span<const uint8_t> pre,
                      std::optional<uint64_t> n2, bool pad_last) {
  const uint64_t toff = 0x30 + pre.size();
  Assert(toff % 16 == 0, "toff % 16 == 0");
  const uint64_t n = children.size();
  const uint64_t tsize = Al(4 * n, 16);
  uint64_t pos = toff + tsize;
  std::vector<uint64_t> offs;
  Bytes body;
  for (size_t k = 0; k < children.size(); ++k) {
    offs.push_back(pos);
    if (pad_last || k + 1 < children.size()) {
      const Bytes cc = Pad16(S(children[k]));
      PutBytes(body, S(cc));
      pos += cc.size();
    } else {
      PutBytes(body, S(children[k]));
      pos += children[k].size();
    }
  }
  Bytes table;
  if (n) {
    Writer w;
    for (uint64_t o : offs) w.U32(o);
    table = Pad16(S(w.bytes()));
  }
  const Bytes head = MakeHead(magic, pos, n, toff, 0, n2);
  return Cat({S(head), pre, S(table), S(body)});
}

// ---------------------------------------------------------------- Hie
Node Node::FromBytes(std::span<const uint8_t> mat, int64_t id, int64_t parent, std::vector<int64_t> children,
                     Bytes tail) {
  Node n;
  n.raw = true;
  n.mat_bytes.assign(mat.begin(), mat.end());
  n.id = id;
  n.parent = parent;
  n.children = std::move(children);
  n.tail = std::move(tail);
  return n;
}

Node Node::FromFloats(std::span<const double> mat, int64_t id, int64_t parent, std::vector<int64_t> children,
                      Bytes tail) {
  if (mat.size() != 16) Fail("a float matrix holds exactly 16 values");
  Node n;
  n.raw = false;
  n.mat_floats.assign(mat.begin(), mat.end());
  n.id = id;
  n.parent = parent;
  n.children = std::move(children);
  n.tail = std::move(tail);
  return n;
}

// parse_hie (line 172)
std::vector<Node> ParseHie(std::span<const uint8_t> blob) {
  const ChunkHead h = ParseChunkHead(blob, 0);
  Assert(MagicIs(h.magic, "Hie"), "h['magic'].rstrip(b'\\0') == b'Hie'");
  const Reader br(blob);
  std::vector<Node> nodes;
  std::vector<uint32_t> offs = h.table;
  offs.push_back(h.size);
  for (uint32_t i = 0; i < h.n; ++i) {
    const auto rec = br.Slice(offs[i], offs[i + 1]);
    const Reader rr(rec);
    const auto mat = rr.Slice(0, 64);
    const int16_t nid = rr.I16(64), par = rr.I16(66), nc = rr.I16(68);
    if (nc < 0) Fail("bad char in struct format");
    const auto ch = rr.I16s(70, static_cast<uint64_t>(nc));
    const uint64_t used = 70 + 2 * static_cast<uint64_t>(nc);
    const auto tail = rr.From(used);
    Assert(AllZero(tail), "tail == bytes(len(tail)) (Hie record tail not zero)");
    Assert(rec.size() == (i + 1 < h.n ? Al(used, 16) : used), "len(rec) == (al(used, 16) if i < h['n'] - 1 else used)");
    nodes.push_back(Node::FromBytes(mat, nid, par, std::vector<int64_t>(ch.begin(), ch.end())));
  }
  return nodes;
}

// mat_floats (line 191)
std::vector<double> MatFloats(const Node& n) {
  if (!n.raw) return n.mat_floats;
  if (n.mat_bytes.size() != 64) Fail("unpack requires a buffer of 64 bytes");
  const Reader r(S(n.mat_bytes));
  std::vector<double> out(16);
  for (int i = 0; i < 16; ++i) out[i] = r.F32(4 * i);
  return out;
}

// build_hie (line 195)
Bytes BuildHie(const std::vector<Node>& nodes) {
  std::vector<Bytes> recs;
  for (const Node& nd : nodes) {
    Writer w;
    if (nd.raw) {
      w.Bytes(S(nd.mat_bytes));
    } else {
      if (nd.mat_floats.size() != 16) Fail("pack expected 16 items for packing");
      for (double v : nd.mat_floats) w.F32(v);
    }
    w.I16(nd.id);
    w.I16(nd.parent);
    w.I16(static_cast<int64_t>(nd.children.size()));
    for (int64_t c : nd.children) w.I16(c);
    recs.push_back(w.Take());
  }
  return BuildTableChunk("Hie", recs, {}, std::nullopt, false);
}

namespace {

// world_mats' mul (line 208): sum(a[r*4+k] * b[k*4+c] for k in range(4)), builtin sum() of floats from the int 0.
std::vector<double> Mul(const std::vector<double>& a, const std::vector<double>& b) {
  std::vector<double> out;
  out.reserve(16);
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) {
      num::PySum s;
      for (int k = 0; k < 4; ++k) {
        const double prod = a[r * 4 + k] * b[k * 4 + c];
        s.Add(prod);
      }
      out.push_back(s.Result());
    }
  return out;
}

const std::vector<double>& WorldOf(const std::vector<Node>& nodes, std::vector<std::vector<double>>& out,
                                   std::vector<bool>& done, int64_t i, int depth) {
  if (depth > 900) Fail("maximum recursion depth exceeded");
  if (i < 0 || static_cast<uint64_t>(i) >= nodes.size()) Fail("list index out of range");
  if (!done[static_cast<size_t>(i)]) {
    std::vector<double> m = MatFloats(nodes[static_cast<size_t>(i)]);
    const int64_t p = nodes[static_cast<size_t>(i)].parent;
    if (p < 0) {
      out[static_cast<size_t>(i)] = std::move(m);
    } else {
      const std::vector<double>& pw = WorldOf(nodes, out, done, p, depth + 1);
      out[static_cast<size_t>(i)] = Mul(m, pw);
    }
    done[static_cast<size_t>(i)] = true;
  }
  return out[static_cast<size_t>(i)];
}

}  // namespace

// world_mats (line 204)
std::vector<std::vector<double>> WorldMats(const std::vector<Node>& nodes) {
  std::vector<std::vector<double>> out(nodes.size());
  std::vector<bool> done(nodes.size(), false);
  for (size_t i = 0; i < nodes.size(); ++i) WorldOf(nodes, out, done, static_cast<int64_t>(i), 0);
  return out;
}

// ---------------------------------------------------------------- MdlGeo
uint32_t Decl::Vb() const {
  if (tails.empty()) Fail("list index out of range");
  return Reader(S(tails[0])).U32(0);
}
uint32_t Decl::Stride() const {
  if (tails.empty()) Fail("list index out of range");
  return Reader(S(tails[0])).U32(4);
}

// Decl.build (line 245)
Bytes Decl::Build() const {
  Writer el;
  for (const DeclElem& e : elems) {
    el.U16(e.a);
    el.U16(e.b);
    el.U32(e.c);
    el.U8(e.d);
    el.U8(e.e);
    el.U8(e.f);
    el.U8(e.g);
  }
  Bytes body = el.bytes();
  body.resize(static_cast<size_t>(Al(0x30 + el.size(), 16) - 0x30), 0);
  auto h = head;
  h[0] = static_cast<uint32_t>(0x30 + body.size());
  h[1] = static_cast<uint32_t>(tails.size());
  h[4] = static_cast<uint32_t>(elems.size());
  Writer w;
  for (uint32_t v : h) w.U32(v);
  w.Bytes(S(body));
  for (const Bytes& t : tails) w.Bytes(S(t));
  return w.Take();
}

// parse_decl (line 255)
std::pair<Decl, uint64_t> Decl::Parse(std::span<const uint8_t> blob, uint64_t o) {
  const Reader r(blob);
  Decl d;
  const auto hv = r.U32s(static_cast<int64_t>(o), 12);
  std::copy(hv.begin(), hv.end(), d.head.begin());
  uint64_t p = o + 0x30;
  for (uint32_t k = 0; k < d.head[4]; ++k) {
    DeclElem e;
    e.a = r.U16(static_cast<int64_t>(p));
    e.b = r.U16(static_cast<int64_t>(p) + 2);
    e.c = r.U32(static_cast<int64_t>(p) + 4);
    e.d = r.U8(static_cast<int64_t>(p) + 8);
    e.e = r.U8(static_cast<int64_t>(p) + 9);
    e.f = r.U8(static_cast<int64_t>(p) + 10);
    e.g = r.U8(static_cast<int64_t>(p) + 11);
    d.elems.push_back(e);
    p += 12;
  }
  if (d.elems.empty()) Fail("list index out of range");
  Assert(d.elems.back().a == 0xFF, "elems[-1][0] == 0xFF");
  const uint64_t t = o + d.head[0];
  if (d.head[1] > (1u << 20)) Fail("too many vertex buffer tails");
  for (uint32_t k = 0; k < d.head[1]; ++k) {
    const auto s = r.Slice(t + 16ull * k, t + 16ull * k + 16);
    d.tails.emplace_back(s.begin(), s.end());
  }
  const uint64_t end = t + 16ull * d.head[1];
  return {std::move(d), end};
}

uint32_t ObjGeo::NodeIndex() const { return Reader(S(pre)).U32(4); }

// ObjGeo.build (line 283)
Bytes ObjGeo::Build() const {
  std::vector<Bytes> entries;
  for (const Decl& d : decls) entries.push_back(d.Build());
  const Bytes gd = BuildTableChunk("GeoDecl", entries);
  const uint64_t n = meshes.size();
  const uint64_t toff = 0x40;
  const uint64_t tsize = Al(4 * n, 16);
  const uint64_t gpos = toff + tsize;
  uint64_t pos = gpos + gd.size();
  std::vector<uint64_t> offs;
  Bytes body;
  for (const Bytes& m : meshes) {
    Assert(m.size() % 16 == 0, "len(m) % 16 == 0");
    offs.push_back(pos);
    PutBytes(body, S(m));
    pos += m.size();
  }
  Bytes table;
  if (n) {
    Writer w;
    for (uint64_t o : offs) w.U32(o);
    table = Pad16(S(w.bytes()));
  }
  const Bytes head = MakeHead("ObjGeo", pos, n, toff, gpos);
  return Cat({S(head), S(pre), S(table), S(gd), S(body)});
}

// parse_objgeo (line 304)
std::pair<ObjGeo, uint32_t> ObjGeo::Parse(std::span<const uint8_t> blob, uint64_t o) {
  const ChunkHead h = ParseChunkHead(blob, o);
  Assert(MagicIs(h.magic, "ObjGeo"), "h['magic'].rstrip(b'\\0') == b'ObjGeo'");
  Assert(h.toff == 0x40, "h['toff'] == 0x40");
  const Reader r(blob);
  const auto pre = r.Slice(o + 0x30, o + 0x40);
  const uint64_t g = o + h.xoff;
  const ChunkHead gh = ParseChunkHead(blob, g);
  Assert(MagicIs(gh.magic, "GeoDecl"), "gh['magic'].rstrip(b'\\0') == b'GeoDecl'");
  ObjGeo obj;
  obj.pre.assign(pre.begin(), pre.end());
  for (uint32_t e : gh.table) obj.decls.push_back(Decl::Parse(blob, g + e).first);
  std::vector<uint32_t> offs = h.table;
  offs.push_back(h.size);
  for (uint32_t i = 0; i < h.n; ++i) {
    const auto m = r.Slice(o + offs[i], o + offs[i + 1]);
    obj.meshes.emplace_back(m.begin(), m.end());
  }
  return {std::move(obj), h.size};
}

// parse_mdlgeo (line 322)
std::vector<ObjGeo> ParseMdlGeo(std::span<const uint8_t> blob) {
  const ChunkHead h = ParseChunkHead(blob, 0);
  Assert(MagicIs(h.magic, "MdlGeo"), "h['magic'].rstrip(b'\\0') == b'MdlGeo'");
  std::vector<ObjGeo> objs;
  for (uint32_t o : h.table) objs.push_back(ObjGeo::Parse(blob, o).first);
  return objs;
}

// build_mdlgeo (line 332)
Bytes BuildMdlGeo(const std::vector<ObjGeo>& objs) {
  std::vector<Bytes> parts;
  for (const ObjGeo& ob : objs) parts.push_back(ob.Build());
  return BuildTableChunk("MdlGeo", parts);
}

// mesh_words (line 338)
std::vector<uint32_t> MeshWords(std::span<const uint8_t> m) { return Reader(m).U32s(0, m.size() / 4); }

// mesh_stage_base (line 342)
std::pair<int64_t, uint32_t> MeshStageBase(std::span<const uint8_t> m) {
  const uint32_t nst = Reader(m).U32(0x18);
  return {static_cast<int64_t>(m.size()) - 0x40 * static_cast<int64_t>(nst), nst};
}

// mesh_textures (line 347)
std::vector<MeshTexture> MeshTextures(std::span<const uint8_t> m) {
  const auto [base, nst] = MeshStageBase(m);
  const Reader r(m);
  std::vector<MeshTexture> out;
  for (uint32_t k = 0; k < nst; ++k) {
    const int64_t at = base + 0x40 * static_cast<int64_t>(k);
    out.push_back({r.U32(at), r.U32(at + 4), r.U32(at + 8)});
  }
  return out;
}

// mesh_patch (line 353)
Bytes MeshPatch(std::span<const uint8_t> m, std::optional<uint64_t> index, std::optional<uint64_t> decl,
                std::optional<uint64_t> istart, std::optional<uint64_t> icount,
                const std::function<uint64_t(uint32_t)>& texmap) {
  Bytes b(m.begin(), m.end());
  if (index) be::PackIntoU32(b, 0, *index);
  if (decl) be::PackIntoU32(b, 8, *decl);
  if (istart) be::PackIntoU32(b, 0x10, *istart);
  if (icount) be::PackIntoU32(b, 0x14, *icount);
  if (texmap) {
    const auto [base, nst] = MeshStageBase(m);
    for (uint32_t k = 0; k < nst; ++k) {
      const int64_t at = base + 0x40 * static_cast<int64_t>(k) + 8;
      const uint32_t t = Reader(S(b)).U32(at);
      if (t != 0xFFFFFFFFu) be::PackIntoU32(b, at, texmap(t));
    }
  }
  return b;
}

// ---------------------------------------------------------------- MdlInfo
// ObjInfo.build (line 382)
Bytes ObjInfo::Build() const {
  auto w = words;
  w[3] = static_cast<uint32_t>(palette.size());
  Writer pre;
  for (uint32_t v : w) pre.U32(v);
  pre.Bytes(S(bounds));
  if (!palette.empty()) {
    Writer p;
    for (uint16_t v : palette) p.U16(v);
    pre.Bytes(S(Pad16(S(p.bytes()))));
  }
  return BuildTableChunk("ObjInfo", infos, S(pre.bytes()));
}

// parse_objinfo (line 391)
ObjInfo ObjInfo::Parse(std::span<const uint8_t> blob, uint64_t o) {
  const ChunkHead h = ParseChunkHead(blob, o);
  Assert(MagicIs(h.magic, "ObjInfo"), "h['magic'].rstrip(b'\\0') == b'ObjInfo'");
  const Reader r(blob);
  ObjInfo oi;
  const auto wv = r.U32s(static_cast<int64_t>(o) + 0x30, 12);
  std::copy(wv.begin(), wv.end(), oi.words.begin());
  const auto bnd = r.Slice(o + 0x60, o + 0x90);
  oi.bounds.assign(bnd.begin(), bnd.end());
  const uint32_t npal = oi.words[3];
  if (npal) oi.palette = r.U16s(static_cast<int64_t>(o) + 0x90, npal);
  const uint64_t expect_toff = 0x90 + (npal ? Al(2ull * npal, 16) : 0);
  Assert(h.toff == expect_toff, "h['toff'] == expect_toff");
  const auto padb = r.Slice(o + 0x90 + 2ull * npal, o + h.toff);
  Assert(AllZero(padb), "padb == bytes(len(padb))");
  std::vector<uint32_t> offs = h.table;
  offs.push_back(h.size);
  for (uint32_t i = 0; i < h.n; ++i) {
    const auto s = r.Slice(o + offs[i], o + offs[i + 1]);
    oi.infos.emplace_back(s.begin(), s.end());
  }
  return oi;
}

// parse_mdlinfo (line 407)
std::vector<ObjInfo> ParseMdlInfo(std::span<const uint8_t> blob) {
  const ChunkHead h = ParseChunkHead(blob, 0);
  Assert(MagicIs(h.magic, "MdlInfo"), "h['magic'].rstrip(b'\\0') == b'MdlInfo'");
  std::vector<ObjInfo> out;
  for (uint32_t o : h.table) out.push_back(ObjInfo::Parse(blob, o));
  return out;
}

// build_mdlinfo (line 413)
Bytes BuildMdlInfo(const std::vector<ObjInfo>& objs) {
  std::vector<Bytes> parts;
  for (const ObjInfo& ob : objs) parts.push_back(ob.Build());
  return BuildTableChunk("MdlInfo", parts);
}

// ---------------------------------------------------------------- vertex formats
namespace {
// DT (line 418), in the dict's order.
constexpr DtEntry kDt[] = {{0x2A23B9, "F3", 12},   {0x2C23A5, "F2", 8},    {0x1A23A6, "F4", 16},
                           {0x2C83A4, "F1", 4},    {0x2A2190, "HEND3N", 4}, {0x2C235F, "H2", 4},
                           {0x1A2360, "H4", 8},    {0x182886, "COLOR", 4},  {0x1A2086, "UB4N", 4},
                           {0x1A2286, "UB4", 4},   {0x2A2187, "DEC3N", 4}};
}  // namespace

std::span<const DtEntry> DT() { return kDt; }

const DtEntry* DtFind(uint32_t typ) {
  for (const DtEntry& e : kDt)
    if (e.typ == typ) return &e;
  return nullptr;
}

// DT_BY_NAME (line 421)
std::optional<uint32_t> DtByName(std::string_view name) {
  for (const DtEntry& e : kDt)
    if (e.name == name) return e.typ;
  return std::nullopt;
}

// USAGE (line 422)
std::string_view UsageName(uint32_t usage) {
  switch (usage) {
    case 0: return "POS";
    case 1: return "BW";
    case 2: return "BI";
    case 3: return "NRM";
    case 5: return "TEX";
    case 6: return "TAN";
    case 7: return "BIN";
    case 10: return "COL";
    default: return {};
  }
}

// half (line 425)
double Half(uint16_t h) { return num::HalfToDouble(h); }

// dec_hend3n (line 429): x / ((1 << (bits-1)) - 1), a true division of small ints (exact in double, correctly rounded).
std::array<double, 3> DecHend3n(uint32_t v) {
  const auto s = [](uint32_t x, int bits) {
    int64_t xi = x;
    if (x & (1u << (bits - 1))) xi -= int64_t(1) << bits;
    return static_cast<double>(xi) / static_cast<double>((int64_t(1) << (bits - 1)) - 1);
  };
  return {s(v & 0x7FF, 11), s((v >> 11) & 0x7FF, 11), s((v >> 22) & 0x3FF, 10)};
}

// decode_elem (line 437)
ElemValue DecodeElem(std::span<const uint8_t> d, int64_t o, uint32_t typ) {
  const DtEntry* e = DtFind(typ);
  if (!e) Fail("KeyError: unknown vertex element type");
  const Reader r(d);
  ElemValue out;
  const std::string_view name = e->name;
  if (name[0] == 'F') {
    const int k = name[1] - '0';
    o = static_cast<int64_t>(be::ResolveOffset(o, d.size(), 4 * k));  // all k floats, or none
    for (int i = 0; i < k; ++i) out.v[i] = r.F32(o + 4 * i);
    out.n = k;
    return out;
  }
  if (name == "HEND3N") {
    const auto t = DecHend3n(r.U32(o));
    out.n = 3;
    for (int i = 0; i < 3; ++i) out.v[i] = t[i];
    return out;
  }
  if (name == "H2" || name == "H4") {
    const int k = name[1] - '0';
    o = static_cast<int64_t>(be::ResolveOffset(o, d.size(), 2 * k));
    for (int i = 0; i < k; ++i) out.v[i] = Half(r.U16(o + 2 * i));
    out.n = k;
    return out;
  }
  if (name == "UB4N" || name == "COLOR" || name == "UB4") {
    if (o < 0) Fail("negative slice offsets are not supported");
    const auto s = r.Slice(static_cast<uint64_t>(o), static_cast<uint64_t>(o) + 4);
    out.n = static_cast<int>(s.size());
    for (size_t i = 0; i < s.size(); ++i) out.v[i] = s[i];
    return out;
  }
  Fail("ValueError: unhandled vertex element type");
}

// ---------------------------------------------------------------- .cat directory (convert.py)
// cat_dir (convert.py line 125)
std::vector<std::pair<uint8_t, uint32_t>> CatDir(std::span<const uint8_t> c) {
  const Reader r(c);
  std::vector<std::pair<uint8_t, uint32_t>> out;
  uint64_t q = r.U32(4);
  while (q + 8 <= c.size()) {
    const uint8_t a = r.U8(static_cast<int64_t>(q)), t = r.U8(static_cast<int64_t>(q) + 2);
    const uint32_t v = r.U32(static_cast<int64_t>(q) + 4);
    if (a == 0) break;
    out.emplace_back(t, v);
    q += 8;
  }
  return out;
}

// parse_t2 (convert.py line 138)
std::pair<std::vector<T2Group>, uint64_t> ParseT2(std::span<const uint8_t> c, uint64_t off) {
  const Reader r(c);
  std::vector<T2Group> groups;
  T2Group cur;
  uint64_t p = off;
  for (;;) {
    const uint8_t b = r.U8(static_cast<int64_t>(p));
    if (b == 0x90 || b == 0x91) {
      cur.items.emplace_back(r.U16(static_cast<int64_t>(p)), r.U8(static_cast<int64_t>(p) + 2));
      p += 3;
    } else {
      groups.push_back(std::move(cur));
      cur = T2Group{};
      if (b == 0) return {std::move(groups), p + 1};
      cur.is_main = false;
      cur.kind = b;
      p += 1;
    }
  }
}

// t4_records (convert.py line 154)
std::vector<Bytes> T4Records(std::span<const uint8_t> c, uint64_t off) {
  const Reader r(c);
  std::vector<Bytes> recs;
  uint64_t p = off;
  while (r.U16(static_cast<int64_t>(p)) != 0xFFFF) {
    const auto s = r.Slice(p, p + 0x40);
    recs.emplace_back(s.begin(), s.end());
    p += 0x40;
  }
  return recs;
}

}  // namespace doa4::suitconv::tpr
