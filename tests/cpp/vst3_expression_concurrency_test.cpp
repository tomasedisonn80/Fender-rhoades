#include "vst3_test_support.hpp"

#include <atomic>
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
using rhodes::StateMailbox;
using rhodes::vst3::RhodesProcessor;

#if defined(RHODES_QUADRATURE_VST3)
constexpr std::uint16_t expected_model = 3;
#elif defined(RHODES_FILTERED_VST3)
constexpr std::uint16_t expected_model = 2;
#else
constexpr std::uint16_t expected_model = 1;
#endif
static_assert(rhodes::vst3::kStateModel == expected_model);

constexpr InstrumentState state(std::uint32_t gain, bool sustain, std::uint32_t expression) {
    return {std::bit_cast<float>(gain), sustain, std::bit_cast<float>(expression)};
}
constexpr InstrumentState initial = state(0x3f000000U, false, 0x3f800000U);
constexpr InstrumentState first = state(0x3e000000U, true, 0x3e800000U);
constexpr InstrumentState expression_only = state(0x3e000000U, true, 0x3f400000U);
constexpr InstrumentState second = state(0x3f600000U, false, 0x3f800000U);
constexpr InstrumentState subnormal_expression = state(0x3f600000U, false, 0x00000001U);
constexpr InstrumentState automation_a = state(0x3ec00000U, true, 0x3f000000U);
constexpr InstrumentState automation_b = state(0x3f200000U, false, 0x00000000U);
constexpr InstrumentState final_recall = state(0x3f700000U, true, 0x3f000001U);
constexpr std::array recalls{first, expression_only, second, subnormal_expression};
constexpr std::array allowed{initial, first, expression_only, second,
                             subnormal_expression, automation_a, automation_b, final_recall};

bool equal(InstrumentState lhs, InstrumentState rhs) {
    return std::bit_cast<std::uint32_t>(lhs.monitoring_gain) ==
               std::bit_cast<std::uint32_t>(rhs.monitoring_gain) &&
           lhs.sustain == rhs.sustain &&
           std::bit_cast<std::uint32_t>(lhs.expression) ==
               std::bit_cast<std::uint32_t>(rhs.expression);
}

bool coherent(InstrumentState value) {
    return std::any_of(allowed.begin(), allowed.end(),
                       [&](InstrumentState candidate) { return equal(value, candidate); });
}

// This independent wire oracle never calls a production encoder, decoder,
// canonicalizer or mailbox packer. Complete triples, version and exact extent
// must match together; accepting each field independently would hide tears.
struct Packet {
    std::array<std::uint8_t, 20> bytes{};
    std::size_t size = 0;
};

Packet packet(InstrumentState value) {
    const auto expression = std::bit_cast<std::uint32_t>(value.expression);
    const bool unity = expression == 0x3f800000U;
    Packet result{{'R', 'H', 'S', 'T', static_cast<std::uint8_t>(unity ? 1 : 2), 0,
                   static_cast<std::uint8_t>(expected_model), 0}, unity ? 16U : 20U};
    const auto put = [&](std::size_t at, std::uint32_t word) {
        for (std::size_t digit = 0; digit < 4; ++digit) {
            result.bytes[at + digit] = static_cast<std::uint8_t>(word >> (8U * digit));
        }
    };
    put(8, std::bit_cast<std::uint32_t>(value.monitoring_gain));
    put(12, value.sustain ? 0x3f800000U : 0U);
    if (!unity) { put(16, expression); }
    return result;
}

struct Outcome {
    std::size_t checks = 0;
    std::size_t failures = 0;
    std::size_t process_calls = 0;
    std::size_t set_calls = 0;
    std::size_t get_calls = 0;
    const char* first_failure = nullptr;

    void check(bool condition, const char* message) {
        ++checks;
        if (!condition) {
            ++failures;
            if (first_failure == nullptr) { first_failure = message; }
        }
    }
    void merge(const Outcome& other) {
        checks += other.checks;
        failures += other.failures;
        process_calls += other.process_calls;
        set_calls += other.set_calls;
        get_calls += other.get_calls;
        if (first_failure == nullptr) { first_failure = other.first_failure; }
    }
    void verify() const {
        host::require(failures == 0, first_failure != nullptr ? first_failure : "thread failure");
    }
    void report(const char* name) const {
        verify();
        std::cout << name << " model=" << expected_model << " checks=" << checks
                  << " process_calls=" << process_calls << " set_calls=" << set_calls
                  << " get_calls=" << get_calls << '\n';
    }
};

