#include "vst3_test_support.hpp"

#include <barrier>
#include <bit>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

namespace {
namespace host = rhodes::vst3::test_host;
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using rhodes::InstrumentState;
using rhodes::vst3::RhodesProcessor;
using Packet = std::array<std::uint8_t, 16>;

constexpr InstrumentState first{0.125F, true};
constexpr InstrumentState second{0.875F, false};
constexpr InstrumentState automation_a{0.375F, true};
constexpr InstrumentState automation_b{0.625F, false};
constexpr InstrumentState final_recall{0.9375F, true};
constexpr std::array allowed{InstrumentState{}, first, second,
                             automation_a, automation_b, final_recall};

// Independent wire oracle: do not call the codec being tested to construct or
// recognize the complete gain/pedal pairs allowed by the concurrent writers.
Packet packet(InstrumentState state) {
    Packet bytes{'R', 'H', 'S', 'T', 1, 0, 1, 0};
    const auto gain = std::bit_cast<std::uint32_t>(state.monitoring_gain);
    const std::uint32_t pedal = state.sustain ? 0x3f800000U : 0U;
    for (std::size_t digit = 0; digit < 4; ++digit) {
        bytes[8 + digit] = static_cast<std::uint8_t>(gain >> (8U * digit));
        bytes[12 + digit] = static_cast<std::uint8_t>(pedal >> (8U * digit));
    }
    return bytes;
}

bool coherent(InstrumentState state) {
    return std::find(allowed.begin(), allowed.end(), state) != allowed.end();
}

struct Outcome {
    std::size_t checks = 0;
    std::size_t failures = 0;
    std::size_t calls = 0;
    const char* first_failure = nullptr;
    void check(bool condition, const char* message) {
        ++checks;
        if (!condition) {
            ++failures;
            if (first_failure == nullptr) { first_failure = message; }
        }
    }
    void verify() const {
        host::require(failures == 0, first_failure != nullptr ? first_failure : "thread failure");
    }
};

void load(host::MemoryStream& stream, InstrumentState state, sdk::int32 chunk = 16) {
    const auto bytes = packet(state);
    std::copy(bytes.begin(), bytes.end(), stream.bytes.begin());
    stream.position = 0;
    stream.extent = 16;
    stream.transfer_limit = chunk;
}

bool recall(RhodesProcessor& processor, InstrumentState state, sdk::int32 chunk = 16) {
    host::MemoryStream stream;
    load(stream, state, chunk);
    return processor.setState(&stream) == sdk::kResultOk;
}

bool saved(RhodesProcessor& processor, InstrumentState expected, sdk::int32 chunk = 16) {
    host::MemoryStream stream;
    stream.transfer_limit = chunk;
    const auto wanted = packet(expected);
    return processor.getState(&stream) == sdk::kResultOk && stream.extent == 16 &&
        std::equal(wanted.begin(), wanted.end(), stream.bytes.begin());
}

bool coherent_save(RhodesProcessor& processor, sdk::int32 chunk) {
    host::MemoryStream stream;
    stream.transfer_limit = chunk;
    if (processor.getState(&stream) != sdk::kResultOk || stream.extent != 16) { return false; }
    for (const auto state : allowed) {
        const auto wanted = packet(state);
        if (std::equal(wanted.begin(), wanted.end(), stream.bytes.begin())) { return true; }
    }
    return false;
}

sdk::tresult flush(RhodesProcessor& processor, sdk::int32 sample_size) {
    vst::ProcessData data{};
    data.symbolicSampleSize = sample_size;
    return processor.process(data);
}

// This barrier deliberately belongs to the synthetic host. The plug-in has
// not consumed its mailbox token yet when getEventCount() is called. It lets
// the UI replace a pending request before a successful or rejected transaction.
struct GatedEvents final : host::FixedHostObject<vst::IEventList> {
    host::EventList events;
    std::barrier<>* phase = nullptr;
    sdk::int32 PLUGIN_API getEventCount() override {
        phase->arrive_and_wait();
        phase->arrive_and_wait();
        return events.count;
    }
    sdk::tresult PLUGIN_API getEvent(sdk::int32 index, vst::Event& event) override {
        return events.getEvent(index, event);
    }
    sdk::tresult PLUGIN_API addEvent(vst::Event&) override { return sdk::kNotImplemented; }
};

template<class Sample>
void ordered_case(bool reject, bool identical, Outcome& total) {
    auto rig = std::make_unique<host::ProcessorHost<Sample, 32>>();
    host::EventList seed;
    seed.count = 1;
    seed.values[0] = host::note_on(0, 77);
    rig->data.inputEvents = &seed;
    total.check(rig->processor.process(rig->data) == sdk::kResultOk, "seed process failed");
    ++total.calls;
    const auto before = std::make_unique<host::ProcessorSnapshot>(rig->processor);
    total.check(recall(rig->processor, first, 3), "initial UI recall rejected");

    std::barrier phase{2};
    GatedEvents gated;
    gated.phase = &phase;
    gated.events.count = reject ? 256 : 1;
    for (sdk::int32 index = 0; index < gated.events.count; ++index) {
        gated.events.values[static_cast<std::size_t>(index)] = host::note_on(0, 42);
    }
    host::ParameterQueue pedal;
    pedal.id = rhodes::vst3::kSustainId;
    pedal.count = reject ? 1 : 0;
    pedal.values[0] = {0, 1.0};
    host::ParameterChanges changes;
    changes.count = 1;
    changes.values[0] = &pedal;
    rig->data.inputEvents = &gated;
    rig->data.inputParameterChanges = &changes;
    rig->fill(Sample{123});
    sdk::tresult result = sdk::kInternalError;
    std::jthread audio([&] { result = rig->processor.process(rig->data); });
    phase.arrive_and_wait();
    const auto latest = identical ? first : second;
    total.check(recall(rig->processor, latest, 1), "replacement UI recall rejected");
    total.check(saved(rig->processor, latest, 2), "pending recall not visible to save");
    phase.arrive_and_wait();
    audio.join();
    ++total.calls;
    total.check(result == (reject ? sdk::kResultFalse : sdk::kResultOk),
                "ordered transaction returned wrong status");
    total.check(saved(rig->processor, latest, 5), "transaction lost latest UI recall");
    if (reject) {
        total.check(rig->processor.current_state() == before->live,
                    "rejected transaction installed a recall");
        total.check(rig->processor.engine_processor().frame_cursor() ==
                    before->engine.frame_cursor(), "rejected transaction advanced cursor");
        total.check(rig->processor.note_tracker().active_count() == before->notes.active_count(),
                    "rejected transaction changed note ownership");
        host::same_mechanics(rig->processor.engine_processor().engine(), before->engine.engine());
        rig->expect_silence();
        total.check(flush(rig->processor, rig->sample_size) == sdk::kResultOk,
                    "restored pending recall could not be consumed");
        ++total.calls;
        total.check(rig->processor.engine_processor().frame_cursor() == 0 &&
                    rig->processor.note_tracker().active_count() == 0 &&
                    rig->processor.engine_processor().engine().mechanical_energy_joules() == 0.0,
                    "restored recall did not reset mechanical/ownership state");
    } else {
        auto control = std::make_unique<host::ProcessorHost<Sample, 32>>();
        total.check(recall(control->processor, latest), "control recall failed");
        control->data.inputEvents = &gated.events;
        total.check(control->processor.process(control->data) == sdk::kResultOk,
                    "control process failed");
        ++total.calls;
        total.check(rig->left == control->left && rig->right == control->right,
                    "preflight recall produced a stale mechanical trajectory");
        host::same_mechanics(rig->processor.engine_processor().engine(),
                             control->processor.engine_processor().engine());
        total.check(rig->processor.note_tracker().active_count() == 1 &&
                    rig->processor.note_tracker().held_count(40) == 1,
                    "accepted recall retained old note ownership");
    }
    total.check(rig->processor.current_state() == latest,
                "latest requested state was not installed coherently");
    // A fresh silent block must advance normally, rather than replaying recall.
    rig->data.inputEvents = nullptr;
    rig->data.inputParameterChanges = nullptr;
    const auto cursor = rig->processor.engine_processor().frame_cursor();
    total.check(rig->processor.process(rig->data) == sdk::kResultOk &&
                rig->processor.engine_processor().frame_cursor() == cursor + 32,
                "pending recall was applied more than once");
    ++total.calls;
}

void state_preflight_race() {
    Outcome total;
    for (const bool reject : {false, true}) {
        for (const bool identical : {false, true}) {
            ordered_case<float>(reject, identical, total);
            ordered_case<double>(reject, identical, total);
        }
    }
    total.verify();
    std::cout << "ordered scenarios=8 process_calls=" << total.calls
              << " explicit_checks=" << total.checks << '\n';
}

template<class Sample>
void stress_case(double rate, Outcome& total) {
    constexpr std::size_t blocks = 1024;
    constexpr std::size_t state_calls = 8192;
    auto rig = std::make_unique<host::ProcessorHost<Sample, 64>>(rate);
    auto gain = std::make_unique<host::ParameterQueue>();
    auto pedal = std::make_unique<host::ParameterQueue>();
    gain->id = rhodes::vst3::kGainId;
    pedal->id = rhodes::vst3::kSustainId;
    gain->count = 1;
    pedal->count = 1;
    host::ParameterChanges changes;
    changes.count = 2;
    changes.values = {gain.get(), pedal.get()};
    host::EventList events;
    events.values.fill(host::note_on(0, 42));
    rig->data.inputEvents = &events;
    rig->data.inputParameterChanges = &changes;
    std::barrier phase{3};
    Outcome audio_result, ui_result, observer_result;
    // All test outcomes have one writer and are inspected only after joins.
    // Only this thread touches audio diagnostics, queues, events and buffers.
    std::jthread audio([&] {
        phase.arrive_and_wait();
        for (std::size_t index = 0; index < blocks; ++index) {
            const bool reject = index % 17 == 0;
            const auto wanted = index % 2 == 0 ? automation_a : automation_b;
            gain->values[0] = {0, static_cast<double>(wanted.monitoring_gain)};
            pedal->values[0] = {0, wanted.sustain ? 1.0 : 0.0};
            events.count = reject ? 256 : 1;
            const auto previous = rig->processor.current_state();
            const auto cursor = rig->processor.engine_processor().frame_cursor();
            const auto energy = rig->processor.engine_processor().engine().mechanical_energy_joules();
            const auto holders = rig->processor.note_tracker().active_count();
            const auto result = rig->processor.process(rig->data);
            ++audio_result.calls;
            audio_result.check(result == (reject ? sdk::kResultFalse : sdk::kResultOk),
                               "concurrent process status differs");
            audio_result.check(coherent(rig->processor.current_state()), "torn live state");
            audio_result.check(coherent(rig->processor.published_state()), "torn published state");
            if (reject) {
                audio_result.check(rig->processor.current_state() == previous &&
                    rig->processor.engine_processor().frame_cursor() == cursor &&
                    rig->processor.engine_processor().engine().mechanical_energy_joules() == energy &&
                    rig->processor.note_tracker().active_count() == holders,
                    "concurrent rejection mutated live state");
            } else {
                audio_result.check(rig->processor.current_state() == wanted &&
                    rig->processor.note_tracker().active_count() == 1,
                    "accepted automation/ownership did not commit");
            }
            for (std::size_t frame = 0; frame < rig->left.size(); ++frame) {
                audio_result.check(std::isfinite(rig->left[frame]) &&
                    rig->left[frame] == rig->right[frame] && (!reject || rig->left[frame] == Sample{0}),
                    "concurrent process output invalid");
            }
        }
        // Both state callback threads quiesce before the deliberate final write.
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        audio_result.check(flush(rig->processor, rig->sample_size) == sdk::kResultOk,
                           "final recall flush failed");
        ++audio_result.calls;
        audio_result.check(rig->processor.current_state() == final_recall &&
            rig->processor.engine_processor().frame_cursor() == 0 &&
            rig->processor.note_tracker().active_count() == 0 &&
            rig->processor.engine_processor().engine().mechanical_energy_joules() == 0.0,
            "final last-writer recall was lost or did not reset");
        rig->data.inputEvents = nullptr;
        rig->data.inputParameterChanges = nullptr;
        audio_result.check(rig->processor.process(rig->data) == sdk::kResultOk &&
            rig->processor.engine_processor().frame_cursor() == 64 &&
            rig->processor.current_state() == final_recall,
            "final recall repeated on following block");
        ++audio_result.calls;
    });
    std::jthread observer([&] {
        phase.arrive_and_wait();
        for (std::size_t index = 0; index < state_calls; ++index) {
            observer_result.check(coherent_save(rig->processor, static_cast<sdk::int32>(index % 16 + 1)),
                                  "save combined fields from different publications");
            ++observer_result.calls;
        }
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        observer_result.check(saved(rig->processor, final_recall, 1), "final observer save differs");
        ++observer_result.calls;
    });
    phase.arrive_and_wait();
    for (std::size_t index = 0; index < state_calls; ++index) {
        const auto wanted = index % 2 == 0 ? first : second;
        host::MemoryStream stream;
        load(stream, wanted, static_cast<sdk::int32>(index % 16 + 1));
        const bool reject = index % 23 == 0;
        if (reject) { stream.bytes[4] = 2; }
        ui_result.check(rig->processor.setState(&stream) ==
                        (reject ? sdk::kResultFalse : sdk::kResultOk),
                        "UI state validation differs during process");
        ++ui_result.calls;
        ui_result.check(coherent_save(rig->processor, 7), "UI save is incoherent");
        ++ui_result.calls;
    }
    phase.arrive_and_wait();
    ui_result.check(recall(rig->processor, final_recall, 1), "final UI recall failed");
    ++ui_result.calls;
    phase.arrive_and_wait();
    audio.join();
    observer.join();
    audio_result.verify();
    ui_result.verify();
    observer_result.verify();
    total.check(saved(rig->processor, final_recall), "quiescent final save differs");
    total.checks += audio_result.checks + ui_result.checks + observer_result.checks;
    total.calls += audio_result.calls;
    std::cout << "stress rate=" << rate << " bits=" << sizeof(Sample) * 8
              << " process_calls=" << audio_result.calls
              << " state_calls=" << ui_result.calls + observer_result.calls
              << " explicit_checks=" << audio_result.checks + ui_result.checks + observer_result.checks
              << '\n';
}

void state_callback_stress() {
    Outcome total;
    stress_case<float>(48000.0, total);
    stress_case<double>(192000.0, total);
    total.verify();
    std::cout << "stress total_process_calls=" << total.calls
              << " explicit_checks=" << total.checks << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view selected = argc > 1 ? argv[1] : "all";
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected == "all" || selected == name) {
                test();
                found = true;
                std::cout << "PASS " << name << '\n';
            }
        };
        run("state_preflight_race", state_preflight_race);
        run("state_callback_stress", state_callback_stress);
        host::require(found, "unknown processor concurrency fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
