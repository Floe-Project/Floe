// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "value.h"

namespace vital {

  class SmoothValue : public Value {
    public:
      static constexpr mono_float kSmoothCutoff = 5.0f;

      SmoothValue(mono_float value = 0.0f);

      virtual Processor* clone() const override {
        return new SmoothValue(*this);
      }

      virtual void process(int num_samples) override;
      void linearInterpolate(int num_samples, poly_mask linear_mask);

      void set(poly_float value) override {
        enable(true);
        value_ = value;
      }

      void setHard(poly_float value) {
        enable(true);
        Value::set(value);
        current_value_ = value;
      }

    private:
      poly_float current_value_;
  };

  namespace cr {
    class SmoothValue : public Value {
      public:
        static constexpr mono_float kSmoothCutoff = 20.0f;

        SmoothValue(mono_float value = 0.0f);

        virtual Processor* clone() const override {
          return new SmoothValue(*this);
        }

        virtual void process(int num_samples) override;

        void setHard(mono_float value) {
          Value::set(value);
          current_value_ = value;
        }

      private:
        poly_float current_value_;

        JUCE_LEAK_DETECTOR(SmoothValue)
    };
  } // namespace cr
} // namespace vital

