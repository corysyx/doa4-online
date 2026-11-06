// doa4_suits_hooks.cpp
//
// sub_823FE870(slot r3, character r4, model r5): the load request. The decision is
// costumes::DecideLoad (host-tested); this file only applies it and logs. A request is changed in
// two cases, both for a model that an old replay or a peer with a different import can ask for:
//  - the model is past the character's records: the loader (sub_823FE150) reads records[model]
//    unchecked, so it would load another character's files;
//  - the model is an empty costume slot: its body .tpr or .cat is a 16-byte placeholder in the
//    archives as the game reads them (a suit that is not imported here, so no overlay fills it),
//    and the loader would read the placeholder as a model.
// Either way the request gets her first listed costume instead. Model 255 is left alone: it
// means "none" to the loader, which then loads nothing. The costume lookup sub_82526B78 already
// clamps its own inputs.
//
// sub_82455E08(entry r3): one shell-pass ("fur") setup call; it reads entry+308, which only a
// body .cat's type-6 block sets (stage 1c: missing -> null read -> crash on the first fur draw).
// When it is null the setup call is skipped. That is all that is skipped: each caller's 8-pass
// loop (generated/default/dead_or_alive_4_recomp.12.cpp:34620-34640 and 35120-35140) still calls
// the draw routine sub_82783700 on every pass, with whatever state the previous pass left. What
// then shows on screen is not decided here; Task 10 (R2) judges it by photo. Both callers
// overwrite r3 after the call. DOA4_SUITS_NO_FUR_GUARD=1 turns the guard off (the positive
// control).
//
// Test switch DOA4_SUITS_SCENE=<entry>=<char>:#<costume>[,...] (ported from the stage-0 probe's
// DOA4_FORCE_SCENE): each attract scene's fighter entry gets that character in that costume, the
// costume resolved by the game's own lookup over the (patched) tables.
//
// Costume-select labels past COS. 10 (doa4_suits_labels.h). The extended sheet-6 table (records
// 197..203 = COS. 11..17) is installed once at start-up by SetUpLabels; these hooks only steer ids:
// - sub_82528190(r3, select struct r4): the costume list drawer, one call per player per frame on
//   the select screen. With a table installed (InstallLabelTable) the original runs inside a
//   labels::ListingScope ("this thread is drawing the costume list"); no lock, no swap. Before that,
//   one 4-byte read checks that sheet 6's pointer word still names the installed table; if anything
//   ever changed it, a WARN is logged once and the drawer runs untouched (no rewrite: ids 197+ would
//   index past the original table). Everything the drawer reaches (16 functions down to
//   sub_823BF210 and sub_823B9C28) has no indirect call, no import and no wait, so the flag cannot
//   leak onto other work on this thread.
// - sub_823BF210(r3, draw struct r4, r5): the sprite resolver. It reads the id at r4 + 4 at entry
//   (.7.cpp:52498/52508) and stores that same word into the queued 2D command (:52940/52942). While
//   the flag is set, ids 133..139 of sheet 6 are rewritten to 197..203 in the draw struct (a copy on
//   sub_823BB470's stack, r1 + 80) before the original runs, so the command carries the new id and
//   the frame flush resolves it through the extended table as well.
// Test switch DOA4_SUITS_LABEL_TRACE=1, read once at start: counts the rewrites and, in hooks on the
// flush's two sprite cases sub_823C1280 / sub_823C15E8 (command in r5, id at +32), the lookups of
// sheet-6 ids 197..203, with the threads seen; one INFO line once the list has been drawn 120 times
// (about a second of the select screen). Off, each trace hook is one branch on that flag.

#include "doa4_suits_hooks.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/thread.h>

#include "doa4_suits_bust.h"
#include "doa4_suits_labels.h"
#include "generated/default/dead_or_alive_4_init.h"

extern "C" {
REX_EXTERN(__imp__sub_82373E50);
REX_EXTERN(__imp__sub_823701F0);
REX_EXTERN(__imp__sub_823FE870);
REX_EXTERN(__imp__sub_82455E08);
REX_EXTERN(__imp__sub_823841D0);
REX_EXTERN(__imp__sub_82528190);
REX_EXTERN(__imp__sub_823BF210);
REX_EXTERN(__imp__sub_823C1280);
REX_EXTERN(__imp__sub_823C15E8);
}

