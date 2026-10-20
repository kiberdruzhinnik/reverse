#pragma once

#include "dsp.hpp"

namespace burnley73 {

struct EqState {
    Biquad highShelfA;
    Biquad highShelfB;
    Biquad lowShelfA;
    Biquad lowShelfB;
    Biquad highPass;
    Biquad mid;
    double highShelfMix = 0.0;
    double lowShelfMix = 0.0;
    double highPassMix = 0.0;
    double highPassAlpha = 0.0;
    double highPassSmoother = 0.0;

    void reset(double sampleRate, const Parameters& parameters);
    void configure(double sampleRate, const Parameters& parameters);
    double process(double input);
};

void configureHighShelfSection(Biquad& section, double gainDb, double frequencyHz,
                               double sampleRate, double shape);

} // namespace burnley73
