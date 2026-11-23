#include "rhodes/audio_engine.hpp"
#include "rhodes/event_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using rhodes::AudioEngine;
using rhodes::EngineEvent;
using rhodes::EventType;
using rhodes::ProcessStatus;
using rhodes::ProcessSubsystem;
constexpr auto no_failure = rhodes::no_failure_index;
constexpr long double voltage_tolerance = 1e-12L;

struct RateCase { double host; std::size_t factor; };
constexpr std::array<RateCase, 3> representative_rates{{
    {44100.0, 4}, {88200.0, 2}, {192000.0, 1}
}};

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

// This offline oracle reconstructs the frozen mathematical recipe. It never
// reads AudioFir, the generated coefficient arrays, circular history, or the
// wrapper's factor selection. I0 is evaluated by its convergent power series.
long double bessel_i0(long double x) {
    long double sum = 1.0L;
    long double term = 1.0L;
    for (unsigned k = 1; k < 128; ++k) {
        const long double denominator = static_cast<long double>(k) * k;
        term *= x * x / (4.0L * denominator);
        const long double previous = sum;
        sum += term;
        if (sum == previous) { return sum; }
    }
    throw std::runtime_error("independent I0 series did not converge");
}

std::vector<long double> independent_coefficients(std::size_t factor) {
    const std::size_t middle = 48 * factor;
    std::vector<long double> coefficients(2 * middle + 1);
    long double normalization = 0.0L;
    const long double i0_beta = bessel_i0(9.0L);
    for (std::size_t k = 0; k < coefficients.size(); ++k) {
        const long double m = static_cast<long double>(k) -
                              static_cast<long double>(middle);
        const long double ratio = m / static_cast<long double>(middle);
        const long double ideal = m == 0.0L ? 0.94L / factor :
            std::sin(std::numbers::pi_v<long double> * 0.94L * m / factor) /
                (std::numbers::pi_v<long double> * m);
        coefficients[k] = ideal * bessel_i0(9.0L *
            std::sqrt(std::max(0.0L, 1.0L - ratio * ratio))) / i0_beta;
        normalization += coefficients[k];
    }
    for (auto& value : coefficients) { value /= normalization; }
    return coefficients;
}

long double direct_convolution(std::span<const double> chronological_samples,
                               std::span<const long double> coefficients,
                               std::size_t internal_time) {
    require(internal_time < chronological_samples.size(), "oracle time outside raw stream");
    long double sum = 0.0L;
    for (std::size_t k = 0; k < coefficients.size() && k <= internal_time; ++k) {
        sum += coefficients[k] * chronological_samples[internal_time - k];
    }
    return sum;
}

struct IndependentReference {
    rhodes::ResearchEngine raw;
    std::size_t factor;
    std::vector<long double> coefficients;
    std::vector<double> chronological_samples;

    explicit IndependentReference(RateCase rate)
        : factor(rate.factor), coefficients(independent_coefficients(rate.factor)) {
        require(raw.prepare(rate.host * static_cast<double>(factor)), "raw reference prepare failed");
    }

    std::vector<long double> render(std::size_t frames) {
        const std::size_t start = chronological_samples.size();
        chronological_samples.resize(start + factor * frames);
        require(raw.process(std::span<double>{chronological_samples}.subspan(start)),
                "raw reference processing failed");
        std::vector<long double> result(frames);
        for (std::size_t n = 0; n < frames; ++n) {
            result[n] = direct_convolution(chronological_samples, coefficients, start + factor * n);
        }
        return result;
    }

    void reset() {
        raw.reset();
        chronological_samples.clear();
    }
};

template<class Actual, class Expected>
void same_physics(const Actual& actual, const Expected& expected) {
    require(actual.mechanical_energy_joules() == expected.mechanical_energy_joules(),
            "mechanical energy differs at equal physical time");
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto a = actual.modal_state(key, mode);
            const auto b = expected.modal_state(key, mode);
            require(a && b, "missing modal diagnostic");
            require(a->displacement_m == b->displacement_m &&
                    a->velocity_m_per_s == b->velocity_m_per_s,
                    "q/v diagnostic is not the undelayed end-of-call state");
        }
    }
}

