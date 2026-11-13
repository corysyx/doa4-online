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

// Alpha-152 playable. Two of the Xenia patch's three items: select entry 22 (Random) becomes Alpha-152, and 0x821B52A7
// switches the 24th cell on (READONLY_DATA). The third item makes the 24th cell Random by rewriting the 40 bytes at
// 0x82C346D0, which the game copies into that cell once at boot. That record is not patched here: it is also the
// stand-in the select code shows for every cell whose state is not 3 (a locked character) and the padding of the Story
// grid, read by sub_82525E80, sub_82523120, sub_82524D78, sub_82525848, sub_825265E0, sub_825267D0, sub_82526960,
// sub_82528D48, sub_82528FF0 and sub_82529DB8; with Random's record there (character 0xFF, flags 1, state 0, instead of
// 0xFE, 0, 1) a locked cell read RANDOM and A on it picked a random fighter (runs f_vs_off / f_vs_on, 2026-11-12, on a
// save with locked characters). Random's record goes into the 24th cell itself, after the game's boot copy
// (Alpha152RandomCell, FillAlpha152RandomCell). The boot copy and the switch are why the tweak applies at load only.
// Then her select view, the port's own (notes/research/alpha152_select_model_2026-11-12.md): her row of three
// per-character tables takes Kasumi's values (charId 5), so her panel has Kasumi's position and cameras instead of the
// filler's distant one: the panel offset (0x82C338F0, four be16 a character: hover x -112, y 0; confirmed 0, 0), the
// hover camera id (0x82C339B8, a be32 each) and the confirmed camera id (0x82C33A50).
std::vector<Patch> Alpha152Patches() {
  constexpr uint32_t kAlpha = 0x10;
  return {
      {0x82F5F2A0,
       {
           0x10, 0x01, 0x01, 0x01, 0x00, 0x06, 0x00, 0x18, 0x00, 0x06, 0x00, 0x18, 0x00, 0x06,
           0x00, 0x32, 0x00, 0x06, 0x00, 0x32, 0x00, 0x06, 0x00, 0xB9, 0x00, 0x06, 0x00, 0xB9,
           0x00, 0x06, 0x00, 0x61, 0x00, 0x06, 0x00, 0x49, 0x00, 0x00, 0x00, 0x00,
       }},
      {0x821B52A7, {0x01}},
      {0x82C338F0 + 8 * kAlpha, {0xFF, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
      {0x82C339B8 + 4 * kAlpha, {0x00, 0x00, 0x00, 0xF8}},
      {0x82C33A50 + 4 * kAlpha, {0x00, 0x00, 0x01, 0x0F}},
  };
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
  const costume_lists::Tables now = costume_lists::Read(guest, c);
  const uint32_t added = uint32_t(addition.models.size());
  costume_lists::FillMoved(guest, now, addition.models, list_block, variants_block, kCostumeBlockSize);
  patches->push_back({kCostumeList + 4 * c, Be32Bytes(list_block)});
  patches->push_back({kCostumeVariants + 4 * c, Be32Bytes(variants_block)});
  patches->push_back({kCostumeLength + c, {uint8_t(now.length + added)}});
  patches->push_back({kCostumeMax + c, {uint8_t(now.maximum + added)}});
  return true;
}

// A costume box's state as AddCostumePatches lays it out: four patches in a row, from `at`.
costume_lists::Tables StateFrom(const std::vector<std::vector<uint8_t>>& words) {
  const auto be32 = [](const std::vector<uint8_t>& b) {
    return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | uint32_t(b[3]);
  };
  return {be32(words[0]), be32(words[1]), words[2][0], words[3][0]};
}

std::vector<std::vector<uint8_t>> WordsOf(const costume_lists::Tables& t) {
  return {Be32Bytes(t.list), Be32Bytes(t.variants), {uint8_t(t.length)}, {uint8_t(t.maximum)}};
}

}  // namespace

// Alpha-152 (charId 0x10) was removed on 2026-10-28 and came back on 2026-11-12: the crash on confirm of that day was the
// relocation defect fixed on 2026-10-30 (the low guest memory reservation, doa4_lowmem.h), and with it she plays through
// whole matches (notes/research/alpha152_runtime_experiment_2026-11-12.md). Her select model: doa4_alpha_select.h.
const std::array<Tweak, 6>& AllTweaks() {
  static const std::array<Tweak, 6> tweaks = {{
      {"doa4_tag_all_stages", "All stages in Tag battle", TagAllStagesPatches(), {}},
      {"doa4_mp_hidden_characters", "Hidden characters in multiplayer select",
       MpHiddenCharacterPatches(), {}},
      // The models the Xenia Rev 1 patches put on slots 1 and 2 (ryu_highabusa), as extra costumes instead: Helena m3
      // (hel03 + helAD), Kokoro m8 (pink) and m3 (blue), Christie m5. On the disc; no costume list uses them.
      {"doa4_helena_opera", "Helena's opera dress", {}, {{0x07, {0x03}}}},
      {"doa4_kokoro_kimonos", "Kokoro's pink and blue kimonos", {}, {{0x0A, {0x08, 0x03}}}},
      {"doa4_christie_costume", "Christie's cutscene costume", {}, {{0x14, {0x05}}}},
      {"doa4_alpha152", "Alpha-152 playable", Alpha152Patches(), {}, true},
  }};
  return tweaks;
}

const Patch& Alpha152RandomCell() {
  static const Patch cell = {kSelectLastCell,
                             {
                                 0xFF, 0x01, 0x01, 0x01, 0x00, 0x06, 0x00, 0x11, 0x00, 0x06, 0x00, 0x11, 0x00, 0x06,
                                 0x00, 0x11, 0x00, 0x06, 0x00, 0x11, 0x00, 0x06, 0x00, 0xB8, 0x00, 0x06, 0x00, 0xB8,
                                 0x00, 0x06, 0x00, 0x3E, 0x00, 0x06, 0x00, 0x6E, 0x00, 0x00, 0x00, 0x00,
                             }};
  return cell;
}

size_t FillAlpha152RandomCell(const GuestBytes& guest, bool applied_at_load) {
  if (!applied_at_load) return 0;
  const Patch& cell = Alpha152RandomCell();
  for (size_t i = 0; i < cell.bytes.size(); ++i) guest.Write(cell.address + uint32_t(i), cell.bytes[i]);
  return cell.bytes.size();
}

bool IsReadOnly(uint32_t address) {
  return (address >= 0x82000000 && address < 0x822F0000) ||
         (address >= 0x835B0000 && address < 0x83600000);
}

void TweakPatcher::CaptureOriginals(const GuestBytes& guest, const AllocFn& alloc) {
  if (captured()) return;  // later calls would capture patched bytes
  patches_.clear();
  unavailable_.assign(AllTweaks().size(), false);
  refuse_on_.assign(AllTweaks().size(), std::string());
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
  if (load_ended_ && AllTweaks()[tweak].at_load_only) return "applies at the next start";
  if (unavailable_[tweak]) return "no guest memory for this costume";
  if (on && !refuse_on_[tweak].empty()) return refuse_on_[tweak];
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

std::string TweakPatcher::CarrySuits(const GuestBytes& guest, const AllocFn& alloc, uint32_t character,
                                     const std::vector<uint8_t>& models) {
  if (!captured()) return "the original bytes were never recorded";
  for (size_t t = 0; t < patches_.size(); ++t) {
    std::vector<Patch>& patches = patches_[t];
    size_t at = patches.size();
    for (size_t p = 0; p + 3 < patches.size(); ++p) {
      if (patches[p].address == kCostumeList + 4 * character) at = p;
    }
    if (at == patches.size()) continue;  // not this character's box
    if (unavailable_[t]) return {};      // it never had its blocks: it stays refused, and the suits stay
    std::vector<std::vector<uint8_t>> on_words, off_words;
    for (size_t p = at; p < at + 4; ++p) {
      on_words.push_back(patches[p].bytes);
      off_words.push_back(originals_[t][p]);
    }
    const costume_lists::Tables box_on = StateFrom(on_words), box_off = StateFrom(off_words);
    const costume_lists::Tables now = costume_lists::Read(guest, character);
    const uint32_t k = uint32_t(models.size());
    // Which state costumes::Apply just built on: the other one gets its own copy with the suits appended.
    const bool is_on = now.maximum == box_on.maximum + k;
    if (!is_on && now.maximum != box_off.maximum + k) {
      return std::format("charId {}: her costume count {} is neither her box's state plus {} suits", character, now.maximum, k);
    }
    // Always these two blocks, whichever state the box is in, so the guest heap does not depend on the box.
    const uint32_t list_block = alloc(costume_lists::kFullBlock);
    const uint32_t variants_block = list_block ? alloc(costume_lists::kFullBlock) : 0;
    if (!variants_block) {
      unavailable_[t] = true;
      return std::format("charId {}: no guest memory for her box to keep her suits; the box cannot be changed", character);
    }
    const costume_lists::Tables& other_from = is_on ? box_off : box_on;
    costume_lists::FillMoved(guest, other_from, models, list_block, variants_block, costume_lists::kFullBlock);
    const costume_lists::Tables other{list_block, variants_block, other_from.length + k, other_from.maximum + k};
    const costume_lists::Tables with_on = is_on ? now : other;
    const costume_lists::Tables with_off = is_on ? other : now;
    if (with_on.maximum > costume_lists::kCostumeLimit) {
      refuse_on_[t] = std::format("no room for this box beside her imported suits (at most {} costumes); it applies at the "
                                  "next start, in place of her last suits",
                                  costume_lists::kCostumeLimit);
    }
    const std::vector<std::vector<uint8_t>> on_after = WordsOf(with_on), off_after = WordsOf(with_off);
    for (size_t p = 0; p < 4; ++p) {
      patches[at + p].bytes = on_after[p];
      originals_[t][at + p] = off_after[p];
    }
    return {};
  }
  return {};
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
