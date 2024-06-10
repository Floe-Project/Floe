// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "wavetable_component.h"

class PhaseModifier : public WavetableComponent {
  public:
    enum PhaseStyle {
      kNormal,
      kEvenOdd,
      kHarmonic,
      kHarmonicEvenOdd,
      kClear,
      kNumPhaseStyles
    };

    class PhaseModifierKeyframe : public WavetableKeyframe {
      public:
        PhaseModifierKeyframe();
        virtual ~PhaseModifierKeyframe() { }

        void copy(const WavetableKeyframe* keyframe) override;
        void interpolate(const WavetableKeyframe* from_keyframe,
                         const WavetableKeyframe* to_keyframe, float t) override;
        void render(vital::WaveFrame* wave_frame) override;
        json stateToJson() override;
        void jsonToState(json data) override;

        float getPhase() { return phase_; }
        float getMix() { return mix_; }
        void setPhase(float phase) { phase_ = phase; }
        void setMix(float mix) { mix_ = mix; }
        void setPhaseStyle(PhaseStyle style) { phase_style_ = style; }

      protected:
        float phase_;
        float mix_;
        PhaseStyle phase_style_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseModifierKeyframe)
    };

    PhaseModifier() : phase_style_(kNormal) { }
    virtual ~PhaseModifier() = default;

    virtual WavetableKeyframe* createKeyframe(int position) override;
    virtual void render(vital::WaveFrame* wave_frame, float position) override;
    virtual WavetableComponentFactory::ComponentType getType() override;
    virtual json stateToJson() override;
    virtual void jsonToState(json data) override;

    PhaseModifierKeyframe* getKeyframe(int index);

    void setPhaseStyle(PhaseStyle style) { phase_style_ = style; }
    PhaseStyle getPhaseStyle() const { return phase_style_; }

  protected:
    PhaseModifierKeyframe compute_frame_;
    PhaseStyle phase_style_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseModifier)
};

