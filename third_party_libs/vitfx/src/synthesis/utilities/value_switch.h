// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "value.h"

namespace vital {

  class ValueSwitch : public cr::Value {
    public:
      enum {
        kValue,
        kSwitch,
        kNumOutputs
      };

      ValueSwitch(mono_float value = 0.0f);

      virtual Processor* clone() const override { return new ValueSwitch(*this); }
      virtual void process(int num_samples) override { }
      virtual void set(poly_float value) override;

      void addProcessor(Processor* processor) { processors_.push_back(processor); }

      virtual void setOversampleAmount(int oversample) override;

    private:
      void setBuffer(int source);
      void setSource(int source);

      std::vector<Processor*> processors_;

      JUCE_LEAK_DETECTOR(ValueSwitch)
  };
} // namespace vital

