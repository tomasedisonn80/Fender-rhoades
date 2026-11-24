#include "plugins/vst3/state_stream.hpp"

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
namespace sdk = Steinberg;
using rhodes::vst3::read_state_stream;
using rhodes::vst3::write_state_stream;

static_assert(rhodes::instrument_state_bytes == 16);
static_assert(rhodes::instrument_state_v2_bytes == 20);
constexpr std::uint32_t one = 0x3f800000U;
constexpr std::uint32_t half = 0x3f000000U;
constexpr std::uint32_t quarter = 0x3e800000U;
constexpr std::uint8_t guard = 0xa5U;
constexpr std::array<std::uint16_t, 3> models{1, 2, 3};
std::size_t checks = 0, cases = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

// Literal independent fixtures, including the little-endian model and version.
// Neither production encoder is used to construct any expected packet.
constexpr std::array<std::array<std::uint8_t, 16>, 3> legacy{
    std::array<std::uint8_t, 16>{'R', 'H', 'S', 'T', 1, 0, 1, 0,
        0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f},
    std::array<std::uint8_t, 16>{'R', 'H', 'S', 'T', 1, 0, 2, 0,
        0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f},
    std::array<std::uint8_t, 16>{'R', 'H', 'S', 'T', 1, 0, 3, 0,
        0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f}};
constexpr std::array<std::array<std::uint8_t, 20>, 3> expression{
    std::array<std::uint8_t, 20>{'R', 'H', 'S', 'T', 2, 0, 1, 0,
        0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f, 0, 0, 0x80, 0x3e},
    std::array<std::uint8_t, 20>{'R', 'H', 'S', 'T', 2, 0, 2, 0,
        0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f, 0, 0, 0x80, 0x3e},
    std::array<std::uint8_t, 20>{'R', 'H', 'S', 'T', 2, 0, 3, 0,
        0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f, 0, 0, 0x80, 0x3e}};

struct Packet {
    std::array<std::uint8_t, 20> bytes{};
    std::size_t size = 0;
};

Packet literal(bool v2, std::uint16_t model) {
    Packet result;
    const auto index = static_cast<std::size_t>(model - 1);
    if (v2) {
        result.bytes = expression[index];
        result.size = 20;
    } else {
        std::copy(legacy[index].begin(), legacy[index].end(), result.bytes.begin());
        result.size = 16;
    }
    return result;
}

void put_word(Packet& packet, std::size_t offset, std::uint32_t word) {
    for (std::size_t digit = 0; digit < 4; ++digit) {
        packet.bytes[offset + digit] = static_cast<std::uint8_t>(word & 255U);
        word >>= 8U;
    }
}

Packet packet(bool v2, std::uint16_t model, std::uint32_t gain,
              std::uint32_t pedal, std::uint32_t value) {
    auto result = literal(v2, 1);
    result.bytes[6] = static_cast<std::uint8_t>(model & 255U);
    result.bytes[7] = static_cast<std::uint8_t>(model >> 8U);
    put_word(result, 8, gain);
    put_word(result, 12, pedal);
    if (v2) { put_word(result, 16, value); }
    return result;
}

std::uint32_t canonical(std::uint32_t word) { return word == 0x80000000U ? 0U : word; }

rhodes::InstrumentState state(std::uint32_t gain, bool pedal, std::uint32_t value) {
    return {std::bit_cast<float>(gain), pedal, std::bit_cast<float>(value)};
}

void expect_state(rhodes::InstrumentState actual, std::uint32_t gain,
                  bool pedal, std::uint32_t value) {
    require(std::bit_cast<std::uint32_t>(actual.monitoring_gain) == gain,
            "decoded monitoring-gain bits differ");
    require(actual.sustain == pedal, "decoded sustain differs");
    require(std::bit_cast<std::uint32_t>(actual.expression) == value,
            "decoded expression bits differ");
}

enum class Fault { none, error, zero, negative, overreported, missing_count };
constexpr std::array faults{Fault::error, Fault::zero, Fault::negative,
                            Fault::overreported, Fault::missing_count};

// A fixed-lifetime standalone host stream. It deliberately has no processor,
// controller, production codec, or shared-fixture dependency.
struct Stream final : sdk::IBStream {
    std::array<std::uint8_t, 64> bytes{};
    std::array<sdk::int32, 20> requests{};
    sdk::int64 position = 0, extent = 64;
    sdk::int32 chunk = 20;
    std::size_t reads = 0, writes = 0, tells = 0, seeks = 0;
    Fault fault = Fault::none;
    std::size_t fault_at = 1;
    bool fail_tell = false, fail_end = false, fail_restore = false, wrong_restore = false;

