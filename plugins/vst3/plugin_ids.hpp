#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cstddef>
#include <cstdint>

namespace rhodes::vst3 {

// Independent identities keep the filtered signal path from replacing saved
// instances of the direct research model. No live quality/latency switch.
#if defined(RHODES_FILTERED_VST3)
inline const Steinberg::FUID kProcessorUID{0x4C0E773A, 0x24FC4BE9, 0xB804BC71, 0xB8B98B33};
inline const Steinberg::FUID kControllerUID{0x0569EDDB, 0xD21D4568, 0xA9ED52C3, 0xD8545B6D};
inline constexpr std::uint16_t kStateModel = 2;
inline constexpr bool kFilteredAudio = true;
inline constexpr const char* kPluginName = "Rhodes Research Filtered";
inline constexpr const char* kControllerName = "Rhodes Filtered Controller";
inline constexpr const char* kPluginVersion = "0.2.0";
#else
inline const Steinberg::FUID kProcessorUID{0xBDDA70F4, 0x6F9244C1, 0xA5639EE3, 0x71A0B804};
inline const Steinberg::FUID kControllerUID{0xEA801E8B, 0x97864ED9, 0xB3A0E657, 0x2D81CE61};
inline constexpr std::uint16_t kStateModel = 1;
inline constexpr bool kFilteredAudio = false;
inline constexpr const char* kPluginName = "Rhodes Research Prototype";
inline constexpr const char* kControllerName = "Rhodes Research Controller";
inline constexpr const char* kPluginVersion = "0.1.0";
#endif
inline constexpr Steinberg::Vst::ParamID kGainId = 0;
inline constexpr Steinberg::Vst::ParamID kSustainId = 1;
// The pinned SDK validator configures 8192-frame host blocks. The adapter
// preflights this fixed bound, then calls the unchanged core one sample at a time.
inline constexpr std::size_t kMaximumHostFrames = 8192;

} // namespace rhodes::vst3
