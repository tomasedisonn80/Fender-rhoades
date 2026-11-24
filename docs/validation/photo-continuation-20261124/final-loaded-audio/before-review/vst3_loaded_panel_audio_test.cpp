#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// SDK-only functional wiring smoke: link rhodes_vst3_loader_support only.
// No production DSP, headers, codec or direct-component fixture is an oracle.
// Public controls/units: docs/VST3.md; gating/ramp/bound contract:
// include/rhodes/front_panel_effects.hpp. This is not a fidelity experiment.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Module = VST3::Hosting::Module;
using Bytes = std::vector<std::uint8_t>;
using Controls = std::array<double, 8>; // Public persistent IDs 0..7.
constexpr Controls neutral{.5, 0, 1, .5, .5, 0, .5, 0};
constexpr sdk::int32 block_frames = 256, total_frames = 96000;
constexpr sdk::int32 note_start = 512, note_release = 60000, switch_off = 48000;
constexpr sdk::int32 ramp_frames = 480; // ceil(0.010 * 48000).
constexpr std::array<sdk::int16, 3> pitches{40, 68, 96};
constexpr std::array<const char*, 3> processor_ids{
    "BDDA70F46F9244C1A5639EE371A0B804", "4C0E773A24FC4BE9B804BC71B8B98B33",
    "D73B886A72E04FC892435F1DC5F0B6A9"};
constexpr std::array<const char*, 3> controller_ids{
    "EA801E8B97864ED9B3A0E6572D81CE61", "0569EDDBD21D4568A9ED52C3D8545B6D",
    "A45F23E1B68A4D79AF31472C8E909D52"};
std::string context;
std::size_t checks = 0, callbacks = 0, renders = 0, sample_comparisons = 0;

void require(bool value, std::string_view why) {
    ++checks;
    if (!value) { throw std::runtime_error(context + ": " + std::string{why}); }
}

class Instance {
    Module::Ptr module_; // Keep the module alive until all interfaces are gone.
    bool initialized_ = false, controller_initialized_ = false;
    bool active_ = false, processing_ = false;
public:
    sdk::IPtr<vst::IComponent> component;
    sdk::IPtr<vst::IAudioProcessor> audio;
    sdk::IPtr<vst::IEditController> controller;
    ~Instance() {
        if (processing_) { (void)audio->setProcessing(false); }
        if (active_) { (void)component->setActive(false); }
        if (controller_initialized_) { (void)controller->terminate(); }
        if (initialized_) { (void)component->terminate(); }
        controller = nullptr; audio = nullptr; component = nullptr;
    }
    void open(const Module::Ptr& module, vst::HostApplication& host, sdk::int32 format) {
        module_ = module;
        require(module->getFactory().classCount() == 2, "two factory classes");
        std::string processor_id, controller_id;
        for (const auto& info : module->getFactory().classInfos()) {
            if (info.category() == kVstAudioEffectClass) {
                require(!component, "single processor class");
                processor_id = info.ID().toString();
                component = module->getFactory().createInstance<vst::IComponent>(info.ID());
            } else if (info.category() == kVstComponentControllerClass) {
                require(!controller, "single controller class");
                controller_id = info.ID().toString();
                controller = module->getFactory().createInstance<vst::IEditController>(info.ID());
            } else { require(false, "unexpected factory class"); }
        }
        const auto found = std::find(processor_ids.begin(), processor_ids.end(), processor_id);
        require(found != processor_ids.end(), "known public processor identity");
        const auto model = static_cast<std::size_t>(found - processor_ids.begin());
        require(component && controller && controller_id == controller_ids[model],
                "matching loaded controller identity");
        vst::IAudioProcessor* queried = nullptr;
        const auto result = component->queryInterface(vst::IAudioProcessor::iid,
            reinterpret_cast<void**>(&queried));
        audio = sdk::owned(queried);
        require(result == sdk::kResultOk && audio, "loaded audio interface");
        require(component->initialize(&host) == sdk::kResultOk, "initialize component");
        initialized_ = true;
        require(controller->initialize(&host) == sdk::kResultOk, "initialize controller");
        controller_initialized_ = true;
        require(controller->getParameterCount() == 24, "24 public parameters");
        require(controller->normalizedParamToPlain(0, .5) == 16 &&
                controller->normalizedParamToPlain(3, 1) == 12 &&
                controller->normalizedParamToPlain(4, 1) == 12 &&
                controller->normalizedParamToPlain(5, .75) == .75 &&
                controller->normalizedParamToPlain(6, .125) == 1.9375 &&
                controller->normalizedParamToPlain(6, .875) == 10.5625,
                "gain x, tone dB, depth x and rate Hz fixture units");
        require(component->activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk &&
                component->activateBus(vst::kEvent, vst::kInput, 0, true) == sdk::kResultOk,
                "activate output and event buses");
        require(audio->canProcessSampleSize(format) == sdk::kResultOk, "supported sample format");
        vst::ProcessSetup setup{vst::kRealtime, format, block_frames, 48000.0};
        require(audio->setupProcessing(setup) == sdk::kResultOk, "setup 48 kHz processing");
        require(audio->getLatencySamples() == (model == 0 ? 0U : 48U), "model latency");
        require(component->setActive(true) == sdk::kResultOk, "activate component");
        active_ = true;
        require(audio->setProcessing(true) == sdk::kResultOk, "start processing");
        processing_ = true;
    }
    Bytes save() {
        sdk::MemoryStream stream;
        require(component->getState(&stream) == sdk::kResultOk && stream.getSize() == 16,
                "save actual neutral legacy state");
        const auto* begin = reinterpret_cast<const std::uint8_t*>(stream.getData());
        return {begin, begin + stream.getSize()};
    }
    void recall(Bytes bytes) {
        sdk::MemoryStream state{bytes.data(), static_cast<sdk::TSize>(bytes.size())};
        require(component->setState(&state) == sdk::kResultOk, "recall saved neutral state");
        require(save() == bytes, "saved-neutral state identity");
    }
};