void same_configuration(const AudioEngine& actual, const AudioEngine& expected) {
    require(actual.sample_rate_hz() == expected.sample_rate_hz() &&
            actual.internal_sample_rate_hz() == expected.internal_sample_rate_hz() &&
            actual.oversampling_factor() == expected.oversampling_factor(),
            "host/internal rate or factor changed unexpectedly");
    same_physics(actual, expected);
}

long double compare(std::span<const double> actual, std::span<const long double> expected) {
    require(actual.size() == expected.size(), "reference output length differs");
    long double maximum_error = 0.0L;
    for (std::size_t n = 0; n < actual.size(); ++n) {
        const long double error = std::abs(static_cast<long double>(actual[n]) - expected[n]);
        require(error <= voltage_tolerance, "audio differs from direct high-rate convolution by >1e-12 V");
        maximum_error = std::max(maximum_error, error);
    }
    return maximum_error;
}

long double compare_render(AudioEngine& audio, IndependentReference& reference, std::size_t frames) {
    std::vector<double> output(frames, 77.0);
    require(audio.process(output), "ordinary audio rendering failed");
    const auto expected = reference.render(frames);
    same_physics(audio, reference.raw);
    return compare(output, expected);
}

template<class Engine>
void apply(Engine& engine, const EngineEvent& event) {
    switch (event.type) {
    case EventType::note_on: require(engine.note_on(event.key, event.value), "note-on failed"); break;
    case EventType::note_off: require(engine.note_off(event.key), "note-off failed"); break;
    case EventType::sustain: engine.set_sustain(event.value != 0.0); break;
    case EventType::all_notes_off: engine.all_notes_off(); break;
    case EventType::reset: engine.reset(); break;
    }
}

void apply_reference(IndependentReference& reference, const EngineEvent& event) {
    if (event.type == EventType::reset) { reference.reset(); }
    else { apply(reference.raw, event); }
}

constexpr std::array<EngineEvent, 21> event_script{{
    {0, 0, EventType::note_on, 24, 0.7},
    {0, 1, EventType::note_on, 72, 0.2},
    {1, 2, EventType::sustain, 0, 1.0},
    {17, 3, EventType::note_off, 24, 0.0},
    {23, 4, EventType::note_on, 24, 0.45},
    {24, 5, EventType::note_on, 24, 0.1},
    {63, 6, EventType::sustain, 0, 0.0},
    {64, 7, EventType::note_on, 72, 0.0},
    {97, 8, EventType::note_on, 0, 0.9},
    {113, 9, EventType::all_notes_off, 0, 0.0},
    {127, 10, EventType::sustain, 0, 1.0},
    {128, 11, EventType::note_on, 36, 0.6},
    {192, 12, EventType::note_off, 36, 0.0},
    {207, 13, EventType::sustain, 0, 0.0},
    {241, 14, EventType::all_notes_off, 0, 0.0},
    {257, 15, EventType::reset, 0, 0.0},
    {257, 16, EventType::note_on, 18, 0.75},
    {383, 17, EventType::note_off, 18, 0.0},
    {384, 18, EventType::sustain, 0, 1.0},
    {448, 19, EventType::sustain, 0, 0.0},
    {511, 20, EventType::all_notes_off, 0, 0.0}
}};

