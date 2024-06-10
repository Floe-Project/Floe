// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "wavetable_component.h"

class FrequencyFilterModifier : public WavetableComponent {
  public:
    enum FilterStyle {
      kLowPass,
      kBandPass,
      kHighPass,
      kComb,
      kNumFilterStyles
    };

    class FrequencyFilterModifierKeyframe : public WavetableKeyframe {
      public:
        FrequencyFilterModifierKeyframe();
        virtual ~FrequencyFilterModifierKeyframe() { }

        void copy(const WavetableKeyframe* keyframe) override;
        void interpolate(const WavetableKeyframe* from_keyframe,
                         const WavetableKeyframe* to_keyframe, float t) override;
        void render(vital::WaveFrame* wave_frame) override;
        json stateToJson() override;
        void jsonToState(json data) override;

        float getMultiplier(float index);

        float getCutoff() { return cutoff_; }
        float getShape() { return shape_; }

        void setStyle(FilterStyle style) { style_ = style; }
        void setCutoff(float cutoff) { cutoff_ = cutoff; }
        void setShape(float shape) { shape_ = shape; }
        void setNormalize(bool normalize) { normalize_ = normalize; }

      protected:
        FilterStyle style_;
        bool normalize_;
        float cutoff_;
        float shape_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyFilterModifierKeyframe)
      };

      FrequencyFilterModifier() : style_(kLowPass), normalize_(true) { }
      virtual ~FrequencyFilterModifier() { }

      virtual WavetableKeyframe* createKeyframe(int position) override;
      virtual void render(vital::WaveFrame* wave_frame, float position) override;
      virtual WavetableComponentFactory::ComponentType getType() override;
      virtual json stateToJson() override;
      virtual void jsonToState(json data) override;

      FrequencyFilterModifierKeyframe* getKeyframe(int index);

      FilterStyle getStyle() { return style_; }
      bool getNormalize() { return normalize_; }

      void setStyle(FilterStyle style) { style_ = style; }
      void setNormalize(bool normalize) { normalize_ = normalize; }

    protected:
      FilterStyle style_;
      bool normalize_;
      FrequencyFilterModifierKeyframe compute_frame_;

      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyFilterModifier)
};

