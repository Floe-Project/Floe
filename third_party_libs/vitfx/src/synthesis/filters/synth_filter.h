// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common.h"
#include "lookup_table.h"
#include "synth_constants.h"

namespace vital {

  class Processor;

  class SynthFilter {
    public:
      static force_inline mono_float computeOnePoleFilterCoefficient(mono_float frequency_ratio) {
        static constexpr float kMaxRads = 0.499f * kPi;
        mono_float scaled = frequency_ratio * vital::kPi;
        return std::tan(std::min(kMaxRads, scaled / (scaled + 1.0f)));
      }

      typedef OneDimLookup<computeOnePoleFilterCoefficient, 2048> CoefficientLookup;
      static const CoefficientLookup coefficient_lookup_;
      static const CoefficientLookup* getCoefficientLookup() { return &coefficient_lookup_; }

      enum {
        kAudio,
        kReset,
        
        kMidiCutoff,
        kResonance,
        kDriveGain,
        kGain,
        kStyle,
        kPassBlend,
        kInterpolateX,
        kInterpolateY,
        kTranspose,
        kSpread,
        kNumInputs
      };

      enum Style {
        k12Db,
        k24Db,
        kNotchPassSwap,
        kDualNotchBand,
        kBandPeakNotch,
        kShelving,
        kNumStyles
      };

      class FilterState {
        public:
          FilterState() : midi_cutoff(1.0f), midi_cutoff_buffer(nullptr),
                          resonance_percent(0.0f), drive(1.0f), drive_percent(0.0f), gain(0.0f),
                          style(0), pass_blend(0.0f), interpolate_x(0.5f), interpolate_y(0.5f), transpose(0.0f) { }

          poly_float midi_cutoff;
          const poly_float* midi_cutoff_buffer;
          poly_float resonance_percent;
          poly_float drive;
          poly_float drive_percent;
          poly_float gain;

          int style;
          poly_float pass_blend;

          poly_float interpolate_x;
          poly_float interpolate_y;
          poly_float transpose;

          void loadSettings(Processor* processor);
      };

      virtual ~SynthFilter() { }

      virtual void setupFilter(const FilterState& filter_state) = 0;

      static SynthFilter* createFilter(constants::FilterModel model);

    protected:
      FilterState filter_state_;

  };
} // namespace vital

