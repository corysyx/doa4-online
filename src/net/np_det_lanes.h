// DOA4 Online lockstep port, task B2: the engine of worker mode 4 ("lanes") in the determinism layer. The rules are np_lanes_pure.h
// (task B1, host-tested); this file is the part that holds threads, reads clocks and guest memory, writes the log lines and counts.
// np_det_workers.cpp decides the mode and calls in from its hooks; np_det.cpp hands it the frame (PreFrame, FrameStep) and the end.
// Design: notes/specs/2026-11-16-doa4-lockstep-port-design.md, sections 2 to 8.
//
// Locks: one engine lock (the placement, the turns, the session's lanes counts, the frame's lane starts). It is never held across an
// original guest call, a sleep, a log line, the end request's Set or the SDK call that makes a section object: every function below
// copies what it needs to log or raise under the lock and does both after leaving it. The pass's counters and the cpu counts have a
// mutex each, taken alone. RaiseEnd is never called with the engine lock held.

#pragma once

#include <cstdint>
#include <string>

#include <rex/ppc/context.h>

#include "np_lanes_pure.h"

namespace doa4::np::det::eng {

// At the decision (np_det_workers.cpp), once a process, before any guest thread: the decided mode, an online session or not, and the
// test switch DOA4_NP_LANES_TEST as parsed (nothing set when it was refused).
void Configure(int mode, bool online, const lanes::LanesTest& test);

// ---- the placement (mode 4; mode 3 as an observer, P13) ----
// A worker's first SignalObjectAndWait call; done and wait are its r3 and r4, read before the original call. rank: its place in the
// creation list, or -1 with why_not (a thread id 0, an id twice in the list, an id not in it). At the 25th placement the chains are
// worked out from the pool's start events (read from base) and the placement line is written; in mode 4 a mismatch, or the test value
// refuse=placement, raises an end request.
void Place(int rank, const char* why_not, uint32_t tid, uint32_t entry, uint32_t done, uint32_t wait, const uint8_t* base);

// ---- mode 4, a worker ----
void WorkerEnd(int w, uint32_t done, uint32_t wait);  // a later call, before the original: Same, then Turns::End
void WorkerWake(int w);                               // after the original returned: its turn, then Begin
void MidWaitBegin(int w);                             // its wait for the pool's mid event, before the original call
void MidWaitEnd(int w);                               // and after it
void StallTest(int w);                                // stall=<w>@<frame>:<ms>: sleeps once, holding no lock

// ---- mode 4, the main thread ----
void MainReachedMid();          // before the mid SetEvent's original call
uint64_t MainWait(bool second);  // the first (after the start events) or second (after the mid event) wait; returns its microseconds
void MainJoinBegin();           // before the join's original call
void MainJoinEnd();             // after it

// ---- the frame ----
// The pre-frame step (P17): mode 4 only, on the game thread at the top of the session's frame step, before the end request is read and
// before anything is hashed. `frame` is the frame about to run.
void PreFrame(uint32_t frame, uint8_t* base);
// WorkersFrame's part, inside det::OnFrame (modes 3 and 4): the cpu marks folded; in mode 4 the frame's counts folded, Turns::FrameReset;
// every 600 frames the lines (the lanes: line's "lanes beside main" is the session's largest Turns::MaxLaneBesideMain). Raises no end
// request.
void FrameStep(int mode, uint32_t frame, const uint8_t* base);
// det::Stop (through WorkersStop): wakes every waiter of the engine, then writes the last lanes:, vsm: and cpu: lines (by the mode).
void Stop(int mode);

// ---- mode 4, the vertex-stream pass at the manager's frame reset (sub_82732950, after the original call) ----
void VsmAfterReset(PPCContext& ctx, uint8_t* base, uint32_t manager, uint32_t lr);

// ---- the end request (design 6) ----
// Logs "lanes end request at frame <f>: <text>" on every call; the first caller's request is the session's: it counts fb_lane_end and,
// offline, writes the WARN line. Wakes every waiter. Never with the engine lock held; cause never kNone. <f> is `frame` when it is 0 or
// more (PreFrame passes the frame it was called with, which det::OnFrame has not stored yet: review M2), else det's current Frame().
void RaiseEnd(lanes::EndCause cause, const std::string& text, int64_t frame = -1);
bool EndRequested(std::string* cause);

// ---- the cpu: line's marks (modes 3 and 4) ----
void CpuSectionBegin();        // a worker, when its section begins (mode 3: the token taken)
void CpuSectionEnd();          // a worker, when its section ends (mode 3: at the call that hands the token on)
void CpuMainWait(uint64_t us);  // the main thread: one of its two waits, or the join

}  // namespace doa4::np::det::eng
