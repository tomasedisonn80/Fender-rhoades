#include "rhodes/research_engine.hpp"

#include <array>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view probe_name = "rhodes_alias_primitive_probe";
constexpr std::size_t maximum_modes = 2;
constexpr std::size_t maximum_frames = 2048;
constexpr std::size_t maximum_json_bytes = 1024 * 1024;
constexpr double modal_mass_kg = 0.001;
constexpr double decay_rate_per_second = 0.0;
constexpr std::array<double, 3> pickup_coefficients{0.4, 800.0, 400000.0};

struct Mode {
    double frequency_hz;
    double amplitude_m;
    double phase_rad;
};

struct Fixture {
    std::string_view id;
    double sample_rate_hz;
    std::size_t frame_count;
    std::size_t mode_count;
    std::array<Mode, maximum_modes> modes;
};

// Frozen synthetic fixtures from W2-025. These are stationary initial states,
// not measured parameters or strikes. The unused second mode is never read.
constexpr Mode one_mode{13500.0, 1.0e-4, std::numbers::pi / 7.0};
constexpr std::array<Mode, maximum_modes> two_modes{{
    {7500.0, 1.0e-4, std::numbers::pi / 7.0},
    {13500.0, 6.0e-5, -std::numbers::pi / 5.0}}};
constexpr std::array<Fixture, 4> fixtures{{
    {"one_mode_48000", 48000.0, 1024, 1, {one_mode, {}}},
    {"one_mode_96000", 96000.0, 2048, 1, {one_mode, {}}},
    {"two_modes_48000", 48000.0, 1024, 2, two_modes},
    {"two_modes_96000", 96000.0, 2048, 2, two_modes}}};

struct Frame {
    std::array<rhodes::ModalState, maximum_modes> modes{};
    double q_m = 0.0;
    double v_m_per_s = 0.0;
    double pickup_volts = 0.0;
};
using Samples = std::array<Frame, maximum_frames>;

void require_finite(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("Nonfinite fixture, modal state, sum, or pickup voltage");
    }
}

const Fixture& preflight(int argc, char** argv) {
    // Exact accepted forms also reject repeated flags, trailing arguments,
    // free-form physical parameters, and combinations with --help.
    if (argc != 3 || std::string_view{argv[1]} != "--case") {
        throw std::runtime_error("Require exactly --case ID or --help");
    }
    const std::string_view id{argv[2]};
    if (id.size() > 64) { throw std::runtime_error("Case ID exceeds 64 bytes"); }
    for (const auto& fixture : fixtures) {
        if (fixture.id == id) { return fixture; }
    }
    throw std::runtime_error("Unsupported case ID; use --help for the four fixed cases");
}

void render(const Fixture& fixture, Samples& samples) {
    require_finite(fixture.sample_rate_hz);
    require_finite(modal_mass_kg);
    require_finite(decay_rate_per_second);
    if (fixture.frame_count == 0 || fixture.frame_count > maximum_frames ||
            fixture.mode_count == 0 || fixture.mode_count > maximum_modes) {
        throw std::runtime_error("Internal fixture dimensions exceed fixed bounds");
    }
    rhodes::PolynomialPickup pickup;
    for (const double value : pickup_coefficients) { require_finite(value); }
    if (!pickup.configure(pickup_coefficients[0], pickup_coefficients[1],
                          pickup_coefficients[2])) {
        throw std::runtime_error("PolynomialPickup configuration failed");
    }
    std::array<rhodes::ModalOscillator, maximum_modes> oscillators{};
    for (std::size_t mode_index = 0; mode_index < fixture.mode_count; ++mode_index) {
        const auto& mode = fixture.modes[mode_index];
        require_finite(mode.frequency_hz);
        require_finite(mode.amplitude_m);
        require_finite(mode.phase_rad);
        auto& oscillator = oscillators[mode_index];
        if (!oscillator.configure({mode.frequency_hz, decay_rate_per_second,
                                   modal_mass_kg}, fixture.sample_rate_hz)) {
            throw std::runtime_error("ModalOscillator configuration failed");
        }
        const rhodes::ModalState initial{
            mode.amplitude_m * std::cos(mode.phase_rad),
            -2.0 * std::numbers::pi * mode.frequency_hz * mode.amplitude_m *
                std::sin(mode.phase_rad)};
        require_finite(initial.displacement_m);
        require_finite(initial.velocity_m_per_s);
        if (!oscillator.set_state(initial)) {
            throw std::runtime_error("ModalOscillator initial state rejected");
        }
    }

    for (std::size_t index = 0; index < fixture.frame_count; ++index) {
        auto& sample = samples[index];
        // Observe t=index/Fs before any advance. Sum local modal motion before
        // exactly one nonlinear pickup conversion, with no harp or audio path.
        for (std::size_t mode_index = 0; mode_index < fixture.mode_count; ++mode_index) {
            const auto state = oscillators[mode_index].state();
            require_finite(state.displacement_m);
            require_finite(state.velocity_m_per_s);
            sample.modes[mode_index] = state;
            sample.q_m += state.displacement_m;
            sample.v_m_per_s += state.velocity_m_per_s;
            require_finite(sample.q_m);
            require_finite(sample.v_m_per_s);
        }
        const auto voltage = pickup.voltage(sample.q_m, sample.v_m_per_s);
        if (!voltage) { throw std::runtime_error("PolynomialPickup voltage failed"); }
        require_finite(*voltage);
        sample.pickup_volts = *voltage;
        // No unobserved transition is needed after the final recorded frame.
        if (index + 1 < fixture.frame_count) {
            for (std::size_t mode_index = 0; mode_index < fixture.mode_count; ++mode_index) {
                if (!oscillators[mode_index].advance()) {
                    throw std::runtime_error("ModalOscillator advance failed");
                }
            }
        }
    }
}

