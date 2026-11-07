// Suit converter (Plan 2): the geometry core, a port of the recipe's convert.py (physics_spike/recipe_m1/convert.py,
// SHA-1 05cb00536712a89d9cd4cb8f79cebe529e6db2fa). Python's name -> C++ name: snake_case -> CamelCase (fit_maps ->
// FitMaps, snap_suit_weights -> SnapSuitWeights ...). ConvertSuit (doa4_suitconv.h) calls these in convert.main's order.
//
// Ported here: Model, main_chain, build_skeleton (orphans included), channel_map and CHANNEL_ALIAS, _sig, select_copy,
// accessory_ids, suit_to_body_ids, fit_maps (torso, seg with the leg midline, move), Part, source_pass, carry_ref_decl,
// decode_part, collect_parts, snap_suit_weights (rule S1 and the N2 ring), attach_accessories (rules A1, A1b), rule A2
// (in ConvertSuit), build_objects (R1, N2, R13), bounds, encode_objects (dummy objects, mesh passes, the verbatim order,
// the FLOAT16_4 branch), tex_spans, doa4_tex_desc, build_tpr, build_cat (sections 2, 4 with ref4, 6; 5 and 9 too),
// strip_tris. cat_dir, parse_t2 and t4_records are Task 4's (doa4_suitconv_tpr.h).
// NOT ported (unreached by the 34 suits): debug_colours, find_neck_copy, multi_cloth_spare_only (build_cat's 'cloth'
// option throws), shadow_chain (build_cat's extra_groups), the report text, and the options --head --shadow --carry-ref
// --carry-hidden --dummy-objects --debug-colours --owner-role --no-rigid-ankle --waist-from-ref --keep-all-nodes (their
// globals keep the value main gives them without the option).
//
// Python semantics kept (plan rows 4-6, 8, 10, 11): builtin sum is num::PySum; float // is num::PyFloorDiv; round(v, n)
// is num::PyRoundDigits; x**2 is x*x and x**0.5 is std::sqrt; a float division by zero throws as Python's
// ZeroDivisionError does; dicts keep insertion order (PyDict, Weights); max / min / sorted are first-wins and stable;
// numpy's `.sum(-1)` is num::NpSumLastAxis and argmin is first-wins; positions stay double until written
// (num::PackF32). Every Python assert is a ConvertError carrying the assert's text; a KeyError / IndexError / ValueError
// is a ConvertError naming it. One Context per conversion holds what the Python kept in module globals.
// Pure C++ (no SDK).

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "doa4_suitconv_num.h"
#include "doa4_suitconv_tpr.h"

namespace doa4::suitconv::core {

using tpr::Bytes;
using Vec3 = std::array<double, 3>;

// A Python KeyError / IndexError / ValueError / AssertionError, as the ConvertError the port throws.
[[noreturn]] void Fail(const std::string& what);
// assert cond, text
void Assert(bool cond, const std::string& text);

// a / b for Python floats: ZeroDivisionError("float division by zero") when b == 0.
double PyDiv(double a, double b);

// A Python dict: insertion order, `d[k] = v` keeps the first position of k and takes the new value.
template <class K, class V>
class PyDict {
 public:
  void Set(const K& k, const V& v) {
    auto it = idx_.find(k);
    if (it != idx_.end()) {
      items_[it->second].second = v;
      return;
    }
    idx_.emplace(k, items_.size());
    items_.emplace_back(k, v);
  }
  const V* Find(const K& k) const {
    auto it = idx_.find(k);
    return it == idx_.end() ? nullptr : &items_[it->second].second;
  }
  V* Find(const K& k) {
    auto it = idx_.find(k);
    return it == idx_.end() ? nullptr : &items_[it->second].second;
  }
  bool Contains(const K& k) const { return idx_.count(k) != 0; }
  // d[k]: KeyError when absent.
  const V& At(const K& k) const {
    const V* v = Find(k);
    if (!v) Fail("KeyError: " + std::to_string(k));
    return *v;
  }
  size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  const std::vector<std::pair<K, V>>& items() const { return items_; }

