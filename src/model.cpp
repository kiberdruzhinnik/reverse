#include "model.hpp"
#include "binary_math.hpp"

#include "eq.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace burnley73 {
namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kFloor = 1.0e-25;

double onePoleAlpha(double c, double sampleRate) {
    return 1.0 - detail::binaryExp(-c / sampleRate);
}

struct RateShape {
    double shelfFrequency;
    double shelfGain;
    double shelfS;
    double outputFrequency;
    double outputC;
    double micDriveScale;
    double micFeedbackScale;
    double micHighScale;
};

RateShape rateShape(double sampleRate) {
    if (sampleRate <= 44100.0)
        return {17000.0, -1.8, 0.376, 20000.0, 125663.70614359173, 1.0, 1.0, 1.0};
    if (sampleRate <= 48000.0)
        return {17200.0, -1.8, 0.376, 20255.0, 127265.91839692251, 1.012, 1.014, 1.018};
    if (sampleRate <= 88200.0)
        return {19200.0, -2.0, 0.55, 22500.0, 141371.6694115407, 1.098, 1.152, 1.097};
    if (sampleRate <= 96000.0)
        return {20000.0, -2.0, 0.55, 22500.0, 141371.6694115407, 1.113, 1.168, 1.106};
    return {21000.0, -2.0, 0.55, 23500.0, 147654.85471872028, 1.238, 1.308, 1.154};
}

double rational(double x) {
    return x / (((x * 0x1.7afffa1aeb983p-2) * x) + 0x1.291a64c75f379p+0)
        + x * 0x1.b6ae8f5980a32p-4;
}

std::int32_t advanceRandom(std::int32_t value) {
    const std::uint32_t product = static_cast<std::uint32_t>(value) * 0x4du;
    std::int32_t wrapped;
    std::memcpy(&wrapped, &product, sizeof(wrapped));
    return wrapped % 0x1000003;
}

double shapeLine(double u) {
    constexpr double limit = 0x1.75de5deec8a01p+1;
    constexpr double saturation = 0x1.fa7cd209d62e2p-1;
    if (u > limit)
        return saturation;
    if (u < -limit)
        return -saturation;
    return rational(u);
}

double shapeMic(double z, double magnitude) {
    const double m = magnitude < 0.0 ? 0.0 : magnitude;
    const double c3 = z >= 0.0 ? 0x1.2e978d4fdf3b6p-3 : 0x1.9374bc6a7ef9ep-3;
    const double c5 = z >= 0.0 ? 0x1.2fc7a398201cep-2 : 0x1.4dd2f1a9fbe77p-2;
    const double z3 = (z * z) * z;
    const double w = ((z3 * c3) * m) + z + ((((z3 * z) * z) * c5) * m);
    constexpr double limit = 0x1.75de5deec8a01p+1;
    constexpr double saturation = 0x1.fa7cd209d62e2p-1;
    if (w > limit)
        return saturation;
    if (w < -limit)
        return -saturation;
    return rational(w);
}

void updateSmootherAlpha(Smoother& s, double c, double fs) {
    // Sample-rate reset updates the coefficient while retaining the one-pole
    // memory. The constructors start these memories at zero.
    s.alpha = onePoleAlpha(c, fs);
}

void configureBypassShelf(Biquad& section, double sampleRate) {
    // FUN_18000bcc0 uses its own coefficient path. Its denominator contains
    // sin(w)/0.752 before the explicit factor of two, rather than the shared
    // EQ builder's sin(w)/(S*2). Keep this separate from the ordinary shelf
    // builder so selector 8 retains the binary's wider alpha.
    const double A = detail::binaryPower(10.0, -0.0275);
    const double w = 106814.15022205297 / sampleRate;
    const auto trig = detail::binarySinCos(w);
    const double c = trig.cos;
    const double s = trig.sin;
    const double sqrtA = std::sqrt(A);
    const double Aminus1 = A - 1.0;
    const double alpha = sqrtA * 2.0 * (s / 0.752);
    const double Aplus1 = A + 1.0;
    const double numeratorBase = Aplus1 + Aminus1 * c;
    const double denominatorBase = Aplus1 - Aminus1 * c;
    const double D = denominatorBase + alpha;
    section.b0 = ((numeratorBase + alpha) * A) / D;
    section.a1 = ((Aminus1 - Aplus1 * c) * 2.0) / D;
    section.a2 = (denominatorBase - alpha) / D;
    section.b1 = (((Aplus1 * c + Aminus1) * A) * -2.0) / D;
    section.b2 = ((numeratorBase - alpha) * A) / D;
}

} // namespace

