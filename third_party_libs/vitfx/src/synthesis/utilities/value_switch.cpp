// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "value_switch.h"

#include "utils.h"

namespace vital {

  ValueSwitch::ValueSwitch(mono_float value) : cr::Value(value) {
    while (numOutputs() < kNumOutputs)
      addOutput();

    enable(false);
  }

  void ValueSwitch::set(poly_float value) {
    cr::Value::set(value);
    setSource(value[0]);
  }

  void ValueSwitch::setOversampleAmount(int oversample) {
    cr::Value::setOversampleAmount(oversample);
    int num_inputs = numInputs();
    for (int i = 0; i < num_inputs; ++i) {
      input(i)->source->owner->setOversampleAmount(oversample);
    }
    setBuffer(value_[0]);
  }

  force_inline void ValueSwitch::setBuffer(int source) {
    source = utils::iclamp(source, 0, numInputs() - 1);
    output(kSwitch)->buffer = input(source)->source->buffer;
    output(kSwitch)->buffer_size = input(source)->source->buffer_size;
  }

  force_inline void ValueSwitch::setSource(int source) {
    setBuffer(source);

    bool enable_processors = source != 0;
    for (Processor* processor : processors_)
      processor->enable(enable_processors);
  }
} // namespace vital