void load(host::MemoryStream& stream, InstrumentState value, sdk::int32 chunk = 20) {
    const auto wanted = packet(value);
    stream.bytes.fill(0xa5U);
    std::copy_n(wanted.bytes.begin(), wanted.size, stream.bytes.begin());
    stream.position = 0;
    stream.extent = static_cast<sdk::int64>(wanted.size);
    stream.transfer_limit = chunk;
}

bool matches(const host::MemoryStream& stream, InstrumentState value) {
    const auto wanted = packet(value);
    return stream.extent == static_cast<sdk::int64>(wanted.size) &&
           stream.position == stream.extent && stream.write_calls > 0 &&
           stream.write_calls <= wanted.size &&
           std::equal(wanted.bytes.begin(), wanted.bytes.begin() +
                      static_cast<std::ptrdiff_t>(wanted.size), stream.bytes.begin()) &&
           std::all_of(stream.bytes.begin() + static_cast<std::ptrdiff_t>(wanted.size),
                       stream.bytes.end(), [](std::uint8_t byte) { return byte == 0xa5U; });
}

bool recall(RhodesProcessor& processor, InstrumentState value, Outcome& result,
            sdk::int32 chunk = 20) {
    host::MemoryStream stream;
    load(stream, value, chunk);
    ++result.set_calls;
    return processor.setState(&stream) == sdk::kResultOk &&
           stream.position == stream.extent && stream.read_calls > 0 &&
           stream.read_calls <= packet(value).size;
}

bool saved(RhodesProcessor& processor, InstrumentState value, Outcome& result,
           sdk::int32 chunk = 20) {
    host::MemoryStream stream;
    stream.bytes.fill(0xa5U);
    stream.transfer_limit = chunk;
    ++result.get_calls;
    return processor.getState(&stream) == sdk::kResultOk && matches(stream, value);
}

bool coherent_save(RhodesProcessor& processor, Outcome& result, sdk::int32 chunk) {
    host::MemoryStream stream;
    stream.bytes.fill(0xa5U);
    stream.transfer_limit = chunk;
    ++result.get_calls;
    if (processor.getState(&stream) != sdk::kResultOk) { return false; }
    return std::any_of(allowed.begin(), allowed.end(),
                      [&](InstrumentState value) { return matches(stream, value); });
}

sdk::tresult flush(RhodesProcessor& processor, sdk::int32 sample_size, Outcome& result) {
    vst::ProcessData data{};
    data.symbolicSampleSize = sample_size;
    ++result.process_calls;
    return processor.process(data);
}

// All token operations in this test belong to this one serialized audio owner.
// The writer can replace pending state, but never consumes or replays a token.
void mailbox_ordered() {
    Outcome result;
    std::size_t scenarios = 0;
    for (const bool consume_before_write : {false, true}) {
        for (const bool abort : {false, true}) {
            for (unsigned replacement = 0; replacement < 5; ++replacement) {
                StateMailbox mailbox;
                result.check(mailbox.request_recall(first), "initial mailbox request failed");
                StateMailbox::BlockToken selected;
                if (consume_before_write) { selected = mailbox.begin_block(); }
                const auto latest = replacement == 1 ? expression_only :
                    replacement == 2 ? second : first;
                std::barrier phase{2};
                Outcome writer_result;
                std::jthread writer([&] {
                    phase.arrive_and_wait();
                    if (replacement == 3) {
                        writer_result.check(mailbox.request_recall(expression_only),
                                            "ABA middle request failed");
                    }
                    if (replacement != 4) {
                        writer_result.check(mailbox.request_recall(latest),
                                            "ordered replacement failed");
                    }
                    const auto invalid = state(0x3e000000U, true, 0x3f800001U);
                    writer_result.check(!mailbox.request_recall(invalid),
                                        "invalid expression request accepted");
                    writer_result.check(equal(mailbox.snapshot(), latest),
                                        "writer snapshot lost an expression field");
                    phase.arrive_and_wait();
                });
                phase.arrive_and_wait();
                phase.arrive_and_wait();
                writer.join();
                result.merge(writer_result);
                if (!consume_before_write) { selected = mailbox.begin_block(); }
                const auto selected_state = consume_before_write ? first : latest;
                result.check(selected.may_publish && selected.apply_recall &&
                             equal(selected.recalled, selected_state),
                             "ordered consume selected the wrong complete triple");
                const std::uint64_t expected_word =
                    std::bit_cast<std::uint32_t>(selected_state.monitoring_gain) |
                    (static_cast<std::uint64_t>(selected_state.sustain) << 32U) |
                    (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(
                        selected_state.expression)) << 34U);
                result.check(selected.publication_token == expected_word,
                             "selected token did not contain all expression bits");
                const bool newer = consume_before_write && replacement != 4;
                if (abort) { mailbox.abort_block(selected); }
                else {
                    result.check(mailbox.end_block(selected, automation_a) == !newer,
                                 "stale publication did not respect newer pending recall");
                }
                const auto expected = newer ? latest : abort ? selected_state : automation_a;
                result.check(equal(mailbox.snapshot(), expected),
                             "completion replaced a newer recall or tore the triple");
                const auto retry = mailbox.begin_block();
                result.check(retry.may_publish && retry.apply_recall == (newer || abort),
                             "pending recall was lost or spuriously restored");
                if (retry.apply_recall) {
                    result.check(equal(retry.recalled, expected), "retry recalled a stale expression");
                }
                result.check(mailbox.end_block(retry, expected), "retry publication failed");
                const auto once = mailbox.begin_block();
                result.check(once.may_publish && !once.apply_recall,
                             "completed recall was consumed more than once");
                result.check(mailbox.end_block(once, expected), "ordinary publication failed");
                ++scenarios;
            }
        }
    }
    std::cout << "mailbox ordered_scenarios=" << scenarios << '\n';
    result.report("mailbox_ordered");
}

