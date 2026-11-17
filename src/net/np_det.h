// DOA4 netplay phase 0: the determinism layer, active only in a session.
// Each fix is switched by DOA4_NP_FIX (a comma list, "all" or "none"; the
// default in a session is "all"), so the divergence a fix removes can be shown
// on one build by running with and without it. The fixes and the evidence for
// each are listed in np_det.cpp.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/ppc/context.h>

namespace doa4::np::det {

// Whether this session may use the "filetimes" fix (np_core.h SessionFixes): true for an online session and for a
// replay of an online session's recording, false otherwise (the default). Set before FixSetName and Start.
void AllowFileTimes(bool allow);
// The inputs of the decision which order the job workers run (np_lanes_pure.h DecideMode; design section 4): called at both session
// starts BEFORE CurrentMeta, whose "workers=" line is the decision's result (the decision is made once a process, at the first ask).
// meta_workers: a replay's recording's MetaValue(meta, "workers"); ignored unless replay. The workers fix is computed as Start will
// compute it (Start has not run yet). A decision asked for before this call decides without it and says so in a WARN line.
void SetWorkerInputs(bool online, bool handoff_serial, bool replay, const std::string& meta_workers);
// The meta value of the decided order: "lanes", "serial" or "mode<n>" (lanes::MetaWorkers). Decides, if nothing has yet.
std::string WorkersMeta();
// Called by Start when a session begins (before guest code runs).
void Start(uint64_t seed, const std::filesystem::path& dir);
// The frame hook's checks of worker mode 4 (P17: placement complete, no worker inside a section, the four section objects and their
// verdict), called at the top of the session's frame step for the frame about to run, before the end request is read and before
// anything is hashed. Returns at once in every other mode and with no session.
void PreFrame(uint32_t frame, uint8_t* base);
// Worker mode 4's end request (design 6): true once one was raised; *cause gets its text.
bool EndRequested(std::string* cause);
// Called by the frame hook (main thread, loop head) with the frame about to run.
void OnFrame(uint32_t frame, PPCContext& ctx, uint8_t* base);
// Called when the session ends (the game keeps running until it exits): the layer stops holding
// threads to the frame boundary and switches off everything Start switched on (lockstep audio and
// XMA, inline completions, headless dialogs, the clock override). Idempotent.
void Stop();

// The layer's counters (np_det_internal.h): names in file order, and their values now.
const std::vector<std::string>& CounterNames();
void CounterValues(std::vector<uint32_t>* out);

// The fix names this session uses (from DOA4_NP_FIX, filetimes only if AllowFileTimes), comma separated in fix
// order, or "none": as Start logs them and the recording's meta holds them. Reads the environment, so it answers
// the same before and after Start.
std::string FixSetName();

// The "keepalive" fix for sub_8275AEE0, whose hook lives in doa4_photo_hook.cpp (sub_8275AD70's is in np_det.cpp):
// true = the caller returns at once (the D3D kick thread's timeout re-present, in a session with the fix on).
// False outside a session, so the hook runs the original exactly as before. lr = the call's return address.
bool SkipKeepAliveSwap(uint32_t lr);

}  // namespace doa4::np::det
