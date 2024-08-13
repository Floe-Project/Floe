// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "audio_data.hpp"
#include "processing/filters.hpp"
#include "sample_library/sample_library.hpp"

inline void
DoMonoCubicInterp(f32 const* f0, f32 const* f1, f32 const* f2, f32 const* fm1, f32 const x, f32& out) {
    out = f0[0] + (((f2[0] - fm1[0] - 3 * f1[0] + 3 * f0[0]) * x + 3 * (f1[0] + fm1[0] - 2 * f0[0])) * x -
                   (f2[0] + 2 * fm1[0] - 6 * f1[0] + 3 * f0[0])) *
                      x / 6.0f;
}

inline void DoStereoLagrangeInterp(f32 const* f0,
                                   f32 const* f1,
                                   f32 const* f2,
                                   f32 const* fm1,
                                   f32 const x,
                                   f32& l,
                                   f32& r) {
    auto xf =
        x + 1; // x is given in the range 0 to 1 but we want the value between f0 and f1, therefore add 1
    auto xfm1 = x;
    auto xfm2 = xf - 2;
    auto xfm3 = xf - 3;

    // IMPROVE: use shuffle instructions instead of manually setting up the vectors

    f32x4 const v0 {xfm1, xf, xf, xf};
    f32x4 const v1 {-1, 1, 2, 3};
    f32x4 const v2 {xfm2, xfm2, xfm1, xfm1};
    f32x4 const v3 {-2, -1, 1, 2};
    f32x4 const v4 {xfm3, xfm3, xfm3, xfm2};
    f32x4 const v5 {-3, -2, -1, 1};

    f32x4 const vd0 = v0 / v1;
    f32x4 const vd1 = v2 / v3;
    f32x4 const vd2 = v4 / v5;

    auto vt = (vd0 * vd1) * vd2;

    alignas(16) f32 t[4];
    StoreToAligned(t, vt);

    l = fm1[0] * t[0] + f0[0] * t[1] + f1[0] * t[2] + f2[0] * t[3];
    r = fm1[1] * t[0] + f0[1] * t[1] + f1[1] * t[2] + f2[1] * t[3];
}

struct NormalisedLoop {
    u32 start {};
    u32 end {};
    u32 crossfade {};
    bool ping_pong {};
};

template <typename Type>
inline Type ClampCrossfadeSize(Type crossfade, Type start, Type end, Type total, bool is_ping_pong) {
    ASSERT(crossfade >= 0);
    ASSERT(start >= 0);
    ASSERT(end >= 0);
    auto loop_size = end - start;
    ASSERT(loop_size >= 0);
    Type result;
    if (!is_ping_pong)
        result = Min(crossfade, loop_size, start);
    else
        result = Max<Type>(0, Min(crossfade, start, total - end, loop_size));
    return result;
}

inline NormalisedLoop NormaliseLoop(sample_lib::Loop loop, usize utotal_frame_count) {
    // This is a bit weird? but I think it's probably important to some already-existing patches
    u32 const smallest_loop_size_allowed = Max((u32)((f64)utotal_frame_count * 0.001), 32u);

    auto const total_frame_count = CheckedCast<s64>(utotal_frame_count);
    NormalisedLoop result {
        .ping_pong = loop.ping_pong,
    };

    result.start = ({
        u32 s;
        if (loop.start_frame < 0)
            s = (u32)Max<s64>(0, (total_frame_count + 1) + loop.start_frame);
        else
            s = (u32)loop.start_frame;
        s;
    });

    result.end = ({
        u32 e;
        if (loop.end_frame < 0)
            e = (u32)Max<s64>(0, (total_frame_count + 1) + loop.end_frame);
        else
            e = (u32)Clamp<s64>(loop.end_frame, 0, total_frame_count);

        if (loop.start_frame + smallest_loop_size_allowed > loop.end_frame)
            e = (u32)Min(loop.end_frame + smallest_loop_size_allowed, total_frame_count);
        e;
    });

    ASSERT(result.end >= result.start);

    result.crossfade = (u32)ClampCrossfadeSize<s64>(loop.crossfade_frames,
                                                    result.start,
                                                    result.end,
                                                    total_frame_count,
                                                    loop.ping_pong);

    return result;
}

