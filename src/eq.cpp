#include "eq.hpp"
#include "binary_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace burnley73 {
namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kFloor = 1.0e-25;
constexpr double kFallback = 0.1;

#include "eq_tables.inc"

template <std::size_t N, std::size_t M>
double interpolate(const std::array<double, N>& axis, const std::array<double, M>& values, double x) {
    const std::size_t count = std::min(N, M);
    for (std::size_t i = 0; i + 1 < count; ++i) {
        if (axis[i] <= x && x <= axis[i + 1]) {
            // Preserve the binary's weighted expression and evaluation order;
            // the algebraically equivalent difference form rounds differently.
            return ((axis[i] * values[i + 1] -
                     (values[i + 1] * x + axis[i + 1] * values[i])) +
                    values[i] * x) /
                   (axis[i] - axis[i + 1]);
        }
    }
    return kFallback;
}

double midGainTransform(double gain) {
    return ((gain * 0.127933175637032 * gain + gain * 0.837144332462058 +
             gain * 0.00242466940631821 * gain * gain * gain) -
            gain * 7.02776897238319e-05 * gain * gain * gain * gain) -
           gain * 0.0273820476005487 * gain * gain;
}

void identity(Biquad& section) {
    section.a1 = section.a2 = section.b1 = section.b2 = 0.0;
    section.b0 = 1.0;
}

void buildHighShelf(Biquad& section, double gainDb, double frequencyHz, double sampleRate, double shape) {
    const double A = detail::binaryPower(10.0, gainDb / 40.0);
    const double w = (frequencyHz * 6.283185307179586) / sampleRate;
    const auto trig = detail::binarySinCos(w);
    const double c = trig.cos;
    const double s = trig.sin;
    const double sqrtA = std::sqrt(A);
    const double Aminus1 = A - 1.0;
    const double alpha = sqrtA * 2.0 * (s / (shape * 2.0));
    const double Aplus1 = A + 1.0;
    const double denominatorBase = Aplus1 - Aminus1 * c;
    const double numeratorBase = Aplus1 + Aminus1 * c;
    const double D = denominatorBase + alpha;
    section.a2 = (denominatorBase - alpha) / D;
    section.b2 = ((numeratorBase - alpha) * A) / D;
    section.b0 = ((numeratorBase + alpha) * A) / D;
    section.a1 = ((Aminus1 - Aplus1 * c) * 2.0) / D;
    section.b1 = (((Aplus1 * c + Aminus1) * A) * -2.0) / D;
}

void buildLowShelf(Biquad& section, double gainDb, double frequencyHz, double sampleRate, double shape) {
    // Keep the zero-Hz lookup fallback: the binary runs this builder at the
    // reachable positive-gain endpoint instead of replacing the section with
    // identity coefficients. Its pole/zero cancellation is state-sensitive.
    const double A = detail::binaryPower(10.0, gainDb / 40.0);
    const double w = (frequencyHz * 6.283185307179586) / sampleRate;
    const auto trig = detail::binarySinCos(w);
    const double c = trig.cos;
    const double s = trig.sin;
    const double sqrtA = std::sqrt(A);
    const double Aplus1 = A + 1.0;
    const double alpha = sqrtA * 2.0 * (s / (shape * 2.0));
    const double Aminus1 = A - 1.0;
    const double denominatorBase = Aplus1 - Aminus1 * c;
    const double numeratorBase = Aminus1 * c + Aplus1;
    const double D = numeratorBase + alpha;
    section.a2 = (numeratorBase - alpha) / D;
    section.b2 = ((denominatorBase - alpha) * A) / D;
    section.b0 = ((denominatorBase + alpha) * A) / D;
    section.a1 = ((Aplus1 * c + Aminus1) * -2.0) / D;
    section.b1 = (((Aminus1 - Aplus1 * c) * A) * 2.0) / D;
}

void buildHighPass(Biquad& section, double q, double frequencyHz, double sampleRate) {
    if (frequencyHz < 1.0) {
        identity(section);
        return;
    }
    const double w = (frequencyHz * 6.283185307179586) / sampleRate;
    const auto trig = detail::binarySinCos(w);
    const double c = trig.cos;
    const double s = trig.sin;
    const double qRoot = std::sqrt((1.0 / q - 1.0) * 2.0 + 2.0);
    const double a = s / ((1.0 / qRoot) * 2.0);
    const double D = 1.0 + a;
    section.a1 = -2.0 * c / D;
    section.a2 = (1.0 - a) / D;
    section.b0 = ((c + 1.0) * 0.5) / D;
    section.b1 = -(1.0 + c) / D;
    section.b2 = ((c + 1.0) * 0.5) / D;
}

