// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "startup.h"
#include "load_save.h"
#include "JuceHeader.h"
#include "synth_base.h"

void Startup::doStartupChecks(MidiManager* midi_manager, vital::StringLayout* layout) {
  if (!LoadSave::isInstalled())
    return;

  if (LoadSave::wasUpgraded())
    LoadSave::saveVersionConfig();

  LoadSave::loadConfig(midi_manager, layout);
}

bool Startup::isComputerCompatible() {
  #if defined(__ARM_NEON__)
  return true;
  #else
  return SystemStats::hasSSE2() || SystemStats::hasAVX2();
  #endif
}
