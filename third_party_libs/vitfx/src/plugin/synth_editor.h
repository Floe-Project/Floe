// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "border_bounds_constrainer.h"
#include "synth_plugin.h"
#include "full_interface.h"
#include "synth_gui_interface.h"

class SynthEditor : public AudioProcessorEditor, public SynthGuiInterface {
  public:
    SynthEditor(SynthPlugin&);

    void paint(Graphics&) override { }
    void resized() override;
    void setScaleFactor(float newScale) override;

    void updateFullGui() override;

  private:
    SynthPlugin& synth_;
    bool was_animating_;
    BorderBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditor)
};

