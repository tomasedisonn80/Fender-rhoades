#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cstddef>
#include <cstdint>

namespace rhodes::vst3 {

// Independent identities keep new signal paths from replacing saved instances
// of either existing research model. No live quality/latency switch.
#if defined(RHODES_FILTERED_VST3) && defined(RHODES_QUADRATURE_VST3)
#error "Filtered and quadrature VST3 variants are mutually exclusive"
#endif

#if defined(RHODES_QUADRATURE_VST3)
inline const Steinberg::FUID kProcessorUID{0xD73B886A, 0x72E04FC8, 0x92435F1D, 0xC5F0B6A9};
inline const Steinberg::FUID kControllerUID{0xA45F23E1, 0xB68A4D79, 0xAF31472C, 0x8E909D52};
inline constexpr std::uint16_t kStateModel = 3;
inline constexpr bool kFilteredAudio = false;
inline constexpr bool kQuadratureAudio = true;
inline constexpr std::uint32_t kLatencyFrames = 48;
inline constexpr const char* kPluginName = "Rhodes Research Quadrature";
inline constexpr const char* kControllerName = "Rhodes Quadrature Controller";
inline constexpr const char* kPluginVersion = "0.3.0";
#elif defined(RHODES_FILTERED_VST3)
inline const Steinberg::FUID kProcessorUID{0x4C0E773A, 0x24FC4BE9, 0xB804BC71, 0xB8B98B33};
inline const Steinberg::FUID kControllerUID{0x0569EDDB, 0xD21D4568, 0xA9ED52C3, 0xD8545B6D};
inline constexpr std::uint16_t kStateModel = 2;
inline constexpr bool kFilteredAudio = true;
inline constexpr bool kQuadratureAudio = false;
inline constexpr std::uint32_t kLatencyFrames = 48;
inline constexpr const char* kPluginName = "Rhodes Research Filtered";
inline constexpr const char* kControllerName = "Rhodes Filtered Controller";
inline constexpr const char* kPluginVersion = "0.2.0";
#else
inline const Steinberg::FUID kProcessorUID{0xBDDA70F4, 0x6F9244C1, 0xA5639EE3, 0x71A0B804};
inline const Steinberg::FUID kControllerUID{0xEA801E8B, 0x97864ED9, 0xB3A0E657, 0x2D81CE61};
inline constexpr std::uint16_t kStateModel = 1;
inline constexpr bool kFilteredAudio = false;
inline constexpr bool kQuadratureAudio = false;
inline constexpr std::uint32_t kLatencyFrames = 0;
inline constexpr const char* kPluginName = "Rhodes Research Prototype";
inline constexpr const char* kControllerName = "Rhodes Research Controller";
inline constexpr const char* kPluginVersion = "0.1.0";
#endif
inline constexpr Steinberg::Vst::ParamID kGainId = 0;
inline constexpr Steinberg::Vst::ParamID kSustainId = 1;
inline constexpr Steinberg::Vst::ParamID kExpressionId = 2;
// Transient per-channel CC123 commands. Existing configuration IDs stay stable.
inline constexpr Steinberg::Vst::ParamID kAllNotesOffBaseId = 0x1000;
inline constexpr std::size_t kMidiChannelCount = 16;
inline constexpr std::size_t kControllerParameterCount = 3 + kMidiChannelCount;
inline constexpr std::size_t kMaximumChannelCommands = 256;

[[nodiscard]] inline constexpr int all_notes_off_channel(
    Steinberg::Vst::ParamID id) noexcept {
    return id >= kAllNotesOffBaseId && id < kAllNotesOffBaseId + kMidiChannelCount ?
        static_cast<int>(id - kAllNotesOffBaseId) : -1;
}
// The pinned SDK validator configures 8192-frame host blocks. The adapter
// preflights this fixed bound, then calls the unchanged core one sample at a time.
inline constexpr std::size_t kMaximumHostFrames = 8192;

} // namespace rhodes::vst3
