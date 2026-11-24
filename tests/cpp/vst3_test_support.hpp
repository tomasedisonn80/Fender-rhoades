#pragma once

#include "plugins/vst3/controller.hpp"
#include "plugins/vst3/processor.hpp"
#include "plugins/vst3/state_stream.hpp"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace rhodes::vst3::test_host {

namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;

// Unlike assert(), these checks remain active in Release/NDEBUG builds. They
// are evaluated outside process(); host callbacks below never allocate.
inline void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

// Host-owned, fixed-lifetime objects. Reference counting deliberately does not
// delete their storage; queryInterface still obeys the SDK interface IDs.
template <class Interface>
class FixedHostObject : public Interface {
public:
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

struct EventList final : FixedHostObject<vst::IEventList> {
    std::array<vst::Event, maximum_block_events> values{};
    sdk::int32 count = 0;
    sdk::int32 failed_index = -1;
    std::size_t get_calls = 0;

    sdk::int32 PLUGIN_API getEventCount() override { return count; }
    sdk::tresult PLUGIN_API getEvent(sdk::int32 index, vst::Event& event) override {
        ++get_calls;
        if (index < 0 || index >= count || index == failed_index ||
            static_cast<std::size_t>(index) >= values.size()) {
            return sdk::kResultFalse;
        }
        event = values[static_cast<std::size_t>(index)];
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addEvent(vst::Event&) override { return sdk::kNotImplemented; }
};

struct ParameterQueue final : FixedHostObject<vst::IParamValueQueue> {
    struct Point {
        sdk::int32 offset = 0;
        vst::ParamValue value = 0.0;
    };
    std::array<Point, kMaximumHostFrames> values{};
    vst::ParamID id = kGainId;
    sdk::int32 count = 0;
    sdk::int32 failed_index = -1;
    std::size_t get_calls = 0;

    vst::ParamID PLUGIN_API getParameterId() override { return id; }
    sdk::int32 PLUGIN_API getPointCount() override { return count; }
    sdk::tresult PLUGIN_API getPoint(sdk::int32 index, sdk::int32& offset,
                                    vst::ParamValue& value) override {
        ++get_calls;
        if (index < 0 || index >= count || index == failed_index ||
            static_cast<std::size_t>(index) >= values.size()) {
            return sdk::kResultFalse;
        }
        const auto& point = values[static_cast<std::size_t>(index)];
        offset = point.offset;
        value = point.value;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addPoint(sdk::int32, vst::ParamValue, sdk::int32&) override {
        return sdk::kNotImplemented;
    }
};

struct ParameterChanges final : FixedHostObject<vst::IParameterChanges> {
    // Legacy gain/sustain fixture capacity, not the public parameter-queue limit.
    std::array<vst::IParamValueQueue*, 2> values{};
    sdk::int32 count = 0;

    sdk::int32 PLUGIN_API getParameterCount() override { return count; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        return index >= 0 && index < count &&
               static_cast<std::size_t>(index) < values.size() ?
            values[static_cast<std::size_t>(index)] : nullptr;
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&,
                                                     sdk::int32&) override {
        return nullptr;
    }
};

enum class TransferFault { none, result_error, zero_progress, negative_count, overreported_count };

struct MemoryStream final : FixedHostObject<sdk::IBStream> {
    std::array<std::uint8_t, 64> bytes{};
    sdk::int64 position = 0;
    sdk::int64 extent = 0;
    sdk::int32 transfer_limit = static_cast<sdk::int32>(instrument_state_bytes);
    std::size_t read_calls = 0;
    std::size_t write_calls = 0;
    TransferFault read_fault = TransferFault::none;
    TransferFault write_fault = TransferFault::none;
    std::size_t read_fault_at = 1;
    std::size_t write_fault_at = 1;
    bool fail_tell = false;
    bool fail_seek_end = false;
    bool fail_seek_set = false;
    bool wrong_restored_position = false;

    void load(InstrumentState state, std::size_t prefix = 0, std::uint16_t model = 1) {
        // This helper deliberately builds legacy v1 packets at Expression unity.
        // Non-unity/v2 fixtures construct their packet and exact extent separately.
        const auto encoded = encode_instrument_state(state, model);
        require(encoded.has_value() && prefix + encoded->size() <= bytes.size(),
                "invalid state-stream fixture");
        std::copy(encoded->begin(), encoded->end(), bytes.begin() +
                  static_cast<std::ptrdiff_t>(prefix));
        position = static_cast<sdk::int64>(prefix);
        extent = position + static_cast<sdk::int64>(encoded->size());
    }

