// DOA4's costume tables, extended with imported suits (spec section 3.3). Byte access through
// GuestBytes, so host tests run on the image's real tables (tests/host/doa4_costume_fixtures.h).
//
// Per charId c:
//   max[c]   u8   0x82C34888 + c       costumes (caps the save's unlock count)
//   len[c]   u8   0x82C348A4 + c       list length (the sum of the variant counts)
//   list[c]  be32 0x82C348C0 + 4c  ->  u8 model index per (costume, variant), costume-major
//   var[c]   be32 0x82C34928 + 4c  ->  u8 variants per costume
//   records  be32 0x82C5E738 + 8c + 4 -> {be16 body tpr, body cat, part tpr, part cat} per model;
//            no count: a block runs to the next character's block (sub_823FE150 reads
//            records[model] unchecked)
//   split    {be32 tpr, be32 cat} 0x82C5E800 + 8c: the body's share of the per-player 20 MiB /
//            4 MiB; 0/0 = the whole budget (no head part)
//   files    {u8 archive, u8 0, be16 entry} 0x82C3B2E8 + 4 * file (1640 files);
//            archive 3 = chrone.afs, 4 = chrtwo.afs, 5 = chrthree.afs
// The lookup sub_82526B78(c, costume, variant) = list[c][sum(var[c][0..costume)) + variant].

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "doa4_costume_lists.h"
#include "doa4_guest.h"
#include "doa4_suits_manifest.h"

namespace doa4::costumes {

inline constexpr uint32_t kMaxTable = costume_lists::kMax;
inline constexpr uint32_t kLenTable = costume_lists::kLength;
inline constexpr uint32_t kListTable = costume_lists::kList;
inline constexpr uint32_t kVarTable = costume_lists::kVariants;
inline constexpr uint32_t kModelTable = 0x82C5E738;
inline constexpr uint32_t kSplitTable = 0x82C5E800;
inline constexpr uint32_t kFileTable = 0x82C3B2E8;
inline constexpr uint32_t kCharCount = 25;
inline constexpr uint32_t kFileCount = 1640;
inline constexpr uint64_t kWholeTpr = 0x1400000;  // per player
inline constexpr uint64_t kWholeCat = 0x400000;
inline constexpr uint32_t kImageStart = 0x82000000;  // the XEX's sections. A record block out here is one Apply moved; a list
inline constexpr uint32_t kImageEnd = 0x83000000;    // pointer out here may be a box's or Apply's block (CheckSuit)
// From Task 1 (notes/research/swimsuit_table_readers.md): sub_8249A710 indexes a 15-entry
// threshold table by the unlock count, so a character keeps at most 17 costumes; a costume
// number at or above max makes the lookup read variant bytes past the array, so the list and
// variant copies are 256 bytes with zeros after the data (a u8 costume number cannot reach past).
inline constexpr uint32_t kMaxCostumesLimit = costume_lists::kCostumeLimit;
inline constexpr uint32_t kTableBlock = costume_lists::kFullBlock;

// A set of model indexes 0..255 (a model index is one byte). Words of 4 bytes, so GuardState has
// no tail padding: the host test's "one past her last record" read still lands on the neighbour
// it plants (a std::bitset would align the struct to 8 and put padding there).
class ModelSet {
 public:
  bool contains(uint32_t model) const { return model < 256 && ((words_[model >> 5] >> (model & 31)) & 1u) != 0; }
  void insert(uint32_t model) {
    if (model < 256) words_[model >> 5] |= 1u << (model & 31);
  }
  uint32_t size() const {
    uint32_t n = 0;
    for (uint32_t w : words_) {
      for (; w; w &= w - 1) ++n;
    }
    return n;
  }

