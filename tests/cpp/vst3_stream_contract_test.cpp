#include "tests/cpp/vst3_test_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace host = rhodes::vst3::test_host;
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Packet = std::array<std::uint8_t, 16>;
using host::MemoryStream;
using host::TransferFault;

std::size_t checks = 0;
std::size_t cases = 0;
std::size_t read_transfers = 0;
std::size_t write_transfers = 0;
std::size_t process_calls = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

// Independent wire construction: neither the codec nor the control conversion
// helpers generate expected bytes. Keep packet lifetimes named across calls.
Packet packet(std::uint32_t gain, std::uint32_t pedal) {
    Packet bytes{'R', 'H', 'S', 'T', 1, 0, 1, 0};
    for (std::size_t digit = 0; digit < 4; ++digit) {
        bytes[8 + digit] = static_cast<std::uint8_t>(gain & 255U);
        bytes[12 + digit] = static_cast<std::uint8_t>(pedal & 255U);
        gain >>= 8U;
        pedal >>= 8U;
    }
    return bytes;
}

constexpr std::uint32_t one = 0x3f800000U;
constexpr std::uint32_t pending_gain = 0x3e000000U;
constexpr std::uint32_t retry_gain = 0x000116c2U;
constexpr std::uint8_t sentinel = 0xa5U;

rhodes::InstrumentState state(std::uint32_t gain, bool sustain) {
    return {std::bit_cast<float>(gain), sustain};
}

void expect_state(rhodes::InstrumentState actual, std::uint32_t gain, bool sustain) {
    check(std::bit_cast<std::uint32_t>(actual.monitoring_gain) == gain,
          "configuration gain bits differ");
    check(actual.sustain == sustain, "configuration sustain differs");
}

void input(MemoryStream& stream, const Packet& bytes, std::size_t prefix,
           sdk::int32 chunk) {
    check(prefix + bytes.size() <= stream.bytes.size(), "invalid input fixture extent");
    stream.bytes.fill(sentinel);
    std::copy(bytes.begin(), bytes.end(), stream.bytes.begin() +
              static_cast<std::ptrdiff_t>(prefix));
    stream.position = static_cast<sdk::int64>(prefix);
    stream.extent = stream.position + 16;
    stream.transfer_limit = chunk;
}

void output(MemoryStream& stream, std::size_t prefix, sdk::int32 chunk) {
    stream.bytes.fill(sentinel);
    stream.position = static_cast<sdk::int64>(prefix);
    // The destination belongs to the host; saving must not truncate its tail.
    stream.extent = static_cast<sdk::int64>(stream.bytes.size());
    stream.transfer_limit = chunk;
    stream.fail_tell = true;
    stream.fail_seek_end = true;
    stream.fail_seek_set = true;
}

std::size_t transfers(sdk::int32 chunk) {
    return static_cast<std::size_t>((16 + chunk - 1) / chunk);
}

void expect_output(const MemoryStream& stream, const Packet& wanted,
                   std::size_t prefix, std::size_t written = 16) {
    for (std::size_t index = 0; index < stream.bytes.size(); ++index) {
        const auto expected = index >= prefix && index < prefix + written ?
            wanted[index - prefix] : sentinel;
        check(stream.bytes[index] == expected, "partial write bytes or caller guards differ");
    }
    check(stream.position == static_cast<sdk::int64>(prefix + written),
          "write position differs from completed transfers");
    check(stream.extent == static_cast<sdk::int64>(stream.bytes.size()),
          "saving truncated the host-owned stream");
}

enum class Reader { helper, processor, controller, component };
enum class Writer { helper, processor, controller };
constexpr std::array readers{Reader::helper, Reader::processor,
                             Reader::controller, Reader::component};
constexpr std::array writers{Writer::helper, Writer::processor, Writer::controller};
constexpr std::array faults{TransferFault::result_error, TransferFault::zero_progress,
                            TransferFault::negative_count, TransferFault::overreported_count};

struct Rig {
    // Large processor scratch is never placed on the platform's default stack.
    std::unique_ptr<host::ProcessorHost<double, 8>> audio =
        std::make_unique<host::ProcessorHost<double, 8>>();
    host::ControllerHost editor;
};

void flush(Rig& rig) {
    vst::ProcessData data{};
    data.symbolicSampleSize = vst::kSample64;
    ++process_calls;
    check(rig.audio->processor.process(data) == sdk::kResultOk, "state-boundary flush failed");
}

