// Host test for the SDK's Intel render target policy (sdk include/rex/graphics/d3d12/intel_policy.h, used by
// src/graphics/d3d12/render_target_cache.cpp Initialize). Built and run by run_d3d12_intel_policy_test.bat.
//
// The contract (issue #21, Intel UHD 770: ClearDepthStencilView with a rectangle leaves the second tile's depth):
// - DrawDepthClears(force, intel_default, is_intel) = force || (is_intel && intel_default): Intel GPUs draw their
//   depth/stencil clears by default, other GPUs keep 0.3.6's ClearDepthStencilView unless forced;
// - ChoosePath(setting, is_intel, rov_supported, depth_clears_drawn): "rtv" and "rov" are the user's word; any other
//   text is automatic, which is rov only on Intel with the clears not drawn (0.3.6's Intel choice); rov never without
//   rasterizer-ordered views.

#include <rex/graphics/d3d12/intel_policy.h>

#include <cstdio>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::fflush(stdout);                                        \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using rex::graphics::d3d12::intel_policy::ChoosePath;
using rex::graphics::d3d12::intel_policy::DrawDepthClears;
using P = rex::graphics::d3d12::intel_policy::Path;

static void TestDrawDepthClears() {
  // DrawDepthClears(force, intel_default, is_intel)
  CHECK(DrawDepthClears(false, true, true));    // Intel default
  CHECK(!DrawDepthClears(false, true, false));  // other GPUs: 0.3.6
  CHECK(!DrawDepthClears(false, false, true));  // Intel opted out
  CHECK(DrawDepthClears(true, false, false));   // forced on any GPU
  CHECK(DrawDepthClears(true, false, true));
}

static void TestChoosePath() {
  // ChoosePath(setting, is_intel, rov_supported, depth_clears_drawn)
  CHECK(ChoosePath("", true, true, true) == P::kHostRenderTargets);        // Intel default: fast path
  CHECK(ChoosePath("", true, true, false) == P::kPixelShaderInterlock);    // Intel opted out: 0.3.6
  CHECK(ChoosePath("", false, true, false) == P::kHostRenderTargets);      // other GPUs: 0.3.6
  CHECK(ChoosePath("", false, true, true) == P::kHostRenderTargets);
  CHECK(ChoosePath("rov", true, true, true) == P::kPixelShaderInterlock);  // the user's word wins
  CHECK(ChoosePath("rov", false, true, false) == P::kPixelShaderInterlock);
  CHECK(ChoosePath("rtv", true, true, false) == P::kHostRenderTargets);
  CHECK(ChoosePath("rov", true, false, true) == P::kHostRenderTargets);    // no ROV support: never rov
  CHECK(ChoosePath("", true, false, false) == P::kHostRenderTargets);
  CHECK(ChoosePath("ROV", true, true, true) == P::kHostRenderTargets);     // unknown text is automatic (as today)
}

int main() {
  TestDrawDepthClears();
  TestChoosePath();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}
