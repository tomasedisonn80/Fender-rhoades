#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cstddef>

namespace rhodes::vst3 {

// Permanent identities for this synthetic model/version. Changing the sound
// model or state semantics requires an explicit compatibility decision.
inline const Steinberg::FUID kProcessorUID{0xBDDA70F4, 0x6F9244C1, 0xA5639EE3, 0x71A0B804};
inline const Steinberg::FUID kControllerUID{0xEA801E8B, 0x97864ED9, 0xB3A0E657, 0x2D81CE61};
inline constexpr Steinberg::Vst::ParamID kGainId = 0;
inline constexpr Steinberg::Vst::ParamID kSustainId = 1;
// The pinned SDK validator configures 8192-frame host blocks. The adapter
// preflights this fixed bound, then calls the unchanged core one sample at a time.
inline constexpr std::size_t kMaximumHostFrames = 8192;
inline constexpr const char* kPluginName = "Rhodes Research Prototype";
inline constexpr const char* kPluginVersion = "0.1.0";

} // namespace rhodes::vst3