    Stream() { bytes.fill(guard); }
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

    void load(const Packet& source, std::size_t prefix = 7) {
        require(prefix + source.size <= bytes.size(), "invalid source fixture extent");
        std::copy_n(source.bytes.begin(), source.size, bytes.begin() +
                    static_cast<std::ptrdiff_t>(prefix));
        position = static_cast<sdk::int64>(prefix);
        extent = position + static_cast<sdk::int64>(source.size);
    }

    sdk::tresult report_fault(sdk::int32 requested, sdk::int32* transferred) const {
        if (transferred != nullptr && fault != Fault::missing_count) {
            *transferred = fault == Fault::negative ? -1 :
                fault == Fault::overreported ? requested + 1 :
                fault == Fault::error ? std::min(requested, chunk) : 0;
        }
        return fault == Fault::error ? sdk::kResultFalse : sdk::kResultOk;
    }

    sdk::tresult PLUGIN_API read(void* buffer, sdk::int32 requested,
                                sdk::int32* transferred) override {
        ++reads;
        if (reads <= requests.size()) { requests[reads - 1] = requested; }
        if (reads == fault_at && fault != Fault::none) {
            return report_fault(requested, transferred);
        }
        if (buffer == nullptr || requested <= 0 || chunk <= 0 || position < 0 ||
            position > extent || extent > static_cast<sdk::int64>(bytes.size())) {
            return sdk::kResultFalse;
        }
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
        ++writes;
        if (writes <= requests.size()) { requests[writes - 1] = requested; }
        if (writes == fault_at && fault != Fault::none) {
            return report_fault(requested, transferred);
        }
        if (buffer == nullptr || requested <= 0 || chunk <= 0 || position < 0 ||
            position > static_cast<sdk::int64>(bytes.size())) {
            return sdk::kResultFalse;
        }
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
        if (fail_tell || result == nullptr) { return sdk::kResultFalse; }
        *result = position;
        return sdk::kResultOk;
    }

    sdk::tresult PLUGIN_API seek(sdk::int64 offset, sdk::int32 mode,
                                sdk::int64* result) override {
        ++seeks;
        if ((mode == kIBSeekEnd && fail_end) || (mode == kIBSeekSet && fail_restore)) {
            return sdk::kResultFalse;
        }
        // The production reader uses only these two bounded seeks. Allow a
        // synthetic large extent so its length rejection is independently tested.
        if (mode == kIBSeekEnd && offset == 0) { position = extent; }
        else if (mode == kIBSeekSet && offset >= 0) { position = offset; }
        else { return sdk::kResultFalse; }
        if (result != nullptr) {
            *result = position + (mode == kIBSeekSet && wrong_restore ? 1 : 0);
        }
        return sdk::kResultOk;
    }
};

std::size_t transfer_count(std::size_t length, sdk::int32 chunk) {
    const auto width = static_cast<std::size_t>(chunk);
    return (length + width - 1) / width;
}

void expect_requests(const Stream& stream, std::size_t calls, std::size_t length) {
    require(calls <= length && calls <= stream.requests.size(), "transfer attempt bound exceeded");
    for (std::size_t index = 0; index < calls; ++index) {
        require(stream.requests[index] == static_cast<sdk::int32>(
                    length - index * static_cast<std::size_t>(stream.chunk)),
                "transfer did not request exactly the remaining packet bytes");
    }
}

void expect_output(const Stream& stream, const Packet& wanted,
                   std::size_t prefix, std::size_t written) {
    for (std::size_t index = 0; index < stream.bytes.size(); ++index) {
        const auto expected = index >= prefix && index < prefix + written ?
            wanted.bytes[index - prefix] : guard;
        require(stream.bytes[index] == expected, "written payload or host guard differs");
    }
    require(stream.position == static_cast<sdk::int64>(prefix + written),
            "write cursor differs from completed transfers");
    require(stream.extent == 64, "writer truncated host-owned storage");
    require(stream.tells == 0 && stream.seeks == 0 && stream.reads == 0,
            "writer inspected or repositioned host storage");
}

