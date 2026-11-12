// Host test for the move-record lookup guard (src/doa4_move_guard.h, issue #9). Built and run by run_move_guard_test.bat.
//
// The contract:
// - a fighter's move id is judged against ITS load slot's move table: ids 1..count are in the table, where count is the
//   table's span to header field 2 (the region that follows it in all 24 fighter files) in whole 10-byte entries; the span
//   is not a multiple of 10 (Ayane's is 6784 bytes = 678 entries + 4), and header field 1 is NOT the table's end (it lies
//   several regions further on: read as the end it made the table look 2866 entries long);
// - 0xFFFF ("no move"), an id past the table and a common-move id (0x4000..) are not in the table: the lookup is guarded;
// - id 0 is left to the game (its read stays inside the file's header), and so is a slot without a readable table (the
//   guard stands down: the original code runs);
// - the damage function is answered with 0 only for the two move types that walk the record's list (10 and 11);
// - the empty record is 24 zero bytes whose +8 pointer names a list holding only the 0xFF terminator.
//
// The fixture's numbers are the crash's: fighter record 0x83033B40 (player 0), Ayane's file at 0x10000000, her move table
// at +0x88, header field 1 at +0x7080 and field 2 at +0x1B08 (anm.afs entry 14, ou_runs/i9/scratch/census.py), move type
// 10, id 0xFFFF.
// What this test reproduces is the MECHANISM (the id that is no move reaching the lookup); the symptom, the crash on a
// throw finisher, is reproduced in the game (notes/research/issue9_story_end_crash_study.md).

#include "../../src/doa4_move_guard.h"

#include <cstdio>
#include <map>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::fflush(stdout);                                        \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

namespace mg = doa4::move_guard;

// Guest memory as a map: an address never written reads 0.
struct Mem {
  std::map<uint32_t, uint8_t> bytes;
  uint8_t operator()(uint32_t address) const {
    const auto it = bytes.find(address);
    return it == bytes.end() ? uint8_t(0) : it->second;
  }
  void Put8(uint32_t a, uint8_t v) { bytes[a] = v; }
  void Put16(uint32_t a, uint16_t v) { Put8(a, uint8_t(v >> 8)), Put8(a + 1, uint8_t(v)); }
  void Put32(uint32_t a, uint32_t v) { Put16(a, uint16_t(v >> 16)), Put16(a + 2, uint16_t(v)); }
};

// Literals from the game's code (sub_82504418, generated/default/dead_or_alive_4_recomp.18.cpp:33338), not from the header
// under test: the fighter records, the slot table, the field offsets.
constexpr uint32_t kRec0 = 0x83033B40, kRecSize = 156, kTable = 0x830AD870, kSlotBytes = 132;

static void Fighter(Mem& m, uint32_t player, uint8_t type, uint8_t slot, uint16_t id) {
  const uint32_t rec = kRec0 + player * kRecSize;
  m.Put8(rec + 55, type);
  m.Put8(rec + 152, slot);
  m.Put16(rec + 50, id);
}
// field1 is the header's second pointer (far above the table in every file), field2 the region right after the table.
static void Slot(Mem& m, uint32_t slot, uint32_t moves, uint32_t field2, uint32_t field1 = 0x7FFFFFF0) {
  m.Put32(kTable + slot * kSlotBytes + 0, moves);
  m.Put32(kTable + slot * kSlotBytes + 4, field1);
  m.Put32(kTable + slot * kSlotBytes + 8, field2);
}

