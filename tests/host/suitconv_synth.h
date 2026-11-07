// Synthetic inputs for the suit converter's input-layer tests: seeded AFS archives, Xbox disc images (XDVDFS, through
// xdvdfs_fixtures.h) written as sparse files, folders, and an in-memory archive source whose bytes can change. No byte
// of any game file is used; every payload is made here from a seed. Header-only; Windows (sparse files).

#pragma once

#include <windows.h>
#include <winioctl.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "../../src/doa4_afs_overlay.h"
#include "../../src/doa4_gamefiles.h"
#include "../../src/doa4_suitconv_recipe.h"
#include "../../src/doa4_suitconv_tpr.h"
#include "xdvdfs_fixtures.h"

namespace synth {

namespace fs = std::filesystem;
using doa4::suitconv::ArcId;
using doa4::suitconv::EntryRef;

// Deterministic payload: a 32-bit LCG's high byte, so neighbouring bytes differ and a flipped byte is never a repeat.
inline std::vector<uint8_t> Seeded(uint32_t seed, size_t n) {
  std::vector<uint8_t> v(n);
  uint32_t x = seed * 2654435761u + 12345u;
  for (size_t i = 0; i < n; ++i) {
    x = x * 1664525u + 1013904223u;
    v[i] = uint8_t(x >> 24);
  }
  return v;
}

inline void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }

// An AFS archive as the game's are laid out: "AFS\0", the count, (offset, size) pairs, the name table's offset and
// size, entry 0 at 0x800 (for up to 255 entries), every entry on a 2048-byte boundary, then the 48-byte-per-entry
// name table, the file padded to a sector.
inline std::vector<uint8_t> BuildAfs(const std::vector<std::vector<uint8_t>>& entries) {
  const size_t n = entries.size();
  std::vector<uint32_t> off(n);
  uint32_t at = 0x800;
  for (size_t i = 0; i < n; ++i) {
    off[i] = at;
    at += uint32_t((entries[i].size() + 2047) / 2048 * 2048);
  }
  const uint32_t names = at;
  const uint32_t total = (names + 48 * uint32_t(n) + 2047) / 2048 * 2048;
  std::vector<uint8_t> a(total, 0);
  std::memcpy(a.data(), "AFS\0", 4);
  Put32(a, 4, uint32_t(n));
  for (size_t i = 0; i < n; ++i) {
    Put32(a, 8 + 8 * i, off[i]);
    Put32(a, 12 + 8 * i, uint32_t(entries[i].size()));
    if (!entries[i].empty()) std::memcpy(&a[off[i]], entries[i].data(), entries[i].size());
  }
  Put32(a, 8 + 8 * n, names);
  Put32(a, 12 + 8 * n, uint32_t(48 * n));
  for (size_t i = 0; i < n; ++i) {
    const std::string name = "e" + std::to_string(i) + ".bin";
    std::memcpy(&a[names + 48 * i], name.data(), name.size());
  }
  return a;
}

// An archive and the entries it holds (kept so a test can build the table and flip a byte).
struct Archive {
  ArcId arc;
  std::vector<std::vector<uint8_t>> entries;
  std::vector<uint8_t> file;
};
inline Archive MakeArchive(ArcId arc, const std::vector<size_t>& sizes, uint32_t seed) {
  Archive a{arc, {}, {}};
  for (size_t i = 0; i < sizes.size(); ++i) a.entries.push_back(Seeded(seed * 100 + uint32_t(i), sizes[i]));
  a.file = BuildAfs(a.entries);
  return a;
}

