// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "value.h"

namespace vital {

constexpr int kNumLfos = 8;
constexpr int kNumOscillators = 3;
constexpr int kNumOscillatorWaveFrames = 257;
constexpr int kNumEnvelopes = 6;
constexpr int kNumRandomLfos = 4;
constexpr int kNumMacros = 4;
constexpr int kNumFilters = 2;
constexpr int kNumFormants = 4;
constexpr int kNumChannels = 2;
constexpr int kMaxPolyphony = 33;
constexpr int kMaxActivePolyphony = 32;
constexpr int kLfoDataResolution = 2048;
constexpr int kMaxModulationConnections = 64;

constexpr int kOscilloscopeMemorySampleRate = 22000;
constexpr int kOscilloscopeMemoryResolution = 512;
constexpr int kAudioMemorySamples = 1 << 15;
constexpr int kDefaultWindowWidth = 1400;
constexpr int kDefaultWindowHeight = 820;
constexpr int kMinWindowWidth = 350;
constexpr int kMinWindowHeight = 205;

constexpr int kDefaultKeyboardOffset = 48;
constexpr wchar_t kDefaultKeyboardOctaveUp = 'x';
constexpr wchar_t kDefaultKeyboardOctaveDown = 'z';

namespace constants {
enum SourceDestination { kFilter1, kFilter2, kDualFilters, kEffects, kDirectOut, kNumSourceDestinations };

static SourceDestination toggleFilter1(SourceDestination current_destination, bool on) {
    if (on)
        if (current_destination == vital::constants::kFilter2)
            return vital::constants::kDualFilters;
        else
            return vital::constants::kFilter1;
    else if (current_destination == vital::constants::kDualFilters)
        return vital::constants::kFilter2;
    else if (current_destination == vital::constants::kFilter1)
        return vital::constants::kEffects;

    return current_destination;
}

static SourceDestination toggleFilter2(SourceDestination current_destination, bool on) {
    if (on)
        if (current_destination == vital::constants::kFilter1)
            return vital::constants::kDualFilters;
        else
            return vital::constants::kFilter2;
    else if (current_destination == vital::constants::kDualFilters)
        return vital::constants::kFilter1;
    else if (current_destination == vital::constants::kFilter2)
        return vital::constants::kEffects;

    return current_destination;
}

enum Effect {
    kChorus,
    kCompressor,
    kDelay,
    kDistortion,
    kEq,
    kFilterFx,
    kFlanger,
    kPhaser,
    kReverb,
    kNumEffects
};

enum FilterModel { kAnalog, kDirty, kLadder, kDigital, kDiode, kFormant, kComb, kPhase, kNumFilterModels };

enum RetriggerStyle {
    kFree,
    kRetrigger,
    kSyncToPlayHead,
    kNumRetriggerStyles,
};

constexpr int kNumSyncedFrequencyRatios = 13;
constexpr vital::mono_float kSyncedFrequencyRatios[kNumSyncedFrequencyRatios] = {0.0f,
                                                                                 1.0f / 128.0f,
                                                                                 1.0f / 64.0f,
                                                                                 1.0f / 32.0f,
                                                                                 1.0f / 16.0f,
                                                                                 1.0f / 8.0f,
                                                                                 1.0f / 4.0f,
                                                                                 1.0f / 2.0f,
                                                                                 1.0f,
                                                                                 2.0f,
                                                                                 4.0f,
                                                                                 8.0f,
                                                                                 16.0f};

poly_float const kLeftOne(1.0f, 0.0f);
poly_float const kRightOne(0.0f, 1.0f);
poly_float const kFirstVoiceOne(1.0f, 1.0f, 0.0f, 0.0f);
poly_float const kSecondVoiceOne(0.0f, 0.0f, 1.0f, 1.0f);
poly_float const kStereoSplit = kLeftOne - kRightOne;
poly_float const kPolySqrt2 = kSqrt2;
poly_mask const kFullMask = poly_float::equal(0.0f, 0.0f);
poly_mask const kLeftMask = poly_float::equal(kLeftOne, 1.0f);
poly_mask const kRightMask = poly_float::equal(kRightOne, 1.0f);
poly_mask const kFirstMask = poly_float::equal(kFirstVoiceOne, 1.0f);
poly_mask const kSecondMask = poly_float::equal(kSecondVoiceOne, 1.0f);

cr::Value const kValueZero(0.0f);
cr::Value const kValueOne(1.0f);
cr::Value const kValueTwo(2.0f);
cr::Value const kValueHalf(0.5f);
cr::Value const kValueFifth(0.2f);
cr::Value const kValueTenth(0.1f);
cr::Value const kValuePi(kPi);
cr::Value const kValue2Pi(2.0f * kPi);
cr::Value const kValueSqrt2(kSqrt2);
cr::Value const kValueNegOne(-1.0f);
} // namespace constants
} // namespace vital