void roundtrip_case(const Packet& source, const Packet& wanted, std::uint16_t model,
                    std::uint32_t gain, bool pedal, std::uint32_t value,
                    std::size_t prefix, sdk::int32 chunk) {
    ++cases;
    Stream reader;
    reader.load(source, prefix);
    reader.chunk = chunk;
    const auto original = reader.bytes;
    const auto decoded = read_state_stream(&reader, model);
    require(decoded.has_value(), "valid packet failed to decode");
    expect_state(*decoded, gain, pedal, value);
    require(reader.bytes == original && reader.position ==
                static_cast<sdk::int64>(prefix + source.size),
            "reader changed caller storage or stopped at the wrong position");
    require(reader.reads == transfer_count(source.size, chunk) && reader.tells == 1 &&
            reader.seeks == 2 && reader.writes == 0, "read call count differs");
    expect_requests(reader, reader.reads, source.size);

    Stream writer;
    writer.position = static_cast<sdk::int64>(prefix);
    writer.chunk = chunk;
    writer.fail_tell = writer.fail_end = writer.fail_restore = true;
    require(write_state_stream(&writer, *decoded, model), "valid decoded state failed to save");
    expect_output(writer, wanted, prefix, wanted.size);
    require(writer.writes == transfer_count(wanted.size, chunk), "write call count differs");
    expect_requests(writer, writer.writes, wanted.size);
}

void roundtrip() {
    for (const auto model : models) {
        for (const bool v2 : {false, true}) {
            const auto source = literal(v2, model);
            for (sdk::int32 chunk = 1; chunk <= 20; ++chunk) {
                for (const std::size_t prefix : {0U, 1U, 7U, 31U}) {
                    roundtrip_case(source, source, model, half, true, v2 ? quarter : one,
                                   prefix, chunk);
                }
            }
        }
        constexpr std::array values{0U, 0x80000000U, 1U, 0x007fffffU, 0x00800000U,
                                    quarter, 0x3f7fffffU, one};
        for (const auto gain : values) {
            for (const auto value : values) {
                for (const auto pedal : {0U, 0x80000000U, one}) {
                    const auto source = packet(true, model, gain, pedal, value);
                    const auto wanted = packet(canonical(value) != one, model,
                        canonical(gain), pedal == one ? one : 0U, canonical(value));
                    for (const sdk::int32 chunk : {1, 7, 20}) {
                        roundtrip_case(source, wanted, model, canonical(gain), pedal == one,
                                       canonical(value), 7, chunk);
                    }
                    ++cases;
                    Stream direct_writer;
                    direct_writer.position = 7;
                    require(write_state_stream(&direct_writer, state(gain, pedal == one, value),
                                               model), "raw boundary state failed to save");
                    expect_output(direct_writer, wanted, 7, wanted.size);
                }
            }
        }
        for (const auto gain : values) {
            for (const auto pedal : {0U, 0x80000000U, one}) {
                const auto source = packet(false, model, gain, pedal, one);
                const auto wanted = packet(false, model, canonical(gain),
                                           pedal == one ? one : 0U, one);
                roundtrip_case(source, wanted, model, canonical(gain), pedal == one, one, 1, 1);
            }
        }
    }
    for (const bool v2 : {false, true}) {
        ++cases;
        Stream reader;
        const auto source = literal(v2, 1);
        reader.load(source);
        const auto decoded = read_state_stream(&reader);
        require(decoded.has_value(), "default model did not read its own packet");
        expect_state(*decoded, half, true, v2 ? quarter : one);
        Stream writer;
        writer.position = 7;
        require(write_state_stream(&writer, *decoded), "default model save failed");
        expect_output(writer, source, 7, source.size);
    }
}

void reject_packet(const Packet& source, std::uint16_t model, sdk::int32 chunk = 7) {
    ++cases;
    Stream reader;
    reader.load(source);
    reader.chunk = chunk;
    const auto original = reader.bytes;
    require(!read_state_stream(&reader, model), "malformed or foreign packet was accepted");
    const auto calls = source.size == 16 || source.size == 20 ?
        transfer_count(source.size, chunk) : 0;
    require(reader.reads == calls && reader.bytes == original && reader.writes == 0,
            "rejected packet changed input or read beyond its bounded candidate");
    require(reader.position == static_cast<sdk::int64>(7 + (calls == 0 ? 0 : source.size)),
            "rejected packet cursor differs");
}