void point(vst::ParameterChanges& changes, vst::ParamID id, sdk::int32 offset, double value) {
    sdk::int32 index = 0;
    auto* queue = changes.addParameterData(id, index);
    require(queue && queue->addPoint(offset, value, index) == sdk::kResultOk,
            "append host parameter point");
}

template<class Sample>
using Trace = std::vector<Sample>; // Interleaved stereo; values are not clipped to +/-1.

template<class Sample>
Trace<Sample> render(const Module::Ptr& module, vst::HostApplication& host,
                     const Controls* controls = nullptr, const Bytes* recalled = nullptr,
                     Bytes* saved = nullptr, bool release = true, bool disable = false) {
    constexpr auto format = std::is_same_v<Sample, float> ? vst::kSample32 : vst::kSample64;
    Instance instance;
    instance.open(module, host, format);
    if (recalled) { instance.recall(*recalled); }
    if (saved) { *saved = instance.save(); }
    Trace<Sample> trace;
    trace.reserve(static_cast<std::size_t>(total_frames) * 2);
    std::array<Sample, static_cast<std::size_t>(block_frames)> left{}, right{};
    std::array<Sample*, 2> pointers{left.data(), right.data()};
    for (sdk::int32 start = 0; start < total_frames; start += block_frames) {
        left.fill(std::numeric_limits<Sample>::quiet_NaN());
        right.fill(std::numeric_limits<Sample>::quiet_NaN());
        vst::ParameterChanges changes;
        if (start == 0 && controls) {
            for (vst::ParamID id = 0; id < controls->size(); ++id) {
                point(changes, id, 0, (*controls)[id]);
            }
        }
        if (disable && switch_off >= start && switch_off < start + block_frames) {
            point(changes, 7, switch_off - start, 0);
        }
        vst::EventList events{6};
        for (const bool on : {true, false}) {
            const auto frame = on ? note_start : note_release;
            if ((!on && !release) || frame < start || frame >= start + block_frames) { continue; }
            for (std::size_t key = 0; key < pitches.size(); ++key) {
                vst::Event event{};
                event.sampleOffset = frame - start;
                event.type = on ? vst::Event::kNoteOnEvent : vst::Event::kNoteOffEvent;
                if (on) {
                    event.noteOn.channel = static_cast<sdk::int16>(key);
                    event.noteOn.pitch = pitches[key];
                    event.noteOn.noteId = static_cast<sdk::int32>(key + 10);
                    event.noteOn.velocity = .75F;
                } else {
                    event.noteOff.channel = static_cast<sdk::int16>(key);
                    event.noteOff.pitch = pitches[key];
                    event.noteOff.noteId = static_cast<sdk::int32>(key + 10);
                }
                require(events.addEvent(event) == sdk::kResultOk, "append fixed note event");
            }
        }
        vst::AudioBusBuffers output{};
        output.numChannels = 2;
        if constexpr (std::is_same_v<Sample, float>) { output.channelBuffers32 = pointers.data(); }
        else { output.channelBuffers64 = pointers.data(); }
        vst::ProcessData data{};
        data.processMode = vst::kRealtime; data.symbolicSampleSize = format;
        data.numSamples = block_frames; data.numOutputs = 1; data.outputs = &output;
        data.inputEvents = &events; data.inputParameterChanges = &changes;
        require(instance.audio->process(data) == sdk::kResultOk, "loaded audio callback");
        ++callbacks;
        for (std::size_t index = 0; index < left.size(); ++index) {
            require(std::isfinite(left[index]) && std::isfinite(right[index]),
                    "finite written stereo samples");
            trace.push_back(left[index]); trace.push_back(right[index]);
        }
    }
    ++renders;
    return trace;
}

