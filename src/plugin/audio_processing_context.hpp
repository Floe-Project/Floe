// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "processing/midi.hpp"

struct MidiNoteState {
    void NoteOn(MidiChannelNote note, f32 velocity) {
        ASSERT(velocity >= 0 && velocity <= 1);
        keys_held[note.channel].Set(note.note);
        velocities[note.channel][note.note] = velocity;
        if (sustain_pedal_down.Get(note.channel)) sustain_keys[note.channel].Set(note.note);
    }

    void NoteOff(MidiChannelNote note) { keys_held[note.channel].Clear(note.note); }

    void SustainPedalDown(u4 channel) {
        if (sustain_pedal_down.Get(channel)) return;
        sustain_pedal_down.Set(channel);
        sustain_keys = keys_held;
    }

    Bitset<128> SustainPedalUp(u4 channel) {
        sustain_pedal_down.Clear(channel);
        return Exchange(sustain_keys[channel], {});
    }

    Bitset<128> NotesCurrentlyHeldAllChannels() const {
        Bitset<128> result {};
        for (auto const chan : Range(16))
            result |= NotesHeldIncludingSustained((u4)chan);
        return result;
    }

    Bitset<128> NotesHeldIncludingSustained(u4 channel) const {
        return keys_held[channel] | sustain_keys[channel];
    }

    Array<Bitset<128>, 16> keys_held {};
    Array<Array<f32, 128>, 16> velocities {};
    Array<Bitset<128>, 16> sustain_keys {};
    Bitset<16> sustain_pedal_down {};
};

struct AudioProcessingContext {
    f32 sample_rate = 44100;
    u32 process_block_size_max = 512;
    f64 tempo = 120;
    MidiNoteState midi_note_state;
};
