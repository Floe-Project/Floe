// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "JuceHeader.h"
#include "synth_base.h"

#if HEADLESS

class FullInterface { };
class AudioDeviceManager { };

#endif

class FullInterface;
class Authentication;

struct SynthGuiData {
  SynthGuiData(SynthBase* synth_base);

  vital::control_map controls;
  vital::output_map mono_modulations;
  vital::output_map poly_modulations;
  vital::output_map modulation_sources;
  WavetableCreator* wavetable_creators[vital::kNumOscillators];
  SynthBase* synth;
};

class SynthGuiInterface {
  public:
    SynthGuiInterface(SynthBase* synth, bool use_gui = true);
    virtual ~SynthGuiInterface();

    virtual AudioDeviceManager* getAudioDeviceManager() { return nullptr; }
    SynthBase* getSynth() { return synth_; }
    virtual void updateFullGui();
    virtual void updateGuiControl(const std::string& name, vital::mono_float value);
    vital::mono_float getControlValue(const std::string& name);

    void notifyModulationsChanged();
    void notifyModulationValueChanged(int index);
    void connectModulation(std::string source, std::string destination);
    void connectModulation(vital::ModulationConnection* connection);
    void setModulationValues(const std::string& source, const std::string& destination,
                             vital::mono_float amount, bool bipolar, bool stereo, bool bypass);
    void initModulationValues(const std::string& source, const std::string& destination);
    void disconnectModulation(std::string source, std::string destination);
    void disconnectModulation(vital::ModulationConnection* connection);

    void setFocus();
    void notifyChange();
    void notifyFresh();
    void openSaveDialog();
    void externalPresetLoaded(File preset);
    void setGuiSize(float scale);
    FullInterface* getGui() { return gui_.get(); }

  protected:
    SynthBase* synth_;

    std::unique_ptr<FullInterface> gui_;
  
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthGuiInterface)
};

