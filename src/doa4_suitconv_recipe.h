// Suit converter (Plan 2): the recipe tables. What to convert (the 34 swimsuits, in picks order), from which archive
// entries (DOAX2 disc, DOA4 game files), with which checksums, budgets and options. Data only: every number here is
// copied from the plan (its per-girl table, Appendix A, and the options paragraph) and from the Python converter's
// outputs (physics_spike/imports/all34_m1/manifest.txt for the output sizes and SHA-1s, recipe_m1/bust_match.json for
// the bust strengths, written as the shortest decimal that round-trips, Python's repr). No game bytes. Pure C++.
//
// "Entry" means the AFS entry index; a .cat is the entry after its .tpr. The Python file names are in comments in
// doa4_suitconv_recipe.cpp.

#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace doa4::suitconv {

// The converter's version word as the Python writes it into the manifest ("converter proto-1d").
inline constexpr std::string_view kConverterWord = "proto-1d";

// A cooperative cancel flag, shared by the caller and the converter's worker threads.
class CancelToken {
 public:
  void Cancel() { flag_.store(true, std::memory_order_relaxed); }
  bool cancelled() const { return flag_.load(std::memory_order_relaxed); }

 private:
  std::atomic<bool> flag_{false};
};

// The archives the converter reads. chr, werone and wertwo are the DOAX2 disc's; chrone, chrtwo and chrthree are the
// DOA4 game files'.
enum class Arc : uint8_t { kChr, kWerone, kWertwo, kChrone, kChrtwo, kChrthree };

inline constexpr const char* ArcName(Arc a) {
  switch (a) {
    case Arc::kChr: return "chr";
    case Arc::kWerone: return "werone";
    case Arc::kWertwo: return "wertwo";
    case Arc::kChrone: return "chrone";
    case Arc::kChrtwo: return "chrtwo";
    case Arc::kChrthree: return "chrthree";
  }
  return "?";
}
inline constexpr const char* ArcFileName(Arc a) {
  switch (a) {
    case Arc::kChr: return "chr.afs";
    case Arc::kWerone: return "werone.afs";
    case Arc::kWertwo: return "wertwo.afs";
    case Arc::kChrone: return "chrone.afs";
    case Arc::kChrtwo: return "chrtwo.afs";
    case Arc::kChrthree: return "chrthree.afs";
  }
  return "?";
}
inline constexpr bool ArcIsDisc(Arc a) { return a == Arc::kChr || a == Arc::kWerone || a == Arc::kWertwo; }

// One archive entry the converter reads: its place, its exact size and its SHA-1 (40 lowercase hex digits).
struct EntryRef {
  Arc arc;
  uint32_t index;
  uint64_t size;
  const char* sha1;
};

// The stable label of an entry in reports: "<arc> <index>", e.g. "werone 283".
std::string EntryLabel(const EntryRef& e);

// One girl's recipe (the plan's per-girl table). `character` is her charId in the picks table (doa4_suits_picks.h).
// body_index is her DOAX2 body .tpr in chr.afs (the .cat follows). ref_* is the reference model and hand_* the hand
// donor (the same entry except Tina's hands). budget_*: a converted file larger than its budget fails R7.
struct GirlRecipe {
  const char* girl;
  uint8_t character;
  uint32_t body_index;
  EntryRef ref_tpr, ref_cat, hand_tpr, hand_cat;
  uint64_t budget_tpr, budget_cat;
  bool leg_midline, carry_head, head_physics, hair_spares;
};

// One suit: DOAX2 design and colour, the girl who wears it (the picks table's character, which is not always the
// design's prefix: vks_wi_b is Hitomi's), the DOAX2 archive and .tpr entry, the per-design options, and the checksums
// of the two files the Python converter writes.
struct SuitRecipe {
  const char* design;
  int colour;
  const char* girl;
  Arc arc;
  uint32_t tpr_index;
  double bust_down, bust_up;
  bool port_cloth, gen_cloth, fit_leg_colliders, unstick_cloth;
  double back_margin, sink_hamstrings, strap_fade;
  uint64_t out_tpr_size;
  const char* out_tpr_sha1;
  uint64_t out_cat_size;
  const char* out_cat_sha1;
};

std::span<const GirlRecipe> Girls();         // 9, in the plan's table order
std::span<const SuitRecipe> Suits();         // 34, picks order
std::span<const EntryRef> DiscEntries();     // 86: 18 bodies, then the 34 suits' .tpr and .cat
std::span<const EntryRef> GameEntries();     // 21

EntryRef WaistTemplate(bool tpr);  // DOA4 chrtwo 20 / 21 (hel05)
EntryRef GenDonor();               // DOAX2 werone 283: vhe_sk_a's own .cat, the generated cloth's donor
EntryRef GenBend();                // DOA4 chrthree 91 (rtm01.cat): the bend limit block

}  // namespace doa4::suitconv