struct ThreeQueues final : host::FixedHostObject<vst::IParameterChanges> {
    std::array<vst::IParamValueQueue*, 3> values{};
    sdk::int32 count = 0;
    sdk::int32 PLUGIN_API getParameterCount() override { return count; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        return index >= 0 && index < count && static_cast<std::size_t>(index) < values.size() ?
            values[static_cast<std::size_t>(index)] : nullptr;
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&, sdk::int32&) override {
        return nullptr;
    }
};

// Both gates are deliberately before begin_block(): all host syntax is copied
// before the processor acquires a token. Post-consume ordering is tested above,
// without pretending these callbacks can force an internal load/CAS loser.
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

struct GatedBadExpression final : host::FixedHostObject<vst::IParamValueQueue> {
    std::barrier<>* phase = nullptr;
    std::size_t calls = 0;
    vst::ParamID PLUGIN_API getParameterId() override { return rhodes::vst3::kExpressionId; }
    sdk::int32 PLUGIN_API getPointCount() override { return 2; }
    sdk::tresult PLUGIN_API getPoint(sdk::int32 index, sdk::int32& offset,
                                    vst::ParamValue& value) override {
        ++calls;
        if (index < 0 || index > 1) { return sdk::kResultFalse; }
        offset = index == 0 ? 0 : 63;
        value = index == 0 ? 0.75 : 1.25;
        if (index == 1) {
            phase->arrive_and_wait();
            phase->arrive_and_wait();
        }
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addPoint(sdk::int32, vst::ParamValue, sdk::int32&) override {
        return sdk::kNotImplemented;
    }
};

void unchanged_audio(const RhodesProcessor& processor, const host::ProcessorSnapshot& before,
                     Outcome& result) {
    result.check(equal(processor.current_state(), before.live), "rejection changed live triple");
    result.check(processor.engine_processor().frame_cursor() == before.engine.frame_cursor(),
                 "rejection advanced audio cursor");
    result.check(processor.requires_reset() == before.fault, "rejection changed fault latch");
    result.check(processor.note_tracker().active_count() == before.notes.active_count(),
                 "rejection changed active ownership");
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        result.check(processor.note_tracker().held_count(key) == before.notes.held_count(key),
                     "rejection changed per-key ownership");
    }
    host::same_mechanics(processor.engine_processor().engine(), before.engine.engine());
    using Engine = std::remove_cvref_t<decltype(before.engine.engine())>;
    auto actual = std::make_unique<Engine>(processor.engine_processor().engine());
    auto expected = std::make_unique<Engine>(before.engine.engine());
    std::array<double, 129> actual_output{}, expected_output{};
    result.check(actual->process(actual_output) && expected->process(expected_output) &&
                 actual_output == expected_output,
                 "rejection changed the 129-sample pre-gain continuation");
}