namespace {

doa4::costumes::GuardState g_state_storage;  // written before guest code, read after
std::atomic<const doa4::costumes::GuardState*> g_state{nullptr};
std::atomic<int> g_model_logs{0};
std::atomic<uint64_t> g_fur_skips{0};

uint32_t Be32(uint8_t* base, uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); }

bool FurGuardOn() {
  static const bool on = [] {
    const char* env = std::getenv("DOA4_SUITS_NO_FUR_GUARD");
    const bool off = env && env[0] == '1';
    if (off) REXLOG_WARN("[doa4][SUITS] test: fur guard OFF (DOA4_SUITS_NO_FUR_GUARD=1)");
    return !off;
  }();
  return on;
}

struct SceneForce {
  uint32_t entry, character, costume;
};

uint32_t ParseNum(std::string_view s) {
  uint32_t v = 0;
  const bool hex = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  for (char c : hex ? s.substr(2) : s) {
    const int d = (c >= '0' && c <= '9') ? c - '0'
                  : (hex && c >= 'a' && c <= 'f') ? c - 'a' + 10
                  : (hex && c >= 'A' && c <= 'F') ? c - 'A' + 10
                                                  : -1;
    if (d < 0) break;
    v = v * (hex ? 16 : 10) + uint32_t(d);
  }
  return v;
}

const std::vector<SceneForce>& Forces() {
  static const std::vector<SceneForce> forces = [] {
    std::vector<SceneForce> out;
    const char* env = std::getenv("DOA4_SUITS_SCENE");
    if (!env) return out;
    std::string_view rest(env);
    while (!rest.empty()) {
      const size_t comma = rest.find(',');
      const std::string_view item = rest.substr(0, comma);
      const size_t eq = item.find('='), colon = item.find(":#");
      if (eq != std::string_view::npos && colon != std::string_view::npos && colon > eq) {
        out.push_back({ParseNum(item.substr(0, eq)), ParseNum(item.substr(eq + 1, colon - eq - 1)),
                       ParseNum(item.substr(colon + 2))});
        REXLOG_INFO("[doa4][SUITS] test: scene entry {} -> charId {:#x} costume #{}", out.back().entry,
                    out.back().character, out.back().costume);
      }
      if (comma == std::string_view::npos) break;
      rest.remove_prefix(comma + 1);
    }
    return out;
  }();
  return forces;
}

std::atomic<uint32_t> g_label_table{0};
std::atomic<bool> g_label_table_lost{false};  // the pointer word stopped naming the table (WARN once)

// DOA4_SUITS_LABEL_TRACE=1. Read during static initialisation, before any guest code; the trace
// hooks only branch on it.
const bool g_label_trace = [] {
  const char* env = std::getenv("DOA4_SUITS_LABEL_TRACE");
  return env && env[0] == '1';
}();

constexpr uint64_t kTraceReportAfter = 120;  // list draws before the INFO line

struct LabelTrace {
  std::mutex mutex;  // trace runs only
  uint64_t lists = 0;
  uint64_t rewrites = 0;            // resolver calls whose id was rewritten 133..139 -> 197..203
  uint64_t flush[2] = {0, 0};       // flush lookups of sheet-6 ids 197..203: sub_823C1280, sub_823C15E8
  std::set<uint32_t> list_threads;  // threads that drew the costume list (first 8)
  std::set<uint32_t> flush_threads;  // threads that flushed a new id (first 8)
};
LabelTrace g_trace;

std::string Threads(const std::set<uint32_t>& threads) {
  std::string out;
  for (uint32_t t : threads) out += (out.empty() ? "" : ", ") + std::to_string(t);
  return out.empty() ? std::string("none") : out;
}

void TraceRewrite() {
  std::lock_guard<std::mutex> lock(g_trace.mutex);
  ++g_trace.rewrites;
}

