#include "tests/cpp/vst3_test_support.hpp"

#include <bit>
#include <cfenv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

// This executable alone is linked with GNU ld's --wrap for the two symbols
// below. The production processor and core are compiled without a test seam.
// A wrapper always executes the real public engine method before changing its
// returned voltage/result. It never reads or writes a private member.
#if !defined(__linux__) || !defined(__ELF__) || !defined(__GNUC__)
#error "This fault executable requires the explicitly registered Linux ELF wrapper lane"
#endif
#if defined(__FAST_MATH__)
#error "Fault classification requires finite/nonfinite checks; fast math is unsupported"
#endif
static_assert(sizeof(std::size_t) == 8);

namespace fault_injection {
using Cell = std::array<double, rhodes::observation_node_count>;

bool real_process(rhodes::ResearchEngine*, std::span<double>) noexcept
    __asm__("__real__ZN6rhodes14ResearchEngine7processESt4spanIdLm18446744073709551615EE");
bool wrapped_process(rhodes::ResearchEngine*, std::span<double>) noexcept
    __asm__("__wrap__ZN6rhodes14ResearchEngine7processESt4spanIdLm18446744073709551615EE");
std::optional<Cell> real_observe_cell(
    const rhodes::ResearchEngine*, const rhodes::FractionalObservationPlan&) noexcept
    __asm__("__real__ZNK6rhodes14ResearchEngine12observe_cellERKNS_25FractionalObservationPlanE");
std::optional<Cell> wrapped_observe_cell(
    const rhodes::ResearchEngine*, const rhodes::FractionalObservationPlan&) noexcept
    __asm__("__wrap__ZNK6rhodes14ResearchEngine12observe_cellERKNS_25FractionalObservationPlanE");

enum class Operation { transparent, source, process_false, process_nan, process_inf,
                       cell_absent, cell_nan, cell_inf };

struct Source {
    double before = 0.0;
    double after = 0.0;
    std::size_t start = 0;
    [[nodiscard]] double at(std::size_t frame) const noexcept {
        return frame < start ? before : after;
    }
};

struct Trace {
    std::size_t process_calls = 0;
    std::size_t process_samples = 0;
    std::size_t observe_calls = 0;
    std::size_t real_process_successes = 0;
    std::size_t real_observe_successes = 0;
    std::size_t real_nonzero_samples = 0;
    std::size_t real_nonzero_observations = 0;
    std::size_t process_overrides = 0;
    std::size_t observe_overrides = 0;
    std::size_t real_nonfinite_values = 0;
    std::size_t instance_mismatches = 0;
    std::size_t recall_calls = 0;
    Steinberg::tresult recall_result = Steinberg::kResultFalse;
    const rhodes::ResearchEngine* instance = nullptr;
};

struct Plan {
    Operation operation = Operation::transparent;
    std::size_t trigger = 0;
    Source source{};
    rhodes::vst3::RhodesProcessor* recall_target = nullptr;
    Steinberg::IBStream* recall_stream = nullptr;
    std::size_t recall_at = 0;
    Trace trace{};
};

Plan* active_plan = nullptr; // Serialized executable; no concurrent audio owner.
Trace totals{};

void observe_instance(Trace& trace, const rhodes::ResearchEngine* engine) noexcept {
    if (trace.instance == nullptr) { trace.instance = engine; }
    else if (trace.instance != engine) { ++trace.instance_mismatches; }
}

bool wrapped_process(rhodes::ResearchEngine* engine,
                                  std::span<double> output) noexcept {
    const bool real_result = real_process(engine, output);
    auto* plan = active_plan;
    if (plan == nullptr) { return real_result; }
    auto& trace = plan->trace;
    observe_instance(trace, engine);
    const auto frame = trace.process_calls++;
    trace.process_samples += output.size();
    trace.real_process_successes += real_result ? 1U : 0U;
    for (const double value : output) {
        trace.real_nonfinite_values += std::isfinite(value) ? 0U : 1U;
        trace.real_nonzero_samples += value != 0.0 ? 1U : 0U;
    }
    if (plan->recall_target != nullptr && frame == plan->recall_at) {
        ++trace.recall_calls;
        trace.recall_result = plan->recall_target->setState(plan->recall_stream);
    }
    if (plan->operation == Operation::source) {
#if !defined(RHODES_QUADRATURE_VST3)
        std::fill(output.begin(), output.end(), plan->source.at(frame));
        ++trace.process_overrides;
#endif
    } else if (frame == plan->trigger) {
        if (plan->operation == Operation::process_false) {
            ++trace.process_overrides;
            return false;
        }
        if (plan->operation == Operation::process_nan ||
            plan->operation == Operation::process_inf) {
            std::fill(output.begin(), output.end(),
                plan->operation == Operation::process_nan ?
                std::numeric_limits<double>::quiet_NaN() :
                std::numeric_limits<double>::infinity());
            ++trace.process_overrides;
        }
    }
    return real_result;
}

std::optional<Cell> wrapped_observe_cell(
    const rhodes::ResearchEngine* engine,
    const rhodes::FractionalObservationPlan& observation_plan) noexcept {
    auto result = real_observe_cell(engine, observation_plan);
    auto* plan = active_plan;
    if (plan == nullptr) { return result; }
    auto& trace = plan->trace;
    observe_instance(trace, engine);
    const auto frame = trace.observe_calls++;
    trace.real_observe_successes += result.has_value() ? 1U : 0U;
    if (result) {
        for (const double value : *result) {
            trace.real_nonfinite_values += std::isfinite(value) ? 0U : 1U;
            trace.real_nonzero_observations += value != 0.0 ? 1U : 0U;
        }
    }
    if (plan->operation == Operation::source) {
        Cell replacement{};
        replacement.fill(plan->source.at(frame));
        result = replacement;
        ++trace.observe_overrides;
    } else if (frame == plan->trigger) {
        if (plan->operation == Operation::cell_absent) {
            ++trace.observe_overrides;
            return std::nullopt;
        }
        if (plan->operation == Operation::cell_nan ||
            plan->operation == Operation::cell_inf) {
            Cell replacement{};
            replacement.fill(0.0);
            replacement[3] = plan->operation == Operation::cell_nan ?
                std::numeric_limits<double>::quiet_NaN() :
                std::numeric_limits<double>::infinity();
            result = replacement;
            ++trace.observe_overrides;
        }
    }
    return result;
}

struct Scope {
    explicit Scope(Plan& plan) {
        rhodes::vst3::test_host::require(active_plan == nullptr, "nested injection scope");
        active_plan = &plan;
    }
    ~Scope() {
        const auto& trace = active_plan->trace;
        totals.process_calls += trace.process_calls;
        totals.process_samples += trace.process_samples;
        totals.observe_calls += trace.observe_calls;
        totals.real_process_successes += trace.real_process_successes;
        totals.real_observe_successes += trace.real_observe_successes;
        totals.real_nonzero_samples += trace.real_nonzero_samples;
        totals.real_nonzero_observations += trace.real_nonzero_observations;
        totals.process_overrides += trace.process_overrides;
        totals.observe_overrides += trace.observe_overrides;
        totals.real_nonfinite_values += trace.real_nonfinite_values;
        totals.instance_mismatches += trace.instance_mismatches;
        totals.recall_calls += trace.recall_calls;
        active_plan = nullptr;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
} // namespace fault_injection

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
using namespace fault_injection;

constexpr std::size_t capacity = 257;
constexpr std::size_t frames = 160;
constexpr std::size_t warm_frames = 128;
constexpr std::size_t no_fault = std::numeric_limits<std::size_t>::max();
constexpr sdk::uint64 untouched_flags = 0x5aU;
constexpr double marker = 91.25;
std::size_t verified_cases = 0;
std::size_t verified_callbacks = 0;
std::size_t verified_prefix_samples = 0;
std::size_t verified_suffix_samples = 0;
std::size_t nonzero_prefix_samples = 0;
long double maximum_relative_error = 0.0L;
std::string context;

#if defined(RHODES_QUADRATURE_VST3)
constexpr const char* variant = "quadrature";
#elif defined(RHODES_FILTERED_VST3)
constexpr const char* variant = "filtered";
#else
constexpr const char* variant = "direct";
#endif

struct Layout { sdk::int32 channels; bool omitted; };
constexpr std::array<Layout, 4> layouts{{{1, false}, {2, false}, {1, true}, {2, true}}};
constexpr std::array<double, 3> rates{48000.0, 96000.0, 192000.0};

struct ThreeQueues final : FixedHostObject<vst::IParameterChanges> {
    std::array<vst::IParamValueQueue*, 3> queues{};
    sdk::int32 count = 0;
    sdk::int32 PLUGIN_API getParameterCount() override { return count; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        return index >= 0 && index < count ? queues[static_cast<std::size_t>(index)] : nullptr;
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&,
                                                     sdk::int32&) override { return nullptr; }
};

void points(ParameterQueue& queue, std::initializer_list<ParameterQueue::Point> values) {
    require(values.size() <= queue.values.size(), "point fixture capacity");
    std::copy(values.begin(), values.end(), queue.values.begin());
    queue.count = static_cast<sdk::int32>(values.size());
}

template<class Sample>
constexpr const char* format() { return std::is_same_v<Sample, float> ? "float" : "double"; }

std::size_t factor(double rate) {
#if defined(RHODES_FILTERED_VST3)
    return rate <= 48000.0 ? 4U : rate <= 96000.0 ? 2U : 1U;
#else
    (void)rate;
    return 1;
#endif
}

void literal_state(MemoryStream& stream, InstrumentState state) {
    stream.bytes.fill(0);
    stream.bytes[0] = 'R'; stream.bytes[1] = 'H';
    stream.bytes[2] = 'S'; stream.bytes[3] = 'T';
    stream.bytes[4] = state.expression == 1.0F ? 1 : 2;
    stream.bytes[6] = static_cast<std::uint8_t>(kStateModel);
    const auto put = [&](std::size_t offset, float value) {
        const auto word = std::bit_cast<std::uint32_t>(value);
        for (std::size_t byte = 0; byte < 4; ++byte) {
            stream.bytes[offset + byte] = static_cast<std::uint8_t>(word >> (8U * byte));
        }
    };
    put(8, state.monitoring_gain);
    put(12, state.sustain ? 1.0F : 0.0F);
    if (state.expression != 1.0F) { put(16, state.expression); }
    stream.position = 0;
    stream.extent = state.expression == 1.0F ? 16 : 20;
}

void saved_state(RhodesProcessor& processor, InstrumentState expected) {
    MemoryStream actual, literal;
    literal_state(literal, expected);
    require(processor.getState(&actual) == sdk::kResultOk, "save fault configuration");
    require(actual.extent == literal.extent &&
        std::equal(actual.bytes.begin(), actual.bytes.begin() + actual.extent,
                   literal.bytes.begin()), "saved fault configuration bytes");
}

template<class Sample>
struct Harness {
    ProcessorHost<Sample, capacity> host;
    std::array<Sample*, 1> mono{host.left.data()};
    ParameterQueue gain, expression, pedal;
    ThreeQueues parameters;
    EventList events;
    Layout layout;
    double rate;