void ModelBank::reset(double sampleRate, int selectorIndex, std::int32_t randomSeed) {
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 44100.0;
    const RateShape rate = rateShape(sampleRate_);
    // The binary resets embedded biquad histories and selected feedback
    // states, but leaves its one-pole memories intact across rate changes.
    line_.filter.reset();
    line_.feedbackMemory = 0.0;
    line_.filter.historyFloor = kFloor;
    configureHighShelfSection(line_.filter, rate.shelfGain, rate.shelfFrequency, sampleRate_, rate.shelfS);
    updateSmootherAlpha(line_.inputLow, 9380.795663619123, sampleRate_);
    updateSmootherAlpha(line_.filterSmooth, rate.outputC, sampleRate_);
    updateSmootherAlpha(line_.envelope, 94.24777960769379, sampleRate_);
    updateSmootherAlpha(line_.feedback, 2199.114857512855, sampleRate_);
    updateSmootherAlpha(line_.residualFeedback, 502.6548245743669, sampleRate_);
    updateSmootherAlpha(line_.slow, 502.6548245743669, sampleRate_);
    updateSmootherAlpha(line_.inputScaled, 9424.77796076938, sampleRate_);
    updateSmootherAlpha(line_.finalScale, 37.69911184307752, sampleRate_);
    updateSmootherAlpha(line_.low, 31.41592653589793, sampleRate_);

    mic_.filterA.reset();
    mic_.filterB.reset();
    mic_.filterA.historyFloor = kFloor;
    mic_.filterB.historyFloor = kFloor;
    mic_.shaped = 0.0;
    mic_.feedbackValue = 0.0;
    mic_.feedbackLp = 0.0;
    mic_.feedbackHp = 0.0;
    mic_.modulation1 = mic_.modulation2 = mic_.modulation3 = mic_.modulation4 = 0.0;
    // The binary takes one CRT rand() result, then advances its independent
    // Mic recurrence rand()%228000+1 times from the initial value 1. The CRT
    // draw is explicit here because its thread-local startup history is host
    // dependent; reproduce the observed inner loop exactly once that input is
    // supplied.
    mic_.resetRandResult = std::clamp(randomSeed, 0, 0x7fff);
    std::uint32_t seedIterations = static_cast<std::uint32_t>(mic_.resetRandResult) % 0x37aa0u + 1u;
    std::int32_t seededState = 1;
    while (seedIterations-- > 0)
        seededState = advanceRandom(seededState);
    mic_.randomState = seededState;
    configureMic(sampleRate_);

    bypass_.filter.reset();
    bypass_.filter.historyFloor = kFloor;
    configureBypassShelf(bypass_.filter, sampleRate_);
    updateSmootherAlpha(bypass_.inputLow, 9380.795663619123, sampleRate_);

    select(sampleRate_, selectorIndex);
}

void ModelBank::select(double sampleRate, int selectorIndex) {
    if (sampleRate > 0.0 && std::isfinite(sampleRate) && sampleRate != sampleRate_)
        // FUN_180015da0 consumes a fresh CRT rand() draw on every reset.
        // Reuse the thread-local MSVC stream rather than replaying the prior
        // draw when selection also carries a sample-rate change.
        reset(sampleRate, selectorIndex, detail::binaryMsvcRand15());
    selectorIndex_ = std::clamp(selectorIndex, 0, 20);
    if (selectorIndex_ < 8)
        configureLine(selectorIndex_);
    else if (selectorIndex_ >= 9)
        configureMic(sampleRate_);
}

