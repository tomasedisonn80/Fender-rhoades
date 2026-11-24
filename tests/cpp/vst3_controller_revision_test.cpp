#include "vst3_test_support.hpp"

#include "base/source/updatehandler.h"

#include <bit>
#include <iostream>
#include <limits>

namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
namespace plugin = rhodes::vst3;
namespace host = plugin::test_host;

using Revisions = std::array<std::uint64_t, 8>;
constexpr std::array<vst::ParamID, 8> persistent_ids{0, 1, 2, 3, 4, 5, 6, 7};
constexpr std::array<vst::ParamID, 4> unknown_ids{
    8, 0xfff, 0x1010, std::numeric_limits<vst::ParamID>::max()};

Revisions revisions(const plugin::RhodesController& controller) {
    Revisions result{};
    for (const auto id : persistent_ids) { result[id] = controller.parameterRevision(id); }
    return result;
}

void expect_increment(const plugin::RhodesController& controller, Revisions before,
                      vst::ParamID id) {
    ++before[id];
    host::require(revisions(controller) == before, "setter did not advance only its own stamp");
}

void setter_cases() {
    plugin::RhodesController uninitialized;
    for (const auto id : persistent_ids) {
        host::require(uninitialized.setParamNormalized(id, 0.25) == sdk::kInvalidArgument,
                      "uninitialized setter was accepted");
    }
    host::require(revisions(uninitialized) == Revisions{}, "uninitialized write advanced a stamp");

    host::ControllerHost instance;
    auto& controller = instance.controller;
    host::require(revisions(controller) == Revisions{}, "initialization advanced a stamp");
    for (const auto id : persistent_ids) {
        const auto value = controller.getParamNormalized(id);
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            const auto before = revisions(controller);
            host::require(controller.setParamNormalized(id, value) == sdk::kResultOk,
                          "equal-value setter was rejected");
            expect_increment(controller, before, id);
            host::require(controller.getParamNormalized(id) == value, "equal write changed value");
        }
        const auto before = revisions(controller);
        host::require(controller.setParamNormalized(id, value == 0.0 ? 1.0 : 0.0) == sdk::kResultOk,
                      "changed-value setter was rejected");
        expect_increment(controller, before, id);
    }

    // Distinct binary64 inputs can resolve to the same persistent binary32 value.
    for (const auto id : {vst::ParamID{0}, vst::ParamID{2}, vst::ParamID{3},
                          vst::ParamID{4}, vst::ParamID{5}, vst::ParamID{6}}) {
        host::require(controller.setParamNormalized(id, 0.25) == sdk::kResultOk, "seed failed");
        const auto before = revisions(controller);
        host::require(controller.setParamNormalized(id, std::bit_cast<double>(0x3fd0000000000001ULL))
                      == sdk::kResultOk, "canonical equal-value setter was rejected");
        expect_increment(controller, before, id);
        host::require(controller.getParamNormalized(id) == 0.25, "canonical value changed");
    }
    for (const auto id : {vst::ParamID{1}, vst::ParamID{7}}) {
        host::require(controller.setParamNormalized(id, 0.0) == sdk::kResultOk, "boolean seed failed");
        const auto before = revisions(controller);
        host::require(controller.setParamNormalized(id, 0.25) == sdk::kResultOk,
                      "canonical equal boolean was rejected");
        expect_increment(controller, before, id);
        host::require(controller.getParamNormalized(id) == 0.0, "canonical boolean changed");
    }

    const auto before = revisions(controller);
    constexpr std::array<std::uint64_t, 8> invalid_values{
        0xbff0000000000000ULL, 0x8000000000000001ULL, 0x3ff0000000000001ULL,
        0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff0000000000001ULL,
        0x7ff80000000012abULL, 0xfff80000000012abULL};
    for (const auto id : persistent_ids) {
        const auto value = controller.getParamNormalized(id);
        for (const auto invalid : invalid_values) {
            host::require(controller.setParamNormalized(id, std::bit_cast<double>(invalid))
                          == sdk::kInvalidArgument, "malformed setter was accepted");
            host::require(revisions(controller) == before, "malformed setter advanced a stamp");
            host::require(controller.getParamNormalized(id) == value, "malformed setter changed value");
        }
    }
    for (const auto id : unknown_ids) {
        host::require(controller.parameterRevision(id) == 0, "unknown ID has a stamp");
        host::require(controller.setParamNormalized(id, 0.5) == sdk::kInvalidArgument,
                      "unknown setter was accepted");
        host::require(revisions(controller) == before, "unknown setter advanced a stamp");
    }
    for (vst::ParamID id = 0x1000; id < 0x1010; ++id) {
        host::require(controller.setParamNormalized(id, 0.25) == sdk::kResultOk,
                      "valid command setter was rejected");
        host::require(controller.parameterRevision(id) == 0, "transient command has a stamp");
        host::require(revisions(controller) == before, "command advanced a persistent stamp");
    }

    host::ControllerHost sibling;
    host::require(revisions(sibling.controller) == Revisions{}, "stamps leaked between instances");
    host::require(controller.terminate() == sdk::kResultOk, "terminate failed");
    for (const auto id : persistent_ids) {
        host::require(controller.setParamNormalized(id, 0.5) == sdk::kInvalidArgument,
                      "terminated setter was accepted");
    }
    host::require(revisions(controller) == before, "terminated setter advanced a stamp");
}

