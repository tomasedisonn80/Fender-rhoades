#include "rhodes/instrument_state.hpp"

#if defined(RHODES_TEST_STATE_STREAM)
#include "plugins/vst3/state_stream.hpp"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using Bytes = std::array<std::uint8_t, 16>;
static_assert(rhodes::instrument_state_bytes == 16);
constexpr Bytes model1_golden{
    0x52, 0x48, 0x53, 0x54, 1, 0, 1, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f};
constexpr Bytes model2_golden{
    0x52, 0x48, 0x53, 0x54, 1, 0, 2, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f};
constexpr std::array<std::uint16_t, 2> models{1, 2};
constexpr std::uint32_t one = 0x3f800000U;
std::size_t checks = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

// This independent fixture uses literal headers and arithmetic base-256 digits;
// it never serializes native structs or calls the production byte helpers.
Bytes packet(std::uint16_t model, std::uint32_t gain, std::uint32_t pedal) {
    Bytes bytes{0x52, 0x48, 0x53, 0x54, 1, 0,
        static_cast<std::uint8_t>(model % 256U),
        static_cast<std::uint8_t>(model / 256U)};
    for (std::size_t digit = 0; digit < 4; ++digit) {
        bytes[8 + digit] = static_cast<std::uint8_t>(gain % 256U);
        bytes[12 + digit] = static_cast<std::uint8_t>(pedal % 256U);
        gain /= 256U;
        pedal /= 256U;
    }
    return bytes;
}

void expect_state(const std::optional<rhodes::InstrumentState>& state,
                  std::uint32_t gain, bool sustain) {
    require(state.has_value(), "valid model state rejected");
    require(std::bit_cast<std::uint32_t>(state->monitoring_gain) == gain &&
            state->sustain == sustain, "model state payload changed");
}

void model1_bytes() {
    require(packet(1, 0x3f000000U, one) == model1_golden,
            "independent model-1 fixture differs from original golden");
    require(rhodes::encode_instrument_state({0.5F, true}) == model1_golden,
            "default model-1 golden bytes changed");
    require(rhodes::encode_instrument_state({0.5F, true}, 1) == model1_golden,
            "explicit model-1 golden bytes changed");
    expect_state(rhodes::decode_instrument_state(model1_golden), 0x3f000000U, true);
    expect_state(rhodes::decode_instrument_state(model1_golden, 1), 0x3f000000U, true);
}

void model2_roundtrip() {
    require(packet(2, 0x3f000000U, one) == model2_golden,
            "independent model-2 fixture differs from literal golden");
    require(rhodes::encode_instrument_state({0.5F, true}, 2) == model2_golden,
            "model-2 golden bytes differ");
    expect_state(rhodes::decode_instrument_state(model2_golden, 2), 0x3f000000U, true);
    constexpr std::array<std::uint32_t, 11> gains{
        0U, 0x80000000U, 1U, 0x007fffffU, 0x00800000U, 0x3a83126fU,
        0x3e800000U, 0x3eaaaaabU, 0x3f000000U, 0x3f7fffffU, one};
    for (const auto gain : gains) {
        const auto canonical = gain == 0x80000000U ? 0U : gain;
        for (const bool sustain : {false, true}) {
            const auto wanted = packet(2, canonical, sustain ? one : 0U);
            const auto encoded = rhodes::encode_instrument_state(
                {std::bit_cast<float>(gain), sustain}, 2);
            require(encoded == wanted, "model-2 encoder changed exact payload bytes");
            expect_state(rhodes::decode_instrument_state(*encoded, 2), canonical, sustain);
            const auto raw = packet(2, gain, sustain ? one : 0x80000000U);
            const auto decoded = rhodes::decode_instrument_state(raw, 2);
            expect_state(decoded, canonical, sustain);
            require(rhodes::encode_instrument_state(*decoded, 2) == wanted,
                    "model-2 signed-zero canonicalization changed");
        }
    }
}

void cross_model_rejection() {
    require(!rhodes::decode_instrument_state(model1_golden, 2),
            "filtered model accepted direct state");
    require(!rhodes::decode_instrument_state(model2_golden, 1),
            "direct model accepted filtered state");
    require(!rhodes::decode_instrument_state(model2_golden),
            "default decoder no longer expects model 1");
    for (const auto model : models) {
        for (const bool sustain : {false, true}) {
            for (const float gain : {0.0F, 0.25F, 0.75F, 1.0F}) {
                const auto bytes = rhodes::encode_instrument_state({gain, sustain}, model);
                require(bytes.has_value(), "cross-model fixture failed to encode");
                const auto before = *bytes;
                const auto other = static_cast<std::uint16_t>(3U - model);
                require(!rhodes::decode_instrument_state(*bytes, other),
                        "cross-model recall accepted");
                require(*bytes == before, "rejected decoder altered caller storage");
                expect_state(rhodes::decode_instrument_state(*bytes, model),
                             std::bit_cast<std::uint32_t>(gain), sustain);
            }
        }
    }
}

