// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "json/json.h"
#include "synth_constants.h"

using json = nlohmann::json;

class WavetableComponent;

namespace vital {
  class WaveFrame;
} // namespace vital

class WavetableKeyframe {
  public:
    static float linearTween(float point_from, float point_to, float t);
    static float cubicTween(float point_prev, float point_from, float point_to, float point_next,
                            float range_prev, float range, float range_next, float t);

    WavetableKeyframe() : position_(0), owner_(nullptr) { }
    virtual ~WavetableKeyframe() { }

    int index();
    int position() const { return position_; }
    void setPosition(int position) { 
      VITAL_ASSERT(position >= 0 && position < vital::kNumOscillatorWaveFrames);
      position_ = position;
    }

    virtual void copy(const WavetableKeyframe* keyframe) = 0;
    virtual void interpolate(const WavetableKeyframe* from_keyframe,
                             const WavetableKeyframe* to_keyframe, float t) = 0;
    virtual void smoothInterpolate(const WavetableKeyframe* prev_keyframe,
                                   const WavetableKeyframe* from_keyframe,
                                   const WavetableKeyframe* to_keyframe,
                                   const WavetableKeyframe* next_keyframe, float t) { }

    virtual void render(vital::WaveFrame* wave_frame) = 0;
    virtual json stateToJson();
    virtual void jsonToState(json data);

    WavetableComponent* owner() { return owner_; }
    void setOwner(WavetableComponent* owner) { owner_ = owner; }

  protected:
    int position_;
    WavetableComponent* owner_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableKeyframe)
};