void high_rate_oracle() {
    constexpr std::array<double, 3> intensities{0.1, 0.7, 1.0};
    constexpr std::size_t frames = 193;
    std::size_t cases = 0;
    std::size_t compared_frames = 0;
    long double maximum_error = 0.0L;
    for (const auto rate : representative_rates) {
        for (std::size_t key = 0; key < rhodes::key_count; ++key) {
            for (const double intensity : intensities) {
                AudioEngine audio;
                require(audio.prepare(rate.host), "audio prepare failed");
                IndependentReference reference(rate);
                maximum_error = std::max(maximum_error, compare_render(audio, reference, 7));
                require(audio.note_on(key, intensity) && reference.raw.note_on(key, intensity),
                        "single-key strike failed");
                maximum_error = std::max(maximum_error, compare_render(audio, reference, frames));
                ++cases;
                compared_frames += frames + 7;
            }
        }
        AudioEngine chord;
        require(chord.prepare(rate.host), "chord prepare failed");
        IndependentReference reference(rate);
        for (std::size_t key = 0; key < rhodes::key_count; ++key) {
            require(chord.note_on(key, 1.0) && reference.raw.note_on(key, 1.0), "full harp strike failed");
        }
        maximum_error = std::max(maximum_error, compare_render(chord, reference, 384));
        ++cases;
        compared_frames += 384;
    }
    std::cout << "oracle cases=" << cases << " host_frames=" << compared_frames
              << " maximum_error_V=" << static_cast<double>(maximum_error) << '\n';
}

void rate_boundaries() {
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<RateCase, 11> rates{{
        {32000.0, 4}, {std::nextafter(32000.0, infinity), 4}, {44100.0, 4},
        {std::nextafter(48000.0, -infinity), 4}, {48000.0, 4},
        {std::nextafter(48000.0, infinity), 2},
        {std::nextafter(96000.0, -infinity), 2}, {96000.0, 2},
        {std::nextafter(96000.0, infinity), 1},
        {std::nextafter(192000.0, -infinity), 1}, {192000.0, 1}
    }};
    long double maximum_error = 0.0L;
    for (const auto rate : rates) {
        AudioEngine audio;
        require(audio.prepare(rate.host), "supported boundary rate rejected");
        require(audio.sample_rate_hz() == rate.host, "host rate rounded or replaced");
        require(audio.oversampling_factor() == rate.factor, "wrong factor at rate boundary");
        require(audio.internal_sample_rate_hz() == rate.host * static_cast<double>(rate.factor),
                "internal rate is not L times host rate");
        require(audio.latency_frames() == 48, "latency is not 48 host frames");
        IndependentReference reference(rate);
        for (const auto key : {std::size_t{0}, std::size_t{36}, std::size_t{72}}) {
            require(audio.note_on(key, 0.7) && reference.raw.note_on(key, 0.7), "boundary strike failed");
        }
        for (const auto count : {std::size_t{1}, std::size_t{35}, std::size_t{108}}) {
            maximum_error = std::max(maximum_error, compare_render(audio, reference, count));
        }
        require(!audio.modal_state(rhodes::key_count, 0) &&
                !audio.modal_state(0, rhodes::modes_per_key), "invalid modal coordinate accepted");
    }
    std::cout << "accepted_rate_edges=" << rates.size() << " host_frames=" << rates.size() * 144
              << " maximum_error_V=" << static_cast<double>(maximum_error) << '\n';
}

