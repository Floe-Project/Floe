// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: MIT

#include "TwoStageFFTConvolver.h"

struct StereoConvolver {
    int num_frames;
    fftconvolver::TwoStageFFTConvolver convolvers[2];
};

StereoConvolver* CreateStereoConvolver() { return new StereoConvolver(); }

int NumFrames(StereoConvolver& convolver) { return convolver.num_frames; }

void DestroyStereoConvolver(StereoConvolver* convolver) { delete convolver; }

void Init(StereoConvolver& convolver, float const* interleaved_stereo, int num_frames) {
    // IMPROVE: this is slow?

    auto samples = new float[(unsigned)num_frames];

    for (int chan = 0; chan < 2; ++chan) {
        for (int frame = 0; frame < num_frames; ++frame)
            samples[frame] = interleaved_stereo[frame * 2 + chan];

        // Just trial and error with these values; these seem to be most efficient
        constexpr size_t k_head_block_size = 512;
        constexpr size_t k_tail_block_size = 16384;

        convolver.convolvers[chan].init(k_head_block_size, k_tail_block_size, samples, num_frames);
    }

    delete[] samples;
}

void Process(StereoConvolver& convolver,
             float const* input_l,
             float const* input_r,
             float* output_l,
             float* output_r,
             int num_frames) {
    convolver.convolvers[0].process(input_l, output_l, (unsigned)num_frames);
    convolver.convolvers[1].process(input_r, output_r, (unsigned)num_frames);
}

void Zero(StereoConvolver& convolver) {
    convolver.convolvers[0].zero();
    convolver.convolvers[1].zero();
}
