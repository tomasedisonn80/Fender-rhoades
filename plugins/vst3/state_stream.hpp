#pragma once

#include "rhodes/instrument_state.hpp"
#include "pluginterfaces/base/ibstream.h"

#include <optional>

namespace rhodes::vst3 {

// UI-thread helpers only. Reading requires exactly sixteen bytes remaining in a
// seekable stream. Neither helper retains the stream or publishes configuration.
[[nodiscard]] std::optional<InstrumentState>
read_state_stream(Steinberg::IBStream* stream) noexcept;

// The host owns destination sizing/truncation. Positive short transfers are
// accepted, with at most sixteen calls; errors and zero progress fail.
[[nodiscard]] bool write_state_stream(Steinberg::IBStream* stream,
                                      InstrumentState state) noexcept;

} // namespace rhodes::vst3