void persistent_events() {
    constexpr auto events = std::span<const EngineEvent>{event_script}.first(15);
    constexpr std::array<double, 3> coupling{1.0, 0.45, 0.2};
    long double maximum_error = 0.0L;
    for (const auto rate : representative_rates) {
        AudioEngine audio;
        require(audio.prepare(rate.host), "persistent fixture prepare failed");
        IndependentReference reference(rate);
        std::size_t position = 0;
        for (const auto& event : events) {
            maximum_error = std::max(maximum_error,
                compare_render(audio, reference, event.offset - position));
            position = event.offset;
            const auto before = audio;
            apply(audio, event);
            apply_reference(reference, event);
            same_physics(audio, reference.raw);
            for (std::size_t key = 0; key < rhodes::key_count; ++key) {
                for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
                    const auto previous = before.modal_state(key, mode).value();
                    const auto current = audio.modal_state(key, mode).value();
                    require(current.displacement_m == previous.displacement_m,
                            "note/release/pedal event reset displacement");
                    if (event.type == EventType::note_on && event.value != 0.0 && key == event.key) {
                        const double expected = previous.velocity_m_per_s + 0.06 * event.value * coupling[mode];
                        require(std::abs(current.velocity_m_per_s - expected) <= 2e-16,
                                "retrigger did not add an impulse to persistent velocity");
                        if (event.offset == 23 || event.offset == 24) {
                            require(previous.displacement_m != 0.0 && previous.velocity_m_per_s != 0.0,
                                    "retrigger fixture did not exercise moving mechanics");
                        }
                    } else {
                        require(current.velocity_m_per_s == previous.velocity_m_per_s,
                                "release/pedal event changed velocity discontinuously");
                    }
                }
            }
        }
        maximum_error = std::max(maximum_error, compare_render(audio, reference, 512 - position));
    }
    std::cout << "persistent_event_checks=" << events.size() * representative_rates.size()
              << " host_frames=1536 maximum_error_V=" << static_cast<double>(maximum_error) << '\n';
}

std::vector<double> render_partitioned(AudioEngine& audio, std::size_t frames,
                                      std::span<const std::size_t> pattern) {
    std::vector<double> output(frames, 77.0);
    std::size_t position = 0;
    std::size_t event_index = 0;
    std::size_t partition = 0;
    while (position < frames) {
        while (event_index < event_script.size() && event_script[event_index].offset == position) {
            apply(audio, event_script[event_index++]);
        }
        std::size_t count = std::min(pattern[partition++ % pattern.size()], frames - position);
        if (event_index < event_script.size()) {
            count = std::min(count, static_cast<std::size_t>(event_script[event_index].offset) - position);
        }
        require(audio.process(std::span<double>{output}.subspan(position, count)), "partition rendering failed");
        position += count;
    }
    return output;
}

void partition_invariance() {
    constexpr std::array<std::size_t, 1> whole{4096};
    constexpr std::array<std::size_t, 10> irregular{1, 17, 3, 64, 2, 127, 5, 31, 385, 9};
    constexpr std::array<std::size_t, 7> empty_and_wrap{97, 0, 96, 193, 7, 0, 11};
    constexpr std::size_t frames = 1537;
    for (const auto rate : representative_rates) {
        AudioEngine baseline;
        require(baseline.prepare(rate.host), "partition prepare failed");
        auto split = baseline;
        auto with_empty = baseline;
        const auto expected = render_partitioned(baseline, frames, whole);
        require(render_partitioned(split, frames, irregular) == expected,
                "irregular block partition changed audio samples");
        require(render_partitioned(with_empty, frames, empty_and_wrap) == expected,
                "empty block or ring-wrap partition changed audio samples");
        same_configuration(split, baseline);
        same_configuration(with_empty, baseline);
    }
    std::cout << "partition_comparisons=6 exact_host_frame_comparisons=" << 6 * frames << '\n';
}

void prepare_atomicity() {
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<double, 8> rejected{
        std::numeric_limits<double>::quiet_NaN(), infinity, -infinity, -1.0, 0.0,
        std::nextafter(32000.0, -infinity), std::nextafter(192000.0, infinity),
        std::numeric_limits<double>::max()
    };
    for (const auto rate : representative_rates) {
        AudioEngine audio;
        require(audio.prepare(rate.host), "atomicity prepare failed");
        require(audio.note_on(0, 0.7) && audio.note_on(37, 1.0) && audio.note_on(72, 0.5),
                "atomicity strikes failed");
        audio.set_sustain(true);
        std::array<double, 137> warmup{};
        require(audio.process(warmup) && audio.note_off(37), "atomicity warmup failed");
        auto unchanged = audio;
        for (const double bad : rejected) {
            require(!audio.prepare(bad), "invalid host rate accepted");
            same_configuration(audio, unchanged);
            std::array<double, 257> actual{};
            std::array<double, 257> expected{};
            require(audio.process(actual) && unchanged.process(expected), "post-rejection rendering failed");
            require(actual == expected, "rejected prepare changed future audio/filter history");
            same_configuration(audio, unchanged);
            audio.set_sustain(false);
            unchanged.set_sustain(false);
            require(audio.note_on(37, 0.25) && unchanged.note_on(37, 0.25),
                    "post-rejection retrigger failed");
        }
    }
    std::cout << "rejected_prepares=" << rejected.size() * representative_rates.size()
              << " exact_future_host_frames=" << rejected.size() * representative_rates.size() * 257 << '\n';
}

