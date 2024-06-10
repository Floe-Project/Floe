// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"

namespace vital {
  class DigitalSvf;

  class EqualizerModule : public SynthModule {
    public:
      EqualizerModule();
      virtual ~EqualizerModule() { }

      void init() override;
      void hardReset() override;
      void enable(bool enable) override;

      void setSampleRate(int sample_rate) override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { return new EqualizerModule(*this); }

      const StereoMemory* getAudioMemory() { return audio_memory_.get(); }

    protected:
      Value* low_mode_;
      Value* band_mode_;
      Value* high_mode_;

      DigitalSvf* high_pass_;
      DigitalSvf* low_shelf_;
      DigitalSvf* notch_;
      DigitalSvf* band_shelf_;
      DigitalSvf* low_pass_;
      DigitalSvf* high_shelf_;

      std::shared_ptr<StereoMemory> audio_memory_;

      JUCE_LEAK_DETECTOR(EqualizerModule) 
  };
} // namespace vital

