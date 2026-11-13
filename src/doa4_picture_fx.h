// The picture effects (the Esc menu's Shader tab): sharpening, clarity, bloom, highlight roll-off and colour grading
// on top of the game's own image. The SDK's D3D12 presenter runs them as one pass over the guest output before it is
// scaled to the window (rex_post_fx_set, sdk_overrides/); the effects need only the finished picture, so none of them
// can use depth (no ambient occlusion, depth of field, haze or light shafts).
//
// The settings live in the cvar doa4_picture_fx as one text, "on=1;split=0;sharpen=0.5;...", so a new effect needs no
// new config key; a key that is missing keeps its neutral value.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace doa4::picture_fx {

enum Effect : size_t {
  kSharpen,
  kClarity,
  kBloom,
  kBloomThreshold,
  kBloomSize,
  kBloomHaze,
  kHighlightRolloff,
  kExposure,
  kContrast,
  kSaturation,
  kVibrance,
  kWarmth,
  kShadowLift,
  kVignette,
  kCount,
};

struct EffectInfo {
  const char* key;    // in the cvar's text
  const char* label;  // the slider's label
  const char* help;   // the slider's tooltip
  float min, max, neutral;
};
extern const std::array<EffectInfo, kCount> kEffects;

struct Settings {
  bool on = false;
  bool split = false;  // the left half of the picture without the effects
  std::array<float, kCount> values{};
  Settings();  // every effect neutral, off
  bool operator==(const Settings& o) const { return on == o.on && split == o.split && values == o.values; }
};

struct Preset {
  const char* name;
  const char* description;
  std::array<float, kCount> values;
};
extern const std::array<Preset, 5> kPresets;

// The preset whose values these are, or -1 (Custom).
int MatchPreset(const Settings& s);

std::string Format(const Settings& s);
// Unknown keys and unreadable numbers are skipped; values are clamped to their range.
Settings Parse(std::string_view text);

// Hands the settings to the presenter (rex_post_fx_set); with every effect neutral the pass is skipped.
void Apply(const Settings& s);
// At start-up: the cvar's settings to the presenter.
void ApplyFromConfig();

}  // namespace doa4::picture_fx
