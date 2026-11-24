#include "state_stream.hpp"

#include <cstddef>
#include <span>

namespace rhodes::vst3 {
namespace {

bool write_bytes(Steinberg::IBStream& stream, std::span<std::uint8_t> bytes) noexcept {
    std::size_t transferred = 0;
    for (std::size_t attempt = 0; attempt < bytes.size() &&
         transferred < bytes.size(); ++attempt) {
        const auto remaining = static_cast<Steinberg::int32>(bytes.size() - transferred);
        Steinberg::int32 written = 0;
        if (stream.write(bytes.data() + transferred, remaining, &written) !=
                Steinberg::kResultOk || written <= 0 || written > remaining) {
            return false;
        }
        transferred += static_cast<std::size_t>(written);
    }
    return transferred == bytes.size();
}

} // namespace

std::optional<InstrumentState> read_state_stream(Steinberg::IBStream* stream,
                                                std::uint16_t model) noexcept {
    if (stream == nullptr || !state_detail::valid_model(model)) { return std::nullopt; }
    Steinberg::int64 start = -1;
    Steinberg::int64 end = -1;
    Steinberg::int64 restored = -1;
    if (stream->tell(&start) != Steinberg::kResultOk || start < 0 ||
        stream->seek(0, Steinberg::IBStream::kIBSeekEnd, &end) != Steinberg::kResultOk ||
        end < start ||
        stream->seek(start, Steinberg::IBStream::kIBSeekSet, &restored) !=
            Steinberg::kResultOk || restored != start ||
        (end - start != static_cast<Steinberg::int64>(instrument_state_bytes) &&
         end - start != static_cast<Steinberg::int64>(instrument_state_v2_bytes))) {
        return std::nullopt;
    }

    EncodedInstrumentStateV2 storage{};
    const auto bytes = std::span{storage}.first(static_cast<std::size_t>(end - start));
    std::size_t transferred = 0;
    for (std::size_t attempt = 0; attempt < bytes.size() &&
         transferred < bytes.size(); ++attempt) {
        const auto remaining = static_cast<Steinberg::int32>(bytes.size() - transferred);
        Steinberg::int32 received = 0;
        if (stream->read(bytes.data() + transferred, remaining, &received) !=
                Steinberg::kResultOk || received <= 0 || received > remaining) {
            return std::nullopt;
        }
        transferred += static_cast<std::size_t>(received);
    }
    if (transferred != bytes.size()) { return std::nullopt; }
    return decode_instrument_state(bytes, model);
}

bool write_state_stream(Steinberg::IBStream* stream, InstrumentState state,
                        std::uint16_t model) noexcept {
    if (stream == nullptr) { return false; }
    // Select by canonical bits without floating arithmetic or host FP-mode effects.
    if (state_detail::gain_bits(state.expression) == 0x3f800000U) {
        auto encoded = encode_instrument_state(state, model);
        return encoded && write_bytes(*stream, *encoded);
    }
    auto encoded = encode_instrument_state_v2(state, model);
    return encoded && write_bytes(*stream, *encoded);
}

} // namespace rhodes::vst3