void buildMid(Biquad& section, double gain, double frequency, double bandwidth, double invFs) {
    if (std::abs(gain) < 0.001) {
        identity(section);
        return;
    }

    constexpr double Gscale = 0.5;
    const double G = detail::binaryExp(gain * 0.1151292546497);
    const double B = detail::binaryPower(2.0, bandwidth * Gscale);
    const double f = frequency / B;
    const double H = detail::binaryPower(2.0, bandwidth);
    const double rootG = std::sqrt(G);
    const double theta = frequency * 6.283185307179586 * invFs;
    const double delta = (H * f - f) * 6.283185307179586;
    const double G2 = G * G;
    const double dg = G2 - rootG * rootG;
    const double dh = rootG * rootG - 1.0;
    const double T = theta * theta - 9.869604401089358;
    const double E = H * f * 6.283185307179586 * invFs - f * 6.283185307179586 * invFs;
    const double A = std::abs(dg);
    const double C = std::abs(dh);
    const double r0 = std::sqrt(((G2 * C * kPi * kPi * E * E) / A + T * T) /
                                ((C * kPi * kPi * E * E) / A + T * T));
    const double rootHG = std::sqrt(r0 * G);
    const double J = rootHG * rootHG;
    double r1 = std::sqrt(((G2 - J) * ((delta * delta * dh) / dg)) / (J - 1.0));
    const double U = std::abs(J - 1.0);
    const double V = std::abs(G2 - J);
    r1 = r1 * invFs;
    const double r2 = std::sqrt(((G2 * U * kPi * kPi * r1 * r1) / V + T * T) /
                                ((U * kPi * kPi * r1 * r1) / V + T * T));
    const double W = std::abs(G2 - r2 * r2);
    const double X = std::abs(J - r2 * r2);
    const double t0 = detail::binaryTan(theta * Gscale);
    const double y0 = std::sqrt(W / std::abs(G2 - 1.0));
    const double K = y0 * t0 * t0;
    const double y1 = std::sqrt(U / X);
    const double t1 = detail::binaryTan(r1 * Gscale);
    const double L = (y1 * K + 1.0) * t1;
    const double y2 = std::sqrt(X * U);
    const double M = L * X * L - (std::abs(J - r2) - y2) * K * 2.0;
    const double y3 = std::sqrt(W * std::abs(G2 - 1.0));
    const double N = (std::abs(G2 - r2) - y3) * K * 2.0;
    // The shipped builder normalizes both radicands by V = |G² - J|.
    const double P = std::sqrt((N + M) / V);
    const double Q = std::sqrt((J * N + G2 * M) / V);
    const double norm = 1.0 / (K + 1.0 + P);

    section.a1 = -((1.0 - K) * -2.0 * norm);
    section.a2 = -((K + 1.0) - P) * norm;
    section.b0 = (K + r2 + Q) * norm;
    section.b1 = (r2 - K) * -2.0 * norm;
    section.b2 = (r2 - Q + K) * norm;
}

void configureHighPass(EqState& eq, double sampleRate, const Parameters& parameters) {
    int index = static_cast<int>(parameters.get(Parameter::HighPass));
    if (parameters.get(Parameter::HighPassPower) < 0.5)
        index = 0;

    if (index != 0 && (index < 1 || index > 4)) {
        // FUN_1800134c0 sends malformed enabled enum values to a distinct
        // 1 Hz/Q=2.29 section, with no smoothed residual mixed back in.
        buildHighPass(eq.highPass, 2.29, 1.0, sampleRate);
        eq.highPassMix = 0.0;
        eq.highPassAlpha = 1.0 - detail::binaryExp(-2.0 * kPi * 20.0 / sampleRate);
        return;
    }

    static constexpr double qValues[] = {0.99, 2.29, 2.29, 2.5, 2.746};
    static constexpr double biquadFrequencies[] = {6.0, 32.0, 52.0, 139.0, 281.0};
    static constexpr double mixValues[] = {-0.5, -0.72, -0.72, -0.53, -0.57};
    static constexpr double poleFrequencies[] = {20.0, 74.0, 117.0, 340.0, 564.0};
    buildHighPass(eq.highPass, qValues[index], biquadFrequencies[index], sampleRate);
    eq.highPassMix = mixValues[index];
    eq.highPassAlpha = 1.0 - detail::binaryExp(-2.0 * kPi * poleFrequencies[index] / sampleRate);
}

