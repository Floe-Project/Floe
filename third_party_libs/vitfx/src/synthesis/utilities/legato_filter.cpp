// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "legato_filter.h"
#include "utils.h"

namespace vital {

  LegatoFilter::LegatoFilter() : Processor(kNumInputs, kNumOutputs, true), last_value_(kVoiceOff) { }

  void LegatoFilter::process(int num_samples) {
    output(kRetrigger)->clearTrigger();

    poly_mask trigger_mask = input(kTrigger)->source->trigger_mask;
    if (trigger_mask.anyMask() == 0)
      return;

    poly_float trigger_value = input(kTrigger)->source->trigger_value;
    poly_int trigger_offset = input(kTrigger)->source->trigger_offset;
    poly_mask legato_mask = poly_float::equal(input(kLegato)->at(0), 0.0f);
    legato_mask |= poly_float::notEqual(trigger_value, kVoiceOn);
    legato_mask |= poly_float::notEqual(last_value_, kVoiceOn);
    trigger_mask &= legato_mask;

    output(kRetrigger)->trigger(trigger_mask, trigger_value, trigger_offset);
    last_value_ = utils::maskLoad(last_value_, trigger_value, trigger_mask);
  }
} // namespace vital
