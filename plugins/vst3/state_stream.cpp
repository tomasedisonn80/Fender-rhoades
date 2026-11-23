#include "state_stream.hpp"

#include <cstddef>

namespace rhodes::vst3 {

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
        end - start != static_cast<Steinberg::int64>(instrument_state_bytes)) {
        return std::nullopt;
    }

    EncodedInstrumentState bytes{};
    std::size_t transferred = 0;
    for (std::size_t attempt = 0; attempt < instrument_state_bytes &&
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
    auto encoded = encode_instrument_state(state, model);
    if (!encoded) { return false; }
    std::size_t transferred = 0;
    for (std::size_t attempt = 0; attempt < instrument_state_bytes &&
         transferred < encoded->size(); ++attempt) {
        const auto remaining = static_cast<Steinberg::int32>(encoded->size() - transferred);
        Steinberg::int32 written = 0;
        if (stream->write(encoded->data() + transferred, remaining, &written) !=
                Steinberg::kResultOk || written <= 0 || written > remaining) {
            return false;
        }
        transferred += static_cast<std::size_t>(written);
    }
    return transferred == encoded->size();
}

} // namespace rhodes::vst3