#if defined(RHODES_QUADRATURE_VST3)
constexpr std::uint8_t model = 3;
#elif defined(RHODES_FILTERED_VST3)
constexpr std::uint8_t model = 2;
#else
constexpr std::uint8_t model = 1;
#endif
// Literal public packets keep recall expectations independent of the encoder.
constexpr std::array<std::uint8_t, 16> legacy_v1{
    'R', 'H', 'S', 'T', 1, 0, model, 0,
    0, 0, 0x80, 0x3e, 0, 0, 0x80, 0x3f};
constexpr std::array<std::uint8_t, 20> legacy_v2{
    'R', 'H', 'S', 'T', 2, 0, model, 0,
    0, 0, 0x80, 0x3e, 0, 0, 0x80, 0x3f, 0, 0, 0x40, 0x3f};
constexpr std::array<std::uint8_t, 40> panel_v3{
    'R', 'H', 'S', 'T', 3, 0, model, 0,
    0, 0, 0x80, 0x3e, 0, 0, 0x80, 0x3f, 0, 0, 0x40, 0x3f,
    0, 0, 0, 0x3e, 0, 0, 0x60, 0x3f, 0, 0, 0, 0x3f,
    0, 0, 0x80, 0x3e, 0, 0, 0x80, 0x3f};

void load(host::MemoryStream& stream, std::span<const std::uint8_t> packet) {
    std::copy(packet.begin(), packet.end(), stream.bytes.begin());
    stream.position = 0;
    stream.extent = static_cast<sdk::int64>(packet.size());
}

void recall_cases() {
    host::ControllerHost instance;
    auto& controller = instance.controller;
    for (const auto packet : {std::span<const std::uint8_t>{legacy_v1},
                              std::span<const std::uint8_t>{legacy_v2},
                              std::span<const std::uint8_t>{panel_v3}}) {
        for (const bool component : {true, false}) {
            // Repeat the same recall to require stamps even when no value changes.
            for (unsigned repeat = 0; repeat < 2; ++repeat) {
                auto expected = revisions(controller);
                for (auto& revision : expected) { ++revision; }
                host::MemoryStream stream;
                load(stream, packet);
                host::require((component ? controller.setComponentState(&stream) :
                                           controller.setState(&stream)) == sdk::kResultOk,
                              "valid recall was rejected");
                host::require(revisions(controller) == expected, "recall did not advance every stamp once");
            }
        }
    }
    const auto before = revisions(controller);
    for (const bool component : {true, false}) {
        const auto recall = [&](sdk::IBStream* stream) {
            return component ? controller.setComponentState(stream) : controller.setState(stream);
        };
        host::require(recall(nullptr) != sdk::kResultOk, "null recall was accepted");
        host::require(revisions(controller) == before, "null recall advanced stamps");
        for (const auto extent : {std::size_t{0}, std::size_t{39}, panel_v3.size()}) {
            host::MemoryStream stream;
            load(stream, panel_v3);
            stream.extent = static_cast<sdk::int64>(extent);
            if (extent == panel_v3.size()) { stream.bytes[39] = 0x7f; }
            host::require(recall(&stream) != sdk::kResultOk, "malformed recall was accepted");
            host::require(revisions(controller) == before, "malformed recall advanced stamps");
        }
    }
    host::MemoryStream saved;
    host::require(controller.getState(&saved) == sdk::kResultOk, "save failed");
    host::require(revisions(controller) == before, "save advanced stamps");
    host::require(saved.extent == 40 && std::equal(panel_v3.begin(), panel_v3.end(), saved.bytes.begin()),
                  "mutation stamps changed the serialized state");
}

