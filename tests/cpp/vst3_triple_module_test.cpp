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

// This host links only SDK loader support. It neither includes the instrument
// headers nor links a core or variant processor implementation into the host.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Module = VST3::Hosting::Module;
using State = std::array<std::uint8_t, 16>;
using Paths = std::array<const char*, 3>;
constexpr std::size_t frames = 128;

std::size_t checks = 0;
std::size_t callbacks = 0;
std::size_t rejected_pairs = 0;

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

void require_distinct(const std::array<Module::Ptr, 3>& modules) {
    std::set<std::string> ids;
    for (std::size_t index = 0; index < modules.size(); ++index) {
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (modules[index]->getFactory().get().get() ==
                modules[prior]->getFactory().get().get()) {
                throw IdentityCollision("bundle handles alias one factory");
            }
        }
        require(modules[index]->getFactory().classCount() == 2,
                "exactly two factory classes per bundle");
        for (const auto& info : modules[index]->getFactory().classInfos()) {
            if (!ids.insert(info.ID().toString()).second) {
                throw IdentityCollision("duplicate class identity across modules");
            }
        }
    }
    require(ids.size() == 6, "six distinct processor/controller class identities");
}

struct Expected {
    const char* processor;
    const char* controller;
    const char* processor_name;
    const char* controller_name;
    const char* version;
    std::uint16_t model;
    sdk::uint32 latency;
};

