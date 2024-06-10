// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common.h"

#include <map>
#include <string>

namespace vital {

  struct ValueDetails {
    enum ValueScale {
      kIndexed,
      kLinear,
      kQuadratic,
      kCubic,
      kQuartic,
      kSquareRoot,
      kExponential
    };

    std::string name;
    int version_added = 0;
    mono_float min = 0.0f;
    mono_float max = 1.0f;
    mono_float default_value = 0.0f;

    // post_offset used to offset quadratic and exponential scaling.
    mono_float post_offset = 0.0f;

    mono_float display_multiply = 1.0f;
    ValueScale value_scale = kLinear;
    bool display_invert = false;
    std::string display_units;
    std::string display_name;
    const std::string* string_lookup = nullptr;
    std::string local_description;
  } typedef ValueDetails;

  class ValueDetailsLookup {
    public:
      ValueDetailsLookup();
      const bool isParameter(const std::string& name) const {
        return details_lookup_.count(name);
      }

      const ValueDetails& getDetails(const std::string& name) const {
        auto details = details_lookup_.find(name);
        VITAL_ASSERT(details != details_lookup_.end());
        return details->second;
      }

      const ValueDetails* getDetails(int index) const {
        return details_list_[index];
      }

      std::string getDisplayName(const std::string& name) const {
        return getDetails(name).display_name;
      }

      int getNumParameters() const {
        return static_cast<int>(details_list_.size());
      }

      mono_float getParameterRange(const std::string& name) const {
        auto details = details_lookup_.find(name);
        VITAL_ASSERT(details != details_lookup_.end());
        return details->second.max - details->second.min;
      }

      std::map<std::string, ValueDetails> getAllDetails() const {
        return details_lookup_;
      }

      void addParameterGroup(const ValueDetails* list, int num_parameters, int index,
                             std::string id_prefix, std::string name_prefix, int version = -1);

      void addParameterGroup(const ValueDetails* list, int num_parameters, std::string id,
                             std::string id_prefix, std::string name_prefix, int version = -1);

      static const ValueDetails parameter_list[];
      static const ValueDetails env_parameter_list[];
      static const ValueDetails lfo_parameter_list[];
      static const ValueDetails random_lfo_parameter_list[];
      static const ValueDetails filter_parameter_list[];
      static const ValueDetails osc_parameter_list[];
      static const ValueDetails mod_parameter_list[];

    private:
      std::map<std::string, ValueDetails> details_lookup_;
      std::vector<const ValueDetails*> details_list_;

      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValueDetailsLookup)
  };

  class Parameters {
    public:
      static const ValueDetails& getDetails(const std::string& name) {
        return lookup_.getDetails(name);
      }

      static int getNumParameters() {
        return lookup_.getNumParameters();
      }

      static const ValueDetails* getDetails(int index) {
        return lookup_.getDetails(index);
      }

      static std::string getDisplayName(const std::string& name) {
        return lookup_.getDisplayName(name);
      }

      static const mono_float getParameterRange(const std::string& name) {
        return lookup_.getParameterRange(name);
      }

      static const bool isParameter(const std::string& name) {
        return lookup_.isParameter(name);
      }

      static std::map<std::string, ValueDetails> getAllDetails() {
        return lookup_.getAllDetails();
      }

      static ValueDetailsLookup lookup_;

    private:
      Parameters() { }
  };
} // namespace vital