void invalid_model() {
    // Exhaust the actual 16-bit argument/wire domain, including high-byte tags
    // whose low byte looks valid. An unknown tag must fail even when expected.
    for (std::uint32_t word = 0; word <= std::numeric_limits<std::uint16_t>::max(); ++word) {
        if (word == 1 || word == 2) { continue; }
        const auto model = static_cast<std::uint16_t>(word);
        const auto bytes = packet(model, 0x3f000000U, one);
        require(!rhodes::encode_instrument_state({0.5F, true}, model),
                "unsupported model argument encoded");
        require(!rhodes::decode_instrument_state(bytes, model),
                "matching unsupported wire/argument models accepted");
        for (const auto expected : models) {
            require(!rhodes::decode_instrument_state(bytes, expected),
                    "unsupported wire model accepted");
            const auto& valid = expected == 1 ? model1_golden : model2_golden;
            require(!rhodes::decode_instrument_state(valid, model),
                    "unsupported decoder model argument accepted");
        }
    }
}

void malformed_models() {
    constexpr std::array<std::uint32_t, 9> bad_gains{
        0x80000001U, 0x80800000U, 0xbf000000U, 0x3f800001U, 0x7f7fffffU,
        0x7f800000U, 0xff800000U, 0x7fc12345U, 0x7f812345U};
    constexpr std::array<std::uint32_t, 9> bad_pedals{
        1U, 0x80000001U, 0x3f000000U, 0x3f7fffffU, 0x3f800001U,
        0xbf800000U, 0x7f800000U, 0x7fc12345U, 0x7f812345U};
    for (const auto model : models) {
        const auto valid = packet(model, 0x3f000000U, one);
        for (std::size_t size = 0; size < valid.size(); ++size) {
            require(!rhodes::decode_instrument_state(std::span{valid}.first(size), model),
                    "truncated model state accepted");
        }
        std::array<std::uint8_t, 17> extended{};
        std::copy(valid.begin(), valid.end(), extended.begin());
        require(!rhodes::decode_instrument_state(extended, model),
                "model state with trailing bytes accepted");
        for (std::size_t at = 0; at < 8; ++at) {
            for (unsigned value = 0; value < 256U; ++value) {
                if (value == valid[at]) { continue; }
                auto changed = valid;
                changed[at] = static_cast<std::uint8_t>(value);
                const auto before = changed;
                require(!rhodes::decode_instrument_state(changed, model),
                        "malformed model header accepted");
                require(changed == before, "malformed decoder changed input bytes");
            }
        }
        for (const auto gain : bad_gains) {
            require(!rhodes::decode_instrument_state(packet(model, gain, one), model),
                    "malformed gain accepted for model");
            require(!rhodes::encode_instrument_state({std::bit_cast<float>(gain), true}, model),
                    "malformed gain encoded for model");
        }
        for (const auto pedal : bad_pedals) {
            require(!rhodes::decode_instrument_state(packet(model, 0x3f000000U, pedal), model),
                    "malformed sustain accepted for model");
        }
    }
}

#if defined(RHODES_TEST_STATE_STREAM)
namespace sdk = Steinberg;
enum class Fault { none, result_error, zero_progress, negative_count, excessive_count };

// Standalone direct IBStream fixture: no processor/controller variant types or
// compiled SDK libraries are needed. The host owns the fixed object lifetime.
struct MemoryStream final : sdk::IBStream {
    std::array<std::uint8_t, 64> bytes{};
    sdk::int64 position = 5;
    sdk::int64 extent = 64;
    sdk::int32 chunk = 16;
    std::size_t reads = 0, writes = 0, tells = 0, seeks = 0;
    Fault fault = Fault::none;
    std::size_t fault_at = 1;

