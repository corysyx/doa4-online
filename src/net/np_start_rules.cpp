// Netplay starting rules (see np_start_rules.h for the contract). Pure: no SDK, no files.

#include "np_start_rules.h"

#include <array>

namespace doa4::np::start_rules {
namespace {

struct Option {
  size_t offset;  // from P
  uint8_t target;
};
constexpr std::array<Option, 3> kOptions = {{
    {kMatchPointSingle, kMatchPointSingleTarget},
    {kLifeGauge, kLifeGaugeTarget},
    {kRoundTime, kRoundTimeTarget},
}};

bool IsRecord(const handoff::File& f) { return f.name == std::string(handoff::kSavePrefix) + kRecordFile; }

}  // namespace

std::string ApplyToSaveFiles(std::vector<handoff::File>* files) {
  for (handoff::File& f : *files) {
    if (!IsRecord(f)) continue;
    if (f.data.size() < kMinRecordFileBytes) {
      return "the profile record " + f.name + " has " + std::to_string(f.data.size()) + " bytes, fewer than the " +
             std::to_string(kMinRecordFileBytes) + " the match options need: not edited";
    }
    for (const Option& o : kOptions) f.data[kRecordInFile + o.offset] = o.target;
    return "";
  }
  return "";
}

bool RulesHeld(const std::vector<handoff::File>& files) {
  for (const handoff::File& f : files) {
    if (!IsRecord(f)) continue;
    if (f.data.size() < kMinRecordFileBytes) return false;
    for (const Option& o : kOptions)
      if (f.data[kRecordInFile + o.offset] != o.target) return false;
    return true;
  }
  return false;
}

bool WriteToRecord(std::span<uint8_t> record) {
  if (record.size() < kRecordMemoryBytes) return false;
  for (const Option& o : kOptions) record[o.offset] = o.target;
  return true;
}

std::string MatchRulesLine(bool tag, uint16_t life, uint8_t rounds, uint8_t time) {
  return std::string("match rules: mode ") + (tag ? "tag" : "single") + " life " + std::to_string(unsigned(life)) +
         " rounds " + std::to_string(unsigned(rounds)) + " time " + std::to_string(unsigned(time));
}

}  // namespace doa4::np::start_rules
