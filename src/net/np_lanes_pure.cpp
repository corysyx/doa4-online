// DOA4 Online lockstep port, task B1: the pure rules of worker mode 4 ("lanes"). See np_lanes_pure.h.

#include "np_lanes_pure.h"

#include <algorithm>
#include <cstdio>

namespace doa4::np::lanes {

// ---- the fixed table (design 2.1 to 2.4) ----
const uint32_t kEntryOf[kWorkers] = {
    0x825414B0u,                                                       // 0 sound
    0x82573A40u, 0x82573A40u, 0x82573A40u, 0x82573A40u, 0x82573A40u,   // 1 to 5 stage (scene drawer)
    0x82573958u,                                                       // 6 stage
    0x825689E8u, 0x825689E8u, 0x825689E8u, 0x825689E8u, 0x825689E8u,   // 7 to 11 stage (scene drawer)
    0x82374990u, 0x82374990u, 0x82374990u, 0x82374990u,                // 12 to 15 morphs
    0x8234A160u, 0x8234A160u, 0x8234A160u, 0x8234A160u,                // 16 to 19 model work
    0x82370638u, 0x82370638u, 0x82370638u, 0x82370638u, 0x82370638u};  // 20 to 24 cloth
const int kChainLen[kChains] = {7, 5, 5, 3, 5};
const int kChain[kChains][kMaxChainLen] = {
    {0, 1, 6, 7, 12, 16, 20}, {2, 8, 13, 17, 21, -1, -1}, {3, 9, 14, 18, 22, -1, -1}, {4, 10, 23, -1, -1, -1, -1}, {5, 11, 15, 19, 24, -1, -1}};
const int kLaneLen[kLanes] = {12, 8};
const int kLane[kLanes][kMaxLaneLen] = {{0, 1, 6, 7, 2, 3, 4, 5, 8, 9, 10, 11}, {12, 16, 13, 14, 15, 17, 18, 19, -1, -1, -1, -1}};
const uint32_t kSections[kSectionCount] = {0x83302240u, 0x8321B178u, 0x83303A68u, 0x835780E0u};

namespace {

std::string Hex8(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08X", v);
  return buf;
}

bool Valid(int w) { return w >= 0 && w < kWorkers; }

// The table's chain and position of worker w (-1, -1 when w is on no chain).
void TableChainPos(int w, int* chain, int* pos) {
  *chain = *pos = -1;
  for (int c = 0; c < kChains; ++c) {
    for (int p = 0; p < kChainLen[c] && p < kMaxChainLen; ++p) {
      if (kChain[c][p] == w) {
        *chain = c;
        *pos = p;
        return;
      }
    }
  }
}

}  // namespace

bool TablesAcyclic(const int (*chains)[kMaxChainLen], const int* chain_len, const int (*lanes)[kMaxLaneLen], const int* lane_len) {
  bool edge[kWorkers][kWorkers] = {};
  const auto add_seq = [&](const int* seq, int len, int cap) {
    if (len < 0 || len > cap) return false;
    for (int k = 0; k < len; ++k) {
      if (!Valid(seq[k])) return false;
      if (k > 0) edge[seq[k - 1]][seq[k]] = true;
    }
    return true;
  };
  for (int c = 0; c < kChains; ++c) {
    if (!add_seq(chains[c], chain_len[c], kMaxChainLen)) return false;
  }
  for (int l = 0; l < kLanes; ++l) {
    if (!add_seq(lanes[l], lane_len[l], kMaxLaneLen)) return false;
  }
  // Kahn's algorithm: the relation has no cycle when every worker can be taken once all its predecessors are.
  int indeg[kWorkers] = {};
  for (int a = 0; a < kWorkers; ++a) {
    for (int b = 0; b < kWorkers; ++b) indeg[b] += edge[a][b] ? 1 : 0;
  }
  int todo[kWorkers], n_todo = 0, seen = 0;
  for (int w = 0; w < kWorkers; ++w) {
    if (indeg[w] == 0) todo[n_todo++] = w;
  }
  while (n_todo > 0) {
    const int a = todo[--n_todo];
    ++seen;
    for (int b = 0; b < kWorkers; ++b) {
      if (edge[a][b] && --indeg[b] == 0) todo[n_todo++] = b;
    }
  }
  return seen == kWorkers;
}

// ---- Placement ----
void Placement::Set(int index, uint32_t entry, uint32_t done, uint32_t wait) {
  if (!Valid(index)) {
    bad_index_ = true;
    return;
  }
  if (set_[index]) {
    if (twice_ < 0) twice_ = index;
    return;  // the first values stay
  }
  set_[index] = true;
  entry_[index] = entry;
  done_[index] = done;
  wait_[index] = wait;
}

bool Placement::Complete() const {
  for (int i = 0; i < kWorkers; ++i) {
    if (!set_[i]) return false;
  }
  return true;
}

void Placement::Resolve(const uint32_t start[kChains]) {
  for (int i = 0; i < kWorkers; ++i) chain_[i] = pos_[i] = -1;
  for (int i = 0; i < kWorkers; ++i) {
    if (!set_[i]) continue;
    for (int c = 0; c < kChains; ++c) {
      if (wait_[i] == start[c]) {
        chain_[i] = c;
        pos_[i] = 0;
        break;
      }
    }
  }
  // A worker whose wait is a placed worker's done event follows it. One pass places at least one more worker or ends the loop, so
  // 25 passes are enough for any input.
  for (int pass = 0; pass < kWorkers; ++pass) {
    bool placed_one = false;
    for (int i = 0; i < kWorkers; ++i) {
      if (!set_[i] || chain_[i] >= 0) continue;
      for (int j = 0; j < kWorkers; ++j) {
        if (j == i || !set_[j] || chain_[j] < 0 || done_[j] != wait_[i]) continue;
        chain_[i] = chain_[j];
        pos_[i] = pos_[j] + 1;
        placed_one = true;
        break;
      }
    }
    if (!placed_one) break;
  }
}

const char* Placement::Mismatch() const {
  why_.clear();
  if (twice_ >= 0) {
    why_ = "worker " + std::to_string(twice_) + ": set twice";
  } else if (bad_index_) {
    why_ = "a worker index outside 0 to 24";
  } else {
    for (int i = 0; i < kWorkers && why_.empty(); ++i) {
      const std::string w = "worker " + std::to_string(i) + ": ";
      int chain = -1, pos = -1;
      TableChainPos(i, &chain, &pos);
      if (!set_[i]) {
        why_ = w + "not placed";
      } else if (entry_[i] != kEntryOf[i]) {
        why_ = w + "entry point " + Hex8(entry_[i]) + ", the table has " + Hex8(kEntryOf[i]);
      } else if (chain_[i] < 0) {
        why_ = w + "on no chain (it waits for " + Hex8(wait_[i]) + ")";
      } else if (chain_[i] != chain || pos_[i] != pos) {
        why_ = w + "chain " + std::to_string(chain_[i]) + " position " + std::to_string(pos_[i]) + ", the table has chain " +
               std::to_string(chain) + " position " + std::to_string(pos);
      }
    }
  }
  return why_.empty() ? nullptr : why_.c_str();
}

bool Placement::Same(int index, uint32_t done, uint32_t wait) const {
  return Valid(index) && set_[index] && done_[index] == done && wait_[index] == wait;
}

int Placement::ChainOf(int index) const { return Valid(index) ? chain_[index] : -1; }

int Placement::PosOf(int index) const { return Valid(index) ? pos_[index] : -1; }

// ---- Turns (wd:np_rb0_sched_pure.cpp:293-422, main=lanes, the constant tables) ----
Turns::Turns() {
  for (int w = 0; w < kWorkers; ++w) lane_of_[w] = lane_pos_[w] = -1;
  for (int l = 0; l < kLanes; ++l) {
    for (int k = 0; k < kLaneLen[l]; ++k) {
      lane_of_[kLane[l][k]] = l;
      lane_pos_[kLane[l][k]] = k;
    }
  }
  FrameReset();
}

void Turns::FrameReset() {
  for (int w = 0; w < kWorkers; ++w) ended_[w] = false;
  mid_ = join_ = main_waiting_ = false;
  max_at_once_ = max_beside_ = at_job_count_;  // 0 unless a worker is still at its job (the pre-frame step ends the session then)
}

int Turns::BlockerOf(int w) const {
  if (!Valid(w) || lane_of_[w] < 0) return -1;
  const int l = lane_of_[w];
  for (int k = 0; k < lane_pos_[w]; ++k) {
    if (!ended_[kLane[l][k]]) return kLane[l][k];
  }
  return -1;
}

bool Turns::MayRun(int w) const {
  if (!Valid(w) || lane_of_[w] < 0) return true;
  return BlockerOf(w) < 0;  // no join clause (wd line 337 is not ported: P19)
}

void Turns::Begin(int w) {
  if (!Valid(w) || in_section_[w]) return;
  in_section_[w] = true;
  mid_waiting_[w] = false;
  ++running_;
}

bool Turns::End(int w) {
  if (!Valid(w) || !in_section_[w]) return false;
  in_section_[w] = false;
  mid_waiting_[w] = false;
  --running_;
  const bool unseen = mid_ && !at_job_[w];
  if (at_job_[w]) {
    at_job_[w] = false;
    --at_job_count_;
  }
  ended_[w] = true;
  return unseen;
}

void Turns::MidWaitBegin(int w) {
  if (Valid(w) && in_section_[w]) mid_waiting_[w] = true;
}

void Turns::MidWaitEnd(int w) {
  if (!Valid(w) || !in_section_[w]) return;
  mid_waiting_[w] = false;
  if (!at_job_[w]) {
    at_job_[w] = true;
    ++at_job_count_;
    Note();
  }
}

void Turns::MainReachedMid() { mid_ = true; }

bool Turns::MainReachedJoin() {
  join_ = true;  // read by Note: the count "most beside main" stops at the join
  return mid_;
}

void Turns::MainWaiting(bool waiting) {
  main_waiting_ = waiting;
  Note();
}

void Turns::Note() {
  max_at_once_ = std::max(max_at_once_, at_job_count_);
  if (!main_waiting_ && !join_) max_beside_ = std::max(max_beside_, at_job_count_);
}

int Turns::FirstWaitBlocker() const {
  for (int c = 0; c < kChains; ++c) {
    for (int k = 0; k < kChainLen[c]; ++k) {
      const int h = kChain[c][k];
      if (ended_[h]) continue;
      // h is the chain's first worker that has not ended this frame
      if (mid_waiting_[h]) break;   // it has gone as far as it can before the mid event
      if (in_section_[h]) return h;  // it runs
      if (!MayRun(h)) break;        // a lane holds it: that lane's earlier worker is on another chain
      return h;                     // it may begin and has not yet reached the mid wait
    }
  }
  return -1;
}

bool Turns::FirstWaitReady() const { return FirstWaitBlocker() < 0; }

bool Turns::SecondWaitReady() const {
  for (int w = 0; w < kWorkers; ++w) {
    if (lane_of_[w] >= 0 && !ended_[w]) return false;
  }
  return true;
}

int Turns::SecondWaitBlocker() const {
  for (int l = 0; l < kLanes; ++l) {
    for (int k = 0; k < kLaneLen[l]; ++k) {
      if (!ended_[kLane[l][k]]) return kLane[l][k];
    }
  }
  return -1;
}

// ---- StallClock ----
StallClock::Step StallClock::Waited(int elapsed_ms) {
  const int before = counted_;
  const int step = std::min(std::max(elapsed_ms, 0), kMaxStepMs);
  if (counted_ <= (1 << 30)) counted_ += step;  // no overflow, whatever the number of calls
  if (counted_ >= kEndMs) return Step::kEnd;
  if (before < kStallMs && counted_ >= kStallMs) return Step::kLogStall;
  return Step::kKeepWaiting;
}

void StallClock::Reset() { counted_ = 0; }

// ---- EndRequest ----
bool EndRequest::Set(EndCause cause, std::string text) {
  std::lock_guard<std::mutex> lock(mu_);
  if (requested_.load(std::memory_order_relaxed)) return false;
  cause_ = cause;
  text_ = std::move(text);
  requested_.store(true, std::memory_order_release);
  return true;
}

EndCause EndRequest::Cause() const {
  std::lock_guard<std::mutex> lock(mu_);
  return cause_;
}

std::string EndRequest::Text() const {
  std::lock_guard<std::mutex> lock(mu_);
  return text_;
}

// ---- DecideMode ----
ModeChoice DecideMode(const ModeInputs& in) {
  ModeChoice out;
  if (in.serial_switch != nullptr && in.serial_switch[0] != '\0') {
    const char* s = in.serial_switch;
    if (s[0] >= '0' && s[0] <= '4' && s[1] == '\0') {
      out.mode = s[0] - '0';
      return out;
    }
    out.refused = true;
    out.why = std::string("DOA4_NP_SERIAL '") + s + "' is not one of 0 to 4";
  }
  if (!in.workers_fix_on) {
    out.mode = 0;
  } else if (in.online) {
    out.mode = in.handoff_serial ? 3 : 4;
  } else if (in.replay) {
    const std::string meta = in.meta_workers ? in.meta_workers : "";
    if (in.meta_workers != nullptr && meta == "lanes") {
      out.mode = 4;
    } else if (in.meta_workers != nullptr && meta == "serial") {
      out.mode = 3;
    } else {
      out.mode = 3;
      out.refused = true;
      if (!out.why.empty()) out.why += "; ";
      out.why += in.meta_workers ? "the recording's workers=" + meta + " is neither lanes nor serial"
                                 : std::string("the recording has no workers= line");
    }
  } else {
    out.mode = 4;
  }
  return out;
}

std::string MetaWorkers(int mode) {
  if (mode == 4) return "lanes";
  if (mode == 3) return "serial";
  return "mode" + std::to_string(mode);
}

// ---- ParseLanesTest ----
namespace {

// A decimal with no sign, blank or leading zero ("0" itself is fine), at most 9 digits.
bool Decimal(const std::string& s, int* out) {
  if (s.empty() || s.size() > 9 || (s.size() > 1 && s[0] == '0')) return false;
  int v = 0;
  for (char ch : s) {
    if (ch < '0' || ch > '9') return false;
    v = v * 10 + (ch - '0');
  }
  *out = v;
  return true;
}

LanesTest Refuse(const std::string& error) {
  LanesTest t;
  t.ok = false;
  t.error = error;
  return t;
}

}  // namespace

LanesTest ParseLanesTest(const char* text) {
  LanesTest t;
  if (text == nullptr || text[0] == '\0') return t;
  const std::string all = text;
  bool seen_vsm = false, seen_refuse = false, seen_stall = false;
  size_t at = 0;
  for (;;) {
    const size_t next = all.find(';', at);
    const std::string field = all.substr(at, next == std::string::npos ? std::string::npos : next - at);
    const size_t eq = field.find('=');
    const std::string name = field.substr(0, eq);
    const std::string value = eq == std::string::npos ? std::string() : field.substr(eq + 1);
    if (field.empty()) return Refuse("an empty field");
    if (eq == std::string::npos) return Refuse("'" + field + "': no such field (vsm=hash, refuse=placement, stall=<w>@<frame>:<ms>)");
    if (name == "vsm") {
      if (seen_vsm) return Refuse("vsm: given twice");
      if (value != "hash") return Refuse("vsm: the value is hash");
      seen_vsm = true;
      t.vsm_hash = true;
    } else if (name == "refuse") {
      if (seen_refuse) return Refuse("refuse: given twice");
      if (value != "placement") return Refuse("refuse: the value is placement");
      seen_refuse = true;
      t.refuse_placement = true;
    } else if (name == "stall") {
      if (seen_stall) return Refuse("stall: given twice");
      const size_t a = value.find('@'), c = value.find(':');
      int w = 0, frame = 0, ms = 0;
      if (a == std::string::npos || c == std::string::npos || c < a || !Decimal(value.substr(0, a), &w) ||
          !Decimal(value.substr(a + 1, c - a - 1), &frame) || !Decimal(value.substr(c + 1), &ms)) {
        return Refuse("stall: the value is <worker>@<frame>:<ms>, each a decimal with no sign, blank or leading zero");
      }
      if (w > 24) return Refuse("stall: the worker is 0 to 24");
      if (ms < 1 || ms > 20000) return Refuse("stall: the time is 1 to 20000 ms");
      seen_stall = true;
      t.stall_worker = w;
      t.stall_frame = frame;
      t.stall_ms = ms;
    } else {
      return Refuse("'" + name + "': no such field (vsm, refuse, stall)");
    }
    if (next == std::string::npos) break;
    at = next + 1;
  }
  return t;
}

// ---- WaitHistogram ----
void WaitHistogram::Add(uint64_t us) {
  ++bucket_[std::min<uint64_t>(us / 250, 63)];
  ++count_;
  max_ = std::max(max_, us);
}

uint64_t WaitHistogram::Percentile(uint64_t num, uint64_t den) const {
  if (count_ == 0) return 0;
  const uint64_t need = (num * count_ + den - 1) / den;  // ceil(p * count), exact
  uint64_t run = 0;
  for (int b = 0; b < 64; ++b) {
    run += bucket_[b];
    if (run >= need) return b == 63 ? max_ : uint64_t(b + 1) * 250;
  }
  return max_;
}

uint64_t WaitHistogram::P50Us() const { return Percentile(50, 100); }

uint64_t WaitHistogram::P99Us() const { return Percentile(99, 100); }

// ---- the critical sections (wd:np_rb0_sched_pure.cpp:254-273; the one change: not_initialised, ledger L30) ----
CsDecision DecideCs(uint8_t type, int32_t lock, uint32_t w8, uint32_t w12) {
  CsDecision d;
  if (type != 1 || lock < -1) {
    d.why = "not an initialised critical section (type byte " + std::to_string(type) + ", lock count " + std::to_string(lock) +
            "; a free section has 1 and -1)";
    d.not_initialised = true;
    return d;
  }
  if (lock >= 0) {
    d.why = "it is held (lock count " + std::to_string(lock) + ")";
    return d;
  }
  d.action = (w8 != 0 || w12 != 0) ? CsAction::kAlready : CsAction::kMake;
  return d;
}

const char* CsAfterMake(bool got_object, uint32_t w8, uint32_t w12) {
  if (!got_object) return "the SDK's call returned no object";
  if (w8 != kStashSignature || w12 == 0) return "the SDK's call returned an object but left no 'REX\\0' stash and handle at +8";
  return nullptr;
}

SectionState StateAfter(const CsDecision& d, const char* after_make) {
  switch (d.action) {
    case CsAction::kAlready:
      return SectionState::kAlready;
    case CsAction::kMake:
      return after_make == nullptr ? SectionState::kMade : SectionState::kRefused;
    case CsAction::kRefuse:
      break;
  }
  return d.not_initialised ? SectionState::kNotInitialised : SectionState::kRefused;
}

bool SectionsVerdict(const SectionState state[kSectionCount], bool fight_frame) {
  if (state[kSectionCount - 1] == SectionState::kRefused) return true;  // the manager's: this PC's alone, at once
  if (!fight_frame) return false;
  for (int s = 0; s < kSectionCount; ++s) {
    if (state[s] != SectionState::kMade && state[s] != SectionState::kAlready) return true;
  }
  return false;
}

// ---- the log lines ----
namespace {

std::string Joined(const std::vector<int>& v) {
  if (v.empty()) return "none";
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) out += (i ? " " : "") + std::to_string(v[i]);
  return out;
}

std::string Ms(uint64_t us) {  // microseconds as milliseconds with two decimals, rounded half up
  const uint64_t hundredths = (us + 5) / 10;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llu.%02llu", (unsigned long long)(hundredths / 100), (unsigned long long)(hundredths % 100));
  return buf;
}

}  // namespace

