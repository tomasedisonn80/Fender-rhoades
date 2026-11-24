#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// SDK-only external host. Link ONLY rhodes_vst3_loader_support (which selects
// the SDK Windows/macOS/Linux module loader), never a plugin or core library.
// IDs, RHST bytes, interpolation and sample conversion below are independent
// fixtures. No production instrument header or direct-test fixture is used.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Module = VST3::Hosting::Module;
using Bytes = std::vector<std::uint8_t>;
using Paths = std::array<const char*, 3>;
using Text = std::array<vst::TChar, 128>;
using ControlValues = std::array<double, 8>;
constexpr sdk::int32 frames = 128;
constexpr vst::ParamID gain_id = 0, sustain_id = 1, expression_id = 2;
constexpr vst::ParamID command_base = 0x1000;
constexpr sdk::int32 unspecified_count = std::numeric_limits<sdk::int32>::min();

struct Identity {
    const char* name;
    const char* processor;
    const char* controller;
    unsigned model;
    sdk::uint32 latency;
};
constexpr std::array<Identity, 3> identities{{
    {"direct", "BDDA70F46F9244C1A5639EE371A0B804", "EA801E8B97864ED9B3A0E6572D81CE61", 1, 0},
    {"filtered", "4C0E773A24FC4BE9B804BC71B8B98B33", "0569EDDBD21D4568A9ED52C3D8545B6D", 2, 48},
    {"quadrature", "D73B886A72E04FC892435F1DC5F0B6A9", "A45F23E1B68A4D79AF31472C8E909D52", 3, 48},
}};
std::string context;
std::size_t checks = 0, callbacks = 0, sample_comparisons = 0, rejected_blocks = 0;
std::size_t foreign_states = 0, load_orders = 0, guarded_mono_callbacks = 0;
std::array<std::size_t, 2> panel_saves{};
std::size_t panel_reopens = 0, panel_rejections = 0, panel_foreign_states = 0;

// Frozen normalized values in public persistent-ID order (0..7). The boolean
// inputs deliberately exercise canonicalization independently of the byte oracle.
constexpr ControlValues default_controls{.5, 0, 1, .5, .5, 0, .5, 0};
constexpr ControlValues panel_inputs{.75, .75, .3125, .125, .875, .375, .625, .5};
constexpr ControlValues panel_controls{.75, 1, .3125, .125, .875, .375, .625, 1};

void require(bool value, std::string_view message) {
    ++checks;
    if (!value) { throw std::runtime_error(context + ": " + std::string{message}); }
}

template<class Sample>
bool same_bits(Sample a, Sample b) {
    using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b);
}

template<class Sample>
bool same_trace(const std::vector<Sample>& a, const std::vector<Sample>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](Sample left, Sample right) { return same_bits(left, right); });
}

Bytes packet(unsigned model, std::uint32_t expression = 0x3f800000U,
             std::uint32_t gain = 0x3f000000U, bool sustain = false, bool force_v2 = false) {
    const bool v2 = force_v2 || expression != 0x3f800000U;
    Bytes result{'R', 'H', 'S', 'T', static_cast<std::uint8_t>(v2 ? 2 : 1), 0,
                 static_cast<std::uint8_t>(model), static_cast<std::uint8_t>(model >> 8U)};
    const auto append = [&result](std::uint32_t word) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            result.push_back(static_cast<std::uint8_t>(word >> shift));
        }
    };
    append(gain);
    append(sustain ? 0x3f800000U : 0U);
    if (v2) { append(expression); }
    return result;
}

Bytes panel_packet(unsigned model) {
    // Hand-written little-endian binary32 bytes, not values serialized by the
    // module, a production codec, or the controller-to-host queue fixture.
    return {'R', 'H', 'S', 'T', 3, 0,
        static_cast<std::uint8_t>(model), static_cast<std::uint8_t>(model >> 8U),
        0x00, 0x00, 0x40, 0x3f, // Monitoring Gain .75
        0x00, 0x00, 0x80, 0x3f, // Sustain 1
        0x00, 0x00, 0xa0, 0x3e, // Expression .3125
        0x00, 0x00, 0x00, 0x3e, // Bass .125
        0x00, 0x00, 0x60, 0x3f, // Treble .875
        0x00, 0x00, 0xc0, 0x3e, // Intensity .375
        0x00, 0x00, 0x20, 0x3f, // Speed .625
        0x00, 0x00, 0x80, 0x3f}; // Vibrato 1
}

template<class Interface>
Bytes snapshot(Interface& object) {
    sdk::MemoryStream stream;
    require(object.getState(&stream) == sdk::kResultOk, "read actual module state");
    require(stream.getSize() == 16 || stream.getSize() == 20 || stream.getSize() == 40,
            "adaptive exact v1/v2/v3 state extent");
    const auto* begin = reinterpret_cast<const std::uint8_t*>(stream.getData());
    return {begin, begin + stream.getSize()};
}

template<class Interface>
sdk::tresult set_state(Interface& object, Bytes bytes) {
    sdk::MemoryStream stream{bytes.data(), static_cast<sdk::TSize>(bytes.size())};
    return object.setState(&stream);
}

sdk::tresult set_component_state(vst::IEditController& controller, Bytes bytes) {
    sdk::MemoryStream stream{bytes.data(), static_cast<sdk::TSize>(bytes.size())};
    return controller.setComponentState(&stream);
}

class OneByteStream final : public sdk::MemoryStream {
public:
    OneByteStream() = default;
    explicit OneByteStream(Bytes& bytes)
        : sdk::MemoryStream(bytes.data(), static_cast<sdk::TSize>(bytes.size())) {}
    sdk::int32 reads = 0, writes = 0;
    sdk::tresult PLUGIN_API read(void* buffer, sdk::int32 count, sdk::int32* done) override {
        ++reads;
        return sdk::MemoryStream::read(buffer, std::min(count, sdk::int32{1}), done);
    }
    sdk::tresult PLUGIN_API write(void* buffer, sdk::int32 count, sdk::int32* done) override {
        ++writes;
        return sdk::MemoryStream::write(buffer, std::min(count, sdk::int32{1}), done);
    }
};