void reset_flush() {
    for (const auto rate : representative_rates) {
        AudioEngine audio;
        require(audio.prepare(rate.host), "reset prepare failed");
        require(audio.note_on(0, 0.8) && audio.note_on(72, 1.0), "reset setup strike failed");
        audio.set_sustain(true);
        std::array<double, 137> warmup{};
        require(audio.process(warmup), "reset warmup failed");
        require(std::any_of(warmup.begin(), warmup.end(), [](double x) { return std::abs(x) > 1e-6; }),
                "reset fixture did not populate audible filter history");
        const auto populated = audio;
        AudioEngine fresh;
        require(fresh.prepare(rate.host), "fresh reset reference prepare failed");
        audio.reset();
        same_configuration(audio, fresh);
        std::array<double, 257> silence{};
        silence.fill(77.0);
        require(audio.process(silence), "reset silence render failed");
        require(std::all_of(silence.begin(), silence.end(), [](double x) { return x == 0.0; }),
                "explicit reset retained filter tail or mechanical motion");
        audio = populated;
        audio.reset();
        require(audio.note_on(37, 0.6) && fresh.note_on(37, 0.6), "reset restrike failed");
        std::array<double, 257> actual{};
        std::array<double, 257> expected{};
        require(audio.process(std::span<double>{actual}.first(17)) &&
                fresh.process(std::span<double>{expected}.first(17)), "reset restrike rendering failed");
        require(audio.note_off(37) && fresh.note_off(37), "reset release failed");
        require(audio.process(std::span<double>{actual}.subspan(17)) &&
                fresh.process(std::span<double>{expected}.subspan(17)), "reset release rendering failed");
        require(actual == expected, "reset retained history, wrong phase, or pedal state");
        same_configuration(audio, fresh);
        for (const auto replacement : representative_rates) {
            auto prepared = populated;
            AudioEngine new_reference;
            require(prepared.prepare(replacement.host) && new_reference.prepare(replacement.host),
                    "valid replacement prepare rejected");
            same_configuration(prepared, new_reference);
            require(prepared.note_on(12, 0.75) && new_reference.note_on(12, 0.75),
                    "replacement prepare strike failed");
            require(prepared.process(std::span<double>{actual}.first(17)) &&
                    new_reference.process(std::span<double>{expected}.first(17)), "replacement prepare render failed");
            require(prepared.note_off(12) && new_reference.note_off(12), "replacement prepare release failed");
            require(prepared.process(std::span<double>{actual}.subspan(17)) &&
                    new_reference.process(std::span<double>{expected}.subspan(17)), "replacement release render failed");
            require(actual == expected, "valid prepare retained previous mechanics/history/phase");
            same_configuration(prepared, new_reference);
        }
    }
    std::cout << "explicit_reset_cases=6 rate_replacement_cases=9 checked_host_frames=3855\n";
}

void expect_result(const rhodes::ProcessResult& result, ProcessStatus status,
                   std::size_t frames, std::size_t sample, std::size_t event,
                   bool reset, ProcessSubsystem subsystem) {
    require(result.status == status && result.succeeded() == (status == ProcessStatus::ok),
            "incorrect event processor status");
    require(result.frames_processed == frames && result.failing_sample == sample &&
            result.failing_event == event && result.requires_reset == reset &&
            result.subsystem == subsystem, "incorrect event processor failure coordinates");
}

