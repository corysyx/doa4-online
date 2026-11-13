// The picture effects (doa4_picture_fx.h).
#include "doa4_picture_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <fmt/format.h>

#ifndef DOA4_PICTURE_FX_HOST_TEST
#include <rex/cvar.h>
#include <rex/logging.h>


// sdk_overrides/src/ui/d3d12/d3d12_presenter.cpp: 16 floats, the shader's constants p0..p3 in order.
extern "C" __declspec(dllimport) void rex_post_fx_set(int on, const float* values, int count);
#endif

namespace doa4::picture_fx {

const std::array<EffectInfo, kCount> kEffects = {{
    {"sharpen", "Sharpen", "Crisper edges and fine detail.", 0.0f, 2.0f, 0.0f},
    {"clarity", "Clarity", "Local contrast: brings out texture and shape without changing the overall brightness.", 0.0f,
     1.0f, 0.0f},
    {"bloom", "Bloom", "A glow around bright parts of the picture.", 0.0f, 2.0f, 0.0f},
    {"bloom_threshold", "Bloom threshold", "How bright a part must be to glow: lower makes more of the picture glow.",
     0.0f, 1.0f, 0.7f},
    {"bloom_size", "Bloom size", "How far the glow spreads.", 0.5f, 3.0f, 1.0f},
    {"bloom_haze", "Bloom haze", "Mixes a wide, soft haze into the glow.", 0.0f, 1.0f, 0.0f},
    {"highlight_rolloff", "Highlight roll-off", "Eases the brightest parts into white instead of clipping them.", 0.0f,
     1.0f, 0.0f},
    {"exposure", "Exposure", "Overall brightness.", 0.5f, 2.0f, 1.0f},
    {"contrast", "Contrast", "The difference between dark and bright.", 0.5f, 1.5f, 1.0f},
    {"saturation", "Saturation", "Colour strength (0: black and white).", 0.0f, 2.0f, 1.0f},
    {"vibrance", "Vibrance", "Strengthens dull colours more than strong ones (skin tones stay natural).", -1.0f, 1.0f,
     0.0f},
    {"warmth", "Warmth", "Warmer (orange) or cooler (blue) colours.", -1.0f, 1.0f, 0.0f},
    {"shadow_lift", "Shadow lift", "Brightens the darkest parts so detail in shadows shows.", 0.0f, 1.0f, 0.0f},
    {"vignette", "Vignette", "Darkens the corners of the picture.", 0.0f, 1.0f, 0.0f},
}};

// Sharpen, clarity, bloom, threshold, size, haze, roll-off, exposure, contrast, saturation, vibrance, warmth, shadow
// lift, vignette.
const std::array<Preset, 5> kPresets = {{
    {"Original", "The game's own picture.",
     {0.0f, 0.0f, 0.0f, 0.7f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}},
    {"Modern Look", "Sharpening, clarity, bloom, smooth highlights and colour grading on top of the game's own image.",
     {0.55f, 0.35f, 0.35f, 0.65f, 1.4f, 0.3f, 0.6f, 1.05f, 1.1f, 1.12f, 0.35f, 0.02f, 0.05f, 0.3f}},
    {"Vivid", "Strong, clean colours and crisp detail.",
     {0.45f, 0.2f, 0.2f, 0.7f, 1.0f, 0.0f, 0.4f, 1.0f, 1.15f, 1.3f, 0.5f, 0.0f, 0.0f, 0.1f}},
    {"Cinematic", "Softer colours, a warm tint, a wide glow and dark corners.",
     {0.3f, 0.25f, 0.45f, 0.6f, 1.8f, 0.5f, 0.8f, 0.98f, 1.12f, 0.9f, 0.1f, 0.15f, 0.0f, 0.55f}},
    {"Soft Glow", "A dreamy glow with lifted shadows.",
     {0.0f, 0.0f, 0.7f, 0.55f, 2.0f, 0.6f, 0.7f, 1.02f, 0.95f, 1.05f, 0.2f, 0.08f, 0.1f, 0.2f}},
}};

Settings::Settings() {
  for (size_t i = 0; i < kCount; ++i) values[i] = kEffects[i].neutral;
}

int MatchPreset(const Settings& s) {
  for (size_t p = 0; p < kPresets.size(); ++p) {
    bool same = true;
    for (size_t i = 0; i < kCount && same; ++i) same = std::fabs(s.values[i] - kPresets[p].values[i]) < 0.0005f;
    if (same) return int(p);
  }
  return -1;
}

std::string Format(const Settings& s) {
  std::string t = fmt::format("on={};split={}", s.on ? 1 : 0, s.split ? 1 : 0);
  for (size_t i = 0; i < kCount; ++i) t += fmt::format(";{}={:.3f}", kEffects[i].key, s.values[i]);
  return t;
}

Settings Parse(std::string_view text) {
  Settings s;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find(';', pos);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view item = text.substr(pos, end - pos);
    const size_t eq = item.find('=');
    if (eq != std::string_view::npos) {
      const std::string key(item.substr(0, eq));
      const std::string value(item.substr(eq + 1));
      char* stop = nullptr;
      const float f = std::strtof(value.c_str(), &stop);
      if (stop != value.c_str() && std::isfinite(f)) {
        if (key == "on") {
          s.on = f != 0.0f;
        } else if (key == "split") {
          s.split = f != 0.0f;
        } else {
          for (size_t i = 0; i < kCount; ++i) {
            if (key == kEffects[i].key) s.values[i] = std::clamp(f, kEffects[i].min, kEffects[i].max);
          }
        }
      }
    }
    pos = end + 1;
  }
  return s;
}

#ifndef DOA4_PICTURE_FX_HOST_TEST
void Apply(const Settings& s) {
  // The shader's constants p0..p3 (sdk_overrides/src/ui/d3d12/d3d12_presenter.cpp).
  float v[16] = {};
  v[0] = s.values[kSharpen];
  v[1] = s.values[kClarity];
  v[2] = s.values[kBloom];
  v[3] = s.values[kBloomThreshold];
  v[4] = s.values[kBloomSize];
  v[5] = s.values[kBloomHaze];
  v[6] = s.values[kHighlightRolloff];
  v[7] = s.values[kExposure];
  v[8] = s.values[kContrast];
  v[9] = s.values[kSaturation];
  v[10] = s.values[kVibrance];
  v[11] = s.values[kWarmth];
  v[12] = s.values[kShadowLift];
  v[13] = s.values[kVignette];
  v[14] = s.split ? 1.0f : 0.0f;
  bool neutral = true;
  for (size_t i = 0; i < kCount; ++i) neutral = neutral && std::fabs(s.values[i] - kEffects[i].neutral) < 0.0005f;
  rex_post_fx_set(s.on && !neutral ? 1 : 0, v, 16);
}

void ApplyFromConfig() {
  const Settings s = Parse(rex::cvar::Query<std::string>("doa4_picture_fx"));
  Apply(s);
  REXLOG_INFO("[doa4][fx] picture effects {} ({})", s.on ? "on" : "off", Format(s));
}
#endif

}  // namespace doa4::picture_fx