// A table of EntryRefs whose sha1 strings it owns (EntryRef holds a const char*).
struct Table {
  std::deque<std::string> sha;
  std::vector<EntryRef> refs;
  void Add(const Archive& a, uint32_t index) {
    sha.push_back(doa4::gamefiles::Sha1Hex(a.entries[index].data(), a.entries[index].size()));
    refs.push_back(EntryRef{a.arc, index, a.entries[index].size(), sha.back().c_str()});
  }
  // An entry whose recorded size or sha1 is chosen by the test.
  void AddRaw(ArcId arc, uint32_t index, uint64_t size, const std::string& sha1) {
    sha.push_back(sha1);
    refs.push_back(EntryRef{arc, index, size, sha.back().c_str()});
  }
  uint64_t total() const {
    uint64_t t = 0;
    for (const EntryRef& r : refs) t += r.size;
    return t;
  }
};

inline void WriteFileBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create " + path.string());
  DWORD put = 0;
  const bool ok = bytes.empty() || WriteFile(h, bytes.data(), DWORD(bytes.size()), &put, nullptr);
  CloseHandle(h);
  if (!ok || put != bytes.size()) throw std::runtime_error("cannot write " + path.string());
}

// An image file whose volume starts at `image.base`: sparse, so a volume at 0xFD90000 does not write 265 MB of
// zeros. Returns the file's size on disk (the allocated bytes) so the report can state it.
inline uint64_t WriteSparseImage(const fs::path& path, const doa4::xdvdfs::fixtures::BuiltImage& image) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create " + path.string());
  DWORD ret = 0;
  DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ret, nullptr);
  LARGE_INTEGER at;
  at.QuadPart = LONGLONG(image.base);
  DWORD put = 0;
  const bool ok = SetFilePointerEx(h, at, nullptr, FILE_BEGIN) &&
                  WriteFile(h, image.bytes.data(), DWORD(image.bytes.size()), &put, nullptr);
  CloseHandle(h);
  if (!ok || put != image.bytes.size()) throw std::runtime_error("cannot write " + path.string());
  DWORD high = 0;
  const DWORD low = GetCompressedFileSizeW(path.c_str(), &high);
  return (uint64_t(high) << 32) | low;
}

// A volume holding the archives at its root under the given names.
inline doa4::xdvdfs::fixtures::BuiltImage BuildVolume(const std::vector<std::pair<std::string, const Archive*>>& files,
                                                      uint64_t base) {
  std::vector<doa4::xdvdfs::fixtures::FakeFile> f;
  for (const auto& [name, a] : files) f.push_back({name, std::string(a->file.begin(), a->file.end())});
  return doa4::xdvdfs::fixtures::BuildImage(f, base);
}

// An archive held in memory whose bytes a test can change after the fact; reads see the current bytes.
class MemArchive final : public doa4::afs::Source {
 public:
  explicit MemArchive(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  uint64_t size() const override { return bytes_.size(); }
  bool Read(uint64_t offset, void* dst, size_t n) const override {
    if (offset > bytes_.size() || n > bytes_.size() - offset) return false;
    std::memcpy(dst, bytes_.data() + offset, n);
    return true;
  }
  std::vector<uint8_t>& bytes() { return bytes_; }

 private:
  std::vector<uint8_t> bytes_;
};

// ---------------------------------------------------------------------------------------------------------------------
// Synthetic models for the converter's geometry core (Task 5): a DOAX2-style body, a suit with an accessory bone, and a
// DOA4-style reference, as .tpr / .cat byte strings built through the tpr writers. Every position and weight is typed
// here; no byte comes from a game file. The body has the 41 DOAX2 ids (a flat tree under the root id 2) with geometry
// on five of them: 0 spine (rigid), 5 shoulder skin (palette [18, 0], HEND3N normal and FLOAT16_2 uv, so the expanded
// formats run), 14 hip (rigid), 21 neck (palette [21, 0], rule N2) and 40 pelvis skin (palette [40, 0]), plus a
// 1-vertex locator on the root (the dummy mesh's template). The suit has the root, 0, 40 (a pelvis piece whose weights
// differ from the body's at the same points: rule S1) and an accessory bone 50 with geometry 20 cm behind the body
// (rules A1 and A1b). The reference has one node per channel the ROLE table needs (plus 0x9100), in the tree RefParent
// gives.
namespace conv {

namespace tp = doa4::suitconv::tpr;

inline void PutBe32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 3; i >= 0; --i) b.push_back(uint8_t(v >> (8 * i)));
}
inline void PutBe16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(uint8_t(v >> 8));
  b.push_back(uint8_t(v));
}
inline void PutBeF(std::vector<uint8_t>& b, float f) { PutBe32(b, std::bit_cast<uint32_t>(f)); }