void event_timing() {
    constexpr std::size_t frames = 640;
    constexpr std::array<std::size_t, 8> pattern{1, 4, 17, 64, 193, 2, 97, 31};
    long double maximum_error = 0.0L;
    for (const auto rate : representative_rates) {
        rhodes::BasicEventProcessor<AudioEngine> whole;
        rhodes::BasicEventProcessor<AudioEngine> partitioned;
        require(whole.prepare(rate.host) && partitioned.prepare(rate.host), "event prepare failed");
        std::array<double, frames> whole_output{};
        expect_result(whole.process(whole_output, event_script), ProcessStatus::ok,
                      frames, no_failure, no_failure, false, ProcessSubsystem::none);
        std::array<double, frames> split_output{};
        IndependentReference reference(rate);
        std::size_t position = 0;
        std::size_t event_index = 0;
        std::size_t partition = 0;
        while (position < frames) {
            const std::size_t count = std::min(pattern[partition++ % pattern.size()], frames - position);
            std::vector<EngineEvent> local_events;
            for (const auto& event : event_script) {
                if (event.offset >= position && event.offset < position + count) {
                    auto local = event;
                    local.offset -= static_cast<std::uint32_t>(position);
                    local_events.push_back(local);
                }
            }
            const auto output = std::span<double>{split_output}.subspan(position, count);
            expect_result(partitioned.process(output, local_events), ProcessStatus::ok,
                          count, no_failure, no_failure, false, ProcessSubsystem::none);
            std::vector<long double> expected;
            expected.reserve(count);
            for (std::size_t n = position; n < position + count; ++n) {
                while (event_index < event_script.size() && event_script[event_index].offset == n) {
                    apply_reference(reference, event_script[event_index++]);
                }
                expected.push_back(reference.render(1).front());
            }
            maximum_error = std::max(maximum_error, compare(output, expected));
            same_physics(partitioned.engine(), reference.raw);
            position += count;
            require(partitioned.frame_cursor() == position, "event cursor changed with reset/block partition");
        }
        require(whole_output == split_output, "filtered event offsets depend on host block partition");
        require(whole.frame_cursor() == frames, "whole event cursor is wrong");
        same_configuration(whole.engine(), partitioned.engine());
    }
    std::cout << "event_schedule_runs=6 scheduled_events=126 oracle_host_frames=1920"
              << " maximum_error_V=" << static_cast<double>(maximum_error) << '\n';
}

// Deterministic test-only engine. Fault positions count internal samples, so
// every substep can fail even after the selected phase-zero value exists.
struct FaultEngine {
    static inline std::size_t fail_at_internal = no_failure;
    static inline bool fail_note_on = false;
    double rate = 48000.0;
    std::size_t samples_attempted = 0;
    std::size_t events_attempted = 0;

    bool prepare(double sample_rate) noexcept {
        if (!std::isfinite(sample_rate) || sample_rate < rhodes::minimum_sample_rate_hz ||
            sample_rate > rhodes::maximum_sample_rate_hz) { return false; }
        rate = sample_rate;
        reset();
        return true;
    }
    void reset() noexcept { samples_attempted = events_attempted = 0; }
    bool note_on(std::size_t key, double) noexcept {
        ++events_attempted;
        return !(fail_note_on && key == 17);
    }
    bool note_off(std::size_t) noexcept { ++events_attempted; return true; }
    void set_sustain(bool) noexcept { ++events_attempted; }
    void all_notes_off() noexcept { ++events_attempted; }
    static double sample(std::size_t n) noexcept {
        return 0.05 + static_cast<double>((n * 37) % 251) / 251.0;
    }
    bool process(std::span<double> output) noexcept {
        for (std::size_t n = 0; n < output.size(); ++n) {
            const std::size_t time = samples_attempted++;
            if (time == fail_at_internal) {
                std::fill(output.begin() + static_cast<std::ptrdiff_t>(n), output.end(), 0.0);
                return false;
            }
            output[n] = sample(time);
        }
        return true;
    }
    std::optional<rhodes::ModalState> modal_state(std::size_t key, std::size_t mode) const noexcept {
        if (key != 0 || mode != 0) { return std::nullopt; }
        return rhodes::ModalState{static_cast<double>(samples_attempted), static_cast<double>(events_attempted)};
    }
    double mechanical_energy_joules() const noexcept { return static_cast<double>(samples_attempted); }
    double sample_rate_hz() const noexcept { return rate; }
};