 private:
  std::vector<std::pair<K, V>> items_;
  std::unordered_map<K, size_t> idx_;
};

// A vertex's weights: an insertion-ordered small vector (plan row 10). Add(id, w) is w4[id] = w4.get(id, 0.0) + w.
class Weights {
 public:
  void Add(int64_t id, double w);
  // w4[id] = w
  void Put(int64_t id, double w);
  const double* Find(int64_t id) const {
    for (const auto& [k, v] : items_)
      if (k == id) return &v;
    return nullptr;
  }
  double Get(int64_t id, double dflt) const {
    const double* v = Find(id);
    return v ? *v : dflt;
  }
  bool Contains(int64_t id) const { return Find(id) != nullptr; }
  size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  const std::vector<std::pair<int64_t, double>>& items() const { return items_; }
  // list(w4) == [id]
  bool OnlyKey(int64_t id) const { return items_.size() == 1 && items_[0].first == id; }
  // {b: w for b, w in w4.items() if w > threshold}
  Weights Above(double threshold) const;

 private:
  std::vector<std::pair<int64_t, double>> items_;
};

using Blend = std::vector<std::pair<int64_t, double>>;

// ---------------------------------------------------------------- Model (line 98)
struct Model {
  std::string name;  // os.path.basename of the Python path
  tpr::Xpr x;
  std::vector<tpr::ObjGeo> geo;
  std::vector<tpr::ObjInfo> info;
  std::vector<tpr::Node> hie;
  std::vector<Bytes> tex, vbs, ibs;  // descriptors
  PyDict<int64_t, int64_t> id2node;
  PyDict<int64_t, Vec3> joint;
  std::shared_ptr<const Bytes> cat;  // null when the Python passed no .cat