    MemoryStream() { bytes.fill(0xa5U); }
    sdk::uint32 PLUGIN_API addRef() override { return 1; }
    sdk::uint32 PLUGIN_API release() override { return 1; }
    sdk::tresult PLUGIN_API queryInterface(const sdk::TUID interface_id, void** object) override {
        if (interface_id == nullptr || object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (sdk::FUnknownPrivate::iidEqual(interface_id, sdk::IBStream_iid) ||
            sdk::FUnknownPrivate::iidEqual(interface_id, sdk::FUnknown_iid)) {
            *object = static_cast<sdk::IBStream*>(this);
            return sdk::kResultOk;
        }
        return sdk::kNoInterface;
    }
    void load(const Bytes& source) {
        std::copy(source.begin(), source.end(), bytes.begin() + 5);
        position = 5;
        extent = 21;
    }
    sdk::tresult report_fault(sdk::int32 requested, sdk::int32* transferred) const {
        if (transferred != nullptr) {
            *transferred = fault == Fault::negative_count ? -1 :
                fault == Fault::excessive_count ? requested + 1 : 0;
        }
        return fault == Fault::result_error ? sdk::kResultFalse : sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API read(void* buffer, sdk::int32 requested,
                                sdk::int32* transferred) override {
        if (++reads == fault_at && fault != Fault::none) {
            return report_fault(requested, transferred);
        }
        if (buffer == nullptr || requested < 0 || position < 0 || position > extent ||
            extent > static_cast<sdk::int64>(bytes.size())) { return sdk::kResultFalse; }
        const auto count = static_cast<sdk::int32>(std::min<sdk::int64>(
            {requested, chunk, extent - position}));
        std::copy_n(bytes.data() + static_cast<std::size_t>(position), count,
                    static_cast<std::uint8_t*>(buffer));
        position += count;
        if (transferred != nullptr) { *transferred = count; }
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API write(void* buffer, sdk::int32 requested,
                                 sdk::int32* transferred) override {
        if (++writes == fault_at && fault != Fault::none) {
            return report_fault(requested, transferred);
        }
        if (buffer == nullptr || requested < 0 || position < 0 ||
            position > static_cast<sdk::int64>(bytes.size())) { return sdk::kResultFalse; }
        const auto count = static_cast<sdk::int32>(std::min<sdk::int64>(
            {requested, chunk, static_cast<sdk::int64>(bytes.size()) - position}));
        std::copy_n(static_cast<const std::uint8_t*>(buffer), count,
                    bytes.data() + static_cast<std::size_t>(position));
        position += count;
        extent = std::max(extent, position);
        if (transferred != nullptr) { *transferred = count; }
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API tell(sdk::int64* result) override {
        ++tells;
        if (result == nullptr) { return sdk::kInvalidArgument; }
        *result = position;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API seek(sdk::int64 offset, sdk::int32 mode,
                                sdk::int64* result) override {
        ++seeks;
        sdk::int64 base = 0;
        if (mode == kIBSeekCur) { base = position; }
        else if (mode == kIBSeekEnd) { base = extent; }
        else if (mode != kIBSeekSet) { return sdk::kInvalidArgument; }
        if (offset < -base || offset > static_cast<sdk::int64>(bytes.size()) - base) {
            return sdk::kResultFalse;
        }
        position = base + offset;
        if (result != nullptr) { *result = position; }
        return sdk::kResultOk;
    }
};

void stream_roundtrip() {
    using namespace rhodes::vst3;
    MemoryStream default_writer;
    require(write_state_stream(&default_writer, {0.5F, true}), "default stream write failed");
    require(std::equal(model1_golden.begin(), model1_golden.end(),
                       default_writer.bytes.begin() + 5), "default stream model changed");
    MemoryStream default_reader;
    default_reader.load(model1_golden);
    expect_state(read_state_stream(&default_reader), 0x3f000000U, true);
    for (const auto model : models) {
        const auto& golden = model == 1 ? model1_golden : model2_golden;
        for (sdk::int32 chunk = 1; chunk <= 16; ++chunk) {
            MemoryStream writer;
            writer.chunk = chunk;
            require(write_state_stream(&writer, {0.5F, true}, model), "model stream write failed");
            for (std::size_t at = 0; at < writer.bytes.size(); ++at) {
                require(writer.bytes[at] == (at >= 5 && at < 21 ? golden[at - 5] : 0xa5U),
                        "model stream write changed payload/guards");
            }
            const auto calls = static_cast<std::size_t>((16 + chunk - 1) / chunk);
            require(writer.position == 21 && writer.extent == 64 && writer.writes == calls,
                    "model write changed caller extent or transfer bound");
            MemoryStream reader;
            reader.load(golden);
            reader.chunk = chunk;
            const auto before = reader.bytes;
            expect_state(read_state_stream(&reader, model), 0x3f000000U, true);
            require(reader.bytes == before && reader.position == 21 && reader.reads == calls,
                    "model read changed source or transfer bound");
        }
    }
}

void stream_invalid_model() {
    using namespace rhodes::vst3;
    for (std::uint32_t word = 0; word <= std::numeric_limits<std::uint16_t>::max(); ++word) {
        if (word == 1 || word == 2) { continue; }
        const auto model = static_cast<std::uint16_t>(word);
        MemoryStream stream;
        stream.load(model2_golden);
        const auto before = stream.bytes;
        require(!write_state_stream(&stream, {0.5F, true}, model),
                "unsupported model stream write accepted");
        require(!read_state_stream(&stream, model), "unsupported model stream read accepted");
        require(stream.writes == 0 && stream.reads == 0 && stream.tells == 0 &&
                stream.seeks == 0 && stream.bytes == before && stream.position == 5 &&
                stream.extent == 21, "unsupported model touched the stream");
    }
}

void stream_rejection() {
    using namespace rhodes::vst3;
    for (const auto model : models) {
        require(!read_state_stream(nullptr, model) &&
                !write_state_stream(nullptr, {0.5F, true}, model), "null model stream accepted");
        const auto other = static_cast<std::uint16_t>(3U - model);
        const std::array<Bytes, 6> bad_packets{
            packet(other, 0x3f000000U, one), packet(0, 0x3f000000U, one),
            packet(0x0102, 0x3f000000U, one), packet(model, 0x7fc12345U, one),
            packet(model, 0x3f000000U, 0x3f000000U), packet(model, 0x3f800001U, one)};
        for (const auto& bytes : bad_packets) {
            MemoryStream reader;
            reader.load(bytes);
            reader.chunk = 1;
            const auto before = reader.bytes;
            require(!read_state_stream(&reader, model), "malformed/cross-model stream accepted");
            require(reader.bytes == before && reader.reads == 16,
                    "malformed model reader changed bytes or exceeded bounds");
        }
        for (sdk::int64 size = 0; size <= 17; ++size) {
            if (size == 16) { continue; }
            MemoryStream reader;
            reader.load(model == 1 ? model1_golden : model2_golden);
            reader.extent = 5 + size;
            require(!read_state_stream(&reader, model) && reader.reads == 0,
                    "partial/extended model stream was read or accepted");
        }
        MemoryStream writer;
        const auto before = writer.bytes;
        require(!write_state_stream(&writer, {std::bit_cast<float>(0x7fc12345U), false}, model),
                "invalid state written for model");
        require(writer.writes == 0 && writer.position == 5 && writer.extent == 64 &&
                writer.bytes == before, "invalid model state touched writer");
    }
    MemoryStream default_reader;
    default_reader.load(model2_golden);
    require(!read_state_stream(&default_reader), "default stream read accepted filtered state");
}

void stream_transfers() {
    using namespace rhodes::vst3;
    for (const auto model : models) {
        const auto& golden = model == 1 ? model1_golden : model2_golden;
        for (const auto fault : {Fault::result_error, Fault::zero_progress,
                                 Fault::negative_count, Fault::excessive_count}) {
            for (const std::size_t fail_at : {1U, 2U}) {
                MemoryStream reader;
                reader.load(golden);
                reader.chunk = 7;
                reader.fault = fault;
                reader.fault_at = fail_at;
                const auto before = reader.bytes;
                require(!read_state_stream(&reader, model) && reader.reads == fail_at &&
                        reader.bytes == before, "faulty model stream read accepted/overrun");
                MemoryStream writer;
                writer.chunk = 7;
                writer.fault = fault;
                writer.fault_at = fail_at;
                require(!write_state_stream(&writer, {0.5F, true}, model) &&
                        writer.writes == fail_at, "faulty model stream write accepted/overrun");
                const auto completed = 7U * (fail_at - 1U);
                for (std::size_t at = 0; at < writer.bytes.size(); ++at) {
                    require(writer.bytes[at] ==
                            (at >= 5 && at < 5 + completed ? golden[at - 5] : 0xa5U),
                            "faulty model write changed bytes outside completed prefix");
                }
            }
        }
    }
}
#endif
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected != "all" && selected != name) { return; }
            found = true;
            checks = 0;
            test();
            std::cout << "PASS " << name << " (" << checks << " always-active checks)\n";
        };
        run("model1_golden", model1_bytes);
        run("model2_roundtrip", model2_roundtrip);
        run("cross_model_rejection", cross_model_rejection);
        run("invalid_model", invalid_model);
        run("malformed_models", malformed_models);
#if defined(RHODES_TEST_STATE_STREAM)
        run("stream_roundtrip", stream_roundtrip);
        run("stream_invalid_model", stream_invalid_model);
        run("stream_rejection", stream_rejection);
        run("stream_transfers", stream_transfers);
#endif
        require(found, "unknown instrument model state selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
