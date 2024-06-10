// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"
#include "utils.h"

namespace vital {

  class Feedback : public Processor {
    public:
      Feedback(bool control_rate = false) : Processor(1, 1, control_rate), buffer_index_(0) {
        utils::zeroBuffer(buffer_, kMaxBufferSize);
      }

      virtual ~Feedback() { }

      virtual Processor* clone() const override { return new Feedback(*this); }
      virtual void process(int num_samples) override;
      virtual void refreshOutput(int num_samples);

      force_inline void tick(int i) {
        buffer_[i] = input(0)->source->buffer[i];
      }

    protected:
      poly_float buffer_[kMaxBufferSize];
      int buffer_index_;

  };

  namespace cr {
    class Feedback : public ::vital::Feedback {
      public:
        Feedback() : ::vital::Feedback(true), last_value_(0.0f) { }

        void process(int num_samples) override {
          last_value_ = input()->at(0);
        }

        virtual Processor* clone() const override { return new cr::Feedback(*this); }

        void refreshOutput(int num_samples) override {
          output()->buffer[0] = last_value_;
        }

        void reset(poly_mask reset_mask) override {
          last_value_ = 0.0f;
          output()->buffer[0] = last_value_;
        }

      protected:
        poly_float last_value_;
    };
  } // namespace cr
} // namespace vital