namespace loop_and_reverse_flags {

enum Bits : u32 {
    CurrentlyReversed = 1 << 0,
    InFirstLoop = 1 << 1,
    LoopedManyTimes = 1 << 2,

    InLoopingRegion = InFirstLoop | LoopedManyTimes,
};

inline u32 CorrectLoopFlagsIfNeeded(u32 flags, NormalisedLoop const loop, f64 frame_pos) {
    auto const end = (f64)loop.end;
    auto const start = (f64)loop.start;

    if (frame_pos >= start && frame_pos < end) {
        if (!(flags & InLoopingRegion)) flags |= InFirstLoop;
    } else {
        flags &= ~InLoopingRegion;
    }
    return flags;
}

} // namespace loop_and_reverse_flags

inline bool IncrementSamplePlaybackPos(Optional<NormalisedLoop> const& loop,
                                       u32& playback_mode,
                                       f64& frame_pos,
                                       f64 pitch_ratio,
                                       f64 num_frames) {
    using namespace loop_and_reverse_flags;

    bool const going_forward = !(playback_mode & CurrentlyReversed);

    if (going_forward)
        frame_pos += pitch_ratio;
    else
        frame_pos -= pitch_ratio;

    if (loop) {
        auto const end = (f64)loop->end;
        auto const start = (f64)loop->start;

        if (going_forward) {
            if (frame_pos >= start && !(playback_mode & InLoopingRegion)) playback_mode |= InFirstLoop;

            if ((playback_mode & InLoopingRegion) && frame_pos >= end) {
                playback_mode &= ~InFirstLoop;
                playback_mode |= LoopedManyTimes;
                if (!loop->ping_pong) {
                    frame_pos = start + (frame_pos - end);
                } else {
                    frame_pos = end - Fmod(frame_pos - end, end);
                    playback_mode ^= CurrentlyReversed;
                }
            }
        } else {
            if (frame_pos < end && !(playback_mode & InLoopingRegion)) playback_mode |= InFirstLoop;

            if ((playback_mode & InLoopingRegion) && frame_pos < start) {
                playback_mode &= ~InFirstLoop;
                playback_mode |= LoopedManyTimes;
                if (!loop->ping_pong) {
                    frame_pos = end - (start - frame_pos);
                } else {
                    frame_pos = start + (start - frame_pos);
                    playback_mode ^= CurrentlyReversed;
                }
            }
        }
    }

    if (frame_pos < 0 || frame_pos >= num_frames) return false;
    return true;
}

