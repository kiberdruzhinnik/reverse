#include "plugin.hpp"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace burnley73 {
namespace {
// The licensed controller advertises the 15 DSP controls as IDs 0..14.
constexpr ParamID kFirstParamId = 0;
constexpr ParamID kHostBypassParamId = 0x62706173; // 'bpas'
ParamID paramId(std::size_t index) { return kFirstParamId + static_cast<ParamID>(index); }
Parameter parameterFor(ParamID id) { return static_cast<Parameter>(id - kFirstParamId); }

const char* const kChoices[kParameterCount][21] = {
    {"OFF", "50", "80", "160", "300"},
    {"35", "60", "110", "220"},
    {}, {"360", "700", "1k6", "3k2", "4k8", "7k2"}, {}, {},
    {"Line 40", "Line 35", "Line 30", "Line 25", "Line 20", "5", "Line 10", "Line 5", "OFF",
     "Mic 5", "Mic 10", "Mic 15", "Mic 20", "Mic 25", "Mic 30", "Mic 35", "Mic 40", "Mic 45", "Mic 50", "Mic 55", "Mic 60"},
    {}, {"Off", "On"}, {"Off", "On"}, {"Off", "On"}, {"Off", "On"}, {"Off", "On"},
    {"Normal", "Inverted"}, {"Off", "On"}
};

bool readValues(IBStream* stream, Parameters& parameters) {
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        double value = 0.0;
        int32 bytes = 0;
        if (stream->read(&value, sizeof(value), &bytes) != kResultOk || bytes != sizeof(value)) return false;
        parameters.setPlain(static_cast<Parameter>(i), value);
    }
    return true;
}
bool writeValues(IBStream* stream, const Parameters& parameters) {
    for (double value : parameters.plain) {
        int32 bytes = 0;
        if (stream->write(&value, sizeof(value), &bytes) != kResultOk || bytes != sizeof(value)) return false;
    }
    return true;
}
class OutputParameter final : public Steinberg::Vst::Parameter {
public:
    OutputParameter(const TChar* title, ParamID id, double defaultPlain)
        : Steinberg::Vst::Parameter(title, id, STR16("dB"), plainToNormalized(burnley73::Parameter::Output, defaultPlain),
                    kStepCountContinuous, Steinberg::Vst::ParameterInfo::kCanAutomate) {
        info.stepCount = kStepCountContinuous;
        info.defaultNormalizedValue = plainToNormalized(burnley73::Parameter::Output, defaultPlain);
        info.flags = Steinberg::Vst::ParameterInfo::kCanAutomate;
    }
    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE {
        return normalizedToPlain(burnley73::Parameter::Output, normalized);
    }
    ParamValue toNormalized(ParamValue plain) const SMTG_OVERRIDE {
        return plainToNormalized(burnley73::Parameter::Output, plain);
    }
    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        UString wrapper(string, 128);
        wrapper.printFloat(toPlain(normalized), 1);
    }
    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        char text[128]{};
        UString(const_cast<TChar*>(string), static_cast<int32>(tstrlen(string))).toAscii(text, 128);
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        if (end == text) return false;
        normalized = toNormalized(value);
        return true;
    }
};
void addParameters(ParameterContainer& container) {
    const auto& infos = parameterInfo();
    auto* bypass = new StringListParameter(USTRING("Bypass"), kHostBypassParamId);
    bypass->appendString(USTRING("Off"));
    bypass->appendString(USTRING("On"));
    bypass->getInfo().defaultNormalizedValue = 0.0;
    bypass->setNormalized(0.0);
    container.addParameter(bypass);
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        const auto& info = infos[i];
        const ParamID id = paramId(i);
        if (info.choices > 0 || info.boolean) {
            auto* list = new StringListParameter(USTRING(info.title), id);
            const int count = info.boolean ? 2 : (i == static_cast<std::size_t>(Parameter::Model) ? 21 : info.choices);
            for (int choice = 0; choice < count; ++choice) {
                UString128 label(kChoices[i][choice]);
                list->appendString(label);
            }
            const double normalizedDefault = plainToNormalized(static_cast<Parameter>(i), info.plainDefault);
            list->getInfo().defaultNormalizedValue = normalizedDefault;
            list->setNormalized(normalizedDefault);
            container.addParameter(list);
        } else if (static_cast<Parameter>(i) == Parameter::Output) {
            container.addParameter(new OutputParameter(USTRING(info.title), id, info.plainDefault));
        } else {
            // The original EQ gain path is continuous despite advertising 0.1 dB
            // display precision. Output also uses a custom nonlinear conversion.
            const int32 steps = kStepCountContinuous;
            auto* range = new RangeParameter(USTRING(info.title), id, nullptr, info.minimum,
                info.maximum, info.plainDefault, steps);
            range->setNormalized(plainToNormalized(static_cast<Parameter>(i), info.plainDefault));
            if (i == static_cast<std::size_t>(Parameter::LowShelfGain)
                || i == static_cast<std::size_t>(Parameter::MidGain)
                || i == static_cast<std::size_t>(Parameter::HighShelfGain)) {
                range->getInfo().units[0] = 'd';
                range->getInfo().units[1] = 'B';
                range->setPrecision(1);
            }
            container.addParameter(range);
        }
    }
}
}

