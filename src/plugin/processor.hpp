// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <clap/process.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "utils/thread_extra/atomic_queue.hpp"

#include "audio_processing_context.hpp"
#include "common/constants.hpp"
#include "effects/effect_bitcrush.hpp"
#include "effects/effect_chorus.hpp"
#include "effects/effect_compressor_stillwell_majortom.hpp"
#include "effects/effect_convo.hpp"
#include "effects/effect_delay.hpp"
#include "effects/effect_distortion.hpp"
#include "effects/effect_filter_iir.hpp"
#include "effects/effect_phaser.hpp"
#include "effects/effect_reverb.hpp"
#include "effects/effect_stereo_widen.hpp"
#include "host_thread_pool.hpp"
#include "layer_processor.hpp"
#include "param.hpp"
#include "param_info.hpp"
#include "plugin.hpp"
#include "processing/smoothed_value_system.hpp"
#include "processing/volume_fade.hpp"
#include "state/state_snapshot.hpp"
#include "voices.hpp"

enum class EventForAudioThreadType : u8 {
    ParamChanged,
    ParamGestureBegin,
    ParamGestureEnd,
    FxOrderChanged,
    ReloadAllAudioState,
    ConvolutionIRChanged,
    LayerInstrumentChanged,
    StartNote,
    EndNote,
    RemoveMidiLearn,
};

struct GuiChangedParam {
    f32 value;
    ParamIndex param;
    bool host_should_not_record;
};

struct GuiStartedChangingParam {
    ParamIndex param;
};

struct GuiEndedChangingParam {
    ParamIndex param;
};

struct GuiNoteClicked {
    u7 key;
    f32 velocity;
};

struct GuiNoteClickReleased {
    u7 key;
};

struct RemoveMidiLearn {
    ParamIndex param;
    u7 midi_cc;
};

struct LayerInstrumentChanged {
    u32 layer_index;
};

using EventForAudioThread =
    TaggedUnion<EventForAudioThreadType,
                TypeAndTag<GuiChangedParam, EventForAudioThreadType::ParamChanged>,
                TypeAndTag<GuiStartedChangingParam, EventForAudioThreadType::ParamGestureBegin>,
                TypeAndTag<GuiEndedChangingParam, EventForAudioThreadType::ParamGestureEnd>,
                TypeAndTag<GuiNoteClicked, EventForAudioThreadType::StartNote>,
                TypeAndTag<GuiNoteClickReleased, EventForAudioThreadType::EndNote>,
                TypeAndTag<LayerInstrumentChanged, EventForAudioThreadType::LayerInstrumentChanged>,
                TypeAndTag<RemoveMidiLearn, EventForAudioThreadType::RemoveMidiLearn>>;

using EffectsArray = Array<Effect*, k_num_effect_types>;

void MoveEffectToNewSlot(EffectsArray& effects, Effect* effect_to_move, usize slot);
usize FindSlotInEffects(EffectsArray const& effects, Effect* fx);

u64 EncodeEffectsArray(EffectsArray const& arr);
u64 EncodeEffectsArray(Array<EffectType, k_num_effect_types> const& arr);
EffectsArray DecodeEffectsArray(u64 val, EffectsArray const& unordered_effects);

using Parameters = UninitialisedArray<Parameter, k_num_parameters>;

bool EffectIsOn(Parameters const& params, Effect*);

template <usize k_bits>
requires(k_bits != 0)
class AtomicBitset {
  public:
    static constexpr usize k_bits_per_element = sizeof(u64) * 8;
    static constexpr ptrdiff_t k_num_elements =
        (k_bits / k_bits_per_element) + ((k_bits % k_bits_per_element == 0) ? 0 : 1);
    using Bool64 = u64;

    AtomicBitset() {}

    void SetToValue(usize bit, bool value) {
        if (value)
            Set(bit);
        else
            Clear(bit);
    }

