// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "synth_constants.h"
#include "line_generator.h"

namespace vital {
  class ModulationConnectionProcessor : public SynthModule {
    public:
      enum {
        kModulationInput,
        kModulationAmount,
        kModulationPower,
        kReset,
        kNumInputs
      };

      enum {
        kModulationOutput,
        kModulationPreScale,
        kModulationSource,
        kNumOutputs
      };

      ModulationConnectionProcessor(int index);
      virtual ~ModulationConnectionProcessor() { }

      void init() override;
      void process(int num_samples) override;
      void processAudioRate(int num_samples, const Output* source);
      void processAudioRateLinear(int num_samples, const Output* source);
      void processAudioRateRemapped(int num_samples, const Output* source);
      void processAudioRateMorphed(int num_samples, const Output* source, poly_float power);
      void processAudioRateRemappedAndMorphed(int num_samples, const Output* source, poly_float power);
      void processControlRate(const Output* source);

      virtual Processor* clone() const override { return new ModulationConnectionProcessor(*this); }

      void initializeBaseValue(Value* base_value) { current_value_ = base_value; }
      void initializeMapping() { map_generator_->initLinear(); }

      mono_float currentBaseValue() const { return current_value_->value(); }
      void setBaseValue(mono_float value) { current_value_->set(value); }

      bool isPolyphonicModulation() const { return polyphonic_; }
      void setPolyphonicModulation(bool polyphonic) { polyphonic_ = polyphonic; }
      bool isBipolar() const { return bipolar_->value() != 0.0f; }
      void setBipolar(bool bipolar) { bipolar_->set(bipolar ? 1.0f : 0.0f); }
      bool isStereo() const { return stereo_->value() != 0.0f; }
      void setStereo(bool stereo) { stereo_->set(stereo ? 1.0f : 0.0f); }
      bool isBypassed() const { return bypass_->value() != 0.0f; }
      force_inline void setDestinationScale(mono_float scale) { *destination_scale_ = scale; }
      force_inline int index() const { return index_; }

      LineGenerator* lineMapGenerator() { return map_generator_.get(); }

    protected:
      int index_;
      bool polyphonic_;
      Value* current_value_;
      Value* bipolar_;
      Value* stereo_;
      Value* bypass_;

      poly_float power_;
      poly_float modulation_amount_;

      std::shared_ptr<mono_float> destination_scale_;
      mono_float last_destination_scale_;
      std::shared_ptr<LineGenerator> map_generator_;

      JUCE_LEAK_DETECTOR(ModulationConnectionProcessor)
  };
} // namespace vital

