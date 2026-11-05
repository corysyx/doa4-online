// Game tweaks for the Extras tab: byte patches to the game's own tables.
//
// The bytes are the Xenia Rev 1 patches by ryu_highabusa ("544307D1 - DEAD OR
// ALIVE 4 (Rev1).patch.toml"). Each tweak is a bool cvar saved to
// dead_or_alive_4.toml; the app applies the ones that are on at
// OnPostLoadXexImage, before any guest code runs, and the menu applies or
// reverts the live ones at once. They change game data, never the save.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "doa4_guest.h"

namespace doa4::tweaks {

struct Patch {
  uint32_t address;
  std::vector<uint8_t> bytes;
};

// Extra costumes for one character: models from her own model records, each with one variant, after her own costumes.
struct CostumeAddition {
  uint8_t character = 0;  // charId
  std::vector<uint8_t> models;
};

struct Tweak {
  const char* cvar;
  const char* label;
  std::vector<Patch> patches;
  std::vector<CostumeAddition> additions;
};

inline constexpr size_t kTagAllStages = 0;
inline constexpr size_t kMpHiddenCharacters = 1;
inline constexpr size_t kHelenaOpera = 2;
inline constexpr size_t kKokoroKimonos = 3;
inline constexpr size_t kChristieCostume = 4;
const std::array<Tweak, 5>& AllTweaks();

// The game's costume tables (notes/research/doa4_costume_system.md), by charId. The lookup sub_82526B78 reads the list,
// the variants and the length; the select screen and the unlocks read the max.
inline constexpr uint32_t kCostumeMax = 0x82C34888;       // u8[25]
inline constexpr uint32_t kCostumeLength = 0x82C348A4;    // u8[25]
inline constexpr uint32_t kCostumeList = 0x82C348C0;      // be32[25]: u8 model per (costume, variant)
inline constexpr uint32_t kCostumeVariants = 0x82C34928;  // be32[25]: u8 variants per costume

// Guest memory of `size` bytes, or 0.
using AllocFn = std::function<uint32_t(uint32_t size)>;

// Whether the address is in one of the image's READONLY_DATA ranges (from the
// XEX page descriptors: 0x82000000-0x822F0000 and 0x835B0000-0x83600000). The
// SDK maps those pages read-only on the host, so a write needs ProtectFn.
bool IsReadOnly(uint32_t address);

// Makes [address, address + size) writable (true) or read-only again (false).
// Returns false when the protection could not be changed.
using ProtectFn = std::function<bool(uint32_t address, uint32_t size, bool writable)>;

class TweakPatcher {
 public:
  // Records every patch's bytes as they are now. Call once, before Apply. The costume tweaks' lists do not fit where
  // the game keeps them, so each gets two guest blocks here, filled but read by nothing until the tweak turns on: the
  // same allocations whichever tweaks are on, so both players of a session have the same guest heap. A tweak whose
  // blocks could not be allocated refuses Apply.
  void CaptureOriginals(const GuestBytes& guest, const AllocFn& alloc);
  bool captured() const { return !originals_.empty(); }

  // Writes the tweak's bytes (on) or the captured originals (off). Read-only
  // patches are made writable first and read-only again after; if any cannot
  // be made writable, nothing is written. Returns "" or what went wrong.
  std::string Apply(const GuestBytes& guest, size_t tweak, bool on, const ProtectFn& protect);

  // Whether guest memory holds the tweak's bytes.
  bool IsOn(const GuestBytes& guest, size_t tweak) const;

 private:
  // patches_[tweak]: AllTweaks()'s patches, or for a costume tweak the words CaptureOriginals worked out.
  std::vector<std::vector<Patch>> patches_;
  // originals_[tweak][patch] = the bytes CaptureOriginals saw.
  std::vector<std::vector<std::vector<uint8_t>>> originals_;
  std::vector<bool> unavailable_;  // by tweak: no guest memory for its blocks
};

}  // namespace doa4::tweaks
