// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "wavetable_component.h"

class WaveWindowModifier : public WavetableComponent {
  public:
    enum WindowShape {
      kCos,
      kHalfSin,
      kLinear,
      kSquare,
      kWiggle,
      kNumWindowShapes
    };

    static float applyWindow(WindowShape window_shape, float t);

    class WaveWindowModifierKeyframe : public WavetableKeyframe {
      public:
        WaveWindowModifierKeyframe();
        virtual ~WaveWindowModifierKeyframe() { }

        void copy(const WavetableKeyframe* keyframe) override;
        void interpolate(const WavetableKeyframe* from_keyframe,
                         const WavetableKeyframe* to_keyframe, float t) override;
        void render(vital::WaveFrame* wave_frame) override;
        json stateToJson() override;
        void jsonToState(json data) override;

        void setLeft(float left) { left_position_ = left; }
        void setRight(float right) { right_position_ = right; }
        float getLeft() { return left_position_; }
        float getRight() { return right_position_; }

        void setWindowShape(WindowShape window_shape) { window_shape_ = window_shape; }

      protected:
        inline float applyWindow(float t) { return WaveWindowModifier::applyWindow(window_shape_, t); }

        float left_position_;
        float right_position_;
        WindowShape window_shape_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveWindowModifierKeyframe)
    };

    WaveWindowModifier() : window_shape_(kCos) { }
    virtual ~WaveWindowModifier() { }

    virtual WavetableKeyframe* createKeyframe(int position) override;
    virtual void render(vital::WaveFrame* wave_frame, float position) override;
    virtual WavetableComponentFactory::ComponentType getType() override;
    virtual json stateToJson() override;
    virtual void jsonToState(json data) override;

    WaveWindowModifierKeyframe* getKeyframe(int index);

    void setWindowShape(WindowShape window_shape) { window_shape_ = window_shape; }
    WindowShape getWindowShape() { return window_shape_; }

  protected:
    WaveWindowModifierKeyframe compute_frame_;
    WindowShape window_shape_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveWindowModifier)
};

