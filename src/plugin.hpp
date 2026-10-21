#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "engine.hpp"

namespace burnley73 {

class VstProcessor final : public Steinberg::Vst::SingleComponentEffect {
public:
    VstProcessor() = default;
    static Steinberg::FUnknown* createInstance(void*) { return static_cast<Steinberg::Vst::IAudioProcessor*>(new VstProcessor()); }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
        Steinberg::int32 numInputs, Steinberg::Vst::SpeakerArrangement* outputs,
        Steinberg::int32 numOutputs) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setEditorState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getEditorState(Steinberg::IBStream* state) SMTG_OVERRIDE;
private:
    AudioEngine engine_;
    bool hostBypass_ = false;
    Steinberg::int32 serializedHostFlag_ = 0;
};

} // namespace burnley73
