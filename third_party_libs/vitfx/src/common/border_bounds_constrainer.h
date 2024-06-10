// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"

class FullInterface;

class BorderBoundsConstrainer : public ComponentBoundsConstrainer {
  public:
    BorderBoundsConstrainer() : ComponentBoundsConstrainer(), gui_(nullptr) { }

    virtual void checkBounds(Rectangle<int>& bounds, const Rectangle<int>& previous,
                             const Rectangle<int>& limits,
                             bool stretching_top, bool stretching_left,
                             bool stretching_bottom, bool stretching_right) override;

    virtual void resizeStart() override;
    virtual void resizeEnd() override;

    void setBorder(const BorderSize<int>& border) { border_ = border; }
    void setGui(FullInterface* gui) { gui_ = gui; }

  protected:
    FullInterface* gui_;
    BorderSize<int> border_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BorderBoundsConstrainer)
};

