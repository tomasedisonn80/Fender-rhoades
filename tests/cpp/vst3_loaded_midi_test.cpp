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
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

// Deliberately SDK-only: all instrument code is reached through the three
// supplied module factories. Public identities and the release oracle below
// are frozen independently of the production headers and processor fixtures.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Module = VST3::Hosting::Module;
using State = std::array<std::uint8_t, 16>;
using Text = std::array<vst::TChar, 128>;
constexpr sdk::int32 frames = 128;
constexpr vst::ParamID gain_id = 0;
constexpr vst::ParamID sustain_id = 1;
constexpr vst::ParamID command_base = 0x1000;
constexpr std::size_t parameter_count = 18;

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
std::string current_case;
std::size_t checks = 0;
std::size_t callbacks = 0;
std::size_t comparisons = 0;
std::size_t rejected_blocks = 0;
std::size_t rejected_texts = 0;
std::size_t flushes = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(context + "/" + current_case + ": " + std::string{message});
    }
}

constexpr vst::ParamID parameter_id(std::size_t index) {
    return index < 2 ? static_cast<vst::ParamID>(index) :
        command_base + static_cast<vst::ParamID>(index - 2);
}

template<class Interface>
State snapshot(Interface& object) {
    sdk::MemoryStream stream;
    require(object.getState(&stream) == sdk::kResultOk, "read loaded state");
    require(stream.getSize() == static_cast<sdk::TSize>(State{}.size()), "16-byte state contract");
    State result{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(stream.getData()), result.size(), result.begin());
    return result;
}

class Instance {
    // This owner is destroyed last, including when open() or a test throws.
    Module::Ptr module_;
    bool component_initialized_ = false;
    bool controller_initialized_ = false;
    bool active_ = false;
    bool processing_ = false;

public:
    Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    ~Instance() {
        if (processing_) { (void)audio->setProcessing(false); }
        if (active_) { (void)component->setActive(false); }
        if (controller_initialized_) { (void)controller->terminate(); }
        if (component_initialized_) { (void)component->terminate(); }
        controller = nullptr;
        audio = nullptr;
        component = nullptr;
    }

    void open(const Module::Ptr& module, const Identity& identity,
              vst::HostApplication& host, sdk::int32 sample_size) {
        module_ = module;
        require(module_->getFactory().classCount() == 2, "two factory classes");
        for (const auto& info : module_->getFactory().classInfos()) {
            if (info.category() == kVstAudioEffectClass) {
                require(info.ID().toString() == identity.processor, "unchanged processor class ID");
                require(component == nullptr, "single processor factory class");
                component = module_->getFactory().createInstance<vst::IComponent>(info.ID());
            } else if (info.category() == kVstComponentControllerClass) {
                require(info.ID().toString() == identity.controller, "unchanged controller class ID");
                require(controller == nullptr, "single controller factory class");
                controller = module_->getFactory().createInstance<vst::IEditController>(info.ID());
            } else {
                require(false, "unexpected factory class");
            }
        }
        require(component != nullptr && controller != nullptr, "create both loaded interfaces");
        vst::IAudioProcessor* queried = nullptr;
        const auto query_result = component->queryInterface(vst::IAudioProcessor::iid,
            reinterpret_cast<void**>(&queried));
        audio = sdk::owned(queried);
        require(query_result == sdk::kResultOk && audio != nullptr, "query actual component audio");
        require(component->initialize(&host) == sdk::kResultOk, "initialize component");
        component_initialized_ = true;
        require(controller->initialize(&host) == sdk::kResultOk, "initialize controller");
        controller_initialized_ = true;
        sdk::TUID controller_id{};
        require(component->getControllerClassId(controller_id) == sdk::kResultOk &&
                    VST3::UID{controller_id}.toString() == identity.controller,
                "component advertises frozen controller ID");
        require(controller->getParameterCount() == static_cast<sdk::int32>(parameter_count),
                "18 public parameters");
        require(audio->getLatencySamples() == identity.latency, "unchanged latency");
        require(component->activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk,
                "activate stereo output");
        require(component->activateBus(vst::kEvent, vst::kInput, 0, true) == sdk::kResultOk,
                "activate event input");
        vst::ProcessSetup setup{vst::kRealtime, sample_size, frames, 48000.0};
        require(audio->setupProcessing(setup) == sdk::kResultOk, "setup loaded processor");
        require(component->setActive(true) == sdk::kResultOk, "activate component");
        active_ = true;
        require(audio->setProcessing(true) == sdk::kResultOk, "start processing");
        processing_ = true;
        const State state = snapshot(*component);
        require(state[0] == 'R' && state[1] == 'H' && state[2] == 'S' && state[3] == 'T' &&
                    state[4] == 1 && state[5] == 0 &&
                    static_cast<unsigned>(state[6]) + 256U * state[7] == identity.model,
                "unchanged state magic, version and model");
        require(snapshot(*controller) == state, "matching default controller state");
    }

