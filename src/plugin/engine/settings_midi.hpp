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
                  sts::key::k_cc_to_param_id_map,
                  (String)fmt::FormatInline<32>("{}:{}", cc_num, param_id));
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
                     sts::key::k_cc_to_param_id_map,
                     (String)fmt::FormatInline<32>("{}:{}", cc_num, param_id));
}

PUBLIC Bitset<128> PersistentCcsForParam(sts::Settings const& settings, u32 param_id) {
    Bitset<128> result {};

    // TODO
    (void)settings;
    (void)param_id;

    return result;
}

} // namespace midi_settings