// A node at a model-space position; parent is the index of its parent node in the list (the first node is the root, at
// the origin). Its bind is a translation by its position minus its parent's.
struct SNode {
  int id;
  float x, y, z;
  int parent = 0;
};
inline std::vector<uint8_t> TransMat(float x, float y, float z) {
  std::vector<uint8_t> m;
  const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
  for (float f : v) PutBeF(m, f);
  return m;
}

struct SVert {
  float x, y, z;  // local to the owner's joint
  float w0, w1;   // blend weights (F2), when the decl has them
};
struct SDecl {
  std::vector<SVert> verts;
  bool weights = false;
  bool extras = false;  // HEND3N normal + FLOAT16_2 uv
  bool locator = false;  // its one mesh is the 1-vertex locator kind (word 6 = 1, word 12 = 7)
};
struct SObj {
  int id;
  std::vector<uint16_t> palette;
  std::vector<SDecl> decls;
};

// A DOAX2-style .tpr: USER (u0, MdlGeo, MdlInfo, Hie), then one VBUF and one IBUF per decl. Objects are indexed by id;
// ids without an entry get an empty object.
inline std::vector<uint8_t> BuildModel(const std::vector<SNode>& nodes, const std::vector<SObj>& objs, int nobj) {
  const uint32_t kF3 = 0x2A23B9, kF2 = 0x2C23A5, kHend3n = 0x2A2190, kH2 = 0x2C235F;
  std::vector<tp::ObjGeo> geos(nobj);
  std::vector<tp::ObjInfo> infos(nobj);
  std::vector<std::vector<uint8_t>> vbs, ibs;
  for (int i = 0; i < nobj; ++i) {
    geos[i].pre.clear();
    PutBe32(geos[i].pre, 0x00020000);
    PutBe32(geos[i].pre, uint32_t(i));
    PutBe32(geos[i].pre, 0);
    PutBe32(geos[i].pre, 0);
    infos[i].words = {0x00010001, uint32_t(i), 0, 0, 1, 0, 0, 2, 0, 0, 0, 0};
    infos[i].bounds.assign(48, 0);
  }
  for (const SObj& ob : objs) {
    tp::ObjGeo& g = geos[ob.id];
    tp::ObjInfo& inf = infos[ob.id];
    inf.palette = ob.palette;
    for (size_t k = 0; k < ob.decls.size(); ++k) {
      const SDecl& d = ob.decls[k];
      std::vector<tp::DeclElem> el{{0, 0, kF3, 0, 0, 0, 0}};
      uint16_t off = 12;
      if (d.extras) {
        el.push_back({0, off, kHend3n, 0, 3, 0, 0});
        off += 4;
        el.push_back({0, off, kH2, 0, 5, 0, 0});
        off += 4;
      }
      if (d.weights) {
        el.push_back({0, off, kF2, 0, 1, 0, 0});
        off += 8;
      }
      el.push_back({0xFF, 0, 0xFFFFFFFFu, 0, 0, 0, 0});
      std::vector<uint8_t> vb;
      for (size_t j = 0; j < d.verts.size(); ++j) {
        const SVert& v = d.verts[j];
        PutBeF(vb, v.x);
        PutBeF(vb, v.y);
        PutBeF(vb, v.z);
        if (d.extras) {
          PutBe32(vb, uint32_t(0x1FF | (0x2A0u << 11) | (0x100u << 22)) + uint32_t(j));  // a normal
          PutBe16(vb, uint16_t(0x3800 + j));                                           // u (half)
          PutBe16(vb, uint16_t(0x3400 + 3 * j));                                       // v (half)
        }
        if (d.weights) {
          PutBeF(vb, v.w0);
          PutBeF(vb, v.w1);
        }
      }
      std::vector<uint8_t> ib;
      for (size_t j = 0; j < d.verts.size(); ++j) PutBe16(ib, uint16_t(j));
      if (d.locator) ib = {0, 0, 0, 0, 0, 0};
      const uint32_t icount = uint32_t(ib.size() / 2);
      tp::Decl dc;
      dc.head = {0, 1, uint32_t(ibs.size()), icount, 0, uint32_t(d.verts.size()), 1, 0, 0, 0, 0, 0};
      dc.elems = el;
      std::vector<uint8_t> tail;
      PutBe32(tail, uint32_t(vbs.size()));
      PutBe32(tail, off);
      PutBe32(tail, 0);
      PutBe32(tail, 0);
      dc.tails = {tail};
      g.decls.push_back(dc);
      vbs.push_back(vb);
      ibs.push_back(ib);
      // mesh record: 0x40 bytes of words, then one 0x40-byte texture stage {stage 0, kind 0, no texture}
      std::vector<uint8_t> m;
      const uint32_t words[16] = {uint32_t(g.meshes.size()), 0, uint32_t(k), 0, 0, icount, 1, 0,
                                  0, 0, 0, 0, d.locator ? 7u : 0u, 0, 0, 0};
      for (uint32_t w : words) PutBe32(m, w);
      PutBe32(m, 0);
      PutBe32(m, 0);
      PutBe32(m, 0xFFFFFFFFu);
      m.resize(0x80, 0);
      std::vector<uint8_t> rec;
      const uint32_t r[8] = {uint32_t(g.meshes.size()), 1, 2, 0, 0, 0, 0, 0};
      for (uint32_t w : r) PutBe32(rec, w);
      rec.resize(0x50, 0);
      inf.infos.push_back(rec);
      g.meshes.push_back(m);
    }
  }
  std::vector<tp::Node> hie;
  for (size_t i = 0; i < nodes.size(); ++i) {
    std::vector<int64_t> ch;
    for (size_t k = 1; k < nodes.size(); ++k)
      if (nodes[k].parent == int(i)) ch.push_back(int64_t(k));
    const SNode& p = nodes[i == 0 ? 0 : size_t(nodes[i].parent)];
    const float px = i == 0 ? 0.0f : p.x, py = i == 0 ? 0.0f : p.y, pz = i == 0 ? 0.0f : p.z;
    hie.push_back(tp::Node::FromBytes(TransMat(nodes[i].x - px, nodes[i].y - py, nodes[i].z - pz), nodes[i].id,
                                      i == 0 ? -1 : nodes[i].parent, ch));
  }
  tp::Xpr x;
  std::vector<uint8_t> u0;
  for (int i = 0; i < 12; ++i) PutBe32(u0, uint32_t(i));
  const tp::Fourcc kUser = tp::MakeFourcc("USER"), kVb = tp::MakeFourcc("VBUF"), kIb = tp::MakeFourcc("IBUF");
  x.res.push_back({kUser, u0});
  x.res.push_back({kUser, tp::BuildMdlGeo(geos)});
  x.res.push_back({kUser, tp::BuildMdlInfo(infos)});
  x.res.push_back({kUser, tp::BuildHie(hie)});
  std::vector<uint8_t> data;
  for (int kind = 0; kind < 2; ++kind) {
    for (const auto& b : kind == 0 ? vbs : ibs) {
      data.resize((data.size() + 0xFF) & ~size_t(0xFF), 0);
      std::vector<uint8_t> d = {0, uint8_t(kind + 1), 0, 1, 0, 0, 0, 0, 0xff, 0xff, 0, 0};
      PutBe32(d, uint32_t(data.size()) | (kind == 0 ? 3u : 0u));
      PutBe32(d, uint32_t((b.size() + 15) & ~size_t(15)) | (kind == 0 ? 2u : 0u));
      x.res.push_back({kind == 0 ? kVb : kIb, d});
      data.insert(data.end(), b.begin(), b.end());
      data.resize((data.size() + 15) & ~size_t(15), 0);
    }
  }
  x.data = data;
  return x.Build(4);
}

