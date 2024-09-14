// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

#include "processing_utils/stereo_audio_frame.hpp"

struct StereoPeakMeter {
    struct Snapshot {
        Array<f32, 2> levels {};
    };

    // not thread-safe
    void PrepareToPlay(f32 sample_rate, ArenaAllocator&) {
        constexpr f32 k_falldown_rate_ms = 500.0f;
        m_falldown_divisor = sample_rate * (k_falldown_rate_ms / 1000.0f);

        constexpr f32 k_clipping_detection_window_ms = 500.0f;
        m_clipping_detection_start_counter = (u32)(sample_rate * (k_clipping_detection_window_ms / 1000.0f));

        Zero();
    }

    // not thread-safe
    void Zero() {
        m_levels = {};
        m_smoothed_levels = {};
        m_prev_levels = {};
        m_clipping_detection_counter = {};
        m_clipping_detection_counter_atomic.Store(0, StoreMemoryOrder::Relaxed);
        m_snapshot.Store({}, StoreMemoryOrder::Relaxed);
    }

    // not thread-safe
    void AddBuffer(Span<StereoAudioFrame> frames) {
        for (auto const i : Range(frames.size)) {
            auto const abs_f = Abs(frames[i]);

            if (abs_f.l > m_levels[0]) {
                m_levels[0] = abs_f.l;
                m_falldown_steps[0] = abs_f.l / m_falldown_divisor;
            } else
                m_levels[0] = Max(0.0f, m_levels[0] - m_falldown_steps[0]);

            if (abs_f.r > m_levels[1]) {
                m_levels[1] = abs_f.r;
                m_falldown_steps[1] = abs_f.r / m_falldown_divisor;
            } else
                m_levels[1] = Max(0.0f, m_levels[1] - m_falldown_steps[1]);

            if (abs_f.l > 1 || abs_f.r > 1)
                m_clipping_detection_counter = m_clipping_detection_start_counter;
            else if (m_clipping_detection_counter != 0)
                --m_clipping_detection_counter;

            m_smoothed_levels[0] = SmoothOutput(m_levels[0], m_prev_levels[0]);
            m_smoothed_levels[1] = SmoothOutput(m_levels[1], m_prev_levels[1]);
        }

        m_snapshot.Store(
            Snapshot {
                .levels = m_smoothed_levels,
            },
            StoreMemoryOrder::Relaxed);
        m_clipping_detection_counter_atomic.Store(m_clipping_detection_counter, StoreMemoryOrder::Relaxed);
    }

    // not thread-safe
    bool Silent() const { return m_levels[0] == 0 && m_levels[1] == 0; }

    // thread-safe
    Snapshot GetSnapshot() const { return m_snapshot.Load(LoadMemoryOrder::Relaxed); }

    bool DidClipRecently() const {
        return m_clipping_detection_counter_atomic.Load(LoadMemoryOrder::Relaxed) != 0;
    }

  private:
    static f32 SmoothOutput(f32 output, f32& prev_output) {
        static constexpr f32 k_smoothing_amount = 0.001f;
        f32 const result = prev_output + k_smoothing_amount * (output - prev_output);
        prev_output = result;
        return result;
    }

    Array<f32, 2> m_falldown_steps {};
    Array<f32, 2> m_levels {};
    Array<f32, 2> m_smoothed_levels {};
    Array<f32, 2> m_prev_levels {};
    f32 m_falldown_divisor {};
    u32 m_clipping_detection_start_counter {};
    u32 m_clipping_detection_counter {};

    Atomic<u32> m_clipping_detection_counter_atomic {};
    Atomic<Snapshot> m_snapshot {};
};