    sdk::tresult PLUGIN_API read(void* buffer, sdk::int32 requested,
                                sdk::int32* transferred) override {
        ++read_calls;
        if (read_fault != TransferFault::none && read_calls == read_fault_at) {
            return report_fault(read_fault, requested, transferred);
        }
        if (requested < 0 || position < 0 || position > extent ||
            extent > static_cast<sdk::int64>(bytes.size())) {
            return sdk::kResultFalse;
        }
        const auto count = static_cast<sdk::int32>(std::min<sdk::int64>(
            {requested, transfer_limit, extent - position}));
        if (count < 0) { return sdk::kResultFalse; }
        std::copy_n(bytes.data() + static_cast<std::size_t>(position), count,
                    static_cast<std::uint8_t*>(buffer));
        position += count;
        if (transferred != nullptr) { *transferred = count; }
        return sdk::kResultOk;
    }

    sdk::tresult PLUGIN_API write(void* buffer, sdk::int32 requested,
                                 sdk::int32* transferred) override {
        ++write_calls;
        if (write_fault != TransferFault::none && write_calls == write_fault_at) {
            return report_fault(write_fault, requested, transferred);
        }
        if (requested < 0 || position < 0 ||
            position > static_cast<sdk::int64>(bytes.size())) {
            return sdk::kResultFalse;
        }
        const auto count = static_cast<sdk::int32>(std::min<sdk::int64>(
            {requested, transfer_limit, static_cast<sdk::int64>(bytes.size()) - position}));
        if (count < 0) { return sdk::kResultFalse; }
        std::copy_n(static_cast<const std::uint8_t*>(buffer), count,
                    bytes.data() + static_cast<std::size_t>(position));
        position += count;
        extent = std::max(extent, position);
        if (transferred != nullptr) { *transferred = count; }
        return sdk::kResultOk;
    }

    sdk::tresult PLUGIN_API seek(sdk::int64 offset, sdk::int32 mode,
                                sdk::int64* result) override {
        if ((mode == kIBSeekEnd && fail_seek_end) ||
            (mode == kIBSeekSet && fail_seek_set)) {
            return sdk::kResultFalse;
        }
        sdk::int64 base = 0;
        if (mode == kIBSeekCur) { base = position; }
        else if (mode == kIBSeekEnd) { base = extent; }
        else if (mode != kIBSeekSet) { return sdk::kResultFalse; }
        if (offset < -base || offset > static_cast<sdk::int64>(bytes.size()) - base) {
            return sdk::kResultFalse;
        }
        position = base + offset;
        if (result != nullptr) {
            *result = position + (mode == kIBSeekSet && wrong_restored_position ? 1 : 0);
        }
        return sdk::kResultOk;
    }

    sdk::tresult PLUGIN_API tell(sdk::int64* result) override {
        if (fail_tell || result == nullptr) { return sdk::kResultFalse; }
        *result = position;
        return sdk::kResultOk;
    }

private:
    static sdk::tresult report_fault(TransferFault fault, sdk::int32 requested,
                                     sdk::int32* transferred) noexcept {
        if (fault == TransferFault::result_error) { return sdk::kResultFalse; }
        if (transferred != nullptr) {
            *transferred = fault == TransferFault::negative_count ? -1 :
                (fault == TransferFault::overreported_count ? requested + 1 : 0);
        }
        return sdk::kResultOk;
    }
};

inline vst::Event note_on(sdk::int32 offset, sdk::int32 id,
                         sdk::int16 pitch = 68, float velocity = 0.75F,
                         sdk::int16 channel = 0) noexcept {
    vst::Event event{};
    event.type = vst::Event::kNoteOnEvent;
    event.sampleOffset = offset;
    event.noteOn.channel = channel;
    event.noteOn.pitch = pitch;
    event.noteOn.velocity = velocity;
    event.noteOn.noteId = id;
    return event;
}

inline vst::Event note_off(sdk::int32 offset, sdk::int32 id,
                          sdk::int16 pitch = 68, sdk::int16 channel = 0) noexcept {
    vst::Event event{};
    event.type = vst::Event::kNoteOffEvent;
    event.sampleOffset = offset;
    event.noteOff.channel = channel;
    event.noteOff.pitch = pitch;
    event.noteOff.noteId = id;
    return event;
}

template <class Sample, std::size_t Frames = 32>
struct ProcessorHost {
    static_assert(std::is_same_v<Sample, float> || std::is_same_v<Sample, double>);
    static constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ?
        vst::kSample32 : vst::kSample64;

private:
    // Construction belongs to host setup, outside the measured audio callback.
    // Keep the processor's growing fixed DSP storage off the test thread stack.
    std::unique_ptr<RhodesProcessor> processor_storage_ =
        std::make_unique<RhodesProcessor>();

public:
    RhodesProcessor& processor = *processor_storage_;
    std::array<Sample, Frames> left{};
    std::array<Sample, Frames> right{};
    std::array<Sample*, 2> channels{left.data(), right.data()};
    vst::AudioBusBuffers output{};
    vst::ProcessData data{};