// A .cat with the given sections, laid out as the converter's build_cat lays them out: "CAT\0", 20 zero bytes, each
// section at an 8-byte boundary, the directory of (1, 0, type, 0, offset) records, 8 zero bytes, padded to 16.
inline std::vector<uint8_t> BuildCat(const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& sections) {
  std::vector<uint8_t> out = {'C', 'A', 'T', 0};
  out.resize(24, 0);
  std::vector<std::pair<uint8_t, uint32_t>> dir;
  for (const auto& [t, blob] : sections) {
    out.resize((out.size() + 7) & ~size_t(7), 0);
    dir.emplace_back(t, uint32_t(out.size()));
    out.insert(out.end(), blob.begin(), blob.end());
  }
  out.resize((out.size() + 7) & ~size_t(7), 0);
  const uint32_t dir_off = uint32_t(out.size());
  for (const auto& [t, v] : dir) {
    out.push_back(1);
    out.push_back(0);
    out.push_back(t);
    out.push_back(0);
    PutBe32(out, v);
  }
  out.resize(out.size() + 8, 0);
  out.resize((out.size() + 15) & ~size_t(15), 0);
  out[4] = uint8_t(dir_off >> 24);
  out[5] = uint8_t(dir_off >> 16);
  out[6] = uint8_t(dir_off >> 8);
  out[7] = uint8_t(dir_off);
  return out;
}

