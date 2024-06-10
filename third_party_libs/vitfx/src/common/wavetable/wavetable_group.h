// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "wave_frame.h"
#include "wavetable_component.h"

namespace vital {
  class Wavetable;
} // namespace vital

class WavetableGroup {
  public:
    WavetableGroup() { }

    int getComponentIndex(WavetableComponent* component);
    void addComponent(WavetableComponent* component) {
      components_.push_back(std::unique_ptr< WavetableComponent>(component));
    }
    void removeComponent(int index);
    void moveUp(int index);
    void moveDown(int index);
    void reset();
    void prerender();

    int numComponents() const { return static_cast<int>(components_.size()); }
    WavetableComponent* getComponent(int index) const { return components_[index].get(); }
    bool isShepardTone();
    void render(vital::WaveFrame* wave_frame, float position) const;
    void renderTo(vital::Wavetable* wavetable);
    void loadDefaultGroup();
    int getLastKeyframePosition();

    json stateToJson();
    void jsonToState(json data);

  protected:
    vital::WaveFrame compute_frame_;
    std::vector<std::unique_ptr<WavetableComponent>> components_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableGroup)
};

