// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"

namespace vital {

class Value : public Processor {
  public:
    enum { kSet, kNumInputs };

    Value(poly_float value = 0.0f, bool control_rate = false);

    virtual Processor *clone() const override { return new Value(*this); }
    virtual void process(int num_samples) override;
    virtual void setOversampleAmount(int oversample) override;

    force_inline mono_float value() const { return value_[0]; }
    virtual void set(poly_float value);

  protected:
    poly_float value_;
};

namespace cr {
class Value : public ::vital::Value {
  public:
    Value(poly_float value = 0.0f) : ::vital::Value(value, true) {}
    virtual Processor *clone() const override { return new Value(*this); }

    void process(int num_samples) override;
};
} // namespace cr
} // namespace vital
