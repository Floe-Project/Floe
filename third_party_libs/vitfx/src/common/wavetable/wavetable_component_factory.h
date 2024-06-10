// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"

class WavetableComponent;

class WavetableComponentFactory {
  public:
    enum ComponentType {
      kWaveSource,
      kLineSource,
      kFileSource,
      kNumSourceTypes,
      kShepardToneSource = kNumSourceTypes, // Deprecated

      kBeginModifierTypes = kNumSourceTypes + 1,
      kPhaseModifier = kBeginModifierTypes,
      kWaveWindow,
      kFrequencyFilter,
      kSlewLimiter,
      kWaveFolder,
      kWaveWarp,
      kNumComponentTypes
    };

    static int numComponentTypes() { return kNumComponentTypes; }
    static int numSourceTypes() { return kNumSourceTypes; }
    static int numModifierTypes() { return kNumComponentTypes - kBeginModifierTypes; }

    static WavetableComponent* createComponent(ComponentType type);
    static WavetableComponent* createComponent(const std::string& type);
    static std::string getComponentName(ComponentType type);
    static ComponentType getSourceType(int type) { return static_cast<ComponentType>(type); }
    static ComponentType getModifierType(int type) {
      return (ComponentType)(type + kBeginModifierTypes);
    }

  private:
    WavetableComponentFactory() { }
};