template<class Sample>
void preflight_case(unsigned rejection, InstrumentState replacement, bool replace_after,
                    Outcome& result) {
    auto rig = std::make_unique<host::ProcessorHost<Sample, 64>>();
    constexpr InstrumentState warm{0.5F, true, 0.625F};
    result.check(recall(rig->processor, warm, result, 1), "warm recall failed");
    result.check(flush(rig->processor, rig->sample_size, result) == sdk::kResultOk,
                 "warm flush failed");
    host::EventList seed;
    seed.count = 2;
    seed.values[0] = host::note_on(0, 77, 68);
    seed.values[1] = host::note_on(4, -1, 70, 0.625F, 1);
    rig->data.inputEvents = &seed;
    ++result.process_calls;
    result.check(rig->processor.process(rig->data) == sdk::kResultOk, "warm notes failed");
    rig->data.inputEvents = nullptr;
    ++result.process_calls;
    result.check(rig->processor.process(rig->data) == sdk::kResultOk &&
                 rig->processor.engine_processor().engine().mechanical_energy_joules() > 0.0,
                 "warm fixture has no mechanical history");
    const auto before = std::make_unique<host::ProcessorSnapshot>(rig->processor);
    result.check(recall(rig->processor, first, result, 3), "pending first recall failed");

    std::barrier phase{2};
    GatedEvents events;
    events.phase = &phase;
    events.events.count = 2;
    events.events.values[0] = host::note_on(0, 42, 68);
    events.events.values[1] = host::note_on(0, rejection == 2 ? 42 : 43, 69);
    GatedBadExpression bad;
    bad.phase = &phase;
    ThreeQueues changes;
    changes.count = rejection == 1 ? 1 : 0;
    changes.values[0] = &bad;
    rig->data.inputParameterChanges = &changes;
    rig->data.inputEvents = rejection == 1 ? nullptr : &events;
    rig->fill(Sample{123});
    sdk::tresult status = sdk::kInternalError;
    // During this process() call only audio touches queues, events, buffers and
    // non-atomic diagnostics. Main calls only the thread-safe state interface.
    std::jthread audio([&] { status = rig->processor.process(rig->data); });
    phase.arrive_and_wait();
    result.check(recall(rig->processor, replacement, result, 1), "preflight replacement failed");
    result.check(saved(rig->processor, replacement, result, 2), "pending triple not saved");
    phase.arrive_and_wait();
    audio.join();
    ++result.process_calls;
    result.check(status == (rejection == 0 ? sdk::kResultOk : sdk::kResultFalse),
                 "preflight returned the wrong status");
    result.check(saved(rig->processor, replacement, result, 5),
                 "preflight lost the latest requested expression");
    if (rejection != 0) {
        if (rejection == 1) { result.check(bad.calls == 2, "late invalid point was not read"); }
        unchanged_audio(rig->processor, *before, result);
        rig->expect_silence();
    } else {
        auto control = std::make_unique<host::ProcessorHost<Sample, 64>>();
        result.check(recall(control->processor, replacement, result), "control recall failed");
        control->data.inputEvents = &events.events;
        ++result.process_calls;
        result.check(control->processor.process(control->data) == sdk::kResultOk &&
                     rig->left == control->left && rig->right == control->right,
                     "selected recall used a stale expression or mechanical state");
        result.check(equal(rig->processor.current_state(), replacement),
                     "accepted preflight did not install the complete triple");
        host::same_mechanics(rig->processor.engine_processor().engine(),
                             control->processor.engine_processor().engine());
    }
    // After either accepted or rejected preflight, a new request may have the
    // same packed value or differ only in expression. Both still require reset.
    auto retry_state = replacement;
    if (replace_after) {
        retry_state.expression = equal(replacement, expression_only) ? 0.25F : 0.75F;
        result.check(recall(rig->processor, retry_state, result, 1),
                     "post-preflight expression-only replacement failed");
    } else if (rejection == 0) {
        result.check(recall(rig->processor, retry_state, result, 1),
                     "post-acceptance identical request failed");
    }
    result.check(flush(rig->processor, rig->sample_size, result) == sdk::kResultOk,
                 "pending recall retry failed");
    result.check(equal(rig->processor.current_state(), retry_state) &&
                 rig->processor.engine_processor().frame_cursor() == 0 &&
                 rig->processor.note_tracker().active_count() == 0 &&
                 rig->processor.engine_processor().engine().mechanical_energy_joules() == 0.0,
                 "retry did not install/reset the latest complete triple");
    result.check(saved(rig->processor, retry_state, result, 1), "retry saved stale expression");
    rig->data.inputEvents = nullptr;
    rig->data.inputParameterChanges = nullptr;
    for (unsigned block = 1; block <= 2; ++block) {
        ++result.process_calls;
        result.check(rig->processor.process(rig->data) == sdk::kResultOk &&
                     rig->processor.engine_processor().frame_cursor() == 64U * block &&
                     equal(rig->processor.current_state(), retry_state),
                     "recall was consumed again after a successful retry");
    }
}

