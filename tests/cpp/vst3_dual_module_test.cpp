#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

// Deliberately link only SDK host support: every instrument interface and
// callback below comes from an independently loaded production module.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Module = VST3::Hosting::Module;
using State = std::array<std::uint8_t, 16>;

std::size_t checks = 0;
std::size_t callbacks = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

struct IdentityCollision : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Module::Ptr load(const char* path) {
    std::string error;
    auto module = Module::create(path, error);
    require(module != nullptr, "module load: " + error);
    return module;
}

void require_distinct(const std::array<Module::Ptr, 2>& modules) {
    if (modules[0]->getFactory().get().get() == modules[1]->getFactory().get().get()) {
        throw IdentityCollision("two bundle handles alias one factory");
    }
    std::set<std::string> ids;
    for (const auto& module : modules) {
        require(module->getFactory().classCount() == 2, "exactly two factory classes");
        for (const auto& info : module->getFactory().classInfos()) {
            if (!ids.insert(info.ID().toString()).second) {
                throw IdentityCollision("duplicate class identity across modules");
            }
        }
    }
    require(ids.size() == 4, "four distinct processor/controller class identities");
}

struct Expected {
    const char* processor;
    const char* controller;
    const char* version;
    std::uint16_t model;
    sdk::uint32 latency;
};

// Frozen public identities, independent of the variant's compile-time macros.
constexpr std::array<Expected, 2> expected{{
    {"BDDA70F46F9244C1A5639EE371A0B804", "EA801E8B97864ED9B3A0E6572D81CE61",
     "0.1.0", 1, 0},
    {"4C0E773A24FC4BE9B804BC71B8B98B33", "0569EDDBD21D4568A9ED52C3D8545B6D",
     "0.2.0", 2, 48},
}};

class Instance {
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

    void open(const Module& module, const Expected& identity, vst::HostApplication& host,
              sdk::int32 sample_size, double rate) {
        for (const auto& info : module.getFactory().classInfos()) {
            require(info.version() == identity.version, "factory version");
            if (info.category() == kVstAudioEffectClass) {
                require(info.ID().toString() == identity.processor, "processor class identity");
                require(component == nullptr, "single processor class");
                component = module.getFactory().createInstance<vst::IComponent>(info.ID());
            } else if (info.category() == kVstComponentControllerClass) {
                require(info.ID().toString() == identity.controller, "controller class identity");
                require(controller == nullptr, "single controller class");
                controller = module.getFactory().createInstance<vst::IEditController>(info.ID());
            } else {
                require(false, "unexpected factory category");
            }
        }
        require(component != nullptr && controller != nullptr, "both factory instances");
        vst::IAudioProcessor* queried = nullptr;
        require(component->queryInterface(vst::IAudioProcessor::iid,
                    reinterpret_cast<void**>(&queried)) == sdk::kResultOk && queried != nullptr,
                "audio interface on the same component instance");
        audio = sdk::owned(queried);
        require(component->initialize(&host) == sdk::kResultOk, "component initialize");
        component_initialized_ = true;
        require(controller->initialize(&host) == sdk::kResultOk, "controller initialize");
        controller_initialized_ = true;
        sdk::TUID controller_id{};
        require(component->getControllerClassId(controller_id) == sdk::kResultOk &&
                    VST3::UID{controller_id}.toString() == identity.controller,
                "component advertises its own controller");
        require(audio->getLatencySamples() == identity.latency, "declared variant latency");
        require(audio->getTailSamples() == vst::kInfiniteTail, "asymptotic tail contract");
        require(component->getBusCount(vst::kAudio, vst::kInput) == 0 &&
                    component->getBusCount(vst::kAudio, vst::kOutput) == 1 &&
                    component->getBusCount(vst::kEvent, vst::kInput) == 1,
                "instrument bus layout");
        require(component->activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk,
                "activate stereo output");
        require(component->activateBus(vst::kEvent, vst::kInput, 0, true) == sdk::kResultOk,
                "activate event input");
        vst::ProcessSetup setup{vst::kRealtime, sample_size, 128, rate};
        require(audio->setupProcessing(setup) == sdk::kResultOk, "setup loaded processor");
        require(component->setActive(true) == sdk::kResultOk, "activate loaded processor");
        active_ = true;
        require(audio->setProcessing(true) == sdk::kResultOk, "start loaded processor");
        processing_ = true;
        const auto state = snapshot(*component);
        require(state[0] == 'R' && state[1] == 'H' && state[2] == 'S' && state[3] == 'T' &&
                    state[4] == 1 && state[5] == 0, "state magic and wire version");
        require(static_cast<unsigned>(state[6]) + 256U * state[7] == identity.model,
                "state model identity");
        require(snapshot(*controller) == state, "controller and processor default state");
    }

