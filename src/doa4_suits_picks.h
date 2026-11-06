// The 34 picks, compiled in: what a session's costume tables hold, so a PC with no suit files builds the same bytes as a PC
// with all of them (doa4_suits.h, "A session"). Design names and numbers only; no game data.
//
// One row per suit, in costume order per character: charId; her empty model record to fill (own) or kAppend for a record
// appended past her block; the placeholder file pair the suit replaces; her head part's file pair (kNoPart: Tina's head is in
// the body); the DOAX2 design and colour. The same fields as a manifest line without its file sizes and hashes. Kasumi's
// vks_on_o is an append because her model 9 draws nothing. suits_host_test compares this table with a real manifest when
// DOA4_SUITS_MANIFEST_REAL names one.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "doa4_suits_manifest.h"

namespace doa4::suits {

namespace picks {

inline constexpr int kAppend = -1;

struct Row {
  uint8_t character;
  int own_model;  // kAppend: a record appended past her block
  uint16_t body_tpr, body_cat;
  uint16_t part_tpr, part_cat;
  const char* design;
  int colour;
};

inline constexpr Row kRows[] = {
    // Kasumi
    {5, 7, 269, 270, 509, 510, "vks_ch_d", 0},
    {5, 8, 271, 272, 509, 510, "vks_cs_a", 0},
    {5, kAppend, 273, 274, 509, 510, "vks_on_o", 0},
    {5, kAppend, 349, 350, 509, 510, "vks_hn_i", 0},
    // Ayane
    {13, kAppend, 275, 276, 543, 544, "vay_cs_b", 0},
    {13, kAppend, 211, 212, 543, 544, "vay_on_k", 0},
    {13, kAppend, 235, 236, 543, 544, "vay_sk_d", 0},
    // Hitomi
    {21, 4, 481, 482, 559, 560, "vht_cs_f", 0},
    {21, kAppend, 347, 348, 559, 560, "vht_sp_h", 0},
    {21, kAppend, 345, 346, 559, 560, "vht_on_e", 0},
    {21, kAppend, 245, 246, 559, 560, "vks_wi_b", 0},
    // Kokoro
    {10, 2, 321, 322, 569, 570, "vco_cs_i", 0},
    {10, kAppend, 319, 320, 569, 570, "vco_sk_c", 0},
    {10, 9, 335, 336, 569, 570, "vhe_on_c", 0},
    {10, 13, 343, 344, 569, 570, "vco_ch_f", 0},
    // Helena
    {7, 4, 295, 296, 591, 592, "vhe_cm_a", 0},
    {7, kAppend, 285, 286, 591, 592, "vhe_on_d", 0},
    {7, kAppend, 365, 366, 591, 592, "vhe_sk_a", 0},
    {7, kAppend, 383, 384, 591, 592, "vhe_dl_l", 0},
    // Lisa
    {15, kAppend, 355, 356, 545, 546, "vls_cs_h", 0},
    {15, kAppend, 353, 354, 545, 546, "vls_on_j", 1},
    {15, kAppend, 351, 352, 545, 546, "vls_sp_a", 0},
    // Christie
    {20, 2, 463, 464, 557, 558, "vcr_cs_g", 0},
    {20, 3, 465, 466, 557, 558, "vcr_on_n", 0},
    {20, kAppend, 385, 386, 557, 558, "vcr_ch_e", 0},
    {20, kAppend, 407, 408, 557, 558, "vcr_cm_c", 0},
    // Leifang
    {12, 7, 381, 382, 517, 518, "vle_cs_c", 0},
    {12, 0, 367, 368, 517, 518, "vle_on_i", 2},
    {12, 3, 373, 374, 517, 518, "vle_sk_h", 0},
    {12, 4, 375, 376, 517, 518, "vle_dl_a", 0},
    // Tina: the head is in the body
    {1, 5, 225, 226, kNoPart, kNoPart, "vtn_cs_e", 0},
    {1, kAppend, 431, 432, kNoPart, kNoPart, "vtn_ch_i", 0},
    {1, kAppend, 449, 450, kNoPart, kNoPart, "vtn_sh_c", 0},
    {1, kAppend, 451, 452, kNoPart, kNoPart, "vls_on_b", 1},
};

}  // namespace picks

// The table as suits: file names as the importer writes them, sizes and hashes unknown (0 and empty, which every budget allows).
inline std::vector<Suit> PickSuits() {
  std::vector<Suit> out;
  for (const picks::Row& r : picks::kRows) {
    Suit s;
    s.character = r.character;
    s.own_model = r.own_model;
    s.body_tpr = r.body_tpr;
    s.body_cat = r.body_cat;
    s.part_tpr = r.part_tpr;
    s.part_cat = r.part_cat;
    s.design = r.design;
    s.colour = r.colour;
    const std::string stem = s.design + "_" + std::to_string(s.colour);
    s.tpr.name = stem + ".tpr";
    s.cat.name = stem + ".cat";
    out.push_back(std::move(s));
  }
  return out;
}

// Whether a manifest's suit is that pick: every number and name the tables use, and the file names. Sizes and hashes are the
// import's own.
inline bool IsPick(const Suit& s) {
  for (const Suit& p : PickSuits()) {
    if (s.design == p.design && s.colour == p.colour && s.character == p.character && s.own_model == p.own_model &&
        s.body_tpr == p.body_tpr && s.body_cat == p.body_cat && s.part_tpr == p.part_tpr && s.part_cat == p.part_cat &&
        s.tpr.name == p.tpr.name && s.cat.name == p.cat.name) {
      return true;
    }
  }
  return false;
}

}  // namespace doa4::suits
