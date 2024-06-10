// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "futils.h"
#include "processor.h"
#include "utils.h"

namespace vital {

class Operator : public Processor {
  public:
    Operator(int num_inputs, int num_outputs, bool control_rate = false)
        : Processor(num_inputs, num_outputs, control_rate) {
        externally_enabled_ = true;
        Processor::enable(false);
    }

    force_inline bool hasEnoughInputs() { return connectedInputs() > 0; }

    void setEnabled() {
        bool will_enable = hasEnoughInputs() && externally_enabled_;
        Processor::enable(will_enable);
        if (!will_enable) {
            for (int i = 0; i < numOutputs(); ++i)
                output(i)->clearBuffer();
            process(1);
        }
    }

    void numInputsChanged() override {
        Processor::numInputsChanged();
        setEnabled();
    }

    void enable(bool enable) override {
        externally_enabled_ = enable;
        setEnabled();
    }

    virtual bool hasState() const override { return false; }

  private:
    Operator() : Processor(0, 0), externally_enabled_(false) {}
    bool externally_enabled_;
};

class Clamp : public Operator {
  public:
    Clamp(mono_float min = -1, mono_float max = 1) : Operator(1, 1), min_(min), max_(max) {}

    virtual Processor *clone() const override { return new Clamp(*this); }

    void process(int num_samples) override;

  private:
    mono_float min_, max_;
};

class Negate : public Operator {
  public:
    Negate() : Operator(1, 1) {}

    virtual Processor *clone() const override { return new Negate(*this); }

    void process(int num_samples) override;

  private:
};

class Inverse : public Operator {
  public:
    Inverse() : Operator(1, 1) {}

    void process(int num_samples) override;

    virtual Processor *clone() const override { return new Inverse(*this); }

  private:
};

class LinearScale : public Operator {
  public:
    LinearScale(mono_float scale = 1.0f) : Operator(1, 1), scale_(scale) {}
    virtual Processor *clone() const override { return new LinearScale(*this); }

    void process(int num_samples) override;

  private:
    mono_float scale_;
};

class Square : public Operator {
  public:
    Square() : Operator(1, 1) {}
    virtual Processor *clone() const override { return new Square(*this); }

    void process(int num_samples) override;

  private:
};

class Add : public Operator {
  public:
    Add() : Operator(2, 1) {}

    virtual Processor *clone() const override { return new Add(*this); }

    void process(int num_samples) override;

  private:
};

class VariableAdd : public Operator {
  public:
    VariableAdd(int num_inputs = 0) : Operator(num_inputs, 1) {}

    virtual Processor *clone() const override { return new VariableAdd(*this); }

    void process(int num_samples) override;

  private:
};

class ModulationSum : public Operator {
  public:
    enum { kReset, kNumStaticInputs };

    ModulationSum(int num_inputs = 0) : Operator(num_inputs + kNumStaticInputs, 1) {
        setPluggingStart(kNumStaticInputs);
    }

    virtual Processor *clone() const override { return new ModulationSum(*this); }

    void process(int num_samples) override;

    virtual bool hasState() const override { return true; }

  private:
    poly_float control_value_;
};

class Subtract : public Operator {
  public:
    Subtract() : Operator(2, 1) {}

    virtual Processor *clone() const override { return new Subtract(*this); }

    void process(int num_samples) override;

  private:
};

class Multiply : public Operator {
  public:
    Multiply() : Operator(2, 1) {}

    virtual ~Multiply() {}

    virtual Processor *clone() const override { return new Multiply(*this); }

    void process(int num_samples) override;

  private:
};

class SmoothMultiply : public Operator {
  public:
    enum { kAudioRate, kControlRate, kReset, kNumInputs };

    SmoothMultiply() : Operator(kNumInputs, 1), multiply_(0.0f) {}

    virtual ~SmoothMultiply() {}

    virtual Processor *clone() const override { return new SmoothMultiply(*this); }

    bool hasState() const override { return true; }

    virtual void process(int num_samples) override;

  protected:
    void processMultiply(int num_samples, poly_float multiply);

    poly_float multiply_;

  private:
};

class SmoothVolume : public SmoothMultiply {
  public:
    static constexpr int kDb = kControlRate;
    static constexpr mono_float kMinDb = -80.0f;
    static constexpr mono_float kDefaultMaxDb = 12.2f;

    SmoothVolume(mono_float max_db = kDefaultMaxDb) : SmoothMultiply(), max_db_(max_db) {}
    virtual ~SmoothVolume() {}

    virtual Processor *clone() const override { return new SmoothVolume(*this); }

    void process(int num_samples) override;

  private:
    mono_float max_db_;
};

class Interpolate : public Operator {
  public:
    enum { kFrom, kTo, kFractional, kReset, kNumInputs };

    Interpolate() : Operator(kNumInputs, 1) {}

    virtual Processor *clone() const override { return new Interpolate(*this); }

    void process(int num_samples) override;

  private:
    poly_float fraction_;
};

class BilinearInterpolate : public Operator {
  public:
    enum { kTopLeft, kTopRight, kBottomLeft, kBottomRight, kXPosition, kYPosition, kNumInputs };