void TraceFlush(int which, uint32_t id) {
  const uint32_t first = (doa4::labels::kSheet << 16) | doa4::labels::kFirstNewLabel;
  if (id < first || id >= first + doa4::labels::kNewLabels) return;
  const uint32_t me = rex::thread::current_thread_system_id();
  std::lock_guard<std::mutex> lock(g_trace.mutex);
  ++g_trace.flush[which];
  if (g_trace.flush_threads.size() < 8) g_trace.flush_threads.insert(me);
}

void TraceListEnd() {
  const uint32_t me = rex::thread::current_thread_system_id();
  std::lock_guard<std::mutex> lock(g_trace.mutex);
  if (g_trace.list_threads.size() < 8) g_trace.list_threads.insert(me);
  if (++g_trace.lists != kTraceReportAfter) return;
  REXLOG_INFO("[doa4][SUITS] label trace: costume list drawn {} times on thread(s) {}; {} ids rewritten 133-139 -> "
              "197-203; flush lookups of ids 197-203: {} in sub_823C1280, {} in sub_823C15E8, on thread(s) {}",
              g_trace.lists, Threads(g_trace.list_threads), g_trace.rewrites, g_trace.flush[0], g_trace.flush[1],
              Threads(g_trace.flush_threads));
}

}  // namespace

void doa4::suits::InstallGuards(const costumes::GuardState& state) {
  g_state_storage = state;
  g_state.store(&g_state_storage, std::memory_order_release);
}

uint32_t doa4::suits::InstalledEmptySlots() {
  const doa4::costumes::GuardState* state = g_state.load(std::memory_order_acquire);
  uint32_t marked = 0;
  if (state) {
    for (const doa4::costumes::ModelSet& set : state->empty_slot) marked += set.size();
  }
  return marked;
}

uint64_t doa4::suits::FurPassesSkipped() { return g_fur_skips.load(std::memory_order_relaxed); }

void doa4::suits::InstallLabelTable(uint32_t table) {
  g_label_table.store(table, std::memory_order_release);
  if (table && g_label_trace) REXLOG_WARN("[doa4][SUITS] test: label trace ON (DOA4_SUITS_LABEL_TRACE=1)");
}

uint32_t doa4::suits::InstalledLabelTable() { return g_label_table.load(std::memory_order_acquire); }

REX_HOOK_RAW(sub_82528190) {
  const uint32_t table = g_label_table.load(std::memory_order_acquire);
  if (table == 0) {
    __imp__sub_82528190(ctx, base);
    return;
  }
  // The measured invariant (no game code writes the pointer array: Task 9b fix 1 census), checked at
  // run time: rewriting to 197+ is only safe while the word still names the extended table.
  if (Be32(base, doa4::labels::kSheetPointer) != table) {
    if (!g_label_table_lost.exchange(true)) {
      REXLOG_WARN("[doa4][SUITS] costume labels: the sheet-6 sprite table pointer is {:#x}, no longer the extended "
                  "table {:#x}; labels past COS. 10 are left to the game",
                  Be32(base, doa4::labels::kSheetPointer), table);
    }
    __imp__sub_82528190(ctx, base);
    return;
  }
  const doa4::labels::ListingScope listing;
  __imp__sub_82528190(ctx, base);
  if (g_label_trace) TraceListEnd();
}

REX_HOOK_RAW(sub_823BF210) {
  if (doa4::labels::Listing()) {
    const doa4::GuestBytes guest{[base](uint32_t address) { return base[address]; },
                                 [base](uint32_t address, uint8_t value) { base[address] = value; }};
    const bool rewritten = doa4::labels::RewriteDrawStruct(guest, ctx.r4.u32);
    if (rewritten && g_label_trace) TraceRewrite();
  }
  __imp__sub_823BF210(ctx, base);
}

// The frame flush's two sprite cases (command in r5, sprite id at +32): trace only.
REX_HOOK_RAW(sub_823C1280) {
  if (g_label_trace) TraceFlush(0, Be32(base, ctx.r5.u32 + 32));
  __imp__sub_823C1280(ctx, base);
}

REX_HOOK_RAW(sub_823C15E8) {
  if (g_label_trace) TraceFlush(1, Be32(base, ctx.r5.u32 + 32));
  __imp__sub_823C15E8(ctx, base);
}

