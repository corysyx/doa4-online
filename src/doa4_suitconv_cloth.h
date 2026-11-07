// Suit converter (Plan 2): the suit's own authored cloth, a port of physics_port.py's find_cloth, prep_cloth,
// band_motion, _vertex_mapper, _mapper, build_cloth, _port_cloth, _move_bust_colliders, pin_rows, weld_chest_cloth,
// _shapes, _inside, fit_leg_colliders, sink_hamstrings, _push_out, unstick_nodes, back_margin and fit_cloth_chunk
// (physics_spike/recipe_m1/physics_port.py lines 178-463 and 985-1306, SHA-1 b8b69003c5ae388868ed8e5fff5f50a91263f98a).
// Python's name -> C++ name: snake_case -> CamelCase. The report text (`report.append`) is not ported; values computed only
// for it are not computed, except where the Python's computation can raise (then the raise is kept).
//
// The DOAX2 suit's MULTI_CLOTH (one per body type) is copied, its grid nodes and pinned rest points retargeted onto the
// DOA4 body, its springs rescaled, its writes renumbered to the converted object, its bone-23 colliders moved onto the
// chest, and (with the options) its leg colliders fitted to the legs, its free nodes started outside every collider and
// its thigh shapes moved back. weld_chest_cloth, band_motion and sink_hamstrings move part vertices before encoding.
//
// Numeric sites: each np.linalg.norm, `@`, math.dist and `** 0.5` is computed the way a census against the recipe's
// interpreter showed numpy 1.26.3 / CPython 3.12.1 compute it on the oracle PC (the comment at each site names its path).
// Pure C++.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "doa4_suitconv_bust.h"
#include "doa4_suitconv_core.h"

namespace doa4::suitconv::cloth {

// One entry of prep_cloth's `where` (decl index -> (oid, k, part)).
struct WhereEntry {
  int64_t oid = 0;
  int64_t k = 0;
  core::Part* part = nullptr;
};
using Where = core::PyDict<int64_t, WhereEntry>;

// find_cloth's (chunk offset, [cloth offsets], written object).
struct Found {
  int64_t mc = 0;
  std::vector<int64_t> cloths;
  int64_t obj = 0;
};
// find_cloth(suit, keep): every MULTI_CLOTH of the suit's .cat whose cloths write an object `keep` holds (all when
// keep is nullopt, Python's None).
std::vector<Found> FindCloth(const core::Model& suit, const std::optional<std::set<int64_t>>& keep);

// prep_cloth's spec (mc, cloths, obj, owner, where).
struct Spec {
  int64_t mc = 0;
  std::vector<int64_t> cloths;
  int64_t obj = 0;
  int64_t owner = 0;
  Where where;
};
std::vector<Spec> PrepCloth(const core::Model& suit, const std::optional<std::set<int64_t>>& keep,
                            std::vector<std::optional<core::Object>>& objects);

// weld_chest_cloth(objects, chest, suit, cloth_objs, report)
void WeldChestCloth(std::vector<std::optional<core::Object>>& objects, int64_t chest, const core::Model& suit,
                    const std::set<int64_t>& cloth_objs);

// band_motion(spec, objects, chest, report, fade, sink)
void BandMotion(const Spec& spec, std::vector<std::optional<core::Object>>& objects, int64_t chest, double fade = 0.04,
                double sink = 0.006);

// sink_hamstrings(objects, suit, report, amount)
void SinkHamstrings(std::vector<std::optional<core::Object>>& objects, const core::Model& suit, double amount);

// One entry of build_cloth's pins_out: (offset of the FIX_GRID rest point in the chunk, the retargeted point).
struct Pin {
  int64_t at = 0;
  core::Vec3 nv{};
};
// build_cloth(suit, spec, geos, bind4, maps, report, pins_out, bust_peak): the retargeted MULTI_CLOTH chunk. peak is
// bust_peak's (y, z) or nullopt (None).
std::vector<uint8_t> BuildCloth(const core::Model& suit, const Spec& spec, const std::vector<tpr::ObjGeo>& geos,
                                const core::PyDict<int64_t, core::Vec3>& bind4, const core::Maps& maps,
                                std::vector<Pin>* pins_out, const std::optional<std::array<double, 2>>& peak);

// fit_cloth_chunk(chunk, bones, bind_own, legs, hips, report, unstick, fit, back). bones: bone n (0..14) -> its bind
// position, in ascending n; legs: bone 6 and 12 -> the limb's vertices; hips: the pelvis object's vertices.
std::vector<uint8_t> FitClothChunk(const std::vector<uint8_t>& chunk, const core::PyDict<int64_t, core::Vec3>& bones,
                                   const core::Vec3& bind_own, const std::map<int64_t, std::vector<core::Vec3>>& legs,
                                   const std::vector<core::Vec3>& hips, bool unstick, bool fit, double back);

// pin_rows(pins, objects, chest, bind4, report, suit): one row per pin (rel = the pin's offset in the chunk).
std::vector<bust::PinRow> PinRows(const std::vector<Pin>& pins, const std::vector<std::optional<core::Object>>& objects,
                                  int64_t chest, const core::PyDict<int64_t, core::Vec3>& bind4, const core::Model& suit);

// math.dist(p, q) for 3-vectors (CPython v3.12.1 Modules/mathmodule.c math_dist_impl and vector_norm, lines 2462-2590):
// the scaled, compensated sum of squares with the differential correction of the square root. Exposed for the test.
double MathDist(const core::Vec3& p, const core::Vec3& q);

// (P - p0) @ uu for one row of a P with more than one row (numpy 1.26.3 matmul of a C-contiguous (N, 3) array by a
// 3-vector calls cblas_dgemv; OpenBLAS 0.3.23's x86_64 dgemv_t_4.c handles m = 3 in its remainder loop, compiled with
// contraction): 0 + fma(a2, u2, fma(a0, u0, a1 * u1)). Chosen by census (0 mismatches over 1.09 million real rows; the
// plain sum and every other fused order miss). Exposed for the test.
double Gemv3(const core::Vec3& a, const core::Vec3& u);

// The order CPython 3.12 iterates set(keys) in, for ints (keys in insertion order; duplicates allowed). A port of
// setobject.c's insertion, resize and iteration (catx::SmallSetOrder is its special case of at most four keys).
// Exposed for the test.
std::vector<int64_t> PySetOrder(const std::vector<int64_t>& keys);

}  // namespace doa4::suitconv::cloth