int main() {
  {  // the crash's state: Ayane in slot 0, type 10, id 0xFFFF
    Mem m;
    Slot(m, 0, 0x10000088, 0x10001B08, 0x10007080);
    Fighter(m, 0, 10, 0, 0xFFFF);
    const mg::Lookup l = mg::Read(m, 0);
    CHECK(l.type == 10);
    CHECK(l.slot == 0);
    CHECK(l.id == 0xFFFF);
    CHECK(l.moves == 0x10000088);
    CHECK(l.count == 678);  // 6784 bytes to field 2: whole entries only, NOT refused for the 4 spare bytes, and NOT
                            // 2866 (the span to field 1)
    CHECK(l.kind == mg::Id::kNone);
    CHECK(mg::Guarded(l.kind));
    CHECK(mg::AnswersDamage(l));
  }
  {  // both edges of the table: the last id in it, the first past it
    Mem m;
    Slot(m, 0, 0x10000088, 0x10001B08, 0x10007080);
    Fighter(m, 0, 10, 0, 678);
    CHECK(mg::Read(m, 0).kind == mg::Id::kInTable);
    CHECK(!mg::Guarded(mg::Read(m, 0).kind));
    CHECK(!mg::AnswersDamage(mg::Read(m, 0)));
    Fighter(m, 0, 10, 0, 679);
    CHECK(mg::Read(m, 0).kind == mg::Id::kPastTable);
    CHECK(mg::Guarded(mg::Read(m, 0).kind));
    Fighter(m, 0, 10, 0, 1);
    CHECK(mg::Read(m, 0).kind == mg::Id::kInTable);
    Fighter(m, 0, 10, 0, 2866);  // inside the span to field 1: other data, not a move
    CHECK(mg::Read(m, 0).kind == mg::Id::kPastTable);
    Fighter(m, 0, 10, 0, 0x3FFF);
    CHECK(mg::Read(m, 0).kind == mg::Id::kPastTable);
    Fighter(m, 0, 10, 0, 0x4000);
    CHECK(mg::Read(m, 0).kind == mg::Id::kCommon);
    CHECK(mg::Guarded(mg::Read(m, 0).kind));
    Fighter(m, 0, 10, 0, 0xFFFE);
    CHECK(mg::Read(m, 0).kind == mg::Id::kCommon);
  }
  {  // id 0: the game's read stays in the file's header; left alone
    Mem m;
    Slot(m, 0, 0x10000088, 0x10001B08, 0x10007080);
    Fighter(m, 0, 10, 0, 0);
    CHECK(mg::Read(m, 0).kind == mg::Id::kZero);
    CHECK(!mg::Guarded(mg::Read(m, 0).kind));
    CHECK(!mg::AnswersDamage(mg::Read(m, 0)));
  }
  {  // the player's own record and its own slot: player 1 in slot 2 with a short table, player 0 in slot 0 with a long one
    Mem m;
    Slot(m, 0, 0x10000088, 0x10001B08, 0x10007080);  // 678 entries
    Slot(m, 2, 0x10400088, 0x10400088 + 105);  // 10 entries + 5 bytes
    Fighter(m, 0, 11, 0, 11);
    Fighter(m, 1, 10, 2, 11);
    const mg::Lookup a = mg::Read(m, 0), b = mg::Read(m, 1);
    CHECK(a.kind == mg::Id::kInTable);    // 11 of 678
    CHECK(b.kind == mg::Id::kPastTable);  // 11 of 10: NOT judged against slot 0's table or player 0's id
    CHECK(b.slot == 2 && b.count == 10 && b.moves == 0x10400088);
    CHECK(a.type == 11 && b.type == 10);
    Fighter(m, 1, 10, 2, 10);
    CHECK(mg::Read(m, 1).kind == mg::Id::kInTable);
  }
  {  // no readable table: the guard stands down (the original code runs), whatever the id
    Mem m;
    Fighter(m, 0, 10, 0, 0xFFFF);  // slot 0 holds no pointers
    CHECK(mg::Read(m, 0).kind == mg::Id::kNoTable);
    CHECK(!mg::Guarded(mg::Read(m, 0).kind));
    CHECK(!mg::AnswersDamage(mg::Read(m, 0)));
    Slot(m, 0, 0, 0x1B08);  // no table pointer, though the next field is set
    CHECK(mg::Read(m, 0).kind == mg::Id::kNoTable);
    Slot(m, 0, 0x10001B08, 0x10000088);  // the next field below the table
    CHECK(mg::Read(m, 0).kind == mg::Id::kNoTable);
    Slot(m, 0, 0x10000088, 0x10000088 + 9);  // not one whole entry
    CHECK(mg::Read(m, 0).kind == mg::Id::kNoTable);
    Slot(m, 0, 0x10000088, 0x10000088 + 10);  // exactly one entry
    Fighter(m, 0, 10, 0, 1);
    CHECK(mg::Read(m, 0).kind == mg::Id::kInTable);
    Fighter(m, 0, 10, 4, 0xFFFF);  // the table has 4 slots (0..3)
    Slot(m, 4, 0x10000088, 0x10001B08, 0x10007080);
    CHECK(mg::Read(m, 0).kind == mg::Id::kNoTable);
    Fighter(m, 0, 10, 3, 0xFFFF);
    Slot(m, 3, 0x10000088, 0x10001B08, 0x10007080);
    CHECK(mg::Read(m, 0).kind == mg::Id::kNone);
  }
  {  // four fighter records (a Tag match): player 3 is judged, an index past them reads nothing and stands down
    Mem m;
    Slot(m, 1, 0x10000088, 0x10001B08, 0x10007080);
    Fighter(m, 3, 10, 1, 0xFFFF);
    Fighter(m, 4, 10, 1, 0xFFFF);
    CHECK(mg::Read(m, 3).kind == mg::Id::kNone);
    CHECK(mg::Read(m, 4).kind == mg::Id::kNoTable);
    CHECK(mg::Read(m, 4).id == 0);  // not read at all
    CHECK(mg::Read(m, 0xFFFFFFFFu).kind == mg::Id::kNoTable);
  }
  {  // the damage function is answered only for the list-walking types 10 and 11
    Mem m;
    Slot(m, 0, 0x10000088, 0x10001B08, 0x10007080);
    for (int type = 0; type < 32; ++type) {
      Fighter(m, 0, uint8_t(type), 0, 0xFFFF);
      const mg::Lookup l = mg::Read(m, 0);
      CHECK(mg::Guarded(l.kind));  // the lookup itself is guarded for every type
      CHECK(mg::AnswersDamage(l) == (type == 10 || type == 11));
    }
  }
  {  // the empty record: 24 zero bytes, +8 names a list that is only the terminator, the byte before the list is 0
    Mem m;
    const uint32_t block = 0x40123460;
    for (uint32_t i = 0; i < mg::kEmptyBlockSize; ++i) m.Put8(block + i, 0xAA);  // the allocator's leftovers
    mg::FillEmptyRecord(block, [&m](uint32_t a, uint8_t v) { m.Put8(a, v); });
    CHECK(mg::kEmptyBlockSize >= 25);
    const uint32_t list = (uint32_t(m(block + 8)) << 24) | (uint32_t(m(block + 9)) << 16) | (uint32_t(m(block + 10)) << 8) |
                          m(block + 11);
    CHECK(list == block + 24);
    CHECK(m(list) == 0xFF);
    CHECK(m(list - 1) == 0);  // sub_82504500 reads list[count - 1] with a count of 0 hits
    bool zero = true;
    for (uint32_t i = 0; i < 24; ++i) zero = zero && (i >= 8 && i < 12 ? true : m(block + i) == 0);
    CHECK(zero);  // the flag bytes +4, +5, +12, +13 and the word +20 the callers read
    bool rest = true;
    for (uint32_t i = 25; i < mg::kEmptyBlockSize; ++i) rest = rest && m(block + i) == 0;
    CHECK(rest);
  }
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
