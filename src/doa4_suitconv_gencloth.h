// Suit converter (Plan 2): the generated cloth, a port of the recipe's cloth_gen.py (physics_spike/recipe_m1/cloth_gen.py
// prep, derive, straight_lines, build and walk, with GENERATED, SOFT_HEM, DONOR_PROPERTY and WELD as constants). Python's name ->
// C++ name: snake_case -> CamelCase. The report text (`report.append`) is not ported; values computed only for it are not
// computed.
//
// A generated cloth is a MULTI_CLOTH built from a mesh that has no authored cloth: one node per welded position, springs,
// hops, normal fans, faces and threads derived from the triangles and the top boundary loop, the PROPERTY block and the
// colliders of a DOAX2 skirt (the donor, DOAX2 werone entry 283) and the bend limit block of a DOA4 skirt (DOA4 chrthree
// entry 91, rtm01.cat). The donors are passed as bytes (SuitInputs::gen_donor_cat, gen_bend_cat), never read from a path.
//
// Numeric sites: each np.linalg.norm, `@`, sum and mean is computed the way a census against the recipe's interpreter showed
// numpy 1.26.3 computes it on the oracle PC (the comment at each site names its path). Pure C++.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "doa4_suitconv_core.h"

namespace doa4::suitconv::gencloth {

// cloth_gen.prep's result: dict(design, oid, k, part, meshes).
struct Spec {
  std::string design;
  int64_t oid = 0;
  int64_t k = 0;
  core::Part* part = nullptr;
  std::vector<int64_t> meshes;
};

// prep(design, suit, objects, chan2id, report): finds the skirt's decl in the converted model (exactly one part of `suit` with
// the GENERATED object and decl), requires it on the rigid hips object (no palette), makes it writable and clears its bust.
// Throws ConvertError (the assert's text, or the KeyError of a design that is not in GENERATED).
Spec Prep(std::string_view design, const core::Model& suit, std::vector<std::optional<core::Object>>& objects,
          const core::PyDict<int64_t, int64_t>& chan2id);

// build(spec, geos, bind4, strip_tris, report): the MULTI_CLOTH chunk, after encoding. donor is DOAX2 werone 283 (cloth_gen's
// DONOR_FILE), bend is DOA4 chrthree 91 (DONOR_BEND). Ends with walk(chunk, 0).
std::vector<uint8_t> Build(const Spec& spec, const std::vector<tpr::ObjGeo>& geos,
                           const core::PyDict<int64_t, core::Vec3>& bind4, std::span<const uint8_t> donor,
                           std::span<const uint8_t> bend);

// The numeric sites of cloth_gen.py, each as the census chose it (see the .cpp), exposed so the test can pin them against the
// recipe's interpreter: float(v @ w / (np.linalg.norm(v) * np.linalg.norm(w) + 1e-12)) at lines 87 and 106; float(np.linalg.norm(a
// - b)) at lines 65, 223 and 457; np.abs(steps[:, 1]).sum() / (np.linalg.norm(steps, axis=1).sum() + 1e-12) at line 262 (steps
// not empty); float(P[L, 1].mean()) at line 207 (ys = P[L, 1]).
double Cosine(const core::Vec3& v, const core::Vec3& w);
double Distance(const core::Vec3& a, const core::Vec3& b);
double VerticalRatio(const std::vector<core::Vec3>& steps);
double LoopKey(const std::vector<double>& ys);

// walk(d, mc): the loader's walk with every rule of the format note's section 2 asserted; throws ConvertError on the first
// broken rule (the assert's text, or "read past end" where Python raises struct.error).
void Walk(std::span<const uint8_t> d, int64_t mc);

}  // namespace doa4::suitconv::gencloth
