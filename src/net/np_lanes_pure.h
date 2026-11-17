// DOA4 Online lockstep port, task B1: the pure rules of worker mode 4 ("lanes") in the player build (no SDK, no guest includes:
// tests/host/np_lanes_host_test.cpp compiles np_lanes_pure.cpp with plain clang-cl through run_np_lanes_test.bat). Design:
// notes/specs/2026-11-16-doa4-lockstep-port-design.md, sections 2 to 4 and 6 to 8; the plan's task B1 gives every interface below.
//
// What is here: the one fixed table (the 25 workers' entry points, the five chains the game links them into, the two lanes, the four
// critical sections), the placement check that compares the game's chains with the table, the rules of a frame (Turns, ported from the
// measurement branch's sched::Turns for main=lanes and this table only), the stall clock, the end request, the decision which order a
// session runs, the test switch DOA4_NP_LANES_TEST, the main thread's wait histogram, the up-front critical section decision (copied
// from the measurement branch) and its verdict, and the log line makers. Nothing here reads a clock, a thread, a file or the
// environment; the callers (task B2) do, and call Turns and Placement under one lock.
//
// What is NOT here, on purpose (design section 3, "Changed or dropped"): a forced begin, a main thread that goes on after a time-out, the
// join opening the lane turns in a frame without the mid event, a schedule text, a run-time cycle check, rng=lazy, main=wait/free.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace doa4::np::lanes {

// ---- the fixed table (design 2.1 to 2.4) ----
constexpr int kWorkers = 25;
constexpr int kChains = 5;
constexpr int kLanes = 2;
constexpr int kMaxChainLen = 7;
constexpr int kMaxLaneLen = 12;
constexpr int kSectionCount = 4;
constexpr const char* kTableId = "s6v1";

// kEntryOf[w]: worker w's thread entry point (w = creation rank). kChain[c][p]: chain c's p-th worker (-1 past kChainLen[c]).
// kLane[l][k]: lane l's k-th worker (-1 past kLaneLen[l]); workers 20 to 24 are in no lane (free). kSections: the guest critical
// sections whose host objects are made up front, in this order; the fourth is the vertex-stream manager's.
extern const uint32_t kEntryOf[kWorkers];
extern const int kChainLen[kChains];
extern const int kChain[kChains][kMaxChainLen];
extern const int kLaneLen[kLanes];
extern const int kLane[kLanes][kMaxLaneLen];
extern const uint32_t kSections[kSectionCount];

// True when the relation "a before b", over every pair of neighbours on every chain and in every lane, has no cycle (and every index
// is 0 to 24). Takes the tables as arguments so a test can hand it a wrong one.
bool TablesAcyclic(const int (*chains)[kMaxChainLen], const int* chain_len, const int (*lanes)[kMaxLaneLen], const int* lane_len);

// ---- the placement (design 2.2; P14: a worker's index is its creation rank) ----
// Set(index, entry, done, wait): worker `index`'s entry point and the two handles of its first SignalObjectAndWait (the event it
// signals when done, the event it waits for). Any order, once an index; a second Set of one index is a mismatch (the first values
// stay). Resolve(start), once Complete(): a worker whose wait is start[c] is chain c position 0; a worker whose wait is another
// worker's done follows it. It ends after at most 25 passes whatever the input. Mismatch(): nullptr only when all 25 are set, every
// worker has a chain, and chain, position and entry point of every index equal kChain and kEntryOf; otherwise a short text. A worker
// set twice and an index outside 0 to 24 are reported first, before the scan over the indexes, so they are named even when a lower
// index also differs: "worker <index>: set twice" for the first index set twice, else "a worker index outside 0 to 24", which names
// no index. Otherwise the text begins "worker <index>: " for the first index that differs and says what differs. The text lives until
// the next call on the object.
struct Placement {
  void Set(int index, uint32_t entry, uint32_t done, uint32_t wait);
  bool Complete() const;
  void Resolve(const uint32_t start[kChains]);
  const char* Mismatch() const;
  // True when worker `index` was set with these two handles (a later call with other handles is a frame failure, design 6).
  bool Same(int index, uint32_t done, uint32_t wait) const;
  // After Resolve: the chain and position worked out for `index`; -1 when none.
  int ChainOf(int index) const;
  int PosOf(int index) const;