std::string DecisionLine(int mode, bool vsm_hash) {
  if (mode == 4) return std::string("workers: lanes, table ") + kTableId + ", cs 4, vsm " + (vsm_hash ? "hash" : "sort");
  if (mode == 3) return "workers: serial";
  return "workers: mode " + std::to_string(mode);
}

std::string PlacementLine(const Placement& p) {
  std::string out = "workers placed:";
  for (int c = 0; c < kChains; ++c) {
    std::vector<int> chain;
    for (int pos = 0; pos < kWorkers; ++pos) {
      for (int i = 0; i < kWorkers; ++i) {
        if (p.ChainOf(i) == c && p.PosOf(i) == pos) chain.push_back(i);
      }
    }
    out += std::string(c ? ";" : "") + " chain " + std::to_string(c) + ":";
    for (int w : chain) out += " " + std::to_string(w);
  }
  const char* why = p.Mismatch();
  return out + (why == nullptr ? std::string("; table: yes") : std::string("; table: NO (") + why + ")");
}

std::string SectionLine(uint32_t addr, SectionState state, uint32_t w8, uint32_t w12, const std::string& why) {
  const std::string head = "section " + Hex8(addr) + ": ";
  switch (state) {
    case SectionState::kMade:
      return head + "made " + Hex8(w8) + " " + Hex8(w12);
    case SectionState::kAlready:
      return head + "already " + Hex8(w8) + " " + Hex8(w12);
    case SectionState::kNotInitialised:
    case SectionState::kRefused:
      break;
  }
  return head + "refused: " + why;
}