    static const int kPositionStart = kTopLeft;

    BilinearInterpolate() : Operator(kNumInputs, 1) {}

    virtual Processor *clone() const override { return new BilinearInterpolate(*this); }

    void process(int num_samples) override;

  private:
};

class SampleAndHoldBuffer : public Operator {
  public:
    SampleAndHoldBuffer() : Operator(1, 1) {}

    virtual Processor *clone() const override { return new SampleAndHoldBuffer(*this); }

    void process(int num_samples) override;

  private:
};

class StereoEncoder : public Operator {
  public:
    enum { kAudio, kEncodingValue, kMode, kNumInputs };

    enum { kSpread, kRotate, kNumStereoModes };

    StereoEncoder(bool decoding = false) : Operator(kNumInputs, 1), cos_mult_(0.0f), sin_mult_(0.0f) {
        decoding_mult_ = 1.0f;
        if (decoding) decoding_mult_ = -1.0f;
    }

    virtual Processor *clone() const override { return new StereoEncoder(*this); }

    void process(int num_samples) override;
    void processRotate(int num_samples);
    void processCenter(int num_samples);

    bool hasState() const override { return true; }

  protected:
    poly_float cos_mult_;
    poly_float sin_mult_;
    mono_float decoding_mult_;

  private:
};

class TempoChooser : public Operator {
  public:
    enum { kFrequencyMode, kTempoMode, kDottedMode, kTripletMode, kKeytrack, kNumSyncModes };

    enum {
        kFrequency,
        kTempoIndex,
        kBeatsPerSecond,
        kSync,
        kMidi,
        kKeytrackTranspose,
        kKeytrackTune,
        kNumInputs
    };

    TempoChooser() : Operator(kNumInputs, 1, true) {}

    virtual Processor *clone() const override { return new TempoChooser(*this); }

    void process(int num_samples) override;

  private:
};

namespace cr {

class Clamp : public Operator {
  public:
    Clamp(mono_float min = -1, mono_float max = 1) : Operator(1, 1, true), min_(min), max_(max) {}

    virtual Processor *clone() const override { return new Clamp(*this); }

    void process(int num_samples) override { output()->buffer[0] = utils::clamp(input()->at(0), min_, max_); }

  private:
    mono_float min_, max_;
};

class LowerBound : public Operator {
  public:
    LowerBound(mono_float min = 0.0f) : Operator(1, 1, true), min_(min) {}

    virtual Processor *clone() const override { return new LowerBound(*this); }

    void process(int num_samples) override { output()->buffer[0] = utils::max(input()->at(0), min_); }

  private:
    mono_float min_;
};

class UpperBound : public Operator {
  public:
    UpperBound(mono_float max = 0.0f) : Operator(1, 1, true), max_(max) {}

    virtual Processor *clone() const override { return new UpperBound(*this); }

    void process(int num_samples) override { output()->buffer[0] = utils::min(input()->at(0), max_); }

  private:
    mono_float max_;
};

class Add : public Operator {
  public:
    Add() : Operator(2, 1, true) {}

    virtual Processor *clone() const override { return new Add(*this); }

    void process(int num_samples) override { output()->buffer[0] = input(0)->at(0) + input(1)->at(0); }

  private:
};

class Multiply : public Operator {
  public:
    Multiply() : Operator(2, 1, true) {}

    virtual ~Multiply() {}

    virtual Processor *clone() const override { return new Multiply(*this); }

    void process(int num_samples) override { output()->buffer[0] = input(0)->at(0) * input(1)->at(0); }

  private:
};

class Interpolate : public Operator {
  public:
    enum { kFrom, kTo, kFractional, kNumInputs };

    Interpolate() : Operator(kNumInputs, 1, true) {}

    virtual Processor *clone() const override { return new Interpolate(*this); }

    void process(int num_samples) override {
        poly_float from = input(kFrom)->at(0);
        poly_float to = input(kTo)->at(0);
        poly_float fraction = input(kFractional)->at(0);
        output()->buffer[0] = utils::interpolate(from, to, fraction);
    }

  private:
};

class Square : public Operator {
  public:
    Square() : Operator(1, 1, true) {}
    virtual Processor *clone() const override { return new Square(*this); }

    void process(int num_samples) override {
        poly_float value = utils::max(input()->at(0), 0.0f);
        output()->buffer[0] = value * value;
    }

  private:
};

class Cube : public Operator {
  public:
    Cube() : Operator(1, 1, true) {}
    virtual Processor *clone() const override { return new Cube(*this); }

    void process(int num_samples) override {
        poly_float value = utils::max(input()->at(0), 0.0f);
        output()->buffer[0] = value * value * value;
    }

  private:
};

class Quart : public Operator {
  public:
    Quart() : Operator(1, 1, true) {}
    virtual Processor *clone() const override { return new Quart(*this); }

    void process(int num_samples) override {
        poly_float value = utils::max(input()->at(0), 0.0f);
        value *= value;
        output()->buffer[0] = value * value;
    }