    void close() {
        require(audio->setProcessing(false) == sdk::kResultOk, "stop processing");
        processing_ = false;
        require(component->setActive(false) == sdk::kResultOk, "deactivate component");
        active_ = false;
        require(controller->terminate() == sdk::kResultOk, "terminate controller");
        controller_initialized_ = false;
        require(component->terminate() == sdk::kResultOk, "terminate component");
        component_initialized_ = false;
        controller = nullptr;
        audio = nullptr;
        component = nullptr;
        module_.reset();
    }

    sdk::IPtr<vst::IComponent> component;
    sdk::IPtr<vst::IEditController> controller;
    sdk::IPtr<vst::IAudioProcessor> audio;
};

Text text(std::string_view input) {
    require(input.size() < Text{}.size(), "bounded UTF-16 fixture");
    Text result{};
    for (std::size_t index = 0; index < input.size(); ++index) {
        result[index] = static_cast<vst::TChar>(static_cast<unsigned char>(input[index]));
    }
    return result;
}

void controller_contract(vst::IEditController& controller) {
    current_case = "controller mapping and text";
    for (std::size_t index = 0; index < parameter_count; ++index) {
        vst::ParameterInfo info{};
        require(controller.getParameterInfo(static_cast<sdk::int32>(index), info) == sdk::kResultOk,
                "read public parameter metadata");
        require(info.id == parameter_id(index), "gain/sustain/command IDs and order unchanged");
        require(controller.getParamNormalized(info.id) == (index == 0 ? 0.5 : 0.0),
                "unchanged defaults and initially zero commands");
        if (index >= 2) {
            require(info.flags == vst::ParameterInfo::kNoFlags && info.stepCount == 0,
                    "writable nonautomatable continuous command metadata");
        }
    }
    vst::IMidiMapping* queried = nullptr;
    const auto query_result = controller.queryInterface(vst::IMidiMapping::iid,
        reinterpret_cast<void**>(&queried));
    auto mapping = sdk::owned(queried);
    require(query_result == sdk::kResultOk && mapping != nullptr, "loaded MIDI mapping interface");
    for (sdk::int16 channel = 0; channel < 16; ++channel) {
        vst::ParamID id = 0xffffffffU;
        require(mapping->getMidiControllerAssignment(0, channel, 123, id) == sdk::kResultOk &&
                    id == command_base + static_cast<vst::ParamID>(channel),
                "CC123 maps every channel to its frozen command ID");
        id = 0xffffffffU;
        require(mapping->getMidiControllerAssignment(0, channel, 64, id) == sdk::kResultOk &&
                    id == sustain_id, "CC64 remains shared across all channels");
        id = 0xffffffffU;
        require(mapping->getMidiControllerAssignment(0, channel, 120, id) != sdk::kResultOk &&
                    id == 0xffffffffU, "CC120 remains unmapped without changing caller output");
    }

    // Seed nondefault values so a parser which resets controls is observable.
    std::array<double, parameter_count> normalized{};
    for (std::size_t index = 0; index < parameter_count; ++index) {
        require(controller.setParamNormalized(parameter_id(index), index == 1 ? 1.0 : 0.25) ==
                    sdk::kResultOk, "seed controller values");
        normalized[index] = controller.getParamNormalized(parameter_id(index));
    }
    const State before = snapshot(controller);
    const auto reject = [&](vst::ParamID id, vst::TChar* input) {
        double value = 0.3125;
        ++rejected_texts;
        require(controller.getParamValueByString(id, input, value) != sdk::kResultOk,
                "strict text rejection");
        require(std::bit_cast<std::uint64_t>(value) == std::bit_cast<std::uint64_t>(0.3125),
                "rejected parse preserves caller output");
    };
    for (std::size_t index = 0; index < parameter_count; ++index) {
        for (const auto* malformed : {"nan", "16junk", "0.5junk", "inf", "0x1", "1,0",
                                      "1e9999", "1e-9999"}) {
            auto input = text(malformed);
            const auto original = input;
            reject(parameter_id(index), input.data());
            require(input == original, "parse preserves supplied SDK UTF-16 text");
        }
        reject(parameter_id(index), nullptr);
    }
    for (const auto id : {vst::ParamID{2}, vst::ParamID{0x0fff}, vst::ParamID{0x1010},
                          vst::ParamID{0xffffffffU}}) {
        auto input = text("0.5");
        reject(id, input.data());
    }
    Text unterminated{};
    unterminated.fill(static_cast<vst::TChar>('0'));
    reject(gain_id, unterminated.data());
    auto valid_gain = text(" \t+1.6e1 x ");
    double value = -1.0;
    require(controller.getParamValueByString(gain_id, valid_gain.data(), value) == sdk::kResultOk &&
                value == 0.5, "valid gain decimal and unit suffix");
    for (std::size_t index = 2; index < parameter_count; ++index) {
        auto valid_command = text(" +2.5e-1 ");
        value = -1.0;
        require(controller.getParamValueByString(parameter_id(index), valid_command.data(), value) ==
                    sdk::kResultOk && value == 0.25, "valid normalized command decimal");
    }
    require(snapshot(controller) == before, "text conversion preserves saved configuration");
    for (std::size_t index = 0; index < parameter_count; ++index) {
        require(controller.getParamNormalized(parameter_id(index)) == normalized[index],
                "text conversion neither applies nor resets controls");
    }
}

