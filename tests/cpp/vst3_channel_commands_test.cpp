#include "tests/cpp/vst3_command_test_support.hpp"

#include <iostream>

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;

template<class Sample>
void trace(double rate) {
    ProcessorHost<Sample, 64> actual(rate), reference(rate);
    EventList actual_events, reference_events;
    CommandQueue command;
    ParameterQueue pedal;
    pedal.id = kSustainId;
    CommandChanges actual_changes, reference_changes;
    actual.data.inputEvents = &actual_events;
    reference.data.inputEvents = &reference_events;
    actual.data.inputParameterChanges = &actual_changes;
    reference.data.inputParameterChanges = &reference_changes;
    const auto process_pair = [&] {
        require(actual.processor.process(actual.data) == sdk::kResultOk, "command trace rejected");
        require(reference.processor.process(reference.data) == sdk::kResultOk, "release oracle rejected");
        require(actual.left == reference.left && actual.right == reference.right,
                "CC123 differs from explicit channel note releases");
        require(actual.output.silenceFlags == reference.output.silenceFlags, "trace flags differ");
        require(actual.processor.current_state() == reference.processor.current_state(), "trace state differs");
        require(actual.processor.note_tracker().active_count() ==
                reference.processor.note_tracker().active_count(), "trace ownership differs");
        same_mechanics(actual.processor.engine_processor().engine(),
                       reference.processor.engine_processor().engine());
    };
    actual_events.values[0] = note_on(0, 10, 60, 0.6F, 0);
    actual_events.values[1] = note_on(0, 11, 60, 0.5F, 1);
    actual_events.values[2] = note_on(1, -1, 67, 0.4F, 0);
    actual_events.values[3] = note_on(2, -1, 67, 0.3F, 0);
    actual_events.values[4] = note_on(3, 12, 100, 0.2F, 2);
    actual_events.count = 5;
    reference_events.values = actual_events.values;
    reference_events.count = actual_events.count;
    pedal.count = 1;
    pedal.values[0] = {0, 1.0};
    actual_changes.count = reference_changes.count = 1;
    actual_changes.values[0] = reference_changes.values[0] = &pedal;
    process_pair();
    require(actual.processor.note_tracker().active_count() == 5, "initial holders");

    command.count = 2;
    command.values[0] = {4, 0.0};
    command.values[1] = {17, 0.0}; // Repeated unchanged values must trigger again.
    actual_changes.values[0] = &command;
    reference_changes.count = 0;
    actual_events.count = 1;
    actual_events.values[0] = note_on(4, 10, 72, 0.7F, 0);
    reference_events.count = 5;
    reference_events.values[0] = note_off(4, 10, 60, 0);
    reference_events.values[1] = note_off(4, -1, 67, 0);
    reference_events.values[2] = note_off(4, -1, 67, 0);
    reference_events.values[3] = actual_events.values[0];
    reference_events.values[4] = note_off(17, 10, 72, 0);
    process_pair();
    require(actual.processor.note_tracker().active_count() == 2 &&
            actual.processor.note_tracker().held_count(32) == 1,
            "channel release removed another channel's shared-key holder");
    require(actual.processor.current_state().sustain, "channel release cleared sustain");

    command.id = kAllNotesOffBaseId + 1;
    command.count = 1;
    command.values[0] = {7, 0.75};
    pedal.values[0] = {7, 0.0};
    actual_changes.count = 2;
    actual_changes.values[0] = &command;
    actual_changes.values[1] = &pedal;
    reference_changes.count = 1;
    reference_changes.values[0] = &pedal;
    actual_events.count = 0;
    reference_events.count = 1;
    reference_events.values[0] = note_off(7, 11, 60, 1);
    process_pair();
    require(actual.processor.note_tracker().active_count() == 1 &&
            actual.processor.note_tracker().held_count(72) == 1,
            "unaddressed channel did not survive");

    // A parameter-only channel command must select release damping without
    // rendering a sample or flushing the existing convolution history.
    using ScalarEngine = std::remove_cvref_t<decltype(actual.processor.engine_processor().engine())>;
    const auto expected = std::make_unique<ScalarEngine>(actual.processor.engine_processor().engine());
    require(expected->note_off(72), "expected release failed");
    const auto old_cursor = actual.processor.engine_processor().frame_cursor();
    MemoryStream before, after;
    require(actual.processor.getState(&before) == sdk::kResultOk, "save before command");
    actual.data.numSamples = 0;
    actual.data.numOutputs = 0;
    actual.data.outputs = nullptr;
    command.id = kAllNotesOffBaseId + 2;
    command.values[0] = {0, 0.0};
    actual_changes.count = 1;
    actual_changes.values[0] = &command;
    require(actual.processor.process(actual.data) == sdk::kResultOk, "zero-frame CC123 rejected");
    require(actual.processor.note_tracker().active_count() == 0, "flush retained holders");
    require(actual.processor.engine_processor().frame_cursor() == old_cursor, "flush advanced time");
    same_mechanics(actual.processor.engine_processor().engine(), *expected);
    require(actual.processor.getState(&after) == sdk::kResultOk && before.bytes == after.bytes &&
            before.extent == after.extent, "command entered saved configuration");
    auto continuation = std::make_unique<HostEventProcessor>(actual.processor.engine_processor());
    std::array<double, 64> observed{}, expected_output{};
    require(continuation->process(observed).succeeded() && expected->process(expected_output) &&
            observed == expected_output, "flush changed convolution or release trajectory");
}
} // namespace

int main() {
    try {
        for (const double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
            trace<float>(rate);
            trace<double>(rate);
        }
        std::cout << "Channel commands: explicit release oracle, repeated zero, shared holders, "
                     "sustain, retrigger and zero-frame state passed (both formats, four rates).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