  private:
};

class Quadratic : public Operator {
  public:
    Quadratic(mono_float offset) : Operator(1, 1, true), offset_(offset) {}
    virtual Processor *clone() const override { return new Quadratic(*this); }

    void process(int num_samples) override {
        poly_float value = utils::max(input()->at(0), 0.0f);
        output()->buffer[0] = value * value + offset_;
    }

  private:
    mono_float offset_;
};

class Cubic : public Operator {
  public:
    Cubic(mono_float offset) : Operator(1, 1, true), offset_(offset) {}
    virtual Processor *clone() const override { return new Cubic(*this); }

    void process(int num_samples) override {
        poly_float value = utils::max(input()->at(0), 0.0f);
        output()->buffer[0] = value * value * value + offset_;
    }

  private:
    mono_float offset_;
};

class Quartic : public Operator {
  public:
    Quartic(mono_float offset) : Operator(1, 1, true), offset_(offset) {}
    virtual Processor *clone() const override { return new Quartic(*this); }

    void process(int num_samples) override {
        poly_float value = utils::max(input()->at(0), 0.0f);
        value *= value;
        output()->buffer[0] = value * value + offset_;
    }

  private:
    mono_float offset_;
};

class Root : public Operator {
  public:
    Root(mono_float offset) : Operator(1, 1, true), offset_(offset) {}
    virtual Processor *clone() const override { return new Root(*this); }

    void process(int num_samples) override {
        poly_float value = utils::max(input()->at(0), 0.0f);
        output()->buffer[0] = utils::sqrt(value) + offset_;
    }

  private:
    mono_float offset_;
};

class ExponentialScale : public Operator {
  public:
    ExponentialScale(mono_float min, mono_float max, mono_float scale = 1, mono_float offset = 0.0f)
        : Operator(1, 1, true)
        , min_(min)
        , max_(max)
        , scale_(scale)
        , offset_(offset) {}

    virtual Processor *clone() const override { return new ExponentialScale(*this); }

    void process(int num_samples) override {
        output()->buffer[0] = futils::pow(scale_, utils::clamp(input()->at(0), min_, max_));
    }

  private:
    mono_float min_;
    mono_float max_;
    mono_float scale_;
    mono_float offset_;
};

class VariableAdd : public Operator {
  public:
    VariableAdd(int num_inputs = 0) : Operator(num_inputs, 1, true) {}

    virtual Processor *clone() const override { return new VariableAdd(*this); }

    void process(int num_samples) override {
        int num_inputs = static_cast<int>(inputs_->size());
        poly_float value = 0.0;

        for (int in = 0; in < num_inputs; ++in)
            value += input(in)->at(0);

        output()->buffer[0] = value;
    }

  private:
};

class FrequencyToPhase : public Operator {
  public:
    FrequencyToPhase() : Operator(1, 1, true) {}

    virtual Processor *clone() const override { return new FrequencyToPhase(*this); }

    void process(int num_samples) override {
        output()->buffer[0] = input()->at(0) * (1.0f / getSampleRate());
    }

  private:
};

class FrequencyToSamples : public Operator {
  public:
    FrequencyToSamples() : Operator(1, 1, true) {}

    virtual Processor *clone() const override { return new FrequencyToSamples(*this); }

    void process(int num_samples) override {
        output()->buffer[0] = poly_float(getSampleRate()) / input()->at(0);
    }

  private:
};

class TimeToSamples : public Operator {
  public:
    TimeToSamples() : Operator(1, 1, true) {}

    virtual Processor *clone() const override { return new TimeToSamples(*this); }

    void process(int num_samples) override { output()->buffer[0] = input()->at(0) * getSampleRate(); }

  private:
};

class MagnitudeScale : public Operator {
  public:
    MagnitudeScale() : Operator(1, 1, true) {}

    virtual Processor *clone() const override { return new MagnitudeScale(*this); }

    void process(int num_samples) override { output()->buffer[0] = futils::dbToMagnitude(input()->at(0)); }

  private:
};

class MidiScale : public Operator {
  public:
    MidiScale() : Operator(1, 1, true) {}

    virtual Processor *clone() const override { return new MidiScale(*this); }

    void process(int num_samples) override {
        output()->buffer[0] = utils::midiCentsToFrequency(input()->at(0));
    }

  private:
};

class BilinearInterpolate : public Operator {
  public:
    enum { kTopLeft, kTopRight, kBottomLeft, kBottomRight, kXPosition, kYPosition, kNumInputs };

    static const int kPositionStart = kTopLeft;

    BilinearInterpolate() : Operator(kNumInputs, 1, true) {}

    virtual Processor *clone() const override { return new BilinearInterpolate(*this); }

    void process(int num_samples) override {
        poly_float top =
            utils::interpolate(input(kTopLeft)->at(0), input(kTopRight)->at(0), input(kXPosition)->at(0));
        poly_float bottom = utils::interpolate(input(kBottomLeft)->at(0),
                                               input(kBottomRight)->at(0),
                                               input(kXPosition)->at(0));
        output()->buffer[0] = utils::interpolate(top, bottom, input(kYPosition)->at(0));
    }

  private:
};
} // namespace cr
} // namespace vital
