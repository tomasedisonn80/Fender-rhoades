#include "rhodes/audio_engine.hpp"
#include "rhodes/control_value.hpp"
#include "rhodes/event_processor.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::size_t maximum_frames = 4096;
constexpr std::size_t maximum_events = 4096;
constexpr std::size_t maximum_line_bytes = 128; // Excludes the newline byte.
constexpr std::size_t processing_block_frames = 64;

struct Input {
    double sample_rate = 0.0;
    std::size_t frames = 0;
    std::array<rhodes::EngineEvent, maximum_events> events{};
    std::size_t event_count = 0;
};

template<class Number>
Number number(std::string_view token) {
    Number value{};
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (token.empty() || result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
        throw std::runtime_error("Invalid numeric syntax or numeric overflow/underflow");
    }
    return value;
}

bool space(char value) noexcept { return value == ' ' || value == '\t' || value == '\r'; }

std::array<std::string_view, 4> fields(std::string_view line) {
    std::array<std::string_view, 4> result{};
    std::size_t cursor = 0;
    for (auto& field : result) {
        while (cursor < line.size() && space(line[cursor])) { ++cursor; }
        const auto start = cursor;
        while (cursor < line.size() && !space(line[cursor])) { ++cursor; }
        if (start == cursor) { throw std::runtime_error("Every row requires offset type key value"); }
        field = line.substr(start, cursor - start);
    }
    while (cursor < line.size() && space(line[cursor])) { ++cursor; }
    if (cursor != line.size()) { throw std::runtime_error("Unexpected extra field in event row"); }
    return result;
}

rhodes::EngineEvent event(std::string_view line, const Input& input) {
    const auto token = fields(line);
    rhodes::EngineEvent result;
    result.offset = number<std::uint32_t>(token[0]);
    result.sequence = static_cast<std::uint32_t>(input.event_count);
    result.key = number<std::size_t>(token[2]);
    result.value = number<double>(token[3]);
    if (result.offset >= input.frames || !std::isfinite(result.value) ||
            (input.event_count != 0 && result.offset < input.events[input.event_count - 1].offset)) {
        throw std::runtime_error("Event offset, ordering, or finite value is invalid");
    }
    if (token[1] == "on") {
        result.type = rhodes::EventType::note_on;
        if (result.key >= rhodes::key_count || !rhodes::control::valid_normalized(result.value)) {
            throw std::runtime_error("on requires key 0..72 and value 0..1");
        }
    } else if (token[1] == "off") {
        result.type = rhodes::EventType::note_off;
        if (result.key >= rhodes::key_count || !rhodes::control::is_zero(result.value)) {
            throw std::runtime_error("off requires key 0..72 and value 0");
        }
    } else {
        if (result.key != 0) { throw std::runtime_error("Global events require key 0"); }
        if (token[1] == "sustain") {
            result.type = rhodes::EventType::sustain;
            if (!rhodes::control::is_boolean(result.value)) {
                throw std::runtime_error("sustain requires value 0 or 1");
            }
        } else if (token[1] == "all_off" || token[1] == "reset") {
            result.type = token[1] == "all_off" ? rhodes::EventType::all_notes_off :
                                                 rhodes::EventType::reset;
            if (!rhodes::control::is_zero(result.value)) {
                throw std::runtime_error("all_off/reset require value 0");
            }
        } else {
            throw std::runtime_error("Unknown event type");
        }
    }
    return result;
}

void read_events(Input& input) {
    // Do not use an unbounded getline allocation. The first over-limit byte or
    // row rejects immediately; at most 4096 * (128 + 1) input bytes are accepted.
    std::array<char, maximum_line_bytes> line{};
    std::size_t length = 0;
    for (;;) {
        const auto byte = std::cin.get();
        if (byte == std::char_traits<char>::eof()) {
            if (std::cin.bad() || !std::cin.eof()) { throw std::runtime_error("Cannot read stdin"); }
            if (length != 0) {
                const auto parsed = event({line.data(), length}, input);
                input.events[input.event_count++] = parsed;
            }
            return;
        }
        if (input.event_count == maximum_events) { throw std::runtime_error("More than 4096 event rows"); }
        if (byte == '\n') {
            // Calculate before incrementing: the count is also producer sequence.
            const auto parsed = event({line.data(), length}, input);
            input.events[input.event_count++] = parsed;
            length = 0;
        } else {
            if (length == maximum_line_bytes) { throw std::runtime_error("Event row exceeds 128 bytes"); }
            line[length++] = static_cast<char>(byte);
        }
    }
}