// Type-4 material records: 0x40 bytes each, key at +4, then 0xFFFF 0000.
inline std::vector<uint8_t> Type4(const std::vector<uint32_t>& keys, uint8_t fill) {
  std::vector<uint8_t> b;
  for (uint32_t k : keys) {
    std::vector<uint8_t> r(0x40, fill);
    r[0] = 0;
    r[1] = 1;
    r[4] = uint8_t(k >> 24);
    r[5] = uint8_t(k >> 16);
    r[6] = uint8_t(k >> 8);
    r[7] = uint8_t(k);
    b.insert(b.end(), r.begin(), r.end());
  }
  for (uint8_t v : {0xff, 0xff, 0, 0}) b.push_back(v);
  return b;
}

// The DOAX2 body's joints (model space): the 41 ids, root 2 at the origin first.
inline std::vector<SNode> BodyNodes() {
  return {{2, 0, 0, 0},           {0, 0, 1.2f, 0},          {1, 0, 1.6f, 0},          {3, -0.11f, 0.09f, 0.01f},
          {4, -0.12f, 0.08f, 0},  {5, -0.2f, 1.38f, 0.01f}, {6, -0.05f, 1.3f, 0.05f}, {7, -0.47f, 1.39f, 0.01f},
          {8, 0.03f, 1.65f, 0.08f}, {9, -0.1f, 0.1f, 0},    {10, -0.13f, 0.07f, 0},   {11, -0.45f, 1.4f, 0},
          {12, -0.7f, 1.4f, 0},   {13, -0.1f, 0.03f, 0.05f}, {14, -0.1f, 0.9f, 0},    {15, -0.11f, 0.48f, 0.03f},
          {16, -0.11f, 0.88f, 0.01f}, {17, -0.1f, 0.5f, 0.02f}, {18, -0.18f, 1.4f, 0}, {19, -0.1f, 0.02f, 0.1f},
          {20, -0.72f, 1.39f, 0.01f}, {21, 0, 1.5f, 0},     {22, 0.11f, 0.09f, 0.01f}, {23, 0.12f, 0.08f, 0},
          {24, 0.2f, 1.38f, 0.01f}, {25, 0.05f, 1.3f, 0.05f}, {26, 0.47f, 1.39f, 0.01f}, {27, -0.03f, 1.65f, 0.08f},
          {28, 0.1f, 0.1f, 0},    {29, 0.13f, 0.07f, 0},    {30, 0.45f, 1.4f, 0},     {31, 0.7f, 1.4f, 0},
          {32, 0.1f, 0.03f, 0.05f}, {33, 0.1f, 0.9f, 0},    {34, 0.11f, 0.48f, 0.03f}, {35, 0.11f, 0.88f, 0.01f},
          {36, 0.1f, 0.5f, 0.02f}, {37, 0.18f, 1.4f, 0},    {38, 0.1f, 0.02f, 0.1f},  {39, 0.72f, 1.39f, 0.01f},
          {40, 0, 0.95f, 0}};
}