bool read(Reader reader, MemoryStream* stream, Rig& rig,
          std::optional<rhodes::InstrumentState>& decoded) {
    bool success = false;
    if (reader == Reader::helper) {
        decoded = rhodes::vst3::read_state_stream(stream);
        success = decoded.has_value();
    } else if (reader == Reader::processor) {
        success = rig.audio->processor.setState(stream) == sdk::kResultOk;
    } else if (reader == Reader::controller) {
        success = rig.editor.controller.setState(stream) == sdk::kResultOk;
    } else {
        success = rig.editor.controller.setComponentState(stream) == sdk::kResultOk;
    }
    if (stream != nullptr) { read_transfers += stream->read_calls; }
    return success;
}

bool write(Writer writer, MemoryStream* stream, Rig& rig,
           rhodes::InstrumentState value) {
    bool success = false;
    if (writer == Writer::helper) {
        success = rhodes::vst3::write_state_stream(stream, value);
    } else if (writer == Writer::processor) {
        success = rig.audio->processor.getState(stream) == sdk::kResultOk;
    } else {
        success = rig.editor.controller.getState(stream) == sdk::kResultOk;
    }
    if (stream != nullptr) { write_transfers += stream->write_calls; }
    return success;
}

void expect_saved(Rig& rig, Writer writer, std::uint32_t gain, bool sustain) {
    MemoryStream stream;
    output(stream, 5, 16);
    check(write(writer, &stream, rig, state(gain, sustain)), "configuration save failed");
    const auto wanted = packet(gain, sustain ? one : 0U);
    expect_output(stream, wanted, 5);
    check(stream.write_calls == 1, "full save used an unexpected transfer count");
}

void prepare_pending(Rig& rig) {
    const auto baseline = packet(0x3f000000U, 0U);
    MemoryStream initial;
    input(initial, baseline, 3, 16);
    check(rig.audio->processor.setState(&initial) == sdk::kResultOk, "baseline recall failed");
    flush(rig);
    host::EventList notes;
    notes.count = 1;
    notes.values[0] = host::note_on(0, 17);
    rig.audio->data.inputEvents = &notes;
    ++process_calls;
    check(rig.audio->processor.process(rig.audio->data) == sdk::kResultOk,
          "ringing baseline failed");
    rig.audio->data.inputEvents = nullptr;
    check(rig.audio->processor.note_tracker().active_count() == 1 &&
          rig.audio->processor.engine_processor().engine().mechanical_energy_joules() > 0.0,
          "rejection fixture was not ringing");
    const auto pending = packet(pending_gain, one);
    MemoryStream processor_input;
    input(processor_input, pending, 7, 16);
    check(rig.audio->processor.setState(&processor_input) == sdk::kResultOk,
          "pending recall failed");
    MemoryStream controller_input;
    input(controller_input, pending, 7, 16);
    check(rig.editor.controller.setState(&controller_input) == sdk::kResultOk,
          "controller baseline failed");
    expect_state(rig.audio->processor.current_state(), 0x3f000000U, false);
    expect_state(rig.audio->processor.published_state(), pending_gain, true);
}

void consume_preserved_pending(Rig& rig) {
    flush(rig);
    expect_state(rig.audio->processor.current_state(), pending_gain, true);
    expect_state(rig.audio->processor.published_state(), pending_gain, true);
    check(rig.audio->processor.engine_processor().frame_cursor() == 0 &&
          rig.audio->processor.note_tracker().active_count() == 0 &&
          rig.audio->processor.engine_processor().engine().mechanical_energy_joules() == 0.0,
          "failed state I/O lost the pending reset");
}

void verify_read_success(Reader reader, MemoryStream& stream, Rig& rig,
                         std::uint32_t gain, bool sustain, sdk::int32 chunk,
                         std::size_t prefix) {
    const auto before_bytes = stream.bytes;
    const auto before_live = rig.audio->processor.current_state();
    std::optional<rhodes::InstrumentState> decoded;
    check(read(reader, &stream, rig, decoded), "valid fragmented recall failed");
    check(stream.bytes == before_bytes, "state reader modified caller storage");
    check(stream.read_calls == transfers(chunk) &&
          stream.position == static_cast<sdk::int64>(prefix + 16),
          "successful read transfer count or final position differs");
    if (reader == Reader::helper) {
        check(decoded.has_value(), "helper did not return decoded state");
        expect_state(*decoded, gain, sustain);
    } else if (reader == Reader::processor) {
        expect_state(rig.audio->processor.current_state(),
                     std::bit_cast<std::uint32_t>(before_live.monitoring_gain), before_live.sustain);
        expect_saved(rig, Writer::processor, gain, sustain);
        flush(rig);
        expect_state(rig.audio->processor.current_state(), gain, sustain);
    } else {
        expect_saved(rig, Writer::controller, gain, sustain);
    }
}