  // parent_id(nid)
  int64_t ParentId(int64_t nid) const;
  // self.hie[i] with Python's index rule (negative counts from the end); IndexError otherwise.
  const tpr::Node& Hie(int64_t i) const;
  const Bytes& Cat() const;
};

Model LoadModel(std::string name, std::span<const uint8_t> tpr_bytes, std::shared_ptr<const Bytes> cat);

// ---------------------------------------------------------------- the module globals (plan row 11)
// One per conversion. Each field is set where convert.main sets the global.
struct Context {
  std::set<int64_t> twist_rigid_body{11, 30};  // TWIST_RIGID_BODY (rule A2 adds 3 / 22)
  std::set<int64_t> owner_slot;                // OWNER_SLOT (rule N2 objects)
  std::optional<int64_t> neck_id;              // NECK_ID[0]
  bool allow_owner_slot = false;               // ALLOW_OWNER_SLOT[0] (--no-rigid-ankle, not ported: False)
  bool neck_mode = false;                      // NECK_MODE[0] (--neck-weights owner)
  bool expand = false;                         // EXPAND[0] (--expand-formats)
  std::optional<Bytes> dummy_mesh;             // DUMMY_MESH
  bool mesh_passes = false;                    // MESH_PASSES (--mesh-passes)
};

// ---------------------------------------------------------------- skeleton and bone map
struct MainChain {
  int64_t main_max = 0;
  std::vector<int64_t> orphans;
};
MainChain MainChainOf(const Model& ref);
std::vector<tpr::Node> BuildSkeleton(const Model& ref, int64_t main_max, const std::vector<int64_t>& orphans,
                                     int64_t lower_root);

struct ChannelMap {
  PyDict<int64_t, int64_t> out;                    // code -> node id
  std::vector<std::pair<uint16_t, uint8_t>> main;  // the reference's main-group records
};
ChannelMap ChannelMapOf(const Model& ref);

// select_copy: the kept suit ids, or nullopt for a single-copy file.
std::optional<std::set<int64_t>> SelectCopy(const Model& suit, const Model& body);
std::vector<int64_t> AccessoryIds(const Model& suit, const Model& body, const std::optional<std::set<int64_t>>& only,
                                  double tol = 0.05);
PyDict<int64_t, int64_t> SuitToBodyIds(const Model& suit, const Model& body,
                                       const std::optional<std::set<int64_t>>& only, const std::set<int64_t>& skip);

// ---------------------------------------------------------------- retarget maps (fit_maps)
struct TorsoFit {
  double yh2, yh4, ys2, ys4, zh2, zh4, zs2, zs4, xh, xs, a;
};
struct RetargetMap {
  enum Kind { kTorso, kSeg, kMove } kind = kTorso;
  const TorsoFit* torso = nullptr;
  Vec3 p0{}, q0{};
  int ax = 0;
  double s = 0.0;
  bool taper = false;
  Vec3 operator()(const Vec3& p) const;
};
struct FitInfo {
  double torso_y_scale = 0, torso_x_scale_hips = 0, torso_x_scale_shoulders = 0;
  std::pair<double, double> hips_y{}, shoulders_y{};
};
struct Maps {
  std::unique_ptr<TorsoFit> torso;
  PyDict<int64_t, RetargetMap> maps;  // body id -> map, in body.joint order
  FitInfo info{};
  const RetargetMap& At(int64_t bid) const { return maps.At(bid); }
};
Maps FitMaps(const Model& body, const Model& ref, const PyDict<int64_t, int64_t>& chan2id, bool leg_midline);

// ---------------------------------------------------------------- parts
struct Vert {
  Vec3 pos{};
  Weights w4;
};
struct SrcV {
  Vec3 pos{};
  Blend blend;
};
struct AttachInfo {  // p.attach = (blend, w4, owner, shift)
  Blend blend;
  Weights w4;
  int64_t owner = 0;
  Vec3 shift{};
};
struct NativeDecl {  // p.native = (elements incl END, stride, vertex bytes, source palette)
  std::vector<tpr::DeclElem> elems;
  uint32_t stride = 0;
  Bytes vb;
  std::vector<int64_t> src_pal;
};
struct PassRec {  // (source index, pass, +0x10)
  int64_t src = 0;
  uint32_t pass = 0, d10 = 0;
};
struct KeptElem {
  uint32_t typ;
  uint8_t usage, ui;
};

struct Part {
  int index = 0;              // creation order (the trace's part index)
  bool has_model = false;     // p.model is not None
  std::string model_name;     // basename(p.model.path)
  int64_t obj_id = 0;         // p.obj_id, or the carried reference object
  int64_t decl_index = 0;     // p.decl_index, or the carried reference decl
  std::vector<Vert> verts;
  std::vector<int> raw_lens;  // the byte length of each raw element of a vertex (the same for every vertex)
  int raw_stride = 0;         // their sum
  Bytes raws;                 // verts.size() * raw_stride bytes: vertex k's raws at k * raw_stride
  std::vector<KeptElem> kept;
  Bytes ib;
  uint32_t icount = 0;
  std::vector<Bytes> meshes;
  std::vector<PassRec> passes;
  int64_t owner = 0;
  std::optional<AttachInfo> attach;
  bool writable = false;
  std::optional<std::vector<std::array<double, 4>>> h4;
  std::optional<NativeDecl> native;
  const Model* model = nullptr;  // p.model (null for a carried reference decl); the Model outlives every Part
  // physics spike attributes (prep_bust and later): p.bust = (down, up) offsets per vertex, p.border and p.band sets of
  // vertex indices, p.welded True or None.
  std::optional<std::pair<std::vector<Vec3>, std::vector<Vec3>>> bust;
  std::optional<std::set<int64_t>> border, band;
  bool welded = false;
  bool has_srcv = false;      // decode_part sets p.srcv; carry_ref_decl does not
  std::vector<SrcV> srcv;

  std::span<const uint8_t> Raw(size_t k) const {
    return std::span<const uint8_t>(raws.data() + k * raw_stride, raw_stride);
  }
};

// Owns every part made in one conversion; Part::index is its position here.
class PartArena {
 public:
  Part* New() {
    parts_.push_back(std::make_unique<Part>());
    parts_.back()->index = static_cast<int>(parts_.size() - 1);
    return parts_.back().get();
  }
  size_t size() const { return parts_.size(); }

