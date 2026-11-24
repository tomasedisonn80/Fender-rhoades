#include "controller.hpp"
#include "processor.hpp"
#include "public.sdk/source/main/pluginfactory.h"

// Separate component/controller classes, with no GUI or example source copied
// into the project. SDK lifecycle entry points are supplied by its CMake target.
BEGIN_FACTORY("Rhodes Research", "https://github.com/baabi-boo/Fender-rhoades", "",
              Steinberg::PFactoryInfo::kNoFlags)

DEF_CLASS2(INLINE_UID_FROM_FUID(rhodes::vst3::kProcessorUID),
           Steinberg::PClassInfo::kManyInstances, kVstAudioEffectClass,
           rhodes::vst3::kPluginName, Steinberg::Vst::kDistributable,
           "Instrument|Synth", rhodes::vst3::kPluginVersion,
           kVstVersionString, rhodes::vst3::RhodesProcessor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(rhodes::vst3::kControllerUID),
           Steinberg::PClassInfo::kManyInstances, kVstComponentControllerClass,
           rhodes::vst3::kControllerName, 0, "", rhodes::vst3::kPluginVersion,
           kVstVersionString, rhodes::vst3::RhodesController::createInstance)

END_FACTORY
