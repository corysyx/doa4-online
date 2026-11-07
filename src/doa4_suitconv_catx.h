// Suit converter (Plan 2): the .cat attachments, a port of the recipe's physics_port.attach and _al
// (physics_spike/recipe_m1/physics_port.py lines 22-24 and 497-523, SHA-1 b8b69003c5ae388868ed8e5fff5f50a91263f98a).
// Attach's whole frame is here in the Python's order; the branches that need a later task's port (the head physics
// section, the MULTI_CLOTH with its MPP row relocation) throw ConvertError("not ported yet: <branch>") until Tasks 9 and
// 10 fill them. The MPV branch appends the block at a 16-byte boundary and points the header's +0xC at it. The donor
// branch (_add_apa) copies the donor's type-5 hand request table to the end, moves its absolute offsets and adds its
// directory entry. At level L0 none of them runs: Attach copies the .cat and aligns it to 16.
// Pure C++.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace doa4::suitconv::catx {

// _al(b, a): b padded with zeros to a multiple of a; returns the new length.
size_t Al(std::vector<uint8_t>& b, size_t a = 16);

// attach(cat, mpv, cloth, donor_cat, head_physics). A null pointer is Python's None; an empty vector is falsy where
// the Python tests truthiness (mpv, cloth, head_physics) and not None where it tests `is not None` (donor_cat).
std::vector<uint8_t> Attach(std::span<const uint8_t> cat, const std::vector<uint8_t>* mpv,
                            const std::vector<uint8_t>* cloth, const std::vector<uint8_t>* donor_cat,
                            const std::vector<uint8_t>* head_physics);

}  // namespace doa4::suitconv::catx