vst::Event note_on(sdk::int32 offset, sdk::int16 channel, sdk::int16 pitch,
                   sdk::int32 id, float velocity = 0.6F) {
    vst::Event event{};
    event.type = vst::Event::kNoteOnEvent;
    event.sampleOffset = offset;
    event.noteOn.channel = channel;
    event.noteOn.pitch = pitch;
    event.noteOn.noteId = id;
    event.noteOn.velocity = velocity;
    return event;
}

vst::Event note_off(sdk::int32 offset, sdk::int16 channel, sdk::int16 pitch, sdk::int32 id) {
    vst::Event event{};
    event.type = vst::Event::kNoteOffEvent;
    event.sampleOffset = offset;
    event.noteOff.channel = channel;
    event.noteOff.pitch = pitch;
    event.noteOff.noteId = id;
    return event;
}

vst::Event pressure(sdk::int32 offset, sdk::int16 channel = 4, sdk::int16 pitch = 74,
                    sdk::int32 id = -1, float value = 0.5F) {
    vst::Event event{};
    event.type = vst::Event::kPolyPressureEvent;
    event.sampleOffset = offset;
    event.polyPressure.channel = channel;
    event.polyPressure.pitch = pitch;
    event.polyPressure.noteId = id;
    event.polyPressure.pressure = value;
    return event;
}

struct Point { vst::ParamID id; sdk::int32 offset; double value; };

template<class Sample>
struct Audio {
    std::array<Sample, static_cast<std::size_t>(frames)> left{}, right{};
    sdk::uint64 silence = 0;
    bool audible = false;
};

