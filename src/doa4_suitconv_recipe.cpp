// Suit converter (Plan 2): the recipe tables. See doa4_suitconv_recipe.h. The rows were generated from the plan's
// Appendix A and per-girl table, from physics_spike/imports/all34_m1/manifest.txt (its SHA-1 is
// dc0d0f8d77d2aad6b9d44372508a7c4ab0a184b8) and from recipe_m1/bust_match.json, and are checked against them again by
// tests/host/suitconv_inputs_host_test.cpp (DOA4_SUITCONV_ORACLE_DIR) and against the picks table
// (doa4_suits_picks.h). Python's file names are in comments.

#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_recipe.h"

#include <array>

namespace doa4::suitconv {

std::string EntryLabel(const EntryRef& e) { return std::string(ArcName(e.arc)) + " " + std::to_string(e.index); }

namespace {

// DOA4 game files: archive, entry, size, SHA-1.
constexpr EntryRef kGameEntries[] = {
    {ArcId::kChrone, 51, 8574976, "4a269fd7203f6cf0be1e4ff78ad938f57ae6c88d"},  // kas00.tpr
    {ArcId::kChrone, 52, 840240, "5632438e49986549d1e8b54e06ba203b251234a4"},  // kas00.cat
    {ArcId::kChrone, 11, 14624768, "d8dce130e6381f9e4d8d31cb2cd0c019ee931319"},  // tin00.tpr
    {ArcId::kChrone, 12, 1053392, "bf6b9f17ab556c5feee87a11284ae89bf190a590"},  // tin00.cat
    {ArcId::kChrone, 13, 15509504, "4c377f1e74c0411bb85267b82aa74c681fb07b27"},  // tin01.tpr
    {ArcId::kChrone, 14, 1056320, "5b820422d0559dbb24563cfaf566a845ac019727"},  // tin01.cat
    {ArcId::kChrtwo, 20, 15628288, "4f1826ed62453c616c6ccb6abb6d5c8e017a45b3"},  // hel05.tpr
    {ArcId::kChrtwo, 21, 706208, "6a2e11883d7acbdf1fc9c4d2931f1b22ff14fef4"},  // hel05.cat
    {ArcId::kChrtwo, 40, 7481344, "dde5c97bd82900ab8aa1b6bb9af615b99e1eaf7c"},  // hak00.tpr
    {ArcId::kChrtwo, 41, 1129760, "5619b013e65fcaf721180232fbbcb18f96f6493c"},  // hak00.cat
    {ArcId::kChrtwo, 102, 5240832, "2455e7474f14fc76dd957528830ed32552982ef9"},  // lei06.tpr
    {ArcId::kChrtwo, 103, 740736, "391df6b6242f35114fc84d217296c47a380ea8b3"},  // lei06.cat
    {ArcId::kChrtwo, 116, 6625280, "43e8ed7c4e27303e70b7b768a2ce6c4cc97497fc"},  // aya00.tpr
    {ArcId::kChrtwo, 117, 704096, "bd684cd14799e2f29bce2c26e45756b305ee379b"},  // aya00.cat
    {ArcId::kChrthree, 10, 7575552, "d47be31cab77a819982c22760354fae6df46af2f"},  // lis00.tpr
    {ArcId::kChrthree, 11, 892880, "0e4756968dd5a38761220689f387b3908c57660a"},  // lis00.cat
    {ArcId::kChrthree, 54, 10745856, "b5c881d02362aba9f623b296a56e33b45c89365f"},  // cri00.tpr
    {ArcId::kChrthree, 55, 668384, "a72cc2ab91d36907439a9d87590a206415abd54e"},  // cri00.cat
    {ArcId::kChrthree, 72, 10311680, "6a6711bcc58a209c4dfdf5ac7a8292da087f7024"},  // kor02.tpr
    {ArcId::kChrthree, 73, 850832, "cc96e226f8930173a169e80865ad256198eaede5"},  // kor02.cat
    {ArcId::kChrthree, 91, 650768, "00b02a74e4c93171b681e23daad29d9c5b2ad1c8"},  // rtm01.cat
};
static_assert(std::size(kGameEntries) == 21);

// DOAX2 disc: the 9 girls' bodies (18), then the 34 suits' .tpr and .cat (68), picks order.
constexpr EntryRef kDiscEntries[] = {
    {ArcId::kChr, 0, 5138432, "6869caa37da1d94233e5aa18738d7fb3ace84361"},  // vtn1.tpr
    {ArcId::kChr, 1, 604, "e7c4bdfa1676385c1a21189fc26a1c3a795ebdbd"},  // vtn1.cat
    {ArcId::kChr, 2, 5138432, "a71b3bba0ac0aef53d30e2ba8182b9aeefc0cb6f"},  // vks1.tpr
    {ArcId::kChr, 3, 668, "63869f6eb61fd96b0d3ead691b58cf6fabd9c781"},  // vks1.cat
    {ArcId::kChr, 4, 5126144, "2635633097f17f32a2d851d4d3c0cb10f9cb59a3"},  // vhe1.tpr
    {ArcId::kChr, 5, 668, "91bd997aefcd43c07736928fdda8044bdffb5d85"},  // vhe1.cat
    {ArcId::kChr, 6, 5105664, "b4f46aec2192491dfd316442a60312e907c56813"},  // vco1.tpr
    {ArcId::kChr, 7, 804, "f9a1b60a1c48f7b4efcb3792639e5bbee11fa71d"},  // vco1.cat
    {ArcId::kChr, 8, 5130240, "9ff4feda42616ac5a6ae80ab4e0b312c30935896"},  // vle1.tpr
    {ArcId::kChr, 9, 740, "aab929254570a19c6dc776890e2e4ea71d16bebb"},  // vle1.cat
    {ArcId::kChr, 10, 5142528, "d59c9935909b8aee9358e3b8f7d7aa0af62f93f6"},  // vay1.tpr
    {ArcId::kChr, 11, 604, "07a9a80875164e8895cb3869809468272284506c"},  // vay1.cat
    {ArcId::kChr, 12, 5122048, "1ff0748b0a025bdff2c854247a2c0a8edc1a1c07"},  // vls1.tpr
    {ArcId::kChr, 13, 604, "eb8e9b1044bc3732a62dc6371b0f7101622393d4"},  // vls1.cat
    {ArcId::kChr, 14, 5122048, "0075d9b08184b185e527950aafde8471820261d4"},  // vcr1.tpr
    {ArcId::kChr, 15, 860, "75bb2ae29b372e701f6ec716cef77251de7230a8"},  // vcr1.cat
    {ArcId::kChr, 16, 5105664, "dc71c50e1b25ede83ee948a2e62b9efc9e10f34d"},  // vht1.tpr
    {ArcId::kChr, 17, 740, "71124ad920790a57266f87409a999eb0a4b1bdd9"},  // vht1.cat
    {ArcId::kWertwo, 26, 6793216, "71323f2e978cb5e1afa9cc306f60c0c3c8ae438a"},  // vks_ch_d
    {ArcId::kWertwo, 27, 328004, "a048e3814fec7bcbbe53e6b63945c94ba0dfe6c2"},
    {ArcId::kWertwo, 40, 2324480, "e8bfb9b3a24f6527867035b7d3cc23d416c3f718"},  // vks_cs_a
    {ArcId::kWertwo, 41, 75876, "632f1990c62508fbc3f884b8cf53b615404dc17f"},
    {ArcId::kWertwo, 70, 8001536, "eb61ae159c3ff4c6f50b2f287e5d46740cbdcace"},  // vks_on_o
    {ArcId::kWertwo, 71, 343184, "5feffee87a48ce220c0a87c3ec2cbf3d53388d06"},
    {ArcId::kWertwo, 64, 2004992, "6ca74b331fa8f9fc9994dc94482d9e5be06acb8d"},  // vks_hn_i
    {ArcId::kWertwo, 65, 75428, "053d36c88bd4f580caa73b955307de46d13e52cd"},
    {ArcId::kWerone, 14, 1878016, "b29ccd5c8b31f0ca5e1617fd459339e924fb3bb1"},  // vay_cs_b
    {ArcId::kWerone, 15, 74836, "e0c02b5753b2550fdec028b27563db1b7659f248"},
    {ArcId::kWerone, 50, 7256064, "586bb8473cac3f4ad63feae598fb4d3c0c5f6a1b"},  // vay_on_k
    {ArcId::kWerone, 51, 328164, "d98dbeba401aee82bf65b7fde8f760517322f04b"},
    {ArcId::kWerone, 56, 7559168, "c65a8f29969742af04ab8b72dc42a03ce463cff1"},  // vay_sk_d
    {ArcId::kWerone, 57, 352824, "f87ba3b7ab078cfc5b0f9c26be64e9510546d0e6"},
    {ArcId::kWerone, 300, 1898496, "4dfac3c0f4785b775605d6d45ab49b8f07e8eae6"},  // vht_cs_f
    {ArcId::kWerone, 301, 67684, "3abb60baaf20d6475230b73b9866d5cafcf61cc3"},
    {ArcId::kWertwo, 20, 5709824, "3a10459ce8c16ead1de8b21fbbd966718a6d09f5"},  // vht_sp_h
    {ArcId::kWertwo, 21, 387988, "d49d4b08986c8daa6677df9760788dfe3c039915"},
    {ArcId::kWerone, 314, 8071168, "d6e8f1224f91237d2f1371268b63fd34774bca75"},  // vht_on_e
    {ArcId::kWerone, 315, 328076, "8479dbd22810ca5e420f13676625eb084a554866"},
    {ArcId::kWertwo, 106, 7356416, "4ece8f5128ab6189dce794544a4bc541ca90d2ec"},  // vks_wi_b
    {ArcId::kWertwo, 107, 328108, "2b9fd1a8e15038cd2a959aff201c39ba473838a3"},
    {ArcId::kWerone, 104, 2041856, "3aaf120908c3ca2e3f9d18e8731fdc7cbea719fc"},  // vco_cs_i
    {ArcId::kWerone, 105, 80636, "0c6d912498e68c7dab475051fced29d9c0ebd7cc"},
    {ArcId::kWerone, 148, 8110080, "35ff967dfe1a24e6c7b94e15d8cbc4e12983a54f"},  // vco_sk_c
    {ArcId::kWerone, 149, 951616, "1d30a9b7b2f7c10473b6837b4d612fb357de1b01"},
    {ArcId::kWerone, 256, 7483392, "718e2cb917869ad5907280bfe885dfc9a478be49"},  // vhe_on_c
    {ArcId::kWerone, 257, 399388, "8eaf3c195ffc787e8ac320fb1e8f20e5f93141db"},
    {ArcId::kWerone, 98, 5888000, "6bbdf26aa51e250550c7f8ec4134e3cc708ef8a6"},  // vco_ch_f
    {ArcId::kWerone, 99, 513788, "e6ee6a1347bb5e8eaf32aa34b9725f3aae5c2745"},
    {ArcId::kWerone, 230, 6137856, "529903dc435f269cc1f7bc2aa808d687d4bde96f"},  // vhe_cm_a
    {ArcId::kWerone, 231, 328076, "2de8664b67bbb9b9f9676c337a78537d44f31fe2"},
    {ArcId::kWerone, 264, 6207488, "faa817610cee1cdd5aefcdb58beec1da36b2e777"},  // vhe_on_d
    {ArcId::kWerone, 265, 327996, "bae355dceadd33e35236717cc4c848741825b124"},
    {ArcId::kWerone, 282, 6502400, "6316d21bfaa5125240b60107c66ec5a7c15d010d"},  // vhe_sk_a
    {ArcId::kWerone, 283, 951312, "3e72583a2644dd21e60d2e54660f69220b1d395c"},  // also the generated cloth's donor
    {ArcId::kWerone, 244, 7682048, "ea32c4c219108093c4529e06b58be9a0b3fcf46e"},  // vhe_dl_l
    {ArcId::kWerone, 245, 491720, "960f09118cadf501a3057f2e5100feb81fa48826"},
    {ArcId::kWertwo, 220, 1943552, "66de500712ffd31cd6ae34bdc684343beb2c8c1a"},  // vls_cs_h
    {ArcId::kWertwo, 221, 76084, "b7030f790081398f5657dcaaf97272fc8946f8a8"},
    {ArcId::kWertwo, 254, 3725312, "61059b53dd740c4cd33c481bffecab6365f70d4f"},  // vls_on_j c1
    {ArcId::kWertwo, 255, 328028, "e7cbfd8fee37fe5803116706643b60c1ad33672f"},
    {ArcId::kWertwo, 258, 7809024, "06ae4f43dce1a8441017c0ad767c67319cc20b86"},  // vls_sp_a
    {ArcId::kWertwo, 259, 328052, "07708004f8476016e2d29a6dbd939de72133009f"},
    {ArcId::kWerone, 184, 1562624, "15792576511e0cd5606c619590f50ec38bf0ebad"},  // vcr_cs_g
    {ArcId::kWerone, 185, 73996, "d2260e094e42f2b2fd6fadc316fbc86969bbcefb"},
    {ArcId::kWerone, 206, 5175296, "646983a8c7eee60a1d8a613bc991aa564be926b5"},  // vcr_on_n
    {ArcId::kWerone, 207, 328060, "0ce8e7c9b9c1f17b27fd73ae53a716e0f09d578e"},
    {ArcId::kWerone, 172, 5613568, "71f346d369d035842024ec7900e9a20cffd27951"},  // vcr_ch_e
    {ArcId::kWerone, 173, 327908, "f253e57cd401b658628ed6f91526526547fffc72"},
    {ArcId::kWerone, 178, 6187008, "2ef1b27e2f4c34aa983f25b65bb26c3a989e6dc1"},  // vcr_cm_c
    {ArcId::kWerone, 179, 687936, "e569d157117362926ccbbcdec743dd3614cd8f08"},
    {ArcId::kWertwo, 120, 1974272, "741c34da4598cd3f7f5449130089adacde384e96"},  // vle_cs_c
    {ArcId::kWertwo, 121, 65700, "cb16dd8d33ff2433788993ffadc5193c0a5ac72d"},
    {ArcId::kWertwo, 168, 3774464, "14cccbccb6015829723a095929b14f0f0d53ac23"},  // vle_on_i c2
    {ArcId::kWertwo, 169, 201600, "0a8358ec0c20e8cd439af99f4a40669a075b75d8"},
    {ArcId::kWertwo, 202, 8241152, "923cc2670dfb8557cd30f709c7d94f45c436e69e"},  // vle_sk_h
    {ArcId::kWertwo, 203, 381904, "ab9bb997153df18f1b58e28645f4966b6fc2cfc7"},
    {ArcId::kWertwo, 122, 7329792, "5d71292c35ce0a7494a811c3817422264d6057ee"},  // vle_dl_a
    {ArcId::kWertwo, 123, 641236, "39c93dfa427d20f1379a2d979263f2ab5c4a2db9"},
    {ArcId::kWertwo, 290, 1701888, "17ce10081be11caf6f866f39a0fc5f701b648c8a"},  // vtn_cs_e
    {ArcId::kWertwo, 291, 98596, "3412450e9b719264204d295f4081ff651a27d6eb"},
    {ArcId::kWertwo, 284, 5564416, "c2ac56726fa17d7b00a0d382b677c607fa13347e"},  // vtn_ch_i
    {ArcId::kWertwo, 285, 400708, "2e2265edb5eb2cf5000f9ac3e9dbf7c29e0dd4f8"},
    {ArcId::kWertwo, 320, 7407616, "406eb8ccd5d4434c47e5d089bc58433c7b6e25c6"},  // vtn_sh_c
    {ArcId::kWertwo, 321, 730208, "daa09ca50e2341296ba7a81c45d87724303c1cbe"},
    {ArcId::kWertwo, 242, 6600704, "7cc3928e9544c06db5952c61b327d41287adfc23"},  // vls_on_b c1
    {ArcId::kWertwo, 243, 348432, "6b18080338dce6d8a45579f9b80f0e68051c1440"},
};
static_assert(std::size(kDiscEntries) == 86);

// The plan's per-girl table. Reference = hand donor except Tina's hands.
constexpr GirlRecipe kGirls[] = {
    {"vks", 5, 2,
     {ArcId::kChrone, 51, 8574976, "4a269fd7203f6cf0be1e4ff78ad938f57ae6c88d"}, {ArcId::kChrone, 52, 840240, "5632438e49986549d1e8b54e06ba203b251234a4"},
     {ArcId::kChrone, 51, 8574976, "4a269fd7203f6cf0be1e4ff78ad938f57ae6c88d"}, {ArcId::kChrone, 52, 840240, "5632438e49986549d1e8b54e06ba203b251234a4"},
     0xA00000, 0x200000, false, false, false, true},
    {"vay", 13, 10,
     {ArcId::kChrtwo, 116, 6625280, "43e8ed7c4e27303e70b7b768a2ce6c4cc97497fc"}, {ArcId::kChrtwo, 117, 704096, "bd684cd14799e2f29bce2c26e45756b305ee379b"},
     {ArcId::kChrtwo, 116, 6625280, "43e8ed7c4e27303e70b7b768a2ce6c4cc97497fc"}, {ArcId::kChrtwo, 117, 704096, "bd684cd14799e2f29bce2c26e45756b305ee379b"},
     0x900000, 0x200000, false, false, false, false},
    {"vht", 21, 16,
     {ArcId::kChrthree, 72, 10311680, "6a6711bcc58a209c4dfdf5ac7a8292da087f7024"}, {ArcId::kChrthree, 73, 850832, "cc96e226f8930173a169e80865ad256198eaede5"},
     {ArcId::kChrthree, 72, 10311680, "6a6711bcc58a209c4dfdf5ac7a8292da087f7024"}, {ArcId::kChrthree, 73, 850832, "cc96e226f8930173a169e80865ad256198eaede5"},
     0xC00000, 0x200000, false, false, false, false},
    {"vco", 10, 6,
     {ArcId::kChrtwo, 40, 7481344, "dde5c97bd82900ab8aa1b6bb9af615b99e1eaf7c"}, {ArcId::kChrtwo, 41, 1129760, "5619b013e65fcaf721180232fbbcb18f96f6493c"},
     {ArcId::kChrtwo, 40, 7481344, "dde5c97bd82900ab8aa1b6bb9af615b99e1eaf7c"}, {ArcId::kChrtwo, 41, 1129760, "5619b013e65fcaf721180232fbbcb18f96f6493c"},
     0xCB0000, 0x300000, false, false, false, true},
    {"vhe", 7, 4,
     {ArcId::kChrtwo, 20, 15628288, "4f1826ed62453c616c6ccb6abb6d5c8e017a45b3"}, {ArcId::kChrtwo, 21, 706208, "6a2e11883d7acbdf1fc9c4d2931f1b22ff14fef4"},
     {ArcId::kChrtwo, 20, 15628288, "4f1826ed62453c616c6ccb6abb6d5c8e017a45b3"}, {ArcId::kChrtwo, 21, 706208, "6a2e11883d7acbdf1fc9c4d2931f1b22ff14fef4"},
     0xFA0000, 0x200000, false, false, false, false},
    {"vls", 15, 12,
     {ArcId::kChrthree, 10, 7575552, "d47be31cab77a819982c22760354fae6df46af2f"}, {ArcId::kChrthree, 11, 892880, "0e4756968dd5a38761220689f387b3908c57660a"},
     {ArcId::kChrthree, 10, 7575552, "d47be31cab77a819982c22760354fae6df46af2f"}, {ArcId::kChrthree, 11, 892880, "0e4756968dd5a38761220689f387b3908c57660a"},
     0xC00000, 0x200000, false, false, false, false},
    {"vcr", 20, 14,
     {ArcId::kChrthree, 54, 10745856, "b5c881d02362aba9f623b296a56e33b45c89365f"}, {ArcId::kChrthree, 55, 668384, "a72cc2ab91d36907439a9d87590a206415abd54e"},
     {ArcId::kChrthree, 54, 10745856, "b5c881d02362aba9f623b296a56e33b45c89365f"}, {ArcId::kChrthree, 55, 668384, "a72cc2ab91d36907439a9d87590a206415abd54e"},
     0xC40000, 0x200000, false, false, false, false},
    {"vle", 12, 8,
     {ArcId::kChrtwo, 102, 5240832, "2455e7474f14fc76dd957528830ed32552982ef9"}, {ArcId::kChrtwo, 103, 740736, "391df6b6242f35114fc84d217296c47a380ea8b3"},
     {ArcId::kChrtwo, 102, 5240832, "2455e7474f14fc76dd957528830ed32552982ef9"}, {ArcId::kChrtwo, 103, 740736, "391df6b6242f35114fc84d217296c47a380ea8b3"},
     0xC00000, 0x200000, true, false, false, true},
    {"vtn", 1, 0,
     {ArcId::kChrone, 11, 14624768, "d8dce130e6381f9e4d8d31cb2cd0c019ee931319"}, {ArcId::kChrone, 12, 1053392, "bf6b9f17ab556c5feee87a11284ae89bf190a590"},
     {ArcId::kChrone, 13, 15509504, "4c377f1e74c0411bb85267b82aa74c681fb07b27"}, {ArcId::kChrone, 14, 1056320, "5b820422d0559dbb24563cfaf566a845ac019727"},
     0x1400000, 0x400000, false, true, true, false},
};
static_assert(std::size(kGirls) == 9);

// The 34 suits in picks order: design, colour, girl, DOAX2 archive, .tpr entry, bust strengths [down, up] from
// bust_match.json, the four cloth options, back margin, hamstring sink, strap fade, and the output .tpr and .cat
// size and SHA-1 from all34_m1/manifest.txt.
constexpr SuitRecipe kSuits[] = {
    {"vks_ch_d", 0, "vks", ArcId::kWertwo, 26, 0.562, 0.704, false, false, false, false, 0.0, 0.0, 0.0,
     8454144, "d9592c3d93d40660568b5c51bb4215c29c2ebb0b", 1032032, "95c9820707270260d00752860e76d5b5db519b2f"},
    {"vks_cs_a", 0, "vks", ArcId::kWertwo, 40, 0.561, 0.706, false, false, false, false, 0.0, 0.0, 0.0,
     8101888, "cbe07c008bf04ed0b0e8fb6eba8f027f0077422d", 1010704, "83210b94aa3092ccb125fdfba669c2b64cab8dad"},
    {"vks_on_o", 0, "vks", ArcId::kWertwo, 70, 0.564, 0.712, false, true, false, false, 0.0, 0.0, 0.0,
     8699904, "8791b1848b62651b364b62358fb39813671669c7", 1050448, "39996cccbaa5d1ed6a05fbb4d4ca0556217aa303"},
    {"vks_hn_i", 0, "vks", ArcId::kWertwo, 64, 0.564, 0.712, false, false, false, false, 0.0, 0.0, 0.0,
     7634944, "0c8d23ee20e7c46125a86d0a53a8d0501b930936", 977744, "980fba1ebd6834a5a39fdf72cdff98720e4bc90c"},
    {"vay_cs_b", 0, "vay", ArcId::kWerone, 14, 0.557, 0.422, false, false, false, false, 0.0, 0.0, 0.0,
     7704576, "ba10b150400155181b050ee4562c8b650ea82e88", 892112, "9dfc7c53b90fd5e400b3403c859f7a2541d6b83a"},
    {"vay_on_k", 0, "vay", ArcId::kWerone, 50, 0.552, 0.416, false, false, false, false, 0.0, 0.0, 0.0,
     8269824, "8e6c1d56426461e98ea6cfc19f47a60e2de9e17e", 877552, "daef67de9cede51e0ede53d81ab3e1adaa984872"},
    {"vay_sk_d", 0, "vay", ArcId::kWerone, 56, 0.563, 0.427, false, false, false, false, 0.0, 0.0, 0.0,
     7864320, "8be33248b1c7df42f70003ebd67458a52462866d", 853600, "41150f29dd04b9894b125fb6255af82e96c40d83"},
    {"vht_cs_f", 0, "vht", ArcId::kWerone, 300, 0.417, 0.343, false, false, false, false, 0.0, 0.0, 0.0,
     7417856, "384022a8a33a439d602c6f0c744d1dbc832525ae", 820880, "539d721e47bb65288bfbf411adff221ca9d91c3e"},
    {"vht_sp_h", 0, "vht", ArcId::kWertwo, 20, 0.418, 0.342, false, false, false, false, 0.0, 0.0, 0.0,
     7716864, "931a25595ed16f83f58ae23fcd39be59ffa815d4", 835760, "667f1b665e26dd5c653bcdd9d4067d95260cb385"},
    {"vht_on_e", 0, "vht", ArcId::kWerone, 314, 0.419, 0.344, false, false, false, false, 0.0, 0.0, 0.0,
     9031680, "628daf7e04d5fa59aafd4a9443b15cc0796d8466", 846864, "a252e240d159d539dd38700c276ed08f754ebeaa"},
    {"vks_wi_b", 0, "vht", ArcId::kWertwo, 106, 0.583, 0.575, false, false, false, false, 0.0, 0.0, 0.0,
     8794112, "9846a27ba3dfead01b060715fdb4e456308961a2", 897888, "e88f501d2e9ad4f2d48c042698ad7ba5d3867bf4"},
    {"vco_cs_i", 0, "vco", ArcId::kWerone, 104, 0.606, 0.408, false, false, false, false, 0.0, 0.0, 0.0,
     7704576, "4e6514cfee6b237949fc38dc6b6ecefe27279a36", 1058512, "7b4485e0b6d7f8e0e031211659b1a3fec6107203"},
    {"vco_sk_c", 0, "vco", ArcId::kWerone, 148, 0.601, 0.403, true, false, true, false, 0.0, 0.0, 0.0,
     8212480, "333e7e7b60960a62c59a5fdad44b5a4d727a26bc", 1231408, "f22f14b5e6857edbca718afbcbc7890d5b51bcf5"},
    {"vhe_on_c", 0, "vco", ArcId::kWerone, 256, 0.525, 0.425, false, false, false, false, 0.0, 0.0, 0.0,
     7901184, "5af713502c574c54c52a846b5b154c2a82429475", 1217440, "5e062ffa426c9ed1a5728f52872a1c1baf79eeb4"},
    {"vco_ch_f", 0, "vco", ArcId::kWerone, 98, 0.606, 0.407, false, false, false, false, 0.0, 0.0, 0.0,
     7901184, "9f17a67ed7c3193c384d8245e6bd78071781a543", 1108160, "168e10f2843006c6b2328786c6d367c49e6fb73f"},
    {"vhe_cm_a", 0, "vhe", ArcId::kWerone, 230, 0.748, 0.474, false, false, false, false, 0.0, 0.0, 0.0,
     7745536, "f902195193ac702fb7838ce729bf7dda0ae2548d", 787680, "b8354dca4e8098d918863916cbc1f922c7955c96"},
    {"vhe_on_d", 0, "vhe", ArcId::kWerone, 264, 0.749, 0.475, false, false, false, false, 0.0, 0.0, 0.0,
     8155136, "270d67c4c7231c31c8fb479f06ba4e89ac29bfeb", 779456, "4d857dbffb3a56b1716c2529f731740fa53d53be"},
    {"vhe_sk_a", 0, "vhe", ArcId::kWerone, 282, 0.75, 0.475, true, false, false, false, 0.0, 0.0, 0.0,
     7811072, "9eac160ee78eff392fe6be658e1508a5dfe8b3ff", 932480, "aa1b2f2fae339ded95945b42a20f5b58a6a3571b"},
    {"vhe_dl_l", 0, "vhe", ArcId::kWerone, 244, 0.745, 0.469, false, false, false, false, 0.0, 0.0, 0.0,
     7942144, "cb3320085793c9d2a62c56dbb19eacf8dbf8825a", 918336, "6b8fa8b69f95953581b7caaa4dbacda8baeddc8d"},
    {"vls_cs_h", 0, "vls", ArcId::kWertwo, 220, 1.0, 0.488, false, false, false, false, 0.0, 0.0, 0.0,
     7462912, "3abd8a7bf57d9c5433bd87553230649524dbbbd2", 980208, "01b322f45429128abe599bb5b6b5e872a96f18f0"},
    {"vls_on_j", 1, "vls", ArcId::kWertwo, 254, 1.0, 0.491, false, false, false, false, 0.0, 0.0, 0.0,
     6660096, "f6b14c0167b2055893adc7c9d3d73adb6f69a50e", 916256, "2c06877ae285c5fc01f042ff80d47125e3fc2994"},
    {"vls_sp_a", 0, "vls", ArcId::kWertwo, 258, 1.0, 0.487, false, false, false, false, 0.0, 0.0, 0.0,
     9646080, "eaca0f46ee1ec2726273f7ed3ba1d5702195da81", 1004848, "c409312ac14ef425446799ce0b36697289a14a86"},
    {"vcr_cs_g", 0, "vcr", ArcId::kWerone, 184, 0.873, 0.385, false, false, false, false, 0.0, 0.0, 0.0,
     7110656, "9e6fab4730df6118fb5cceec13fb1f098e16220c", 789008, "e60eec15b190d700327eb5bb7651054749906afc"},
    {"vcr_on_n", 0, "vcr", ArcId::kWerone, 206, 0.875, 0.385, false, false, false, false, 0.0, 0.0, 0.0,
     6586368, "265bbde154379da1e41b33bbf8e9cb604389c05f", 739744, "75ee75a6cd9cec20c36f59d9fedca68e75f8439e"},
    {"vcr_ch_e", 0, "vcr", ArcId::kWerone, 172, 0.874, 0.385, false, false, false, false, 0.0, 0.0, 0.0,
     7598080, "aa7429a80759c9bd71814a411389c52034f7850b", 834784, "a8d25ee6232414a5409aaa9de48ffcc6538bd6f1"},
    {"vcr_cm_c", 0, "vcr", ArcId::kWerone, 178, 0.859, 0.377, true, false, true, true, 0.0, 0.0, 0.035,
     7856128, "d755a111551195f01fa2c4defc7813d00d1d3397", 855136, "abd598399216c171c69140c2ff6a147a93f6455a"},
    {"vle_cs_c", 0, "vle", ArcId::kWertwo, 120, 0.493, 0.458, false, false, false, false, 0.0, 0.0, 0.0,
     7589888, "cdb0c3b979e5880cf6aa33f2a1a1ecdd72237f6d", 752592, "82b981544650a5e79fc184337ff23d7c8bb3b977"},
    {"vle_on_i", 2, "vle", ArcId::kWertwo, 168, 0.495, 0.46, true, false, true, true, 0.012, 0.008, 0.0,
     9748480, "f3779634719f3877bd84e2d044cf48286c4ce262", 881200, "c71ce704f23ce9faadab9c21b920ff61e4a5b838"},
    {"vle_sk_h", 0, "vle", ArcId::kWertwo, 202, 0.497, 0.464, false, true, false, true, 0.0, 0.0, 0.0,
     8814592, "1ece41df88276f469566c07e60a38e15cc0c6154", 842752, "e1ea575026513c45c438bff5313090183d8dee23"},
    {"vle_dl_a", 0, "vle", ArcId::kWertwo, 122, 0.484, 0.444, false, false, false, false, 0.0, 0.0, 0.0,
     8515584, "53cacd67deb0001ed9926a04edb10a78cdc27a9f", 814416, "3c9cd726a51a956c357af8a6c7152ddd28ecdae1"},
    {"vtn_cs_e", 0, "vtn", ArcId::kWertwo, 290, 0.75, 0.527, false, false, false, false, 0.0, 0.0, 0.0,
     9920512, "ce03ce82284e080f59f780379a12690d5871a45b", 1272128, "a0988e044efed586801f31c25d50a560c300ad71"},
    {"vtn_ch_i", 0, "vtn", ArcId::kWertwo, 284, 0.747, 0.523, false, false, false, false, 0.0, 0.0, 0.0,
     9863168, "8295bf4f9ec84d6aab930c2b49d8b6623c061f23", 1327584, "d274de3589d906e1dc564ba2e25ab6cff9e971b6"},
    {"vtn_sh_c", 0, "vtn", ArcId::kWertwo, 320, 0.751, 0.53, true, false, false, false, 0.0, 0.0, 0.0,
     10731520, "58a0f195bdbde46c81c824b57b219070d051e096", 1319936, "a60f07f9faa863b878792ce1394f3d9b7c5a5403"},
    {"vls_on_b", 1, "vtn", ArcId::kWertwo, 242, 0.708, 0.42, false, false, false, false, 0.0, 0.0, 0.0,
     10104832, "d8c60e64c6c09c1433ae2a514ff70a435d5ca4b6", 1249504, "fe1a816ce1d9c8bc03cff87795f564f5a63a9b89"},
};
static_assert(std::size(kSuits) == 34);

}  // namespace

std::span<const GirlRecipe> Girls() { return kGirls; }
std::span<const SuitRecipe> Suits() { return kSuits; }
std::span<const EntryRef> DiscEntries() { return kDiscEntries; }
std::span<const EntryRef> GameEntries() { return kGameEntries; }

EntryRef WaistTemplate(bool tpr) {
  if (tpr) return {ArcId::kChrtwo, 20, 15628288, "4f1826ed62453c616c6ccb6abb6d5c8e017a45b3"};
  return {ArcId::kChrtwo, 21, 706208, "6a2e11883d7acbdf1fc9c4d2931f1b22ff14fef4"};
}
EntryRef GenDonor() { return {ArcId::kWerone, 283, 951312, "3e72583a2644dd21e60d2e54660f69220b1d395c"}; }
EntryRef GenBend() { return {ArcId::kChrthree, 91, 650768, "00b02a74e4c93171b681e23daad29d9c5b2ad1c8"}; }

}  // namespace doa4::suitconv