void reject_read(Rig& rig, Reader reader, MemoryStream* stream,
                 std::size_t expected_calls, sdk::int32 retry_chunk = 7) {
    ++cases;
    prepare_pending(rig);
    const auto snapshot = std::make_unique<host::ProcessorSnapshot>(rig.audio->processor);
    const auto before_bytes = stream != nullptr ? stream->bytes : std::array<std::uint8_t, 64>{};
    std::optional<rhodes::InstrumentState> decoded;
    check(!read(reader, stream, rig, decoded), "invalid state stream was accepted");
    if (stream != nullptr) {
        check(stream->read_calls == expected_calls && stream->read_calls <= 16,
              "failed read retried or exceeded its transfer bound");
        check(stream->bytes == before_bytes, "failed reader modified source bytes");
    }
    snapshot->expect_unchanged(rig.audio->processor);
    expect_saved(rig, Writer::processor, pending_gain, true);
    expect_saved(rig, Writer::controller, pending_gain, true);
    consume_preserved_pending(rig);
    MemoryStream retry;
    const auto wanted = packet(retry_gain, 0U);
    input(retry, wanted, 9, retry_chunk);
    verify_read_success(reader, retry, rig, retry_gain, false, retry_chunk, 9);
}

void partial_roundtrip() {
    Rig rig;
    struct Value { std::uint32_t gain; std::uint32_t pedal; std::uint32_t canonical; };
    constexpr std::array values{
        Value{0x80000000U, 0x80000000U, 0U}, Value{0U, one, 0U},
        Value{1U, 0U, 1U}, Value{0x007fffffU, one, 0x007fffffU},
        Value{0x00800000U, 0U, 0x00800000U},
        Value{0x3e000000U, one, 0x3e000000U}, Value{one, 0U, one}};
    for (sdk::int32 chunk = 1; chunk <= 16; ++chunk) {
        for (const std::size_t prefix : {1U, 7U, 31U}) {
            for (const auto value : values) {
                ++cases;
                const auto source = packet(value.gain, value.pedal);
                const bool sustain = value.pedal == one;
                for (const auto reader : readers) {
                    MemoryStream stream;
                    input(stream, source, prefix, chunk);
                    verify_read_success(reader, stream, rig, value.canonical,
                                        sustain, chunk, prefix);
                }
                const auto wanted = packet(value.canonical, sustain ? one : 0U);
                for (const auto writer : writers) {
                    MemoryStream stream;
                    output(stream, prefix, chunk);
                    check(write(writer, &stream, rig, state(value.gain, sustain)),
                          "valid fragmented save failed");
                    expect_output(stream, wanted, prefix);
                    check(stream.write_calls == transfers(chunk),
                          "fragmented save exceeded its exact call bound");
                }
            }
        }
    }
}

void read_failures() {
    Rig rig;
    const auto incoming = packet(retry_gain, 0U);
    for (sdk::int32 chunk = 1; chunk <= 16; ++chunk) {
        for (std::size_t fail_at = 1; fail_at <= transfers(chunk); ++fail_at) {
            for (const auto fault : faults) {
                for (const auto reader : readers) {
                    MemoryStream stream;
                    input(stream, incoming, 5, chunk);
                    stream.read_fault = fault;
                    stream.read_fault_at = fail_at;
                    reject_read(rig, reader, &stream, fail_at, chunk);
                }
            }
        }
    }
}