 private:
  bool set_[kWorkers] = {};
  uint32_t entry_[kWorkers] = {}, done_[kWorkers] = {}, wait_[kWorkers] = {};
  int chain_[kWorkers] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  int pos_[kWorkers] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  int twice_ = -1;          // the first index set twice
  bool bad_index_ = false;  // a Set with an index outside 0 to 24
  mutable std::string why_;
};

// ---- the rules of a frame (design section 3; wd:np_rb0_sched_pure.cpp:293-422 for main=lanes and this table) ----
// One object a session; every method is called under one lock (task B2).
class Turns {
 public:
  Turns();
  // A new frame (the frame hook, when no worker runs): no worker has ended, the mid and join marks are clear, the frame's two maxima
  // start again. A section still open stays open (AnyInSection, Running).
  void FrameReset();
  // Worker w's section may begin: a free worker (20 to 24) always; a lane worker when every worker before it in its lane has ended this
  // frame. Nothing opens at the join (rule 5, P19).
  bool MayRun(int w) const;
  // The section begins (after MayRun). There is no forced begin.
  void Begin(int w);
  // The section ends (the worker's next SignalObjectAndWait): w has ended this frame. True when the section ended in a frame that had
  // the mid event without w ever having been at its job in it ("ended unseen"; a frame failure, design 6). False when no section was
  // open.
  bool End(int w);
  // Worker w waits for / has got the pool's mid event inside its section; the return makes it "at its job".
  void MidWaitBegin(int w);
  void MidWaitEnd(int w);
  // The main thread reached the mid SetEvent (before the call).
  void MainReachedMid();
  // The main thread reached the join: sets the join mark and returns whether this frame had the mid event (false: rule 5's end
  // request). It opens no turn.
  bool MainReachedJoin();
  // The main thread waits (its first or second wait, the join) or runs its own code.
  void MainWaiting(bool waiting);
  // Rule 3: on every chain, the first worker that has not ended this frame waits for the mid event, or is held by its lane, or the
  // chain has all ended.
  bool FirstWaitReady() const;
  // Rule 4: every lane worker has ended this frame (the free workers may still run).
  bool SecondWaitReady() const;
  // Workers inside a section, of all 25, the free ones included (not the number at their job).
  int Running() const { return running_; }
  // This frame's most workers at their job at once, and at once while the main thread ran its own code before the join.
  int MaxAtOnce() const { return max_at_once_; }
  int MaxBesideMain() const { return max_beside_; }
  bool AnyInSection() const { return running_ > 0; }
  // The first worker before w in its lane, in lane order, that has not ended; -1 when none or w is free.
  int BlockerOf(int w) const;
  // The chain head that makes FirstWaitReady false, lowest chain first; -1 when ready.
  int FirstWaitBlocker() const;
  // The first lane worker that has not ended, lane 1 before lane 2, in lane order; -1 when ready.
  int SecondWaitBlocker() const;

 private:
  void Note();

  int lane_of_[kWorkers] = {}, lane_pos_[kWorkers] = {};
  bool in_section_[kWorkers] = {}, ended_[kWorkers] = {}, mid_waiting_[kWorkers] = {}, at_job_[kWorkers] = {};
  int running_ = 0, at_job_count_ = 0;
  bool mid_ = false, join_ = false, main_waiting_ = false;
  int max_at_once_ = 0, max_beside_ = 0;
};

// ---- the waiting time (design 6, "How the waiting time is counted"; P18) ----
// One clock a wait; Reset at the wait's start. The waiter calls Waited each time its 50 ms slice returns with its condition still false
// (by time-out or by a wake), with the steady-clock milliseconds since its previous call (or since Reset). The clock adds
// min(elapsed_ms, 100), a negative value as 0. kLogStall on the one call that takes the count from below 2000 to 2000 or more; kEnd on
// every call that leaves the count at 8000 or more; kKeepWaiting otherwise. The pure code reads no clock.
struct StallClock {
  enum class Step : uint8_t { kKeepWaiting, kLogStall, kEnd };
  static constexpr int kSliceMs = 50;
  static constexpr int kMaxStepMs = 100;
  static constexpr int kStallMs = 2000;
  static constexpr int kEndMs = 8000;

  Step Waited(int elapsed_ms);
  void Reset();
  int CountedMs() const { return counted_; }

 private:
  int counted_ = 0;
};

