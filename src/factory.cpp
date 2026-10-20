#include "plugin.hpp"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID Burnley73ProcessorUID(0x10B1C2D3, 0xE4F54768, 0x90123456, 0x789ABCDE);
static const FUID Burnley73ControllerUID(0xA0B1C2D3, 0xE4F54768, 0x90123456, 0x789ABCDE);

BEGIN_FACTORY_DEF("Burnley Audio", "https://example.invalid", "mailto:audio@example.invalid")

DEF_CLASS2(INLINE_UID_FROM_FUID(Burnley73ProcessorUID), PClassInfo::kManyInstances,
    kVstAudioEffectClass, "Burnley 73 DSP", kDistributable,
    "Fx|EQ", "1.0.0", kVstVersionString, burnley73::VstProcessor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(Burnley73ControllerUID), PClassInfo::kManyInstances,
    kVstComponentControllerClass, "Burnley 73 DSP Controller", 0, "", "1.0.0",
    kVstVersionString, burnley73::VstController::createInstance)

END_FACTORY