void ModelBank::configureMic(double sampleRate) {
    // FUN_1800163f0 reapplies this configuration when a Mic mode is selected.
    // Keep this separate from reset so selector edits retain sample histories.
    const RateShape rate = rateShape(sampleRate);
    configureHighShelfSection(mic_.filterA, rate.shelfGain, rate.shelfFrequency,
                              sampleRate, rate.shelfS);

    const double w = 1413.7166941154069 / sampleRate;
    const double p = detail::binaryPower(10.0, -0.075);
    const auto trig = detail::binarySinCos(w);
    const double c = trig.cos;
    const double s = trig.sin;
    const double u = (s / 1.7) / p;
    const double v = (s / 1.7) * p;
    const double denominator = 1.0 + u;
    mic_.filterB.a1 = -2.0 * c / denominator;
    mic_.filterB.a2 = (1.0 - u) / denominator;
    mic_.filterB.b0 = (1.0 + v) / denominator;
    mic_.filterB.b1 = -2.0 * c / denominator;
    mic_.filterB.b2 = (1.0 - v) / denominator;

    updateSmootherAlpha(mic_.inputLow, 9380.795663619123, sampleRate);
    updateSmootherAlpha(mic_.filterBInput, 565.4866776461628, sampleRate);
    updateSmootherAlpha(mic_.dc, 6.283185307179586, sampleRate);
    updateSmootherAlpha(mic_.nonlinearLow, 31.41592653589793, sampleRate);
    updateSmootherAlpha(mic_.envelope1, 125.66370614359172, sampleRate);
    updateSmootherAlpha(mic_.outputLow, 515.221195188726, sampleRate);
    updateSmootherAlpha(mic_.envelope2, 502.6548245743669, sampleRate);
    updateSmootherAlpha(mic_.highPass1, 3198.1413213544092, sampleRate);
    updateSmootherAlpha(mic_.highPass2, 647.1680866394973, sampleRate);
    updateSmootherAlpha(mic_.driveLow, 490.0884539600077, sampleRate);
    updateSmootherAlpha(mic_.outputFinal, rate.outputC, sampleRate);
    mic_.fixedMix.alpha = 0.97625;
    mic_.modulationScale = detail::binaryExp(-13.815510557964274);
    mic_.driveScale = 0.948418463301 * rate.micDriveScale;
    mic_.feedbackScale = 0.95 * rate.micFeedbackScale;
    mic_.highScale = 1.8 * rate.micHighScale;
}

void ModelBank::configureLine(int mode) {
    static constexpr double inputScale[] = {1.5, 0.9, 0.6, 0.3, 0.2, 0.15, 0.1, 0.07};
    static constexpr double outputScale[] = {0x1.5555555555555p-1, 0x1.1c71c71c71c72p+0,
        0x1.aaaaaaaaaaaabp+0, 0x1.aaaaaaaaaaaabp+1, 0x1.4p+2,
        0x1.aaaaaaaaaaaabp+2, 0x1.4p+3, 0x1.c924924924924p+3};
    static constexpr double pole[] = {5000.0, 4500.0, 3500.0, 2500.0, 2500.0, 1400.0, 1300.0, 1250.0};
    static constexpr double feedback[] = {0.1174, 0.1174, 0.1033, 0.1174, 0.1174, 0.12, 0.12, 0.12};
    static constexpr double exponents[] = {0.9210340371976, 0.6907755278982, 0.4605170185988,
        0.2302585092994, 0.0, -0.028782313662425, -0.05756462732485, -0.070228845336317};
    mode = std::clamp(mode, 0, 7);
    line_.inputScale = inputScale[mode];
    line_.outputScale = outputScale[mode];
    line_.feedbackWeight = feedback[mode];
    line_.residualWeight = 4.362;
    line_.finalTarget = detail::binaryExp(exponents[mode]);
    line_.inputScaled.alpha = 1.0 - detail::binaryExp(-2.0 * kPi * pole[mode] / sampleRate_);
}

double ModelBank::inputFactor() const {
    if (selectorIndex_ >= 9) {
        const double modeDelta = (static_cast<double>(selectorIndex_) - 9.0) * 2.6;
        return detail::binaryExp(modeDelta * 0.1151292546497);
    }
    return 1.0;
}

double ModelBank::outputFactor() const {
    if (selectorIndex_ >= 9) {
        const double modeDelta = (static_cast<double>(selectorIndex_) - 9.0) * 2.6;
        return detail::binaryExp(modeDelta * -0.7 * 0.1151292546497);
    }
    if (selectorIndex_ < 8)
        return 1.0 / line_.finalTarget;
    return 1.0;
}

