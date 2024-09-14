// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "volume_fade.hpp"

#include "tests/framework.hpp"

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

TEST_CASE(TestDSPVolumefade) {
    f32 buffer[4410] {};

    SUBCASE("General") {
        VolumeFade volume_fade;
        constexpr f32 k_sample_rate = 44100;
        volume_fade.SetAsFadeIn(k_sample_rate);

        constexpr usize k_sample_to_trigger_fade_out = 1000;
        usize num_samples_to_fade_in = 0;
        for (auto const i : Range(k_sample_to_trigger_fade_out)) {
            auto& s = buffer[i];
            s = volume_fade.GetFade();
            REQUIRE(s >= 0);
            REQUIRE(s <= 1);

            REQUIRE(!volume_fade.IsFadingOut());
            REQUIRE(!volume_fade.IsSilent());
            if (volume_fade.IsFullVolume()) {
                REQUIRE(s == 1);
                if (!num_samples_to_fade_in) num_samples_to_fade_in = i;
            }
        }
        REQUIRE(buffer[0] == 0);
        REQUIRE(volume_fade.IsFullVolume());

        usize num_samples_to_fade_out = 0;
        volume_fade.SetAsFadeOut(k_sample_rate);
        for (usize i = k_sample_to_trigger_fade_out; i < ArraySize(buffer); ++i) {
            auto& s = buffer[i];
            s = volume_fade.GetFade();
            REQUIRE(s >= 0);
            REQUIRE(s <= 1);

            REQUIRE(!volume_fade.IsFadingIn());
            REQUIRE(!volume_fade.IsFullVolume());
            if (volume_fade.IsSilent()) {
                REQUIRE(s == 0);
                if (!num_samples_to_fade_out) num_samples_to_fade_out = i - k_sample_to_trigger_fade_out;
            }
        }
        constexpr usize k_reasonable_min_num_samples = 8;
        REQUIRE(num_samples_to_fade_in >= k_reasonable_min_num_samples);
        REQUIRE(num_samples_to_fade_out >= k_reasonable_min_num_samples);
        REQUIRE(volume_fade.IsSilent());
    }

    SUBCASE("JumpMultipleSteps") {
        VolumeFade fade;
        fade.ForceSetFullVolume();
        fade.SetAsFadeOut(44100, 10);
        REQUIRE(fade.JumpMultipleSteps(100000) == VolumeFade::State::Silent);
        REQUIRE(fade.GetCurrentState() == VolumeFade::State::Silent);
        REQUIRE(fade.JumpMultipleSteps(9) == VolumeFade::State::NoStateChanged);
        fade.ForceSetAsFadeIn(44100, 10);
        REQUIRE(fade.JumpMultipleSteps(100000) == VolumeFade::State::FullVolume);
        REQUIRE(fade.GetCurrentState() == VolumeFade::State::FullVolume);
        REQUIRE(fade.JumpMultipleSteps(9) == VolumeFade::State::NoStateChanged);
    }

    SUBCASE("Change fade mode while fading") {
        for (auto& v : buffer)
            v = 100;

        constexpr f32 k_sample_rate = 1000;

        SUBCASE("Change from fade-in to fade-out") {
            VolumeFade volume_fade(VolumeFade::State::Silent);
            volume_fade.SetAsFadeIn(k_sample_rate, 10);

            int i = 0;
            for (; i < 6; ++i) {
                buffer[i] *= volume_fade.GetFade();
                REQUIRE(buffer[i] >= 0 && buffer[i] <= 100);
            }

            volume_fade.SetAsFadeOut(k_sample_rate, 10);
            for (; i < 40; ++i) {
                buffer[i] *= volume_fade.GetFade();
                REQUIRE(buffer[i] >= 0 && buffer[i] <= 100);
            }
        }

        SUBCASE("Change from fade-out to fade-in") {
            VolumeFade volume_fade(VolumeFade::State::FullVolume);

            volume_fade.SetAsFadeOut(k_sample_rate, 10);
            int i = 0;
            for (; i < 6; ++i) {
                buffer[i] *= volume_fade.GetFade();
                REQUIRE(buffer[i] >= 0 && buffer[i] <= 100);
            }

            volume_fade.SetAsFadeIn(k_sample_rate, 10);
            for (; i < 40; ++i) {
                buffer[i] *= volume_fade.GetFade();
                REQUIRE(buffer[i] >= 0 && buffer[i] <= 100);
            }
        }
    }
    return k_success;
}

TEST_REGISTRATION(RegisterVolumeFadeTests) { REGISTER_TEST(TestDSPVolumefade); }
