// The move-record lookup guard's hooks (issue #9; the rules and the reasons: doa4_move_guard.h).
//
// sub_82504418(r3 = player) returns the record of the fighter's current move; sub_82504500(r3 = player, r4 = flag) is the
// damage function whose types 10 and 11 follow that record's hit list. Both are guest main-thread work inside game frames
// and read only game state, so two players of an online session decide alike on the same frame.
//
// Every lookup whose id is not a move of the table is logged (the first lines in full, then counted), guarded or not: the
// log names the caller the next time a report comes in. A count line (by kind, with the highest in-table id seen per
// slot beside the slot's table length) is written at the first lookup and whenever the total doubles, so a log without a
// warning also shows that lookups ran. DOA4_MOVE_GUARD=off (a test switch, np_session.h TestSwitch:
// never in an online session without a test folder) runs the game's own code, so one build shows the crash beside the fix.

#include <atomic>
#include <cstdint>
#include <string_view>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "doa4_move_guard.h"
#include "net/np_session.h"

extern "C" {
REX_EXTERN(__imp__sub_82504418);
REX_EXTERN(__imp__sub_82504500);
}

namespace {
namespace mg = doa4::move_guard;

std::atomic<uint32_t> g_empty{0};
std::atomic<uint64_t> g_seen[6];    // lookups by mg::Id
std::atomic<uint32_t> g_lines[6];   // detail lines written by mg::Id
std::atomic<uint32_t> g_max_id[mg::kSlotCount];     // the highest in-table id looked up, per load slot
std::atomic<uint32_t> g_max_count[mg::kSlotCount];  // that slot's table length at the time
constexpr uint32_t kDetailLines = 16;

struct GuestMem {
  const uint8_t* base;
  uint8_t operator()(uint32_t address) const { return base[address]; }
};

bool On() {
  static const bool on = [] {
    const char* v = doa4::np::TestSwitch("DOA4_MOVE_GUARD");
    const bool off = v && std::string_view(v) == "off";
    if (off) REXLOG_WARN("[doa4][MOVEGUARD] DOA4_MOVE_GUARD=off: the move-record lookups run unguarded (test switch)");
    return !off;
  }();
  return on;
}

const char* Name(mg::Id kind) {
  switch (kind) {
    case mg::Id::kInTable: return "in the table";
    case mg::Id::kZero: return "id 0";
    case mg::Id::kPastTable: return "past the table";
    case mg::Id::kCommon: return "a common-move id";
    case mg::Id::kNone: return "no move (0xFFFF)";
    case mg::Id::kNoTable: return "no table";
  }
  return "?";
}

// Counts the lookup and logs it when its id is not a move of the table.
void Note(const mg::Lookup& l, uint32_t player, uint32_t lr, const char* what, const char* action) {
  const size_t k = size_t(l.kind);
  uint64_t total = 0;
  g_seen[k].fetch_add(1, std::memory_order_relaxed);
  for (const auto& n : g_seen) total += n.load(std::memory_order_relaxed);
  if (l.kind == mg::Id::kInTable && l.slot < mg::kSlotCount && l.id > g_max_id[l.slot].load(std::memory_order_relaxed)) {
    g_max_id[l.slot].store(l.id, std::memory_order_relaxed);
    g_max_count[l.slot].store(l.count, std::memory_order_relaxed);
  }
  if (l.kind != mg::Id::kInTable && g_lines[k].fetch_add(1, std::memory_order_relaxed) < kDetailLines) {
    REXLOG_WARN("[doa4][MOVEGUARD] {}: player {} move type {} id {:#06x} is {} (slot {}, {} moves, caller {:#010x}): {}", what,
                player, l.type, l.id, Name(l.kind), l.slot, l.count, lr, action);
  }
  if ((total & (total - 1)) == 0) {  // 1, 2, 4, 8, ...
    REXLOG_INFO("[doa4][MOVEGUARD] {} lookups: {} in the table, {} id 0, {} past the table, {} common ids, {} no move, {} no table; "
                "highest in-table id per slot {}/{} {}/{} {}/{} {}/{}",
                total, g_seen[0].load(), g_seen[1].load(), g_seen[2].load(), g_seen[3].load(), g_seen[4].load(), g_seen[5].load(),
                g_max_id[0].load(), g_max_count[0].load(), g_max_id[1].load(), g_max_count[1].load(), g_max_id[2].load(),
                g_max_count[2].load(), g_max_id[3].load(), g_max_count[3].load());
  }
}
}  // namespace

namespace doa4::move_guard {
void Install(uint32_t empty_record) {
  g_empty.store(empty_record, std::memory_order_release);
  if (empty_record) {
    REXLOG_INFO("[doa4][MOVEGUARD] empty move record at {:#010x}", empty_record);
  } else {
    REXLOG_ERROR("[doa4][MOVEGUARD] no memory for the empty move record: the lookup runs unguarded");
  }
}
}  // namespace doa4::move_guard

REX_HOOK_RAW(sub_82504418) {
  const uint32_t player = ctx.r3.u32;
  const mg::Lookup l = mg::Read(GuestMem{base}, player);
  const uint32_t empty = g_empty.load(std::memory_order_acquire);
  const bool guard = mg::Guarded(l.kind) && empty != 0 && On();
  Note(l, player, uint32_t(ctx.lr), "lookup", guard ? "the empty record is returned" : "the game's own read runs");
  if (guard) {
    ctx.r3.u64 = empty;
    return;
  }
  __imp__sub_82504418(ctx, base);
}

REX_HOOK_RAW(sub_82504500) {
  const uint32_t player = ctx.r3.u32;
  const mg::Lookup l = mg::Read(GuestMem{base}, player);
  if (mg::AnswersDamage(l) && On()) {
    Note(l, player, uint32_t(ctx.lr), "damage", "answered with 0");
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82504500(ctx, base);
}