std::string serialize(const Fixture& fixture, const Samples& samples) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << "{\"schema_version\":1,\"schema\":\"rhodes.alias_primitive_probe.v1\","
              "\"provenance\":\"synthetic_development_fixture\",\"case_id\":\""
           << fixture.id << "\",\"settings\":{\"sample_rate_hz\":" << fixture.sample_rate_hz
           << ",\"frame_count\":" << fixture.frame_count
           << ",\"modal_mass_kg\":" << modal_mass_kg
           << ",\"decay_rate_per_second\":" << decay_rate_per_second
           << ",\"observation\":\"before_advance\",\"boundary\":\"single_pickup_voltage\","
              "\"pickup_coefficients\":{\"a1_wb_turn_per_m\":" << pickup_coefficients[0]
           << ",\"a2_wb_turn_per_m2\":" << pickup_coefficients[1]
           << ",\"a3_wb_turn_per_m3\":" << pickup_coefficients[2] << "}},\"modes\":[";
    for (std::size_t mode_index = 0; mode_index < fixture.mode_count; ++mode_index) {
        if (mode_index != 0) { output << ','; }
        const auto& mode = fixture.modes[mode_index];
        output << "{\"frequency_hz\":" << mode.frequency_hz
               << ",\"amplitude_m\":" << mode.amplitude_m
               << ",\"phase_rad\":" << mode.phase_rad << '}';
    }
    output << "],\"frames\":[";
    for (std::size_t index = 0; index < fixture.frame_count; ++index) {
        if (index != 0) { output << ','; }
        const auto& sample = samples[index];
        output << "{\"index\":" << index << ",\"modal_q_m\":[";
        for (std::size_t mode_index = 0; mode_index < fixture.mode_count; ++mode_index) {
            if (mode_index != 0) { output << ','; }
            output << sample.modes[mode_index].displacement_m;
        }
        output << "],\"modal_v_m_per_s\":[";
        for (std::size_t mode_index = 0; mode_index < fixture.mode_count; ++mode_index) {
            if (mode_index != 0) { output << ','; }
            output << sample.modes[mode_index].velocity_m_per_s;
        }
        output << "],\"q_m\":" << sample.q_m << ",\"v_m_per_s\":" << sample.v_m_per_s
               << ",\"P_volts\":" << sample.pickup_volts << '}';
    }
    output << "],\"status\":\"ok\",\"success\":true}\n";
    if (!output) { throw std::runtime_error("Cannot serialize JSON"); }
    auto document = output.str();
    if (document.size() > maximum_json_bytes) {
        throw std::runtime_error("Internal JSON exceeds 1 MiB output bound");
    }
    return document;
}

void write_stdout(std::string_view document) {
    std::cout.write(document.data(), static_cast<std::streamsize>(document.size()));
    std::cout.flush();
    if (!std::cout) { throw std::runtime_error("Cannot write stdout"); }
}

void help() {
    write_stdout(
        "rhodes_alias_primitive_probe --case ID\n"
        "rhodes_alias_primitive_probe --help\n"
        "Offline synthetic stationary primitive probe; no measured fidelity claim.\n"
        "Cases: one_mode_48000, one_mode_96000, two_modes_48000, two_modes_96000.\n"
        "48 kHz cases contain 1024 frames; 96 kHz cases contain 2048 frames.\n"
        "Observe modal q/v before advancing; sum motion before one pickup conversion.\n"
        "Successful --case output is one complete JSON document, at most 1 MiB,\n"
        "with modal and summed SI motion plus pickup P_volts at 17 significant digits.\n"
        "Only these exact forms are accepted. No stdin, files, or free parameters.\n"
        "Errors exit 2 with stderr. Computation errors leave stdout empty; write\n"
        "errors can leave partial bytes. Require exit 0 and complete JSON for success.\n");
}
} // namespace

int main(int argc, char** argv) {
    try {
#if defined(SIGPIPE)
        // A closed stdout pipe must reach the ordinary checked-write error path
        // instead of terminating without the stderr diagnostic.
        if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
            throw std::runtime_error("Cannot configure stdout pipe error handling");
        }
#endif
        if (argc == 2 && std::string_view{argv[1]} == "--help") {
            help();
            return 0;
        }
        const auto& fixture = preflight(argc, argv);
        Samples samples{};
        render(fixture, samples);
        // Build the entire finite, bounded document before touching stdout.
        write_stdout(serialize(fixture, samples));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << probe_name << ": " << error.what() << '\n';
        return 2;
    }
}