    Bool64 Clear(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_data[bit / k_bits_per_element].FetchAnd(~mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Set(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_data[bit / k_bits_per_element].FetchOr(mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Flip(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_data[bit / k_bits_per_element].FetchXor(mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Get(usize bit) const {
        ASSERT(bit < k_bits);
        return m_data[bit / k_bits_per_element].Load(LoadMemoryOrder::Relaxed) &
               (u64(1) << bit % k_bits_per_element);
    }

    // NOTE: these Blockwise methods are not atomic in terms of the _whole_ bitset, but they will be atomic in
    // regard to each 64-bit block - and that might be good enough for some needs

    void AssignBlockwise(Bitset<k_bits> other) {
        auto const other_raw = other.parts;
        for (auto const i : Range(m_data.size))
            m_data[i].Store(other_raw[i], StoreMemoryOrder::Relaxed);
    }

    Bitset<k_bits> GetBlockwise() const {
        Bitset<k_bits> result;
        for (auto const i : Range(m_data.size))
            result.parts[i] = m_data[i].Load(LoadMemoryOrder::Relaxed);
        return result;
    }

    void SetAllBlockwise() {
        for (auto& block : m_data)
            block.store(~(u64)0);
    }

    void ClearAllBlockwise() {
        for (auto& block : m_data)
            block.store(0);
    }

    Bitset<k_bits> ExchangeClearAllBlockwise() {
        Bitset<k_bits> result;
        for (auto const i : Range(m_data.Size()))
            result.Raw()[i] = m_data[i].exchange(0);
        return result;
    }

  private:
    Array<Atomic<u64>, k_num_elements> m_data {};
};

struct AudioProcessor {
    AudioProcessor(clap_host const& host);
    ~AudioProcessor();

    clap_host const& host;

    FloeSmoothedValueSystem smoothed_value_system;
    ArenaAllocator audio_data_allocator {PageAllocator::Instance()};
    AudioProcessingContext audio_processing_context {};

    int restart_voices_for_layer_bitset {};
    bool fx_need_another_frame_of_processing = {};

    // IMPROVE: rather than have atomics here for the ccs, would FIFO communication be better?
    Array<AtomicBitset<128>, k_num_parameters> param_learned_ccs {};
    Array<Atomic<TimePoint>, k_num_parameters> time_when_cc_moved_param {};

    Atomic<OptionalIndex<s32>> midi_learn_param_index {};

    enum class FadeType { None, OutAndIn, OutAndRestartVoices };
    FadeType whole_engine_volume_fade_type {};
    VolumeFade whole_engine_volume_fade {};

    u32 previous_block_size = 0;

    StereoPeakMeter peak_meter = {};
    Optional<HostThreadPool> host_thread_pool;

    f32 dynamics_value_01 {};
    f32 velocity_to_volume_01 {};
    Bitset<k_num_layers> solo {};
    Bitset<k_num_layers> mute {};

    static constexpr usize k_max_num_events = 128;
    AtomicQueue<EventForAudioThread, k_max_num_events, NumProducers::Many, NumConsumers::One>
        events_for_audio_thread;
    AtomicQueue<EventForAudioThread, k_max_num_events, NumProducers::One, NumConsumers::One>
        param_events_for_audio_thread;

    Bitset<k_num_parameters> pending_param_changes;

    enum MainThreadCallbackFlags {
        MainThreadCallbackFlagsUpdateGui = 1 << 0,
        MainThreadCallbackFlagsRescanParameters = 1 << 1,
    };

    struct {
        AtomicBitset<128> notes_currently_held;
        Atomic<u32> flags {0}; // MainThreadCallbackFlags bitset
    } for_main_thread;

    clap_process_status previous_process_status {-1};

    VoicePool voice_pool {};

    Parameters params;

    Array<LayerProcessor, k_num_layers> layer_processors {
        LayerProcessor {smoothed_value_system, 0, params.data + k_num_layer_parameters * 0, host},
        LayerProcessor {smoothed_value_system, 1, params.data + k_num_layer_parameters * 1, host},
        LayerProcessor {smoothed_value_system, 2, params.data + k_num_layer_parameters * 2, host},
    };
    DynamicArray<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>> lifetime_extended_insts {
        Malloc::Instance()};

    FloeSmoothedValueSystem::FloatId const master_vol_smoother_id {smoothed_value_system.CreateSmoother()};

    Distortion distortion;
    BitCrush bit_crush;
    Compressor compressor;
    FilterEffect filter_effect;
    StereoWiden stereo_widen;
    Chorus chorus;
    Reverb reverb;
    Delay delay;
    Phaser phaser;
    ConvolutionReverb convo;

    // The effects indexable by EffectType
    EffectsArray const effects_ordered_by_type;

    Atomic<u64> desired_effects_order {EncodeEffectsArray(effects_ordered_by_type)};
    EffectsArray actual_fx_order {effects_ordered_by_type};

    bool activated = false;

    PluginCallbacks<AudioProcessor> processor_callbacks;
};

void SetInstrument(AudioProcessor& processor, u32 layer_index, Instrument const& instrument);
void SetConvolutionIrAudioData(AudioProcessor& processor, AudioData const* audio_data);

// doesn't set instruments or convolution because they require loaded audio data which is often available at a
// later time
void ApplyNewState(AudioProcessor& processor, StateSnapshot const& state, StateSource source);

StateSnapshot MakeStateSnapshot(AudioProcessor const& processor);

void ParameterJustStartedMoving(AudioProcessor& processor, ParamIndex index);
void ParameterJustStoppedMoving(AudioProcessor& processor, ParamIndex index);

struct ParamChangeFlags {
    u32 host_should_not_record : 1;
};

bool SetParameterValue(AudioProcessor& processor, ParamIndex index, f32 value, ParamChangeFlags flags);

void SetAllParametersToDefaultValues(AudioProcessor&);
void RandomiseAllParameterValues(AudioProcessor&);
void RandomiseAllEffectParameterValues(AudioProcessor&);

bool IsMidiCCLearnActive(AudioProcessor const& processor);
void LearnMidiCC(AudioProcessor& processor, ParamIndex param);
void CancelMidiCCLearn(AudioProcessor& processor);
void UnlearnMidiCC(AudioProcessor& processor, ParamIndex param, u7 cc_num_to_remove);
Bitset<128> GetLearnedCCsBitsetForParam(AudioProcessor const& processor, ParamIndex param);
bool CcControllerMovedParamRecently(AudioProcessor const& processor, ParamIndex param);
