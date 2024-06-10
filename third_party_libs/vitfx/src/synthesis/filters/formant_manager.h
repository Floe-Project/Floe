// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor_router.h"
#include "synth_constants.h"

namespace vital {

  class DigitalSvf;

  class FormantManager : public ProcessorRouter {
    public:
      static constexpr mono_float kMinResonance = 4.0f;
      static constexpr mono_float kMaxResonance = 30.0f;

      FormantManager(int num_formants = 4);
      virtual ~FormantManager() { }

      virtual void init() override;
      void reset(poly_mask reset_mask) override;
      void hardReset() override;

      virtual Processor* clone() const override {
        return new FormantManager(*this);
      }

      DigitalSvf* getFormant(int index = 0) { return formants_[index]; }
      int numFormants() { return static_cast<int>(formants_.size()); }

    protected:
      std::vector<DigitalSvf*> formants_;

  };
} // namespace vital