void configureLowShelf(EqState& eq, double sampleRate, const Parameters& parameters) {
    const int frequencyIndex = static_cast<int>(parameters.get(Parameter::LowShelfFrequency));
    const double requestedGain = parameters.get(Parameter::LowShelfPower) >= 0.5
        ? parameters.get(Parameter::LowShelfGain) : 0.0;
    // The binary scales the shared gain before branching, including the
    // positive-gain table paths.
    const double gain = requestedGain * 0.9375;
    if (gain < 0.0) {
        const double x = gain;
        static constexpr double frequencyScales[] = {0.473, 0.729, 1.0, 0x1.8c49ba5e353f8p+0};
        static constexpr double secondShapeScales[] = {0.837, 0.837, 1.0, 0.837};
        const int idx = std::clamp(frequencyIndex, 0, 3);
        const double scale = frequencyScales[idx];
        eq.lowShelfMix = -1.0;
        buildLowShelf(eq.lowShelfA, interpolate(kTable_1800d4af0, kTable_1800d4bd0, x),
            static_cast<int>(interpolate(kTable_1800d4af0, kTable_1800d4b60, x) * scale), sampleRate,
            0.5);
        buildLowShelf(eq.lowShelfB, interpolate(kTable_1800d4af0, kTable_1800d4cb0, x),
            static_cast<int>(interpolate(kTable_1800d4af0, kTable_1800d4c40, x) * scale), sampleRate,
            interpolate(kTable_1800d4af0, kTable_1800d4d20, x) * secondShapeScales[idx]);
        return;
    }

    eq.lowShelfMix = 1.0;
    if (frequencyIndex == 1) {
        const double x = gain;
        buildLowShelf(eq.lowShelfA, interpolate(kTable_1800d4da0, kTable_1800d4ea0, x),
            static_cast<int>(interpolate(kTable_1800d4da0, kTable_1800d4e20, x)), sampleRate,
            interpolate(kTable_1800d4da0, kTable_1800d4f20, x));
        buildLowShelf(eq.lowShelfB, interpolate(kTable_1800d4da0, kTable_1800d4fa0, x),
            740.0, sampleRate, 0.759);
    } else if (frequencyIndex == 0) {
        const double x = gain;
        buildLowShelf(eq.lowShelfA, interpolate(kTable_1800d5020, kTable_1800d5120, x),
            static_cast<int>(interpolate(kTable_1800d5020, kTable_1800d50a0, x)), sampleRate,
            interpolate(kTable_1800d5020, kTable_1800d51a0, x));
        buildLowShelf(eq.lowShelfB, interpolate(kTable_1800d5020, kTable_1800d52a0, x),
            static_cast<int>(interpolate(kTable_1800d5020, kTable_1800d5220, x)), sampleRate, 0.483);
    } else {
        const int idx = std::clamp(frequencyIndex, 2, 3);
        const double frequencyScale = idx == 3 ? 1.616 : 1.0;
        const double shapeScale = idx == 3 ? 0.837 : 1.0;
        const double x = gain;
        buildLowShelf(eq.lowShelfA, interpolate(kTable_1800d47e0, kTable_1800d48e0, x),
            static_cast<int>(interpolate(kTable_1800d47e0, kTable_1800d4860, x) * frequencyScale), sampleRate,
            interpolate(kTable_1800d47e0, kTable_1800d4960, x));
        buildLowShelf(eq.lowShelfB, interpolate(kTable_1800d47e0, kTable_1800d4a60, x),
            static_cast<int>(interpolate(kTable_1800d47e0, kTable_1800d49e0, x) * frequencyScale), sampleRate,
            0.5 * shapeScale);
    }
}

void configureHighShelf(EqState& eq, double sampleRate, const Parameters& parameters) {
    const double gain = parameters.get(Parameter::HighShelfPower) >= 0.5
        ? parameters.get(Parameter::HighShelfGain) : 0.0;
    if (gain < 0.0) {
        const double x = 1.125 * gain;
        eq.highShelfMix = -1.0;
        buildHighShelf(eq.highShelfA, interpolate(kTable_1800d4410, kTable_1800d4550, x),
            static_cast<int>(interpolate(kTable_1800d4410, kTable_1800d44b0, x)), sampleRate, 0.578);
        buildHighShelf(eq.highShelfB, interpolate(kTable_1800d4410, kTable_1800d4690, x),
            static_cast<int>(interpolate(kTable_1800d4410, kTable_1800d45f0, x)), sampleRate,
            interpolate(kTable_1800d4410, kTable_1800d4730, x));
        return;
    }

    const double gain2 = gain * gain;
    const double T = (((gain2 * 0.168824404823126 + gain * 0.513095238010316 +
                        gain2 * gain2 * 0.00281622023947216) -
                       gain2 * gain2 * 8.0915178614009e-05 * gain) -
                      gain2 * 0.0329947916812927 * gain) * 1.125;
    const double coordinate = interpolate(kTable_1800d4040, kTable_1800d42d0, T);
    eq.highShelfMix = 1.0;
    buildHighShelf(eq.highShelfA, interpolate(kTable_1800d4040, kTable_1800d4180, T),
        static_cast<int>(interpolate(kTable_1800d4040, kTable_1800d40e0, T)), sampleRate, 0.521);
    buildHighShelf(eq.highShelfB, coordinate,
        static_cast<int>(interpolate(kTable_1800d42d0, kTable_1800d4230, coordinate)), sampleRate,
        interpolate(kTable_1800d42d0, kTable_1800d4370, coordinate));
}