template<class Sample>
Audio<Sample> process(Instance& instance, std::initializer_list<vst::Event> input,
                      std::initializer_list<Point> points, bool accept = true,
                      sdk::int32 count = frames) {
    require(count >= 0 && count <= frames && input.size() <= 32 && points.size() <= 32,
            "bounded loaded fixture");
    vst::EventList events{32};
    for (auto event : input) {
        require(events.addEvent(event) == sdk::kResultOk, "prepare SDK event list");
    }
    vst::ParameterChanges changes{static_cast<sdk::int32>(parameter_count)};
    for (const auto& point : points) {
        sdk::int32 queue_index = 0;
        auto* queue = changes.addParameterData(point.id, queue_index);
        require(queue != nullptr, "prepare SDK parameter queue");
        sdk::int32 point_index = 0;
        require(queue->addPoint(point.offset, point.value, point_index) == sdk::kResultOk,
                "prepare SDK command point");
    }
    Audio<Sample> result;
    result.left.fill(Sample{17});
    result.right.fill(Sample{17});
    std::array<Sample*, 2> channels{result.left.data(), result.right.data()};
    vst::AudioBusBuffers output{};
    output.numChannels = 2;
    output.silenceFlags = 0xffU;
    if constexpr (std::is_same_v<Sample, float>) { output.channelBuffers32 = channels.data(); }
    else { output.channelBuffers64 = channels.data(); }
    vst::ProcessData data{};
    data.processMode = vst::kRealtime;
    data.symbolicSampleSize = std::is_same_v<Sample, float> ? vst::kSample32 : vst::kSample64;
    data.numSamples = count;
    data.numOutputs = count == 0 ? 0 : 1;
    data.outputs = count == 0 ? nullptr : &output;
    data.inputEvents = input.size() == 0 ? nullptr : &events;
    data.inputParameterChanges = points.size() == 0 ? nullptr : &changes;
    ++callbacks;
    const auto status = instance.audio->process(data);
    require(accept ? status == sdk::kResultOk : status != sdk::kResultOk,
            accept ? "loaded callback rejected valid input" : "loaded callback accepted invalid input");
    if (!accept) { ++rejected_blocks; }
    for (std::size_t frame = 0; frame < result.left.size(); ++frame) {
        if (frame >= static_cast<std::size_t>(count)) {
            require(result.left[frame] == Sample{17} && result.right[frame] == Sample{17},
                    "callback wrote beyond requested frame count");
            continue;
        }
        require(std::isfinite(result.left[frame]) && std::isfinite(result.right[frame]),
                "finite loaded stereo output");
        require(result.left[frame] == result.right[frame], "identical mono-derived stereo");
        if (!accept) {
            require(result.left[frame] == Sample{0} && result.right[frame] == Sample{0},
                    "rejected callback clears both output channels");
        }
        result.audible = result.audible || result.left[frame] != Sample{0};
    }
    result.silence = output.silenceFlags;
    if (count != 0) {
        require(result.silence == (result.audible ? 0U : 3U), "accurate loaded silence flags");
    }
    return result;
}

template<class Sample>
struct Pair {
    Instance actual;
    Instance oracle;

    void compare(std::initializer_list<vst::Event> actual_events,
                 std::initializer_list<vst::Event> oracle_events,
                 std::initializer_list<Point> actual_points = {},
                 std::initializer_list<Point> oracle_points = {}) {
        const auto actual_audio = process<Sample>(actual, actual_events, actual_points);
        const auto oracle_audio = process<Sample>(oracle, oracle_events, oracle_points);
        using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
        for (std::size_t frame = 0; frame < actual_audio.left.size(); ++frame) {
            require(std::bit_cast<Bits>(actual_audio.left[frame]) ==
                        std::bit_cast<Bits>(oracle_audio.left[frame]) &&
                        std::bit_cast<Bits>(actual_audio.right[frame]) ==
                        std::bit_cast<Bits>(oracle_audio.right[frame]),
                    "loaded audio differs from explicit-note-off/no-pressure oracle");
        }
        require(actual_audio.audible && oracle_audio.audible, "oracle comparison is audibly nonvacuous");
        require(actual_audio.silence == oracle_audio.silence, "oracle silence flags match");
        require(snapshot(*actual.component) == snapshot(*oracle.component),
                "commands/pressure preserve matching saved configuration");
        ++comparisons;
    }