struct Level { double rms = 0, peak = 0; };
template<class Sample>
Level level(const Trace<Sample>& trace, sdk::int32 first = 0) {
    Level result;
    for (std::size_t index = static_cast<std::size_t>(first) * 2; index < trace.size(); ++index) {
        const double value = trace[index];
        result.rms += value * value;
        result.peak = std::max(result.peak, std::abs(value));
    }
    result.rms = std::sqrt(result.rms / static_cast<double>(trace.size() - static_cast<std::size_t>(first) * 2));
    require(std::isfinite(result.rms) && result.rms > 1e-5 && result.peak > 1e-4,
            "meaningful nonzero signal (RMS > 1e-5, peak > 1e-4)");
    return result;
}

template<class Sample>
void identical(const Trace<Sample>& a, const Trace<Sample>& b,
               sdk::int32 first = 0, sdk::int32 end = total_frames) {
    using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
    const auto begin = static_cast<std::ptrdiff_t>(first) * 2;
    const auto finish = static_cast<std::ptrdiff_t>(end) * 2;
    require(a.size() == b.size() && std::equal(a.begin() + begin, a.begin() + finish,
        b.begin() + begin, [](Sample x, Sample y) {
            return std::bit_cast<Bits>(x) == std::bit_cast<Bits>(y);
        }), "bit-exact fresh-instance audio comparison");
    sample_comparisons += static_cast<std::size_t>(finish - begin);
}

template<class Sample>
void bounded(const Trace<Sample>& trace, double neutral_peak) {
    // B is the matching neutral source's observed peak, in monitored synthetic
    // harp-voltage units, not normalized full scale. At 48 kHz the documented
    // low-pass histories are convex combinations, so |low| <= input peak.
    // G <= 10^(12/20) < 4: bass <= 4B, treble <= 7 times its input;
    // both shelves <= 28B. AM gains are in [0,1], gain never exceeds default.
    // 32B allows ample conversion/roundoff margin. Sustain uses its own held-
    // note neutral reference, because its post-release source differs.
    require(level(trace).peak <= 32 * neutral_peak, "documented conservative 32B peak bound");
}

template<class Sample>
void different(const Trace<Sample>& actual, const Trace<Sample>& reference,
               std::string_view label, sdk::int32 first = 0) {
    const auto source = level(reference, first);
    double energy = 0, peak = 0;
    const auto begin = static_cast<std::size_t>(first) * 2;
    for (std::size_t index = begin; index < actual.size(); ++index) {
        const double delta = static_cast<double>(actual[index]) - reference[index];
        energy += delta * delta; peak = std::max(peak, std::abs(delta));
    }
    const double rms = std::sqrt(energy / static_cast<double>(actual.size() - begin));
    require(rms > std::max(1e-6, .02 * source.rms) && peak > .01 * source.peak,
            "waveform change exceeds RMS/peak significance thresholds");
    sample_comparisons += actual.size() - begin;
    std::cout << context << " case=" << label << " delta_rms=" << rms
              << " relative_rms=" << rms / source.rms << " delta_peak=" << peak << '\n';
}