    Harness(double sample_rate, Layout shape) : host(sample_rate), layout(shape), rate(sample_rate) {
        require(host.processor.setProcessing(false) == sdk::kResultOk, "suspend for layout");
        require(host.processor.setActive(false) == sdk::kResultOk, "deactivate for layout");
        auto arrangement = shape.channels == 1 ? vst::SpeakerArr::kMono : vst::SpeakerArr::kStereo;
        require(host.processor.setBusArrangements(nullptr, 0, &arrangement, 1) == sdk::kResultTrue,
                "negotiate fault layout");
        require(host.processor.activateBus(vst::kAudio, vst::kOutput, 0, !shape.omitted) ==
                sdk::kResultOk, "activate or omit output");
        require(host.processor.setActive(true) == sdk::kResultOk, "activate fault host");
        require(host.processor.setProcessing(true) == sdk::kResultOk, "start fault host");
        require(host.processor.getLatencySamples() == kLatencyFrames, "variant latency changed");
        host.output.numChannels = shape.channels;
        if constexpr (std::is_same_v<Sample, float>) {
            host.output.channelBuffers32 = shape.channels == 1 ? mono.data() : host.channels.data();
        } else {
            host.output.channelBuffers64 = shape.channels == 1 ? mono.data() : host.channels.data();
        }
        host.data.numOutputs = shape.omitted ? 0 : 1;
        // Keep a real supplied object as a write witness, even when numOutputs
        // says it is omitted. No invalid pointer or private-memory trick is used.
        host.data.outputs = &host.output;
        expression.id = kExpressionId;
        pedal.id = kSustainId;
        parameters.queues = {&gain, &expression, &pedal};
        parameters.count = 3;
        clear_inputs();
    }