    void reject(std::initializer_list<vst::Event> events,
                std::initializer_list<Point> points, sdk::int32 count = frames) {
        compare({note_on(0, 4, 74, 40)}, {note_on(0, 4, 74, 40)});
        const State before = snapshot(*actual.component);
        (void)process<Sample>(actual, events, points, false, count);
        require(snapshot(*actual.component) == before, "rejected input preserves saved configuration");
        // The peer receives no rejected callback. Exact continuation therefore
        // detects time advancement, changed mechanics, and lost FIR history.
        compare({}, {});
        // A matching later release also exposes ownership-only corruption.
        compare({note_off(0, 4, 74, 40)}, {note_off(0, 4, 74, 40)});
    }
};

template<class Sample>
void command_timeline(Pair<Sample>& pair) {
    current_case = "shared holders and co-batched pressure";
    pair.compare(
        {note_on(0, 0, 60, 10), pressure(0, 0, 60, std::numeric_limits<sdk::int32>::min(), 0.0F),
         note_on(0, 1, 60, 11, 0.5F), note_on(1, 0, 67, -1, 0.4F),
         note_on(2, 0, 67, -1, 0.3F), note_on(3, 2, 100, 12, 0.2F),
         pressure(3, 15, 127, -1, 1.0F)},
        {note_on(0, 0, 60, 10), note_on(0, 1, 60, 11, 0.5F),
         note_on(1, 0, 67, -1, 0.4F), note_on(2, 0, 67, -1, 0.3F),
         note_on(3, 2, 100, 12, 0.2F)},
        {{sustain_id, 0, 1.0}}, {{sustain_id, 0, 1.0}});

    current_case = "repeated zero CC123, same-offset restrike and pedal release";
    // End sustain before the second command. If the first command incorrectly
    // follows the restrike, the new holder is lost and damping begins at 8;
    // the correct order keeps that holder alive until the command at 17.
    pair.compare(
        {note_on(4, 0, 67, 10, 0.7F), pressure(8, 0, 67, 10)},
        {note_off(4, 0, 60, 10), note_off(4, 0, 67, -1), note_off(4, 0, 67, -1),
         note_on(4, 0, 67, 10, 0.7F), note_off(17, 0, 67, 10)},
        {{command_base, 4, 0.0}, {command_base, 17, 0.0}, {sustain_id, 8, 0.0}},
        {{sustain_id, 8, 0.0}});

    current_case = "shared-channel survival after sustain ends";
    // With sustain off, channel 1's shared-key holder must survive until its
    // explicit release. A preceding channel 0 command must not remove it.
    pair.compare(
        {pressure(7, 1, 60, 11), note_on(19, 3, 65, 13, 0.55F),
         pressure(22, 3, 65, 13), note_off(31, 3, 65, 13)},
        {note_on(19, 3, 65, 13, 0.55F), note_off(29, 1, 60, 11), note_off(31, 3, 65, 13)},
        {{command_base + 1, 29, 0.75}, {sustain_id, 7, 0.0}}, {{sustain_id, 7, 0.0}});

    current_case = "zero-frame CC123 and audible continuation";
    const State before = snapshot(*pair.actual.component);
    (void)process<Sample>(pair.actual, {}, {{command_base + 2, 0, 0.0}}, true, 0);
    ++flushes;
    require(snapshot(*pair.actual.component) == before, "zero-frame command is absent from saved state");
    // An explicit release at the next rendered sample is the public-interface
    // oracle for a zero-frame release that must not advance or reset history.
    pair.compare({}, {note_off(0, 2, 100, 12)});
    pair.compare({}, {});
}

