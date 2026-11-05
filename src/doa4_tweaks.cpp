// Game tweaks for the Extras tab. See doa4_tweaks.h.

#include "doa4_tweaks.h"

#include <format>

namespace doa4::tweaks {

namespace {

// Tag battle stage list: 17 x 16 bytes at 0x82C32AF8, byte +1 = "allowed in
// Tag". The patch allows all 17 and puts the default cursor (0x82C32AF7) on
// Random.
std::vector<Patch> TagAllStagesPatches() {
  std::vector<Patch> patches = {{0x82C32AF7, {0xFF}}};
  for (uint32_t k = 0; k < 17; ++k) patches.push_back({0x82C32AF9 + 0x10 * k, {0x01}});
  return patches;
}

// Versus select table: 24 x 40 bytes at 0x82F5EF30, byte +2 = selectable in
// the select mode that ignores the profile. Entries 16-21: Helena, Ein,
// Gen Fu, Leon, Tengu, Spartan-458.
std::vector<Patch> MpHiddenCharacterPatches() {
  std::vector<Patch> patches;
  for (uint32_t k = 0; k < 6; ++k) patches.push_back({0x82F5F1B2 + 0x28 * k, {0x01}});
  return patches;
}

constexpr uint32_t kCostumeBlockSize = 32;  // zero-filled past the entries; no character here comes near it

std::vector<uint8_t> Be32Bytes(uint32_t value) {
  return {uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
}

uint32_t ReadBe32(const GuestBytes& guest, uint32_t address) {
  uint32_t value = 0;
  for (uint32_t i = 0; i < 4; ++i) value = (value << 8) | guest.Read(address + i);
  return value;
}

// Allocates and fills copies of the character's list and variants with the new costumes appended, and adds the patches
// that point the game at them and raise the length and the max. Nothing the game reads changes until the patches apply.
bool AddCostumePatches(const GuestBytes& guest, const AllocFn& alloc, const CostumeAddition& addition,
                       std::vector<Patch>* patches) {
  const uint32_t c = addition.character;
  const uint32_t list_block = alloc(kCostumeBlockSize);
  const uint32_t variants_block = alloc(kCostumeBlockSize);
  if (!list_block || !variants_block) return false;
  const uint32_t list = ReadBe32(guest, kCostumeList + 4 * c);
  const uint32_t variants = ReadBe32(guest, kCostumeVariants + 4 * c);
  const uint32_t length = guest.Read(kCostumeLength + c);
  const uint32_t maximum = guest.Read(kCostumeMax + c);
  const uint32_t added = uint32_t(addition.models.size());
  for (uint32_t i = 0; i < kCostumeBlockSize; ++i) {
    const uint8_t entry = i < length ? guest.Read(list + i)
                          : i < length + added ? addition.models[i - length]
                                               : uint8_t(0);
    guest.Write(list_block + i, entry);
    const uint8_t count = i < maximum ? guest.Read(variants + i) : i < maximum + added ? uint8_t(1) : uint8_t(0);
    guest.Write(variants_block + i, count);
  }
  patches->push_back({kCostumeList + 4 * c, Be32Bytes(list_block)});
  patches->push_back({kCostumeVariants + 4 * c, Be32Bytes(variants_block)});
  patches->push_back({kCostumeLength + c, {uint8_t(length + added)}});
  patches->push_back({kCostumeMax + c, {uint8_t(maximum + added)}});
  return true;
}

}  // namespace

// Alpha-152 (a third patch) was removed on 2026-10-28: her character data
// lacks the motions and script entries a player-controlled fighter needs, and
// selecting her crashed in Survival, Time Attack and Versus.
const std::array<Tweak, 5>& AllTweaks() {
  static const std::array<Tweak, 5> tweaks = {{
      {"doa4_tag_all_stages", "All stages in Tag battle", TagAllStagesPatches(), {}},
      {"doa4_mp_hidden_characters", "Hidden characters in multiplayer select",
       MpHiddenCharacterPatches(), {}},
      // The models the Xenia Rev 1 patches put on slots 1 and 2 (ryu_highabusa), as extra costumes instead: Helena m3
      // (hel03 + helAD), Kokoro m8 (pink) and m3 (blue), Christie m5. On the disc; no costume list uses them.
      {"doa4_helena_opera", "Helena's opera dress", {}, {{0x07, {0x03}}}},
      {"doa4_kokoro_kimonos", "Kokoro's pink and blue kimonos", {}, {{0x0A, {0x08, 0x03}}}},
      {"doa4_christie_costume", "Christie's cutscene costume", {}, {{0x14, {0x05}}}},
  }};
  return tweaks;
}

bool IsReadOnly(uint32_t address) {
  return (address >= 0x82000000 && address < 0x822F0000) ||
         (address >= 0x835B0000 && address < 0x83600000);
}

void TweakPatcher::CaptureOriginals(const GuestBytes& guest, const AllocFn& alloc) {
  if (captured()) return;  // later calls would capture patched bytes
  patches_.clear();
  unavailable_.assign(AllTweaks().size(), false);
  for (const Tweak& tweak : AllTweaks()) {
    patches_.push_back(tweak.patches);
    for (const CostumeAddition& addition : tweak.additions) {
      if (!AddCostumePatches(guest, alloc, addition, &patches_.back())) {
        unavailable_[patches_.size() - 1] = true;
      }
    }
    std::vector<std::vector<uint8_t>> per_patch;
    for (const Patch& patch : patches_.back()) {
      std::vector<uint8_t> bytes(patch.bytes.size());
      for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = guest.Read(patch.address + uint32_t(i));
      per_patch.push_back(std::move(bytes));
    }
    originals_.push_back(std::move(per_patch));
  }
}

std::string TweakPatcher::Apply(const GuestBytes& guest, size_t tweak, bool on,
                                const ProtectFn& protect) {
  if (tweak >= AllTweaks().size()) return std::format("no tweak {}", tweak);
  if (!captured()) return "the original bytes were never recorded";
  if (unavailable_[tweak]) return "no guest memory for this costume";
  const std::vector<Patch>& patches = patches_[tweak];

  // Make every read-only patch writable first; on any failure undo and write nothing.
  std::vector<const Patch*> unprotected;
  for (const Patch& patch : patches) {
    if (!IsReadOnly(patch.address)) continue;
    if (!protect(patch.address, uint32_t(patch.bytes.size()), true)) {
      for (const Patch* done : unprotected) {
        protect(done->address, uint32_t(done->bytes.size()), false);
      }
      return std::format("could not make {:#010x} writable", patch.address);
    }
    unprotected.push_back(&patch);
  }

  // In list order on, in reverse off: a costume tweak's lists move before the counts grow, and the counts shrink before
  // the lists move back, so the game reading them meanwhile never indexes past a list.
  for (size_t n = 0; n < patches.size(); ++n) {
    const size_t p = on ? n : patches.size() - 1 - n;
    const std::vector<uint8_t>& bytes = on ? patches[p].bytes : originals_[tweak][p];
    if (bytes.size() == 4 && guest.write_be32) {
      guest.write_be32(patches[p].address, (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
                                               (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]));
      continue;
    }
    for (size_t i = 0; i < bytes.size(); ++i) {
      guest.Write(patches[p].address + uint32_t(i), bytes[i]);
    }
  }

  std::string error;
  for (const Patch* patch : unprotected) {
    if (!protect(patch->address, uint32_t(patch->bytes.size()), false)) {
      error = std::format("wrote, but could not make {:#010x} read-only again", patch->address);
    }
  }
  return error;
}

bool TweakPatcher::IsOn(const GuestBytes& guest, size_t tweak) const {
  if (tweak >= patches_.size() || unavailable_[tweak]) return false;
  for (const Patch& patch : patches_[tweak]) {
    for (size_t i = 0; i < patch.bytes.size(); ++i) {
      if (guest.Read(patch.address + uint32_t(i)) != patch.bytes[i]) return false;
    }
  }
  return true;
}

}  // namespace doa4::tweaks
