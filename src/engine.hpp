#pragma once

#include "dsp.hpp"
#include "eq.hpp"
#include "model.hpp"

namespace burnley73 {

class AudioEngine {
public:
    AudioEngine();
    Parameters parameters;
    void reset(double sampleRate);
    void setPlain(Parameter parameter, double value);
    void setNormalized(Parameter parameter, double value);
    void process(double* const* inputs, double* const* outputs, int channels, int sampleCount,
                 bool blockBypass = false);

private:
    double sampleRate_ = 44100.0;
    EqState eq_[2];
    ModelBank model_[2];
    Smoother inputFactor_, outputGain_, outputFactor_, polarity_, firstLowpass_;
    bool firstLowpassInitialized_ = false;
    double inputFactorTarget_ = 1.0;
    double outputFactorTarget_ = 1.0;
    double outputGainTarget_ = 1.0;
    double polarityTarget_ = 1.0;
    void configure(Parameter parameter);
};

} // namespace burnley73
