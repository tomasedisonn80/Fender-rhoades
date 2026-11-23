#include "vst3_test_support.hpp"

#include "rhodes/quadrature_filter_coefficients.hpp"

#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>

#if !defined(RHODES_QUADRATURE_VST3) || defined(RHODES_FILTERED_VST3)
#error "This contract fixture requires only RHODES_QUADRATURE_VST3"
#endif

namespace {
namespace host = rhodes::vst3::test_host;
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
namespace qf = rhodes::quadrature_filter;
using rhodes::InstrumentState;
using rhodes::vst3::RhodesProcessor;
using Packet = std::array<std::uint8_t, 16>;

constexpr std::size_t kContractFrames = 256;
constexpr InstrumentState initial{0.625F, true};
constexpr InstrumentState pending{0.125F, false};
constexpr InstrumentState replacement{0.75F, false};
constexpr InstrumentState automated{0.375F, true};
constexpr std::array rates{32000.0, 48000.0, 192000.0};
std::size_t checks = 0;
std::size_t cases = 0;
std::size_t calls = 0;
double largest_error = 0.0;

void check(bool condition, std::string_view message) {
    ++checks;
    host::require(condition, message);
}

// Literal wire construction keeps cross-model expectations independent of the
// codec/variant constant under test. The exact 16-byte RHST v1 envelope stays.
Packet packet(InstrumentState state, std::uint16_t model = 3) {
    Packet result{'R', 'H', 'S', 'T', 1, 0,
        static_cast<std::uint8_t>(model), static_cast<std::uint8_t>(model >> 8U)};
    const auto gain = std::bit_cast<std::uint32_t>(state.monitoring_gain);
    const std::uint32_t pedal = state.sustain ? 0x3f800000U : 0U;
    for (std::size_t digit = 0; digit < 4; ++digit) {
        result[8 + digit] = static_cast<std::uint8_t>(gain >> (digit * 8U));
        result[12 + digit] = static_cast<std::uint8_t>(pedal >> (digit * 8U));
    }
    return result;
}

void load(host::MemoryStream& stream, const Packet& bytes, sdk::int32 chunk = 16) {
    // Nonzero stream offsets ensure the helper respects the remaining extent.
    std::copy(bytes.begin(), bytes.end(), stream.bytes.begin() + 7);
    stream.position = 7;
    stream.extent = 23;
    stream.transfer_limit = chunk;
}

void recall(RhodesProcessor& processor, InstrumentState state, sdk::int32 chunk = 16) {
    host::MemoryStream stream;
    load(stream, packet(state), chunk);
    check(processor.setState(&stream) == sdk::kResultOk, "model-3 recall rejected");
}

void expect_saved(RhodesProcessor& processor, InstrumentState state) {
    host::MemoryStream stream;
    stream.transfer_limit = 3;
    const auto wanted = packet(state);
    check(processor.getState(&stream) == sdk::kResultOk && stream.extent == 16 &&
          std::equal(wanted.begin(), wanted.end(), stream.bytes.begin()),
          "saved model-3 bytes or pending state changed");
}

sdk::tresult process(RhodesProcessor& processor, vst::ProcessData& data) {
    ++calls;
    return processor.process(data);
}

void flush(RhodesProcessor& processor, sdk::int32 sample_size) {
    vst::ProcessData data{};
    data.symbolicSampleSize = sample_size;
    check(process(processor, data) == sdk::kResultOk, "zero-frame recall flush failed");
}

template<class Sample>
using Rig = host::ProcessorHost<Sample, kContractFrames>;

template<class Sample>
void seed(Rig<Sample>& rig, std::size_t key = 0) {
    recall(rig.processor, initial);
    flush(rig.processor, rig.sample_size);
    auto events = std::make_unique<host::EventList>();
    events->count = 1;
    events->values[0] = host::note_on(0, 10, static_cast<sdk::int16>(28 + key), 0.75F);
    rig.data.inputEvents = events.get();
    check(process(rig.processor, rig.data) == sdk::kResultOk, "warm history seed failed");
    rig.data.inputEvents = nullptr;
    check(std::any_of(rig.left.begin(), rig.left.end(),
                     [](Sample value) { return value != Sample{0}; }),
          "history seed produced no output");
    check(rig.processor.note_tracker().active_count() == 1 &&
          rig.processor.engine_processor().engine().mechanical_energy_joules() > 0.0,
          "history seed did not preserve moving ownership");
}

void expect_history_unchanged(const host::ProcessorSnapshot& before,
                              const RhodesProcessor& processor) {
    before.expect_unchanged(processor);
    auto actual = std::make_unique<rhodes::QuadratureEngine>(
        processor.engine_processor().engine());
    auto expected = std::make_unique<rhodes::QuadratureEngine>(before.engine.engine());
    std::array<double, 2 * qf::history_frames + 1> a{}, b{};
    check(actual->process(a) && expected->process(b), "history continuation failed");
    check(a == b, "rejection changed a delayed cell or ring position");
}

template<class Sample>
void output_case(double rate, std::size_t key) {
    auto rig = std::make_unique<Rig<Sample>>(rate);
    auto events = std::make_unique<host::EventList>();
    const auto pitch = static_cast<sdk::int16>(28 + key);
    events->count = 4;
    events->values[0] = host::note_on(0, 1, pitch, 0.75F);
    events->values[1] = host::note_off(47, 1, pitch);
    events->values[2] = host::note_on(63, 2, pitch, 0.5F);
    events->values[3] = host::note_off(95, 2, pitch);
    auto pedal = std::make_unique<host::ParameterQueue>();
    pedal->id = rhodes::vst3::kSustainId;
    pedal->count = 2;
    pedal->values[0] = {46, 1.0};
    pedal->values[1] = {128, 0.0};
    host::ParameterChanges changes;
    changes.count = 1;
    changes.values[0] = pedal.get();
    rig->data.inputEvents = events.get();
    rig->data.inputParameterChanges = &changes;
    check(process(rig->processor, rig->data) == sdk::kResultOk,
          "quadrature host event render failed");

    // Independent chronological storage and ordinary long-double summation:
    // this oracle never constructs QuadratureEngine or QuadratureFir. It tests
    // the adapter/wrapper's absolute cell indexing, event boundary and gain.
    // Fractional source observations and checked-in taps are shared ingredients;
    // their independent analytic/numerical checks belong to separate fixtures.
    auto source = std::make_unique<rhodes::ResearchEngine>();
    auto plan = std::make_unique<rhodes::FractionalObservationPlan>();
    using Cells = std::array<std::array<double, qf::node_count>, kContractFrames>;
    auto cells = std::make_unique<Cells>();
    check(source->prepare(rate) && source->prepare_observation_plan(qf::nodes, *plan),
          "chronological source preparation failed");
    for (std::size_t frame = 0; frame < kContractFrames; ++frame) {
        if (frame == 46) { source->set_sustain(true); }
        if (frame == 128) { source->set_sustain(false); }
        if (frame == 0 || frame == 63) {
            check(source->note_on(key, frame == 0 ? 0.75 : 0.5), "reference strike failed");
        }
        if (frame == 47 || frame == 95) {
            check(source->note_off(key), "reference release failed");
        }
        long double voltage = 0.0L;
        const std::size_t oldest = frame > qf::history_frames ? frame - qf::history_frames : 0;
        for (std::size_t cell = oldest; cell < frame; ++cell) {
            const auto age = frame - cell - 1;
            for (std::size_t node = 0; node < qf::node_count; ++node) {
                voltage += static_cast<long double>((*cells)[cell][node]) *
                           static_cast<long double>(qf::coefficients[node][age]);
            }
        }
        const auto expected = static_cast<Sample>(16.0L * voltage);
        const double error = std::abs(static_cast<double>(rig->left[frame]) -
                                      static_cast<double>(expected));
        largest_error = std::max(largest_error, error);
        const double tolerance = std::is_same_v<Sample, float> ?
            4.0 * static_cast<double>(std::numeric_limits<float>::epsilon()) *
                std::max(1e-9, std::abs(static_cast<double>(expected))) : 2e-14;
        check(std::isfinite(rig->left[frame]) && error <= tolerance,
              "host quadrature voltage differs from chronological convolution");
        check(rig->left[frame] == rig->right[frame], "quadrature stereo channels differ");
        for (std::size_t node = 0; node < qf::node_count; ++node) {
            const auto value = source->observe(*plan, node);
            check(value.has_value(), "reference fractional observation failed");
            (*cells)[frame][node] = *value;
        }
        std::array<double, 1> unused{};
        check(source->process(unused), "reference boundary advance failed");
    }
    check(rig->left[0] == Sample{0} && rig->left[1] != Sample{0},
          "completed-cell onset convention changed");
    host::same_mechanics(rig->processor.engine_processor().engine(), *source);
    check(rig->processor.getLatencySamples() == 48, "latency changed after events");
    ++cases;
}

void identity_delay() {
    static_assert(rhodes::vst3::kQuadratureAudio && !rhodes::vst3::kFilteredAudio);
    static_assert(rhodes::vst3::kStateModel == 3 && rhodes::vst3::kLatencyFrames == 48);
    const sdk::FUID processor{0xD73B886A, 0x72E04FC8, 0x92435F1D, 0xC5F0B6A9};
    const sdk::FUID controller{0xA45F23E1, 0xB68A4D79, 0xAF31472C, 0x8E909D52};
    check(rhodes::vst3::kProcessorUID == processor && rhodes::vst3::kControllerUID == controller,
          "model-3 FUID changed");
    check(std::string_view{rhodes::vst3::kPluginName} == "Rhodes Research Quadrature" &&
          std::string_view{rhodes::vst3::kPluginVersion} == "0.3.0",
          "model-3 product identity changed");
    auto unconfigured = std::make_unique<RhodesProcessor>();
    check(unconfigured->getLatencySamples() == 48 &&
          unconfigured->getTailSamples() == vst::kInfiniteTail,
          "latency/tail metadata depends on setup");
    long double sum = 0.0L;
    long double moment = 0.0L;
    for (std::size_t node = 0; node < qf::node_count; ++node) {
        for (std::size_t age = 0; age < qf::history_frames; ++age) {
            const auto tap = static_cast<long double>(qf::coefficients[node][age]);
            sum += tap;
            moment += tap * (static_cast<long double>(age + 1) - qf::nodes[node]);
        }
    }
    check(std::abs(sum - 1.0L) <= 5e-15L && std::abs(moment / sum - 48.0L) <= 1e-12L,
          "physical sample coordinates do not give the advertised 48-frame centre");
    for (const double rate : rates) {
        for (const std::size_t key : {std::size_t{0}, std::size_t{40}, std::size_t{72}}) {
            output_case<float>(rate, key);
            output_case<double>(rate, key);
        }
    }
}

template<class Sample>
void foreign_case(double rate, std::uint16_t foreign_model, sdk::int32 chunk) {
    auto rig = std::make_unique<Rig<Sample>>(rate);
    seed(*rig, 72);
    recall(rig->processor, pending);
    const auto before = std::make_unique<host::ProcessorSnapshot>(rig->processor);
    host::MemoryStream stream;
    load(stream, packet(replacement, foreign_model), chunk);
    check(rig->processor.setState(&stream) != sdk::kResultOk, "foreign model recall accepted");
    expect_history_unchanged(*before, rig->processor);
    expect_saved(rig->processor, pending);
    host::ControllerHost editor;
    host::MemoryStream controller_state;
    load(controller_state, packet(pending));
    check(editor.controller.setState(&controller_state) == sdk::kResultOk,
          "controller model-3 seed failed");
    for (bool component : {false, true}) {
        host::MemoryStream bad;
        load(bad, packet(replacement, foreign_model), chunk);
        const auto result = component ? editor.controller.setComponentState(&bad) :
                                        editor.controller.setState(&bad);
        check(result != sdk::kResultOk, "controller accepted a foreign model");
        check(editor.controller.getParamNormalized(rhodes::vst3::kGainId) == 0.125 &&
              editor.controller.getParamNormalized(rhodes::vst3::kSustainId) == 0.0,
              "foreign controller state changed controls");
    }
    flush(rig->processor, rig->sample_size);
    check(rig->processor.current_state() == pending &&
          rig->processor.engine_processor().frame_cursor() == 0 &&
          rig->processor.note_tracker().active_count() == 0,
          "foreign packet disturbed pending valid reset");
    rig->fill(Sample{1});
    check(process(rig->processor, rig->data) == sdk::kResultOk, "pending recall render failed");
    rig->expect_silence();
    ++cases;
}

void foreign_recall() {
    for (const double rate : rates) {
        // 1 and 2 are every other supported model; 0, 4 and 65535 sample invalid
        // wire tags. Exhaustive invalid-tag codec coverage lives in its suite.
        for (const std::uint16_t model : std::array<std::uint16_t, 5>{1, 2, 0, 4, 65535}) {
            for (const sdk::int32 chunk : {1, 3, 16}) {
                foreign_case<float>(rate, model, chunk);
                foreign_case<double>(rate, model, chunk);
            }
        }
    }
}

template<class Sample>
void ordering_case(double rate) {
    auto rig = std::make_unique<Rig<Sample>>(rate);
    seed(*rig);
    recall(rig->processor, pending, 1);
    recall(rig->processor, replacement, 3);
    check(rig->processor.current_state() == initial &&
          rig->processor.published_state() == replacement,
          "UI recall mutated live state or did not publish the latest request");
    const auto before = std::make_unique<host::ProcessorSnapshot>(rig->processor);
    auto events = std::make_unique<host::EventList>();
    events->count = 2;
    events->values[0] = host::note_on(0, 11, 100);
    events->values[1] = host::note_on(static_cast<sdk::int32>(kContractFrames), 12, 68);
    rig->data.inputEvents = events.get();
    check(process(rig->processor, rig->data) != sdk::kResultOk,
          "late invalid event accepted with recall pending");
    rig->expect_silence();
    expect_history_unchanged(*before, rig->processor);
    expect_saved(rig->processor, replacement);

    auto gain = std::make_unique<host::ParameterQueue>();
    auto pedal = std::make_unique<host::ParameterQueue>();
    gain->id = rhodes::vst3::kGainId;
    gain->count = 1;
    gain->values[0] = {0, 0.375};
    pedal->id = rhodes::vst3::kSustainId;
    pedal->count = 1;
    pedal->values[0] = {0, 1.0};
    host::ParameterChanges changes;
    changes.count = 2;
    changes.values = {pedal.get(), gain.get()};
    events->count = 1;
    rig->data.inputParameterChanges = &changes;
    auto control = std::make_unique<Rig<Sample>>(rate);
    recall(control->processor, replacement);
    control->data.inputEvents = events.get();
    control->data.inputParameterChanges = &changes;
    check(process(rig->processor, rig->data) == sdk::kResultOk &&
          process(control->processor, control->data) == sdk::kResultOk,
          "valid transaction did not consume last recall");
    check(rig->left == control->left && rig->right == control->right,
          "accepted recall retained pre-recall FIR history or lost offset-zero automation");
    check(rig->processor.current_state() == automated &&
          rig->processor.published_state() == automated,
          "offset-zero controls did not follow recall");
    check(rig->processor.note_tracker().held_count(0) == 0 &&
          rig->processor.note_tracker().held_count(72) == 1 &&
          rig->processor.note_tracker().active_count() == 1,
          "recall did not replace old physical note ownership");
    host::same_mechanics(rig->processor.engine_processor().engine(),
                         control->processor.engine_processor().engine());
    rig->data.inputEvents = nullptr;
    rig->data.inputParameterChanges = nullptr;
    control->data.inputEvents = nullptr;
    control->data.inputParameterChanges = nullptr;
    flush(rig->processor, rig->sample_size);
    check(process(rig->processor, rig->data) == sdk::kResultOk &&
          process(control->processor, control->data) == sdk::kResultOk,
          "post-recall continuation failed");
    check(rig->left == control->left && rig->right == control->right &&
          rig->processor.engine_processor().frame_cursor() == 2 * kContractFrames,
          "recall replayed after its accepted transaction");
    expect_saved(rig->processor, automated);
    ++cases;
}

void recall_order() {
    for (const double rate : rates) {
        ordering_case<float>(rate);
        ordering_case<double>(rate);
    }
}

enum class Reset { processing, activation, setup, recall };

std::string_view reset_name(Reset reset) {
    switch (reset) {
    case Reset::processing: return "processing";
    case Reset::activation: return "activation";
    case Reset::setup: return "setup";
    case Reset::recall: return "recall";
    }
    return "unknown";
}

template<class Sample>
void diagnose_reset_difference(const Rig<Sample>& actual, const Rig<Sample>& expected,
                               double rate, Reset reset) {
    std::size_t first_frame = kContractFrames;
    std::size_t differing_frames = 0;
    double maximum_error = 0.0;
    for (std::size_t frame = 0; frame < kContractFrames; ++frame) {
        if (actual.left[frame] != expected.left[frame] ||
            actual.right[frame] != expected.right[frame]) {
            if (first_frame == kContractFrames) { first_frame = frame; }
            ++differing_frames;
        }
        maximum_error = std::max({maximum_error,
            std::abs(static_cast<double>(actual.left[frame]) -
                     static_cast<double>(expected.left[frame])),
            std::abs(static_cast<double>(actual.right[frame]) -
                     static_cast<double>(expected.right[frame]))});
    }
    const auto& actual_engine = actual.processor.engine_processor().engine();
    const auto& expected_engine = expected.processor.engine_processor().engine();
    std::size_t differing_states = 0;
    double maximum_displacement_error = 0.0;
    double maximum_velocity_error = 0.0;
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto a = actual_engine.modal_state(key, mode);
            const auto e = expected_engine.modal_state(key, mode);
            if (!a || !e) {
                std::cerr << "Reset diagnostic missing state key=" << key
                          << " mode=" << mode << '\n';
                continue;
            }
            differing_states += a->displacement_m != e->displacement_m ||
                                a->velocity_m_per_s != e->velocity_m_per_s ? 1U : 0U;
            maximum_displacement_error = std::max(maximum_displacement_error,
                std::abs(a->displacement_m - e->displacement_m));
            maximum_velocity_error = std::max(maximum_velocity_error,
                std::abs(a->velocity_m_per_s - e->velocity_m_per_s));
        }
    }
    std::cerr << std::setprecision(17)
              << "Reset diagnostic rate=" << rate
              << " sample_bits=" << sizeof(Sample) * 8
              << " reset=" << reset_name(reset)
              << " first_frame=" << first_frame
              << " differing_frames=" << differing_frames
              << " max_output_error=" << maximum_error
              << " differing_modal_states=" << differing_states
              << " max_displacement_error=" << maximum_displacement_error
              << " max_velocity_error=" << maximum_velocity_error
              << " actual_cursor=" << actual.processor.engine_processor().frame_cursor()
              << " expected_cursor=" << expected.processor.engine_processor().frame_cursor()
              << '\n';
    if (first_frame < kContractFrames) {
        std::cerr << "Reset first sample actual_left=" << actual.left[first_frame]
                  << " expected_left=" << expected.left[first_frame]
                  << " actual_right=" << actual.right[first_frame]
                  << " expected_right=" << expected.right[first_frame] << std::hexfloat
                  << " actual_hex=" << actual.left[first_frame]
                  << " expected_hex=" << expected.left[first_frame]
                  << std::defaultfloat << '\n';
    }
}