template<class Sample>
void run(const Module::Ptr& module, vst::HostApplication& host) {
    Bytes state;
    const auto baseline = render<Sample>(module, host, nullptr, nullptr, &state);
    const auto source = level(baseline);
    std::cout << context << " neutral_rms=" << source.rms << " neutral_peak=" << source.peak
              << " effect_peak_limit=" << 32 * source.peak << '\n';
    identical(render<Sample>(module, host, nullptr, &state), baseline);
    for (const vst::ParamID id : {vst::ParamID{0}, vst::ParamID{3}, vst::ParamID{4}}) {
        auto controls = neutral;
        controls[id] = id == 0 ? .25 : 1; // Gain 8x; each shelf separately +12 dB.
        const auto trace = render<Sample>(module, host, &controls);
        bounded(trace, source.peak);
        different(trace, baseline, id == 0 ? "gain_8x" : id == 3 ? "bass_12dB" : "treble_12dB");
        if (id == 0) {
            double error = 0;
            for (std::size_t i = 0; i < trace.size(); ++i) {
                error = std::max(error, std::abs(static_cast<double>(trace[i]) - .5 * baseline[i]));
            }
            require(error <= 8 * std::numeric_limits<Sample>::epsilon() * source.peak,
                    "8x gain gives half of 16x baseline");
        }
    }
    auto controls = neutral;
    controls[1] = 1;
    const auto sustained = render<Sample>(module, host, &controls);
    const auto held = render<Sample>(module, host, nullptr, nullptr, nullptr, false);
    identical(sustained, held);
    identical(sustained, baseline, 0, note_release);
    bounded(sustained, level(held).peak);
    different(sustained, baseline, "sustain_after_release", note_release);
    require(level(sustained, note_release).rms > 2 * level(baseline, note_release).rms,
            "pedal preserves a substantial released-note tail");
    for (const auto& values : {std::array{.25, .125, 0.}, std::array{.75, .125, 0.},
                               std::array{.75, .875, 0.}, std::array{0., .125, 1.},
                               std::array{0., .875, 1.}}) {
        controls = neutral;
        controls[5] = values[0]; controls[6] = values[1]; controls[7] = values[2];
        const auto trace = render<Sample>(module, host, &controls);
        bounded(trace, source.peak);
        identical(trace, baseline); // Disabled depth/rate and zero-depth enabled.
    }
    controls = neutral; controls[5] = .25; controls[6] = .125; controls[7] = 1;
    const auto shallow = render<Sample>(module, host, &controls);
    bounded(shallow, source.peak); different(shallow, baseline, "vibrato_on_depth_025");
    controls[5] = .75;
    const auto deep = render<Sample>(module, host, &controls);
    bounded(deep, source.peak); different(deep, baseline, "vibrato_on_depth_075");
    different(deep, shallow, "enabled_intensity_change");
    const auto disabled = render<Sample>(module, host, &controls, nullptr, nullptr, true, true);
    bounded(disabled, source.peak); different(disabled, baseline, "vibrato_on_then_off");
    identical(disabled, deep, 0, switch_off);
    identical(disabled, baseline, switch_off + ramp_frames);
    controls[6] = .875;
    const auto fast = render<Sample>(module, host, &controls);
    bounded(fast, source.peak); different(fast, baseline, "vibrato_10_5625Hz");
    different(fast, deep, "enabled_speed_change");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "usage: vst3_loaded_panel_audio_test BUNDLE [BUNDLE...]");
        std::cout << std::setprecision(12);
        for (int index = 1; index < argc; ++index) {
            context = argv[index];
            std::string error;
            const auto module = Module::create(argv[index], error);
            require(module != nullptr, "load module: " + error);
            vst::HostApplication host;
            context = std::string{argv[index]} + "/float32"; run<float>(module, host);
            context = std::string{argv[index]} + "/float64"; run<double>(module, host);
        }
        std::cout << "PASS loaded panel audio: renders=" << renders << " callbacks=" << callbacks
                  << " scalar_comparisons=" << sample_comparisons << " checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL loaded panel audio: " << error.what() << '\n';
        return 1;
    }
}
