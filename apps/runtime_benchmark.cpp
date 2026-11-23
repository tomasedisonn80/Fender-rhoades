#include "rhodes/event_processor.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <locale>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef RHODES_BUILD_CONFIGURATION
#define RHODES_BUILD_CONFIGURATION "unspecified"
#endif
#ifndef RHODES_SOURCE_REVISION
#define RHODES_SOURCE_REVISION "unrecorded"
#endif
#ifndef RHODES_SOURCE_DIFF_HASH
#define RHODES_SOURCE_DIFF_HASH "unrecorded"
#endif
#ifndef RHODES_COMPLETE_BUILD_FLAGS
#define RHODES_COMPLETE_BUILD_FLAGS "unrecorded"
#endif

namespace {
using Clock = std::chrono::steady_clock;
constexpr double intensity = 0.7;

struct Options {
    std::size_t blocks = 1000;
    std::size_t frames = 128;
    std::size_t warmup = 32;
    std::uint32_t rate = 48000;
    std::size_t keys = rhodes::key_count;
    std::string workload = "chord";
    std::string offsets = "distributed";
    std::string machine = "unknown";
};

struct Events {
    std::array<rhodes::EngineEvent, rhodes::maximum_block_events> data{};
    std::size_t count = 0;
    void add(rhodes::EventType type, std::size_t key, double value,
             std::uint32_t offset = 0) {
        if (count == data.size()) { throw std::runtime_error("Internal event capacity exceeded"); }
        data[count] = {offset, static_cast<std::uint32_t>(count), type, key, value};
        ++count;
    }
    std::span<const rhodes::EngineEvent> span() const { return {data.data(), count}; }
};

void usage() {
    std::cout <<
        "rhodes_runtime_benchmark: synthetic EventProcessor full-call observations\n"
        "Usage: rhodes_runtime_benchmark [--blocks 1..1000000] [--block-size 1..4096]\n"
        "  [--sample-rate 32000..192000] [--active-keys 0..73] [--warmup-blocks 0..10000]\n"
        "  [--workload silence|chord|retrigger256|pedal256|tail-held|tail-sustained|tail-release]\n"
        "  [--offsets distributed|same] [--machine PRINTABLE_ASCII_LABEL]\n"
        "Defaults: 1000 blocks, 128 frames, 48000 Hz, 73 keys, 32 warmup blocks, chord.\n"
        "Prints JSON including every raw callback timing. Events, validation and rendering\n"
        "are timed together. Schedule generation, initial conditions, output checks and JSON\n"
        "are excluded. No host/device scheduling, allocation or realtime guarantee.\n";
}

template<class T> T number(std::string_view text, std::string_view flag) {
    T value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error("Invalid value for " + std::string(flag));
    }
    return value;
}

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag{argv[i]};
        if (flag != "--blocks" && flag != "--block-size" && flag != "--sample-rate" &&
            flag != "--active-keys" && flag != "--warmup-blocks" && flag != "--workload" &&
            flag != "--offsets" && flag != "--machine") {
            throw std::runtime_error("Unknown option: " + std::string(flag));
        }
        if (++i == argc) { throw std::runtime_error("Missing value for " + std::string(flag)); }
        const std::string_view value{argv[i]};
        if (flag == "--blocks") { options.blocks = number<std::size_t>(value, flag); }
        else if (flag == "--block-size") { options.frames = number<std::size_t>(value, flag); }
        else if (flag == "--sample-rate") { options.rate = number<std::uint32_t>(value, flag); }
        else if (flag == "--active-keys") { options.keys = number<std::size_t>(value, flag); }
        else if (flag == "--warmup-blocks") { options.warmup = number<std::size_t>(value, flag); }
        else if (flag == "--workload") { options.workload = value; }
        else if (flag == "--offsets") { options.offsets = value; }
        else { options.machine = value; }
    }
    constexpr std::array<std::string_view, 7> workloads{
        "silence", "chord", "retrigger256", "pedal256", "tail-held", "tail-sustained", "tail-release"};
    if (options.blocks == 0 || options.blocks > 1000000 || options.frames == 0 ||
        options.frames > rhodes::maximum_block_frames || options.warmup > 10000 ||
        options.rate < rhodes::minimum_sample_rate_hz || options.rate > rhodes::maximum_sample_rate_hz ||
        options.keys > rhodes::key_count || options.machine.empty() || options.machine.size() > 128 ||
        std::any_of(options.machine.begin(), options.machine.end(), [](char c) {
            return static_cast<unsigned char>(c) < 0x20U || static_cast<unsigned char>(c) > 0x7eU;
        }) || std::find(workloads.begin(), workloads.end(), options.workload) == workloads.end() ||
        (options.offsets != "same" && options.offsets != "distributed") ||
        (options.workload == "retrigger256" && options.keys == 0)) {
        throw std::runtime_error("Arguments outside documented ranges; see --help");
    }
    return options;
}