template<class Sample>
void reset_case(double rate, Reset reset) {
    auto rig = std::make_unique<Rig<Sample>>(rate);
    seed(*rig, 40);
    auto expected_state = initial;
    if (reset == Reset::processing) {
        check(rig->processor.setProcessing(false) == sdk::kResultOk &&
              rig->processor.setProcessing(true) == sdk::kResultOk,
              "processing transition reset failed");
    } else if (reset == Reset::activation) {
        check(rig->processor.setActive(false) == sdk::kResultOk &&
              rig->processor.setActive(true) == sdk::kResultOk,
              "activation transition reset failed");
    } else if (reset == Reset::setup) {
        vst::ProcessSetup setup{vst::kRealtime, rig->sample_size,
            static_cast<sdk::int32>(rhodes::vst3::kMaximumHostFrames), rate};
        check(rig->processor.setupProcessing(setup) == sdk::kResultOk,
              "same-rate setup reset failed");
    } else {
        expected_state = pending;
        recall(rig->processor, expected_state);
        flush(rig->processor, rig->sample_size);
    }
    check(rig->processor.getLatencySamples() == 48 &&
          rig->processor.getTailSamples() == vst::kInfiniteTail &&
          rig->processor.current_state() == expected_state &&
          rig->processor.engine_processor().frame_cursor() == 0 &&
          rig->processor.note_tracker().active_count() == 0 &&
          rig->processor.engine_processor().engine().mechanical_energy_joules() == 0.0,
          "host reset lost identity/configuration or retained mechanics");
    rig->fill(Sample{1});
    check(process(rig->processor, rig->data) == sdk::kResultOk,
          "post-reset silent render failed");
    rig->expect_silence();
    auto events = std::make_unique<host::EventList>();
    events->count = 1;
    events->values[0] = host::note_on(0, 99, 100, 1.0F);
    auto control = std::make_unique<Rig<Sample>>(rate);
    recall(control->processor, expected_state);
    // Compare identical post-reset timelines, including the silent prelude.
    // Unequal elapsed cells put equivalent zero histories at different ring
    // positions; permitted FMA contraction can then change final rounding.
    // The dirty instance's entire prelude is already required to be silent.
    control->fill(Sample{1});
    check(process(control->processor, control->data) == sdk::kResultOk,
          "fresh post-recall silent render failed");
    control->expect_silence();
    control->data.inputEvents = events.get();
    rig->data.inputEvents = events.get();
    check(process(rig->processor, rig->data) == sdk::kResultOk &&
          process(control->processor, control->data) == sdk::kResultOk,
          "post-reset restrike failed");
    const bool same_output = rig->left == control->left && rig->right == control->right;
    if (!same_output) { diagnose_reset_difference(*rig, *control, rate, reset); }
    check(same_output,
          "post-reset quadrature trajectory differs from a fresh recalled instance");
    host::same_mechanics(rig->processor.engine_processor().engine(),
                         control->processor.engine_processor().engine());
    ++cases;
}

void reset_tail() {
    for (const double rate : rates) {
        for (const auto reset : {Reset::processing, Reset::activation, Reset::setup, Reset::recall}) {
            reset_case<float>(rate, reset);
            reset_case<double>(rate, reset);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view selected = argc > 1 ? argv[1] : "all";
        bool found = false;
        const std::array<std::pair<std::string_view, void (*)()>, 4> fixtures{{
            {"qhost_identity_delay", identity_delay},
            {"qhost_foreign_recall", foreign_recall},
            {"qhost_recall_order", recall_order},
            {"qhost_reset_tail", reset_tail}
        }};
        for (const auto& [name, fixture] : fixtures) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            checks = 0;
            cases = 0;
            calls = 0;
            largest_error = 0.0;
            fixture();
            std::cout << name << " passed cases=" << cases << " checks=" << checks
                      << " process_calls=" << calls << " max_output_error=" << largest_error
                      << '\n';
        }
        host::require(found && argc <= 2, "unknown quadrature host contract selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "quadrature host contract failed: " << error.what() << '\n';
        return 1;
    }
}
