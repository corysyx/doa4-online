// The netplay starting rules' hook (small fixes 2, Part 2; the rules and the reasons: net/np_start_rules.h).
//
// sub_823F70A0 is the Versus match-rule load (generated dead_or_alive_4_recomp.9.cpp; reached through a table, after the
// stage is chosen): it reads P = be32 0x83035CC8 and the options at P+0x14..0x18, then sub_823E39F0 writes the match's
// values (notes/research/sf2_match_options.md, section 3). It is guest main-thread work inside a game frame, so in a
// live session it runs at the same frame on both sides, on the same memory.
//
// Outside an np session (DOA4_NP unset, np::Active false) the hook only calls the game's function: offline play is
// unchanged. In any np session (harness record and replay runs, and live netplay) the match-rules line is logged after
// the game's function has returned, at every load (ruling S15). In a LIVE session only, when the session process was
// armed (its save does not already hold the rules: ArmMemoryWrite, decided once in dead_or_alive_4_app.h
// RedirectUserData from the hand-off's files, which both sides hold byte-equal), the first load of the session writes
// the three option bytes at P before the game reads them, and no later load writes again (ruling S14 d). Tag's 99 s is
// the game's own constant: no code.

#include <atomic>
#include <cstdint>
#include <span>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/ppc/context.h>

#include "net/np_session.h"
#include "net/np_start_rules.h"

extern "C" {
REX_EXTERN(__imp__sub_823F70A0);
}

namespace {
namespace sr = doa4::np::start_rules;

std::atomic<bool> g_armed{false};
std::atomic<bool> g_written{false};  // the session's one write has been made (or tried)

uint32_t Be32(const uint8_t* base, uint32_t a) {
  return (uint32_t(base[a]) << 24) | (uint32_t(base[a + 1]) << 16) | (uint32_t(base[a + 2]) << 8) | uint32_t(base[a + 3]);
}
uint16_t Be16(const uint8_t* base, uint32_t a) { return uint16_t((uint32_t(base[a]) << 8) | uint32_t(base[a + 1])); }
}  // namespace

namespace doa4::np::start_rules {
void ArmMemoryWrite(bool armed) { g_armed.store(armed, std::memory_order_release); }
}  // namespace doa4::np::start_rules

REX_HOOK_RAW(sub_823F70A0) {
  if (!doa4::np::Active()) {
    __imp__sub_823F70A0(ctx, base);
    return;
  }
  if (doa4::np::Online() && g_armed.load(std::memory_order_acquire) && !g_written.exchange(true)) {
    const uint32_t p = Be32(base, sr::kProfilePointer);
    if (p != 0 && sr::WriteToRecord(std::span<uint8_t>(base + p, sr::kRecordMemoryBytes))) {
      doa4::np::LogLine(fmt::format("start rules: frame {}: written in memory at P {:08X} (+0x{:X} = {}, +0x{:X} = {}, +0x{:X} = {})",
                                    doa4::np::CurrentFrame(), p, sr::kMatchPointSingle, sr::kMatchPointSingleTarget,
                                    sr::kLifeGauge, sr::kLifeGaugeTarget, sr::kRoundTime, sr::kRoundTimeTarget));
    } else {
      doa4::np::LogLine(fmt::format("start rules: frame {}: no profile record (P = 0): not written", doa4::np::CurrentFrame()));
    }
  }
  __imp__sub_823F70A0(ctx, base);
  doa4::np::LogLine(sr::MatchRulesLine(base[sr::kTagFlag] != 0, Be16(base, sr::kLifeValue), base[sr::kRoundsToWin],
                                       base[sr::kTimeSeconds]));
}
