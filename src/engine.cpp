#include "engine.hpp"
#include "binary_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>

namespace burnley73 {
namespace {
constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kFloor = 1.0e-25;
double alpha(double hz, double fs) { return 1.0 - detail::binaryExp(-2.0 * kPi * hz / fs); }

void initializeReferenceRandomTable() {
    // Static initialization sets DAT_1800e59e8 to zero, and the analyzed
    // constructor path never makes it nonzero. Thus each parent instance calls
    // FUN_180040310, which reseeds this thread from _time64.
    detail::binaryMsvcSrand(static_cast<std::uint32_t>(std::time(nullptr)));
    std::array<std::int32_t, 0xc08> table{};
    for (auto& value : table)
        value = detail::binaryMsvcRand15() % (999 - 11 + 1) + 11;

    // FUN_1800406f0 compares entries 0xc00..0xc03 against 1850, 752,
    // 850, and 1263. Out-of-window values consume a fallback CRT draw.
    (void)detail::binaryMsvcRand15();
    if (table[0xc01] < 752 || table[0xc01] > 761)
        (void)detail::binaryMsvcRand15();
    if (table[0xc02] < 850 || table[0xc02] > 859)
        (void)detail::binaryMsvcRand15();
    (void)detail::binaryMsvcRand15();
}
}

AudioEngine::AudioEngine() {
    initializeReferenceRandomTable();
    // FUN_1800415e0 consumes four CRT values during controller/UI setup before
    // the parent constructs its two Mic children.
    for (int i = 0; i < 4; ++i)
        (void)detail::binaryMsvcRand15();

    // Each Mic child constructor performs its own sample-rate reset and draws
    // the random-loop count once, before setupProcessing resets it again.
    for (auto& model : model_)
        model.reset(sampleRate_, static_cast<int>(parameters.get(Parameter::Model)),
                    detail::binaryMsvcRand15());
}

void AudioEngine::reset(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 44100.0;
    for (auto& eq : eq_) eq.reset(sampleRate_, parameters);
    // The reference performs Mic resets on this caller thread and consumes
    // one MSVC CRT rand() value per child, even when Mic is not selected.
    for (auto& model : model_)
        model.reset(sampleRate_, static_cast<int>(parameters.get(Parameter::Model)),
                    detail::binaryMsvcRand15());
    // The binary's sample-rate reset refreshes these coefficients but leaves
    // their running memories intact. They begin at zero from object setup.
    inputFactor_.alpha = alpha(10.0, sampleRate_);
    outputGain_.alpha = alpha(10.0, sampleRate_);
    outputFactor_.alpha = alpha(10.0, sampleRate_);
    polarity_.alpha = alpha(10.0, sampleRate_);
    if (!firstLowpassInitialized_) {
        // The binary constructor initializes this first-stream smoother to
        // 1 - exp((-2*pi*fs)/fs) + 0.001867; preserve the binary's multiply
        // then divide sequence even though the sample-rate terms cancel.
        const double constructorExponent = sampleRate_ * (-2.0 * kPi) / sampleRate_;
        firstLowpass_.reset(0.0,
            1.0 - detail::binaryExp(constructorExponent) + 0.001867);
        firstLowpassInitialized_ = true;
    }
    inputFactorTarget_ = model_[0].inputFactor();
    outputFactorTarget_ = model_[0].outputFactor();
    outputGainTarget_ = detail::binaryExp(parameters.get(Parameter::Output) * 0.1151292546497);
    polarityTarget_ = parameters.get(Parameter::PhaseInvert) >= 0.5 ? -1.0 : 1.0;
}

void AudioEngine::configure(Parameter parameter) {
    if (parameter == Parameter::Model) {
        const int mode = static_cast<int>(parameters.get(Parameter::Model));
        for (auto& model : model_) model.select(sampleRate_, mode);
        inputFactorTarget_ = model_[0].inputFactor();
        outputFactorTarget_ = model_[0].outputFactor();
    }
    if (parameter == Parameter::Output)
        outputGainTarget_ = detail::binaryExp(parameters.get(Parameter::Output) * 0.1151292546497);
    if (parameter == Parameter::PhaseInvert)
        polarityTarget_ = parameters.get(Parameter::PhaseInvert) >= 0.5 ? -1.0 : 1.0;
    const bool eqParameter = parameter <= Parameter::HighShelfGain ||
        (parameter >= Parameter::HighPassPower && parameter <= Parameter::HighShelfPower) ||
        parameter == Parameter::EqIn;
    if (eqParameter) {
        for (auto& eq : eq_) eq.configure(sampleRate_, parameters);
    }
}

void AudioEngine::setPlain(Parameter parameter, double value) {
    parameters.setPlain(parameter, value);
    configure(parameter);
}

void AudioEngine::setNormalized(Parameter parameter, double value) {
    setPlain(parameter, normalizedToPlain(parameter, value));
}

void AudioEngine::process(double* const* inputs, double* const* outputs, int channels,
                          int sampleCount, bool blockBypass) {
    if (!inputs || !outputs || channels <= 0 || sampleCount <= 0) return;
    channels = std::min(channels, 2);
    if (blockBypass) {
        for (int ch = 0; ch < channels; ++ch)
            std::copy_n(inputs[ch], sampleCount, outputs[ch]);
        return;
    }
    for (int i = 0; i < sampleCount; ++i) {
        const double gIn = inputFactor_.update(inputFactorTarget_);
        const double gOut = outputGain_.update(outputGainTarget_);
        const double gPost = outputFactor_.update(outputFactorTarget_);
        const double pol = polarity_.update(polarityTarget_);
        const double offset = 0.0; // +0x5d0 is zero on the ordinary, non-protection path.
        const double fixedGain = 1.0; // +0x5d8 is initialized to unity.
        const double leftIn = inputs[0][i] + offset;
        const double rightIn = channels > 1 ? inputs[1][i] : leftIn;
        double left = model_[0].process(leftIn * gIn);
        double right = channels > 1 ? model_[1].process(rightIn * gIn) : left;
        // The parent callback applies this extra one-pole to its first stream only.
        left = firstLowpass_.update(left);
        left = eq_[0].process(left);
        right = channels > 1 ? eq_[1].process(right) : left;
        left = gOut * left * gPost * fixedGain * pol + offset;
        right = gOut * right * gPost * fixedGain * pol + offset;
        outputs[0][i] = left;
        if (channels > 1) outputs[1][i] = right;
    }
}

} // namespace burnley73
