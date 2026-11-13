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
#include <optional>
#include <string>
#include <vector>

#include "doa4_costume_lists.h"
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
  // The game reads part of it once at boot: applied at load or not at all. The menu only saves it, for the next start.
  bool at_load_only = false;
};

inline constexpr size_t kTagAllStages = 0;
inline constexpr size_t kMpHiddenCharacters = 1;
inline constexpr size_t kHelenaOpera = 2;
inline constexpr size_t kKokoroKimonos = 3;
inline constexpr size_t kChristieCostume = 4;
inline constexpr size_t kAlpha152 = 5;
const std::array<Tweak, 6>& AllTweaks();
// The order the Extras tab lists them in: the table tweaks and Alpha-152 under "Game tweaks", then the costume boxes.
inline constexpr std::array<size_t, 6> kMenuOrder = {kTagAllStages,  kMpHiddenCharacters, kAlpha152,
                                                     kHelenaOpera,   kKokoroKimonos,      kChristieCostume};

// Alpha-152's Random cell: the one item of her tweak that is a write after the load, not a patch. The game fills the
// 24th select cell (kSelectLastCell) once at boot, by copying the 40 bytes at kSelectStandIn (its static initialiser
// sub_82A3DCB0). The Xenia patch puts Random's record into that source. The port leaves the source alone, because the
// select code shows the same record in place of every locked character and pads the Story grid with it (as Random, a
// locked cell reads RANDOM and A on it picks a random fighter: runs f_vs_off / f_vs_on of 2026-11-12), and writes
// Random's record into the cell itself, right after the game's copy (the hook on sub_82A3DCB0,
// doa4_alpha_select_hooks.cpp), only when her patches were applied at load.
inline constexpr uint32_t kSelectStandIn = 0x82C346D0;   // 40 bytes: the boot copy's source, and the locked cell's stand-in
inline constexpr uint32_t kSelectLastCell = 0x82F5F2C8;  // 40 bytes: the 24th select cell
// Random's record at kSelectLastCell (the Xenia patch's 40 bytes for kSelectStandIn).
const Patch& Alpha152RandomCell();
// The step right after the game's boot copy. `applied_at_load`: her tweak's bytes are in guest memory since the load
// (TweakPatcher::IsOn once EndLoad was called), so all of her patches or none; the menu's box never changes it. Writes
// Random's record into the 24th cell, or nothing; returns the bytes written.
size_t FillAlpha152RandomCell(const GuestBytes& guest, bool applied_at_load);

// What the Extras tab says under an at-load-only tweak's box (Alpha-152). `saved`: the setting as saved (her cvar, which
// the box changes). `running`: whether her tweak is in the running game, or nothing while the image is not loaded yet:
// then the saved setting is still to be applied at this very start, so there is nothing to restart for.
enum class RestartNote : uint8_t {
  kNone,            // the running game is as saved, or not loaded yet
  kRunningWith,     // "Applies after a restart (running with ...)"
  kRunningWithout,  // "Applies after a restart (running without ...)"
};
inline RestartNote RestartNoteFor(bool saved, std::optional<bool> running) {
  if (!running || saved == *running) return RestartNote::kNone;
  return *running ? RestartNote::kRunningWith : RestartNote::kRunningWithout;
}

// The game's costume tables, by charId. The lookup sub_82526B78 reads the list,
// the variants and the length; the select screen and the unlocks read the max.
inline constexpr uint32_t kCostumeMax = costume_lists::kMax;            // u8[25]
inline constexpr uint32_t kCostumeLength = costume_lists::kLength;      // u8[25]
inline constexpr uint32_t kCostumeList = costume_lists::kList;          // be32[25]: u8 model per (costume, variant)
inline constexpr uint32_t kCostumeVariants = costume_lists::kVariants;  // be32[25]: u8 variants per costume

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

  // The load is over (the app calls it once, after it applied the saved tweaks and before any guest code runs): from here
  // on Apply refuses an at_load_only tweak in both directions, "applies at the next start", and writes nothing for it.
  void EndLoad() { load_ended_ = true; }
  bool load_ended() const { return load_ended_; }

  // Writes the tweak's bytes (on) or the off-state bytes (off): what CaptureOriginals saw, or for a girl with suits the state
  // CarrySuits worked out. Read-only
  // patches are made writable first and read-only again after; if any cannot
  // be made writable, nothing is written. Returns "" or what went wrong.
  std::string Apply(const GuestBytes& guest, size_t tweak, bool on, const ProtectFn& protect);

  // Whether guest memory holds the tweak's bytes.
  bool IsOn(const GuestBytes& guest, size_t tweak) const;

  // The suits (the import's offline, the picks in a session) come after the boxes' costumes in the same tables, and go in after
  // the boxes applied (costumes::Apply). Call this right after, for each character Apply gave suits, with her suits' models in order: her
  // box (at most one per character; the host test pins that), on or off, then holds them too, so toggling it later moves
  // only its own costumes and her suits stay, numbered after them. Takes two guest blocks in either state; a character
  // with no box takes none. With the box off and the suits leaving no room for its costumes, turning it on is refused by
  // name. "" or what went wrong (no guest memory: the box then refuses to change, and her suits stay).
  std::string CarrySuits(const GuestBytes& guest, const AllocFn& alloc, uint32_t character,
                         const std::vector<uint8_t>& models);

 private:
  // patches_[tweak]: AllTweaks()'s patches, or for a costume tweak the words CaptureOriginals worked out.
  std::vector<std::vector<Patch>> patches_;
  // originals_[tweak][patch] = the bytes CaptureOriginals saw; CarrySuits replaces a costume box's words with the off state that
  // holds her suits.
  std::vector<std::vector<std::vector<uint8_t>>> originals_;
  std::vector<bool> unavailable_;  // by tweak: no guest memory for its blocks
  std::vector<std::string> refuse_on_;  // by tweak: why turning it on is refused (CarrySuits), or empty
  bool load_ended_ = false;
};

}  // namespace doa4::tweaks