template<class Sample>
void rejection_cases(Pair<Sample>& pair) {
    std::array<vst::Event, 10> malformed{};
    malformed.fill(pressure(9));
    malformed[0].busIndex = 1;
    malformed[1].sampleOffset = -1;
    malformed[2].sampleOffset = frames;
    malformed[3].sampleOffset = 0; // Follows the valid event at sample 1.
    malformed[4].ppqPosition = std::numeric_limits<double>::infinity();
    malformed[5].polyPressure.channel = 16;
    malformed[6].polyPressure.pitch = 128;
    malformed[7].polyPressure.pressure = std::numeric_limits<float>::quiet_NaN();
    malformed[8].polyPressure.pressure = -0.1F;
    malformed[9].polyPressure.pressure = 1.1F;
    for (std::size_t index = 0; index < malformed.size(); ++index) {
        current_case = "malformed pressure " + std::to_string(index);
        pair.reject({note_off(0, 4, 74, 40), note_on(1, 5, 81, 50), malformed[index]},
                    {{gain_id, 0, 0.125}, {sustain_id, 0, 1.0}, {command_base + 4, 0, 0.0}});
    }
    const std::array<Point, 5> invalid_commands{{
        {command_base + 5, 4, std::numeric_limits<double>::quiet_NaN()},
        {command_base + 5, 4, 1.25},
        {command_base + 5, -1, 0.0},
        {command_base + 5, frames, 0.0},
        {command_base + 16, 4, 0.0},
    }};
    for (std::size_t index = 0; index < invalid_commands.size(); ++index) {
        current_case = "malformed command " + std::to_string(index);
        pair.reject({note_off(0, 4, 74, 40), note_on(1, 5, 81, 50), pressure(9)},
                    {{gain_id, 0, 0.125}, {sustain_id, 0, 1.0},
                     {command_base + 4, 0, 0.0}, invalid_commands[index]});
    }
    current_case = "zero-frame nonzero command offset";
    pair.reject({}, {{command_base + 4, 1, 0.0}}, 0);
    current_case = "zero-frame pressure list rejection";
    pair.reject({pressure(0)}, {{command_base + 4, 0, 0.0}}, 0);
}

template<class Sample>
void exercise(const Module::Ptr& module, const Identity& identity, vst::HostApplication& host) {
    context = std::string{identity.name} + (std::is_same_v<Sample, float> ? "/float" : "/double");
    current_case = "load";
    Pair<Sample> pair;
    constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ? vst::kSample32 : vst::kSample64;
    pair.actual.open(module, identity, host, sample_size);
    pair.oracle.open(module, identity, host, sample_size);
    require(pair.actual.component.get() != pair.oracle.component.get() &&
                pair.actual.controller.get() != pair.oracle.controller.get(),
            "oracle and command paths use distinct factory instances");
    controller_contract(*pair.actual.controller);
    command_timeline(pair);
    rejection_cases(pair);
    current_case = "shutdown";
    pair.oracle.close();
    pair.actual.close();
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            throw std::runtime_error("usage: vst3_loaded_midi_test DIRECT_BUNDLE FILTERED_BUNDLE QUADRATURE_BUNDLE");
        }
        vst::HostApplication host;
        for (std::size_t index = 0; index < identities.size(); ++index) {
            context = identities[index].name;
            current_case = "module load";
            std::string error;
            auto module = Module::create(argv[index + 1], error);
            require(module != nullptr, "module load: " + error);
            exercise<float>(module, identities[index], host);
            exercise<double>(module, identities[index], host);
            require(module.use_count() == 1, "all instance owners released before module unload");
            module.reset();
        }
        require(flushes == 6 && rejected_blocks == 102 && rejected_texts == 1002,
                "all six format/variant fixture sets executed");
        std::cout << "PASS loaded MIDI: 3 modules, 2 formats, " << comparisons << " exact audio pairs, "
                  << callbacks << " loaded callbacks, " << flushes << " zero-frame releases, "
                  << rejected_blocks << " rejected blocks, " << rejected_texts << " rejected texts, "
                  << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL loaded MIDI: " << error.what() << '\n';
        return 1;
    }
}