 private:
  std::vector<std::unique_ptr<Part>> parts_;
};

using IdMap = PyDict<int64_t, int64_t>;

std::vector<Part*> CollectParts(const Context& ctx, PartArena& arena, const Model& model, const IdMap& id_map,
                                const Maps& maps, int64_t tex_offset, const PyDict<int64_t, int64_t>& chan2id,
                                const std::set<int64_t>& drop_objects, const std::set<int64_t>& skip = {});

// carry_ref_decl: the part and the reference textures it uses.
std::pair<Part*, std::vector<uint32_t>> CarryRefDecl(PartArena& arena, const Model& ref, int64_t oid, int64_t k,
                                                     int64_t tex_offset);

// snap_suit_weights: returns, in suit_parts order and vertex order, the (part, vertex) entries it replaced.
std::vector<std::pair<Part*, size_t>> SnapSuitWeights(const std::vector<Part*>& body_parts,
                                                      const std::vector<Part*>& suit_parts, double tol = 0.0005,
                                                      const std::set<int64_t>& rigid_owners = {});

std::vector<Part*> AttachAccessories(const Context& ctx, PartArena& arena, const Model& suit,
                                     const std::vector<int64_t>& acc, const std::vector<Part*>& parts,
                                     const Maps& maps, int64_t tex_offset, const PyDict<int64_t, int64_t>& chan2id,
                                     double far = 0.02);

// ---------------------------------------------------------------- objects
struct Object {
  std::vector<Part*> ps;
  std::vector<int64_t> pal;
};
struct BuiltObjects {
  std::vector<std::optional<Object>> objects;
  PyDict<int64_t, Vec3> bind4;
};
BuiltObjects BuildObjects(Context& ctx, const std::vector<Part*>& parts, const std::vector<tpr::Node>& nodes);

// bounds(pts): the 48 bytes of a bounds record.
Bytes Bounds(const std::vector<Vec3>& pts);

struct Encoded {
  std::vector<tpr::ObjGeo> geos;
  std::vector<tpr::ObjInfo> infos;
  std::vector<Bytes> vbs, ibs;
};
Encoded EncodeObjects(const Context& ctx, const std::vector<std::optional<Object>>& objects,
                      const PyDict<int64_t, Vec3>& bind4);

// ---------------------------------------------------------------- container and cat
struct TexSpan {
  Bytes desc;
  Bytes span;
  uint32_t base = 0, mip = 0;
};
std::vector<TexSpan> TexSpans(const Model& model);
Bytes Doa4TexDesc(std::span<const uint8_t> desc);
Bytes BuildTpr(const std::vector<tpr::Node>& nodes, const std::vector<tpr::ObjGeo>& geos,
               const std::vector<tpr::ObjInfo>& infos, const std::vector<Bytes>& vbs, const std::vector<Bytes>& ibs,
               const std::vector<TexSpan>& textures);
// build_cat with the given sections (2, 4, 5, 6, 9 in the Python's order) and options ('ref4', 'refonly'); the
// options 'cloth' (multi_cloth_spare_only) and extra_groups (shadow chain) are not ported.
Bytes BuildCat(const Model& ref, const Model& body, const std::vector<int>& sections, const std::vector<int64_t>& kept_ids,
               const std::vector<std::string>& sections_opts);

// strip_tris(idx): the triangles of a strip with 0xFFFF restarts, odd triangles swapped (the Python generator's order).
std::vector<std::array<uint16_t, 3>> StripTris(std::span<const uint16_t> idx);

// The ROLE table's channel codes, sorted (the assert after build_skeleton).
std::vector<int64_t> RoleCodesSorted();
// The DOAX2 alternate-foot objects (DROP_OBJECTS_BODY).
const std::set<int64_t>& DropObjectsBody();
// u32(d, o) of a mesh record.
uint32_t U32(std::span<const uint8_t> d, int64_t o);

}  // namespace doa4::suitconv::core
