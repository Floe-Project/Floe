// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

struct LFO {
    enum class Waveform { None, Sine, Triangle, Sawtooth, Square };

    // returns [-1, 1]
    f32 Tick() {
        // We track the phase of the LFO using the method described by Remy Muller:
        // https://www.musicdsp.org/en/latest/Synthesis/152-another-lfo-class.html
        auto const index = phase >> 24; // top 8 bits is the table index which overflows automatically
        auto const frac =
            (phase & 0x00FFFFFF) * (1.0f / (f32)(1 << 24)); // bottom 24 bits is the fractional part

        phase += phase_increment_per_tick;

        auto const output = LinearInterpolate(frac, table[index], table[index + 1]);
        return (output + 1.0f) - 1.0f;
    }

    void SetRate(f32 sample_rate, f32 new_rate_hz) {
        phase_increment_per_tick = (u32)((256.0f * new_rate_hz / sample_rate) * (f32)(1 << 24));
    }

    void SetWaveform(Waveform w) {
        switch (w) {
            case Waveform::Sine: {
                for (u32 i = 0; i <= 256; i++)
                    table[i] = trig_table_lookup::SinTurnsPositive((f32)i / 256.0f);

                break;
            }
            case Waveform::Triangle: {
                for (u32 i = 0; i < 64; i++) {
                    table[i] = (f32)i / 64.0f;
                    table[i + 64] = (64 - (f32)i) / 64.0f;
                    table[i + 128] = -(f32)i / 64.0f;
                    table[i + 192] = -(64 - (f32)i) / 64.0f;
                }
                table[256] = 0.0f;
                break;
            }
            case Waveform::Sawtooth: {
                for (u32 i = 0; i < 256; i++)
                    table[i] = 2.0f * ((f32)i / 255.0f) - 1.0f;
                table[256] = -1.0f;
                break;
            }
            case Waveform::Square: {
                for (u32 i = 0; i < 128; i++) {
                    table[i] = 1.0f;
                    table[i + 128] = -1.0f;
                }
                table[256] = 1.0f;
                break;
            }
            case Waveform::None: break;
        }
        waveform = w;
    }

    Waveform waveform {Waveform::None};
    u32 phase = 0;
    u32 phase_increment_per_tick = 0;
    f32 table[257] = {}; // table[0] == table[256] to avoid edge case
};