 private:
  std::array<uint32_t, 8> words_{};
};

// What the load-request guard compares against. `records`: model records per character (0 =
// unknown, no count rule), taken from the image before any patch, then raised by Apply.
// `empty_slot`: per character, the models whose body .tpr or body .cat is an empty costume slot
// (16 bytes of 0x77) in the archives as the game reads them; filled by suits::MarkEmptySlots,
// empty until then. `records` stays the LAST member (see ModelSet).
struct GuardState {
  std::array<ModelSet, kCharCount> empty_slot{};
  std::array<uint32_t, kCharCount> records{};
};

// Records in character c's block: the distance to the next character's block (pointers inside
// the image only); 0 for the last block and for c >= 25.
uint32_t RecordCount(const GuestBytes& guest, uint32_t character);
GuardState ReadGuardState(const GuestBytes& guest);

struct FileLoc {
  const char* archive;  // "chrone.afs", "chrtwo.afs" or "chrthree.afs"
  uint32_t entry;
};
// Where a global file number lives, for the three character archives only.
std::optional<FileLoc> FileLocation(const GuestBytes& guest, uint32_t file);

// Whether a suit fits this game's tables as they stand: after the Extras boxes, before Apply (a box has already moved her list
// to the heap; after Apply her records have moved too and a second check of her suits would see other records). Empty when it
// fits, else why not. A named head part must be a pair one of her own records already uses, and both part files must be
// character archive files.
std::string CheckSuit(const GuestBytes& guest, const suits::Suit& suit);

// The suits of `suits` that pass CheckSuit, in order: what a session puts in the tables from the picks. It reads only the
// tables, which are equal on both PCs of a session, so both keep the same ones. `refused` (may be null): "<label>: <why>".
std::vector<suits::Suit> Fitting(const GuestBytes& guest, const std::vector<suits::Suit>& suits, std::vector<std::string>* refused);

struct Applied {
  uint8_t character = 0;
  uint32_t model = 0;    // her model index for the suit
  uint32_t costume = 0;  // her costume number for the suit (the lookup's argument)
  std::string label;
};
struct ApplyResult {
  std::vector<Applied> applied;
  // One per character left unpatched, and one per suit refused while her others went in; each names what it refused.
  std::vector<std::string> errors;
  std::map<std::string, std::string> refused;  // suit label -> why, for a suit refused while her others went in
  GuardState guard;
};
// Guest memory for the moved tables: a block of `size` bytes, or 0.
using AllocFn = std::function<uint32_t(uint32_t size)>;

// Adds the suits (each already passed CheckSuit) as each character's next costumes, in order:
// fills her own empty records' head parts or appends records to a copy of her record block,
// appends to copies of her list and variant array (one variant each, doa4_costume_lists.h: the
// tables as they stand at this call, so after the Extras boxes), raises len and max.
// Per character, every block is allocated before anything is written: a failed allocation
// leaves her tables as they were. At most kMaxCostumesLimit costumes per character, and at most
// 255 model records (model index 255 means "none" to the loader, sub_823FE870): a suit that would
// pass either is refused by name and her earlier suits stay. The record copy is never freed or
// moved: the loader caches a pointer into it (slot + 32, sub_823FE150).
// Afterwards the app hands her models to TweakPatcher::CarrySuits, so a box toggled later keeps them. The input's order, and
// which suits it holds, are the caller's: the import's offline, the picks in a session (doa4_suits.h).
ApplyResult Apply(const GuestBytes& guest, const AllocFn& alloc, const std::vector<suits::Suit>& suits);

// Every byte of the costume tables as the game reads them, in a fixed order: per character her maximum, length, the list and
// variant pointers and what they point at, her record pointer and her `state.records[c]` records. The session logs its digest,
// so two peers' logs compare the moved blocks too, which the frame-0 .data dump does not reach.
std::vector<uint8_t> TableBytes(const GuestBytes& guest, const GuardState& state);

// How many costumes the select screen offers one girl. The save's count is one number per girl and the suits come after
// all her costumes, so a girl with suits is offered all of them (`list_length`: her costume maximum, which includes the
// suits) whatever the save's count says, and a girl without suits keeps the count: her other girls' suits do not change
// it. Never below the count (a count above the maximum is not lowered here).
uint8_t VisibleCount(uint8_t save_count, uint8_t list_length, bool has_suits);

// The select screen's per-player copy of the save's counts: u8[25] per player (row = base + 25 * player), indexed by
// charId. Written by exactly two guest functions (fix round notes: Task 3 report): sub_82525E80 fills one player's row
// from the loaded profile when the screen starts, and sub_82523C18 refills every row from the screen's snapshot of the
// save. Read by the cursor sub_825265E0, the list drawer sub_82528190 and the random costume sub_82523120.
inline constexpr uint32_t kCountCopy = 0x83103000;
inline constexpr uint32_t kCountCopyRow = kCharCount;
inline constexpr uint32_t kCountCopyPlayers = 4;

// How many suits each character has in the tables (from Apply's result); 0 for a girl without.
using SuitCounts = std::array<uint8_t, kCharCount>;
SuitCounts CountSuits(const std::vector<Applied>& applied);

// One girl's copy raised to what VisibleCount offers.
struct Raised {
  uint8_t character = 0;
  uint8_t save = 0;   // the copy's count before (the save's, as the game filled it)
  uint8_t shows = 0;  // after
  uint8_t of = 0;     // her costume maximum
};
// Raises one player's row (`row` = the row's address) for every girl with suits; returns the girls it changed.
// Writes only the copy, never the save. A count of 0 is a place the game did not fill (a player not in the match, a
// charId outside the table; a save's count is 2 or more) and stays 0.
std::vector<Raised> RaiseCountRow(const GuestBytes& guest, uint32_t row, const SuitCounts& suits);

// Which rule of the load-request guard a request (character, model) meets, first match wins:
//  - model 255 and any character that is not one of the 25: none (255 means "none" to the loader,
//    sub_823FE870, which then loads nothing);
//  - kPastRecords: her record count is known and the model is at or past it (the loader reads
//    records[model] unchecked, so it would load another character's files);
//  - kEmptySlot: the model is marked in `empty_slot` (its body .tpr or .cat is an empty costume
//    slot: a suit that is not imported here, asked for by an old replay or a peer);
//  - kNone.
enum class GuardRule { kNone, kPastRecords, kEmptySlot };
GuardRule RuleFor(const GuardState& state, uint32_t character, uint32_t model);

// The model a load request for (character, model) should use: `model` under kNone, else
// `first_listed` (her first costume's model).
uint32_t GuardModel(const GuardState& state, uint32_t character, uint32_t model, uint32_t first_listed);

// The hook's whole decision for sub_823FE870(slot r3, character r4, model r5), on guest bytes so a
// host test can drive it: r4 and r5 are masked to a byte, the rule is looked up, and only when one
// fires her first listed model is read from the list table (list[c][0]); `use` is the model to
// load. With no state installed (`state` null) nothing is changed.
struct LoadDecision {
  uint32_t character = 0;  // r4 & 0xFF
  uint32_t asked = 0;      // r5 & 0xFF
  uint32_t use = 0;        // the model to load
  GuardRule rule = GuardRule::kNone;
};
LoadDecision DecideLoad(const GuardState* state, const GuestBytes& guest, uint32_t r4, uint32_t r5);

}  // namespace doa4::costumes
