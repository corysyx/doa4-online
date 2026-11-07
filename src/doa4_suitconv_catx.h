// Suit converter (Plan 2): the .cat attachments, a port of the recipe's physics_port.attach, _al, with_spares and
// prep_head_physics (physics_spike/recipe_m1/physics_port.py lines 22-24, 466-523 and 917-967, SHA-1
// b8b69003c5ae388868ed8e5fff5f50a91263f98a).
// Attach's whole frame is here in Python's order: the donor's hand request table (_add_apa), the head physics section
// (+8), the MPV (+0xC) and the MULTI_CLOTH (+0x14) with the relocation of the MPV's pin rows (MPP +0x40). Each block is
// appended at a 16-byte boundary and the file ends aligned. WithSpares puts the reference body's hair collision
// SPARE_CHUNK onto the suit's MULTI_CLOTH or a new empty one; PrepHeadPhysics carries Tina's head physics section and
// face morph channels whole. Pure C++.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "doa4_suitconv_core.h"

namespace doa4::suitconv::catx {

// _al(b, a): b padded with zeros to a multiple of a; returns the new length.
size_t Al(std::vector<uint8_t>& b, size_t a = 16);

// attach(cat, mpv, cloth, donor_cat, head_physics). A null pointer is Python's None; an empty vector is falsy where
// the Python tests truthiness (mpv, cloth, head_physics) and not None where it tests `is not None` (donor_cat).
std::vector<uint8_t> Attach(std::span<const uint8_t> cat, const std::vector<uint8_t>* mpv,
                            const std::vector<uint8_t>* cloth, const std::vector<uint8_t>* donor_cat,
                            const std::vector<uint8_t>* head_physics);

// with_spares(cloth, ref_cat, report): `cloth` null is Python's None. Returns the cloth with the reference's SPARE_CHUNK
// appended (a new empty MULTI_CLOTH, the reference's first 0x50 bytes with +0x18 = 0, when there is none), the cloth
// unchanged when the reference has no spares (nullopt when that is None too). Every assert is a ConvertError.
std::optional<std::vector<uint8_t>> WithSpares(const std::vector<uint8_t>* cloth, std::span<const uint8_t> ref_cat);

// prep_head_physics(ref, head_ids, objects, report): the physics section and the face morph channels 2 and 3 (dict
// order: 2 then 3). Marks every part of each head object writable.
struct HeadPhysics {
  std::map<int64_t, std::vector<uint8_t>> channels;
  std::vector<uint8_t> section;
};
HeadPhysics PrepHeadPhysics(std::span<const uint8_t> ref_cat, const std::vector<int64_t>& head_ids,
                            std::vector<std::optional<core::Object>>& objects);

// {**hand, **face}: a face key replaces an equal hand key (convert.py line 1516).
void MergeChannels(std::map<int64_t, std::vector<uint8_t>>& hand, const std::map<int64_t, std::vector<uint8_t>>& face);

// The order CPython 3.12 iterates `set(ids)` in, for up to four distinct ints (an 8-slot table that never resizes).
// Exposed for the test.
std::vector<int64_t> SmallSetOrder(const std::vector<int64_t>& ids);

}  // namespace doa4::suitconv::catx
