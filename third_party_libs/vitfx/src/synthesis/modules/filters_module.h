// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_module.h"
#include "filter_module.h"

namespace vital {

  class FiltersModule : public SynthModule {
    public:
      enum {
        kFilter1Input,
        kFilter2Input,
        kKeytrack,
        kMidi,
        kReset,
        kNumInputs
      };

      FiltersModule();
      virtual ~FiltersModule() { }

      void process(int num_samples) override;
      void processParallel(int num_samples);
      void processSerialForward(int num_samples);
      void processSerialBackward(int num_samples);

      void init() override;
      Processor* clone() const override { return new FiltersModule(*this); }

      const Value* getFilter1OnValue() const { return filter_1_->getOnValue(); }
      const Value* getFilter2OnValue() const { return filter_2_->getOnValue(); }

      void setOversampleAmount(int oversample) override {
        SynthModule::setOversampleAmount(oversample);
        filter_1_input_->ensureBufferSize(oversample * kMaxBufferSize);
        filter_2_input_->ensureBufferSize(oversample * kMaxBufferSize);
      }

    protected:
      FilterModule* filter_1_;
      FilterModule* filter_2_;

      Value* filter_1_filter_input_;
      Value* filter_2_filter_input_;

      std::shared_ptr<Output> filter_1_input_;
      std::shared_ptr<Output> filter_2_input_;

      JUCE_LEAK_DETECTOR(FiltersModule)
  };
} // namespace vital

