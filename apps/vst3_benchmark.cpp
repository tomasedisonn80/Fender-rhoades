#include "plugins/vst3/processor.hpp"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

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
#include <limits>
#include <locale>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
namespace plugin = rhodes::vst3;
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t hash_basis = 14695981039346656037ULL;
constexpr float intensity = 0.7F;

struct Options {
    std::size_t blocks = 1000;
    std::size_t frames = 128;
    std::size_t warmup = 32;
    std::uint32_t rate = 48000;
    std::size_t keys = rhodes::key_count;
    unsigned sample_format = 64;
    std::string workload = "held";
    std::string machine = "unknown";
};

void usage() {
    std::cout <<
        "rhodes_vst3_benchmark: synthetic VST3 process() full-call observations\n"
        "Usage: rhodes_vst3_benchmark [--blocks 1..1000000] [--block-size 1..8192]\n"
        "  [--sample-rate 32000..192000] [--active-keys 0..73] [--warmup-blocks 0..10000]\n"
        "  [--workload held|pedal|gain] [--sample-format 32|64] [--machine ASCII_LABEL]\n"
        "Defaults: 1000 blocks, 128 frames, 48000 Hz, 73 keys, 32 warmup blocks, held, 64.\n"
        "All workloads retrigger the selected keys at block zero and about every 0.5 s.\n"
        "held keeps note ownership; gain adds one gain point per sample. pedal releases\n"
        "the struck keys immediately and flips sustain at every sample while they ring.\n"
        "Only the actual process() call is timed, including host input access, validation,\n"
        "ownership, automation, output clearing, rendering and state publication.\n"
        "Prints JSON with raw timings and output checks. No device/realtime guarantee.\n";
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
    for (int index = 1; index < argc; ++index) {
        const std::string_view flag{argv[index]};
        if (flag != "--blocks" && flag != "--block-size" && flag != "--sample-rate" &&
            flag != "--active-keys" && flag != "--warmup-blocks" && flag != "--workload" &&
            flag != "--sample-format" && flag != "--machine") {
            throw std::runtime_error("Unknown option: " + std::string(flag));
        }
        if (++index == argc) { throw std::runtime_error("Missing value for " + std::string(flag)); }
        const std::string_view value{argv[index]};
        if (flag == "--blocks") { options.blocks = number<std::size_t>(value, flag); }
        else if (flag == "--block-size") { options.frames = number<std::size_t>(value, flag); }
        else if (flag == "--sample-rate") { options.rate = number<std::uint32_t>(value, flag); }
        else if (flag == "--active-keys") { options.keys = number<std::size_t>(value, flag); }
        else if (flag == "--warmup-blocks") { options.warmup = number<std::size_t>(value, flag); }
        else if (flag == "--sample-format") { options.sample_format = number<unsigned>(value, flag); }
        else if (flag == "--workload") { options.workload = value; }
        else { options.machine = value; }
    }
    if (options.blocks == 0 || options.blocks > 1000000 || options.frames == 0 ||
        options.frames > plugin::kMaximumHostFrames || options.warmup > 10000 ||
        options.rate < rhodes::minimum_sample_rate_hz ||
        options.rate > rhodes::maximum_sample_rate_hz || options.keys > rhodes::key_count ||
        (options.sample_format != 32 && options.sample_format != 64) ||
        (options.workload != "held" && options.workload != "pedal" && options.workload != "gain") ||
        options.machine.empty() || options.machine.size() > 128 ||
        std::any_of(options.machine.begin(), options.machine.end(), [](char value) {
            return static_cast<unsigned char>(value) < 0x20U ||
                   static_cast<unsigned char>(value) > 0x7eU;
        })) {
        throw std::runtime_error("Arguments outside documented ranges; see --help");
    }
    return options;
}

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string(message)); }
}

