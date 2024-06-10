// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "formant_manager.h"

#include "digital_svf.h"
#include "operators.h"

namespace vital {

  FormantManager::FormantManager(int num_formants) : ProcessorRouter(0, 1) {
    for (int i = 0; i < num_formants; ++i) {
      DigitalSvf* formant = new DigitalSvf();
      formant->setResonanceBounds(kMinResonance, kMaxResonance);
      formants_.push_back(formant);
      addProcessor(formant);
    }
  }

  void FormantManager::init() {
    int num_formants = static_cast<int>(formants_.size());
    VariableAdd* total = new VariableAdd(num_formants);
    for (DigitalSvf* formant : formants_)
      total->plugNext(formant);

    addProcessor(total);
    total->useOutput(output(0), 0);

    ProcessorRouter::init();
  }

  void FormantManager::reset(poly_mask reset_mask) {
    for (DigitalSvf* formant : formants_)
      getLocalProcessor(formant)->reset(reset_mask);
  }

  void FormantManager::hardReset() {
    for (DigitalSvf* formant : formants_)
      getLocalProcessor(formant)->hardReset();
  }
} // namespace vital