void host_preflight() {
    Outcome result;
    for (unsigned rejection = 0; rejection < 3; ++rejection) {
        for (const auto replacement : {first, expression_only, second}) {
            for (const bool replace_after : {false, true}) {
                preflight_case<float>(rejection, replacement, replace_after, result);
                preflight_case<double>(rejection, replacement, replace_after, result);
            }
        }
    }
    std::cout << "host ordered_scenarios=36 formats=2\n";
    result.report("host_preflight");
}

// The observer owns this stream exclusively. Its first byte is already written
// when the gate opens, so replacing/consuming the mailbox while it is paused
// must not alter the remainder or version/extent of this captured snapshot.
struct GatedWriteStream final : host::FixedHostObject<sdk::IBStream> {
    host::MemoryStream storage;
    std::barrier<>* phase = nullptr;
    sdk::tresult PLUGIN_API read(void* buffer, sdk::int32 requested,
                                sdk::int32* transferred) override {
        return storage.read(buffer, requested, transferred);
    }
    sdk::tresult PLUGIN_API write(void* buffer, sdk::int32 requested,
                                 sdk::int32* transferred) override {
        const auto status = storage.write(buffer, requested, transferred);
        if (storage.write_calls == 1) {
            phase->arrive_and_wait();
            phase->arrive_and_wait();
        }
        return status;
    }
    sdk::tresult PLUGIN_API seek(sdk::int64 offset, sdk::int32 mode,
                                sdk::int64* position) override {
        return storage.seek(offset, mode, position);
    }
    sdk::tresult PLUGIN_API tell(sdk::int64* position) override { return storage.tell(position); }
};

void adaptive_snapshots() {
    Outcome result;
    constexpr std::array old_states{second, first, first, first};
    constexpr std::array new_states{subnormal_expression, second, expression_only, first};
    for (std::size_t transition = 0; transition < old_states.size(); ++transition) {
        for (const bool reject : {false, true}) {
            auto rig = std::make_unique<host::ProcessorHost<double, 64>>();
            result.check(recall(rig->processor, old_states[transition], result),
                         "snapshot starting recall failed");
            std::barrier phase{2};
            Outcome observer_result;
            std::jthread observer([&] {
                GatedWriteStream stream;
                stream.phase = &phase;
                stream.storage.bytes.fill(0xa5U);
                stream.storage.transfer_limit = 1;
                ++observer_result.get_calls;
                observer_result.check(rig->processor.getState(&stream) == sdk::kResultOk &&
                                      matches(stream.storage, old_states[transition]) &&
                                      stream.storage.write_calls == packet(old_states[transition]).size,
                                      "adaptive writer mixed snapshots or changed packet extent");
            });
            phase.arrive_and_wait();
            result.check(recall(rig->processor, new_states[transition], result, 1),
                         "recall during partial save failed");
            host::EventList events;
            events.count = 2;
            events.values[0] = host::note_on(0, 41, 68);
            events.values[1] = host::note_on(0, reject ? 41 : 42, 69);
            rig->data.inputEvents = &events;
            ++result.process_calls;
            result.check(rig->processor.process(rig->data) ==
                         (reject ? sdk::kResultFalse : sdk::kResultOk),
                         "processing during partial save returned wrong status");
            result.check(saved(rig->processor, new_states[transition], result, 3),
                         "latest state was not separately saveable during older save");
            phase.arrive_and_wait();
            observer.join();
            result.merge(observer_result);
            result.check(saved(rig->processor, new_states[transition], result, 1),
                         "finishing an older save changed the latest triple");
            result.check(flush(rig->processor, rig->sample_size, result) == sdk::kResultOk &&
                         equal(rig->processor.current_state(), new_states[transition]),
                         "partial save lost pending recall on retry");
        }
    }
    std::cout << "adaptive ordered_scenarios=8 max_transfer_bytes=1\n";
    result.report("adaptive_snapshots");
}