// These stack-owned interface adapters preserve the exact supplied queue and
// point sequence. SDK ParameterChanges/ParameterValueQueue would merge IDs,
// sort decreasing offsets and erase duplicate points before the plugin sees them.
template<class Interface>
struct HostObject : Interface {
    sdk::tresult PLUGIN_API queryInterface(const sdk::TUID iid, void** object) override {
        if (object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (sdk::FUnknownPrivate::iidEqual(iid, Interface::iid) ||
            sdk::FUnknownPrivate::iidEqual(iid, sdk::FUnknown::iid)) {
            *object = static_cast<Interface*>(this);
            return sdk::kResultOk;
        }
        return sdk::kNoInterface;
    }
    sdk::uint32 PLUGIN_API addRef() override { return 1; }
    sdk::uint32 PLUGIN_API release() override { return 1; }
};

struct Point { sdk::int32 offset; double value; };
struct RawQueue final : HostObject<vst::IParamValueQueue> {
    vst::ParamID id = expression_id;
    std::vector<Point> points;
    sdk::int32 reported_count = unspecified_count;
    sdk::int32 fail_at = -1;
    sdk::int32 point_reads = 0;
    RawQueue() = default;
    RawQueue(vst::ParamID tag, std::initializer_list<Point> values) : id(tag), points(values) {}
    vst::ParamID PLUGIN_API getParameterId() override { return id; }
    sdk::int32 PLUGIN_API getPointCount() override {
        return reported_count == unspecified_count ? static_cast<sdk::int32>(points.size()) : reported_count;
    }
    sdk::tresult PLUGIN_API getPoint(sdk::int32 index, sdk::int32& offset, double& value) override {
        ++point_reads;
        if (index < 0 || index == fail_at || static_cast<std::size_t>(index) >= points.size()) {
            return sdk::kResultFalse;
        }
        offset = points[static_cast<std::size_t>(index)].offset;
        value = points[static_cast<std::size_t>(index)].value;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addPoint(sdk::int32, double, sdk::int32&) override {
        return sdk::kNotImplemented;
    }
};

struct RawChanges final : HostObject<vst::IParameterChanges> {
    std::vector<RawQueue> queues;
    sdk::int32 reported_count = unspecified_count;
    sdk::int32 null_at = -1;
    RawChanges() = default;
    RawChanges(std::initializer_list<RawQueue> values) : queues(values) {}
    sdk::int32 PLUGIN_API getParameterCount() override {
        return reported_count == unspecified_count ? static_cast<sdk::int32>(queues.size()) : reported_count;
    }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        return index < 0 || index == null_at || static_cast<std::size_t>(index) >= queues.size() ?
            nullptr : &queues[static_cast<std::size_t>(index)];
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&, sdk::int32&) override {
        return nullptr;
    }
};

// A real single-pointer mono array ends immediately before an inaccessible
// page. An accidental read of channel pointer 1 therefore faults in the loaded
// binary even when the plugin was built without a sanitizer.
class GuardedPage {
    void* memory_ = nullptr;
    std::size_t page_ = 0;
public:
    GuardedPage() {
#if defined(_WIN32)
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        page_ = static_cast<std::size_t>(info.dwPageSize);
        memory_ = VirtualAlloc(nullptr, 2 * page_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        require(memory_ != nullptr, "allocate native guarded pages");
        DWORD previous = 0;
        if (VirtualProtect(static_cast<char*>(memory_) + page_, page_, PAGE_NOACCESS, &previous) == 0) {
            (void)VirtualFree(memory_, 0, MEM_RELEASE);
            memory_ = nullptr;
            require(false, "protect native guard page");
        }
#else
        const auto size = sysconf(_SC_PAGESIZE);
        require(size > 0, "read native page size");
        page_ = static_cast<std::size_t>(size);
        memory_ = mmap(nullptr, 2 * page_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        require(memory_ != MAP_FAILED, "allocate native guarded pages");
        if (mprotect(static_cast<char*>(memory_) + page_, page_, PROT_NONE) != 0) {
            (void)munmap(memory_, 2 * page_);
            memory_ = nullptr;
            require(false, "protect native guard page");
        }
#endif
    }
    GuardedPage(const GuardedPage&) = delete;
    GuardedPage& operator=(const GuardedPage&) = delete;
    ~GuardedPage() {
#if defined(_WIN32)
        if (memory_ != nullptr) { (void)VirtualFree(memory_, 0, MEM_RELEASE); }
#else
        if (memory_ != nullptr && memory_ != MAP_FAILED) { (void)munmap(memory_, 2 * page_); }
#endif
    }
    void* inaccessible() const { return static_cast<char*>(memory_) + page_; }
    template<class Sample>
    Sample** mono(Sample* samples) {
        auto* slot = static_cast<char*>(memory_) + page_ - sizeof(Sample*);
        return ::new (static_cast<void*>(slot)) Sample*(samples);
    }
};

bool ascii_equals(const vst::TChar* value, std::string_view expected) {
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (value[index] != static_cast<vst::TChar>(expected[index])) { return false; }
    }
    return value[expected.size()] == 0;
}

class Instance {
    Module::Ptr module_;
    bool initialized_ = false, controller_initialized_ = false, active_ = false, processing_ = false;
    sdk::IPtr<vst::IConnectionPoint> component_connection_, controller_connection_;
    bool component_connected_ = false, controller_connected_ = false;
public:
    sdk::IPtr<vst::IComponent> component;
    sdk::IPtr<vst::IEditController> controller;
    sdk::IPtr<vst::IAudioProcessor> audio;
    sdk::int32 width = 2;
    unsigned model = 0;
    sdk::uint32 latency = 0;
    Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    ~Instance() {
        if (processing_) { (void)audio->setProcessing(false); }
        if (active_) { (void)component->setActive(false); }
        if (controller_connected_) { (void)controller_connection_->disconnect(component_connection_); }
        if (component_connected_) { (void)component_connection_->disconnect(controller_connection_); }
        controller_connection_ = nullptr; component_connection_ = nullptr;
        if (controller_initialized_) { (void)controller->terminate(); }
        if (initialized_) { (void)component->terminate(); }
        controller = nullptr; audio = nullptr; component = nullptr;
    }
    void open(const Module::Ptr& module, const Identity& identity, vst::HostApplication& host,
              bool connect_pair = false) {
        module_ = module;
        model = identity.model;
        latency = identity.latency;
        require(module_->getFactory().classCount() == 2, "exactly two module factory classes");
        for (const auto& info : module_->getFactory().classInfos()) {
            if (info.category() == kVstAudioEffectClass) {
                require(info.ID().toString() == identity.processor && component == nullptr,
                        "frozen unique processor class ID");
                require(info.subCategoriesString() == "Instrument|Synth",
                        "factory must not advertise a mono- or stereo-only restriction");
                component = module_->getFactory().createInstance<vst::IComponent>(info.ID());
            } else if (info.category() == kVstComponentControllerClass) {
                require(info.ID().toString() == identity.controller && controller == nullptr,
                        "frozen unique controller class ID");
                controller = module_->getFactory().createInstance<vst::IEditController>(info.ID());
            } else { require(false, "unexpected module factory category"); }
        }
        require(component != nullptr && controller != nullptr, "instantiate both actual module classes");
        vst::IAudioProcessor* queried = nullptr;
        require(component->queryInterface(vst::IAudioProcessor::iid, reinterpret_cast<void**>(&queried)) ==
                    sdk::kResultOk && queried != nullptr, "audio interface from component");
        audio = sdk::owned(queried);
        require(component->initialize(&host) == sdk::kResultOk, "initialize loaded component");
        initialized_ = true;
        require(controller->initialize(&host) == sdk::kResultOk, "initialize loaded controller");
        controller_initialized_ = true;
        sdk::TUID controller_id{};
        require(component->getControllerClassId(controller_id) == sdk::kResultOk &&
                    VST3::UID{controller_id}.toString() == identity.controller,
                "component advertises frozen controller class");
        require(component->getBusCount(vst::kAudio, vst::kInput) == 0 &&
                    component->getBusCount(vst::kAudio, vst::kOutput) == 1 &&
                    component->getBusCount(vst::kEvent, vst::kInput) == 1, "unchanged instrument buses");
        require(controller->getParameterCount() == 24, "twenty-four controls");
        require(snapshot(*component) == packet(model) && snapshot(*controller) == packet(model),
                "literal legacy sixteen-byte default states and model ID");
        check_layout(2);
        if (connect_pair) {
            vst::IConnectionPoint* connection = nullptr;
            require(component->queryInterface(vst::IConnectionPoint::iid,
                        reinterpret_cast<void**>(&connection)) == sdk::kResultOk && connection != nullptr,
                    "query loaded component connection point");
            component_connection_ = sdk::owned(connection);
            connection = nullptr;
            require(controller->queryInterface(vst::IConnectionPoint::iid,
                        reinterpret_cast<void**>(&connection)) == sdk::kResultOk && connection != nullptr,
                    "query loaded controller connection point");
            controller_connection_ = sdk::owned(connection);
            require(component_connection_->connect(controller_connection_) == sdk::kResultOk,
                    "connect loaded component to controller");
            component_connected_ = true;
            require(controller_connection_->connect(component_connection_) == sdk::kResultOk,
                    "connect loaded controller to component");
            controller_connected_ = true;
        }
    }
    void check_layout(sdk::int32 expected) {
        vst::SpeakerArrangement arrangement = 0;
        vst::BusInfo info{};
        require(audio->getBusArrangement(vst::kOutput, 0, arrangement) == sdk::kResultOk &&
                    arrangement == (expected == 1 ? vst::SpeakerArr::kMono : vst::SpeakerArr::kStereo),
                "negotiated standard output arrangement");
        require(component->getBusInfo(vst::kAudio, vst::kOutput, 0, info) == sdk::kResultOk &&
                    info.channelCount == expected && info.busType == vst::kMain &&
                    info.mediaType == vst::kAudio && info.direction == vst::kOutput &&
                    ascii_equals(info.name, expected == 1 ? "Mono Out" : "Stereo Out"),
                "coherent layout name, channel count and main-bus metadata");
        require(audio->getLatencySamples() == latency && audio->getTailSamples() == vst::kInfiniteTail,
                "layout preserves frozen latency and infinite tail");
    }
    void layout(sdk::int32 channels) {
        require(!active_ && !processing_, "fixture negotiates only while suspended");
        auto arrangement = channels == 1 ? vst::SpeakerArr::kMono : vst::SpeakerArr::kStereo;
        const auto before = snapshot(*component);
        require(audio->setBusArrangements(nullptr, 0, &arrangement, 1) == sdk::kResultOk,
                "negotiate mono or stereo");
        width = channels;
        check_layout(channels);
        require(snapshot(*component) == before, "layout does not enter instrument configuration");
    }
    void start(sdk::int32 sample_size, double rate, sdk::int32 channels) {
        layout(channels);
        require(component->activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk &&
                    component->activateBus(vst::kEvent, vst::kInput, 0, true) == sdk::kResultOk,
                "activate audio and event buses");
        vst::ProcessSetup setup{vst::kRealtime, sample_size, frames, rate};
        require(audio->setupProcessing(setup) == sdk::kResultOk, "prepare actual loaded processor");
        require(component->setActive(true) == sdk::kResultOk, "activate loaded component");
        active_ = true;
        require(audio->setProcessing(true) == sdk::kResultOk, "start actual loaded processor");
        processing_ = true;
    }
    void suspend() {
        require(audio->setProcessing(false) == sdk::kResultOk, "stop actual loaded processor");
        processing_ = false;
        require(component->setActive(false) == sdk::kResultOk, "suspend loaded component");
        active_ = false;
    }
    void output_active(bool active) {
        require(component->activateBus(vst::kAudio, vst::kOutput, 0, active) == sdk::kResultOk,
                "set output activation");
    }
    void recall(const Bytes& bytes) {
        require(set_state(*component, bytes) == sdk::kResultOk, "queue literal component recall");
    }
};

vst::Event on(sdk::int32 offset = 0, sdk::int16 channel = 5, sdk::int16 pitch = 68,
              sdk::int32 id = 77) {
    vst::Event event{};
    event.type = vst::Event::kNoteOnEvent; event.sampleOffset = offset;
    event.noteOn.channel = channel; event.noteOn.pitch = pitch;
    event.noteOn.noteId = id; event.noteOn.velocity = 0.75F;
    return event;
}
vst::Event off(sdk::int32 offset = 0, sdk::int16 channel = 5, sdk::int16 pitch = 68,
               sdk::int32 id = 77) {
    vst::Event event{};
    event.type = vst::Event::kNoteOffEvent; event.sampleOffset = offset;
    event.noteOff.channel = channel; event.noteOff.pitch = pitch; event.noteOff.noteId = id;
    return event;
}

enum class Output { supplied, omitted_null, omitted_guard, null_samples };
template<class Sample>
struct Trace { std::vector<Sample> samples; sdk::uint64 silence = 0; bool audible = false; };

template<class Sample>
Trace<Sample> process(Instance& instance, RawChanges* changes = nullptr,
                      std::initializer_list<vst::Event> input = {}, Output mode = Output::supplied,
                      bool accepted = true, sdk::int32 count = frames, sdk::int32 declared_width = -1) {
    require(count > 0 && count <= frames, "bounded fixture callback");
    std::array<Sample, static_cast<std::size_t>(frames) + 2> left{}, right{};
    left.fill(Sample{123}); right.fill(Sample{456});
    GuardedPage guard;
    const auto width = declared_width < 0 ? instance.width : declared_width;
    const bool null_samples = mode == Output::null_samples;
    std::array<Sample*, 2> stereo{null_samples ? nullptr : left.data() + 1,
                                null_samples ? nullptr : right.data() + 1};
    Sample** pointers = width == 1 ? guard.mono(null_samples ? nullptr : left.data() + 1) : stereo.data();
    vst::AudioBusBuffers output{};
    output.numChannels = width;
    output.silenceFlags = 0x8877U;
    if constexpr (std::is_same_v<Sample, float>) { output.channelBuffers32 = pointers; }
    else { output.channelBuffers64 = pointers; }
    vst::EventList events{static_cast<sdk::int32>(input.size())};
    for (auto event : input) { require(events.addEvent(event) == sdk::kResultOk, "append event fixture"); }
    vst::ProcessData data{};
    data.processMode = vst::kRealtime;
    data.symbolicSampleSize = std::is_same_v<Sample, float> ? vst::kSample32 : vst::kSample64;
    data.numSamples = count;
    data.numOutputs = mode == Output::omitted_null || mode == Output::omitted_guard ? 0 : 1;
    data.outputs = mode == Output::omitted_null ? nullptr : mode == Output::omitted_guard ?
        static_cast<vst::AudioBusBuffers*>(guard.inaccessible()) : &output;
    data.inputParameterChanges = changes;
    data.inputEvents = input.size() == 0 ? nullptr : &events;
    ++callbacks;
    if (width == 1 && data.numOutputs == 1) { ++guarded_mono_callbacks; }
    if (!accepted) { ++rejected_blocks; }
    const auto result = instance.audio->process(data);
    require(result == (accepted ? sdk::kResultOk : sdk::kResultFalse),
            accepted ? "loaded callback accepted" : "loaded callback rejected");
    require(left.front() == Sample{123} && left.back() == Sample{123} &&
                right.front() == Sample{456} && right.back() == Sample{456}, "sample buffer end canaries");
    Trace<Sample> trace;
    trace.silence = output.silenceFlags;
    for (sdk::int32 index = 0; index < count; ++index) {
        const auto at = static_cast<std::size_t>(index) + 1;
        if (data.numOutputs == 0 || null_samples) {
            require(left[at] == Sample{123} && right[at] == Sample{456}, "omitted/null buffers untouched");
        } else {
            require(std::isfinite(left[at]), "finite loaded sample");
            if (width == 2) { require(same_bits(left[at], right[at]), "stereo exact duplicate scalar"); }
            else { require(right[at] == Sample{456}, "mono never touches a second sample buffer"); }
            trace.samples.push_back(left[at]);
            trace.audible = trace.audible || left[at] != Sample{0};
            if (!accepted) { require(left[at] == Sample{0}, "bounded rejected-block cleanup"); }
        }
    }
    for (sdk::int32 index = count; index < frames; ++index) {
        const auto at = static_cast<std::size_t>(index) + 1;
        require(left[at] == Sample{123} && right[at] == Sample{456}, "short callback preserves sample storage beyond declared frame count");
    }
    if (data.numOutputs == 0) { require(output.silenceFlags == 0x8877U, "omission leaves silence flags untouched"); }
    else if (!null_samples) {
        require(output.silenceFlags == (trace.audible ? 0U : width == 1 ? 1U : 3U),
                "width-correct silence flags");
    }
    return trace;
}

void flush(Instance& instance, RawChanges* changes, int bus_form, bool accepted = true) {
    GuardedPage guard;
    vst::AudioBusBuffers output{};
    output.numChannels = bus_form == 1 ? 0 : instance.width;
    output.silenceFlags = 0xffffU;
    output.channelBuffers64 = static_cast<double**>(guard.inaccessible());
    vst::ProcessData data{};
    data.processMode = vst::kRealtime;
    data.symbolicSampleSize = vst::kSample64;
    data.numOutputs = bus_form == 0 ? 0 : 1;
    data.outputs = bus_form == 0 ? static_cast<vst::AudioBusBuffers*>(guard.inaccessible()) : &output;
    data.inputParameterChanges = changes;
    ++callbacks;
    if (!accepted) { ++rejected_blocks; }
    require(instance.audio->process(data) == (accepted ? sdk::kResultOk : sdk::kResultFalse),
            "zero-frame flush result");
    require(output.silenceFlags == (bus_form == 0 ? 0xffffU : bus_form == 1 ? 0U : instance.width == 1 ? 1U : 3U),
            "zero-frame bus width and no pointer access");
}

Text text(std::string_view source) {
    require(source.size() < Text{}.size(), "bounded text fixture");
    Text result{};
    for (std::size_t index = 0; index < source.size(); ++index) { result[index] = static_cast<vst::TChar>(source[index]); }
    return result;
}

void controls(Instance& instance) {
    auto& controller = *instance.controller;
    for (sdk::int32 index = 0; index < 19; ++index) {
        vst::ParameterInfo info{};
        require(controller.getParameterInfo(index, info) == sdk::kResultOk, "read public control metadata");
        require(info.id == (index < 3 ? static_cast<vst::ParamID>(index) :
                    command_base + static_cast<vst::ParamID>(index - 3)), "unchanged parameter IDs and order");
        require(controller.getParamNormalized(info.id) == (index == 0 ? 0.5 : index == 2 ? 1.0 : 0.0),
                "unchanged defaults and unity expression");
        if (index == 2) {
            require(ascii_equals(info.title, "Expression") && ascii_equals(info.units, "x") &&
                        info.stepCount == 0 && info.defaultNormalizedValue == 1.0 &&
                        info.flags == vst::ParameterInfo::kCanAutomate, "expression continuous automatable metadata");
            for (const double value : {0.0, 0.25, 0.5, 1.0}) {
                require(controller.normalizedParamToPlain(2, value) == value &&
                            controller.plainParamToNormalized(2, value) == value, "expression plain factor range");
            }
        }
        if (index >= 3) { require(info.flags == vst::ParameterInfo::kNoFlags, "legacy commands remain nonautomatable"); }
    }
    vst::IMidiMapping* queried = nullptr;
    require(controller.queryInterface(vst::IMidiMapping::iid, reinterpret_cast<void**>(&queried)) ==
                sdk::kResultOk && queried != nullptr, "actual controller MIDI mapping");
    auto mapping = sdk::owned(queried);
    for (sdk::int16 channel = 0; channel < 16; ++channel) {
        for (const auto cc : {11, 64, 123}) {
            vst::ParamID id = 0xffffffffU;
            const vst::ParamID wanted = cc == 11 ? expression_id : cc == 64 ? sustain_id :
                command_base + static_cast<vst::ParamID>(channel);
            require(mapping->getMidiControllerAssignment(0, channel, static_cast<vst::CtrlNumber>(cc), id) ==
                        sdk::kResultOk && id == wanted, "all sixteen channels preserve expected global CC11 and CC64 mapping");
            if (cc == 11) {
                const auto value = static_cast<double>(channel) / 16.0;
                RawChanges changes{{id, {{0, value}}}};
                flush(instance, &changes, channel % 3);
                require(snapshot(*instance.component) == packet(instance.model, std::bit_cast<std::uint32_t>(static_cast<float>(value))),
                        "each mapped CC11 ID reaches actual processor configuration");
            }
        }
        for (const auto cc : {43, 120}) {
            vst::ParamID id = 0xffffffffU;
            require(mapping->getMidiControllerAssignment(0, channel, static_cast<vst::CtrlNumber>(cc), id) !=
                        sdk::kResultOk && id == 0xffffffffU, "unmapped controllers preserve output sentinel");
        }
    }
    for (const auto bus : {-1, 1}) {
        vst::ParamID id = 0xffffffffU;
        require(mapping->getMidiControllerAssignment(bus, 0, 11, id) != sdk::kResultOk && id == 0xffffffffU,
                "invalid CC11 bus is rejected atomically");
    }
    for (const sdk::int16 channel : {sdk::int16{-1}, sdk::int16{16}}) {
        vst::ParamID id = 0xffffffffU;
        require(mapping->getMidiControllerAssignment(0, channel, 11, id) != sdk::kResultOk && id == 0xffffffffU,
                "invalid CC11 channel is rejected atomically");
    }
    require(controller.setParamNormalized(2, 0.25) == sdk::kResultOk, "seed nonunity controller state");
    const auto before = snapshot(controller);
    for (const auto source : {"0", "-0", "  +.25x\t", " \t+2.5e-1 x\r\n", "1", "0.1"}) {
        auto input = text(source);
        const auto original = input;
        double value = -3.0;
        const double expected = std::string_view{source} == "1" ? 1.0 : std::string_view{source} == "0.1" ?
            static_cast<double>(0.1F) : std::string_view{source} == "0" || std::string_view{source} == "-0" ? 0.0 : 0.25;
        require(controller.getParamValueByString(2, input.data(), value) == sdk::kResultOk &&
                    same_bits(value, expected) && input == original, "strict expression decimal grammar and binary32 canonicalization");
    }
    for (const auto source : {"", "nan", "inf", "0x1", "1,0", ".5junk", "1X", "1xx", "1e9999", "1e-9999",
                              "-1e-46", "1.00000001", "1.0000000000000002", "-.0001", "1 2", "x"}) {
        auto input = text(source);
        double value = 0.3125;
        require(controller.getParamValueByString(2, input.data(), value) != sdk::kResultOk &&
                    same_bits(value, 0.3125), "invalid expression text preserves caller value");
    }
    for (const auto bits : {0U, 1U, 0x007fffffU, 0x00800000U, 0x3dcccccdU, 0x3f7fffffU, 0x3f800000U}) {
        const auto value = static_cast<double>(std::bit_cast<float>(bits));
        Text rendered{};
        double parsed = -1;
        require(controller.getParamStringByValue(2, value, rendered.data()) == sdk::kResultOk &&
                    controller.getParamValueByString(2, rendered.data(), parsed) == sdk::kResultOk &&
                    same_bits(parsed, value), "expression canonical binary32 text round trip across edge regions");
    }
    auto command_text = text("0.10000000000000001");
    double command = 0;
    require(controller.getParamValueByString(command_base, command_text.data(), command) == sdk::kResultOk &&
                same_bits(command, 0.1) && !same_bits(command, static_cast<double>(0.1F)), "command retains binary64 text value");
    command_text = text(".25x");
    command = 0.3125;
    require(controller.getParamValueByString(command_base, command_text.data(), command) != sdk::kResultOk &&
                command == 0.3125, "commands still reject factor unit suffix");
    require(snapshot(controller) == before, "all text conversion is side-effect free");
}

void state_contract(Instance& instance) {
    for (const auto bits : {0U, 1U, 0x007fffffU, 0x00800000U, 0x3dcccccdU, 0x3e800000U, 0x3f7fffffU, 0x3f800000U, 0x80000000U}) {
        auto source = packet(instance.model, bits, 0x3e800000U, true, true);
        const auto expected = packet(instance.model, bits == 0x80000000U ? 0U : bits, 0x3e800000U, true);
        instance.recall(source);
        require(set_state(*instance.controller, source) == sdk::kResultOk && snapshot(*instance.controller) == expected,
                "controller setState canonical literal v2 packet");
        require(set_component_state(*instance.controller, source) == sdk::kResultOk && snapshot(*instance.component) == expected &&
                    snapshot(*instance.controller) == expected, "component and controller agree on adaptive canonical bytes");
        flush(instance, nullptr, 0);
        require(snapshot(*instance.component) == expected, "pending canonical recall consumed by zero-frame flush");
    }
    const auto unity = packet(instance.model);
    instance.recall(unity);
    require(set_component_state(*instance.controller, unity) == sdk::kResultOk &&
                instance.controller->getParamNormalized(2) == 1.0 && snapshot(*instance.component) == unity &&
                snapshot(*instance.controller) == unity, "literal v1 recall restores unity and exact legacy sixteen bytes");
    const auto pending = packet(instance.model, 0x3e800000U, 0x3f400000U, true);
    instance.recall(pending);
    require(set_state(*instance.controller, pending) == sdk::kResultOk, "seed valid pending v2 state");
    std::vector<Bytes> malformed;
    for (const bool version_two : {false, true}) {
        auto valid = packet(instance.model, version_two ? 0x3e800000U : 0x3f800000U);
        for (std::size_t length = 0; length < valid.size(); ++length) {
            malformed.emplace_back(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(length));
        }
        valid.push_back(0); malformed.push_back(valid);
    }
    auto wrong_length = packet(instance.model); wrong_length.resize(20, 0); malformed.push_back(wrong_length);
    wrong_length = packet(instance.model, 0x3e800000U); wrong_length.resize(16); malformed.push_back(wrong_length);
    for (const auto version : {0U, 3U, 255U}) {
        auto invalid = pending; invalid[4] = static_cast<std::uint8_t>(version); malformed.push_back(invalid);
    }
    for (const auto bits : {0x80000001U, 0xbf000000U, 0x3f800001U, 0x7f800000U, 0x7fc00000U}) {
        malformed.push_back(packet(instance.model, bits));
    }
    for (const auto& source : identities) {
        if (source.model == instance.model) { continue; }
        malformed.push_back(packet(source.model));
        malformed.push_back(packet(source.model, 0x3e800000U));
        foreign_states += 2;
    }
    for (const auto& invalid : malformed) {
        require(set_state(*instance.component, invalid) == sdk::kResultFalse &&
                    set_state(*instance.controller, invalid) == sdk::kResultFalse &&
                    set_component_state(*instance.controller, invalid) == sdk::kResultFalse,
                "malformed/foreign literal state rejected by all three entry points");
        require(snapshot(*instance.component) == pending && snapshot(*instance.controller) == pending,
                "state rejection preserves whole pending configuration");
    }
    flush(instance, nullptr, 0);
    require(snapshot(*instance.component) == pending, "pending recall survives every malformed/foreign packet");
    auto streamed = packet(instance.model, 0x3f000000U);
    OneByteStream read{streamed};
    require(instance.component->setState(&read) == sdk::kResultOk && read.reads == 20,
            "loaded reader completes twenty one-byte transfers");
    OneByteStream written;
    require(instance.component->getState(&written) == sdk::kResultOk && written.writes == 20 &&
                written.getSize() == 20 && std::memcmp(written.getData(), streamed.data(), streamed.size()) == 0,
            "loaded writer completes bounded one-byte v2 transfers with literal bytes");
    instance.recall(unity);
    flush(instance, nullptr, 0);
}

// Independent barycentric evaluation. All audio-test segment lengths and
// values are dyadic, so this form and the specified linear curve agree exactly.
std::array<double, static_cast<std::size_t>(frames)> curve(double anchor, const RawQueue* queue) {
    std::vector<Point> points{{-1, anchor}};
    if (queue != nullptr) {
        for (auto point : queue->points) {
            point.value = static_cast<double>(static_cast<float>(point.value));
            if (points.back().offset == point.offset) { points.back() = point; }
            else { points.push_back(point); }
        }
    }
    std::array<double, static_cast<std::size_t>(frames)> result{};
    std::size_t right = 1;
    for (sdk::int32 frame = 0; frame < frames; ++frame) {
        while (right < points.size() && points[right].offset < frame) { ++right; }
        if (right == points.size()) { result[static_cast<std::size_t>(frame)] = points.back().value; }
        else {
            const auto& a = points[right - 1]; const auto& b = points[right];
            result[static_cast<std::size_t>(frame)] =
                (a.value * static_cast<double>(b.offset - frame) + b.value * static_cast<double>(frame - a.offset)) /
                static_cast<double>(b.offset - a.offset);
        }
    }
    return result;
}

template<class Sample>
void scalar_compare(const Trace<Sample>& actual, const Trace<double>& unity,
                    const std::array<double, static_cast<std::size_t>(frames)>& expression) {
    require(actual.samples.size() == unity.samples.size(), "matched scalar trace lengths");
    for (std::size_t index = 0; index < actual.samples.size(); ++index) {
        const double monitored = unity.samples[index];
        const double converted = expression[index] == 1.0 ? monitored : monitored * expression[index];
        ++sample_comparisons;
        require(same_bits(actual.samples[index], static_cast<Sample>(converted)),
                "independent expression scalar before final host-format conversion");
    }
}

template<class Sample>
void audio_case(const Module::Ptr& module, const Identity& identity, vst::HostApplication& host,
                double rate, sdk::int32 width) {
    constexpr auto format = std::is_same_v<Sample, float> ? vst::kSample32 : vst::kSample64;
    Instance target, reference;
    target.open(module, identity, host); reference.open(module, identity, host);
    target.start(format, rate, width); reference.start(vst::kSample64, rate, 2);
    double anchor = 1;
    bool sustain = false;
    for (int block = 0; block < 7; ++block) {
        RawChanges actual, dry;
        if (block == 1) {
            actual.queues.emplace_back(gain_id, std::initializer_list<Point>{{15, .25}, {47, .75}, {111, .125}, {127, .5}});
            actual.queues.emplace_back(sustain_id, std::initializer_list<Point>{{0, 1}});
            dry = actual;
            actual.queues.emplace_back(expression_id, std::initializer_list<Point>{{31, .25}, {31, .75}, {95, .5}});
            sustain = true;
        } else if (block == 2) {
            actual.queues.emplace_back(sustain_id, std::initializer_list<Point>{{64, 0}});
            dry = actual;
            actual.queues.emplace_back(expression_id, std::initializer_list<Point>{{0, 0}});
            sustain = false;
        } else if (block >= 4) {
            actual.queues.emplace_back(expression_id, std::initializer_list<Point>{{0, block == 5 ? .25 : 1.0}});
        }
        const RawQueue* expression = nullptr;
        for (const auto& queue : actual.queues) { if (queue.id == expression_id) { expression = &queue; } }
        const auto factors = curve(anchor, expression);
        Trace<Sample> observed;
        Trace<double> expected;
        const auto run = [&](std::initializer_list<vst::Event> events) {
            observed = process<Sample>(target, &actual, events);
            expected = process<double>(reference, &dry, events);
        };
        if (block == 0) { run({on()}); }
        else if (block == 1) { run({off(32), on(79, 8, 72, 91)}); }
        else if (block == 2) { run({on(48, 14, 81, 99), off(96, 8, 72, 91)}); }
        else if (block == 3) { run({on(5, 0, 57, 100), off(96, 0, 57, 100)}); }
        else if (block == 4) { run({off(10, 14, 81, 99)}); }
        else { run({}); }
        require(expected.audible, "nonvacuous unity reference including muted evolution and release tail");
        scalar_compare(observed, expected, factors);
        require(observed.audible == (block != 2 && block != 3), "expression mute/resume remains nonvacuous");
        anchor = factors.back();
        require(snapshot(*target.component) == packet(identity.model, std::bit_cast<std::uint32_t>(static_cast<float>(anchor)),
                    0x3f000000U, sustain), "render publishes independent canonical adaptive state");
    }
    // Inactive omission and supplied-null buffers must advance exactly the same
    // mechanics, automation, filter history and release ownership as the peer.
    target.output_active(false);
    RawChanges half{{expression_id, {{31, .5}, {127, .25}}}};
    (void)process<Sample>(target, &half, {on(11, 3, 64, 122)}, Output::omitted_guard);
    auto dry_trace = process<double>(reference, nullptr, {on(11, 3, 64, 122)});
    require(dry_trace.audible && snapshot(*target.component) == packet(identity.model, 0x3e800000U),
            "omitted inactive output advances automation and audible mechanics");
    RawChanges resume{{expression_id, {{0, 1}}}};
    const auto null_output = process<Sample>(target, &resume, {off(47, 3, 64, 122)}, Output::null_samples);
    const auto null_peer = process<double>(reference, nullptr, {off(47, 3, 64, 122)});
    require(null_output.silence == (null_peer.audible ? 0U : width == 1 ? 1U : 3U),
            "supplied inactive null buffers retain width-correct scalar silence flags");
    (void)process<Sample>(target, nullptr, {}, Output::omitted_null);
    (void)process<double>(reference);
    target.output_active(true);
    const auto resumed = process<Sample>(target);
    dry_trace = process<double>(reference);
    scalar_compare(resumed, dry_trace, curve(1, nullptr));
    require(resumed.audible, "reactivated omitted-output release tail matches rendered peer");
    const auto before = snapshot(*target.component);
    (void)process<Sample>(target, nullptr, {}, Output::omitted_guard, false);
    require(snapshot(*target.component) == before, "active omission rejection preserves configuration");
    scalar_compare(process<Sample>(target), process<double>(reference), curve(1, nullptr));
    // A rejected width mismatch clears the declared supported width, without
    // touching a second mono pointer or advancing the running voice.
    (void)process<Sample>(target, nullptr, {}, Output::supplied, false, frames, width == 1 ? 2 : 1);
    scalar_compare(process<Sample>(target), process<double>(reference), curve(1, nullptr));
    target.recall(packet(identity.model, 0x3e800000U));
    target.suspend(); reference.suspend();
    target.start(format, rate, width == 1 ? 2 : 1);
    reference.start(vst::kSample64, rate, 2);
    require(snapshot(*target.component) == packet(identity.model, 0x3e800000U),
            "suspended layout transition retains recalled expression configuration");
    const auto switched = process<Sample>(target, nullptr, {on()});
    dry_trace = process<double>(reference, nullptr, {on()});
    require(switched.audible && dry_trace.audible, "audible fresh strike after suspended layout transition");
    scalar_compare(switched, dry_trace, curve(.25, nullptr));
}

template<class Sample>
void partition_case(const Module::Ptr& module, const Identity& identity, vst::HostApplication& host,
                    double rate, sdk::int32 width) {
    constexpr auto format = std::is_same_v<Sample, float> ? vst::kSample32 : vst::kSample64;
    Instance whole, partitioned;
    whole.open(module, identity, host); partitioned.open(module, identity, host);
    whole.start(format, rate, 2); partitioned.start(format, rate, width);
    RawChanges full{{gain_id, {{31, .25}, {63, .75}, {95, .5}, {127, .5}}},
                    {expression_id, {{31, .75}, {63, .5}, {95, .25}, {127, 1}}},
                    {sustain_id, {{0, 1}, {96, 0}}}};
    const auto complete = process<Sample>(whole, &full, {on(5, 7, 64, 777), off(91, 7, 64, 777)});
    require(complete.audible, "partition curve comparison has audible output");
    std::vector<Sample> joined;
    constexpr std::array<double, 4> gain{.25, .75, .5, .5};
    constexpr std::array<double, 4> expression{.75, .5, .25, 1};
    for (std::size_t part = 0; part < 4; ++part) {
        RawChanges changes{{gain_id, {{31, gain[part]}}}, {expression_id, {{31, expression[part]}}}};
        if (part == 0 || part == 3) {
            changes.queues.emplace_back(sustain_id, std::initializer_list<Point>{{0, part == 0 ? 1.0 : 0.0}});
        }
        Trace<Sample> trace;
        if (part == 0) { trace = process<Sample>(partitioned, &changes, {on(5, 7, 64, 777)}, Output::supplied, true, 32); }
        else if (part == 2) { trace = process<Sample>(partitioned, &changes, {off(27, 7, 64, 777)}, Output::supplied, true, 32); }
        else { trace = process<Sample>(partitioned, &changes, {}, Output::supplied, true, 32); }
        joined.insert(joined.end(), trace.samples.begin(), trace.samples.end());
    }
    require(joined.size() == complete.samples.size(), "equivalent partition trace extent");
    for (std::size_t index = 0; index < joined.size(); ++index) {
        ++sample_comparisons;
        require(same_bits(joined[index], complete.samples[index]),
                "dyadic gain/expression curves and offset events are bit-exact across equivalent block partitions");
    }
    require(snapshot(*whole.component) == packet(identity.model) &&
                snapshot(*partitioned.component) == packet(identity.model), "partition curves publish exact legacy unity configuration");
}

void negotiation(Instance& instance) {
    for (const sdk::int32 width : {1, 2, 1, 2}) {
        instance.layout(width);
        const auto state = snapshot(*instance.component);
        auto unsupported = vst::SpeakerArr::k51;
        require(instance.audio->setBusArrangements(nullptr, 0, &unsupported, 1) == sdk::kResultFalse,
                "reject unsupported output arrangement");
        auto valid = width == 1 ? vst::SpeakerArr::kMono : vst::SpeakerArr::kStereo;
        require(instance.audio->setBusArrangements(nullptr, 1, &valid, 1) == sdk::kResultFalse &&
                    instance.audio->setBusArrangements(nullptr, 0, &valid, 0) == sdk::kResultFalse &&
                    instance.audio->setBusArrangements(nullptr, 0, &valid, 2) == sdk::kResultFalse &&
                    instance.audio->setBusArrangements(nullptr, 0, nullptr, 1) == sdk::kResultFalse,
                "reject unsupported bus counts and null arrangement");
        instance.check_layout(width);
        require(snapshot(*instance.component) == state, "rejected negotiation preserves configuration");
    }
}

void transactions(const Module::Ptr& module, const Identity& identity, vst::HostApplication& host) {
    Instance target, reference;
    target.open(module, identity, host); reference.open(module, identity, host);
    target.start(vst::kSample64, 48000, 1); reference.start(vst::kSample64, 48000, 2);
    RawChanges dense;
    for (sdk::int32 index = 0; index < 19; ++index) {
        dense.queues.emplace_back(index < 3 ? static_cast<vst::ParamID>(index) :
            command_base + static_cast<vst::ParamID>(index - 3),
            std::initializer_list<Point>{{0, index == 0 ? .5 : index == 2 ? 1.0 : 0.0}});
    }
    flush(target, &dense, 0);
    require(snapshot(*target.component) == packet(identity.model), "nineteen distinct queues include every CC123 channel");
    RawChanges maximum{{expression_id, {}}};
    maximum.queues[0].points.assign(8192, Point{0, .25});
    maximum.queues[0].points.back().value = 1;
    flush(target, &maximum, 2);
    require(snapshot(*target.component) == packet(identity.model), "8192 raw expression points retain final duplicate offset");
    const auto first = process<double>(target, nullptr, {on()});
    const auto peer = process<double>(reference, nullptr, {on()});
    require(first.audible && same_trace(first.samples, peer.samples), "audible transaction oracle seeded");
    std::vector<RawChanges> invalid;
    invalid.emplace_back(std::initializer_list<RawQueue>{{expression_id, {{0, .25}}}, {expression_id, {{1, .75}}}});
    invalid.emplace_back(std::initializer_list<RawQueue>{{expression_id, {{20, .25}, {19, .5}}}});
    for (const double value : {-0.001, 1.00000001, std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()}) {
        invalid.emplace_back(std::initializer_list<RawQueue>{{expression_id, {{0, .25}, {127, value}}}});
    }
    invalid.emplace_back(std::initializer_list<RawQueue>{{expression_id, {{-1, .25}}}});
    invalid.emplace_back(std::initializer_list<RawQueue>{{expression_id, {{128, .25}}}});
    invalid.push_back(maximum); invalid.back().queues[0].points.push_back({0, .75});
    invalid.emplace_back(std::initializer_list<RawQueue>{{expression_id, {{0, .25}}}});
    invalid.back().queues[0].reported_count = -1;
    invalid.emplace_back(std::initializer_list<RawQueue>{{expression_id, {{0, .25}, {127, .5}}}});
    invalid.back().queues[0].fail_at = 1;
    invalid.emplace_back(); invalid.back().reported_count = 25;
    invalid.emplace_back(); invalid.back().reported_count = -1;
    invalid.emplace_back(std::initializer_list<RawQueue>{{expression_id, {{0, .25}}}});
    invalid.back().null_at = 0;
    for (auto& changes : invalid) {
        const auto before = snapshot(*target.component);
        (void)process<double>(target, &changes, {off()}, Output::supplied, false);
        require(snapshot(*target.component) == before, "rejected raw transaction preserves whole configuration");
        const auto observed = process<double>(target);
        const auto expected = process<double>(reference);
        require(observed.audible && same_trace(observed.samples, expected.samples),
                "rejected parameters/events preserve mechanics and note ownership");
    }
    const auto pending = packet(identity.model, 0x3e800000U, 0x3e800000U, true);
    target.recall(pending); reference.recall(pending);
    (void)process<double>(target, &invalid.front(), {off()}, Output::supplied, false);
    require(snapshot(*target.component) == pending, "malformed block preserves pending recall");
    auto observed = process<double>(target);
    auto expected = process<double>(reference);
    require(!observed.audible && same_trace(observed.samples, expected.samples) && snapshot(*target.component) == pending,
            "valid pending recall resets on next accepted callback only");
    observed = process<double>(target, nullptr, {on()});
    expected = process<double>(reference, nullptr, {on()});
    require(observed.audible && same_trace(observed.samples, expected.samples), "post-rejection recalled strike equals reference");
    // Flush expressions in all SDK buffer forms, then compare exact ongoing
    // audio to establish that zero frames consumed no synthesis time.
    for (const int form : {0, 1, 2}) {
        RawChanges values{{expression_id, {{0, .5}, {0, .25}}}};
        // Reference deliberately receives no flush: both already have .25.
        // Comparing against the skipped callback detects accidental time advance.
        flush(target, &values, form);
        RawChanges late{{expression_id, {{1, .75}}}};
        const auto before = snapshot(*target.component);
        flush(target, &late, form, false);
        require(snapshot(*target.component) == before, "invalid flush offset preserves state");
        require(same_trace(process<double>(target).samples, process<double>(reference).samples), "flush consumes no audio time");
    }
}

RawChanges panel_changes(const ControlValues& values, bool panel_only = false) {
    // Deliberately neither ID nor controller-enumeration order. Speed is last,
    // so its malformed second point follows seven fully staged controls.
    constexpr std::array<vst::ParamID, 8> order{4, 0, 7, 2, 5, 1, 3, 6};
    RawChanges changes;
    for (const auto id : order) {
        if (panel_only && id < 3) { continue; }
        const auto value = values[static_cast<std::size_t>(id)];
        const double prior = id == 1 || id == 7 ? (value >= .5 ? 0.0 : 1.0) : .25;
        changes.queues.emplace_back(id, std::initializer_list<Point>{{0, prior}, {0, value}});
    }
    return changes;
}

void controller_edits(Instance& instance, const RawChanges& changes) {
    // Model the host's public controller update independently of delivery to
    // IAudioProcessor. This is not an attached editor/gesture simulation.
    for (const auto& queue : changes.queues) {
        for (const auto& point : queue.points) {
            require(instance.controller->setParamNormalized(queue.id, point.value) == sdk::kResultOk,
                    "reflect persistent control edit through loaded controller");
        }
    }
}

void expect_controls(Instance& instance, const ControlValues& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        require(same_bits(instance.controller->getParamNormalized(static_cast<vst::ParamID>(index)), values[index]),
                "exact reopened normalized persistent control ID " + std::to_string(index));
    }
}

void expect_pair(Instance& instance, const Bytes& expected, const ControlValues& values) {
    require(snapshot(*instance.component) == expected && snapshot(*instance.controller) == expected,
            "loaded processor/controller match independent complete packet");
    expect_controls(instance, values);
}

struct SavedPair { Bytes component, controller; };

SavedPair save_pair(Instance& instance) {
    return {snapshot(*instance.component), snapshot(*instance.controller)};
}

void restore_pair(Instance& instance, const SavedPair& saved,
                  const Bytes& component_expected, const Bytes& controller_expected,
                  const ControlValues& values) {
    instance.recall(saved.component);
    require(snapshot(*instance.component) == component_expected,
            "fresh loaded processor restores saved component bytes before setup");
    require(set_component_state(*instance.controller, saved.component) == sdk::kResultOk &&
                snapshot(*instance.controller) == component_expected,
            "fresh loaded controller accepts independently saved component state");
    require(set_state(*instance.controller, saved.controller) == sdk::kResultOk &&
                snapshot(*instance.controller) == controller_expected,
            "fresh loaded controller restores its separately saved state");
    expect_controls(instance, values);
    ++panel_reopens;
}

void reject_panel_flushes(Instance& instance, const Bytes& expected, int bus_form, bool pending) {
    for (int fault = 0; fault < 3; ++fault) {
        if (pending) { instance.recall(expected); }
        auto changes = panel_changes(default_controls);
        auto& last = changes.queues.back();
        if (fault == 0) { last.points.back().offset = 1; }
        else if (fault == 1) { last.points.back().value = std::numeric_limits<double>::quiet_NaN(); }
        else { last.fail_at = 1; }
        flush(instance, &changes, bus_form, false);
        require(std::all_of(changes.queues.begin(), changes.queues.end(),
                    [](const auto& queue) { return queue.point_reads == 2; }),
                "rejection reaches final raw point after all eight persistent queues");
        expect_pair(instance, expected, panel_controls);
        // The next accepted empty callback must not commit partially staged
        // reset values left over from rejection; pending recall must survive.
        flush(instance, nullptr, bus_form);
        expect_pair(instance, expected, panel_controls);
        ++panel_rejections;
    }
}

void panel_persistence_case(const Module::Ptr& module, const Identity& identity,
                            vst::HostApplication& host, bool stopped, int bus_form) {
    const auto prefix = std::string{identity.name} + "/panel_persistence/" +
        (stopped ? "after-stop/" : "without-setup/") + "bus-form=" + std::to_string(bus_form);
    context = prefix + "/save";
    const auto legacy = packet(identity.model);
    const auto extended = panel_packet(identity.model);
    const auto v2 = packet(identity.model, 0x3ea00000U, 0x3f400000U, true);
    const auto v1 = packet(identity.model, 0x3f800000U, 0x3f400000U, true);
    SavedPair withheld, accepted, reset_panel, reset_expression;
    {
        Instance instance;
        instance.open(module, identity, host, true);
        if (stopped) {
            instance.start(vst::kSample64, 48000, 2);
            require(process<double>(instance, nullptr, {on()}).audible,
                    "stopped case first runs a nonvacuous loaded audio callback");
            instance.suspend();
        }
        auto changes = panel_changes(panel_inputs);
        controller_edits(instance, changes);
        require(snapshot(*instance.controller) == extended,
                "all eight controller edits produce literal v3 bytes");
        expect_controls(instance, panel_controls);
        // An empty host callback cannot substitute for the withheld parameter
        // queues. No direct controller-to-processor configuration channel exists.
        flush(instance, nullptr, bus_form);
        withheld = save_pair(instance);
        require(withheld.component == legacy && withheld.controller == extended,
                "withheld host delivery leaves processor save stale despite edited controller");
        flush(instance, &changes, bus_form);
        require(std::all_of(changes.queues.begin(), changes.queues.end(),
                    [](const auto& queue) { return queue.point_reads == 2; }),
                "accepted flush reads both duplicate-offset endpoints of all eight queues");
        expect_pair(instance, extended, panel_controls);
        reject_panel_flushes(instance, extended, bus_form, false);
        accepted = save_pair(instance);
    } // Destroy both loaded objects before restoring either saved pair.
    context = prefix + "/reopen-withheld";
    {
        Instance reopened;
        reopened.open(module, identity, host, true);
        restore_pair(reopened, withheld, legacy, extended, panel_controls);
        flush(reopened, nullptr, bus_form);
        require(snapshot(*reopened.component) == legacy && snapshot(*reopened.controller) == extended,
                "reopening separate saved controller state cannot repair withheld processor delivery");
    }
    auto neutral = panel_controls;
    std::copy(default_controls.begin() + 3, default_controls.end(), neutral.begin() + 3);
    context = prefix + "/reopen-v3";
    {
        Instance reopened;
        reopened.open(module, identity, host, true);
        restore_pair(reopened, accepted, extended, extended, panel_controls);
        // Each of the other model IDs is rejected while the full v3 recall is
        // pending, through the component and both controller state entry points.
        for (const auto& other : identities) {
            if (other.model == identity.model) { continue; }
            const auto foreign = panel_packet(other.model);
            require(set_state(*reopened.component, foreign) == sdk::kResultFalse &&
                        set_state(*reopened.controller, foreign) == sdk::kResultFalse &&
                        set_component_state(*reopened.controller, foreign) == sdk::kResultFalse,
                    "foreign-model literal v3 rejected by all three state entry points");
            expect_pair(reopened, extended, panel_controls);
            ++panel_foreign_states;
        }
        reject_panel_flushes(reopened, extended, bus_form, true);
        auto changes = panel_changes(neutral, true);
        controller_edits(reopened, changes);
        require(snapshot(*reopened.component) == extended && snapshot(*reopened.controller) == v2,
                "panel reset reaches processor only through delivered queues");
        flush(reopened, &changes, bus_form);
        expect_pair(reopened, v2, neutral);
        reset_panel = save_pair(reopened);
    }
    context = prefix + "/reopen-v2";
    auto unity = neutral;
    unity[2] = 1;
    {
        Instance reopened;
        reopened.open(module, identity, host, true);
        restore_pair(reopened, reset_panel, v2, v2, neutral);
        flush(reopened, nullptr, bus_form);
        expect_pair(reopened, v2, neutral);
        RawChanges changes{{expression_id, {{0, .125}, {0, 1}}}};
        controller_edits(reopened, changes);
        require(snapshot(*reopened.component) == v2 && snapshot(*reopened.controller) == v1,
                "unity expression reset still needs processor delivery");
        flush(reopened, &changes, bus_form);
        expect_pair(reopened, v1, unity);
        reset_expression = save_pair(reopened);
    }
    context = prefix + "/reopen-v1";
    {
        Instance reopened;
        reopened.open(module, identity, host, true);
        restore_pair(reopened, reset_expression, v1, v1, unity);
        flush(reopened, nullptr, bus_form);
        expect_pair(reopened, v1, unity);
    }
    ++panel_saves[stopped ? 1U : 0U];
}

std::array<Module::Ptr, 3> load(const Paths& paths, const std::array<std::size_t, 3>& order) {
    std::array<Module::Ptr, 3> modules;
    std::set<std::string> ids;
    for (const auto index : order) {
        std::string error;
        modules[index] = Module::create(paths[index], error);
        require(modules[index] != nullptr, "SDK platform module loader: " + error);
    }
    for (std::size_t index = 0; index < 3; ++index) {
        for (std::size_t prior = 0; prior < index; ++prior) {
            require(modules[index]->getFactory().get().get() != modules[prior]->getFactory().get().get(),
                    "three physically distinct module factories");
        }
        for (const auto& info : modules[index]->getFactory().classInfos()) {
            require(ids.insert(info.ID().toString()).second, "six unique external class IDs");
        }
    }
    require(ids.size() == 6, "exactly six loaded class IDs");
    return modules;
}

void controls_state(const Paths& paths) {
    auto modules = load(paths, {0, 1, 2});
    vst::HostApplication host;
    for (std::size_t index = 0; index < 3; ++index) {
        context = std::string{identities[index].name} + "/controls_state";
        Instance instance;
        instance.open(modules[index], identities[index], host);
        controls(instance); state_contract(instance); negotiation(instance);
        transactions(modules[index], identities[index], host);
        for (const bool stopped : {false, true}) {
            for (const int bus_form : {0, 1, 2}) {
                panel_persistence_case(modules[index], identities[index], host, stopped, bus_form);
            }
        }
    }
    require(foreign_states == 12, "all six ordered foreign-model pairs rejected in v1 and v2");
    require(panel_saves == std::array<std::size_t, 2>{9, 9} && panel_reopens == 72 &&
                panel_rejections == 108 && panel_foreign_states == 36,
            "complete three-model panel persistence, reopen and rejection matrix");
}

void audio_layout(const Paths& paths) {
    auto modules = load(paths, {0, 1, 2});
    vst::HostApplication host;
    for (std::size_t index = 0; index < 3; ++index) {
        for (const double rate : {48000.0, 192000.0}) {
            for (const sdk::int32 width : {1, 2}) {
                context = std::string{identities[index].name} + "/audio_layout/" + std::to_string(rate) +
                    "/channels=" + std::to_string(width) + "/float";
                audio_case<float>(modules[index], identities[index], host, rate, width);
                partition_case<float>(modules[index], identities[index], host, rate, width);
                context = std::string{identities[index].name} + "/audio_layout/" + std::to_string(rate) +
                    "/channels=" + std::to_string(width) + "/double";
                audio_case<double>(modules[index], identities[index], host, rate, width);
                partition_case<double>(modules[index], identities[index], host, rate, width);
            }
        }
    }
}

template<class Sample>
std::array<std::vector<Sample>, 3> coexist_once(const Paths& paths,
    const std::array<std::size_t, 3>& order, double rate) {
    auto modules = load(paths, order);
    vst::HostApplication host;
    std::array<std::vector<Sample>, 3> traces;
    {
        std::array<Instance, 3> targets, references;
        constexpr auto format = std::is_same_v<Sample, float> ? vst::kSample32 : vst::kSample64;
        for (std::size_t index = 0; index < 3; ++index) {
            targets[index].open(modules[index], identities[index], host);
            references[index].open(modules[index], identities[index], host);
            targets[index].start(format, rate, index == 1 ? 2 : 1);
            references[index].start(vst::kSample64, rate, 2);
        }
        for (std::size_t rotation = 0; rotation < 3; ++rotation) {
            for (std::size_t index = 0; index < 3; ++index) {
                const auto assignment = (index + rotation) % 3;
                const double expression = assignment == 0 ? .25 : assignment == 1 ? .5 : 1.0;
                if (rotation != 0) {
                    targets[index].suspend();
                    targets[index].start(format, rate, assignment == 1 ? 2 : 1);
                }
                targets[index].recall(packet(identities[index].model, std::bit_cast<std::uint32_t>(static_cast<float>(expression))));
                references[index].recall(packet(identities[index].model));
                const auto observed = process<Sample>(targets[index], nullptr, {on()});
                const auto expected = process<double>(references[index], nullptr, {on()});
                require(observed.audible && expected.audible, "nonvacuous independently recalled module strikes");
                scalar_compare(observed, expected, curve(expression, nullptr));
                traces[index].insert(traces[index].end(), observed.samples.begin(), observed.samples.end());
            }
            // Mutate one module while every other module keeps its current
            // note/filter state and remains checked against its own reference.
            targets[rotation].recall(packet(identities[rotation].model, 0U));
            references[rotation].recall(packet(identities[rotation].model));
            for (std::size_t index = 0; index < 3; ++index) {
                const auto assignment = (index + rotation) % 3;
                const double expression = index == rotation ? 0.0 : assignment == 0 ? .25 : assignment == 1 ? .5 : 1.0;
                const auto observed = process<Sample>(targets[index]);
                const auto expected = process<double>(references[index]);
                scalar_compare(observed, expected, curve(expression, nullptr));
                require(observed.audible == (index != rotation), "one module recall cannot silence another module");
                require(snapshot(*targets[index].component) == packet(identities[index].model,
                            std::bit_cast<std::uint32_t>(static_cast<float>(expression))), "module-isolated configuration");
                targets[index].check_layout(assignment == 1 ? 2 : 1);
            }
        }
        for (auto index = order.rbegin(); index != order.rend(); ++index) {
            targets[*index].suspend(); references[*index].suspend();
        }
    }
    for (auto index = order.rbegin(); index != order.rend(); ++index) {
        require(modules[*index].use_count() == 1, "release every instance before module unload");
        modules[*index].reset();
    }
    return traces;
}

template<class Sample>
void coexist_orders(const Paths& paths, double rate) {
    std::array<std::size_t, 3> order{0, 1, 2};
    std::array<std::vector<Sample>, 3> baseline;
    bool first = true;
    do {
        context = std::string{"coexist/"} + std::to_string(rate) +
            (std::is_same_v<Sample, float> ? "/float/" : "/double/") +
            std::to_string(order[0]) + std::to_string(order[1]) + std::to_string(order[2]);
        const auto observed = coexist_once<Sample>(paths, order, rate);
        if (first) { baseline = observed; first = false; }
        else {
            for (std::size_t index = 0; index < observed.size(); ++index) {
                require(same_trace(observed[index], baseline[index]),
                        "bit-exact feature traces independent of all six physical module load orders");
            }
        }
        ++load_orders;
    } while (std::next_permutation(order.begin(), order.end()));
}

void coexist(const Paths& paths) {
    for (const double rate : {48000.0, 192000.0}) {
        coexist_orders<float>(paths, rate); coexist_orders<double>(paths, rate);
    }
    require(load_orders == 24, "all six orders at both rates and both host formats");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5) {
            throw std::runtime_error("usage: vst3_loaded_expression_test controls_state|audio_layout|coexist|all DIRECT_BUNDLE FILTERED_BUNDLE QUADRATURE_BUNDLE");
        }
        const std::string_view selector{argv[1]};
        const Paths paths{argv[2], argv[3], argv[4]};
        require(selector == "controls_state" || selector == "audio_layout" || selector == "coexist" || selector == "all",
                "known loaded expression selector");
        if (selector == "controls_state" || selector == "all") { controls_state(paths); }
        if (selector == "audio_layout" || selector == "all") { audio_layout(paths); }
        if (selector == "coexist" || selector == "all") { coexist(paths); }
        std::cout << "PASS loaded expression " << selector << ": " << checks << " checks, " << callbacks
                  << " loaded callbacks, " << sample_comparisons << " scalar samples, " << rejected_blocks
                  << " rejected blocks, " << foreign_states << " foreign states, " << guarded_mono_callbacks
                  << " guarded mono callbacks, " << load_orders << " module load orders, "
                  << panel_saves[0] << "/" << panel_saves[1] << " no-setup/stopped panel cases, "
                  << panel_reopens << " panel reopens, " << panel_rejections << " atomic panel rejections, "
                  << panel_foreign_states << " foreign v3 states\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL loaded expression: " << error.what() << '\n';
        return 1;
    }
}