double ModelBank::processLine(double input) {
    const double slowInput = line_.inputLow.update(input);
    const double highPassed = input + 0.083 * (input - slowInput);
    const double filtered = line_.filter.processModel(highPassed);
    const double filterOut = line_.filterSmooth.update(filtered);
    const double envelope = line_.feedback.update(line_.feedbackMemory);
    const double scaledInput = filterOut * line_.inputScale;
    const double v = line_.inputScaled.update(scaledInput);
    const double q = line_.envelope.update(envelope * envelope) * 0.5;
    const double u = 0.4 * (q + v - 0.1 * envelope);
    const double shaped = shapeLine(u);
    line_.feedbackMemory = 2.5 * shaped + 0.1 * envelope;
    const double vSlow = line_.slow.update(v);
    const double residualFeedback = line_.residualFeedback.update(v - line_.feedbackMemory);
    const double z = scaledInput - vSlow * line_.feedbackWeight
        + residualFeedback * line_.residualWeight - q;
    const double residual = z - line_.low.update(z);
    const double final = line_.finalScale.update(line_.finalTarget);
    return final * line_.outputScale * residual;
}

double ModelBank::processMic(double input) {
    const double low = mic_.inputLow.update(input);
    const double highPassed = input + 0.083 * (input - low);
    const double yA = mic_.filterA.processModel(highPassed);

    mic_.randomState = advanceRandom(mic_.randomState);
    const double n = static_cast<double>(mic_.randomState) * 5.93e-8;
    mic_.modulation1 = 0.99765 * mic_.modulation1 + 0.099046 * n;
    mic_.modulation2 = 0.963 * mic_.modulation2 + 0.2965164 * n;
    mic_.modulation3 = 0.57 * mic_.modulation3 + 1.0526913 * n;
    mic_.modulation4 = 0.00077 * mic_.modulation4 + 1.5 * n;
    const double modulation = mic_.modulation1 + mic_.modulation2 + mic_.modulation3
        + mic_.modulation4 + 0.1848 * n;
    const double xBTarget = (yA + modulation * mic_.modulationScale) * 0.3 * mic_.driveScale;
    const double xB = mic_.filterBInput.update(xBTarget);
    const double yB = mic_.filterB.processModel(xB);

    const double t = mic_.dc.update(0x1.a65539785ded2p-10) + 1.34839892781 + yB;
    const double z = 0.55 * t * t - 1.0;
    const double zLow = mic_.nonlinearLow.update(z);
    const double qOld = mic_.shaped;
    // The reference sample loop reads the reset-time coefficient fields
    // (+0x308/+0x2c8) instead of recalculating exp() for every sample.
    const double e2 = ((qOld * qOld) * 2.0 - mic_.envelope2Value)
        * mic_.envelope2.alpha + kFloor + mic_.envelope2Value;
    mic_.envelope2Value = e2;
    const double e1 = (qOld * qOld - mic_.envelope1Value)
        * mic_.envelope1.alpha + kFloor + mic_.envelope1Value;
    mic_.envelope1Value = e1;

    const double fb = 2.0 * mic_.feedbackValue;
    // The binary captures the previous shaper output before updating it and
    // uses that delayed value in the feedback drive subtraction.
    const double drive = (rational(fb) * 0.542 - qOld) * 0.085;
    const double previousLp = mic_.feedbackLp;
    const double previousHp = mic_.feedbackHp;
    const double residual = ((((z - zLow) * 0.85 + xBTarget - yB) * (1.0 / 0.3)) * 1.0
        - previousLp * mic_.feedbackScale - previousHp * mic_.highScale - 0.06 * qOld)
        + e1 * -0.0143999 * 20.0;
    mic_.feedbackValue = residual;

    const double magnitude = (0.651 - e2) * 0.586 + 1.0;
    const double shaped = shapeMic(mic_.feedbackValue, magnitude)
        - (drive - mic_.driveLow.update(drive));
    mic_.shaped = shaped;

    const double lp = mic_.outputLow.update(shaped);
    mic_.feedbackLp = lp;
    const double hp1 = mic_.highPass1.update(shaped);
    const double hp2 = mic_.highPass2.update(hp1);
    const double hp = hp1 - hp2;
    mic_.feedbackHp = hp;
    const double mixTarget = (0.9015 * lp + 1.0 * shaped) + 1.99632 * hp;
    const double mix = mic_.fixedMix.update(mixTarget);
    const double preOut = mix * 1.022;
    return mic_.outputFinal.update(preOut);
}

double ModelBank::processBypass(double input) {
    const double low = bypass_.inputLow.update(input);
    const double highPassed = input + 0.03 * (input - low);
    return bypass_.filter.processModel(highPassed);
}

double ModelBank::process(double input) {
    if (selectorIndex_ < 8)
        return processLine(input);
    if (selectorIndex_ == 8)
        return processBypass(input);
    return processMic(input);
}

} // namespace burnley73