inline std::vector<SObj> BodyObjects() {
  std::vector<SObj> o;
  o.push_back({0, {0}, {{{{-0.05f, 0, 0.1f, 0, 0}, {0.05f, 0, 0.1f, 0, 0}, {-0.05f, 0.1f, 0.1f, 0, 0},
                          {0.05f, 0.1f, 0.1f, 0, 0}}}}});
  o.push_back({5, {18, 0}, {{{{0, 0, 0.05f, 0.6f, 0.4f}, {0.03f, 0, 0.05f, 1.0f, 0}, {0, 0.04f, 0.05f, 0.5f, 0.5f}},
                             true, true}}});
  o.push_back({14, {14}, {{{{0, 0, 0.06f, 0, 0}, {0.02f, -0.1f, 0.06f, 0, 0}, {-0.02f, -0.1f, 0.06f, 0, 0}}}}});
  // neck: vertex 0 sits on spine vertex 2 ((-0.05, 1.3, 0.1)), with other weights: the N2 ring snaps it
  o.push_back({21, {21, 0}, {{{{-0.05f, -0.2f, 0.1f, 0.3f, 0.7f}, {0.05f, -0.2f, 0.1f, 0.2f, 0.8f},
                              {-0.05f, 0, 0.1f, 0.9f, 0.1f}, {0.05f, 0, 0.1f, 1.0f, 0}},
                             true}}});
  o.push_back({40, {40, 0}, {{{{-0.06f, 0, 0.12f, 1.0f, 0}, {0.06f, 0, 0.12f, 0.5f, 0.5f},
                              {-0.06f, 0.08f, 0.12f, 0.25f, 0.75f}, {0.06f, 0.08f, 0.12f, 0, 1.0f}},
                             true}}});
  SDecl loc{{{0, 0, 0, 0, 0}}, false, false, true};
  o.push_back({2, {2}, {loc}});
  return o;
}

inline std::vector<SNode> SuitNodes() { return {{2, 0, 0, 0}, {0, 0, 1.2f, 0}, {40, 0, 0.95f, 0}, {50, 0, 1.0f, -0.3f}}; }

// nan_accessory: the accessory's vertices have NaN x, so every contact distance is NaN (attach_accessories' cand[-1] path).
inline std::vector<SObj> SuitObjects(bool nan_accessory = false) {
  std::vector<SObj> o;
  // two vertices on the body's pelvis vertices 0 and 1 with other weights (rule S1 gives them the body's)
  o.push_back({40, {40, 0}, {{{{-0.06f, 0, 0.12f, 0.8f, 0.2f}, {0.06f, 0, 0.12f, 0.5f, 0.5f},
                              {0, 0.04f, 0.13f, 0.6f, 0.4f}},
                             true}}});
  const float nx = nan_accessory ? std::numeric_limits<float>::quiet_NaN() : 0.0f;
  o.push_back({50, {50}, {{{{nx, 0, 0, 0, 0}, {nan_accessory ? nx : 0.02f, 0, 0, 0, 0}, {nx, 0.02f, 0, 0, 0}}}}});
  return o;
}

