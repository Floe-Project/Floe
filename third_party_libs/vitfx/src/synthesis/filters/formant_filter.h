// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "synth_filter.h"

#include "formant_manager.h"

namespace vital {
  class DigitalSvf;

  class FormantFilter : public ProcessorRouter, public SynthFilter {
    public:
      enum FormantStyle {
        kAOIE,
        kAIUO,
        kNumFormantStyles,
        kVocalTract = kNumFormantStyles,
        kTotalFormantFilters
      };

      static constexpr float kCenterMidi = 80.0f;

      FormantFilter(int style = 0);
      virtual ~FormantFilter() { }
      
      void reset(poly_mask reset_mask) override;
      void hardReset() override;
      void init() override;

      virtual Processor* clone() const override { return new FormantFilter(*this); }

      void setupFilter(const FilterState& filter_state) override;

      DigitalSvf* getFormant(int index) { return formant_manager_->getFormant(index); }

    protected:
      FormantManager* formant_manager_;
      int style_;

  };
} // namespace vital

