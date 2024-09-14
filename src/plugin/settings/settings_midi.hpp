// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "infos/param_info.hpp"
#include "settings/settings_file.hpp"

namespace midi_settings {

PUBLIC void
AddPersistentCcToParamMapping(Settings::Midi& midi, ArenaAllocator& arena, u8 cc_num, u32 param_id) {
    for (auto cc_map = midi.cc_to_param_mapping; cc_map != nullptr; cc_map = cc_map->next) {
        if (cc_map->cc_num == cc_num) {
            for (auto param = cc_map->param; param != nullptr; param = param->next)
                if (param_id == param->id) return;

            ASSERT(cc_map->param);
            auto new_param = arena.New<Settings::Midi::CcToParamMapping::Param>();
            new_param->next = cc_map->param;
            new_param->id = param_id;
            cc_map->param = new_param;
            return;
        }
    }

    auto new_ccs = arena.New<Settings::Midi::CcToParamMapping>();
    new_ccs->cc_num = cc_num;
    new_ccs->next = nullptr;
    new_ccs->param = arena.New<Settings::Midi::CcToParamMapping::Param>();
    new_ccs->param->next = nullptr;
    new_ccs->param->id = param_id;
    if (!midi.cc_to_param_mapping) {
        midi.cc_to_param_mapping = new_ccs;
    } else {
        new_ccs->next = midi.cc_to_param_mapping;
        midi.cc_to_param_mapping = new_ccs;
    }
}

PUBLIC bool Initialise(Settings::Midi& midi, ArenaAllocator& arena, bool file_is_brand_new) {
    if (file_is_brand_new) {
        AddPersistentCcToParamMapping(midi, arena, 1, k_param_infos[ToInt(ParamIndex::MasterDynamics)].id);
        return true;
    }
    return false;
}

PUBLIC void RemovePersistentCcToParamMapping(Settings::Midi& midi, u8 cc_num, u32 param_id) {
    Settings::Midi::CcToParamMapping* prev_cc_map = nullptr;
    for (auto cc_map = midi.cc_to_param_mapping; cc_map != nullptr; cc_map = cc_map->next) {
        if (cc_map->cc_num == cc_num) {
            Settings::Midi::CcToParamMapping::Param* prev_param = nullptr;
            for (auto param = cc_map->param; param != nullptr; param = param->next) {
                if (param->id == param_id) {
                    if (prev_param)
                        prev_param->next = param->next;
                    else if (prev_cc_map)
                        prev_cc_map->next = cc_map->next;
                    else
                        midi.cc_to_param_mapping = nullptr;
                    return;
                }
                prev_param = param;
            }
        }
        prev_cc_map = cc_map;
    }
}

PUBLIC Bitset<128> PersistentCcsForParam(Settings::Midi const& midi, u32 param_id) {
    Bitset<128> result {};

    for (auto cc_map = midi.cc_to_param_mapping; cc_map != nullptr; cc_map = cc_map->next) {
        for (auto param = cc_map->param; param != nullptr; param = param->next)
            if (param->id == param_id) result.Set(cc_map->cc_num);
    }

    return result;
}

} // namespace midi_settings