void string(std::string_view value) {
    std::cout << '"';
    for (const char c : value) {
        if (c == '"' || c == '\\') { std::cout << '\\'; }
        if (static_cast<unsigned char>(c) < 0x20U) { std::cout << '?'; }
        else { std::cout << c; }
    }
    std::cout << '"';
}

std::size_t key_at(const Options& options, std::size_t index) {
    return options.keys == 1 ? rhodes::key_count / 2 :
        index * (rhodes::key_count - 1) / (options.keys - 1);
}

std::size_t retrigger_interval(const Options& options) {
    return (static_cast<std::size_t>(options.rate) + 2 * options.frames - 1) / (2 * options.frames);
}

Events schedule(const Options& options, std::size_t block) {
    Events events;
    if (options.workload == "chord" && block % retrigger_interval(options) == 0) {
        for (std::size_t i = 0; i < options.keys; ++i) {
            events.add(rhodes::EventType::note_on, key_at(options, i), intensity);
        }
    } else if (options.workload == "retrigger256" || options.workload == "pedal256") {
        for (std::size_t i = 0; i < rhodes::maximum_block_events; ++i) {
            const auto offset = static_cast<std::uint32_t>(options.offsets == "same" ? 0 :
                i * options.frames / rhodes::maximum_block_events);
            if (options.workload == "pedal256") {
                events.add(rhodes::EventType::sustain, 0, static_cast<double>(i % 2), offset);
            } else {
                const auto key = key_at(options, (block * 128 + i / 2) % options.keys);
                events.add(i % 2 == 0 ? rhodes::EventType::note_off : rhodes::EventType::note_on,
                           key, i % 2 == 0 ? 0.0 : intensity, offset);
            }
        }
    }
    return events;
}

Events initial_conditions(const Options& options) {
    Events events;
    const bool tail = options.workload.starts_with("tail-");
    const bool pedal = options.workload == "pedal256" || options.workload == "tail-sustained";
    if (!tail && !pedal) { return events; }
    if (pedal) { events.add(rhodes::EventType::sustain, 0, 1.0); }
    for (std::size_t i = 0; i < options.keys; ++i) {
        events.add(rhodes::EventType::note_on, key_at(options, i), intensity);
    }
    if (options.workload != "tail-held") {
        for (std::size_t i = 0; i < options.keys; ++i) {
            events.add(rhodes::EventType::note_off, key_at(options, i), 0.0);
        }
    }
    return events;
}

// A portable noncryptographic schedule fingerprint; canonical little-endian u64 fields.
void hash_word(std::uint64_t& hash, std::uint64_t word) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        hash ^= (word >> shift) & 0xffU;
        hash *= 1099511628211ULL;
    }
}
void hash_events(std::uint64_t& hash, const Events& events) {
    hash_word(hash, events.count);
    for (const auto& event : events.span()) {
        hash_word(hash, event.offset);
        hash_word(hash, event.sequence);
        hash_word(hash, static_cast<std::uint64_t>(event.type));
        hash_word(hash, event.key);
        hash_word(hash, std::bit_cast<std::uint64_t>(event.value));
    }
}

