// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"

class PitchDetector {
  public:
    static constexpr int kNumPoints = 2520;

    PitchDetector();

    void setSize(int size) { size_ = size; }
    void loadSignal(const float* signal, int size);

    float getPeriodError(float period);
    float findYinPeriod(int max_period);
    float matchPeriod(int max_period);

    const float* data() const { return signal_data_.get(); }

  protected:
    int size_;
    std::unique_ptr<float[]> signal_data_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetector)
};

