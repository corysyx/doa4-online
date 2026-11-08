// Suit converter (Plan 2): the hands, a port of physics_port.py's prep_hands, _tuck_open_edge, _tuck_arm_edge,
// build_hands and _mpp_read (the Python recipe's physics_port.py lines 705-871, SHA-1
// b8b69003c5ae388868ed8e5fff5f50a91263f98a). Python's name -> C++ name: snake_case -> CamelCase. The report text
// (`report.append`) is not ported.
//
// PrepHands runs before encode_objects: the two hand objects (channels 0x900B and 0x9005) must be rigid, their parts
// become writable, the open edge of each hand is drawn into the arm and the arm's own edge is drawn into the hand.
// BuildHands runs after encode_objects: it reads the donor costume's finger poses (the MPV block of its .cat, channels 0
// and 1) and writes, for each hand, an MPP channel that carries the poses over to this mesh's vertices by position,
// with the wrist end eased back to where the forearm expects it. Pure C++.

#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "doa4_suitconv_core.h"

namespace doa4::suitconv::hands {

// One coordinate of the weld key in _tuck_open_edge: np.round(x / 0.0002).astype(int), numpy's rint (half to even), then
// the int32 truncation of this Windows numpy (a NaN, infinity or value beyond int32 gives -2147483648).
int32_t WeldCoord(double x);

// prep_hands(chan2id, objects, report, bind4, strip_tris): {index: oid} in the order of HAND_CHANNELS (0, then 1).
std::vector<std::pair<int64_t, int64_t>> PrepHands(const core::PyDict<int64_t, int64_t>& chan2id,
                                                   std::vector<std::optional<core::Object>>& objects,
                                                   const core::PyDict<int64_t, core::Vec3>& bind4);

// build_hands(donor, hand_objects, objects, geos, bind4, report, tol=0.0003): {index: MPP bytes} in hand_objects order.
std::vector<std::pair<int64_t, std::vector<uint8_t>>> BuildHands(
    const core::Model& donor, const std::vector<std::pair<int64_t, int64_t>>& hand_objects,
    const std::vector<std::optional<core::Object>>& objects, const std::vector<tpr::ObjGeo>& geos,
    const core::PyDict<int64_t, core::Vec3>& bind4, double tol = 0.0003);

}  // namespace doa4::suitconv::hands
