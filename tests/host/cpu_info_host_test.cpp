// Host test for src/doa4_cpu_info.h (the start-up log's cpu line). Built and run by run_cpu_info_test.bat.

#include "../../src/doa4_cpu_info.h"

#include <cstdio>
#include <cstring>

#pragma comment(lib, "advapi32.lib")  // RegGetValueA (TestRead's second source)

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::cpu_info;

// A 48-byte brand field the way processors fill it.
static std::string Field(const char* text) {
  std::string field(48, '\0');
  std::memcpy(field.data(), text, std::strlen(text));
  return field;
}

static void TestCleanBrand() {
  // Intel pads on the left on older parts, AMD on the right; both end in NULs.
  CHECK(CleanBrand(Field("      Intel(R) Core(TM) i5-4590 CPU @ 3.30GHz")) == "Intel(R) Core(TM) i5-4590 CPU @ 3.30GHz");
  CHECK(CleanBrand(Field("AMD Ryzen 5 5600X 6-Core Processor             ")) == "AMD Ryzen 5 5600X 6-Core Processor");
  // Runs of blanks inside the name become one space (Xeon names carry them).
  CHECK(CleanBrand(Field("Intel(R) Xeon(R) CPU E5-2666 v3  @   2.90GHz")) == "Intel(R) Xeon(R) CPU E5-2666 v3 @ 2.90GHz");
  // The first NUL ends the name: bytes after it are not the name.
  CHECK(CleanBrand(std::string_view("Name\0junk", 9)) == "Name");
  // Bytes outside printable ASCII are dropped, and do not leave a blank behind at the end.
  CHECK(CleanBrand(std::string("A\x01" "B \xff", 5)) == "AB");
  // A full field with no NUL at all (48 name bytes).
  CHECK(CleanBrand(std::string(48, 'x')) == std::string(48, 'x'));
  // Nothing readable.
  CHECK(CleanBrand("") == "unknown");
  CHECK(CleanBrand(Field("")) == "unknown");
  CHECK(CleanBrand("   \t ") == "unknown");
}

static void TestLogLine() {
  Info info;
  info.brand = Field("AMD Ryzen 5 5600X 6-Core Processor             ");
  info.cores = 6;
  info.logical = 12;
  info.ram_mb = 32691;
  CHECK(LogLine(info) == "AMD Ryzen 5 5600X 6-Core Processor, 6 cores, 12 threads, 32691 MB RAM");
  // Each count that is not known reads "?", on its own.
  CHECK(LogLine(Info{}) == "unknown, ? cores, ? threads, ? MB RAM");
  Info partial;
  partial.brand = "X";
  partial.logical = 4;
  CHECK(LogLine(partial) == "X, ? cores, 4 threads, ? MB RAM");
  // More than 4 GB of memory in MB does not wrap.
  Info big;
  big.brand = "X";
  big.cores = 64;
  big.logical = 128;
  big.ram_mb = 5000000;
  CHECK(LogLine(big) == "X, 64 cores, 128 threads, 5000000 MB RAM");
}

// This machine: every field is read, and the counts make sense together.
static void TestRead() {
  const Info info = Read();
  std::printf("this machine: %s\n", LogLine(info).c_str());
  CHECK(CleanBrand(info.brand) != "unknown");
  CHECK(info.cores >= 1);
  CHECK(info.logical >= info.cores);
  CHECK(info.logical <= info.cores * 2);  // x86: at most two threads per core
  CHECK(info.ram_mb >= 1024);
  CHECK(LogLine(info).find('?') == std::string::npos);
  // The name Windows keeps for processor 0 is the same text (a second source: it catches the three brand leaves
  // read in the wrong order, which every check above passes).
  char name[256] = {};
  DWORD size = sizeof name - 1;
  const LSTATUS status = RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                      "ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size);
  CHECK(status == ERROR_SUCCESS);
  std::printf("windows says: %s\n", name);
  CHECK(CleanBrand(name) == CleanBrand(info.brand));
}

int main() {
  TestCleanBrand();
  TestLogLine();
  TestRead();
  if (g_failures == 0) {
    std::printf("PASSED: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
  return 1;
}