using FaultAudio = rhodes::BasicAudioEngine<FaultEngine>;
using FaultProcessor = rhodes::BasicEventProcessor<FaultAudio>;

std::vector<long double> fault_reference(RateCase rate, std::size_t frames) {
    const auto coefficients = independent_coefficients(rate.factor);
    std::vector<double> raw(frames * rate.factor);
    for (std::size_t n = 0; n < raw.size(); ++n) { raw[n] = FaultEngine::sample(n); }
    std::vector<long double> result(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        result[n] = direct_convolution(raw, coefficients, n * rate.factor);
    }
    return result;
}

void check_failed_output(std::span<const double> output, std::span<const long double> expected,
                         std::size_t failed_host_frame) {
    (void)compare(output.first(failed_host_frame), expected.first(failed_host_frame));
    for (std::size_t n = failed_host_frame; n < output.size(); ++n) {
        require(output[n] == 0.0, "failed host frame or suffix was not silenced");
    }
}

void check_fault_fence(FaultProcessor& processor, std::size_t cursor,
                     double attempted_samples, double attempted_events) {
    constexpr std::array<EngineEvent, 1> queued_reset{{{0, 0, EventType::reset, 0, 0.0}}};
    std::array<double, 13> blocked{};
    blocked.fill(77.0);
    expect_result(processor.process(blocked, queued_reset), ProcessStatus::reset_required,
                  0, no_failure, no_failure, true, ProcessSubsystem::engine);
    require(std::all_of(blocked.begin(), blocked.end(), [](double x) { return x == 0.0; }),
            "latched processor leaked audio");
    require(!processor.prepare(std::numeric_limits<double>::quiet_NaN()) && processor.requires_reset(),
            "invalid prepare cleared arithmetic fault latch");
    require(!processor.set_sustain(true), "fault latch accepted sustain change");
    const auto state = processor.engine().modal_state(0, 0).value();
    require(processor.frame_cursor() == cursor && state.displacement_m == attempted_samples &&
            state.velocity_m_per_s == attempted_events, "fault fence executed queued work");
}