// ---- the end request (design 6) ----
enum class EndCause : uint8_t { kNone, kPlacement, kSection, kFrame, kStall, kVsm, kTest };
// First writer wins: Set returns true for exactly one caller and stores its cause and text. Safe from any thread.
class EndRequest {
 public:
  bool Set(EndCause cause, std::string text);
  bool Requested() const { return requested_.load(std::memory_order_acquire); }
  EndCause Cause() const;
  std::string Text() const;

 private:
  mutable std::mutex mu_;
  std::atomic<bool> requested_{false};
  EndCause cause_ = EndCause::kNone;
  std::string text_;
};

// ---- which order a session runs (design section 4) ----
struct ModeInputs {
  const char* serial_switch = nullptr;  // DOA4_NP_SERIAL's text (null when unset)
  bool workers_fix_on = true;
  bool online = false;
  bool handoff_serial = false;          // the hand-off's worker_order is serial
  bool replay = false;
  const char* meta_workers = nullptr;   // the recording's meta "workers=" value (null when the line is missing)
};
struct ModeChoice {
  int mode = 4;
  bool refused = false;
  std::string why;  // why an input was refused (empty when none was)
};
// In this order: (1) the switch, when not null and not empty: exactly "0" to "4" gives that mode, any other text is refused; (2) the
// workers fix off: 0; (3) online: 3 if handoff_serial, else 4; (4) a replay: meta "lanes" 4, "serial" 3, anything else refused; (5)
// an offline record: 4. A refused choice carries the mode rules (2) to (5) give without the refused input, 3 where rule (4) refused.
ModeChoice DecideMode(const ModeInputs& in);
// The meta value for a mode: "lanes" (4), "serial" (3), "mode<n>" otherwise.
std::string MetaWorkers(int mode);

// ---- the test switch DOA4_NP_LANES_TEST (design section 8) ----
// Fields joined by ';', each at most once, any order: vsm=hash, refuse=placement, stall=<w>@<frame>:<ms> (w 0 to 24, frame 0 or more,
// ms 1 to 20000; decimal with no sign, blank or leading zero; a number of more than 9 digits is refused). Null or empty: ok with nothing
// set. Anything else: not ok, nothing set, error names the field.
struct LanesTest {
  bool ok = true;
  std::string error;
  bool vsm_hash = false;
  bool refuse_placement = false;
  int stall_worker = -1, stall_frame = -1, stall_ms = 0;
};
LanesTest ParseLanesTest(const char* text);

// ---- the main thread's wait a fight frame, in microseconds (the cpu: line) ----
// 64 buckets, bucket min(us / 250, 63). A percentile is the upper bound (b + 1) * 250 of the first bucket at which the running count
// reaches ceil(p * count); if that bucket is 63 it is the largest sample. Empty: all 0.
class WaitHistogram {
 public:
  void Add(uint64_t us);
  uint64_t P50Us() const;
  uint64_t P99Us() const;
  uint64_t MaxUs() const { return max_; }
  uint64_t Count() const { return count_; }

 private:
  uint64_t Percentile(uint64_t num, uint64_t den) const;
  uint64_t bucket_[64] = {};
  uint64_t count_ = 0, max_ = 0;
};

// ---- the four critical sections' host objects, made up front (design 2.4; copied from wd:np_rb0_sched_pure.h:98-120) ----
// The first contention on a critical section makes the SDK create a host object for it (RtlEnterCriticalSection -> xeKeWaitForSingleObject
// -> XObject::GetNativeObject, which takes the object's type from the header's type byte) and stash 'REX\0' and the object's handle in
// guest memory at the section's address + 8 and + 12. When that happens depends on host timing; np_rb0_cseager.cpp makes the object
// itself, through the same SDK call, at a fixed point. Before it does, DecideCs judges the values read there:
//   type: the dispatcher header's type byte (+0); lock: the lock count (+0x10, a host-order int32, as the SDK keeps it); w8, w12: the
//   two big-endian guest words at +8 and +12.
// The SDK's initialisation (xeRtlInitializeCriticalSection) leaves type 1, lock -1, w8 = w12 = 0; an enter makes lock >= 0; only the
// stash writes w8 and w12. So, in this order:
//   type != 1, or lock < -1          -> kRefuse ("not an initialised critical section": the SDK would make an object of whatever type
//                                       the byte names and write a signature into data that is not a section);
//   lock >= 0                         -> kRefuse ("held");
//   w8 != 0 or w12 != 0               -> kAlready (its object exists: make nothing, never a second one);
//   else                              -> kMake.
constexpr uint32_t kStashSignature = 0x52455800;  // 'REX\0' (sdk-rb0 include/rex/system/xobject.h kXObjSignature, StashHandle)
enum class CsAction : uint8_t { kMake, kAlready, kRefuse };
struct CsDecision {
  CsAction action = CsAction::kRefuse;
  std::string why;  // for kRefuse; empty otherwise
  bool not_initialised = false;  // B1 (ledger L30): set only by the first refusal (type != 1, or lock < -1)
};
CsDecision DecideCs(uint8_t type, int32_t lock, uint32_t w8, uint32_t w12);
// After the SDK call: nullptr when it returned an object and +8 holds kStashSignature and +12 a non-zero handle; else why not.
const char* CsAfterMake(bool got_object, uint32_t w8, uint32_t w12);