    template<class Interface>
    static State snapshot(Interface& object) {
        sdk::MemoryStream stream;
        require(object.getState(&stream) == sdk::kResultOk, "read loaded instance state");
        require(stream.getSize() == 16, "exact state length");
        State state{};
        std::copy_n(reinterpret_cast<const std::uint8_t*>(stream.getData()),
                    state.size(), state.begin());
        return state;
    }

    void reject_other_state(State other) {
        const auto before_component = snapshot(*component);
        const auto before_controller = snapshot(*controller);
        sdk::MemoryStream component_stream{other.data(), static_cast<sdk::TSize>(other.size())};
        sdk::MemoryStream controller_stream{other.data(), static_cast<sdk::TSize>(other.size())};
        require(component->setState(&component_stream) == sdk::kResultFalse,
                "processor rejects the other model's actual state");
        require(controller->setComponentState(&controller_stream) == sdk::kResultFalse,
                "controller rejects the other model's actual state");
        require(snapshot(*component) == before_component, "rejected processor state is atomic");
        require(snapshot(*controller) == before_controller, "rejected controller state is atomic");
    }

    void reset() {
        require(audio->setProcessing(false) == sdk::kResultOk, "stop for reset");
        processing_ = false;
        require(audio->setProcessing(true) == sdk::kResultOk, "restart for reset");
        processing_ = true;
    }

    void close() {
        require(audio->setProcessing(false) == sdk::kResultOk, "stop loaded processor");
        processing_ = false;
        require(component->setActive(false) == sdk::kResultOk, "deactivate loaded processor");
        active_ = false;
        require(controller->terminate() == sdk::kResultOk, "terminate loaded controller");
        controller_initialized_ = false;
        require(component->terminate() == sdk::kResultOk, "terminate loaded component");
        component_initialized_ = false;
        controller = nullptr;
        audio = nullptr;
        component = nullptr;
    }

    sdk::IPtr<vst::IComponent> component;
    sdk::IPtr<vst::IEditController> controller;
    sdk::IPtr<vst::IAudioProcessor> audio;

private:
    bool component_initialized_ = false;
    bool controller_initialized_ = false;
    bool active_ = false;
    bool processing_ = false;
};

enum class Note { none, on, off };