tresult PLUGIN_API VstProcessor::initialize(FUnknown* context) {
    const tresult result = SingleComponentEffect::initialize(context);
    if (result != kResultOk) return result;
    addAudioInput(STR16("Audio In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Audio Out"), SpeakerArr::kStereo);
    addParameters(parameters);
    return kResultOk;
}

tresult PLUGIN_API VstProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numInputs,
    SpeakerArrangement* outputs, int32 numOutputs) {
    if (numInputs != 1 || numOutputs != 1 || inputs[0] != outputs[0]
        || (inputs[0] != SpeakerArr::kMono && inputs[0] != SpeakerArr::kStereo)) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(inputs, numInputs, outputs, numOutputs);
}

tresult PLUGIN_API VstProcessor::setupProcessing(ProcessSetup& setup) {
    const tresult result = SingleComponentEffect::setupProcessing(setup);
    if (result != kResultOk) return result;
    engine_.reset(setup.sampleRate);
    return kResultOk;
}

tresult PLUGIN_API VstProcessor::setProcessing(TBool) {
    return kResultOk;
}

tresult PLUGIN_API VstProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API VstProcessor::process(ProcessData& data) {
    // The shipped processor consumes the final point from each queue before
    // processing the block; it does not split DSP at individual sample offsets.
    if (data.inputParameterChanges) {
        for (int32 q = 0; q < data.inputParameterChanges->getParameterCount(); ++q) {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData(q);
            if (!queue) continue;
            const ParamID id = queue->getParameterId();
            const int32 pointCount = queue->getPointCount();
            if (pointCount <= 0) continue;
            int32 sampleOffset = 0;
            ParamValue value = 0.0;
            if (queue->getPoint(pointCount - 1, sampleOffset, value) != kResultTrue) continue;
            if (id == kHostBypassParamId) {
                hostBypass_ = value > 0.5;
            } else if (id >= kFirstParamId && id < kFirstParamId + kParameterCount) {
                engine_.setNormalized(parameterFor(id), value);
            }
        }
    }
    if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0) return kResultOk;
    const int32 channels = std::min(data.inputs[0].numChannels, data.outputs[0].numChannels);
    if (channels <= 0) return kResultOk;
    if (hostBypass_) {
        if (data.symbolicSampleSize == kSample32) {
            for (int32 ch = 0; ch < channels; ++ch) {
                const auto* source = static_cast<const float*>(data.inputs[0].channelBuffers32[ch]);
                auto* destination = static_cast<float*>(data.outputs[0].channelBuffers32[ch]);
                std::copy_n(source, data.numSamples, destination);
            }
        } else if (data.symbolicSampleSize == kSample64) {
            for (int32 ch = 0; ch < channels; ++ch) {
                const auto* source = static_cast<const double*>(data.inputs[0].channelBuffers64[ch]);
                auto* destination = static_cast<double*>(data.outputs[0].channelBuffers64[ch]);
                std::copy_n(source, data.numSamples, destination);
            }
        }
        return kResultOk;
    }
    if (data.symbolicSampleSize == kSample32) {
        std::array<std::vector<double>, 2> in, out;
        std::array<double*, 2> inPtrs{}, outPtrs{};
        for (int32 ch = 0; ch < channels; ++ch) {
            in[ch].resize(data.numSamples); out[ch].resize(data.numSamples);
            auto* source = static_cast<float*>(data.inputs[0].channelBuffers32[ch]);
            for (int32 i = 0; i < data.numSamples; ++i) in[ch][i] = source[i];
            inPtrs[ch] = in[ch].data(); outPtrs[ch] = out[ch].data();
        }
        engine_.process(inPtrs.data(), outPtrs.data(), channels, data.numSamples);
        for (int32 ch = 0; ch < channels; ++ch) {
            auto* dest = static_cast<float*>(data.outputs[0].channelBuffers32[ch]);
            for (int32 i = 0; i < data.numSamples; ++i) dest[i] = static_cast<float>(out[ch][i]);
        }
    } else if (data.symbolicSampleSize == kSample64) {
        std::array<double*, 2> inPtrs{}, outPtrs{};
        for (int32 ch = 0; ch < channels; ++ch) {
            inPtrs[ch] = static_cast<double*>(data.inputs[0].channelBuffers64[ch]);
            outPtrs[ch] = static_cast<double*>(data.outputs[0].channelBuffers64[ch]);
        }
        engine_.process(inPtrs.data(), outPtrs.data(), channels, data.numSamples);
    } else return kResultFalse;
    return kResultOk;
}