// A section's state after one try (ledger L30). after_make is CsAfterMake's result, read only when d.action is kMake: kAlready gives
// kAlready; kMake with a null after_make gives kMade; kMake with a text gives kRefused; kRefuse with not_initialised gives
// kNotInitialised; any other kRefuse gives kRefused.
enum class SectionState : uint8_t { kMade, kAlready, kNotInitialised, kRefused };
SectionState StateAfter(const CsDecision& d, const char* after_make);
// True (end the session) on a fight frame when at least one of the four is neither kMade nor kAlready; and on any frame when the
// fourth (the manager's) is kRefused (a section not yet initialised is game state, the same on both PCs; any other refusal is this PC's
// alone). False otherwise.
bool SectionsVerdict(const SectionState state[kSectionCount], bool fight_frame);

// ---- the log lines (the plan's task B1, "The log texts") ----
constexpr int kMainFirstWait = -1;   // StallLine's waiter for the main thread's first wait
constexpr int kMainSecondWait = -2;  // and for its second wait
// 4: "workers: lanes, table s6v1, cs 4, vsm sort" ("vsm hash" when vsm_hash); 3: "workers: serial"; else "workers: mode <n>".
std::string DecisionLine(int mode, bool vsm_hash);
// "workers placed: chain 0: <workers>; ...; chain 4: <workers>; table: yes" or "...; table: NO (<why>)", each chain's workers in
// position order as Resolve worked them out, joined by blanks.
std::string PlacementLine(const Placement& p);
// "section <addr>: made <w8> <w12>", "section <addr>: already <w8> <w12>", "section <addr>: refused: <why>" (kNotInitialised and
// kRefused); each value 8 upper-case hex digits.
std::string SectionLine(uint32_t addr, SectionState state, uint32_t w8, uint32_t w12, const std::string& why);
// "lanes: frames <n>, 2+ at once <n>, most at once <n>, most beside main <n>, stalls <n>, end requests <n>"
std::string LanesLine(uint64_t frames, uint64_t frames_2plus, int most_at_once, int most_beside_main, uint64_t stalls,
                      uint64_t end_requests);
// "cpu: fight frames <n>, wait p50 <x.xx> ms p99 <x.xx> ms max <x.xx> ms, cycles main <n> workers <n>" (ms rounded half up to the
// hundredth from the microseconds)
std::string CpuLine(uint64_t fight_frames, const WaitHistogram& wait, uint64_t cycles_main, uint64_t cycles_workers);
// "lanes stall at frame <f>: <who> has waited 2 s for worker <n>"; who: "worker <n>" for waiter 0 to 24, "main (first wait)" for
// kMainFirstWait, "main (second wait)" for kMainSecondWait.
std::string StallLine(int32_t frame, int waiter, int waited_for);
// "lanes stall over at frame <f> after <ms> ms: lane 1 began <list>; lane 2 began <list>" (each list the workers joined by blanks,
// "none" when empty)
std::string StallOverLine(int32_t frame, int ms, const std::vector<int>& lane1_began, const std::vector<int>& lane2_began);
// "lanes end request at frame <f>: <cause text>"
std::string EndRequestLine(int32_t frame, const std::string& cause_text);
// "WARN lanes: <cause text>: the run is not evidence" (offline only)
std::string EndRequestWarn(const std::string& cause_text);

}  // namespace doa4::np::lanes
