// The game's text and subtitles language at run time (update G1). The ids, addresses and rules are doa4_language.h (pure,
// host-tested); this file applies and notes them.
//
// sub_82305BA8 (generated/default/dead_or_alive_4_recomp.0.cpp:49138) is the boot writer: it calls XGetLanguage (always
// English in this port) and stores the one id into 0x82FCC7C6, 0x82FCC7AF and 0x82FCC7C7 (its stb lines at
// recomp.0.cpp:49261, 49265, 49269), then returns. The hook reads and writes AFTER the original, so after those stores.
// Its only caller, sub_823069B8 (recomp.0.cpp:51165), goes on to sub_8230C840 -> sub_82306318, whose copy of 0x82FCC7AF
// into 0x82FCC93F (recomp.0.cpp:50335-50338) is the first read of the text byte we know of: it follows the hook's apply
// on the same thread. Step 1's probe (ou_runs/t3/runs/probe1) found the call once per process, on the game thread,
// inside the first frame-function call; nothing else wrote the three bytes before the options screen (no change at any
// frame-function call from the first after it to frame 16210). The first frame after the boot apply re-checks that.
//
// sub_8251D748 (generated/default/dead_or_alive_4_recomp.19.cpp:18172) is the options screen's language handler, called
// once per frame while that screen is shown, on the game thread (probe: 1320 calls over 1320 frames, one thread). It
// writes the text to 0x82FCC7AF and 0x82FCC93F and the subtitles to 0x82FCC7C7 (recomp.19.cpp:18351-18355 and
// 18386-18396). The hook reads AFTER the original, so after those stores, and notes a changed pair for the UI thread to
// save; never in a session or a replay.

#include "doa4_language_hooks.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "generated/default/dead_or_alive_4_init.h"

#include "doa4_guest.h"
#include "net/np_session.h"

extern "C" {
REX_EXTERN(__imp__sub_82305BA8);
REX_EXTERN(__imp__sub_8251D748);
}

namespace doa4::language {
namespace {

GuestBytes Guest(uint8_t* base) {
  return GuestBytes{[base](uint32_t a) { return base[a]; }, [base](uint32_t a, uint8_t v) { base[a] = v; }};
}

std::string Shown(const std::optional<uint8_t>& v) { return v ? std::to_string(*v) : std::string("game"); }

struct ThisRun {
  Saved saved;
  const char* source = "settings";
  std::string note;  // the ignored values, if any
};

// The latch (#475): the first call takes the values, every later call returns the same. The session mode is fixed by
// np::Start (OnPostLoadXexImage) before any guest code, so the boot hook's first call already sees it.
const ThisRun& Run() {
  static const ThisRun run = [] {
    ThisRun r;
    if (np::Online() || np::Replaying()) {
      r.source = np::Online() ? "hand-off" : "recording";
      r.saved = ResolveText(np::SessionGuestValue("doa4_language"), np::SessionGuestValue("doa4_subtitles"), &r.note);
    } else {
      r.saved = Resolve(rex::cvar::Query<int32_t>("doa4_language"), rex::cvar::Query<int32_t>("doa4_subtitles"), &r.note);
    }
    return r;
  }();
  return run;
}

// Set by the boot hook: the next frame-function call checks that the applied values still hold.
std::atomic<bool> g_check_boot{false};

}  // namespace

Saved SavedForThisRun() { return Run().saved; }

namespace {

// An online session, a replay or a recording: the dropdown's request is dropped there (the dropdown is locked then, and a
// recording's guest must not change at a wall-clock moment). DOA4_EXTRAS_REQUEST is the one exception, as in the app's
// SyncCostumeCounts: a record run stands in for an offline game because only it takes scripted input, and
// np::TestSwitch is null for a player. A recording made with the `language N` test action holds no trace of the pick, so
// its replay (which always drops it) shows the game's own language from that point.
bool SessionActive() {
  static const bool offline_stand_in = np::TestSwitch("DOA4_EXTRAS_REQUEST") != nullptr;
  return np::Online() || np::Replaying() || (np::Active() && !offline_stand_in);
}

void CheckBoot(uint8_t* base) {
  if (!g_check_boot.load(std::memory_order_relaxed)) return;
  if (!g_check_boot.exchange(false, std::memory_order_acquire)) return;
  const GuestBytes g = Guest(base);
  const Saved& want = Run().saved;
  const Seen seen = Read(g);
  if ((want.text && seen.text != *want.text) || (want.subtitles && seen.subtitles != *want.subtitles)) {
    // a writer the probe did not see ran between the boot apply and this frame: the frame's apply wins
    REXLOG_WARN("[doa4][LANG] first frame: text {} subtitles {} overwrote the boot values; applied again", seen.text, seen.subtitles);
    Apply(g, want);
    SetBaseline(Read(g));
  } else {
    REXLOG_INFO("[doa4][LANG] first frame: text {} subtitles {} (the boot values hold)", seen.text, seen.subtitles);
  }
}

}  // namespace

void OnFrame(uint8_t* base) {
  CheckBoot(base);  // first: a live request on the same frame must not read as an overwrite of the boot values
  if (!LiveRequestPending()) return;  // the idle case: one atomic load
  if (const std::optional<uint8_t> live = ApplyLiveRequest(Guest(base), SessionActive())) {
    REXLOG_INFO("[doa4][LANG] live text {} (Esc menu)", *live);
  }
}

}  // namespace doa4::language

REX_HOOK_RAW(sub_82305BA8) {
  __imp__sub_82305BA8(ctx, base);
  namespace lang = doa4::language;
  const doa4::GuestBytes g = lang::Guest(base);
  const lang::ThisRun& run = lang::Run();
  lang::Apply(g, run.saved);
  lang::SetBaseline(lang::Read(g));  // what the bytes hold now is no choice of the player's
  REXLOG_INFO("[doa4][LANG] boot: text {} subtitles {} (from {})", lang::Shown(run.saved.text), lang::Shown(run.saved.subtitles),
              run.source);
  if (!run.note.empty()) REXLOG_INFO("[doa4][LANG] boot: ignored ({}): the game's own value instead", run.note);
  lang::g_check_boot.store(true, std::memory_order_release);
}

REX_HOOK_RAW(sub_8251D748) {
  __imp__sub_8251D748(ctx, base);
  // The gate is asked again on every call and holds nothing across calls (#324): a session or a replay never saves.
  if (doa4::np::Online() || doa4::np::Replaying()) return;
  doa4::language::NoteGameChoice(doa4::language::Read(doa4::language::Guest(base)));
}