void malformed() {
    constexpr std::array invalid{0x80000001U, 0x807fffffU, 0x80800000U, 0xbf800000U,
        0x3f800001U, 0x7f800000U, 0xff800000U, 0x7fc12345U, 0x7f812345U, 0xff812345U};
    std::size_t foreign_pairs = 0;
    for (const auto model : models) {
        for (const bool v2 : {false, true}) {
            const auto valid = literal(v2, model);
            for (std::size_t size = 0; size <= 32; ++size) {
                if (size == valid.size) { continue; }
                ++cases;
                Stream reader;
                reader.load(valid);
                reader.chunk = 7;
                reader.extent = static_cast<sdk::int64>(7 + size);
                const auto original = reader.bytes;
                require(!read_state_stream(&reader, model), "version/length mismatch was accepted");
                require(reader.reads == (size == 16 || size == 20 ? transfer_count(size, 7) : 0) &&
                        reader.bytes == original, "length rejection read count or source differs");
            }
            for (std::size_t offset = 0; offset < 8; ++offset) {
                auto bad = valid;
                bad.bytes[offset] ^= 0x80U;
                reject_packet(bad, model);
            }
            for (const auto version : {0U, 3U, 255U}) {
                auto bad = valid;
                bad.bytes[4] = static_cast<std::uint8_t>(version);
                reject_packet(bad, model);
            }
            for (const auto other : models) {
                if (other == model) { continue; }
                reject_packet(literal(v2, other), model, 1);
                ++foreign_pairs;
            }
            for (const std::uint16_t tag : {std::uint16_t{0}, std::uint16_t{4},
                                          std::uint16_t{0x0101}, std::uint16_t{0xffff}}) {
                reject_packet(packet(v2, tag, half, one, quarter), model);
            }
            for (const auto word : invalid) {
                reject_packet(packet(v2, model, word, one, quarter), model);
                if (v2) { reject_packet(packet(true, model, half, one, word), model); }
            }
            for (const auto pedal : {1U, 0x80000001U, half, 0x3f7fffffU, 0x3f800001U,
                                     0x7f800000U, 0x7fc12345U, 0x7f812345U}) {
                reject_packet(packet(v2, model, half, pedal, quarter), model);
            }
            for (int fault = 0; fault < 7; ++fault) {
                ++cases;
                Stream reader;
                reader.load(valid);
                reader.fail_tell = fault == 0;
                reader.fail_end = fault == 1;
                reader.fail_restore = fault == 2;
                reader.wrong_restore = fault == 3;
                if (fault == 4) { reader.position = -1; }
                if (fault == 5) { reader.extent = reader.position - 1; }
                if (fault == 6) { reader.extent = std::numeric_limits<sdk::int64>::max(); }
                const auto original = reader.bytes;
                require(!read_state_stream(&reader, model) && reader.reads == 0 &&
                        reader.writes == 0 && reader.bytes == original,
                        "position/extent failure transferred or accepted payload");
            }
        }
        for (const auto word : invalid) {
            for (const auto candidate : {state(word, true, one), state(word, true, quarter),
                                          state(half, true, word)}) {
                ++cases;
                Stream writer;
                writer.position = 7;
                const auto original = writer.bytes;
                require(!write_state_stream(&writer, candidate, model), "invalid output state accepted");
                require(writer.writes == 0 && writer.tells == 0 && writer.seeks == 0 &&
                        writer.bytes == original && writer.position == 7 && writer.extent == 64,
                        "state validation happened after stream mutation");
            }
        }
        require(!read_state_stream(nullptr, model) &&
                !write_state_stream(nullptr, state(half, true, one), model) &&
                !write_state_stream(nullptr, state(half, true, quarter), model),
                "null stream was accepted");
    }
    require(foreign_pairs == 12, "both versions did not cover all six ordered foreign-model pairs");
    for (const std::uint16_t model : {std::uint16_t{0}, std::uint16_t{4}, std::uint16_t{0xffff}}) {
        for (const bool v2 : {false, true}) {
            ++cases;
            Stream stream;
            stream.load(literal(v2, 1));
            const auto original = stream.bytes;
            require(!read_state_stream(&stream, model) &&
                    !write_state_stream(&stream, state(half, true, v2 ? quarter : one), model),
                    "unsupported caller model was accepted");
            require(stream.reads == 0 && stream.writes == 0 && stream.tells == 0 &&
                    stream.seeks == 0 && stream.position == 7 && stream.bytes == original,
                    "unsupported caller model touched host storage");
        }
    }
    for (const auto model : {std::uint16_t{2}, std::uint16_t{3}}) {
        for (const bool v2 : {false, true}) {
            ++cases;
            Stream reader;
            reader.load(literal(v2, model));
            require(!read_state_stream(&reader), "default reader accepted a foreign model");
        }
    }
}