// The callback oracle identifies a selected recall from its unautomated gain
// tag. If no recall was selected, the cursor proves continuation and the prior
// audio-owner expression is the anchor. The raw engine copy excludes both
// gain and expression, so a stale mailbox anchor cannot agree by construction.
template<class Sample>
void live_anchor_case(Outcome& total) {
    constexpr std::size_t blocks = 48;
    constexpr std::size_t frames = 256;
    constexpr std::array anchor_states{
        InstrumentState{0.125F, true, 0.25F},
        InstrumentState{0.375F, false, 0.75F},
        InstrumentState{0.625F, true, 1.0F},
        InstrumentState{0.875F, false, 0.125F}};
    auto rig = std::make_unique<host::ProcessorHost<Sample, frames>>();
    host::ParameterQueue expression;
    expression.id = rhodes::vst3::kExpressionId;
    expression.count = 1;
    expression.values[0] = {static_cast<sdk::int32>(frames - 1), 0.5};
    ThreeQueues changes;
    changes.count = 1;
    changes.values[0] = &expression;
    rig->data.inputParameterChanges = &changes;
    host::EventList events;
    events.count = 1;
    events.values[0] = host::note_on(0, 40, 68, 0.75F);
    rig->data.inputEvents = &events;
    // Seed cursor > 0 so a reset and a continued block are distinguishable.
    ++total.process_calls;
    total.check(rig->processor.process(rig->data) == sdk::kResultOk, "anchor seed process failed");
    std::barrier phase{2};
    Outcome writer_result;
    std::jthread writer([&] {
        for (std::size_t block = 0; block < blocks; ++block) {
            phase.arrive_and_wait();
            // Finite work, no spin/poll loop. The actual mid-render interleaving
            // is scheduler dependent; the sample oracle covers every outcome.
            for (std::size_t write = 0; write < 128; ++write) {
                const auto value = anchor_states[(write + block) % anchor_states.size()];
                writer_result.check(recall(rig->processor, value, writer_result,
                                          static_cast<sdk::int32>(write % 20 + 1)),
                                    "anchor concurrent recall failed");
            }
            phase.arrive_and_wait();
        }
    });
    std::size_t selected_count = 0;
    std::size_t differing_published = 0;
    std::size_t audible_samples = 0;
    for (std::size_t block = 0; block < blocks; ++block) {
        auto reference = std::make_unique<rhodes::vst3::HostEventProcessor>(
            rig->processor.engine_processor());
        const auto previous = rig->processor.current_state();
        const auto previous_cursor = reference->frame_cursor();
        phase.arrive_and_wait();
        ++total.process_calls;
        total.check(rig->processor.process(rig->data) == sdk::kResultOk,
                    "anchor concurrent process failed");
        phase.arrive_and_wait();
        const auto live = rig->processor.current_state();
        const auto cursor = rig->processor.engine_processor().frame_cursor();
        InstrumentState anchor = previous;
        if (cursor == frames) {
            ++selected_count;
            bool found = false;
            for (const auto candidate : anchor_states) {
                if (std::bit_cast<std::uint32_t>(candidate.monitoring_gain) ==
                        std::bit_cast<std::uint32_t>(live.monitoring_gain)) {
                    anchor = candidate;
                    found = true;
                }
            }
            total.check(found, "selected recall has no unique independent gain tag");
            reference->reset();
            total.check(reference->set_sustain(anchor.sustain), "anchor reference sustain failed");
        } else {
            total.check(cursor == previous_cursor + frames,
                        "anchor cursor is neither reset nor continued");
        }
        total.check(live.sustain == anchor.sustain &&
                    std::bit_cast<std::uint32_t>(live.monitoring_gain) ==
                        std::bit_cast<std::uint32_t>(anchor.monitoring_gain) &&
                    std::bit_cast<std::uint32_t>(live.expression) == 0x3f000000U,
                    "anchor process committed an unexpected triple");
        if (!equal(live, rig->processor.published_state())) { ++differing_published; }
        std::array<double, frames> volts{};
        constexpr std::array physical{rhodes::EngineEvent{0, 0, rhodes::EventType::note_on, 40, 0.75}};
        total.check(reference->process(volts, physical).succeeded(), "raw anchor reference failed");
        for (std::size_t frame = 0; frame < frames; ++frame) {
            // Exact dyadic barycentric interpolation, intentionally independent
            // of production's left + delta*fraction evaluation.
            const double expression_value =
                (static_cast<double>(anchor.expression) * static_cast<double>(frames - frame - 1) +
                 0.5 * static_cast<double>(frame + 1)) / static_cast<double>(frames);
            const double monitored = (32.0 * static_cast<double>(anchor.monitoring_gain)) * volts[frame];
            const auto wanted = static_cast<Sample>(expression_value == 1.0 ?
                                                    monitored : monitored * expression_value);
            total.check(rig->left[frame] == wanted && rig->right[frame] == wanted,
                        "sample interpolation used a stale or unpublished expression anchor");
            if (wanted != Sample{0}) { ++audible_samples; }
        }
        const auto& actual_engine = rig->processor.engine_processor().engine();
        total.check(actual_engine.mechanical_energy_joules() ==
                        reference->engine().mechanical_energy_joules(),
                    "anchor process changed pre-gain mechanical energy");
        for (std::size_t key = 0; key < rhodes::key_count; ++key) {
            for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
                const auto actual_mode = actual_engine.modal_state(key, mode);
                const auto expected_mode = reference->engine().modal_state(key, mode);
                total.check(actual_mode.has_value() && expected_mode.has_value() &&
                            actual_mode->displacement_m == expected_mode->displacement_m &&
                            actual_mode->velocity_m_per_s == expected_mode->velocity_m_per_s,
                            "anchor process changed a pre-gain modal state");
            }
        }
    }
    writer.join();
    total.merge(writer_result);
    total.check(audible_samples > 0, "anchor comparison was vacuously silent");
    total.check(recall(rig->processor, final_recall, total, 1), "anchor final recall failed");
    total.check(flush(rig->processor, rig->sample_size, total) == sdk::kResultOk &&
                equal(rig->processor.current_state(), final_recall), "anchor final recall lost");
    std::cout << "anchor bits=" << sizeof(Sample) * 8 << " blocks=" << blocks
              << " selected_recalls=" << selected_count
              << " differing_published_after_join=" << differing_published
              << " audible_samples=" << audible_samples << '\n';
}