REX_HOOK_RAW(sub_823FE870) {
  const doa4::costumes::GuardState* state = g_state.load(std::memory_order_acquire);
  const doa4::GuestBytes guest{[base](uint32_t address) { return base[address]; }, {}};
  const doa4::costumes::LoadDecision d = doa4::costumes::DecideLoad(state, guest, ctx.r4.u32, ctx.r5.u32);
  if (d.use != d.asked) {
    ctx.r5.u64 = d.use;
    if (g_model_logs.fetch_add(1, std::memory_order_relaxed) < 8) {
      if (d.rule == doa4::costumes::GuardRule::kEmptySlot) {
        REXLOG_WARN(
            "[doa4][SUITS] load request: charId {:#x} model {} is an empty costume slot (no suit imported for it); "
            "model {} instead",
            d.character, d.asked, d.use);
      } else {
        REXLOG_WARN("[doa4][SUITS] load request: charId {:#x} model {} is past her {} records; model {} instead",
                    d.character, d.asked, state->records[d.character], d.use);
      }
    }
  }
  __imp__sub_823FE870(ctx, base);
}

// The morph dispatcher (r3 = a morph state, +0 = its bound MPP block) runs for every morph of every fighter each frame,
// before the appliers write the pose into the vertices. A converted suit's bust block is marked by the converter; moving
// its poses here makes the Extras checkbox take effect at once. Vanilla blocks fail the marker check and are not touched.
// Test switch DOA4_SUITS_BUST_FLIP_S=<seconds>: alternates the setting on that period, read once.
namespace {

struct GuestMem {
  uint8_t* base;
  uint32_t ReadU32(uint32_t a) const { return rex::memory::load_and_swap<uint32_t>(base + a); }
  void WriteU32(uint32_t a, uint32_t v) { rex::memory::store_and_swap<uint32_t>(base + a, v); }
};

std::mutex g_bust_mutex;
std::atomic<int> g_bust_logs{0};

bool FullBustWanted() {
  static const int flip = [] {
    const char* env = std::getenv("DOA4_SUITS_BUST_FLIP_S");
    return env ? std::atoi(env) : 0;
  }();
  if (flip > 0) {
    using namespace std::chrono;
    return (duration_cast<seconds>(steady_clock::now().time_since_epoch()).count() / flip) % 2 != 0;
  }
  return rex::cvar::Query<bool>("doa4_suits_full_bust");
}

}  // namespace

REX_HOOK_RAW(sub_82373E50) {
  const uint32_t state = ctx.r3.u32;
  const uint32_t mpp = state ? Be32(base, state) : 0;
  if (mpp >= 0x10000000 && mpp < 0xC0000000) {
    GuestMem mem{base};
    namespace bust = doa4::suits::bust;
    if (bust::IsMarked(mem, mpp)) {
      const bool full = FullBustWanted();
      std::lock_guard<std::mutex> lock(g_bust_mutex);
      if (bust::Rescale(mem, mpp, full) && g_bust_logs.fetch_add(1, std::memory_order_relaxed) < 40) {
        REXLOG_INFO("[doa4][SUITS] bust strength: block {:#x} now {}", mpp, full ? "full DOAX2 range" : "the file's own");
      }
      const bust::Blend blend = bust::ReadBlend(mem, state);
      bust::FollowPins(mem, mpp, blend);
      static const bool trace = std::getenv("DOA4_SUITS_PIN_TRACE") != nullptr;
      static std::atomic<uint64_t> calls{0};
      if (trace) {
        const uint64_t n = calls.fetch_add(1, std::memory_order_relaxed);
        if (n % 600 == 0) {
          REXLOG_INFO("[doa4][SUITS] pins: block {:#x} count {} poses {} {} {} weights {} {} {} list {} -> down {} up {}", mpp, mem.ReadU32(mpp + 0x44), int32_t(mem.ReadU32(state + 12)), int32_t(mem.ReadU32(state + 16)), int32_t(mem.ReadU32(state + 20)),
                      std::bit_cast<float>(mem.ReadU32(state + 44)), std::bit_cast<float>(mem.ReadU32(state + 48)), std::bit_cast<float>(mem.ReadU32(state + 52)), mem.ReadU32(state + 76), blend.down, blend.up);
        }
      }
    }
  }
  __imp__sub_82373E50(ctx, base);
}