Input preflight(int argc, char** argv) {
    if (argc != 5) { throw std::runtime_error("Require --sample-rate Hz --frames N; use --help"); }
    Input input;
    bool have_rate = false;
    bool have_frames = false;
    for (int index = 1; index < argc; index += 2) {
        const std::string_view option{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (value.size() > maximum_line_bytes) { throw std::runtime_error("Argument value exceeds 128 bytes"); }
        if (option == "--sample-rate" && !have_rate) {
            input.sample_rate = number<double>(value);
            have_rate = true;
        } else if (option == "--frames" && !have_frames) {
            input.frames = number<std::size_t>(value);
            have_frames = true;
        } else {
            throw std::runtime_error("Unknown or repeated option");
        }
    }
    if (!have_rate || !have_frames || !std::isfinite(input.sample_rate) ||
            input.sample_rate < rhodes::minimum_sample_rate_hz ||
            input.sample_rate > rhodes::maximum_sample_rate_hz ||
            input.frames == 0 || input.frames > maximum_frames) {
        throw std::runtime_error("Require finite sample rate 32000..192000 Hz and 1..4096 frames");
    }
    read_events(input);
    return input;
}

bool apply(rhodes::AudioEngine& engine, const rhodes::EngineEvent& value) noexcept {
    switch (value.type) {
    case rhodes::EventType::note_on: return engine.note_on(value.key, value.value);
    case rhodes::EventType::note_off: return engine.note_off(value.key);
    case rhodes::EventType::sustain: engine.set_sustain(!rhodes::control::is_zero(value.value)); return true;
    case rhodes::EventType::all_notes_off: engine.all_notes_off(); return true;
    case rhodes::EventType::reset: engine.reset(); return true;
    }
    return false;
}

void run(const Input& input) {
    // No engine exists until every argument and input row has passed preflight.
    rhodes::AudioEngine engine;
    if (!engine.prepare(input.sample_rate)) { throw std::runtime_error("AudioEngine prepare failed"); }
    std::array<double, maximum_frames> samples{};
    std::size_t next_event = 0;
    for (std::size_t block = 0; block < input.frames; block += processing_block_frames) {
        const auto end = std::min(block + processing_block_frames, input.frames);
        for (std::size_t frame = block; frame < end; ++frame) {
            // Direct physical dispatch retains all producer ordering, including
            // more than 256 same-offset rows, without a host ownership adapter.
            while (next_event < input.event_count && input.events[next_event].offset == frame) {
                if (!apply(engine, input.events[next_event++])) {
                    throw std::runtime_error("AudioEngine event application failed");
                }
            }
            if (!engine.process(std::span<double>{samples}.subspan(frame, 1))) {
                throw std::runtime_error("AudioEngine processing failed");
            }
            if (!std::isfinite(samples[frame])) { throw std::runtime_error("Nonfinite audio sample"); }
        }
    }
    // Emit only after successful rendering, without float32 conversion,
    // monitoring gain, waveform normalization, latency removal, or file I/O.
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
              << "{\"schema_version\":1,\"model\":\"synthetic-filtered-audio\","
                 "\"scope\":\"Uncalibrated synthetic loaded-harp voltage; causal FIR; "
                 "no gain, normalization, time shift, or measured fidelity claim\","
                 "\"sample_rate_hz\":" << engine.sample_rate_hz()
              << ",\"internal_sample_rate_hz\":" << engine.internal_sample_rate_hz()
              << ",\"oversampling_factor\":" << engine.oversampling_factor()
              << ",\"latency_frames\":" << engine.latency_frames()
              << ",\"frames\":" << input.frames
              << ",\"event_count\":" << input.event_count << ",\"samples_volts\":[";
    for (std::size_t frame = 0; frame < input.frames; ++frame) {
        if (frame != 0) { std::cout << ','; }
        std::cout << samples[frame];
    }
    std::cout << "]}\n";
    if (!std::cout) { throw std::runtime_error("Cannot write stdout"); }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--help") {
            std::cout << "rhodes_audio_probe --sample-rate Hz --frames N\n"
                "Synthetic AudioEngine double-precision JSON probe; no measured fidelity claim.\n"
                "Finite host rate 32000..192000 Hz; 1..4096 frames; 48-frame causal FIR delay.\n"
                "stdin: offset type key value, exactly four fields on each nonempty row.\n"
                "Types: on (key 0..72, value 0..1; zero releases), off (key 0..72, value 0),\n"
                "sustain (key 0, value 0/1), all_off/reset (key 0, value 0).\n"
                "Ordered offsets 0..N-1; equal offsets retain row order before that sample.\n"
                "At most 4096 rows, each at most 128 bytes excluding newline; blank rows reject.\n"
                "Empty stdin is silence. Entire input preflights before engine mutation/output.\n"
                "Processing uses 64-frame blocks and bounded physical-event dispatch.\n"
                "JSON stdout contains volts with 17 significant digits; no gain or time shift.\n"
                "Invalid input exits 2 with diagnostics on stderr and empty stdout.\n";
            return std::cout ? 0 : 2;
        }
        run(preflight(argc, argv));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "rhodes_audio_probe: " << error.what() << '\n';
        return 2;
    }
}
