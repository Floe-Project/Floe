// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "wavetable_component.h"

class WaveFoldModifier : public WavetableComponent {
  public:
    class WaveFoldModifierKeyframe : public WavetableKeyframe {
      public:
        WaveFoldModifierKeyframe();
        virtual ~WaveFoldModifierKeyframe() { }

        void copy(const WavetableKeyframe* keyframe) override;
        void interpolate(const WavetableKeyframe* from_keyframe,
                         const WavetableKeyframe* to_keyframe, float t) override;
        void render(vital::WaveFrame* wave_frame) override;
        json stateToJson() override;
        void jsonToState(json data) override;

        float getWaveFoldBoost() { return wave_fold_boost_; }
        void setWaveFoldBoost(float boost) { wave_fold_boost_ = boost; }

      protected:
        float wave_fold_boost_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveFoldModifierKeyframe)
    };

    WaveFoldModifier() { }
    virtual ~WaveFoldModifier() { }

    virtual WavetableKeyframe* createKeyframe(int position) override;
    virtual void render(vital::WaveFrame* wave_frame, float position) override;
    virtual WavetableComponentFactory::ComponentType getType() override;

    WaveFoldModifierKeyframe* getKeyframe(int index);

  protected:
    WaveFoldModifierKeyframe compute_frame_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveFoldModifier)
};

