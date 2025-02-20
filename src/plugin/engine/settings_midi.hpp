// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/settings/settings_file.hpp"

namespace midi_settings {

PUBLIC void AddPersistentCcToParamMapping(sts::Settings& settings, u8 cc_num, u32 param_id) {
    ASSERT(cc_num > 0 && cc_num <= 127);
    ASSERT(ParamIdToIndex(param_id));
    sts::AddValue(settings,
                  sts::SectionedKey {sts::key::section::k_cc_to_param_id_map_section, (s64)cc_num},
                  (s64)param_id);
}

PUBLIC bool Initialise(sts::Settings& settings, bool file_is_brand_new) {
    if (file_is_brand_new) {
        AddPersistentCcToParamMapping(settings, 1, k_param_descriptors[ToInt(ParamIndex::MasterDynamics)].id);
        return true;
    }
    return false;
}

PUBLIC void RemovePersistentCcToParamMapping(sts::Settings& settings, u8 cc_num, u32 param_id) {
    sts::RemoveValue(settings,
                     sts::SectionedKey {sts::key::section::k_cc_to_param_id_map_section, (s64)cc_num},
                     (s64)param_id);
}

PUBLIC Bitset<128> PersistentCcsForParam(sts::Settings const& settings, u32 param_id) {
    Bitset<128> result {};

    for (auto const [key_union, value_list_ptr] : settings) {
        auto const sectioned_key = key_union.TryGet<sts::SectionedKey>();
        if (!sectioned_key) continue;
        auto const [section, key] = *sectioned_key;
        if (section != sts::key::section::k_cc_to_param_id_map_section) continue;
        if (key.tag != sts::KeyValueType::Int) continue;

        auto const cc_num = key.Get<s64>();
        if (cc_num < 1 || cc_num > 127) continue;

        for (auto value = *value_list_ptr; value; value = value->next) {
            if (*value == (s64)param_id) {
                result.Set((usize)cc_num);
                break;
            }
        }
    }

    return result;
}

} // namespace midi_settings