struct ScopedUpdates {
    sdk::IUpdateHandler* previous = sdk::FObject::getUpdateHandler();
    sdk::UpdateHandler handler;
    ScopedUpdates() { sdk::FObject::setUpdateHandler(&handler); }
    ~ScopedUpdates() { sdk::FObject::setUpdateHandler(previous); }
};

struct Observer final : host::FixedHostObject<sdk::IDependent> {
    plugin::RhodesController& controller;
    vst::Parameter* parameter;
    std::array<std::uint64_t, 4> entry{};
    std::array<std::uint64_t, 4> exit{};
    std::array<double, 4> values{};
    std::size_t count = 0;
    bool overflow = false;
    bool reenter = false;
    vst::ParamID nested_id = 3;
    double nested_value = 0.0;
    sdk::tresult nested_result = sdk::kResultFalse;

    explicit Observer(plugin::RhodesController& value)
        : controller(value), parameter(controller.getParameterObject(3)) {
        host::require(parameter != nullptr, "observed parameter is missing");
        parameter->addDependent(this);
    }
    ~Observer() { parameter->removeDependent(this); }

    void PLUGIN_API update(sdk::FUnknown*, sdk::int32 message) override {
        if (message != sdk::IDependent::kChanged) { return; }
        if (count == entry.size()) { overflow = true; return; }
        const auto index = count++;
        entry[index] = controller.parameterRevision(3);
        values[index] = controller.getParamNormalized(3);
        if (reenter) {
            reenter = false;
            nested_result = controller.setParamNormalized(nested_id, nested_value);
        }
        exit[index] = controller.parameterRevision(3);
    }
};

void observer_cases() {
    ScopedUpdates updates;
    host::ControllerHost instance;
    auto& controller = instance.controller;
    Observer observer(controller);
    host::require(controller.setParamNormalized(3, 0.25) == sdk::kResultOk, "observed setter failed");
    host::require(observer.count == 1 && observer.entry[0] == 1 && observer.exit[0] == 1 &&
                  observer.values[0] == 0.25, "observer ran before the mutation stamp advanced");
    host::require(controller.setParamNormalized(3, 0.25) == sdk::kResultOk &&
                  controller.parameterRevision(3) == 2 && observer.count == 1,
                  "equal write stamp or SDK notification semantics changed");

    observer.reenter = true;
    observer.nested_value = 0.75;
    host::require(controller.setParamNormalized(3, 0.75) == sdk::kResultOk, "outer equal-nested write failed");
    host::require(observer.nested_result == sdk::kResultOk && observer.count == 2 &&
                  observer.entry[1] == 3 && observer.exit[1] == 4 &&
                  controller.parameterRevision(3) == 4 && controller.getParamNormalized(3) == 0.75,
                  "nested equal write was not distinguishable from the outer write");

    observer.reenter = true;
    observer.nested_value = 0.5;
    host::require(controller.setParamNormalized(3, 0.25) == sdk::kResultOk, "outer changed-nested write failed");
    host::require(observer.nested_result == sdk::kResultOk && observer.count == 4 && !observer.overflow &&
                  observer.entry[2] == 5 && observer.entry[3] == 6 &&
                  observer.exit[2] == 6 && observer.exit[3] == 6 &&
                  observer.values[2] == 0.25 && observer.values[3] == 0.5 &&
                  controller.parameterRevision(3) == 6 && controller.getParamNormalized(3) == 0.5,
                  "nested changed-value observer sequence lost a mutation stamp");

    observer.count = 0;
    observer.reenter = true;
    observer.nested_id = 4;
    observer.nested_value = 0.625;
    host::require(controller.setParamNormalized(3, 0.75) == sdk::kResultOk, "outer other-parameter write failed");
    host::require(observer.nested_result == sdk::kResultOk && observer.count == 1 &&
                  observer.entry[0] == 7 && observer.exit[0] == 7 &&
                  controller.parameterRevision(4) == 1,
                  "nested other-parameter write changed the outer parameter stamp");
}
} // namespace

int main() {
    try {
        setter_cases();
        recall_cases();
        observer_cases();
        std::cout << "Controller mutation revision checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