// Host-owned objects have fixed lifetime and storage. SDK interface calls are
// part of the timed callback; constructing their contents is not.
template<class Interface> struct HostObject : Interface {
    sdk::uint32 PLUGIN_API addRef() override { return 1; }
    sdk::uint32 PLUGIN_API release() override { return 1; }
    sdk::tresult PLUGIN_API queryInterface(const sdk::TUID iid, void** object) override {
        if (iid == nullptr || object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (sdk::FUnknownPrivate::iidEqual(iid, Interface::iid) ||
            sdk::FUnknownPrivate::iidEqual(iid, sdk::FUnknown::iid)) {
            *object = static_cast<Interface*>(this);
            return sdk::kResultOk;
        }
        return sdk::kNoInterface;
    }
};

struct Events final : HostObject<vst::IEventList> {
    std::array<vst::Event, 2 * rhodes::key_count> values{};
    sdk::int32 count = 0;
    std::uint64_t reads = 0;
    sdk::int32 PLUGIN_API getEventCount() override { return count; }
    sdk::tresult PLUGIN_API getEvent(sdk::int32 index, vst::Event& event) override {
        ++reads;
        if (index < 0 || index >= count) { return sdk::kResultFalse; }
        event = values[static_cast<std::size_t>(index)];
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addEvent(vst::Event&) override { return sdk::kNotImplemented; }
};

struct Queue final : HostObject<vst::IParamValueQueue> {
    struct Point { sdk::int32 offset = 0; double value = 0.0; };
    std::array<Point, plugin::kMaximumHostFrames> values{};
    vst::ParamID id = 0;
    sdk::int32 count = 0;
    std::uint64_t reads = 0;
    vst::ParamID PLUGIN_API getParameterId() override { return id; }
    sdk::int32 PLUGIN_API getPointCount() override { return count; }
    sdk::tresult PLUGIN_API getPoint(sdk::int32 index, sdk::int32& offset,
                                   vst::ParamValue& value) override {
        ++reads;
        if (index < 0 || index >= count) { return sdk::kResultFalse; }
        const auto& point = values[static_cast<std::size_t>(index)];
        offset = point.offset;
        value = point.value;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addPoint(sdk::int32, vst::ParamValue, sdk::int32&) override {
        return sdk::kNotImplemented;
    }
};

struct Parameters final : HostObject<vst::IParameterChanges> {
    std::array<Queue*, 2> queues{};
    sdk::int32 count = 0;
    sdk::int32 PLUGIN_API getParameterCount() override { return count; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        return index < 0 || index >= count ? nullptr : queues[static_cast<std::size_t>(index)];
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&, sdk::int32&) override {
        return nullptr;
    }
};

std::size_t retrigger_interval(const Options& options) {
    return (static_cast<std::size_t>(options.rate) + 2 * options.frames - 1) / (2 * options.frames);
}

std::size_t key_at(const Options& options, std::size_t index) {
    return options.keys == 1 ? rhodes::key_count / 2 :
        index * (rhodes::key_count - 1) / (options.keys - 1);
}

void hash_word(std::uint64_t& hash, std::uint64_t word) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash ^= (word >> shift) & 0xffU;
        hash *= 1099511628211ULL;
    }
}

template<class Sample> struct Host {
    static constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ?
        vst::kSample32 : vst::kSample64;
    plugin::RhodesProcessor processor;
    Events events;
    Queue gain;
    Queue pedal;
    Parameters parameters;
    std::array<Sample, plugin::kMaximumHostFrames> left{};
    std::array<Sample, plugin::kMaximumHostFrames> right{};
    std::array<Sample*, 2> channels{left.data(), right.data()};
    vst::AudioBusBuffers output{};
    vst::ProcessData data{};

    explicit Host(const Options& options) {
        gain.id = plugin::kGainId;
        pedal.id = plugin::kSustainId;
        require(processor.initialize(nullptr) == sdk::kResultOk, "Processor initialize failed");
        require(processor.activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk,
                "Output activation failed");
        vst::ProcessSetup setup{vst::kRealtime, sample_size,
            static_cast<sdk::int32>(options.frames), static_cast<double>(options.rate)};
        require(processor.setupProcessing(setup) == sdk::kResultOk, "Processor setup failed");
        require(processor.setActive(true) == sdk::kResultOk, "Processor activation failed");
        output.numChannels = 2;
        if constexpr (std::is_same_v<Sample, float>) { output.channelBuffers32 = channels.data(); }
        else { output.channelBuffers64 = channels.data(); }
        data.processMode = vst::kRealtime;
        data.symbolicSampleSize = sample_size;
        data.numSamples = static_cast<sdk::int32>(options.frames);
        data.numOutputs = 1;
        data.outputs = &output;
        data.inputEvents = &events;
        data.inputParameterChanges = &parameters;
    }
    ~Host() {
        (void)processor.setProcessing(false);
        (void)processor.setActive(false);
        (void)processor.terminate();
    }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    void reset(const Options& options) {
        require(processor.setProcessing(false) == sdk::kResultOk, "Processor stop failed");
        require(processor.setProcessing(true) == sdk::kResultOk, "Processor start failed");
        // Lifecycle reset preserves configuration. Explicitly restore both
        // controls before each phase so warmup cannot change measured inputs.
        gain.values[0] = {0, 0.5};
        pedal.values[0] = {0, 0.0};
        gain.count = 1;
        pedal.count = 1;
        parameters.queues = {&gain, &pedal};
        parameters.count = 2;
        events.count = 0;
        data.numSamples = 0;
        require(processor.process(data) == sdk::kResultOk, "Baseline parameter flush failed");
        data.numSamples = static_cast<sdk::int32>(options.frames);
        require(processor.engine_processor().frame_cursor() == 0 &&
                processor.note_tracker().active_count() == 0,
                "Reset did not restore empty initial conditions");
        events.reads = gain.reads = pedal.reads = 0;
    }

    void schedule(const Options& options, std::size_t block) {
        events.count = 0;
        gain.count = pedal.count = parameters.count = 0;
        if (block % retrigger_interval(options) == 0) {
            for (std::size_t index = 0; index < options.keys; ++index) {
                const auto pitch = static_cast<sdk::int16>(rhodes::NoteTracker::lowest_pitch +
                                                         key_at(options, index));
                vst::Event event{};
                event.type = vst::Event::kNoteOnEvent;
                event.noteOn.channel = 0;
                event.noteOn.pitch = pitch;
                event.noteOn.noteId = static_cast<sdk::int32>(index);
                event.noteOn.velocity = intensity;
                events.values[static_cast<std::size_t>(events.count++)] = event;
                if (options.workload == "pedal") {
                    event = {};
                    event.type = vst::Event::kNoteOffEvent;
                    event.noteOff.channel = 0;
                    event.noteOff.pitch = pitch;
                    event.noteOff.noteId = static_cast<sdk::int32>(index);
                    events.values[static_cast<std::size_t>(events.count++)] = event;
                }
            }
        }
        if (options.workload != "held") {
            auto& queue = options.workload == "gain" ? gain : pedal;
            queue.count = static_cast<sdk::int32>(options.frames);
            parameters.queues[0] = &queue;
            parameters.count = 1;
            for (std::size_t frame = 0; frame < options.frames; ++frame) {
                const bool high = ((static_cast<std::uint64_t>(block) * options.frames + frame) % 2) == 0;
                queue.values[frame] = {static_cast<sdk::int32>(frame),
                    options.workload == "gain" ? (high ? 0.75 : 0.25) : (high ? 1.0 : 0.0)};
            }
        }
        // Sentinels prove the callback overwrote the entire host output. This
        // host-side fill is excluded; the processor's own clearing is timed.
        std::fill_n(left.begin(), options.frames, std::numeric_limits<Sample>::quiet_NaN());
        std::fill_n(right.begin(), options.frames, std::numeric_limits<Sample>::quiet_NaN());
        output.silenceFlags = std::numeric_limits<sdk::uint64>::max();
    }
};

void json_string(std::string_view value) {
    std::cout << '"';
    for (const char byte : value) {
        if (byte == '"' || byte == '\\') { std::cout << '\\'; }
        std::cout << (static_cast<unsigned char>(byte) < 0x20U ? '?' : byte);
    }
    std::cout << '"';
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

void summary(const std::vector<double>& values, double deadline) {
    if (values.empty()) { std::cout << "null"; return; }
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&](std::size_t percent) {
        return sorted[(sorted.size() * percent + 99) / 100 - 1];
    };
    double sum = 0.0;
    std::size_t misses = 0;
    for (const double value : sorted) { sum += value; misses += value > deadline ? 1U : 0U; }
    std::cout << "{\"observations\":" << sorted.size() << ",\"mean_us\":"
        << sum / static_cast<double>(sorted.size()) << ",\"p50_us\":" << percentile(50)
        << ",\"p95_us\":" << percentile(95) << ",\"p99_us\":" << percentile(99)
        << ",\"max_us\":" << sorted.back() << ",\"deadline_us\":" << deadline
        << ",\"deadline_misses\":" << misses << '}';
}

template<class Sample> int run(const Options& options) {
    // Fixed-capacity owners are allocated before either phase, avoiding a large
    // platform-dependent stack frame. The measured loop does not grow vectors.
    auto host = std::make_unique<Host<Sample>>(options);
    std::vector<double> timings(options.blocks);
    host->reset(options);
    std::size_t warmed = 0;
    for (; warmed < options.warmup; ++warmed) {
        host->schedule(options, warmed);
        if (host->processor.process(host->data) != sdk::kResultOk) { break; }
    }
    const bool warmup_ok = warmed == options.warmup;
    host->reset(options);
    std::size_t measured = 0;
    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::uint64_t offered_notes = 0;
    std::uint64_t offered_points = 0;
    std::uint64_t output_hash = hash_basis;
    double checksum = 0.0;
    double peak = 0.0;
    std::uint64_t nonzero_samples = 0;
    bool finite = true;
    bool stereo_equal = true;
    bool silence_flags = true;
    sdk::tresult last_status = sdk::kResultOk;
    for (std::size_t block = 0; warmup_ok && block < options.blocks; ++block) {
        host->schedule(options, block);
        offered_notes += static_cast<std::uint64_t>(host->events.count);
        offered_points += static_cast<std::uint64_t>(host->gain.count + host->pedal.count);
        const auto start = Clock::now();
        last_status = host->processor.process(host->data);
        const auto finish = Clock::now();
        timings[measured++] = std::chrono::duration<double, std::micro>(finish - start).count();
        if (last_status == sdk::kResultOk) { ++succeeded; } else { ++failed; }
        bool block_nonzero = false;
        for (std::size_t frame = 0; frame < options.frames; ++frame) {
            const auto left = host->left[frame];
            const auto right = host->right[frame];
            finite = finite && std::isfinite(left) && std::isfinite(right);
            stereo_equal = stereo_equal && left == right;
            for (const auto sample : {left, right}) {
                if constexpr (std::is_same_v<Sample, float>) {
                    hash_word(output_hash, std::bit_cast<std::uint32_t>(sample));
                } else { hash_word(output_hash, std::bit_cast<std::uint64_t>(sample)); }
                if (std::isfinite(sample)) {
                    const double value = static_cast<double>(sample);
                    checksum += value * value;
                    peak = std::max(peak, std::abs(value));
                    if (sample != Sample{0}) { ++nonzero_samples; block_nonzero = true; }
                }
            }
        }
        finite = finite && std::isfinite(checksum);
        silence_flags = silence_flags && host->output.silenceFlags == (block_nonzero ? 0U : 3U);
        if (last_status != sdk::kResultOk || !finite || !stereo_equal || !silence_flags) { break; }
    }
    timings.resize(measured);
    const auto point_reads = host->gain.reads + host->pedal.reads;
    const auto cursor = host->processor.engine_processor().frame_cursor();
    const auto owners = host->processor.note_tracker().active_count();
    const auto expected_owners = options.workload == "pedal" ? std::size_t{0} : options.keys;
    const bool nonzero_expected = options.keys != 0;
    const bool expected_output = (nonzero_samples != 0) == nonzero_expected;
    const bool complete = warmup_ok && measured == options.blocks && failed == 0 && finite &&
        stereo_equal && silence_flags && expected_output && !host->processor.requires_reset() &&
        cursor == static_cast<std::uint64_t>(measured) * options.frames &&
        owners == expected_owners &&
        host->events.reads == offered_notes && point_reads == offered_points;
    const double deadline = 1e6 * static_cast<double>(options.frames) / options.rate;
    std::cout.imbue(std::locale::classic());
    std::cout << std::boolalpha << std::setprecision(17)
        << "{\n\"schema_version\":1,\"benchmark\":\"synthetic_vst3_process\",\"complete\":" << complete
        << ",\n\"identity\":{\"source_revision\":"; json_string(RHODES_SOURCE_REVISION);
    std::cout << ",\"source_diff_hash\":"; json_string(RHODES_SOURCE_DIFF_HASH);
    std::cout << ",\"timing_scope\":\"RhodesProcessor::process including host getters, validation, ownership, automation, output clearing, rendering and mailbox publication\"},\n"
        "\"environment\":{\"machine_label\":"; json_string(options.machine);
    std::cout << ",\"platform\":"; json_string(platform());
    std::cout << ",\"architecture\":"; json_string(architecture());
    std::cout << ",\"cpu_model\":null,\"kernel\":null,\"power_policy\":null,"
        "\"affinity\":\"uncontrolled\",\"priority\":\"uncontrolled\",\"competing_load\":\"uncontrolled\","
        "\"floating_point_rounding_code\":" << std::fegetround()
        << ",\"flush_to_zero\":\"not inspected\"},\n\"build\":{\"configuration\":";
    json_string(RHODES_BUILD_CONFIGURATION);
    std::cout << ",\"compiler\":"; json_string(compiler());
#if defined(_MSC_FULL_VER)
    std::cout << ",\"msvc_full_version\":" << _MSC_FULL_VER;
#endif
    std::cout << ",\"complete_flags\":"; json_string(RHODES_COMPLETE_BUILD_FLAGS);
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
    json_string(options.workload);
    std::cout << ",\"sample_format_bits\":" << options.sample_format
        << ",\"sample_rate_hz\":" << options.rate << ",\"block_frames\":" << options.frames
        << ",\"requested_blocks\":" << options.blocks << ",\"measured_blocks\":" << measured
        << ",\"requested_warmup_blocks\":" << options.warmup << ",\"completed_warmup_blocks\":" << warmed
        << ",\"selected_key_count\":" << options.keys << ",\"retrigger_interval_blocks\":"
        << retrigger_interval(options) << ",\"note_intensity_binary32\":" << intensity
        << ",\"offered_note_events\":" << offered_notes << ",\"offered_parameter_points\":" << offered_points
        << ",\"initial_normalized_gain\":0.5,\"initial_sustain\":false},\n\"timing\":{\"clock\":\"std::chrono::steady_clock\","
        "\"percentile_rule\":\"nearest rank, one-based ceil(N*p/100)\",\"summary\":";
    summary(timings, deadline);
    std::cout << ",\"raw_callback_us\":[";
    for (std::size_t index = 0; index < timings.size(); ++index) {
        std::cout << (index == 0 ? "" : ",") << timings[index];
    }
    std::cout << "]},\n\"integrity\":{\"warmup_success\":" << warmup_ok
        << ",\"successful_callbacks\":" << succeeded << ",\"failed_callbacks\":" << failed
        << ",\"last_result_code\":" << last_status << ",\"frame_cursor\":" << cursor
        << ",\"active_note_owners\":" << owners << ",\"expected_note_owners\":" << expected_owners
        << ",\"note_getter_reads\":" << host->events.reads << ",\"parameter_point_getter_reads\":" << point_reads
        << ",\"finite_output_and_checksum\":" << finite << ",\"stereo_equal\":" << stereo_equal
        << ",\"silence_flags_correct\":" << silence_flags << ",\"nonzero_output_expected\":" << nonzero_expected
        << ",\"nonzero_channel_samples\":" << nonzero_samples << ",\"output_presence_correct\":" << expected_output
        << ",\"checksum_sum_squares\":";
    if (std::isfinite(checksum)) { std::cout << checksum; } else { std::cout << "null"; }
    std::cout << ",\"peak_abs_digital_output\":" << peak << ",\"output_fnv1a64\":\""
        << std::hex << std::setw(16) << std::setfill('0') << output_hash << std::dec << std::setfill(' ')
        << "\",\"allocation_evidence\":\"not measured by this executable\"},\n\"limits\":["
        "\"Synthetic impulse/loss-switch audio core; offline contact reference and measured specimen data are absent.\","
        "\"Direct actual processor calls linked with the pinned SDK; no plug-in loader, commercial DAW or audio device is timed.\","
        "\"Setup, warmup, lifecycle resets, baseline parameter flushes, schedule construction, sentinels, output checks, sorting and JSON are excluded.\","
        "\"Warmup uses the same schedule then resets mechanics, ownership, gain and sustain before measurement.\","
        "\"All 73 physical-key slots and pickup branches execute even when selected_key_count is smaller.\","
        "\"Output hash uses interleaved left/right raw IEEE words, float32 zero-extended to u64, each encoded little-endian.\","
        "\"Timing includes clock overhead and OS scheduling; no affinity, priority control or overhead subtraction.\","
        "\"Observed maxima and nominal block-period misses are not worst-case bounds or audio-device underruns.\","
        "\"No allocation, locking, fidelity, cross-platform or realtime guarantee follows from this benchmark.\","
        "\"Unknown build/source/environment fields require an accompanying reproducibility record.\"]\n}\n";
    if (!std::cout) { throw std::runtime_error("Unable to write JSON"); }
    return complete ? 0 : 3;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") { usage(); return 0; }
        const auto options = parse(argc, argv);
        return options.sample_format == 32 ? run<float>(options) : run<double>(options);
    } catch (const std::exception& error) {
        std::cerr << "rhodes_vst3_benchmark: " << error.what() << '\n';
        return 2;
    }
}
