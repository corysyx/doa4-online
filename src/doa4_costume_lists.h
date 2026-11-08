// The one rule for moving a character's costume list, shared by the two systems that write the costume tables: the Extras
// boxes (doa4_tweaks) and the imported suits (doa4_costumes). Boxes first, suits after them.
//
// By charId c:
//   max[c]   u8   kMax + c        costumes (caps the save's unlock count)
//   len[c]   u8   kLength + c     list length (the sum of the variant counts)
//   list[c]  be32 kList + 4c   -> u8 model index per (costume, variant), costume-major
//   var[c]   be32 kVariants + 4c-> u8 variants per costume
//
// The rule: the list and variants as they stand now, then each added model with one variant, then written zeros to the end
// of the block. Whoever moves a list reads her tables at that moment, never the image's. Nothing the game reads changes
// until the pointers and counts are written.

#pragma once

#include <cstdint>
#include <vector>

#include "doa4_guest.h"

namespace doa4::costume_lists {

inline constexpr uint32_t kMax = 0x82C34888;       // u8[25]
inline constexpr uint32_t kLength = 0x82C348A4;    // u8[25]
inline constexpr uint32_t kList = 0x82C348C0;      // be32[25]
inline constexpr uint32_t kVariants = 0x82C34928;  // be32[25]

// sub_8249A710 indexes a 15-entry threshold table by the unlock count, so a character keeps at most 17 costumes.
inline constexpr uint32_t kCostumeLimit = 17;
// A costume number at or above max makes the lookup read variant bytes past the array, and a costume number is a u8: a
// block this size with zeros after the data cannot be read past. The boxes' own blocks stay at their v1.0.9 size (32
// bytes, doa4_tweaks.cpp), so a session with no suits has v1.0.9's heap.
inline constexpr uint32_t kFullBlock = 256;

struct Tables {
  uint32_t list = 0, variants = 0;  // guest addresses
  uint32_t length = 0, maximum = 0;
};

inline Tables Read(const GuestBytes& guest, uint32_t character) {
  return {guest.ReadBe32(kList + 4 * character), guest.ReadBe32(kVariants + 4 * character), guest.Read(kLength + character),
          guest.Read(kMax + character)};
}

// Fills the two blocks: `from`, then `added`, then zeros. Nothing the game reads changes.
inline void FillMoved(const GuestBytes& guest, const Tables& from, const std::vector<uint8_t>& added, uint32_t list_block,
                      uint32_t variants_block, uint32_t block_size) {
  const uint32_t n = uint32_t(added.size());
  for (uint32_t i = 0; i < block_size; ++i) {
    const uint8_t entry = i < from.length ? guest.Read(from.list + i)
                          : i < from.length + n ? added[i - from.length]
                                                : uint8_t(0);
    guest.Write(list_block + i, entry);
    const uint8_t count = i < from.maximum ? guest.Read(from.variants + i) : i < from.maximum + n ? uint8_t(1) : uint8_t(0);
    guest.Write(variants_block + i, count);
  }
}

}  // namespace doa4::costume_lists