// The DOA4 reference's channels: the ROLE table's codes and 0x9100, node ids in this order.
inline std::vector<uint16_t> RefCodes() {
  return {0x9002, 0x9000, 0x9003, 0x9004, 0x9005, 0x9006, 0x9007, 0x9008, 0x9009, 0x900A,
          0x900B, 0x900C, 0x900D, 0x900E, 0x9100, 0x9101, 0x9103, 0x9104, 0x9105, 0x9106,
          0x9107, 0x9108, 0x9109, 0x910A, 0x910B, 0x910C, 0x910D, 0x910E};
}

// The body id each reference channel stands for (its joint is that body joint, scaled), 0x9100 at the pelvis.
inline int RefBodyId(uint16_t code) {
  switch (code) {
    case 0x9002: return 2;   case 0x9000: return 0;   case 0x9003: return 9;   case 0x9004: return 11;
    case 0x9005: return 12;  case 0x9006: return 14;  case 0x9007: return 17;  case 0x9008: return 18;
    case 0x9009: return 28;  case 0x900A: return 30;  case 0x900B: return 31;  case 0x900C: return 33;
    case 0x900D: return 36;  case 0x900E: return 37;  case 0x9100: return 40;  case 0x9101: return 21;
    case 0x9103: return 3;   case 0x9104: return 7;   case 0x9105: return 20;  case 0x9106: return 16;
    case 0x9107: return 15;  case 0x9108: return 5;   case 0x9109: return 22;  case 0x910A: return 26;
    case 0x910B: return 39;  case 0x910C: return 35;  case 0x910D: return 34;  case 0x910E: return 24;
  }
  return 2;
}

// The reference's tree, as indices into RefCodes(): the upper root 0x9000 under the lower root; the neck 0x9101, the
// pelvis skin holder 0x9100 and both shoulders under 0x9000; the shoulder skin holder 0x9108 under the LEFT shoulder
// 0x900E (not a real layout: it puts a node that no weight names at palette[0], so "palette[0] = the owner's parent"
// cannot be met by "the root", "the first weight" or "the upper root"); every other channel under the lower root.
inline int RefParent(uint16_t code) {
  switch (code) {
    case 0x9101: case 0x9100: case 0x9008: case 0x900E: return 1;
    case 0x9108: return 13;
  }
  return 0;
}

struct Inputs {
  std::vector<uint8_t> body_tpr, body_cat, suit_tpr, suit_cat, ref_tpr, ref_cat;
};

inline Inputs MakeInputs(bool nan_accessory = false) {
  Inputs in;
  in.body_tpr = BuildModel(BodyNodes(), BodyObjects(), 41);
  in.body_cat = BuildCat({{4, Type4({0x101010, 0x202020, 0x303030}, 0x11)}});
  in.suit_tpr = BuildModel(SuitNodes(), SuitObjects(nan_accessory), 51);
  in.suit_cat = BuildCat({{4, Type4({0x404040}, 0x22)}});
  std::vector<SNode> rn;
  const auto body = BodyNodes();
  std::vector<uint8_t> t2;
  const auto codes = RefCodes();
  for (size_t i = 0; i < codes.size(); ++i) {
    const int bid = RefBodyId(codes[i]);
    SNode b{0, 0, 0, 0};
    for (const SNode& n : body)
      if (n.id == bid) b = n;
    rn.push_back({int(i), b.x * 1.05f, b.y * 1.08f, b.z, RefParent(codes[i])});
    PutBe16(t2, codes[i]);
    t2.push_back(uint8_t(i));
  }
  t2.push_back(0);
  std::vector<uint8_t> t6;
  for (uint32_t w : {0x01020304u, 0x05060708u, 0x090A0B0Cu, 0x0D0E0F10u, 0x11121314u, 0x3F800000u}) PutBe32(t6, w);
  in.ref_tpr = BuildModel(rn, {}, 0);
  in.ref_cat = BuildCat({{2, t2}, {4, Type4({0x202020, 0x505050}, 0x33)}, {6, t6}});
  return in;
}

}  // namespace conv

}  // namespace synth
