// doa4_suits_bust.h
// Live strength of a converted DOAX2 suit's bust morph (ESC menu, Extras tab).
//
// The game writes a morph's poses straight into the vertices as absolute positions, so the strength is in the pose
// data. The converter marks a suit's bust MPP block in header bytes vanilla blocks leave zero. The binder
// (sub_82374E98) only touches +0x08, +0x18 and the pose pointers, so the marks survive loading:
//   +0x08 1 once bound; +0x14 pose count (rest, down, up); +0x1C/+0x20/+0x24 absolute pose addresses, 12 bytes a record
//   +0x28 kMagic; +0x2C record count
//   +0x30 / +0x34 float: the down pose's ratio now, and its ratio at the full DOAX2 range
//   +0x38 / +0x3C float: the same for the up pose
//   +0x40 pin block, from the block; +0x44 pin count. A chest cloth's pinned edge follows the morph (DOAX2 shirts do this; DOA4 has no such mechanism).
//   A row is 40 bytes: s32 from the block to the cloth's FIX_GRID rest position, then rest, down and up as 3 floats each.
// DOAX2's up target is about twice its down target and DOA4's is not, so the two poses have their own ratios. A ratio
// is relative to the strength baked into the file, which is 1. Pure code over a big-endian memory accessor
// (ReadU32 / WriteU32), so host tests run it without the game.
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

namespace doa4::suits::bust {

inline constexpr uint32_t kMagic = 0x44583242;  // "DX2B"

template <class Mem>
bool IsMarked(const Mem& m, uint32_t mpp) {
  return m.ReadU32(mpp + 0x28) == kMagic;
}

namespace detail {
inline bool Usable(float ratio) { return std::isfinite(ratio) && ratio > 0.0f; }
}  // namespace detail

// Moves the down and up poses of a marked, bound block about the rest pose to the file's strength (full = false) or
// the full DOAX2 range (full = true). True if it wrote anything.
template <class Mem>
bool Rescale(Mem& m, uint32_t mpp, bool full) {
  if (!IsMarked(m, mpp) || m.ReadU32(mpp + 0x08) != 1 || m.ReadU32(mpp + 0x14) != 3) return false;
  const uint32_t records = m.ReadU32(mpp + 0x2C);
  if (records == 0) return false;
  struct Pose {
    uint32_t slot;
    uint32_t now_at;
    uint32_t full_at;
  };
  const Pose poses[2] = {{0x20, 0x30, 0x34}, {0x24, 0x38, 0x3C}};
  float current[2], wanted[2];
  for (int k = 0; k < 2; ++k) {
    current[k] = std::bit_cast<float>(m.ReadU32(mpp + poses[k].now_at));
    wanted[k] = full ? std::bit_cast<float>(m.ReadU32(mpp + poses[k].full_at)) : 1.0f;
    if (!detail::Usable(current[k]) || !detail::Usable(wanted[k])) return false;
  }
  if (current[0] == wanted[0] && current[1] == wanted[1]) return false;
  const uint32_t rest = m.ReadU32(mpp + 0x1C);
  for (int k = 0; k < 2; ++k) {
    const float factor = wanted[k] / current[k];
    const uint32_t pose = m.ReadU32(mpp + poses[k].slot);
    for (uint32_t i = 0; i < records * 3; ++i) {
      const float r = std::bit_cast<float>(m.ReadU32(rest + 4 * i));
      const float p = std::bit_cast<float>(m.ReadU32(pose + 4 * i));
      m.WriteU32(pose + 4 * i, std::bit_cast<uint32_t>(r + (p - r) * factor));
    }
    m.WriteU32(mpp + poses[k].now_at, std::bit_cast<uint32_t>(wanted[k]));
  }
  return true;
}

// The morph state's blend, in pose weights (pose 0 rest, 1 down, 2 up), as sub_82373E50 hands it to its six appliers.
struct Blend {
  float down = 0.0f;
  float up = 0.0f;
};

// state: +12/+16/+20 pose numbers (-1 none), +44/+48/+52 weights, +76 count of a longer list at the same places.
template <class Mem>
Blend ReadBlend(const Mem& m, uint32_t state) {
  Blend out;
  const auto pose = [&](int i) { return static_cast<int32_t>(m.ReadU32(state + 12 + 4 * i)); };
  const auto weight = [&](int i) { return std::bit_cast<float>(m.ReadU32(state + 44 + 4 * i)); };
  const auto add = [&](int p, float w) {
    if (p == 1) out.down += w;
    if (p == 2) out.up += w;
  };
  const int32_t list = static_cast<int32_t>(m.ReadU32(state + 76));
  if (list > 0) {
    for (int i = 0; i < list && i < 3; ++i) add(pose(i), weight(i));
    return out;
  }
  if (pose(0) >= 0 && pose(0) == pose(1) && weight(2) == 0.0f) {  // one pose, drawn whole
    add(pose(0), 1.0f);
    return out;
  }
  for (int i = 0; i < 3; ++i) {
    if (pose(i) >= 0) add(pose(i), weight(i));
  }
  return out;
}

// Writes the chest cloth's pin positions for this blend. The toggle's strength applies to them too.
template <class Mem>
void FollowPins(Mem& m, uint32_t mpp, const Blend& blend) {
  const uint32_t block = m.ReadU32(mpp + 0x40);
  const uint32_t count = m.ReadU32(mpp + 0x44);
  if (block == 0 || count == 0 || count > 64) return;
  const float dn_now = std::bit_cast<float>(m.ReadU32(mpp + 0x30));
  const float up_now = std::bit_cast<float>(m.ReadU32(mpp + 0x38));
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t row = mpp + block + 40 * i;
    const uint32_t target = mpp + static_cast<uint32_t>(static_cast<int32_t>(m.ReadU32(row)));
    for (uint32_t c = 0; c < 3; ++c) {
      const float rest = std::bit_cast<float>(m.ReadU32(row + 4 + 4 * c));
      const float dn = std::bit_cast<float>(m.ReadU32(row + 16 + 4 * c));
      const float up = std::bit_cast<float>(m.ReadU32(row + 28 + 4 * c));
      m.WriteU32(target + 4 * c, std::bit_cast<uint32_t>(rest + blend.down * dn_now * dn + blend.up * up_now * up));
    }
  }
}

}  // namespace doa4::suits::bust
