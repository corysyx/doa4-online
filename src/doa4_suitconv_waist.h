// Suit converter (Plan 2): the waist fit, a port of physics_port.py's _waist_samples and prep_waist_fit
// (physics_spike/recipe_m1/physics_port.py lines 585-609 and 610-704, SHA-1 b8b69003c5ae388868ed8e5fff5f50a91263f98a).
// Python's name -> C++ name: snake_case -> CamelCase. The report text (`report.append`) is not ported.
//
// PrepWaistFit runs after the bust stage and before encode_objects: it finds the one object whose palette is
// [chest, pelvis], builds a (height, angle) grid of 4 weights from the template's bare waist (the model passed as
// `ref`: hel05, bones 2 and 3), stretches it over the band between this mesh's chest and pelvis seams, and sets
// p.h4 on every part of that object (FLOAT16_4 weights, written by encode_objects). Returns the object id, or nullopt
// (Python None) when no single object has that palette. Pure C++.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "doa4_suitconv_core.h"

namespace doa4::suitconv::waist {

// prep_waist_fit(ref, template_ids, objects, parts, nodes, chan2id, report, seam_tol=0.0015); template_ids = (t_chest,
// t_pelvis). Throws ConvertError("('waist band too short', <lo>, <hi>)") as the Python assert does.
std::optional<int64_t> PrepWaistFit(const core::Model& ref, int64_t t_chest, int64_t t_pelvis,
                                    std::vector<std::optional<core::Object>>& objects,
                                    const std::vector<core::Part*>& parts, const std::vector<tpr::Node>& nodes,
                                    const core::PyDict<int64_t, int64_t>& chan2id, double seam_tol = 0.0015);

}  // namespace doa4::suitconv::waist
