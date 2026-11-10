// The latency trace's pure parts (doa4_lagtrace.h).

#include "doa4_lagtrace.h"

namespace doa4::lagtrace {

void Joiner::AddFrame(const Frame& frame) { frames_.push_back(frame); }

void Joiner::AddPresent(const Present& present) {
  if (present.swap_index > newest_) newest_ = present.swap_index;
  // its frame was handed out already (index 0, a present of no guest output, is never above done_ either)
  if (present.swap_index <= done_) return;
  first_.try_emplace(present.swap_index, present);  // keeps the first present of the swap
}

std::vector<Row> Joiner::Take(bool flush) {
  std::vector<Row> out;
  while (!frames_.empty()) {
    const Frame& f = frames_.front();
    Row row;
    row.frame = f;
    const uint32_t s = f.guest_swap_index;
    if (s != 0) {
      auto it = first_.find(s);
      if (it != first_.end()) {
        row.joined = true;
        row.present = it->second;
      } else if (s > done_ && newest_ <= s && !flush && frames_.size() <= max_waiting_) {
        break;  // its present may still come
      }
      // drop every present of this swap and of the swaps before it (kick-thread re-presents in between)
      first_.erase(first_.begin(), first_.upper_bound(s));
      if (s > done_) done_ = s;
    }
    out.push_back(row);
    frames_.pop_front();
  }
  return out;
}

static void Field(std::string& line, int64_t v) {
  line += ',';
  if (v != 0) line += std::to_string(v);
}

std::string CsvHeader() {
  return "frame,t_input_us,t_frame_start_us,t_guest_swap_us,t_host_present_us,guest_swap_index,"
         "np_frame,t_np_done_us,t_frame_fn_done_us,t_swap_call_us,t_swap_return_us,t_present_call_us,"
         "pad_calls_main,pad_calls_other,swap_calls_main,swap_calls_other,present_flags";
}

std::string CsvLine(const Row& row) {
  const Frame& f = row.frame;
  std::string line = std::to_string(f.frame);
  Field(line, f.t_input);
  Field(line, f.t_frame_start);
  Field(line, f.t_guest_swap);
  Field(line, row.joined ? row.present.t_return : 0);
  Field(line, f.guest_swap_index);
  line += ',' + std::to_string(f.np_frame);
  Field(line, f.t_np_done);
  Field(line, f.t_frame_fn_done);
  Field(line, f.t_swap_call);
  Field(line, f.t_swap_return);
  Field(line, row.joined ? row.present.t_call : 0);
  line += ',' + std::to_string(f.pad_calls_main);
  line += ',' + std::to_string(f.pad_calls_other);
  line += ',' + std::to_string(f.swap_calls_main);
  line += ',' + std::to_string(f.swap_calls_other);
  line += ',';
  if (row.joined) line += std::to_string(row.present.flags);
  return line;
}

int64_t QpcToUs(int64_t qpc, int64_t frequency) {
  return (qpc / frequency) * 1000000 + (qpc % frequency) * 1000000 / frequency;
}

}  // namespace doa4::lagtrace