void write_failures() {
    Rig rig;
    const auto wanted = packet(pending_gain, one);
    for (sdk::int32 chunk = 1; chunk <= 16; ++chunk) {
        for (std::size_t fail_at = 1; fail_at <= transfers(chunk); ++fail_at) {
            for (const auto fault : faults) {
                for (const auto writer : writers) {
                    ++cases;
                    prepare_pending(rig);
                    const auto snapshot = std::make_unique<host::ProcessorSnapshot>(
                        rig.audio->processor);
                    MemoryStream stream;
                    output(stream, 5, chunk);
                    stream.write_fault = fault;
                    stream.write_fault_at = fail_at;
                    check(!write(writer, &stream, rig, state(pending_gain, true)),
                          "faulted save was accepted");
                    check(stream.write_calls == fail_at && stream.write_calls <= 16,
                          "faulted save retried or exceeded its bound");
                    const auto completed = (fail_at - 1) * static_cast<std::size_t>(chunk);
                    expect_output(stream, wanted, 5, completed);
                    snapshot->expect_unchanged(rig.audio->processor);
                    expect_saved(rig, Writer::controller, pending_gain, true);
                    // Retrying from the original position overwrites any valid
                    // prefix; the API never promises rollback of host storage.
                    stream.position = 5;
                    stream.write_fault = TransferFault::none;
                    stream.write_calls = 0;
                    check(write(writer, &stream, rig, state(pending_gain, true)),
                          "retry after a partial save failed");
                    expect_output(stream, wanted, 5);
                    check(stream.write_calls == transfers(chunk), "retry transfer count differs");
                    snapshot->expect_unchanged(rig.audio->processor);
                    consume_preserved_pending(rig);
                }
            }
        }
    }
    for (const auto writer : writers) {
        ++cases;
        prepare_pending(rig);
        const auto snapshot = std::make_unique<host::ProcessorSnapshot>(rig.audio->processor);
        check(!write(writer, nullptr, rig, state(pending_gain, true)), "null save accepted");
        snapshot->expect_unchanged(rig.audio->processor);
        expect_saved(rig, Writer::controller, pending_gain, true);
        consume_preserved_pending(rig);
    }
    constexpr std::array invalid{0x80000001U, 0x80800000U, 0xbf800000U, 0x3f800001U,
                                  0x7f800000U, 0xff800000U, 0x7fc12345U, 0x7f812345U};
    for (const auto gain : invalid) {
        ++cases;
        MemoryStream stream;
        output(stream, 5, 1);
        const auto before = stream.bytes;
        check(!write(Writer::helper, &stream, rig, state(gain, false)),
              "invalid gain reached state output");
        check(stream.write_calls == 0 && stream.position == 5 && stream.bytes == before,
              "invalid state was written before validation");
    }
}

void malformed_state() {
    Rig rig;
    const auto valid = packet(retry_gain, 0U);
    for (const std::size_t prefix : {0U, 5U, 31U}) {
        for (sdk::int64 remaining = 0; remaining <= 32; ++remaining) {
            if (remaining == 16) { continue; }
            for (const auto reader : readers) {
                MemoryStream stream;
                input(stream, valid, prefix, 7);
                stream.extent = static_cast<sdk::int64>(prefix) + remaining;
                reject_read(rig, reader, &stream, 0);
            }
        }
        for (int fault = 0; fault < 6; ++fault) {
            for (const auto reader : readers) {
                MemoryStream stream;
                input(stream, valid, prefix, 7);
                if (fault == 0) { stream.fail_tell = true; }
                if (fault == 1) { stream.fail_seek_end = true; }
                if (fault == 2) { stream.fail_seek_set = true; }
                if (fault == 3) { stream.wrong_restored_position = true; }
                if (fault == 4) { stream.position = -1; }
                if (fault == 5) { stream.extent = stream.position - 1; }
                reject_read(rig, reader, &stream, 0);
            }
        }
    }
    constexpr std::array invalid_gain{0x80000001U, 0x80800000U, 0xbf800000U, 0x3f800001U,
                                       0x7f800000U, 0xff800000U, 0x7fc12345U, 0x7f812345U};
    constexpr std::array invalid_pedal{1U, 0x80000001U, 0x3f000000U, 0x3f7fffffU,
                                        0x3f800001U, 0x7f800000U, 0x7fc12345U, 0x7f812345U};
    std::array<Packet, 24> malformed{};
    for (std::size_t index = 0; index < 8; ++index) {
        malformed[index] = valid;
        malformed[index][index] ^= 0x80U;
        malformed[index + 8] = packet(invalid_gain[index], 0U);
        malformed[index + 16] = packet(retry_gain, invalid_pedal[index]);
    }
    for (const auto& bytes : malformed) {
        for (const sdk::int32 chunk : {1, 7, 16}) {
            for (const auto reader : readers) {
                MemoryStream stream;
                input(stream, bytes, 5, chunk);
                reject_read(rig, reader, &stream, transfers(chunk), chunk);
            }
        }
    }
    for (const auto reader : readers) { reject_read(rig, reader, nullptr, 0); }
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        struct Test { std::string_view name; void (*run)(); };
        constexpr std::array tests{
            Test{"partial_roundtrip", partial_roundtrip}, Test{"read_failures", read_failures},
            Test{"write_failures", write_failures}, Test{"malformed_state", malformed_state}};
        bool found = false;
        for (const auto& test : tests) {
            if (selected != "all" && selected != test.name) { continue; }
            found = true;
            checks = 0; cases = 0; read_transfers = 0; write_transfers = 0; process_calls = 0;
            test.run();
            std::cout << "PASS " << test.name << " cases=" << cases << " checks=" << checks
                      << " checked_read_transfers=" << read_transfers
                      << " checked_write_transfers=" << write_transfers
                      << " process_calls=" << process_calls << '\n';
        }
        check(found, "unknown state-stream contract selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << " case=" << cases << ": " << error.what() << '\n';
        return 1;
    }
}
