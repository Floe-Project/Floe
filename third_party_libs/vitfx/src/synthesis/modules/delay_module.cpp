// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "delay_module.h"

#include "delay.h"
#include "memory.h"

namespace vital {

  DelayModule::DelayModule(const Output* beats_per_second) : SynthModule(0, 1), beats_per_second_(beats_per_second) {
    int size = kMaxDelayTime * getSampleRate();
    delay_ = new StereoDelay(size);
    addIdleProcessor(delay_);
  }

  DelayModule::~DelayModule() { }

  void DelayModule::init() {
    delay_->useOutput(output());

    Output* free_frequency = createMonoModControl("delay_frequency");
    Output* frequency = createTempoSyncSwitch("delay", free_frequency->owner, beats_per_second_, false);
    Output* free_frequency_aux = createMonoModControl("delay_aux_frequency");
    Output* frequency_aux = createTempoSyncSwitch("delay_aux", free_frequency_aux->owner, beats_per_second_, false);
    Output* feedback = createMonoModControl("delay_feedback");
    Output* wet = createMonoModControl("delay_dry_wet");

    Output* filter_cutoff = createMonoModControl("delay_filter_cutoff");
    Output* filter_spread = createMonoModControl("delay_filter_spread");

    Value* style = createBaseControl("delay_style");

    delay_->plug(frequency, StereoDelay::kFrequency);
    delay_->plug(frequency_aux, StereoDelay::kFrequencyAux);
    delay_->plug(feedback, StereoDelay::kFeedback);
    delay_->plug(wet, StereoDelay::kWet);
    delay_->plug(style, StereoDelay::kStyle);
    delay_->plug(filter_cutoff, StereoDelay::kFilterCutoff);
    delay_->plug(filter_spread, StereoDelay::kFilterSpread);

    SynthModule::init();
  }

  void DelayModule::setSampleRate(int sample_rate) {
    SynthModule::setSampleRate(sample_rate);
    delay_->setSampleRate(sample_rate);
    delay_->setMaxSamples(kMaxDelayTime * getSampleRate());
  }
  
  void DelayModule::setOversampleAmount(int oversample) {
    SynthModule::setOversampleAmount(oversample);
    delay_->setMaxSamples(kMaxDelayTime * getSampleRate());
  }
  
  void DelayModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);
    delay_->processWithInput(audio_in, num_samples);
  }
} // namespace vital