double percentile(const std::vector<double>& sorted, std::size_t percent) {
    return sorted[(sorted.size() * percent + 99) / 100 - 1];
}

void summary(std::span<const double> values, double deadline) {
    if (values.empty()) { std::cout << "null"; return; }
    std::vector<double> sorted(values.begin(), values.end());
    double sum = 0.0;
    std::size_t misses = 0;
    for (const double value : sorted) { sum += value; misses += value > deadline ? 1U : 0U; }
    std::sort(sorted.begin(), sorted.end());
    std::cout << "{\"observations\":" << sorted.size() << ",\"mean_us\":"
        << sum / static_cast<double>(sorted.size()) << ",\"p50_us\":" << percentile(sorted, 50)
        << ",\"p95_us\":" << percentile(sorted, 95) << ",\"p99_us\":" << percentile(sorted, 99)
        << ",\"max_us\":" << sorted.back() << ",\"deadline_us\":" << deadline
        << ",\"deadline_misses\":" << misses << '}';
}

std::string_view compiler() {
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(_MSC_VER)
    return "MSVC";
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown";
#endif
}
std::string_view platform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "Apple";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}
std::string_view architecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#else
    return "unknown";
#endif
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") { usage(); return 0; }
        const Options options = parse(argc, argv);
        std::vector<double> output(options.frames);
        std::vector<double> timings(options.blocks);
        const Events initial = initial_conditions(options);
        rhodes::EventProcessor processor;
        if (!processor.prepare(static_cast<double>(options.rate))) {
            throw std::runtime_error("Processor rejected sample rate");
        }
        const auto initialize = [&]() {
            processor.reset();
            if (initial.count == 0) { return true; }
            std::array<double, 1> discarded{};
            return processor.process(discarded, initial.span()).succeeded();
        };
        bool initial_ok = initialize();
        std::size_t warmup_completed = 0;
        std::size_t warmup_attempted = 0;
        bool warmup_ok = initial_ok;
        for (; warmup_ok && warmup_completed < options.warmup;) {
            const auto events = schedule(options, warmup_completed);
            ++warmup_attempted;
            warmup_ok = processor.process(output, events.span()).succeeded();
            if (warmup_ok) { ++warmup_completed; }
        }
        const bool measurement_reset = warmup_ok;
        if (measurement_reset) { initial_ok = initialize(); }
        const double deadline = 1.0e6 * static_cast<double>(options.frames) / options.rate;
        std::array<std::size_t, 4> statuses{};
        std::array<std::uint64_t, 5> event_types{};
        std::uint64_t schedule_hash = 14695981039346656037ULL;
        hash_word(schedule_hash, options.rate);
        hash_word(schedule_hash, options.frames);
        hash_events(schedule_hash, initial);
        std::size_t measured = 0;
        std::uint64_t processed_frames = 0;
        bool finite = true;
        double checksum = 0.0;
        double peak = 0.0;
        std::uint64_t subnormal_outputs = 0;
        std::uint64_t subnormal_states = 0;
        rhodes::ProcessResult last_result{};
        for (std::size_t block = 0; initial_ok && warmup_ok && block < options.blocks; ++block) {
            const auto events = schedule(options, block);
            hash_events(schedule_hash, events);
            for (const auto& event : events.span()) { ++event_types[static_cast<std::size_t>(event.type)]; }
            const auto start = Clock::now();
            const auto result = processor.process(output, events.span());
            const auto finish = Clock::now();
            timings[measured++] = std::chrono::duration<double, std::micro>(finish - start).count();
            ++statuses[static_cast<std::size_t>(result.status)];
            processed_frames += result.frames_processed;
            last_result = result;
            // All observation, hashing, scheduling and serialization are outside the timer.
            for (const double sample : output) {
                finite = finite && std::isfinite(sample);
                if (std::isfinite(sample)) {
                    checksum += sample * sample;
                    peak = std::max(peak, std::abs(sample));
                    subnormal_outputs += std::fpclassify(sample) == FP_SUBNORMAL ? 1U : 0U;
                }
            }
            for (std::size_t key = 0; key < rhodes::key_count; ++key) {
                for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
                    const auto state = processor.engine().modal_state(key, mode);
                    if (state) {
                        subnormal_states += std::fpclassify(state->displacement_m) == FP_SUBNORMAL ? 1U : 0U;
                        subnormal_states += std::fpclassify(state->velocity_m_per_s) == FP_SUBNORMAL ? 1U : 0U;
                    }
                }
            }
            finite = finite && std::isfinite(checksum);
            if (!result.succeeded() || !finite) { break; }
        }
        timings.resize(measured);
        const bool complete = initial_ok && warmup_ok && measured == options.blocks &&
            statuses[0] == measured && finite;
        std::cout.imbue(std::locale::classic());
        std::cout << std::boolalpha << std::setprecision(17)
            << "{\n\"schema_version\":2,\"benchmark\":\"synthetic_event_processor\",\"complete\":"
            << complete << ",\n\"identity\":{\"source_revision\":";
        string(RHODES_SOURCE_REVISION);
        std::cout << ",\"source_diff_hash\":"; string(RHODES_SOURCE_DIFF_HASH);
        std::cout << ",\"schedule_hash_fnv1a64\":\"" << std::hex << schedule_hash << std::dec
            << "\",\"timing_scope\":\"EventProcessor::process including preflight, dispatch and sample rendering\"},\n"
            << "\"environment\":{\"machine_label\":"; string(options.machine);
        std::cout << ",\"platform\":"; string(platform());
        std::cout << ",\"architecture\":"; string(architecture());
        std::cout << ",\"cpu_model\":null,\"kernel\":null,\"power_policy\":null,"
            "\"affinity\":\"uncontrolled\",\"priority\":\"uncontrolled\",\"competing_load\":\"uncontrolled\","
            "\"floating_point_rounding_code\":" << std::fegetround()
            << ",\"flush_to_zero\":\"not inspected\"},\n\"build\":{\"configuration\":";
        string(RHODES_BUILD_CONFIGURATION);
        std::cout << ",\"compiler\":"; string(compiler());
