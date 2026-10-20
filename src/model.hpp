#pragma once

#include "dsp.hpp"

#include <cstdint>

namespace burnley73 {

class ModelBank {
public:
    void reset(double sampleRate, int selectorIndex, std::int32_t randomSeed = 1);
    void select(double sampleRate, int selectorIndex);
    double process(double input);
    double inputFactor() const;
    double outputFactor() const;

private:
    struct LineState {
        Biquad filter;
        Smoother inputLow, filterSmooth, envelope, feedback, residualFeedback;
        Smoother slow, inputScaled, finalScale, low;
        double feedbackMemory = 0.0;
        double inputScale = 1.0, outputScale = 1.0, feedbackWeight = 0.1174;
        double residualWeight = 4.362, finalTarget = 1.0;
    } line_;

    struct MicState {
        Biquad filterA, filterB;
        Smoother inputLow, filterBInput, dc, nonlinearLow, envelope1, outputLow;
        Smoother envelope2, highPass1, highPass2, driveLow, outputFinal;
        Smoother fixedMix;
        double modulation1 = 0.0, modulation2 = 0.0, modulation3 = 0.0, modulation4 = 0.0;
        double feedbackValue = 0.0, feedbackLp = 0.0, feedbackHp = 0.0;
        double envelope1Value = 0.0, envelope2Value = 0.0, shaped = 0.0;
        double modulationScale = 1.0e-6;
        double driveScale = 0.948418463301, feedbackScale = 0.95, highScale = 1.8;
        std::int32_t resetRandResult = 1;
        std::int32_t randomState = 1;
    } mic_;

    struct BypassState {
        Biquad filter;
        Smoother inputLow;
    } bypass_;

    double sampleRate_ = 44100.0;
    int selectorIndex_ = 4;

    void configureLine(int mode);
    void configureMic(double sampleRate);
    double processLine(double input);
    double processMic(double input);
    double processBypass(double input);
};

} // namespace burnley73