template<class Sample>
void process(Instance& instance, Note note, bool expect_silence) {
    constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ?
        vst::kSample32 : vst::kSample64;
    std::array<Sample, 128> left{}, right{};
    left.fill(Sample{123});
    right.fill(Sample{456});
    std::array<Sample*, 2> channels{left.data(), right.data()};
    vst::AudioBusBuffers output{};
    output.numChannels = 2;
    output.silenceFlags = 0xFF;
    if constexpr (std::is_same_v<Sample, float>) { output.channelBuffers32 = channels.data(); }
    else { output.channelBuffers64 = channels.data(); }
    vst::EventList events{1};
    if (note != Note::none) {
        vst::Event event{};
        if (note == Note::on) {
            event.type = vst::Event::kNoteOnEvent;
            event.noteOn.pitch = 68;
            event.noteOn.velocity = 0.75F;
            event.noteOn.noteId = 77;
        } else {
            event.type = vst::Event::kNoteOffEvent;
            event.noteOff.pitch = 68;
            event.noteOff.noteId = 77;
        }
        require(events.addEvent(event) == sdk::kResultOk, "prepare SDK event list");
    }
    vst::ProcessData data{};
    data.processMode = vst::kRealtime;
    data.symbolicSampleSize = sample_size;
    data.numSamples = 128;
    data.numOutputs = 1;
    data.outputs = &output;
    data.inputEvents = &events;
    ++callbacks;
    require(instance.audio->process(data) == sdk::kResultOk, "loaded audio callback");
    bool nonzero = false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        require(std::isfinite(left[i]) && std::isfinite(right[i]), "finite output samples");
        require(left[i] == right[i], "identical mono-derived stereo channels");
        nonzero = nonzero || left[i] != Sample{0};
    }
    require(nonzero != expect_silence, "expected independent instrument activity");
    require(output.silenceFlags == (expect_silence ? 3U : 0U), "accurate stereo silence flags");
}

template<class Sample>
void coexist(const char* direct, const char* filtered, double rate, bool reverse_load) {
    vst::HostApplication host;
    std::array<Module::Ptr, 2> modules;
    // Reverse physical loading order on the second pass to catch symbol-order dependence.
    if (reverse_load) { modules[1] = load(filtered); modules[0] = load(direct); }
    else { modules[0] = load(direct); modules[1] = load(filtered); }
    require_distinct(modules);
    {
        std::array<Instance, 2> instances;
        constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ?
            vst::kSample32 : vst::kSample64;
        for (std::size_t i = 0; i < instances.size(); ++i) {
            instances[i].open(*modules[i], expected[i], host, sample_size, rate);
        }
        const auto direct_state = Instance::snapshot(*instances[0].component);
        const auto filtered_state = Instance::snapshot(*instances[1].component);
        instances[0].reject_other_state(filtered_state);
        instances[1].reject_other_state(direct_state);
        process<Sample>(instances[0], Note::none, true);
        process<Sample>(instances[1], Note::none, true);
        process<Sample>(instances[0], Note::on, false);
        process<Sample>(instances[1], Note::none, true);
        process<Sample>(instances[0], Note::none, false);
        process<Sample>(instances[1], Note::on, false);
        instances[0].reset();
        process<Sample>(instances[0], Note::none, true);
        process<Sample>(instances[1], Note::none, false);
        process<Sample>(instances[0], Note::none, true);
        process<Sample>(instances[1], Note::off, false);
        instances[1].reset();
        process<Sample>(instances[0], Note::none, true);
        process<Sample>(instances[1], Note::none, true);
        instances[1].close();
        instances[0].close();
    }
    // All plugin interfaces are released before the SDK Module destructor runs
    // ModuleExit/dlclose. The next pass loads both complete bundles again.
    require(modules[0].use_count() == 1 && modules[1].use_count() == 1,
            "no retained module ownership before unload");
    modules[1].reset();
    modules[0].reset();
}

void duplicate_guard(const char* direct) {
    const std::array<Module::Ptr, 2> modules{load(direct), load(direct)};
    bool detected = false;
    try { require_distinct(modules); }
    catch (const IdentityCollision&) { detected = true; }
    require(detected, "duplicate-bundle negative guard must detect identity collision");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view{argv[1]} == "coexist") {
            coexist<float>(argv[2], argv[3], 48000.0, false);
            coexist<double>(argv[2], argv[3], 96000.0, true);
        } else if (argc == 3 && std::string_view{argv[1]} == "duplicate_guard") {
            duplicate_guard(argv[2]);
        } else {
            throw std::runtime_error("usage: vst3_dual_module_test coexist DIRECT_BUNDLE FILTERED_BUNDLE | duplicate_guard DIRECT_BUNDLE");
        }
        std::cout << "PASS dual module " << argv[1] << ": " << checks
                  << " checks, " << callbacks << " loaded callbacks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL dual module: " << error.what() << '\n';
        return 1;
    }
}
