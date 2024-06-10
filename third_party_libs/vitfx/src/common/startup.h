// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"

#include <map>

class SynthBase;

namespace vital {
  class StringLayout;
}

class MidiManager;

class Startup {
  public:
    static void doStartupChecks(MidiManager* midi_manager, vital::StringLayout* layout = nullptr);
    static bool isComputerCompatible();

  private:
    Startup() = delete;
};

