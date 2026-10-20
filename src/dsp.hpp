#pragma once

#include <array>
#include <cstddef>

namespace burnley73 {

enum class Parameter : std::size_t {
    HighPass = 0,
    LowShelfFrequency,
    LowShelfGain,
    MidFrequency,
    MidGain,
    HighShelfGain,
    Model,
    Output,
    HighPassPower,
    LowShelfPower,
    MidPower,
    HighShelfPower,
    EqIn,
    PhaseInvert,
    ShowLabels,
    Count
};

constexpr std::size_t kParameterCount = static_cast<std::size_t>(Parameter::Count);

struct ParameterInfo {
    const char* title;
    double minimum;
    double maximum;
    double step;
    double plainDefault;
    int choices;
    bool boolean;
};

const std::array<ParameterInfo, kParameterCount>& parameterInfo();
double normalizedToPlain(Parameter parameter, double normalized);
double plainToNormalized(Parameter parameter, double plain);

struct Parameters {
    std::array<double, kParameterCount> plain{};

    Parameters();
    double get(Parameter parameter) const;
    void setPlain(Parameter parameter, double value);
    void setNormalized(Parameter parameter, double value);
};

struct Smoother {
    double value = 0.0;
    double alpha = 0.0;

    double update(double target);
    void reset(double initial, double coefficient);
};

struct Biquad {
    double a1 = 0.0;
    double a2 = 0.0;
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
    double historyFloor = 1.0e-25;

    double process(double input);
    // The shipped mid peaking section adds its stored feedback terms; the
    // shelf and high-pass sections subtract them.
    double processMid(double input);
    // The Line and Mic model biquads add the delayed b1*x1 term before the
    // current b0*x term. EQ sections use process()'s current-first order.
    double processModel(double input);
    void reset();
};

class Processor {
public:
    void reset(double sampleRate);
    void process(double* left, double* right, int sampleCount);

    Parameters parameters;
    double sampleRate() const { return sampleRate_; }

private:
    double sampleRate_ = 44100.0;
};

} // namespace burnley73
