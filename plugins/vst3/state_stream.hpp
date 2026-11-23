#pragma once

#include "rhodes/instrument_state.hpp"
#include "pluginterfaces/base/ibstream.h"

#include <optional>

namespace rhodes::vst3 {

// UI-thread helpers only. Reading requires exactly sixteen bytes remaining in a
// seekable stream. Neither helper retains the stream or publishes configuration.
// The caller's immutable model must be 1 (direct), 2 (filtered), or 3 (quadrature).
// Defaults stay model 1 in every build; mismatched wire models are rejected,
// never converted.
[[nodiscard]] std::optional<InstrumentState>
read_state_stream(Steinberg::IBStream* stream, std::uint16_t model = 1) noexcept;

// The host owns destination sizing/truncation. Positive short transfers are
// accepted, with at most sixteen calls; errors and zero progress fail.
// Invalid state/model arguments fail before the first stream write.
[[nodiscard]] bool write_state_stream(Steinberg::IBStream* stream,
                                      InstrumentState state, std::uint16_t model = 1) noexcept;

} // namespace rhodes::vst3
