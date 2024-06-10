// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wave_fold_modifier.h"

#include "utils.h"
#include "wave_frame.h"

WaveFoldModifier::WaveFoldModifierKeyframe::WaveFoldModifierKeyframe() {
  wave_fold_boost_ = 1.0f;
}

void WaveFoldModifier::WaveFoldModifierKeyframe::copy(const WavetableKeyframe* keyframe) {
  const WaveFoldModifierKeyframe* source = dynamic_cast<const WaveFoldModifierKeyframe*>(keyframe);
  wave_fold_boost_ = source->wave_fold_boost_;
}

void WaveFoldModifier::WaveFoldModifierKeyframe::interpolate(const WavetableKeyframe* from_keyframe,
                                                             const WavetableKeyframe* to_keyframe,
                                                             float t) {
  const WaveFoldModifierKeyframe* from = dynamic_cast<const WaveFoldModifierKeyframe*>(from_keyframe);
  const WaveFoldModifierKeyframe* to = dynamic_cast<const WaveFoldModifierKeyframe*>(to_keyframe);

  wave_fold_boost_ = linearTween(from->wave_fold_boost_, to->wave_fold_boost_, t);
}

void WaveFoldModifier::WaveFoldModifierKeyframe::render(vital::WaveFrame* wave_frame) {
  float max_value = std::max(1.0f, wave_frame->getMaxZeroOffset());

  for (int i = 0; i < vital::WaveFrame::kWaveformSize; ++i) {
    float value = vital::utils::clamp(wave_frame->time_domain[i] / max_value, -1.0f, 1.0f);
    float adjusted_value = max_value * wave_fold_boost_ * asinf(value);

    wave_frame->time_domain[i] = sinf(adjusted_value);
  }
  wave_frame->toFrequencyDomain();
}

json WaveFoldModifier::WaveFoldModifierKeyframe::stateToJson() {
  json data = WavetableKeyframe::stateToJson();
  data["fold_boost"] = wave_fold_boost_;
  return data;
}

void WaveFoldModifier::WaveFoldModifierKeyframe::jsonToState(json data) {
  WavetableKeyframe::jsonToState(data);
  wave_fold_boost_ = data["fold_boost"];
}

WavetableKeyframe* WaveFoldModifier::createKeyframe(int position) {
  WaveFoldModifierKeyframe* keyframe = new WaveFoldModifierKeyframe();
  interpolate(keyframe, position);
  return keyframe;
}

void WaveFoldModifier::render(vital::WaveFrame* wave_frame, float position) {
  interpolate(&compute_frame_, position);
  compute_frame_.render(wave_frame);
}

WavetableComponentFactory::ComponentType WaveFoldModifier::getType() {
  return WavetableComponentFactory::kWaveFolder;
}

WaveFoldModifier::WaveFoldModifierKeyframe* WaveFoldModifier::getKeyframe(int index) {
  WavetableKeyframe* wavetable_keyframe = keyframes_[index].get();
  return dynamic_cast<WaveFoldModifier::WaveFoldModifierKeyframe*>(wavetable_keyframe);
}