inline void SampleGetData(AudioData const& s,
                          Optional<NormalisedLoop> const opt_loop,
                          u32 loop_and_reverse_flags,
                          f64 frame_pos,
                          f32& l,
                          f32& r,
                          bool recurse = false) {
    using namespace loop_and_reverse_flags;
    auto const loop = opt_loop.NullableValue();

    auto const frames_in_sample = s.num_frames;
    ASSERT(s.num_frames != 0);
    auto const last_frame = frames_in_sample - 1;

    auto const forward = !(loop_and_reverse_flags & CurrentlyReversed);

    if (loop) {
        ASSERT(loop->end <= frames_in_sample);
        ASSERT(loop->start < frames_in_sample);
        ASSERT(loop->end > loop->start);
    }
    ASSERT(frame_pos < frames_in_sample);

    auto const frame_index = (int)frame_pos;
    auto x = (f32)frame_pos - (f32)frame_index;

    s64 xm1;
    s64 x0;
    s64 x1;
    s64 x2;

    if (forward) {
        xm1 = frame_index - 1;
        x0 = frame_index;
        x1 = frame_index + 1;
        x2 = frame_index + 2;

        if (loop && loop->ping_pong && (loop_and_reverse_flags & InLoopingRegion)) {
            if (loop_and_reverse_flags & LoopedManyTimes && xm1 < loop->start)
                xm1 = loop->start;
            else if (xm1 < 0)
                xm1 = 0;
            if (x1 >= loop->end) x1 = loop->end - 1;
            if (x2 >= loop->end) x2 = (loop->end - 1) - (x2 - loop->end);
        } else if (loop && !loop->ping_pong && (loop_and_reverse_flags & InLoopingRegion) &&
                   loop->crossfade == 0) {
            if (xm1 < 0) xm1 = loop->end + xm1;
            if (x1 >= loop->end) x1 = loop->start + (x1 - loop->end);
            if (x2 >= loop->end) x2 = loop->start + (x2 - loop->end);
        } else {
            if (xm1 < 0) xm1 = 0;
            if (x1 >= frames_in_sample) x1 = last_frame;
            if (x2 >= frames_in_sample) x2 = last_frame;
        }
    } else {
        x = 1 - x;
        xm1 = frame_index + 1;
        x0 = frame_index;
        x1 = frame_index - 1;
        x2 = frame_index - 2;

        if (loop && loop->ping_pong && (loop_and_reverse_flags & InLoopingRegion)) {
            if (loop_and_reverse_flags & LoopedManyTimes) {
                if (xm1 >= loop->end) xm1 = loop->end - 1;
            }
            if (x1 < loop->start) x1 = loop->start;
            if (x2 < loop->start) x2 = loop->start + ((loop->start - x2) - 1);
        } else if (loop && !loop->ping_pong && (loop_and_reverse_flags & InLoopingRegion) &&
                   loop->crossfade == 0) {
            if (xm1 >= loop->end) xm1 = loop->start;
            if (x1 < 0) x1 = loop->end + x1;
            if (x2 < 0) x2 = loop->end + x2;
        } else {
            if (xm1 >= frames_in_sample) xm1 = last_frame;
            if (x1 < 0) x1 = 0;
            if (x2 < 0) x2 = 0;
        }
    }

    ASSERT(x0 >= 0 && x0 < frames_in_sample);

    f32 const* sample_data = s.interleaved_samples.data;
    auto* f0 = sample_data + x0 * s.channels;
    auto* f1 = sample_data + x1 * s.channels;
    auto* f2 = sample_data + x2 * s.channels;
    auto* fm1 = sample_data + xm1 * s.channels;
    Array<f32, 2> outs = {};
    if (s.channels == 1) {
        DoMonoCubicInterp(f0, f1, f2, fm1, x, outs[0]);
        outs[1] = outs[0];
    } else if (s.channels == 2) {
        DoStereoLagrangeInterp(f0, f1, f2, fm1, x, outs[0], outs[1]);
    } else {
        PanicIfReached();
    }

    if (loop && loop->crossfade) {
        f32 crossfade_pos = 0;
        bool is_crossfading = false;
        f32 xfade_r = 0;
        f32 xfade_l = 0;
        if (!loop->ping_pong) {
            auto const xfade_fade_out_start =
                loop->end - loop->crossfade; // the bit before the loop end point
            auto const xfade_fade_in_start =
                loop->start - loop->crossfade; // the bit before the loop start point

            if (frame_pos >= xfade_fade_out_start && frame_pos < loop->end) {
                if (forward || (!forward && (loop_and_reverse_flags & LoopedManyTimes))) {
                    auto frames_info_fade = frame_pos - xfade_fade_out_start;

                    SampleGetData(s,
                                  opt_loop,
                                  loop_and_reverse_flags & CurrentlyReversed,
                                  xfade_fade_in_start + frames_info_fade,
                                  xfade_l,
                                  xfade_r,
                                  true);
                    crossfade_pos = (f32)frames_info_fade / (f32)loop->crossfade;
                    ASSERT(crossfade_pos >= 0 && crossfade_pos <= 1);

                    is_crossfading = true;
                }
            }
        } else if (loop_and_reverse_flags & LoopedManyTimes) { // Ping-pong
            ASSERT(!recurse);
            ASSERT(loop->ping_pong);

            if (forward && (frame_pos <= (loop->start + loop->crossfade)) && frame_pos >= loop->start) {
                auto frames_into_fade = frame_pos - loop->start;
                auto fade_pos = (f64)loop->start - frames_into_fade;
                SampleGetData(s, opt_loop, CurrentlyReversed, fade_pos, xfade_l, xfade_r, true);
                crossfade_pos = 1.0f - ((f32)frames_into_fade / (f32)loop->crossfade);
                ASSERT(crossfade_pos >= 0 && crossfade_pos <= 1);

                is_crossfading = true;
            } else if (!forward && frame_pos >= (loop->end - loop->crossfade) && frame_pos < loop->end) {
                auto frames_into_fade = loop->end - frame_pos;
                auto fade_pos = loop->end + frames_into_fade;
                SampleGetData(s, opt_loop, 0, fade_pos, xfade_l, xfade_r, true);
                crossfade_pos = 1.0f - ((f32)frames_into_fade / (f32)loop->crossfade);
                ASSERT(crossfade_pos >= 0 && crossfade_pos <= 1);

                is_crossfading = true;
            }
        }

        if (is_crossfading) {
            f32x4 t {1 - crossfade_pos, crossfade_pos, 1, 1};
            t = Sqrt(t);

            outs[0] *= t[0];
            outs[1] *= t[0];
            xfade_l *= t[1];
            xfade_r *= t[1];

            outs[0] += xfade_l;
            outs[1] += xfade_r;
        }
    }

    l = outs[0];
    r = outs[1];
}

