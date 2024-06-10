// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "wave_source.h"

class ShepardToneSource : public WaveSource {
  public:
    ShepardToneSource();
    virtual ~ShepardToneSource();

    virtual void render(vital::WaveFrame* wave_frame, float position) override;
    virtual WavetableComponentFactory::ComponentType getType() override;
    virtual bool hasKeyframes() override { return false; }

  protected: 
    std::unique_ptr<WaveSourceKeyframe> loop_frame_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShepardToneSource)
};