void transfers() {
    for (const auto model : models) {
        for (const bool v2 : {false, true}) {
            const auto wanted = literal(v2, model);
            const auto value = state(half, true, v2 ? quarter : one);
            for (sdk::int32 chunk = 1; chunk <= 20; ++chunk) {
                const auto total = transfer_count(wanted.size, chunk);
                for (std::size_t fail_at = 1; fail_at <= total; ++fail_at) {
                    for (const auto fault : faults) {
                        ++cases;
                        const auto completed = (fail_at - 1) * static_cast<std::size_t>(chunk);
                        Stream reader;
                        reader.load(wanted);
                        reader.chunk = chunk;
                        reader.fault = fault;
                        reader.fault_at = fail_at;
                        const auto original = reader.bytes;
                        require(!read_state_stream(&reader, model), "faulted read succeeded");
                        require(reader.reads == fail_at && reader.position ==
                                    static_cast<sdk::int64>(7 + completed) &&
                                reader.bytes == original && reader.writes == 0,
                                "faulted read retried, advanced, or changed caller bytes");
                        expect_requests(reader, reader.reads, wanted.size);

                        Stream writer;
                        writer.position = 7;
                        writer.chunk = chunk;
                        writer.fault = fault;
                        writer.fault_at = fail_at;
                        require(!write_state_stream(&writer, value, model), "faulted write succeeded");
                        require(writer.writes == fail_at, "faulted write retried");
                        expect_requests(writer, writer.writes, wanted.size);
                        expect_output(writer, wanted, 7, completed);

                        // The host retries from the original position. A failed
                        // partial write promises neither truncation nor rollback.
                        writer.position = 7;
                        writer.fault = Fault::none;
                        writer.writes = 0;
                        require(write_state_stream(&writer, value, model), "partial-write retry failed");
                        expect_output(writer, wanted, 7, wanted.size);
                        require(writer.writes == total, "retry transfer count differs");
                        reader.position = 7;
                        reader.fault = Fault::none;
                        reader.reads = 0;
                        const auto decoded = read_state_stream(&reader, model);
                        require(decoded.has_value() && reader.reads == total,
                                "read retry failed or changed transfer count");
                        expect_state(*decoded, half, true, v2 ? quarter : one);
                    }
                }
            }
        }
    }
}

void overwrite() {
    for (const auto model : models) {
        for (const sdk::int32 chunk : {1, 7, 20}) {
            ++cases;
            Stream stream;
            const auto old_packet = literal(true, model);
            const auto unity_packet = literal(false, model);
            stream.load(old_packet);
            stream.chunk = chunk;
            require(write_state_stream(&stream, state(half, true, one), model),
                    "unity overwrite failed");
            require(stream.extent == 27 && stream.position == 23 && stream.tells == 0 &&
                    stream.seeks == 0, "unity overwrite changed host extent or seek policy");
            require(std::equal(unity_packet.bytes.begin(), unity_packet.bytes.begin() + 16,
                               stream.bytes.begin() + 7), "unity overwrite changed legacy bytes");
            require(std::equal(old_packet.bytes.begin() + 16, old_packet.bytes.end(),
                               stream.bytes.begin() + 23), "unity overwrite destroyed host tail");
            stream.position = 7;
            const auto after_write = stream.bytes;
            require(!read_state_stream(&stream, model) && stream.reads == transfer_count(20, chunk) &&
                    stream.bytes == after_write, "untruncated v1-over-v2 stream was accepted");
            // Only the owning host can establish the new exact packet extent.
            stream.position = 7;
            stream.extent = 23;
            stream.reads = 0;
            const auto decoded = read_state_stream(&stream, model);
            require(decoded.has_value() && stream.reads == transfer_count(16, chunk),
                    "host-sized unity overwrite was rejected");
            expect_state(*decoded, half, true, one);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        struct Test { std::string_view name; void (*run)(); };
        constexpr std::array tests{Test{"roundtrip", roundtrip}, Test{"malformed", malformed},
                                   Test{"transfers", transfers}, Test{"overwrite", overwrite}};
        bool found = false;
        for (const auto& test : tests) {
            if (selected != "all" && selected != test.name) { continue; }
            found = true;
            checks = cases = 0;
            test.run();
            std::cout << "PASS " << test.name << " models=3 cases=" << cases
                      << " checks=" << checks << '\n';
        }
        require(found, "unknown expression-stream selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << " case=" << cases << ": " << error.what() << '\n';
        return 1;
    }
}
