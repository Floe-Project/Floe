// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "circular_queue.h"
#include "synth_module.h"
#include "note_handler.h"
#include "tuning.h"

class LineGenerator;

namespace vital {
  class PeakMeter;
  class ReorderableEffectChain;
  class EffectsModulationHandler;
  class Sample;
  class StereoMemory;
  class SynthLfo;
  class Upsampler;
  class Value;
  class ValueSwitch;
  class Wavetable;

  class SoundEngine : public SynthModule, public NoteHandler {
    public:
      static constexpr int kDefaultOversamplingAmount = 2;
      static constexpr int kDefaultSampleRate = 44100;

      SoundEngine();
      virtual ~SoundEngine();

      void init() override;
      void processWithInput(const poly_float* audio_in, int num_samples) override;
      void correctToTime(double seconds) override;

      int getNumPressedNotes();
      void connectModulation(const modulation_change& change);
      void disconnectModulation(const modulation_change& change);
      int getNumActiveVoices();
      ModulationConnectionBank& getModulationBank();
      mono_float getLastActiveNote() const;

      void setTuning(const Tuning* tuning);
      void setOversamplingAmount(int oversampling_amount, int sample_rate);

      void allSoundsOff() override;
      void allNotesOff(int sample) override;
      void allNotesOff(int sample, int channel) override;
      void allNotesOffRange(int sample, int from_channel, int to_channel);

      void noteOn(int note, mono_float velocity, int sample, int channel) override;
      void noteOff(int note, mono_float lift, int sample, int channel) override;
      void setModWheel(mono_float value, int channel);
      void setModWheelAllChannels(mono_float value);
      void setPitchWheel(mono_float value, int channel);
      void setZonedPitchWheel(mono_float value, int from_channel, int to_channel);
      void disableUnnecessaryModSources();
      void enableModSource(const std::string& source);
      void disableModSource(const std::string& source);
      bool isModSourceEnabled(const std::string& source);
      const StereoMemory* getEqualizerMemory();

      void setBpm(mono_float bpm);
      void setAftertouch(mono_float note, mono_float value, int sample, int channel);
      void setChannelAftertouch(int channel, mono_float value, int sample);
      void setChannelRangeAftertouch(int from_channel, int to_channel, mono_float value, int sample);
      void setChannelSlide(int channel, mono_float value, int sample);
      void setChannelRangeSlide(int from_channel, int to_channel, mono_float value, int sample);
      Wavetable* getWavetable(int index);
      Sample* getSample();
      LineGenerator* getLfoSource(int index);

      void sustainOn(int channel);
      void sustainOff(int sample, int channel);
      void sostenutoOn(int channel);
      void sostenutoOff(int sample, int channel);

      void sustainOnRange(int from_channel, int to_channel);
      void sustainOffRange(int sample, int from_channel, int to_channel);
      void sostenutoOnRange(int from_channel, int to_channel);
      void sostenutoOffRange(int sample, int from_channel, int to_channel);
      force_inline int getOversamplingAmount() const { return last_oversampling_amount_; }

      void checkOversampling();

    private:
      EffectsModulationHandler* modulation_handler_;
      Upsampler* upsampler_;
      ReorderableEffectChain* effect_chain_;

      int last_oversampling_amount_;
      int last_sample_rate_;
      Value* oversampling_;
      Value* bps_;
      Value* legato_;
      PeakMeter* peak_meter_;

      CircularQueue<Processor*> modulation_processors_;

      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundEngine)
  };
} // namespace vital

