// The move-record lookup guard (issue #9: the crash when a throw is the final hit).
//
// The game's sub_82504418(player) finds the record of the move a fighter is in: it takes the fighter's move id (half-word
// +50 of the fighter record 0x83033B40 + player * 156), reads the sub-index byte of that move's 10-byte entry in the move
// table of the fighter's load slot (table 0x830AD870, 132 bytes a slot: +0 the move table, +24 / +28 the record arrays of
// move types 10 and the rest), and returns record sub-index of the array. It checks nothing. When the id is 0xFFFF ("no
// move") while the move type is still 10, the entry is 655 KB past the table: the sub-index is whatever byte lies there (in
// the report, a byte of the costume's model file), the record is past the end of its array, and sub_82504500, the damage
// function, follows that record's +8 word as a pointer to a hit list. On the console the word points into mapped pool
// memory and the read is silent garbage; here the low-memory reservation (doa4_lowmem.h) makes it an access violation.
// The study with every address: notes/research/issue9_story_end_crash_study.md.
//
// The guard (doa4_move_guard_hooks.cpp): when the id is not in the slot's move table, sub_82504418 returns an empty record
// (24 zero bytes whose hit list is only its 0xFF terminator), so every caller reads "nothing set" (the bytes +4, +5, +12,
// +13 and the word +20 they read are 0; sub_824FBAD8 also reads the float +16, but only past a byte +13 that is not 0),
// and sub_82504500 returns 0 damage at once for the two types that walk the list.
// An id in the table is never touched, whatever its sub-index: those reads stay inside the fighter's file and are what
// the console does. Id 0 is left alone too (its entry byte is inside the file's header, before the table).
//
// The table's length is measured, not documented: the span from header field 0 to header field 2, the region that
// follows the table in all 24 fighter files (anm.afs entries 1-24, ou_runs/i9/scratch/census.py), in whole 10-byte
// entries (0 to 6 spare bytes of alignment follow the last entry, which is an end mark). Within that span every type-10
// and type-11 move's sub-index is inside its record array, in every file. Header field 1 is NOT the table's end: it lies
// 9 to 11 regions further on, the two record arrays among them (read as the end, the table looked four times as long).
// When a slot shows no such table (no pointer, or field 2 not above it) the guard stands down and the original code runs.
#pragma once

#include <cstdint>

namespace doa4::move_guard {

inline constexpr uint32_t kFighters = 0x83033B40;  // one record per player (sub_82504418: 0x83033B40 + r3 * 156)
inline constexpr uint32_t kFighterSize = 156;
inline constexpr uint32_t kPlayers = 4;  // a Tag match's four fighters; an index past them is not judged
inline constexpr uint32_t kIdOffset = 50;    // be16: the move id; 0xFFFF = no move
inline constexpr uint32_t kTypeOffset = 55;   // u8: the move type (copied from byte +4 of the move's entry)
inline constexpr uint32_t kSlotOffset = 152;  // u8: the fighter's load slot
inline constexpr uint32_t kSlots = 0x830AD870;  // per load slot, the fighter file's 33 header pointers
inline constexpr uint32_t kSlotSize = 132;
inline constexpr uint32_t kMovesField = 0;     // be32: the move table
inline constexpr uint32_t kMovesEndField = 8;  // be32: header field 2, the region right after the move table
inline constexpr uint32_t kSlotCount = 4;
inline constexpr uint32_t kMoveEntrySize = 10;
inline constexpr uint16_t kCommonIds = 0x4000;  // ids from here on name common moves, not entries of the fighter's table
inline constexpr uint16_t kNoMoveId = 0xFFFF;
inline constexpr uint32_t kEmptyBlockSize = 32;  // the empty record (24) and its list (1), rounded up

enum class Id : uint8_t {
  kInTable,    // 1..count: a move of this fighter
  kZero,       // 0: left to the game
  kPastTable,  // count+1..0x3FFF
  kCommon,     // 0x4000..0xFFFE
  kNone,       // 0xFFFF
  kNoTable,    // the slot shows no move table: the guard stands down
};

struct Lookup {
  uint8_t type = 0, slot = 0;
  uint16_t id = 0;
  uint32_t moves = 0;  // the slot's move table (guest address), 0 without one
  uint32_t count = 0;  // its whole entries
  Id kind = Id::kNoTable;
};

namespace detail {
template <class Mem>
uint32_t Be16(const Mem& mem, uint32_t a) {
  return (uint32_t(mem(a)) << 8) | uint32_t(mem(a + 1));
}
template <class Mem>
uint32_t Be32(const Mem& mem, uint32_t a) {
  return (Be16(mem, a) << 16) | Be16(mem, a + 2);
}
}  // namespace detail

inline Id Classify(uint16_t id, uint32_t count) {
  if (id == 0) return Id::kZero;
  if (id == kNoMoveId) return Id::kNone;
  if (id >= kCommonIds) return Id::kCommon;
  return id <= count ? Id::kInTable : Id::kPastTable;
}

// What sub_82504418(player) is about to look up. mem(address) reads one guest byte.
template <class Mem>
Lookup Read(const Mem& mem, uint32_t player) {
  Lookup l;
  if (player >= kPlayers) return l;
  const uint32_t rec = kFighters + player * kFighterSize;
  l.type = mem(rec + kTypeOffset);
  l.slot = mem(rec + kSlotOffset);
  l.id = uint16_t(detail::Be16(mem, rec + kIdOffset));
  if (l.slot >= kSlotCount) return l;
  const uint32_t slot = kSlots + uint32_t(l.slot) * kSlotSize;
  const uint32_t moves = detail::Be32(mem, slot + kMovesField), next = detail::Be32(mem, slot + kMovesEndField);
  if (moves == 0 || next <= moves || next - moves < kMoveEntrySize) return l;
  l.moves = moves;
  l.count = (next - moves) / kMoveEntrySize;
  l.kind = Classify(l.id, l.count);
  return l;
}

// The lookup returns the empty record instead of reading past the table.
inline bool Guarded(Id kind) { return kind == Id::kPastTable || kind == Id::kCommon || kind == Id::kNone; }

// The damage function (sub_82504500) walks the record's hit list only for the move types 10 and 11: there it is answered
// with 0 without running, which is its own "nothing to add" exit (no event posted).
inline bool AnswersDamage(const Lookup& l) { return (l.type == 10 || l.type == 11) && Guarded(l.kind); }

// The empty record in a block of kEmptyBlockSize guest bytes: put(address, byte) writes one guest byte.
template <class Put>
void FillEmptyRecord(uint32_t block, Put put) {
  for (uint32_t i = 0; i < kEmptyBlockSize; ++i) put(block + i, uint8_t(0));
  const uint32_t list = block + 24;
  put(block + 8, uint8_t(list >> 24));
  put(block + 9, uint8_t(list >> 16));
  put(block + 10, uint8_t(list >> 8));
  put(block + 11, uint8_t(list));
  put(list, uint8_t(0xFF));
}

// The app, once the game image is loaded: the guest address of a filled empty record, or 0 (the lookup guard then stands
// down; the damage answer needs no record). Defined in doa4_move_guard_hooks.cpp.
void Install(uint32_t empty_record);

}  // namespace doa4::move_guard