    void clear_inputs() {
        gain.count = expression.count = pedal.count = events.count = 0;
        host.data.inputParameterChanges = nullptr;
        host.data.inputEvents = nullptr;
    }

    void controls(float g, float e, bool sustain = false) {
        points(gain, {{0, g}}); points(expression, {{0, e}});
        points(pedal, {{0, sustain ? 1.0 : 0.0}});
        host.data.inputParameterChanges = &parameters;
        host.data.numSamples = 0;
        ++verified_callbacks;
        require(host.processor.process(host.data) == sdk::kResultOk, "valid initial control flush");
        require(host.processor.current_state() == InstrumentState{g, sustain, e},
                "initial live controls");
        require(host.processor.published_state() == InstrumentState{g, sustain, e},
                "initial published controls");
        clear_inputs();
    }

    void note(std::size_t future = no_fault) {
        events.values[0] = note_on(0, 100, 68);
        events.count = 1;
        if (future != no_fault) {
            events.values[1] = note_on(static_cast<sdk::int32>(future), 200, 72);
            events.count = 2;
        }
        host.data.inputEvents = &events;
    }

    sdk::tresult run(Plan& plan, std::size_t count) {
        require(count < capacity, "canary needs undeclared suffix");
        host.fill(static_cast<Sample>(marker));
        host.output.silenceFlags = untouched_flags;
        host.data.numSamples = static_cast<sdk::int32>(count);
        Scope scope(plan);
        ++verified_callbacks;
        return host.processor.process(host.data);
    }
};

void trace_matches(const Plan& plan, std::size_t process_calls,
                   std::size_t observe_calls, double rate) {
    const auto& trace = plan.trace;
    require(trace.process_calls == process_calls, "wrapper process call count: missing interception");
    require(trace.process_samples == process_calls * factor(rate), "wrapped span size/rate factor");
#if defined(RHODES_QUADRATURE_VST3)
    require(trace.observe_calls == observe_calls, "Q6 observation wrapper call count");
#else
    (void)observe_calls;
    require(trace.observe_calls == 0, "non-Q6 unexpectedly observed cells");
#endif
    require(trace.real_process_successes == trace.process_calls &&
            trace.real_observe_successes == trace.observe_calls,
            "real engine unexpectedly failed before override");
    require(trace.real_nonfinite_values == 0 && trace.instance_mismatches == 0,
            "real engine output/instance witness failed");
    if (process_calls + trace.observe_calls > 0) {
        require(trace.instance != nullptr, "no real engine instance was intercepted");
    }
    if (plan.operation == Operation::source) {
#if defined(RHODES_QUADRATURE_VST3)
        require(trace.process_overrides == 0 && trace.observe_overrides == observe_calls,
                "Q6 source must replace cells, not discarded endpoints");
#else
        require(trace.process_overrides == process_calls && trace.observe_overrides == 0,
                "source override count");
#endif
    }
}

// Independent, chronological long-double convolution of the published fixed
// coefficients. This uses neither production FIR class nor processor rendering.
// The large test inputs and all controls are exact dyadic values. Fault limits
// have an explicit margin; a close rounding-boundary case is rejected below.
std::vector<long double> reference_volts(double rate, Source source, std::size_t count) {
    std::vector<long double> result(count);
    const auto sample = [&](std::ptrdiff_t host_frame) -> long double {
        return host_frame < 0 ? 0.0L : source.at(static_cast<std::size_t>(host_frame));
    };
    for (std::size_t frame = 0; frame < count; ++frame) {
        long double sum = 0.0L;
#if defined(RHODES_QUADRATURE_VST3)
        (void)rate;
        for (std::size_t node = 0; node < quadrature_filter::node_count; ++node) {
            for (std::size_t age = 0; age < quadrature_filter::history_frames; ++age) {
                sum += static_cast<long double>(quadrature_filter::coefficients[node][age]) *
                    sample(static_cast<std::ptrdiff_t>(frame) - 1 -
                           static_cast<std::ptrdiff_t>(age));
            }
        }
#elif defined(RHODES_FILTERED_VST3)
        const auto up = factor(rate);
        const std::span<const double> coefficients = up == 4 ?
            std::span<const double>(audio_filter::coefficients_l4) : up == 2 ?
            std::span<const double>(audio_filter::coefficients_l2) :
            std::span<const double>(audio_filter::coefficients_l1);
        for (std::size_t age = 0; age < coefficients.size(); ++age) {
            const auto internal = static_cast<std::ptrdiff_t>(frame * up) -
                                  static_cast<std::ptrdiff_t>(age);
            if (internal >= 0) {
                sum += static_cast<long double>(coefficients[age]) *
                       sample(internal / static_cast<std::ptrdiff_t>(up));
            }
        }
#else
        (void)rate;
        sum = sample(static_cast<std::ptrdiff_t>(frame));
#endif
        result[frame] = sum;
    }
    return result;
}

template<class Sample>
std::size_t first_fault(const std::vector<long double>& volts, std::size_t begin,
                        double gain, double expression, bool monitoring) {
    const long double limit = monitoring ? std::numeric_limits<double>::max() :
                                           std::numeric_limits<Sample>::max();
    for (std::size_t frame = begin; frame < volts.size(); ++frame) {
        const long double monitored = (32.0L * gain) * volts[frame];
        const long double value = monitoring ? monitored : monitored * expression;
        const long double ratio = std::abs(value) / limit;
        require(std::abs(ratio - 1.0L) > 1.0e-4L,
                "fixture too close to arithmetic overflow threshold");
        if (ratio > 1.0L) { return frame - begin; }
    }
    return no_fault;
}

template<class Sample>
std::size_t outputs(const Harness<Sample>& harness, std::size_t count,
                    std::size_t failure, const std::vector<long double>* volts = nullptr,
                    std::size_t begin = 0, double gain = 1.0, double expression = 1.0) {
    const auto& host = harness.host;
    std::size_t nonzero = 0;
    const std::size_t prefix = failure == no_fault ? count : failure;
    for (std::size_t channel = 0; channel < 2; ++channel) {
        const auto& buffer = channel == 0 ? host.left : host.right;
        const bool supplied = !harness.layout.omitted &&
                              channel < static_cast<std::size_t>(harness.layout.channels);
        for (std::size_t frame = 0; frame < capacity; ++frame) {
            if (!supplied || frame >= count) {
                require(buffer[frame] == static_cast<Sample>(marker), "undeclared output/canary changed");
            } else if (frame >= prefix) {
                using Bits = std::conditional_t<std::is_same_v<Sample, float>,
                                                std::uint32_t, std::uint64_t>;
                require(std::bit_cast<Bits>(buffer[frame]) == 0, "failed sample/suffix not cleared");
                ++verified_suffix_samples;
            } else {
                require(std::isfinite(buffer[frame]), "prefix contains nonfinite sample");
                if (channel == 0 && buffer[frame] != Sample{0}) { ++nonzero; }
                if (volts != nullptr) {
                    const long double expected = ((32.0L * gain) * (*volts)[begin + frame]) * expression;
                    if (expected == 0.0L) {
                        require(buffer[frame] == Sample{0}, "zero expression or source produced audio");
                    } else {
                        const long double relative = std::abs(buffer[frame] - expected) / std::abs(expected);
                        maximum_relative_error = std::max(maximum_relative_error, relative);
                        if constexpr (std::is_same_v<Sample, float>) {
                            const auto expected_float = static_cast<float>(expected);
                            const auto a = std::bit_cast<std::uint32_t>(buffer[frame]);
                            const auto b = std::bit_cast<std::uint32_t>(expected_float);
                            const auto distance = a > b ? a - b : b - a;
                            require(distance <= 1, "finite float prefix differs by more than one ULP");
                        } else {
                            require(relative <= 256.0L * std::numeric_limits<double>::epsilon(),
                                    "finite double prefix exceeds convolution rounding bound");
                        }
                    }
                }
                ++verified_prefix_samples;
            }
        }
    }
    if (harness.layout.omitted) {
        require(host.output.silenceFlags == untouched_flags, "omitted output silence flags changed");
    } else {
        const auto silent = harness.layout.channels == 1 ? 1U : 3U;
        require(host.output.silenceFlags == (nonzero == 0 ? silent : 0U),
                "fault prefix silence mask is incorrect");
        if (harness.layout.channels == 2) {
            require(host.left == host.right, "fault stereo channels differ");
        }
    }
    nonzero_prefix_samples += nonzero;
    return nonzero;
}

template<class Sample>
void identity(const char* name, const Harness<Sample>& harness, std::size_t failure,
              const Plan& plan, bool upstream) {
    ++verified_cases;
    std::cout << "case=" << name << " variant=" << variant << " format=" << format<Sample>()
              << " rate=" << harness.rate << " channels=" << harness.layout.channels
              << " omitted=" << harness.layout.omitted << " first_failure=";
    if (failure == no_fault) { std::cout << "none"; } else { std::cout << failure; }
    std::cout << " upstream=" << upstream
              << " cursor=" << harness.host.processor.engine_processor().frame_cursor()
              << " process_calls=" << plan.trace.process_calls
              << " process_samples=" << plan.trace.process_samples
              << " observe_calls=" << plan.trace.observe_calls
              << " process_overrides=" << plan.trace.process_overrides
              << " observe_overrides=" << plan.trace.observe_overrides
              << " real_nonzero=" << plan.trace.real_nonzero_samples
              << " real_observed_nonzero=" << plan.trace.real_nonzero_observations << '\n';
}

template<class Sample>
void prime(Harness<Sample>& harness, Source source) {
    harness.controls(0.0F, 1.0F);
    Plan plan;
    plan.operation = Operation::source; plan.source = source;
    require(harness.run(plan, warm_frames) == sdk::kResultOk, "finite zero-gain filter priming");
    trace_matches(plan, warm_frames, warm_frames, harness.rate);
    require(!harness.host.processor.requires_reset(), "priming latched an unexpected filter fault");
    const auto volts = reference_volts(harness.rate, source, warm_frames);
    (void)outputs(harness, warm_frames, no_fault, &volts, 0, 0.0, 1.0);
}

template<class Sample>
void rejected_after_fault(Harness<Sample>& harness) {
    ProcessorSnapshot snapshot(harness.host.processor);
    harness.clear_inputs();
    Plan rejected;
    require(harness.run(rejected, 17) == sdk::kResultFalse, "fault did not fence next positive block");
    trace_matches(rejected, 0, 0, harness.rate);
    (void)outputs(harness, 17, 0);
    snapshot.expect_unchanged(harness.host.processor);
    points(harness.expression, {{0, 0.125}});
    harness.host.data.inputParameterChanges = &harness.parameters;
    Plan flush;
    require(harness.run(flush, 0) == sdk::kResultFalse, "fault did not fence parameter-only flush");
    trace_matches(flush, 0, 0, harness.rate);
    (void)outputs(harness, 0, 0);
    snapshot.expect_unchanged(harness.host.processor);
    harness.clear_inputs();
}

template<class Sample>
void interception_typed() {
    for (const auto rate : rates) {
        for (const auto layout : layouts) {
            Harness<Sample> harness(rate, layout);
            harness.controls(0.5F, 0.75F);
            harness.note();
            Plan plan;
            require(harness.run(plan, frames) == sdk::kResultOk, "transparent wrapped render failed");
            trace_matches(plan, frames, frames, rate);
            require(plan.trace.process_overrides == 0 && plan.trace.observe_overrides == 0,
                    "transparent wrapper changed returned data");
            require(plan.trace.real_nonzero_samples > 0, "transparent wrapper never saw real audio");
#if defined(RHODES_QUADRATURE_VST3)
            require(plan.trace.real_nonzero_observations == 6 * frames,
                    "Q6 real six-node observations were not exercised");
#endif
            require(harness.host.processor.engine_processor().frame_cursor() == frames &&
                    !harness.host.processor.requires_reset(), "transparent cursor/fault state");
            const auto nonzero = outputs(harness, frames, no_fault);
            require(layout.omitted || nonzero > 48, "transparent audible-prefix nonvacuity");
#if defined(RHODES_QUADRATURE_VST3)
            require(layout.omitted || harness.host.left[0] == Sample{0}, "Q6 completed-cell delay");
#endif
            identity("interception", harness, no_fault, plan, false);
        }
    }
}

template<class Sample>
void forced_result_case(Operation operation, std::size_t failure, float expression,
                        Layout layout, double rate) {
    Harness<Sample> harness(rate, layout);
    harness.controls(0.5F, expression);
    harness.note(failure + 3);
    Plan plan; plan.operation = operation; plan.trigger = failure;
    require(harness.run(plan, frames) == sdk::kResultFalse, "injected engine failure was accepted");
    const bool cell_failure = operation == Operation::cell_absent ||
                              operation == Operation::cell_nan || operation == Operation::cell_inf;
    const auto process_calls = failure + (cell_failure ? 0U : 1U);
    trace_matches(plan, process_calls, failure + 1, rate);
    require(plan.trace.process_overrides + plan.trace.observe_overrides == 1,
            "engine fault injection did not fire exactly once");
#if defined(RHODES_FILTERED_VST3) || defined(RHODES_QUADRATURE_VST3)
    const bool upstream = true;
#else
    const bool upstream = operation == Operation::process_false;
#endif
    require(harness.host.processor.requires_reset(), "engine/monitor fault latch absent");
    require(harness.host.processor.engine_processor().requires_reset() == upstream,
            "injected fault reached the wrong subsystem");
    require(harness.host.processor.engine_processor().frame_cursor() == failure + (upstream ? 0U : 1U),
            "failed frame cursor semantics");
    require(harness.host.processor.current_state() == InstrumentState{0.5F, false, expression} &&
            harness.host.processor.published_state() == InstrumentState{0.5F, false, expression},
            "fault controls were not published");
    require(harness.host.processor.note_tracker().active_count() == 2 &&
            harness.host.processor.note_tracker().held_count(40) == 1 &&
            harness.host.processor.note_tracker().held_count(44) == 1,
            "accepted fault lost complete preflighted ownership");
    for (std::size_t mode = 0; mode < modes_per_key; ++mode) {
        const auto future = harness.host.processor.engine_processor().engine().modal_state(44, mode);
        require(future && future->displacement_m == 0.0 && future->velocity_m_per_s == 0.0,
                "future note was dispatched after the fault");
    }
    const auto nonzero = outputs(harness, frames, failure);
    require(layout.omitted || expression == 0.0F || failure == 0 || nonzero > 0,
            "positive expression fault prefix was vacuous");
    require(layout.omitted || expression != 0.0F || nonzero == 0, "muted fault prefix was audible");
    // Execute the same valid host transaction without injection. The retained
    // successful prefix must be byte-identical, not merely finite/nonzero.
    Harness<Sample> peer(rate, {layout.channels, false});
    peer.controls(0.5F, expression);
    peer.note(failure + 3);
    Plan transparent;
    require(peer.run(transparent, frames) == sdk::kResultOk, "unfaulted prefix control render");
    trace_matches(transparent, frames, frames, rate);
    (void)outputs(peer, frames, no_fault);
    if (!layout.omitted) {
        using Bits = std::conditional_t<std::is_same_v<Sample, float>,
                                        std::uint32_t, std::uint64_t>;
        for (std::size_t frame = 0; frame < failure; ++frame) {
            require(std::bit_cast<Bits>(harness.host.left[frame]) ==
                    std::bit_cast<Bits>(peer.host.left[frame]), "fault altered the valid audio prefix");
        }
    }
    if (!cell_failure || failure > 0) {
        require(plan.trace.real_nonzero_samples > 0, "real process was not executed before override");
    }
#if defined(RHODES_QUADRATURE_VST3)
    require(plan.trace.real_nonzero_observations > 0, "real observation was not executed before override");
#endif
    saved_state(harness.host.processor, {0.5F, false, expression});
    const char* label = operation == Operation::process_false ? "engine_false" :
        operation == Operation::process_nan ? "process_nan" :
        operation == Operation::process_inf ? "process_inf" :
        operation == Operation::cell_absent ? "cell_absent" :
        operation == Operation::cell_nan ? "cell_nan" : "cell_inf";
    identity(label, harness, failure, plan, upstream);
    rejected_after_fault(harness);
}

template<class Sample>
void engine_failure_typed() {
    for (const auto layout : layouts) {
        for (const auto failure : {std::size_t{0}, std::size_t{61}}) {
            for (const float expression : {0.0F, 0.5F}) {
                forced_result_case<Sample>(Operation::process_false, failure, expression, layout, 48000.0);
#if defined(RHODES_QUADRATURE_VST3)
                forced_result_case<Sample>(Operation::cell_absent, failure, expression, layout, 48000.0);
                forced_result_case<Sample>(Operation::cell_nan, failure, expression, layout, 48000.0);
                forced_result_case<Sample>(Operation::cell_inf, failure, expression, layout, 48000.0);
#endif
            }
        }
    }
}

template<class Sample>
void monitored_case(double rate, Layout layout, bool warm, float expression) {
    context = std::string("monitored_case ") + format<Sample>() + " " + std::to_string(rate) +
              " warm=" + std::to_string(warm) + " expr=" + std::to_string(expression);
    Harness<Sample> harness(rate, layout);
    // 2^1021 puts the overflow threshold at response 1/4. A 2^1020 step
    // crosses at 1/2, exactly Q6's symmetric midpoint. Retaining power-of-two
    // scaling also avoids extra source-multiplication rounding at FIR zeros.
    Source source{0.015625, std::ldexp(1.0, 1021), warm ? 0U : 8U};
    const auto begin = warm ? warm_frames : 0;
    if (warm) { prime(harness, source); }
    harness.controls(1.0F, expression);
    const auto volts = reference_volts(rate, source, begin + frames);
    const auto failure = first_fault<Sample>(volts, begin, 1.0, expression, true);
    require(failure != no_fault && failure + 3 < frames, "missing monitored-overflow oracle witness");
    require(!warm || failure == 0, "primed monitoring fault must occur at frame zero");
#if defined(RHODES_FILTERED_VST3) || defined(RHODES_QUADRATURE_VST3)
    require(warm || failure > 8, "delayed filter monitoring witness ignored its history");
#endif
    harness.note(failure + 3);
    Plan plan; plan.operation = Operation::source; plan.source = source;
    require(harness.run(plan, frames) == sdk::kResultFalse, "finite monitored overflow was accepted");
    trace_matches(plan, failure + 1, failure + 1, rate);
    require(harness.host.processor.requires_reset() &&
            !harness.host.processor.engine_processor().requires_reset(),
            "monitored fault occurred upstream instead of in the real host adapter");
    require(harness.host.processor.engine_processor().frame_cursor() == begin + failure + 1,
            "host monitored fault did not retain the completed engine frame");
    (void)outputs(harness, frames, failure, &volts, begin, 1.0, expression);
    require(harness.host.processor.current_state() == InstrumentState{1.0F, false, expression} &&
            harness.host.processor.published_state() == InstrumentState{1.0F, false, expression},
            "monitored fault control publication");
    std::cout << "monitor_witness_volts=" << volts[begin + failure]
              << " monitored_long_double=" << 32.0L * volts[begin + failure]
              << " double_limit=" << std::numeric_limits<double>::max() << '\n';
    identity(warm ? "monitor_primed" : "monitor_delayed", harness, failure, plan, false);
    rejected_after_fault(harness);
}

template<class Sample>
void monitor_fault_typed() {
    for (const auto layout : layouts) {
        for (const auto failure : {std::size_t{0}, std::size_t{61}}) {
            forced_result_case<Sample>(Operation::process_nan, failure, 0.0F, layout, 48000.0);
            forced_result_case<Sample>(Operation::process_inf, failure, 0.0F, layout, 48000.0);
        }
        for (const auto rate : rates) {
            monitored_case<Sample>(rate, layout, false, 0.0F);
            monitored_case<Sample>(rate, layout, true, 0.0F);
            monitored_case<Sample>(rate, layout, true, 0.5F);
            if constexpr (std::is_same_v<Sample, double>) {
                monitored_case<Sample>(rate, layout, false, 0.5F);
            }
        }
    }
}

template<class Sample>
void finite_conversion_case(double rate, Layout layout, bool warm, float expression,
                            bool expect_failure) {
    context = std::string("finite_conversion ") + format<Sample>() + " " + std::to_string(rate) +
              " warm=" + std::to_string(warm) + " expr=" + std::to_string(expression);
    Harness<Sample> harness(rate, layout);
    Source source{0.015625, std::ldexp(1.0, 128), warm ? 0U : 8U};
    const auto begin = warm ? warm_frames : 0;
    if (warm) { prime(harness, source); }
    harness.controls(0.5F, expression);
    const auto volts = reference_volts(rate, source, begin + frames);
    const auto failure = first_fault<Sample>(volts, begin, 0.5, expression, false);
    require((failure != no_fault) == expect_failure, "finite conversion oracle expectation");
    require(first_fault<double>(volts, begin, 0.5, 1.0, true) == no_fault,
            "float fixture accidentally has a monitored-double overflow");
    const auto unattenuated = first_fault<float>(volts, begin, 0.5, 1.0, false);
    require(unattenuated != no_fault, "no out-of-float-range attenuation witness");
    require(!warm || !expect_failure || failure == 0, "primed final conversion must fail at frame zero");
    harness.note(expect_failure ? failure + 3 : no_fault);
    Plan plan; plan.operation = Operation::source; plan.source = source;
    const auto result = harness.run(plan, frames);
    require(result == (expect_failure ? sdk::kResultFalse : sdk::kResultOk),
            "real final host-format conversion outcome");
    const auto processed = expect_failure ? failure + 1 : frames;
    trace_matches(plan, processed, processed, rate);
    require(harness.host.processor.requires_reset() == expect_failure &&
            !harness.host.processor.engine_processor().requires_reset(),
            "finite conversion was confused with an upstream engine fault");
    require(harness.host.processor.engine_processor().frame_cursor() == begin + processed,
            "finite conversion cursor");
    const auto nonzero = outputs(harness, frames, failure, &volts, begin, 0.5, expression);
    require(layout.omitted || expression == 0.0F || expect_failure || nonzero > 48,
            "attenuated/unbounded-double success was silent");
    const long double witness = 16.0L * volts[begin + unattenuated];
    std::cout << "float_witness_monitored=" << witness
              << " attenuated=" << witness * expression
              << " float_limit=" << std::numeric_limits<float>::max() << '\n';
    identity(expect_failure ? "float_overflow" : expression == 0.0F ? "finite_mute" :
        std::is_same_v<Sample, float> ? "attenuation_avoids_overflow" : "double_accepts_float_excess",
        harness, failure, plan, false);
    if (expect_failure) { rejected_after_fault(harness); }
}

void float_conversion() {
    for (const auto rate : rates) {
        for (const auto layout : layouts) {
            for (const bool warm : {false, true}) {
                finite_conversion_case<float>(rate, layout, warm, 1.0F, true);
                finite_conversion_case<float>(rate, layout, warm, 0.03125F, false);
                finite_conversion_case<float>(rate, layout, warm, 0.0F, false);
                finite_conversion_case<double>(rate, layout, warm, 1.0F, false);
            }
        }
    }
}

enum class FaultKind { engine, monitoring, conversion };

template<class Sample>
void state_case(FaultKind kind, Layout layout, bool pending_recall, int reset_method = -1) {
    context = std::string("state_case ") + format<Sample>() + " kind=" +
              std::to_string(static_cast<int>(kind)) + " pending=" + std::to_string(pending_recall) +
              " reset=" + std::to_string(reset_method);
    Harness<Sample> harness(48000.0, layout);
    const Source source{0.0, std::ldexp(1.0, kind == FaultKind::monitoring ? 1020 : 128), 0};
    if (kind != FaultKind::engine) { prime(harness, source); }
    const auto before = kind == FaultKind::engine ? 0U : warm_frames;
    const float initial_gain = kind == FaultKind::engine ? 0.5F :
        kind == FaultKind::monitoring ? (std::is_same_v<Sample, float> ? 0.0F : 0.03125F) : 0.015625F;
    const float initial_expression = kind == FaultKind::monitoring && std::is_same_v<Sample, float> ?
        0.0F : 0.5F;
    harness.controls(initial_gain, initial_expression);
    if (kind == FaultKind::engine) {
        points(harness.gain, {{0, 0.5}, {16, 0.25}});
    } else {
        points(harness.gain, {{0, initial_gain}, {8, initial_gain}, {9, 1.0}, {16, 0.25}});
    }
    if (initial_expression == 0.0F) {
        points(harness.expression, {{0, 0.0}, {16, 0.0}});
    } else {
        points(harness.expression, {{0, 0.5}, {16, 0.75}});
    }
    points(harness.pedal, {{0, 1.0}, {10, 0.0}});
    harness.host.data.inputParameterChanges = &harness.parameters;
    harness.events.values[0] = note_on(0, 100, 68);
    harness.events.values[1] = note_off(9, 100, 68);
    harness.events.values[2] = note_on(12, 200, 72);
    harness.events.count = 3;
    harness.host.data.inputEvents = &harness.events;
    const InstrumentState failed_state{kind == FaultKind::engine ? 0.359375F : 1.0F,
                                      true, initial_expression == 0.0F ? 0.0F : 0.640625F};
    const InstrumentState recalled{0.375F, false, 0.25F};
    MemoryStream stream;
    literal_state(stream, recalled);
    Plan plan;
    plan.operation = kind == FaultKind::engine ? Operation::process_false : Operation::source;
    plan.trigger = 9; plan.source = source;
    if (pending_recall) {
        plan.recall_target = &harness.host.processor;
        plan.recall_stream = &stream;
        plan.recall_at = 9;
    }
    require(harness.run(plan, 32) == sdk::kResultFalse, "scheduled arithmetic fault was accepted");
    trace_matches(plan, 10, 10, harness.rate);
    require(harness.host.processor.requires_reset() &&
            harness.host.processor.engine_processor().requires_reset() == (kind == FaultKind::engine),
            "scheduled fault subsystem");
    require(harness.host.processor.engine_processor().frame_cursor() ==
            before + (kind == FaultKind::engine ? 9U : 10U), "scheduled fault cursor");
    require(harness.host.processor.current_state() == failed_state,
            "failing frame did not retain its independently interpolated control pair");
    require(harness.host.processor.published_state() == (pending_recall ? recalled : failed_state),
            "fault publication overwrote a newer recall or lost the current controls");
    require(plan.trace.recall_calls == (pending_recall ? 1U : 0U) &&
            (!pending_recall || plan.trace.recall_result == sdk::kResultOk), "scheduled public recall");
    require(harness.host.processor.note_tracker().active_count() == 1 &&
            harness.host.processor.note_tracker().held_count(40) == 0 &&
            harness.host.processor.note_tracker().held_count(44) == 1,
            "accepted fault ownership is not the preflighted whole transaction");
    require(harness.host.processor.engine_processor().engine().mechanical_energy_joules() > 0.0,
            "real mechanics were rolled back at the arithmetic fault");
    for (std::size_t mode = 0; mode < modes_per_key; ++mode) {
        const auto future = harness.host.processor.engine_processor().engine().modal_state(44, mode);
        require(future && future->displacement_m == 0.0 && future->velocity_m_per_s == 0.0,
                "suffix note reached physical synthesis");
    }
    const auto nonzero = outputs(harness, 32, 9);
    require(layout.omitted || initial_expression == 0.0F || kind == FaultKind::engine || nonzero > 0,
            "scheduled conversion prefix was vacuous");
    saved_state(harness.host.processor, pending_recall ? recalled : failed_state);
    identity(kind == FaultKind::engine ? "state_engine" : kind == FaultKind::monitoring ?
        "state_monitor" : "state_float", harness, 9, plan, kind == FaultKind::engine);
    harness.clear_inputs();
    if (!pending_recall) { rejected_after_fault(harness); }
    if (reset_method < 0 && !pending_recall) { return; }

    InstrumentState reset_state = failed_state;
    if (pending_recall) {
        Plan recall;
        require(harness.run(recall, 0) == sdk::kResultOk, "newer recall did not reset a faulted owner");
        trace_matches(recall, 0, 0, harness.rate);
        reset_state = recalled;
    } else if (reset_method == 0) {
        require(harness.host.processor.setProcessing(false) == sdk::kResultOk &&
                harness.host.processor.setProcessing(true) == sdk::kResultOk, "processing reset");
    } else if (reset_method == 1) {
        require(harness.host.processor.setActive(false) == sdk::kResultOk &&
                harness.host.processor.setActive(true) == sdk::kResultOk, "activation reset");
    } else if (reset_method == 2) {
        vst::ProcessSetup setup{vst::kRealtime, ProcessorHost<Sample, capacity>::sample_size,
            static_cast<sdk::int32>(kMaximumHostFrames), harness.rate};
        require(harness.host.processor.setupProcessing(setup) == sdk::kResultOk, "setup reset");
    } else {
        literal_state(stream, recalled);
        require(harness.host.processor.setState(&stream) == sdk::kResultOk, "explicit recovery recall");
        Plan recall;
        require(harness.run(recall, 0) == sdk::kResultOk, "consume recovery recall");
        trace_matches(recall, 0, 0, harness.rate);
        reset_state = recalled;
    }
    require(!harness.host.processor.requires_reset() &&
            !harness.host.processor.engine_processor().requires_reset() &&
            harness.host.processor.engine_processor().frame_cursor() == 0 &&
            harness.host.processor.note_tracker().active_count() == 0 &&
            harness.host.processor.engine_processor().engine().mechanical_energy_joules() == 0.0,
            "explicit reset did not clear both fault layers, ownership and mechanics");
    require(harness.host.processor.current_state() == reset_state &&
            harness.host.processor.published_state() == reset_state, "reset configuration policy");
    saved_state(harness.host.processor, reset_state);
    Plan silent;
    require(harness.run(silent, 17) == sdk::kResultOk, "silent post-reset render");
    trace_matches(silent, 17, 17, harness.rate);
    const std::vector<long double> zero(17);
    (void)outputs(harness, 17, no_fault, &zero);
    harness.controls(0.5F, 0.75F);
    harness.note();
    Plan recovery;
    require(harness.run(recovery, frames) == sdk::kResultOk, "real audio recovery failed");
    trace_matches(recovery, frames, frames, harness.rate);
    require(recovery.trace.real_nonzero_samples > 0, "recovery did not execute real nonzero engine");
    const auto audible = outputs(harness, frames, no_fault);
    require(layout.omitted || audible > 48, "reset recovery was silent");
    require(harness.host.processor.engine_processor().frame_cursor() == 17 + frames &&
            !harness.host.processor.requires_reset() &&
            harness.host.processor.note_tracker().active_count() == 1, "recovery cursor/ownership");
}

template<class Sample>
void state_typed(bool reset) {
    for (const auto layout : layouts) {
        for (const auto kind : {FaultKind::engine, FaultKind::monitoring, FaultKind::conversion}) {
            if (kind == FaultKind::conversion && !std::is_same_v<Sample, float>) { continue; }
            if (reset) {
                for (int method = 0; method < 4; ++method) { state_case<Sample>(kind, layout, false, method); }
            } else {
                state_case<Sample>(kind, layout, false);
                state_case<Sample>(kind, layout, true);
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "one fault selector is required");
        require(std::numeric_limits<double>::is_iec559 && std::numeric_limits<float>::is_iec559 &&
                std::numeric_limits<double>::max_exponent == 1024 &&
                std::numeric_limits<float>::max_exponent == 128,
                "fault witnesses require IEEE binary64/binary32");
        require(std::fesetround(FE_TONEAREST) == 0, "select nearest rounding for overflow witnesses");
        std::cout << std::setprecision(21);
        const std::string_view selector(argv[1]);
        if (selector == "interception") { interception_typed<float>(); interception_typed<double>(); }
        else if (selector == "engine_failure") { engine_failure_typed<float>(); engine_failure_typed<double>(); }
        else if (selector == "monitor_fault") { monitor_fault_typed<float>(); monitor_fault_typed<double>(); }
        else if (selector == "float_conversion") { float_conversion(); }
        else if (selector == "fault_state") { state_typed<float>(false); state_typed<double>(false); }
        else if (selector == "reset_recovery") { state_typed<float>(true); state_typed<double>(true); }
        else { throw std::runtime_error("unknown fault selector"); }
        require(verified_cases > 0 && totals.process_calls > 0 &&
                totals.real_process_successes == totals.process_calls &&
                totals.real_observe_successes == totals.observe_calls &&
                totals.real_nonzero_samples > 0 && totals.real_nonfinite_values == 0 &&
                totals.instance_mismatches == 0, "aggregate nonvacuity/real-engine witness");
#if defined(RHODES_QUADRATURE_VST3)
        require(totals.observe_calls > 0 && totals.real_nonzero_observations > 0,
                "Q6 observation wrappers were not exercised");
#endif
        std::cout << "PASS selector=" << selector << " variant=" << variant
                  << " cases=" << verified_cases << " callbacks=" << verified_callbacks
                  << " process_calls=" << totals.process_calls
                  << " process_samples=" << totals.process_samples
                  << " observe_calls=" << totals.observe_calls
                  << " process_overrides=" << totals.process_overrides
                  << " observe_overrides=" << totals.observe_overrides
                  << " real_nonzero=" << totals.real_nonzero_samples
                  << " real_observed_nonzero=" << totals.real_nonzero_observations
                  << " prefix_samples=" << verified_prefix_samples
                  << " suffix_samples=" << verified_suffix_samples
                  << " nonzero_prefix=" << nonzero_prefix_samples
                  << " recall_calls=" << totals.recall_calls
                  << " max_relative_error=" << maximum_relative_error << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL variant=" << variant << " context=" << context << ": " << error.what()
                  << " cases=" << verified_cases << " process_calls=" << totals.process_calls
                  << " observe_calls=" << totals.observe_calls << '\n';
        return 1;
    }
}