#if defined(_MSC_FULL_VER)
        std::cout << ",\"msvc_full_version\":" << _MSC_FULL_VER;
#endif
        std::cout << ",\"complete_flags\":"; string(RHODES_COMPLETE_BUILD_FLAGS);
#if defined(NDEBUG)
        std::cout << ",\"ndebug\":true";
#else
        std::cout << ",\"ndebug\":false";
#endif
#if defined(__FAST_MATH__)
        std::cout << ",\"fast_math_macro\":true";
#else
        std::cout << ",\"fast_math_macro\":false";
#endif
        std::cout << ",\"sanitizer\":\"not recorded\",\"lto\":\"not recorded\"},\n\"workload\":{\"id\":";
        string(options.workload);
        std::cout << ",\"offset_policy\":"; string(options.offsets);
        std::cout << ",\"sample_rate_hz\":" << options.rate << ",\"block_frames\":" << options.frames
            << ",\"requested_blocks\":" << options.blocks << ",\"measured_blocks\":" << measured
            << ",\"measured_duration_s\":" << static_cast<double>(processed_frames) / options.rate
            << ",\"warmup_blocks_requested\":" << options.warmup << ",\"warmup_blocks_completed\":" << warmup_completed
            << ",\"warmup_blocks_attempted\":" << warmup_attempted
            << ",\"warmup_excluded\":true,\"reset_before_measurement\":" << measurement_reset
            << ",\"fixed_slots\":" << rhodes::key_count
            << ",\"selected_key_count\":" << options.keys << ",\"key_indices\":[";
        for (std::size_t i = 0; i < options.keys; ++i) { std::cout << (i ? "," : "") << key_at(options, i); }
        std::cout << "],\"impulse_intensity\":" << intensity << ",\"retrigger_interval_blocks\":" << retrigger_interval(options)
            << ",\"initial_condition_frames_excluded\":" << (initial.count > 0 ? 1 : 0)
            << ",\"initial_condition_events_excluded\":" << initial.count
            << ",\"timed_event_counts\":{\"note_on\":" << event_types[0] << ",\"note_off\":" << event_types[1]
            << ",\"sustain\":" << event_types[2] << ",\"all_notes_off\":" << event_types[3]
            << ",\"reset\":" << event_types[4] << "}},\n\"timing\":{\"clock\":\"std::chrono::steady_clock\","
            "\"is_steady\":" << Clock::is_steady << ",\"nominal_tick_seconds\":"
            << static_cast<double>(Clock::period::num) / static_cast<double>(Clock::period::den)
            << ",\"observed_clock_resolution\":null,\"percentile_method\":\"nearest rank\",\"summary\":";
        summary(timings, deadline);
        std::cout << ",\"consecutive_quarter_windows\":[";
        for (std::size_t quarter = 0; quarter < 4; ++quarter) {
            const auto begin = measured * quarter / 4;
            const auto end = measured * (quarter + 1) / 4;
            std::cout << (quarter ? "," : "") << "{\"begin_block\":" << begin << ",\"end_block_exclusive\":" << end << ",\"summary\":";
            summary(std::span<const double>(timings).subspan(begin, end - begin), deadline);
            std::cout << '}';
        }
        std::cout << "],\"raw_callback_us\":[";
        for (std::size_t i = 0; i < timings.size(); ++i) { std::cout << (i ? "," : "") << timings[i]; }
        std::cout << "]},\n\"integrity\":{\"initial_condition_success\":" << initial_ok
            << ",\"warmup_success\":" << warmup_ok << ",\"finite_output_and_checksum\":" << finite
            << ",\"checksum_sum_squares\":";
        if (std::isfinite(checksum)) { std::cout << checksum; } else { std::cout << "null"; }
        std::cout << ",\"peak_abs_synthetic_volts\":" << peak << ",\"status_counts\":{\"ok\":" << statuses[0]
            << ",\"invalid_input\":" << statuses[1] << ",\"arithmetic_fault\":" << statuses[2]
            << ",\"reset_required\":" << statuses[3] << "},\"last_failure_sample\":";
        if (last_result.failing_sample == rhodes::no_failure_index) { std::cout << "null"; }
        else { std::cout << last_result.failing_sample; }
        std::cout << ",\"observed_subnormal_output_samples\":" << subnormal_outputs
            << ",\"observed_subnormal_modal_values_at_block_ends\":" << subnormal_states
            << ",\"allocation_evidence\":\"not measured by this executable\"},\n\"limits\":["
            "\"Synthetic impulse/loss-switch core; no physical specimen or contact reference in the callback.\","
            "\"No MIDI ownership translation, state mailbox, VST3 interface or audio device is timed.\","
            "\"Preparation, one-frame initial conditions, schedules, hashes, output checks, modal inspection, sorting and JSON are excluded.\","
            "\"All key slots and pickup branches execute; selected_key_count is a schedule input, not an active-voice optimization.\","
            "\"Warmup follows the same schedule then the processor is reset and initialized for measurement.\","
            "\"Timing includes clock overhead and operating-system scheduling; no overhead subtraction or affinity/priority control.\","
            "\"Observed maxima and deadline misses are not worst-case bounds or realtime guarantees.\","
            "\"Subnormal modal values are inspected only at block ends; unobserved subnormals are not ruled out.\","
            "\"Unknown source/build/environment fields require an accompanying reproducibility record.\"]\n}\n";
        if (!std::cout) { throw std::runtime_error("Unable to write JSON"); }
        return complete ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "rhodes_runtime_benchmark: " << error.what() << '\n';
        return 2;
    }
}
