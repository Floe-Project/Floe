// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "line_map.h"

#include "line_generator.h"
#include "utils.h"

namespace vital {
  LineMap::LineMap(LineGenerator* source) : Processor(1, kNumOutputs, true), source_(source) { }

  void LineMap::process(int num_samples) {
    process(input()->at(0));
  }

  void LineMap::process(poly_float phase) {
    mono_float* buffer = source_->getCubicInterpolationBuffer();
    int resolution = source_->resolution();
    poly_float boost = utils::clamp(phase * resolution, 0.0f, resolution);
    poly_int indices = utils::clamp(utils::toInt(boost), 0, resolution - 1);
    poly_float t = boost - utils::toFloat(indices);

    matrix interpolation_matrix = utils::getPolynomialInterpolationMatrix(t);
    matrix value_matrix = utils::getValueMatrix(buffer, indices);

    value_matrix.transpose();

    poly_float result = utils::clamp(interpolation_matrix.multiplyAndSumRows(value_matrix), -1.0f, 1.0f);
    output(kValue)->buffer[0] = result;
    output(kPhase)->buffer[0] = phase;
  }
} // namespace vital