void configureMid(EqState& eq, double sampleRate, const Parameters& parameters) {
    if (parameters.get(Parameter::MidPower) < 0.5) {
        buildMid(eq.mid, 0.0, 100.0, 2.0, 1.0 / sampleRate);
        return;
    }

    const int index = std::clamp(static_cast<int>(parameters.get(Parameter::MidFrequency)), 0, 5);
    const double gain = parameters.get(Parameter::MidGain);
    const double magnitude = std::abs(gain);
    const double P = midGainTransform(magnitude);
    const double L = interpolate(kTable_1800d53d0, kTable_1800d5320, P);
    static constexpr double negativeF[] = {0.487, 1.0, 2.157, 4.355, 6.897, 10.038};
    static constexpr double negativeB[] = {1.0, 1.0, 0.949, 0.975, 0.678, 0.547};
    static constexpr double positiveR[] = {1.0, 1.0, 1.0, 0.88, 0.85, 0.8};
    static constexpr double positiveF[] = {0.485, 1.0, 2.219, 4.757, 7.409, 9.9};
    static constexpr double positiveB[] = {1.0, 1.0, 0.961, 1.075, 0.74, 0.7};

    double builderGain;
    double frequency;
    double bandwidth;
    if (gain < 0.0) {
        builderGain = -P;
        frequency = 700.0 * negativeF[index];
        bandwidth = L * negativeB[index];
    } else {
        const double scaledGain = gain * positiveR[index];
        const double positiveP = midGainTransform(scaledGain);
        const double positiveL = interpolate(kTable_1800d53d0, kTable_1800d5320, positiveP);
        builderGain = positiveP;
        frequency = (708.763982890133 + 198.347764931732 / (positiveP * positiveP + 0.681056044846105))
            * positiveF[index];
        bandwidth = positiveL * positiveB[index];
    }
    buildMid(eq.mid, builderGain, frequency, bandwidth, 1.0 / sampleRate);
}

} // namespace

void EqState::reset(double sampleRate, const Parameters& parameters) {
    highShelfA.reset(); highShelfB.reset(); lowShelfA.reset(); lowShelfB.reset();
    highPass.reset(); mid.reset();
    configure(sampleRate, parameters);
}

void configureHighShelfSection(Biquad& section, double gainDb, double frequencyHz,
                               double sampleRate, double shape) {
    buildHighShelf(section, gainDb, frequencyHz, sampleRate, shape);
}

void EqState::configure(double sampleRate, const Parameters& parameters) {
    const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    configureHighPass(*this, fs, parameters);
    configureLowShelf(*this, fs, parameters);
    configureMid(*this, fs, parameters);
    configureHighShelf(*this, fs, parameters);
    if (parameters.get(Parameter::EqIn) < 0.5) {
        // EQ-off keeps the documented 6 Hz / Q=.99 high-pass and clears the
        // shelf and mid gains by rebuilding their identity configurations.
        buildHighPass(highPass, 0.99, 6.0, fs);
        highPassMix = -0.5;
        highPassAlpha = 1.0 - detail::binaryExp(-2.0 * kPi * 20.0 / fs);
        Parameters eqOff = parameters;
        eqOff.setPlain(Parameter::LowShelfGain, 0.0);
        eqOff.setPlain(Parameter::MidGain, 0.0);
        eqOff.setPlain(Parameter::HighShelfGain, 0.0);
        eqOff.setPlain(Parameter::LowShelfPower, 1.0);
        eqOff.setPlain(Parameter::MidPower, 1.0);
        eqOff.setPlain(Parameter::HighShelfPower, 1.0);
        configureLowShelf(*this, fs, eqOff);
        configureMid(*this, fs, eqOff);
        configureHighShelf(*this, fs, eqOff);
    }
}

double EqState::process(double input) {
    const double hsA = highShelfA.process(input);
    const double hsB = highShelfB.process(input);
    const double xH = (highShelfMix * hsB + hsA) - highShelfMix * input;
    const double lsA = lowShelfA.process(xH);
    const double lsB = lowShelfB.process(xH);
    const double xL = (lowShelfMix * lsB + lsA) - lowShelfMix * xH;
    const double high = highPass.process(xL);
    highPassSmoother = (high - highPassSmoother) * highPassAlpha + kFloor + highPassSmoother;
    const double afterHighPass = highPassSmoother * highPassMix + high;
    return mid.processMid(afterHighPass);
}

} // namespace burnley73