// Frozen external identities; compile-time instrument macros cannot alter them.
constexpr std::array<Expected, 3> expected{{
    {"BDDA70F46F9244C1A5639EE371A0B804", "EA801E8B97864ED9B3A0E6572D81CE61",
     "Rhodes Research Prototype", "Rhodes Research Controller", "0.1.0", 1, 0},
    {"4C0E773A24FC4BE9B804BC71B8B98B33", "0569EDDBD21D4568A9ED52C3D8545B6D",
     "Rhodes Research Filtered", "Rhodes Filtered Controller", "0.2.0", 2, 48},
    {"D73B886A72E04FC892435F1DC5F0B6A9", "A45F23E1B68A4D79AF31472C8E909D52",
     "Rhodes Research Quadrature", "Rhodes Quadrature Controller", "0.3.0", 3, 48},
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
                require(info.name() == identity.processor_name, "processor factory name");
                require(component == nullptr, "single processor class");
                component = module.getFactory().createInstance<vst::IComponent>(info.ID());
            } else if (info.category() == kVstComponentControllerClass) {
                require(info.ID().toString() == identity.controller, "controller class identity");
                require(info.name() == identity.controller_name, "controller factory name");
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
        vst::ProcessSetup setup{vst::kRealtime, sample_size, static_cast<sdk::int32>(frames), rate};
        require(audio->setupProcessing(setup) == sdk::kResultOk, "setup loaded processor");
        require(component->setActive(true) == sdk::kResultOk, "activate loaded processor");
        active_ = true;
        require(audio->setProcessing(true) == sdk::kResultOk, "start loaded processor");
        processing_ = true;
        const State state = snapshot(*component);
        require(state[0] == 'R' && state[1] == 'H' && state[2] == 'S' && state[3] == 'T' &&
                    state[4] == 1 && state[5] == 0, "state magic and wire version");
        require(static_cast<unsigned>(state[6]) + 256U * state[7] == identity.model,
                "state model identity");
        require(snapshot(*controller) == state, "controller and processor default state");
        require(controller->getParameterCount() == 24 &&
                    controller->getParamNormalized(0) == 0.5 &&
                    controller->getParamNormalized(1) == 0.0 &&
                    controller->getParamNormalized(2) == 1.0,
                "unchanged public parameter identities and defaults");
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

    void recall(State state) {
        sdk::MemoryStream component_stream{state.data(), static_cast<sdk::TSize>(state.size())};
        sdk::MemoryStream controller_stream{state.data(), static_cast<sdk::TSize>(state.size())};
        require(component->setState(&component_stream) == sdk::kResultOk, "queue own model recall");
        require(controller->setComponentState(&controller_stream) == sdk::kResultOk,
                "load own controller state");
        require(snapshot(*component) == state, "pending recall is visible in saved state");
        require(snapshot(*controller) == state, "controller has matching state");
    }

    void reject_other_state(State other) {
        const State before_component = snapshot(*component);
        const State before_controller = snapshot(*controller);
        sdk::MemoryStream component_stream{other.data(), static_cast<sdk::TSize>(other.size())};
        sdk::MemoryStream controller_stream{other.data(), static_cast<sdk::TSize>(other.size())};
        require(component->setState(&component_stream) == sdk::kResultFalse,
                "processor rejects the other model's actual state");
        require(controller->setComponentState(&controller_stream) == sdk::kResultFalse,
                "controller rejects the other model's actual state");
        require(snapshot(*component) == before_component, "rejected processor state is atomic");
        require(snapshot(*controller) == before_controller, "rejected controller state is atomic");
        ++rejected_pairs;
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
std::array<Sample, frames> process(Instance& instance, Note note, bool expect_silence) {
    constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ?
        vst::kSample32 : vst::kSample64;
    std::array<Sample, frames> left{}, right{};
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
    data.numSamples = static_cast<sdk::int32>(frames);
    data.numOutputs = 1;
    data.outputs = &output;
    data.inputEvents = &events;
    ++callbacks;
    require(instance.audio->process(data) == sdk::kResultOk, "loaded audio callback");
    bool nonzero = false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        require(std::isfinite(left[index]) && std::isfinite(right[index]), "finite output samples");
        require(left[index] == right[index], "identical mono-derived stereo channels");
        nonzero = nonzero || left[index] != Sample{0};
    }
    require(nonzero != expect_silence, "expected independent instrument activity");
    require(output.silenceFlags == (expect_silence ? 3U : 0U), "accurate stereo silence flags");
    return left;
}

template<class Sample>
using Traces = std::array<std::array<Sample, frames>, 3>;

template<class Sample>
Traces<Sample> coexist_once(const Paths& paths, double rate,
                           const std::array<std::size_t, 3>& order) {
    vst::HostApplication host;
    std::array<Module::Ptr, 3> modules;
    for (const std::size_t index : order) { modules[index] = load(paths[index]); }
    require_distinct(modules);
    Traces<Sample> traces{};
    {
        std::array<Instance, 3> instances;
        std::array<State, 3> defaults{};
        constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ?
            vst::kSample32 : vst::kSample64;
        for (std::size_t index = 0; index < instances.size(); ++index) {
            instances[index].open(*modules[index], expected[index], host, sample_size, rate);
            defaults[index] = Instance::snapshot(*instances[index].component);
            (void)process<Sample>(instances[index], Note::none, true);
            traces[index] = process<Sample>(instances[index], Note::on, false);
        }
        require(traces[0] != traces[1] && traces[0] != traces[2] && traces[1] != traces[2],
                "three loaded paths produce distinct strike traces");

        for (std::size_t target = 0; target < instances.size(); ++target) {
            for (std::size_t source = 0; source < instances.size(); ++source) {
                if (source == target) { continue; }
                Instance& instance = instances[target];
                instance.recall(defaults[target]);
                (void)process<Sample>(instance, Note::none, true);
                require(process<Sample>(instance, Note::on, false) == traces[target],
                        "valid default recall restores exact own strike trace");

                // Construct the public 16-byte RHST packet independently:
                // binary32 gain .25 (half the default) and sustain 1.0.
                State pending = defaults[target];
                pending[8] = 0; pending[9] = 0; pending[10] = 0x80; pending[11] = 0x3E;
                pending[12] = 0; pending[13] = 0; pending[14] = 0x80; pending[15] = 0x3F;
                instance.recall(pending);
                instance.reject_other_state(defaults[source]);
                // A discarded pending flag would leave the preceding note or
                // FIR tail audible here; a discarded gain would fail below.
                (void)process<Sample>(instance, Note::none, true);
                require(Instance::snapshot(*instance.component) == pending,
                        "valid pending recall survives foreign rejection and callback");
                const auto recalled = process<Sample>(instance, Note::on, false);
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    require(recalled[frame] == traces[target][frame] * Sample{0.5},
                            "half-gain pending recall applies to the exact own model trace");
                }
                (void)process<Sample>(instance, Note::off, false);
            }
        }

        // Each reset/strike must affect only its own loaded module. Advance all
        // peers on every iteration so activity cannot hide in uncalled modules.
        for (std::size_t index = 0; index < instances.size(); ++index) {
            instances[index].recall(defaults[index]);
            (void)process<Sample>(instances[index], Note::none, true);
        }
        for (std::size_t struck = 0; struck < instances.size(); ++struck) {
            for (std::size_t index = 0; index < instances.size(); ++index) {
                (void)process<Sample>(instances[index], index == struck ? Note::on : Note::none,
                                      index > struck);
            }
        }
        for (std::size_t cleared = 0; cleared < instances.size(); ++cleared) {
            instances[cleared].reset();
            for (std::size_t index = 0; index < instances.size(); ++index) {
                (void)process<Sample>(instances[index], Note::none, index <= cleared);
            }
        }
        for (auto index = order.rbegin(); index != order.rend(); ++index) {
            instances[*index].close();
        }
    }
    for (auto index = order.rbegin(); index != order.rend(); ++index) {
        require(modules[*index].use_count() == 1, "no retained module ownership before unload");
        modules[*index].reset();
    }
    return traces;
}

template<class Sample>
void all_load_orders(const Paths& paths, double rate) {
    std::array<std::size_t, 3> order{0, 1, 2};
    const auto baseline = coexist_once<Sample>(paths, rate, order);
    std::size_t orders = 1;
    while (std::next_permutation(order.begin(), order.end())) {
        require(coexist_once<Sample>(paths, rate, order) == baseline,
                "exact output is independent of physical module load order");
        ++orders;
    }
    require(orders == 6, "all six module load orders executed");
}

void duplicate_guard(const Paths& paths) {
    for (std::size_t repeated = 0; repeated < paths.size(); ++repeated) {
        for (std::size_t unique_slot = 0; unique_slot < paths.size(); ++unique_slot) {
            std::array<Module::Ptr, 3> modules;
            for (std::size_t index = 0; index < modules.size(); ++index) {
                modules[index] = load(paths[index == unique_slot ?
                    (repeated + 1) % paths.size() : repeated]);
            }
            bool detected = false;
            try { require_distinct(modules); }
            catch (const IdentityCollision&) { detected = true; }
            require(detected, "repeated bundle must be rejected in every pair of slots");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5) {
            throw std::runtime_error("usage: vst3_triple_module_test coexist|duplicate_guard DIRECT_BUNDLE FILTERED_BUNDLE QUADRATURE_BUNDLE");
        }
        const Paths paths{argv[2], argv[3], argv[4]};
        if (std::string_view{argv[1]} == "coexist") {
            for (const double rate : {48000.0, 192000.0}) {
                all_load_orders<float>(paths, rate);
                all_load_orders<double>(paths, rate);
            }
            require(rejected_pairs == 144, "six ordered foreign recalls in each of 24 sessions");
        } else if (std::string_view{argv[1]} == "duplicate_guard") {
            duplicate_guard(paths);
        } else {
            throw std::runtime_error("unknown triple-module selector");
        }
        std::cout << "PASS triple module " << argv[1] << ": " << checks
                  << " checks, " << callbacks << " loaded callbacks, " << rejected_pairs
                  << " rejected foreign recalls\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL triple module: " << error.what() << '\n';
        return 1;
    }
}
