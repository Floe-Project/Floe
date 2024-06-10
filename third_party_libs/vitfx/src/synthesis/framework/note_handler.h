// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common.h"

namespace vital {

  class NoteHandler {
    public:
      virtual ~NoteHandler() { }
      virtual void allSoundsOff() = 0;
      virtual void allNotesOff(int sample) = 0;
      virtual void allNotesOff(int sample, int channel) = 0;
      virtual void noteOn(int note, mono_float velocity, int sample, int channel) = 0;
      virtual void noteOff(int note, mono_float lift, int sample, int channel) = 0;
  };
} // namespace vital