tresult PLUGIN_API VstProcessor::getState(IBStream* state) {
    if (!writeValues(state, engine_.parameters)) return kResultFalse;
    int32 bytes = 0;
    return state->write(&serializedHostFlag_, sizeof(serializedHostFlag_), &bytes) == kResultOk
        && bytes == sizeof(serializedHostFlag_) ? kResultOk : kResultFalse;
}
tresult PLUGIN_API VstProcessor::setState(IBStream* state) {
    Parameters restored;
    if (!readValues(state, restored)) return kResultFalse;
    int32 bytes = 0;
    if (state->read(&serializedHostFlag_, sizeof(serializedHostFlag_), &bytes) != kResultOk
        || bytes != sizeof(serializedHostFlag_)) return kResultFalse;
    for (std::size_t i = 0; i < kParameterCount; ++i)
        engine_.setPlain(static_cast<Parameter>(i), restored.plain[i]);
    return kResultOk;
}

tresult PLUGIN_API VstProcessor::setComponentState(IBStream* state) {
    Parameters restored;
    if (!readValues(state, restored)) return kResultFalse;
    int32 bytes = 0;
    if (state->read(&serializedHostFlag_, sizeof(serializedHostFlag_), &bytes) != kResultOk
        || bytes != sizeof(serializedHostFlag_)) return kResultFalse;
    for (std::size_t i = 0; i < kParameterCount; ++i)
        setParamNormalized(paramId(i), plainToNormalized(static_cast<Parameter>(i), restored.plain[i]));
    return kResultOk;
}
tresult PLUGIN_API VstProcessor::setEditorState(IBStream* state) {
    Parameters restored;
    if (!readValues(state, restored)) return kResultFalse;
    int32 bytes = 0;
    if (state->read(&serializedHostFlag_, sizeof(serializedHostFlag_), &bytes) != kResultOk
        || bytes != sizeof(serializedHostFlag_)) return kResultFalse;
    for (std::size_t i = 0; i < kParameterCount; ++i)
        setParamNormalized(paramId(i), plainToNormalized(static_cast<Parameter>(i), restored.plain[i]));
    return kResultOk;
}
tresult PLUGIN_API VstProcessor::getEditorState(IBStream* state) {
    Parameters current;
    for (std::size_t i = 0; i < kParameterCount; ++i)
        current.setNormalized(static_cast<Parameter>(i), getParamNormalized(paramId(i)));
    if (!writeValues(state, current)) return kResultFalse;
    int32 bytes = 0;
    return state->write(&serializedHostFlag_, sizeof(serializedHostFlag_), &bytes) == kResultOk
        && bytes == sizeof(serializedHostFlag_) ? kResultOk : kResultFalse;
}

} // namespace burnley73
