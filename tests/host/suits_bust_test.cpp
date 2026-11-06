// Host test for doa4_suits_bust.h: the live rescale of a converted suit's bust morph poses.
// Build: clang++ -std=c++23 -I src tests/host/suits_bust_test.cpp -o suits_bust_test.exe
#include "doa4_suits_bust.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int g_checks = 0;
int g_fails = 0;

void Check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_fails;
    std::printf("FAIL: %s\n", what);
  }
}

// Guest memory: big-endian words in a byte array, addresses start at kBase, and writes are counted.
constexpr uint32_t kBase = 0x40000000;

struct Mem {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x400, 0xAB);
  int writes = 0;
  uint32_t ReadU32(uint32_t a) const {
    const uint8_t* p = &bytes.at(a - kBase);
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  }
  void WriteU32(uint32_t a, uint32_t v) {
    uint8_t* p = &bytes.at(a - kBase);
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
    ++writes;
  }
  void Fill(uint32_t a, uint32_t v) {
    WriteU32(a, v);
    --writes;
  }
  void FillF(uint32_t a, float f) { Fill(a, std::bit_cast<uint32_t>(f)); }
  float F(uint32_t a) const { return std::bit_cast<float>(ReadU32(a)); }
};

constexpr uint32_t kMpp = kBase + 0x10;
constexpr uint32_t kRest = kBase + 0x100;
constexpr uint32_t kDown = kBase + 0x200;
constexpr uint32_t kUp = kBase + 0x300;

