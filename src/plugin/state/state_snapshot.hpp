// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "effect_infos.hpp"
#include "instrument.hpp"
#include "param_info.hpp"

struct StateSnapshot {
    f32& LinearParam(ParamIndex index) { return param_values[ToInt(index)]; }
    f32 LinearParam(ParamIndex index) const { return param_values[ToInt(index)]; }

    bool operator==(StateSnapshot const& other) const = default;
    bool operator!=(StateSnapshot const& other) const = default;

    Optional<sample_lib::IrId> ir_id {};
    InitialisedArray<InstrumentId, k_num_layers> inst_ids {InstrumentType::None};
    Array<f32, k_num_parameters> param_values {};
    Array<EffectType, k_num_effect_types> fx_order {};
    Array<Bitset<128>, k_num_parameters> param_learned_ccs {};
};

enum class StateSource { PresetFile, Daw };

struct StateSnapshotMetadata {
    StateSnapshotMetadata Clone(Allocator& a, CloneType clone_type = CloneType::Shallow) const {
        auto _ = clone_type;
        return {
            .name_or_path = name_or_path.Clone(a, CloneType::Shallow),
        };
    }

    Optional<String> Path() const {
        if (path::IsAbsolute(name_or_path)) return name_or_path;
        return k_nullopt;
    }
    String Name() const { return path::FilenameWithoutExtension(name_or_path); }

    String name_or_path;
};

struct StateSnapshotWithMetadata {
    StateSnapshot state;
    StateSnapshotMetadata metadata;
};