void live_anchors() {
    Outcome result;
    live_anchor_case<float>(result);
    live_anchor_case<double>(result);
    result.report("live_anchors");
}

template<class Sample>
void callback_stress_case(double rate, Outcome& total) {
    constexpr std::size_t blocks = 256;
    constexpr std::size_t state_calls = 4096;
    auto rig = std::make_unique<host::ProcessorHost<Sample, 64>>(rate);
    auto gain = std::make_unique<host::ParameterQueue>();
    auto expression = std::make_unique<host::ParameterQueue>();
    auto pedal = std::make_unique<host::ParameterQueue>();
    gain->id = rhodes::vst3::kGainId;
    expression->id = rhodes::vst3::kExpressionId;
    pedal->id = rhodes::vst3::kSustainId;
    gain->count = expression->count = pedal->count = 1;
    ThreeQueues changes;
    changes.count = 3;
    changes.values = {gain.get(), expression.get(), pedal.get()};
    host::EventList events;
    events.values[0] = host::note_on(0, 42, 68);
    events.values[1] = host::note_on(0, 42, 69);
    rig->data.inputEvents = &events;
    rig->data.inputParameterChanges = &changes;
    std::barrier phase{3};
    Outcome audio_result, writer_result, observer_result;
    std::jthread audio([&] {
        phase.arrive_and_wait();
        for (std::size_t block = 0; block < blocks; ++block) {
            const auto wanted = block % 2 == 0 ? automation_a : automation_b;
            gain->values[0] = {0, static_cast<double>(wanted.monitoring_gain)};
            expression->values[0] = {0, static_cast<double>(wanted.expression)};
            pedal->values[0] = {0, wanted.sustain ? 1.0 : 0.0};
            const bool reject = block % 11 == 0;
            events.count = reject ? 2 : 1;
            const auto previous = rig->processor.current_state();
            const auto cursor = rig->processor.engine_processor().frame_cursor();
            const auto energy = rig->processor.engine_processor().engine().mechanical_energy_joules();
            const auto owners = rig->processor.note_tracker().active_count();
            ++audio_result.process_calls;
            audio_result.check(rig->processor.process(rig->data) ==
                               (reject ? sdk::kResultFalse : sdk::kResultOk),
                               "concurrent block returned wrong status");
            audio_result.check(coherent(rig->processor.current_state()) &&
                               coherent(rig->processor.published_state()),
                               "concurrent state is not a complete permitted triple");
            if (reject) {
                audio_result.check(equal(rig->processor.current_state(), previous) &&
                                   rig->processor.engine_processor().frame_cursor() == cursor &&
                                   rig->processor.engine_processor().engine().mechanical_energy_joules() == energy &&
                                   rig->processor.note_tracker().active_count() == owners,
                                   "concurrent rejection mutated audio-owned state");
            } else {
                audio_result.check(equal(rig->processor.current_state(), wanted) &&
                                   rig->processor.note_tracker().active_count() == 1,
                                   "concurrent automation lost an expression field");
            }
            for (std::size_t frame = 0; frame < rig->left.size(); ++frame) {
                audio_result.check(std::isfinite(rig->left[frame]) &&
                                   rig->left[frame] == rig->right[frame] &&
                                   (!reject || rig->left[frame] == Sample{0}),
                                   "concurrent output is nonfinite, mismatched or uncleared");
            }
        }
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        audio_result.check(flush(rig->processor, rig->sample_size, audio_result) == sdk::kResultOk &&
                           equal(rig->processor.current_state(), final_recall) &&
                           rig->processor.engine_processor().frame_cursor() == 0 &&
                           rig->processor.note_tracker().active_count() == 0 &&
                           rig->processor.engine_processor().engine().mechanical_energy_joules() == 0.0,
                           "quiescent final recall did not reset/install exactly once");
        rig->data.inputEvents = nullptr;
        rig->data.inputParameterChanges = nullptr;
        for (unsigned block = 1; block <= 2; ++block) {
            ++audio_result.process_calls;
            audio_result.check(rig->processor.process(rig->data) == sdk::kResultOk &&
                               rig->processor.engine_processor().frame_cursor() == 64U * block &&
                               equal(rig->processor.current_state(), final_recall),
                               "final pending recall repeated on a later block");
        }
    });
    std::jthread observer([&] {
        phase.arrive_and_wait();
        for (std::size_t index = 0; index < state_calls; ++index) {
            observer_result.check(coherent_save(rig->processor, observer_result,
                                                static_cast<sdk::int32>(index % 20 + 1)),
                                  "observer saved a torn triple, version or length");
        }
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        observer_result.check(saved(rig->processor, final_recall, observer_result, 1),
                              "observer final snapshot lost expression");
    });
    phase.arrive_and_wait();
    for (std::size_t index = 0; index < state_calls; ++index) {
        const auto wanted = recalls[index % recalls.size()];
        host::MemoryStream stream;
        load(stream, wanted, static_cast<sdk::int32>(index % 20 + 1));
        const bool invalid = index % 23 == 0;
        if (invalid) {
            // Build an explicit v2 invalid expression even for a v1/unity input.
            stream.bytes[4] = 2;
            stream.extent = 20;
            stream.bytes[16] = 1;
            stream.bytes[17] = 0;
            stream.bytes[18] = 0;
            stream.bytes[19] = 0x80U; // negative minimum subnormal
        }
        ++writer_result.set_calls;
        writer_result.check(rig->processor.setState(&stream) ==
                            (invalid ? sdk::kResultFalse : sdk::kResultOk),
                            "concurrent state expression validation differs");
        writer_result.check(coherent_save(rig->processor, writer_result, 7),
                            "writer saved a torn complete configuration");
    }
    // All concurrent workloads quiesce before the final request. Its exact
    // once-only consumption is deterministic, unlike the stress interleavings.
    phase.arrive_and_wait();
    writer_result.check(recall(rig->processor, final_recall, writer_result, 1),
                        "final writer recall failed");
    phase.arrive_and_wait();
    audio.join();
    observer.join();
    total.merge(audio_result);
    total.merge(writer_result);
    total.merge(observer_result);
    total.check(saved(rig->processor, final_recall, total, 1), "quiescent final bytes differ");
    std::cout << "stress bits=" << sizeof(Sample) * 8 << " rate=" << rate
              << " blocks=" << blocks << " writer_iterations=" << state_calls
              << " observer_iterations=" << state_calls << '\n';
}

void callback_stress() {
    Outcome result;
    callback_stress_case<float>(48000.0, result);
    callback_stress_case<double>(192000.0, result);
    result.report("callback_stress");
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
        run("mailbox_ordered", mailbox_ordered);
        run("host_preflight", host_preflight);
        run("adaptive_snapshots", adaptive_snapshots);
        run("live_anchors", live_anchors);
        run("callback_stress", callback_stress);
        host::require(found, "unknown expression concurrency selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
