// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "trigger_random.h"

#include <cstdlib>

namespace vital {

  TriggerRandom::TriggerRandom() : Processor(1, 1, true), value_(0.0f), random_generator_(0.0f, 1.0f) { }

  void TriggerRandom::process(int num_samples) {
    poly_mask trigger_mask = getResetMask(kReset);
    if (trigger_mask.anyMask()) {
      for (int i = 0; i < poly_float::kSize; i += 2) {
        if ((poly_float(1.0f) & trigger_mask)[i]) {
          mono_float rand_value = random_generator_.next();
          value_.set(i, rand_value);
          value_.set(i + 1, rand_value);
        }
      }
    }

    output()->buffer[0] = value_;
  }
} // namespace vital
