// The start-up log's hardware line: "[doa4][LIFE] cpu: <name>, N cores, M threads, R MB RAM". A slowdown report's log
// named the GPU and no CPU, so "a better CPU" could not be checked. Header-only and free of the SDK, for
// tests/host/cpu_info_host_test.cpp. CleanBrand and LogLine are pure; Read asks Windows and the processor.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#endif

namespace doa4::cpu_info {

struct Info {
  std::string brand;     // as the processor reports it (48 bytes, padded with spaces and NULs); empty: not known
  uint32_t cores = 0;    // physical cores; 0: not known
  uint32_t logical = 0;  // logical processors, all groups; 0: not known
  uint64_t ram_mb = 0;   // installed memory the OS can use; 0: not known
};

// The brand text without its padding: NULs end it, runs of blanks become one space, no blank at either end, bytes
// outside printable ASCII dropped (the line goes into a UTF-8 log). Nothing left: "unknown".
inline std::string CleanBrand(std::string_view raw) {
  std::string out;
  bool pending_space = false;
  for (const char c : raw) {
    if (c == '\0') break;
    if (c == ' ' || c == '\t') {
      pending_space = !out.empty();
      continue;
    }
    if (c < 0x21 || c > 0x7e) continue;
    if (pending_space) out.push_back(' ');
    pending_space = false;
    out.push_back(c);
  }
  return out.empty() ? std::string("unknown") : out;
}

// The text after "[doa4][LIFE] cpu: ". A count that is not known reads "?".
inline std::string LogLine(const Info& info) {
  const auto count = [](uint64_t n) { return n ? std::to_string(n) : std::string("?"); };
  return CleanBrand(info.brand) + ", " + count(info.cores) + " cores, " + count(info.logical) + " threads, " +
         count(info.ram_mb) + " MB RAM";
}

#ifdef _WIN32
// This machine. Every field that cannot be read stays "not known"; nothing here throws or fails the start-up.
inline Info Read() {
  Info info;
#if defined(_M_X64) || defined(_M_IX86)
  int regs[4] = {};
  __cpuid(regs, int(0x80000000u));
  if (uint32_t(regs[0]) >= 0x80000004u) {
    char brand[48] = {};
    for (int leaf = 0; leaf < 3; ++leaf) {
      __cpuid(regs, int(0x80000002u) + leaf);
      for (int r = 0; r < 4; ++r) {
        for (int b = 0; b < 4; ++b) brand[leaf * 16 + r * 4 + b] = char((uint32_t(regs[r]) >> (8 * b)) & 0xff);
      }
    }
    info.brand.assign(brand, sizeof brand);
  }
#endif
  info.logical = uint32_t(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
  if (bytes) {
    std::vector<unsigned char> buffer(bytes);
    if (GetLogicalProcessorInformationEx(
            RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes)) {
      for (DWORD at = 0; at < bytes;) {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + at);
        if (!entry->Size) break;
        if (entry->Relationship == RelationProcessorCore) ++info.cores;
        at += entry->Size;
      }
    }
  }
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof memory;
  if (GlobalMemoryStatusEx(&memory)) info.ram_mb = memory.ullTotalPhys / (1024ull * 1024ull);
  return info;
}
#endif

}  // namespace doa4::cpu_info
