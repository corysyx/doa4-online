// Suit converter (Plan 2): the bust channel, a port of physics_port.py's prep_bust, build_mpv and bust_peak
// (the Python recipe's physics_port.py lines 28-121, 124-174 and 971-982, SHA-1 b8b69003c5ae388868ed8e5fff5f50a91263f98a).
// Python's name -> C++ name: snake_case -> CamelCase. The report text (`report.append`) is not ported.
//
// PrepBust runs after build_objects: per chest part with a 7-buffer morph decl it computes the down and up offsets per
// vertex (p.bust), eases them near the cloth and near the suit's straps, holds the border vertices still (p.border) and
// marks the part writable. BuildMpv runs after encode_objects and writes the MPV block (channel 4 and any extra
// channels). BustPeak is the chest's most forward point. Pure C++.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "doa4_suitconv_core.h"

namespace doa4::suitconv::bust {

// prep_bust(objects, parts, chan2id, down_scale, up_scale, report, tol, cloth_objs, cloth_model, fade, strap_fade):
// returns the chest object id. `cloth_model` is the model `is_cloth` and the strap test compare against (the suit);
// cloth_objs is empty unless --port-cloth (Task 10).
int64_t PrepBust(std::vector<std::optional<core::Object>>& objects, const std::vector<core::Part*>& parts,
                 const core::PyDict<int64_t, int64_t>& chan2id, double down_scale, double up_scale,
                 const std::set<int64_t>& cloth_objs, const core::Model* cloth_model, double strap_fade,
                 double tol = 0.001, double fade = 0.04);

// bust_peak(objects, chest, bind4): (y, z) in the chest's frame, or nullopt (Python None).
std::optional<std::array<double, 2>> BustPeak(const std::vector<std::optional<core::Object>>& objects, int64_t chest,
                                              const core::PyDict<int64_t, core::Vec3>& bind4);

// One row of the cloth pin block (pins argument): s32 rel, then rest, down and up as 3 floats each.
struct PinRow {
  int32_t rel = 0;
  std::array<double, 3> rest{}, down{}, up{};
};

// build_mpv(objects, geos, bind4, chest, report, thresh, full_down, full_up, extra, pins): the MPV block. `extra` is
// the dict of extra channels (index -> bytes, the hand and face channels); null or empty is Python's None / {}.
// `pins` null or empty is None.
std::vector<uint8_t> BuildMpv(const std::vector<std::optional<core::Object>>& objects,
                              const std::vector<tpr::ObjGeo>& geos, const core::PyDict<int64_t, core::Vec3>& bind4,
                              int64_t chest, double thresh, double full_down, double full_up,
                              const std::map<int64_t, std::vector<uint8_t>>* extra = nullptr,
                              const std::vector<PinRow>* pins = nullptr);

}  // namespace doa4::suitconv::bust