// Two records. Rest (1,2,3) (4,5,6); down (1,1,3) (4,5,5); up (1,4,3) (6,5,6). After bind: +8 = 1, pose words absolute.
// Full range: down at ratio 2, up at ratio 3, different so a mix-up of the two shows.
Mem Fixture() {
  Mem m;
  m.Fill(kMpp + 0x08, 1);
  m.Fill(kMpp + 0x14, 3);
  m.Fill(kMpp + 0x1C, kRest);
  m.Fill(kMpp + 0x20, kDown);
  m.Fill(kMpp + 0x24, kUp);
  m.Fill(kMpp + 0x28, doa4::suits::bust::kMagic);
  m.Fill(kMpp + 0x2C, 2);
  m.FillF(kMpp + 0x30, 1.0f);
  m.FillF(kMpp + 0x34, 2.0f);
  m.FillF(kMpp + 0x38, 1.0f);
  m.FillF(kMpp + 0x3C, 3.0f);
  const float rest[] = {1, 2, 3, 4, 5, 6}, down[] = {1, 1, 3, 4, 5, 5}, up[] = {1, 4, 3, 6, 5, 6};
  for (uint32_t i = 0; i < 6; ++i) {
    m.FillF(kRest + 4 * i, rest[i]);
    m.FillF(kDown + 4 * i, down[i]);
    m.FillF(kUp + 4 * i, up[i]);
  }
  // A third record's worth of rest differs from the poses' filler, so a loop that overruns changes the poses' bytes.
  for (uint32_t i = 6; i < 9; ++i) m.FillF(kRest + 4 * i, 1.0f);
  return m;
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

void TestFullScalesEachPoseByItsOwnRatio() {
  Mem m = Fixture();
  Check(doa4::suits::bust::Rescale(m, kMpp, true), "a marked block goes to the full range");
  // Hand-derived, pose = rest + (pose - rest) * ratio.
  // Down, ratio 2: y 2 + (1 - 2) * 2 = 0; second record z 6 + (5 - 6) * 2 = 4.
  // Up, ratio 3: y 2 + (4 - 2) * 3 = 8; second record x 4 + (6 - 4) * 3 = 10.
  const float down[] = {1, 0, 3, 4, 5, 4}, up[] = {1, 8, 3, 10, 5, 6}, rest[] = {1, 2, 3, 4, 5, 6};
  for (uint32_t i = 0; i < 6; ++i) {
    Check(Near(m.F(kDown + 4 * i), down[i]), "down pose at its own ratio, every component of every record");
    Check(Near(m.F(kUp + 4 * i), up[i]), "up pose at its own ratio, every component of every record");
    Check(m.F(kRest + 4 * i) == rest[i], "rest pose untouched");
  }
  Check(m.F(kMpp + 0x30) == 2.0f, "down's current ratio recorded");
  Check(m.F(kMpp + 0x38) == 3.0f, "up's current ratio recorded");
  Check(m.F(kMpp + 0x34) == 2.0f && m.F(kMpp + 0x3C) == 3.0f, "full ratios unchanged");
  Check(m.ReadU32(kDown + 24) == 0xABABABAB && m.ReadU32(kUp + 24) == 0xABABABAB, "bytes after the last record untouched");
}

void TestBackToTheFilesStrength() {
  Mem m = Fixture();
  doa4::suits::bust::Rescale(m, kMpp, true);
  Check(doa4::suits::bust::Rescale(m, kMpp, false), "toggle off is applied");
  Check(Near(m.F(kDown + 4), 1.0f) && Near(m.F(kDown + 20), 5.0f), "toggle off restores the down pose");
  Check(Near(m.F(kUp + 4), 4.0f) && Near(m.F(kUp + 12), 6.0f), "toggle off restores the up pose");
  Check(m.F(kMpp + 0x30) == 1.0f && m.F(kMpp + 0x38) == 1.0f, "current ratios back to 1");
}

void TestFactorIsWantedOverCurrent() {
  // Poses at ratio 0.5 now, up at 0.25: toggle off is a factor of 2 for down and 4 for up.
  Mem m = Fixture();
  m.FillF(kMpp + 0x30, 0.5f);
  m.FillF(kMpp + 0x38, 0.25f);
  Check(doa4::suits::bust::Rescale(m, kMpp, false), "ratios other than 1 are rewritten");
  Check(Near(m.F(kDown + 4), 0.0f), "down: 2 + (1 - 2) * 2");
  Check(Near(m.F(kUp + 4), 10.0f), "up: 2 + (4 - 2) * 4");
}

void TestOnlyOnePoseOutOfDateStillRewrites() {
  Mem m = Fixture();
  m.FillF(kMpp + 0x38, 3.0f);  // up already at 3, down at 1, toggle off wants 1 and 1
  Check(doa4::suits::bust::Rescale(m, kMpp, false), "one pose out of date is enough");
  Check(Near(m.F(kUp + 4), 2.0f + 2.0f / 3.0f), "up: 2 + (4 - 2) / 3");
  Check(Near(m.F(kDown + 4), 1.0f), "down stays where it is");
}

void TestOnlyDownOutOfDateStillRewrites() {
  Mem m = Fixture();
  m.FillF(kMpp + 0x30, 2.0f);  // down at 2, up at 1, toggle off wants 1 and 1
  Check(doa4::suits::bust::Rescale(m, kMpp, false), "down alone out of date is enough");
  Check(Near(m.F(kDown + 4), 1.5f), "down: 2 + (1 - 2) / 2");
  Check(Near(m.F(kUp + 4), 4.0f), "up stays where it is");
}

void TestNothingWrittenWhenAlreadyThere() {
  Mem m = Fixture();
  Check(!doa4::suits::bust::Rescale(m, kMpp, false), "already at the wanted ratios: no rewrite");
  Check(m.writes == 0, "already at the wanted ratios: no write");
}

void TestRefusesEverythingElse() {
  struct Case {
    uint32_t offset;
    uint32_t value;
    const char* what;
  };
  const uint32_t nan = 0x7FC00000;
  const Case cases[] = {
      {0x28, 0, "a vanilla block has no magic"},
      {0x14, 21, "a block with another pose count (hands)"},
      {0x08, 0, "a block the binder has not bound (pose words are still offsets)"},
      {0x2C, 0, "no records"},
      {0x30, 0, "down's current ratio zero"},
      {0x30, nan, "down's current ratio not a number"},
      {0x38, 0, "up's current ratio zero"},
      {0x38, nan, "up's current ratio not a number"},
      {0x34, 0, "down's full ratio zero"},
      {0x34, nan, "down's full ratio not a number"},
      {0x3C, 0, "up's full ratio zero"},
      {0x3C, nan, "up's full ratio not a number"},
      {0x3C, 0x7F800000, "up's full ratio infinite"},
      {0x34, 0xBF800000, "down's full ratio negative"},
  };
  for (const Case& c : cases) {
    Mem m = Fixture();
    m.Fill(kMpp + c.offset, c.value);
    Check(!doa4::suits::bust::Rescale(m, kMpp, true), c.what);
    Check(m.writes == 0, c.what);
  }
}

void TestIsMarked() {
  Mem m = Fixture();
  Check(doa4::suits::bust::IsMarked(m, kMpp), "a suit's block is marked");
  m.Fill(kMpp + 0x28, 0);
  Check(!doa4::suits::bust::IsMarked(m, kMpp), "a vanilla block is not");
}

}  // namespace

int main() {
  TestFullScalesEachPoseByItsOwnRatio();
  TestBackToTheFilesStrength();
  TestFactorIsWantedOverCurrent();
  TestOnlyOnePoseOutOfDateStillRewrites();
  TestOnlyDownOutOfDateStillRewrites();
  TestNothingWrittenWhenAlreadyThere();
  TestRefusesEverythingElse();
  TestIsMarked();
  std::printf("%d checks\n", g_checks);
  if (g_fails) {
    std::printf("%d check(s) failed\n", g_fails);
    return 1;
  }
  std::printf("suits_bust: all checks passed\n");
  return 0;
}
