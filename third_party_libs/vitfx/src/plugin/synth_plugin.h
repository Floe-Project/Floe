// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"

#include "synth_base.h"
#include "value_bridge.h"

class ValueBridge;

class SynthPlugin : public SynthBase, public AudioProcessor, public ValueBridge::Listener {
  public:
    static constexpr int kSetProgramWaitMilliseconds = 500;

    SynthPlugin();
    virtual ~SynthPlugin();

    SynthGuiInterface* getGuiInterface() override;
    void beginChangeGesture(const std::string& name) override;
    void endChangeGesture(const std::string& name) override;
    void setValueNotifyHost(const std::string& name, vital::mono_float value) override;
    const CriticalSection& getCriticalSection() override;
    void pauseProcessing(bool pause) override;

    void prepareToPlay(double sample_rate, int buffer_size) override;
    void releaseResources() override;
    void processBlock(AudioSampleBuffer&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const String getName() const override;
    bool supportsMPE() const override { return true; }

    const String getInputChannelName(int channel_index) const override;
    const String getOutputChannelName(int channel_index) const override;
    bool isInputChannelStereoPair(int index) const override;
    bool isOutputChannelStereoPair(int index) const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool silenceInProducesSilenceOut() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { }
    const String getProgramName(int index) override;
    void changeProgramName(int index, const String& new_name) override { }

    void getStateInformation(MemoryBlock& destData) override;
    void setStateInformation(const void* data, int size_in_bytes) override;
    AudioProcessorParameter* getBypassParameter() const override { return bypass_parameter_; }

    void parameterChanged(std::string name, vital::mono_float value) override;

  private:
    ValueBridge* bypass_parameter_;
    double last_seconds_time_;

    AudioPlayHead::CurrentPositionInfo position_info_;

    std::map<std::string, ValueBridge*> bridge_lookup_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthPlugin)
};

