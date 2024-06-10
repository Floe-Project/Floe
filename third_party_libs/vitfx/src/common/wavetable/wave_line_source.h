// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "wavetable_component.h"

class WaveLineSource : public WavetableComponent {
  public:
    static constexpr int kDefaultLinePoints = 4;

    class WaveLineSourceKeyframe : public WavetableKeyframe {
      public:
        WaveLineSourceKeyframe();
        virtual ~WaveLineSourceKeyframe() = default;

        void copy(const WavetableKeyframe* keyframe) override;
        void interpolate(const WavetableKeyframe* from_keyframe,
                         const WavetableKeyframe* to_keyframe, float t) override;
        void render(vital::WaveFrame* wave_frame) override;
        json stateToJson() override;
        void jsonToState(json data) override;

        inline std::pair<float, float> getPoint(int index) const { return line_generator_.getPoint(index); }
        inline float getPower(int index) const { return line_generator_.getPower(index); }
        inline void setPoint(std::pair<float, float> point, int index) { line_generator_.setPoint(index, point); }
        inline void setPower(float power, int index) { line_generator_.setPower(index, power); }
        inline void removePoint(int index) { line_generator_.removePoint(index); }
        inline void addMiddlePoint(int index) { line_generator_.addMiddlePoint(index); }
        inline int getNumPoints() const { return line_generator_.getNumPoints(); }
        inline void setSmooth(bool smooth) { line_generator_.setSmooth(smooth); }

        void setPullPower(float power) { pull_power_ = power; }
        float getPullPower() const { return pull_power_; }
        const LineGenerator* getLineGenerator() const { return &line_generator_; }
        LineGenerator* getLineGenerator() { return &line_generator_; }

      protected:
        LineGenerator line_generator_;
        float pull_power_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveLineSourceKeyframe)
    };

    WaveLineSource() : num_points_(kDefaultLinePoints), compute_frame_() { }
    virtual ~WaveLineSource() = default;

    virtual WavetableKeyframe* createKeyframe(int position) override;
    virtual void render(vital::WaveFrame* wave_frame, float position) override;
    virtual WavetableComponentFactory::ComponentType getType() override;
    virtual json stateToJson() override;
    virtual void jsonToState(json data) override;

    void setNumPoints(int num_points);
    int numPoints() { return num_points_; }
    WaveLineSourceKeyframe* getKeyframe(int index);

  protected:
    int num_points_;
    WaveLineSourceKeyframe compute_frame_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveLineSource)
};

