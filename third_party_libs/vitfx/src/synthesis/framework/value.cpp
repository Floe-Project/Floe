// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "value.h"

#include "utils.h"

namespace vital {

  Value::Value(poly_float value, bool control_rate) : Processor(kNumInputs, 1, control_rate), value_(value) {
    for (int i = 0; i < output()->buffer_size; ++i)
      output()->buffer[i] = value_;
  }

  void Value::set(poly_float value) {
    value_ = value;
    for (int i = 0; i < output()->buffer_size; ++i)
      output()->buffer[i] = value;
  }

  void Value::process(int num_samples) {
    poly_mask trigger_mask = input(kSet)->source->trigger_mask;
    if (trigger_mask.anyMask()) {
      poly_float trigger_value = input(kSet)->source->trigger_value;
      value_ = utils::maskLoad(value_, trigger_value, trigger_mask);
    }

    poly_float* dest = output()->buffer;
    for (int i = 0; i < num_samples; ++i)
      dest[i] = value_;
  }

  void Value::setOversampleAmount(int oversample) {
    Processor::setOversampleAmount(oversample);
    for (int i = 0; i < output()->buffer_size; ++i)
      output()->buffer[i] = value_;
  }

  void cr::Value::process(int num_samples) {
    poly_mask trigger_mask = input(kSet)->source->trigger_mask;
    if (trigger_mask.anyMask()) {
      poly_float trigger_value = input(kSet)->source->trigger_value;
      value_ = utils::maskLoad(value_, trigger_value, trigger_mask);
    }

    output()->buffer[0] = value_;
  }
} // namespace vital
