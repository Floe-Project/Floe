// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shepard_tone_source.h"
#include "wavetable_component_factory.h"

ShepardToneSource::ShepardToneSource() {
  loop_frame_ = std::make_unique<WaveSourceKeyframe>();
}

ShepardToneSource::~ShepardToneSource() { }

void ShepardToneSource::render(vital::WaveFrame* wave_frame, float position) {
  if (numFrames() == 0)
    return;

  WaveSourceKeyframe* keyframe = getKeyframe(0);
  vital::WaveFrame* key_wave_frame = keyframe->wave_frame();
  vital::WaveFrame* loop_wave_frame = loop_frame_->wave_frame();

  for (int i = 0; i < vital::WaveFrame::kWaveformSize / 2; ++i) {
    loop_wave_frame->frequency_domain[i * 2] = key_wave_frame->frequency_domain[i];
    loop_wave_frame->frequency_domain[i * 2 + 1] = 0.0f;
  }

  loop_wave_frame->toTimeDomain();

  compute_frame_->setInterpolationMode(interpolation_mode_);
  compute_frame_->interpolate(keyframe, loop_frame_.get(), position / (vital::kNumOscillatorWaveFrames - 1.0f));
  wave_frame->copy(compute_frame_->wave_frame());
}

WavetableComponentFactory::ComponentType ShepardToneSource::getType() {
  return WavetableComponentFactory::kShepardToneSource;
}