struct IntRange {
    int lo;
    int hi;
};

inline int Overlap(IntRange a, IntRange b) { return Max(0, Min(a.hi, b.hi) - Max(a.lo, b.lo) + 1); }

enum class WaveformAudioSourceType { AudioData, Sine, WhiteNoise };

using WaveformAudioSource =
    TaggedUnion<WaveformAudioSourceType, TypeAndTag<AudioData const*, WaveformAudioSourceType::AudioData>>;

PUBLIC DynamicArray<u8> GetWaveformImageFromSample(WaveformAudioSource source, UiSize size) {
    u32 num_frames = 256;
    if (auto const audio_file = source.TryGet<AudioData const*>()) num_frames = (*audio_file)->num_frames;

    auto const px_size = size.width * size.height * 4;
    DynamicArray<u8> px {PageAllocator::Instance()};
    dyn::Resize(px, (usize)px_size);

    constexpr int k_supersample_scale = 10;
    auto const scaled_width = size.width * k_supersample_scale;
    auto const scaled_height = size.height * k_supersample_scale;

    DynamicArray<IntRange> ranges {PageAllocator::Instance()};
    ranges.Reserve((usize)scaled_width);

    auto mid_y = (int)(scaled_height / 2);
    auto samples_per_pixel = (f32)num_frames / ((f32)scaled_width);

    int min_y = scaled_height - 1;
    int max_y = 0;

    sv_filter::CachedHelpers c_l {};
    sv_filter::CachedHelpers c_r {};
    sv_filter::Data d_l {};
    sv_filter::Data d_r {};
    c_l.Update(44100, 2000, 0.5f);
    c_r.Update(44100, 2000, 0.5f);

    {
        f32 first_sample = 0;
        for (auto const x : Range(scaled_width)) {
            f32 avg_l = 0;
            f32 avg_r = 0;

            f32 const end_sample = first_sample + samples_per_pixel;
            int const first_sample_x = RoundPositiveFloat(first_sample);
            int const end_sample_x = Min((int)num_frames - 1, RoundPositiveFloat(end_sample));
            first_sample = end_sample;
            int const window_size = (end_sample_x + 1) - first_sample_x;

            f32 const max_samples_per_px = 8;
            int const step = Max(1, (int)((f32)window_size / max_samples_per_px));
            int num_sampled = 0;

            for (int i = first_sample_x; i <= end_sample_x; i += step) {
                f32 l;
                f32 r;

                switch (source.tag) {
                    case WaveformAudioSourceType::AudioData: {
                        auto const& audio_data = *source.Get<AudioData const*>();
                        auto frame_ptr = audio_data.interleaved_samples.data + (i * audio_data.channels);
                        l = frame_ptr[0];
                        if (audio_data.channels != 1)
                            r = frame_ptr[1];
                        else
                            r = l;
                        break;
                    }
                    case WaveformAudioSourceType::Sine: {
                        // TODO
                        r = 0;
                        l = 0;
                        break;
                    }
                    case WaveformAudioSourceType::WhiteNoise: {
                        // TODO
                        r = 0;
                        l = 0;
                        break;
                    }
                }

                avg_l += Abs(l);
                avg_r += Abs(r);
                num_sampled++;
            }

            avg_l /= (f32)Max(1, num_sampled);
            avg_r /= (f32)Max(1, num_sampled);
            if (x == 0) {
                f32 l;
                f32 r;
                for (auto _ : Range(150)) {
                    sv_filter::Process(avg_l, l, d_l, sv_filter::Type::Lowpass, c_l);
                    sv_filter::Process(avg_r, r, d_r, sv_filter::Type::Lowpass, c_r);
                }
            }
            sv_filter::Process(avg_l, avg_l, d_l, sv_filter::Type::Lowpass, c_l);
            sv_filter::Process(avg_r, avg_r, d_r, sv_filter::Type::Lowpass, c_r);

            avg_l = Clamp(avg_l, 0.0f, 1.0f);
            avg_r = Clamp(avg_r, 0.0f, 1.0f);

            // arbitrary skew to make the waveform a bit more prominent
            avg_l = Pow(avg_l, 0.6f);
            avg_r = Pow(avg_r, 0.6f);
            ASSERT(avg_l >= 0 && avg_l <= 1);
            ASSERT(avg_r >= 0 && avg_r <= 1);

            struct FloatRange {
                f32 lo;
                f32 hi;
            };
            FloatRange const fval {avg_l * (f32)scaled_height, avg_r * (f32)scaled_height};

            int const val_l = Min((int)fval.lo, scaled_height);
            int const val_r = Min((int)fval.hi, scaled_height);

            auto const start = (int)(mid_y - Abs(val_l / 2));
            int end = (int)(mid_y + Abs(val_r / 2)) +
                      1; // +1 because we always want the centre row of pixels to be filled
            if (end >= scaled_height) end = scaled_height - 1;

            dyn::Append(ranges, IntRange {start, end});
            min_y = Min(min_y, start / k_supersample_scale);
            max_y = Max(max_y, end / k_supersample_scale);
        }
    }

    {
        min_y = Max(0, min_y - 1);
        max_y = Min(size.height - 1, max_y + 1);
        FillMemory({px.data + min_y * size.width * 4, (usize)((max_y - min_y + 1) * size.width * 4)}, 0xff);

        int alpha_chan_px_index = min_y * size.width * 4 + 3;
        for (int y = min_y; y <= max_y; ++y) {
            auto const ss_y = y * k_supersample_scale;
            IntRange const ss_range = {ss_y, ss_y + k_supersample_scale - 1};

            for (auto const x : Range(size.width)) {
                int num_filled_pixels = 0;
                auto const ss_x = x * k_supersample_scale;
                for (int i_x = ss_x; i_x < ss_x + k_supersample_scale; ++i_x)
                    num_filled_pixels += Overlap(ss_range, ranges[(usize)i_x]);

                auto const avg =
                    ((f32)num_filled_pixels * 255.0f) / (k_supersample_scale * k_supersample_scale);
                px[(usize)alpha_chan_px_index] = (u8)(avg + 0.5f);
                alpha_chan_px_index += 4;
            }
        }
    }

    return px;
}