std::string LanesLine(uint64_t frames, uint64_t frames_2plus, int most_at_once, int most_beside_main, uint64_t stalls,
                      uint64_t end_requests) {
  return "lanes: frames " + std::to_string(frames) + ", 2+ at once " + std::to_string(frames_2plus) + ", most at once " +
         std::to_string(most_at_once) + ", most beside main " + std::to_string(most_beside_main) + ", stalls " + std::to_string(stalls) +
         ", end requests " + std::to_string(end_requests);
}

std::string CpuLine(uint64_t fight_frames, const WaitHistogram& wait, uint64_t cycles_main, uint64_t cycles_workers) {
  return "cpu: fight frames " + std::to_string(fight_frames) + ", wait p50 " + Ms(wait.P50Us()) + " ms p99 " + Ms(wait.P99Us()) +
         " ms max " + Ms(wait.MaxUs()) + " ms, cycles main " + std::to_string(cycles_main) + " workers " + std::to_string(cycles_workers);
}

std::string StallLine(int32_t frame, int waiter, int waited_for) {
  const std::string who = waiter == kMainFirstWait    ? std::string("main (first wait)")
                          : waiter == kMainSecondWait ? std::string("main (second wait)")
                                                      : "worker " + std::to_string(waiter);
  return "lanes stall at frame " + std::to_string(frame) + ": " + who + " has waited 2 s for worker " + std::to_string(waited_for);
}

std::string StallOverLine(int32_t frame, int ms, const std::vector<int>& lane1_began, const std::vector<int>& lane2_began) {
  return "lanes stall over at frame " + std::to_string(frame) + " after " + std::to_string(ms) + " ms: lane 1 began " + Joined(lane1_began) +
         "; lane 2 began " + Joined(lane2_began);
}

std::string EndRequestLine(int32_t frame, const std::string& cause_text) {
  return "lanes end request at frame " + std::to_string(frame) + ": " + cause_text;
}

std::string EndRequestWarn(const std::string& cause_text) { return "WARN lanes: " + cause_text + ": the run is not evidence"; }

}  // namespace doa4::np::lanes
