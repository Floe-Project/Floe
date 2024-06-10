// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "envelope_module.h"

#include "envelope.h"

namespace vital {

  EnvelopeModule::EnvelopeModule(const std::string& prefix, bool force_audio_rate) :
      SynthModule(kNumInputs, kNumOutputs), prefix_(prefix), force_audio_rate_(force_audio_rate) {
    envelope_ = new Envelope();
    envelope_->useInput(input(kTrigger), Envelope::kTrigger);
    
    envelope_->useOutput(output(kValue), Envelope::kValue);
    envelope_->useOutput(output(kPhase), Envelope::kPhase);
    addProcessor(envelope_);

    setControlRate(!force_audio_rate_);
  }

  void EnvelopeModule::init() {
    Output* delay = createPolyModControl(prefix_ + "_delay");
    Output* attack = createPolyModControl(prefix_ + "_attack");
    Output* hold = createPolyModControl(prefix_ + "_hold");
    Output* decay = createPolyModControl(prefix_ + "_decay");
    Output* sustain = createPolyModControl(prefix_ + "_sustain");
    Output* release = createPolyModControl(prefix_ + "_release");

    Value* attack_power = createBaseControl(prefix_ + "_attack_power");
    Value* decay_power = createBaseControl(prefix_ + "_decay_power");
    Value* release_power = createBaseControl(prefix_ + "_release_power");

    envelope_->plug(delay, Envelope::kDelay);
    envelope_->plug(attack, Envelope::kAttack);
    envelope_->plug(hold, Envelope::kHold);
    envelope_->plug(decay, Envelope::kDecay);
    envelope_->plug(sustain, Envelope::kSustain);
    envelope_->plug(release, Envelope::kRelease);
    envelope_->plug(attack_power, Envelope::kAttackPower);
    envelope_->plug(decay_power, Envelope::kDecayPower);
    envelope_->plug(release_power, Envelope::kReleasePower);
  }
} // namespace vital