    explicit ProcessorHost(double rate = 48000.0) {
        require(processor.initialize(nullptr) == sdk::kResultOk, "processor initialize");
        require(processor.activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk,
                "activate stereo output");
        vst::ProcessSetup setup{vst::kRealtime, sample_size,
            static_cast<sdk::int32>(kMaximumHostFrames), rate};
        require(processor.setupProcessing(setup) == sdk::kResultOk, "processor setup");
        require(processor.setActive(true) == sdk::kResultOk, "processor activate");
        require(processor.setProcessing(true) == sdk::kResultOk, "processor start");
        output.numChannels = 2;
        if constexpr (std::is_same_v<Sample, float>) { output.channelBuffers32 = channels.data(); }
        else { output.channelBuffers64 = channels.data(); }
        data.processMode = vst::kRealtime;
        data.symbolicSampleSize = sample_size;
        data.numSamples = static_cast<sdk::int32>(std::min(Frames, kMaximumHostFrames));
        data.numOutputs = 1;
        data.outputs = &output;
    }
    ~ProcessorHost() { (void)processor.terminate(); }
    ProcessorHost(const ProcessorHost&) = delete;
    ProcessorHost& operator=(const ProcessorHost&) = delete;

    void fill(Sample value) { left.fill(value); right.fill(value); }
    void expect_silence(std::size_t samples_to_check = Frames) const {
        require(samples_to_check <= Frames, "invalid silence-check bound");
        const auto zero = [](Sample value) { return value == Sample{0}; };
        require(std::all_of(left.begin(), left.begin() + static_cast<std::ptrdiff_t>(samples_to_check), zero)
                && std::all_of(right.begin(), right.begin() +
                               static_cast<std::ptrdiff_t>(samples_to_check), zero),
                "complete stereo output was not silenced");
        require(output.silenceFlags == 3, "silent stereo flags are incorrect");
    }
};

struct ControllerHost {
    RhodesController controller;
    ControllerHost() {
        require(controller.initialize(nullptr) == sdk::kResultOk, "controller initialize");
    }
    ~ControllerHost() { (void)controller.terminate(); }
};

template<class Actual, class Expected>
inline void same_mechanics(const Actual& actual, const Expected& expected) {
    require(actual.sample_rate_hz() == expected.sample_rate_hz(), "sample rate changed");
    require(actual.mechanical_energy_joules() == expected.mechanical_energy_joules(),
            "mechanical energy changed");
    for (std::size_t key = 0; key < key_count; ++key) {
        for (std::size_t mode = 0; mode < modes_per_key; ++mode) {
            const auto observed = actual.modal_state(key, mode);
            const auto reference = expected.modal_state(key, mode);
            require(observed.has_value() && reference.has_value(), "missing modal state");
            require(observed->displacement_m == reference->displacement_m &&
                    observed->velocity_m_per_s == reference->velocity_m_per_s,
                    "modal state changed");
        }
    }
}

struct ProcessorSnapshot {
private:
    std::unique_ptr<const HostEventProcessor> engine_storage_;

public:
    const HostEventProcessor& engine;
    NoteTracker notes;
    InstrumentState live;
    InstrumentState published;
    bool fault;

    explicit ProcessorSnapshot(const RhodesProcessor& processor)
        : engine_storage_(std::make_unique<const HostEventProcessor>(processor.engine_processor())),
          engine(*engine_storage_), notes(processor.note_tracker()),
          live(processor.current_state()), published(processor.published_state()),
          fault(processor.requires_reset()) {}

    void expect_unchanged(const RhodesProcessor& processor) const {
        require(processor.engine_processor().frame_cursor() == engine.frame_cursor(),
                "rejected call advanced frame cursor");
        require(processor.requires_reset() == fault, "rejected call changed fault latch");
        require(processor.current_state() == live && processor.published_state() == published,
                "rejected call changed live or published configuration");
        require(processor.note_tracker().active_count() == notes.active_count(),
                "rejected call changed holder count");
        for (std::size_t key = 0; key < key_count; ++key) {
            require(processor.note_tracker().held_count(key) == notes.held_count(key),
                    "rejected call changed per-key ownership");
        }
        same_mechanics(processor.engine_processor().engine(), engine.engine());
        // A matching present state alone would miss a changed damping/held
        // flag. Copy both engines and compare their subsequent trajectories.
        // Snapshot validation runs after the allocation-probed callback. These
        // independent continuation copies can contain large prepared DSP plans.
        using Engine = std::remove_cvref_t<decltype(engine.engine())>;
        const auto actual = std::make_unique<Engine>(processor.engine_processor().engine());
        const auto expected = std::make_unique<Engine>(engine.engine());
        std::array<double, 17> actual_output{}, expected_output{};
        require(actual->process(actual_output) && expected->process(expected_output),
                "snapshot continuation failed");
        require(actual_output == expected_output, "rejected call changed future trajectory");
    }
};

} // namespace rhodes::vst3::test_host
