// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common.h"
#include "poly_utils.h"

namespace vital {

  template<mono_float(*function)(mono_float), size_t resolution>
  class OneDimLookup {
    static constexpr int kExtraValues = 4;
    public:
      OneDimLookup(float scale = 1.0f) {
        scale_ = resolution / scale;
        for (int i = 0; i < resolution + kExtraValues; ++i) {
          mono_float t = (i - 1.0f) / (resolution - 1.0f);
          lookup_[i] = function(t * scale);
        }
      }

      ~OneDimLookup() { }

      force_inline poly_float cubicLookup(poly_float value) const {
        poly_float boost = value * scale_;
        poly_int indices = utils::clamp(utils::toInt(boost), 0, resolution);
        poly_float t = boost - utils::toFloat(indices);

        matrix interpolation_matrix = utils::getCatmullInterpolationMatrix(t);
        matrix value_matrix = utils::getValueMatrix(lookup_, indices);
        value_matrix.transpose();

        return interpolation_matrix.multiplyAndSumRows(value_matrix);
      }

    private:
      mono_float lookup_[resolution + kExtraValues];
      mono_float scale_;

  };
} // namespace vital

