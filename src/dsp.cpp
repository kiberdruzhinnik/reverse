#include "dsp.hpp"
#include "binary_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace burnley73 {
namespace {

constexpr double kOutputExponent = 0x1.6437c5692b3ccp-1;
constexpr double kHistoryFloor = 1.0e-25;

constexpr std::array<ParameterInfo, kParameterCount> kParameterInfo{{
    {"High Pass", 0.0, 4.0, 1.0, 0.0, 5, false},
    {"Low Shelv Freq", 0.0, 3.0, 1.0, 2.0, 4, false},
    {"Low Shelv Gain", -16.0, 16.0, 0.1, 0.0, 0, false},
    {"Mid Freq", 0.0, 5.0, 1.0, 2.0, 6, false},
    {"Mid Gain", -16.0, 16.0, 0.1, 0.0, 0, false},
    {"High Shelv", -16.0, 16.0, 0.1, 0.0, 0, false},
    {"Gain", 0.0, 20.0, 1.0, 4.0, 21, false},
    // The DSP parameter queue's normalized range is -24..+16 dB. This
    // maps the saved/default host value 0.479879 to 0 dB with exponent
    // 0.695738; the controller's formatted metadata is separately inconsistent.
    {"Output", -24.0, 16.0, 0.1, 0.0, 0, false},
    {"HighPass Power", 0.0, 1.0, 1.0, 1.0, 0, true},
    {"Low Shelv Power", 0.0, 1.0, 1.0, 1.0, 0, true},
    {"Mid Power", 0.0, 1.0, 1.0, 1.0, 0, true},
    {"High Shelv Power", 0.0, 1.0, 1.0, 1.0, 0, true},
    {"EQ In", 0.0, 1.0, 1.0, 1.0, 0, true},
    {"Phase invert", 0.0, 1.0, 1.0, 0.0, 0, true},
    {"Show Labels", 0.0, 1.0, 1.0, 0.0, 0, true},
}};

std::size_t indexOf(Parameter parameter) {
    return static_cast<std::size_t>(parameter);
}

double clampFinite(double value, double minimum, double maximum) {
    if (!std::isfinite(value))
        return minimum;
    return std::max(minimum, std::min(maximum, value));
}

} // namespace

const std::array<ParameterInfo, kParameterCount>& parameterInfo() {
    return kParameterInfo;
}

double normalizedToPlain(Parameter parameter, double normalized) {
    const auto& info = kParameterInfo[indexOf(parameter)];
    const double n = clampFinite(normalized, 0.0, 1.0);
    if (info.boolean)
        return n >= 0.5 ? 1.0 : 0.0;
    if (info.choices > 0)
        return std::floor(n * (info.choices - 1) + 0.5);
    if (parameter == Parameter::Output)
        return info.minimum + detail::binaryPower(n, kOutputExponent) * (info.maximum - info.minimum);
    // The binary assigns type tag 4 to its three EQ gains; that path is linear
    // and does not snap to the metadata's 0.1 dB step.
    return info.minimum + n * (info.maximum - info.minimum);
}

double plainToNormalized(Parameter parameter, double plain) {
    const auto& info = kParameterInfo[indexOf(parameter)];
    const double value = clampFinite(plain, info.minimum, info.maximum);
    if (info.boolean)
        return value >= 0.5 ? 1.0 : 0.0;
    if (info.choices > 0)
        return value / (info.choices - 1);
    const double proportion = (value - info.minimum) / (info.maximum - info.minimum);
    if (parameter == Parameter::Output)
        return detail::binaryPower(proportion, 1.0 / kOutputExponent);
    return proportion;
}

Parameters::Parameters() {
    for (std::size_t i = 0; i < kParameterCount; ++i)
        plain[i] = kParameterInfo[i].plainDefault;
}

double Parameters::get(Parameter parameter) const {
    return plain[indexOf(parameter)];
}

void Parameters::setPlain(Parameter parameter, double value) {
    const auto& info = kParameterInfo[indexOf(parameter)];
    double bounded = clampFinite(value, info.minimum, info.maximum);
    if (info.boolean)
        bounded = bounded >= 0.5 ? 1.0 : 0.0;
    else if (info.choices > 0)
        bounded = std::floor(bounded + 0.5);
    plain[indexOf(parameter)] = bounded;
}

void Parameters::setNormalized(Parameter parameter, double value) {
    setPlain(parameter, normalizedToPlain(parameter, value));
}

double Smoother::update(double target) {
    value = (target - value) * alpha + kHistoryFloor + value;
    return value;
}

void Smoother::reset(double initial, double coefficient) {
    value = initial;
    alpha = coefficient;
}

double Biquad::process(double input) {
    const double oldX1 = x1;
    const double oldY1 = y1;
    const double output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x1 = input;
    x2 = oldX1 + historyFloor;
    y1 = output;
    y2 = oldY1;
    return output;
}

double Biquad::processMid(double input) {
    const double oldX1 = x1;
    const double oldY1 = y1;
    const double output = b1 * x1 + b0 * input + b2 * x2 + a1 * y1 + a2 * y2;
    x1 = input;
    x2 = oldX1 + historyFloor;
    y1 = output;
    y2 = oldY1;
    return output;
}

double Biquad::processModel(double input) {
    const double oldX1 = x1;
    const double oldY1 = y1;
    double output = b1 * x1;
    output += b0 * input;
    output += b2 * x2;
    output -= a1 * y1;
    output -= a2 * y2;
    x1 = input;
    x2 = oldX1 + historyFloor;
    y1 = output;
    y2 = oldY1;
    return output;
}

void Biquad::reset() {
    x1 = x2 = y1 = y2 = 0.0;
}

void Processor::reset(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 44100.0;
}

void Processor::process(double* left, double* right, int sampleCount) {
    // Replaced by the documented model and EQ cascade in the following tasks.
    (void)left;
    (void)right;
    (void)sampleCount;
}

} // namespace burnley73