// sub_823701F0(slot r3, bits r4): sub_8234F030 calls it with bit 1 at its two budget sites (lr 0x8234F308, 0x8234F314) when 3 fighters run, or 2 with a
// cost sum over 37000; cloths with PROPERTY 0x200 then have no collision. A vanilla Kasumi mirror match goes over (74432). This port runs on PCs that have the time, so
// those two calls are skipped. The other callers go through. DOA4_SUITS_NATIVE_CLOTH_BUDGET=1 keeps the game's rule (A/B tests). Online both sides must agree.
// Test switch DOA4_SUITS_CLOTH_TRACE=1, read once: logs calls; r29 still holds the caller's cost sum at this entry.
REX_HOOK_RAW(sub_823701F0) {
  static const bool trace = std::getenv("DOA4_SUITS_CLOTH_TRACE") != nullptr;
  if (trace) {
    static std::atomic<uint64_t> calls{0};
    const uint64_t n = calls.fetch_add(1, std::memory_order_relaxed);
    if (n < 8 || n % 1200 == 0) {
      REXLOG_INFO("[doa4][SUITS] cloth budget: call {} lr {:#x} slot {} bits {} cost sum {} fighters {}", n, uint32_t(ctx.lr), ctx.r3.s32, ctx.r4.u32, ctx.r29.u32, ctx.r27.s32);
    }
  }
  static const bool native_budget = std::getenv("DOA4_SUITS_NATIVE_CLOTH_BUDGET") != nullptr;
  const uint32_t lr = uint32_t(ctx.lr);
  if (!native_budget && (lr == 0x8234F308 || lr == 0x8234F314)) {
    return;
  }
  __imp__sub_823701F0(ctx, base);
}

REX_HOOK_RAW(sub_82455E08) {
  const uint32_t entry = ctx.r3.u32;
  if (entry && FurGuardOn() && Be32(base, entry + 308) == 0) {
    if (g_fur_skips.fetch_add(1, std::memory_order_relaxed) == 0) {
      REXLOG_WARN("[doa4][SUITS] fur pass skipped: shell-pass entry {:#x} was never set up (a costume .cat "
                  "without a type-6 block)",
                  entry);
    }
    return;
  }
  __imp__sub_82455E08(ctx, base);
}

// Scene setup (r3 = scene, r4 = id, r5 = flag): four fighter entries of 6 bytes at scene + 4:
// {u8 character, u8 model, ...}; 0xFF = unused. The scene pointer is r3 on ENTRY, read BEFORE the
// original runs, as the stage-0 probe did. Why that is the scene: the callee itself never reads r3
// (its first use is `li r3,4`, recomp.5.cpp:54456; it writes the scene global 0x830369F4 through
// r30), and all six callers load that global into r3 right before the call (`lis -31997` +
// `addi 27124`, then `mr r3,rN`): recomp.5.cpp:52078, 54818, 54973 and recomp.9.cpp:23455, 25438,
// 26251. So r3 on entry is the scene (the S2 log prints it: 0x830369f4), while r3 after the call
// is the callee's scratch (recomp.5.cpp:54456, 54575, 54673) and is not.
REX_HOOK_RAW(sub_823841D0) {
  const uint32_t scene = ctx.r3.u32;
  __imp__sub_823841D0(ctx, base);
  if (Forces().empty()) return;
  for (const SceneForce& f : Forces()) {
    if (f.entry >= 4) continue;
    const uint32_t e = scene + 4 + 6 * f.entry;
    if (base[e] == 0xFF) continue;
    const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, lr = ctx.lr;
    ctx.r3.u64 = f.character;
    ctx.r4.u64 = f.costume;
    ctx.r5.u64 = 0;
    sub_82526B78(ctx, base);
    const uint32_t model = ctx.r3.u32 & 0xFF;
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    ctx.r5.u64 = r5;
    ctx.lr = lr;
    base[e] = uint8_t(f.character);
    base[e + 1] = uint8_t(model);
    REXLOG_INFO("[doa4][SUITS] test: scene {:#x} entry {} = charId {:#x} costume #{} -> model {}", scene,
                f.entry, f.character, f.costume, model);
  }
}