void fault_substeps() {
    constexpr std::size_t warmup_frames = 11;
    constexpr std::size_t block_frames = 113;
    std::size_t direct_cases = 0;
    std::size_t processor_cases = 0;
    for (const auto rate : representative_rates) {
        const auto expected = fault_reference(rate, warmup_frames + block_frames);
        for (const std::size_t failed_host : {std::size_t{0}, std::size_t{17}, std::size_t{63}}) {
            for (std::size_t substep = 0; substep < rate.factor; ++substep) {
                FaultEngine::fail_at_internal = no_failure;
                FaultAudio audio;
                require(audio.prepare(rate.host), "fault audio prepare failed");
                std::array<double, warmup_frames> warmup{};
                require(audio.process(warmup), "fault audio warmup failed");
                const std::size_t failed_internal = (warmup_frames + failed_host) * rate.factor + substep;
                FaultEngine::fail_at_internal = failed_internal;
                std::array<double, block_frames> output{};
                output.fill(77.0);
                require(!audio.process(output), "internal arithmetic fault was swallowed");
                check_failed_output(output, std::span<const long double>{expected}.subspan(warmup_frames), failed_host);
                require(audio.modal_state(0, 0)->displacement_m == static_cast<double>(failed_internal + 1),
                        "wrapper continued internal processing after a fault");
                ++direct_cases;

                FaultEngine::fail_at_internal = no_failure;
                FaultProcessor processor;
                require(processor.prepare(rate.host), "fault processor prepare failed");
                expect_result(processor.process(warmup), ProcessStatus::ok, warmup_frames,
                              no_failure, no_failure, false, ProcessSubsystem::none);
                FaultEngine::fail_at_internal = failed_internal;
                output.fill(77.0);
                expect_result(processor.process(output), ProcessStatus::arithmetic_fault,
                              failed_host, failed_host, no_failure, true, ProcessSubsystem::engine);
                check_failed_output(output, std::span<const long double>{expected}.subspan(warmup_frames), failed_host);
                check_fault_fence(processor, warmup_frames + failed_host,
                                  static_cast<double>(failed_internal + 1), 0.0);
                FaultEngine::fail_at_internal = no_failure;
                processor.reset();
                require(!processor.requires_reset() && processor.frame_cursor() == 0,
                        "explicit reset did not clear arithmetic fault latch");
                expect_result(processor.process(output), ProcessStatus::ok, block_frames,
                              no_failure, no_failure, false, ProcessSubsystem::none);
                (void)compare(output, std::span<const long double>{expected}.first(block_frames));
                ++processor_cases;
            }
        }

        FaultEngine::fail_at_internal = no_failure;
        FaultProcessor processor;
        require(processor.prepare(rate.host), "event-fault prepare failed");
        std::array<double, warmup_frames> warmup{};
        expect_result(processor.process(warmup), ProcessStatus::ok, warmup_frames,
                      no_failure, no_failure, false, ProcessSubsystem::none);
        constexpr std::array<EngineEvent, 3> events{{
            {0, 0, EventType::note_off, 2, 0.0},
            {5, 1, EventType::note_on, 17, 0.75},
            {5, 2, EventType::reset, 0, 0.0}
        }};
        FaultEngine::fail_note_on = true;
        std::array<double, block_frames> output{};
        output.fill(77.0);
        expect_result(processor.process(output, events), ProcessStatus::arithmetic_fault,
                      5, 5, 1, true, ProcessSubsystem::events);
        check_failed_output(output, std::span<const long double>{expected}.subspan(warmup_frames), 5);
        check_fault_fence(processor, warmup_frames + 5,
                          static_cast<double>((warmup_frames + 5) * rate.factor), 2.0);
        FaultEngine::fail_note_on = false;
        processor.reset();
        constexpr std::array<EngineEvent, 1> valid{{{0, 0, EventType::note_on, 18, 0.5}}};
        expect_result(processor.process(output, valid), ProcessStatus::ok, block_frames,
                      no_failure, no_failure, false, ProcessSubsystem::none);
        (void)compare(output, std::span<const long double>{expected}.first(block_frames));
        require(processor.engine().modal_state(0, 0)->velocity_m_per_s == 1.0,
                "event-fault reset did not restore event processing");
    }
    std::cout << "internal_fault_direct_cases=" << direct_cases
              << " internal_fault_processor_cases=" << processor_cases
              << " event_fault_latch_cases=3\n";
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 8> tests{{
    {"high_rate_oracle", high_rate_oracle},
    {"rate_boundaries", rate_boundaries},
    {"persistent_events", persistent_events},
    {"partition_invariance", partition_invariance},
    {"prepare_atomicity", prepare_atomicity},
    {"reset_flush", reset_flush},
    {"event_timing", event_timing},
    {"fault_substeps", fault_substeps}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    try {
        require(argc <= 2, "usage: audio_engine_test [fixture|all]");
        std::cout << std::setprecision(17);
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (selected == "all" || selected == name) {
                test();
                std::cout << "PASS " << name << '\n';
                found = true;
            }
        }
        require(found, "unknown audio-engine fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
