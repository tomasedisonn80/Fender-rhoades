#include "rhodes/research_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
void near(double actual, double expected, double tolerance, std::string_view message) {
    require(std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <= tolerance, message);
}
rhodes::ModalOscillator oscillator(double decay = 4.0) {
    rhodes::ModalOscillator mode;
    require(mode.configure({440.0, decay, 0.001}, 48000.0), "configure oscillator");
    require(mode.set_state({0.0001, 0.03}), "initial state");
    return mode;
}
void exact_solution() {
    auto mode = oscillator();
    constexpr int frames = 1739;
    const double time = static_cast<double>(frames) / 48000.0;
    const double omega = 2.0 * std::numbers::pi * 440.0;
    const double wd = std::sqrt(omega * omega - 16.0);
    const double a = 0.0001;
    const double b = (0.03 + 4.0 * a) / wd;
    const double expected_q = std::exp(-4.0 * time) *
        (a * std::cos(wd * time) + b * std::sin(wd * time));
    const double expected_v = -4.0 * expected_q + std::exp(-4.0 * time) * wd *
        (-a * std::sin(wd * time) + b * std::cos(wd * time));
    for (int i = 0; i < frames; ++i) { require(mode.advance(), "advance"); }
    near(mode.state().displacement_m, expected_q, 1e-16, "closed-form displacement");
    near(mode.state().velocity_m_per_s, expected_v, 5e-13, "closed-form velocity");
}
void energy_loss() {
    auto mode = oscillator();
    double previous = mode.energy_joules();
    const double initial = previous;
    for (int i = 0; i < 48000; ++i) {
        require(mode.advance(), "advance");
        const double energy = mode.energy_joules();
        require(energy <= previous * (1.0 + 1e-12), "unforced damped energy increased");
        previous = energy;
    }
    require(previous < initial * 0.001, "energy must decay over one second");
}
void energy_conservation() {
    auto mode = oscillator(0.0);
    const double energy = mode.energy_joules();
    for (int i = 0; i < 96000; ++i) { require(mode.advance(), "advance lossless"); }
    near(mode.energy_joules(), energy, energy * 2e-11, "lossless energy conservation");
}
void retrigger() {
    auto mode = oscillator();
    for (int i = 0; i < 37; ++i) { require(mode.advance(), "advance"); }
    const auto before = mode.state();
    require(mode.add_impulse(0.00002), "impulse accepted");
    near(mode.state().displacement_m, before.displacement_m, 0.0, "impulse preserves q");
    near(mode.state().velocity_m_per_s, before.velocity_m_per_s + 0.02, 1e-16, "impulse adds momentum");
    rhodes::ResearchEngine engine;
    require(engine.note_on(40, 0.6), "strike");
    std::array<double, 97> block{};
    require(engine.process(block), "advance engine");
    const auto state = *engine.modal_state(40, 0);
    require(engine.note_on(40, 0.4), "restrike");
    const auto after = *engine.modal_state(40, 0);
    near(after.displacement_m, state.displacement_m, 0.0, "restrike preserves q");
    near(after.velocity_m_per_s, state.velocity_m_per_s + 0.024, 1e-15, "restrike preserves prior v");
    require(engine.note_off(40), "release");
    near(engine.modal_state(40, 0)->displacement_m, after.displacement_m, 0.0, "release preserves q");
    near(engine.modal_state(40, 0)->velocity_m_per_s, after.velocity_m_per_s, 0.0, "release preserves v");
}
void pickup_derivative() {
    rhodes::PolynomialPickup pickup;
    constexpr double x = 0.0002;
    constexpr double h = 1e-9;
    constexpr double velocity = -0.03;
    const double derivative = (*pickup.flux_linkage(x + h) - *pickup.flux_linkage(x - h)) / (2.0 * h);
    near(*pickup.voltage(x, velocity), -derivative * velocity, 1e-11, "voltage is negative flux derivative");
    near(*pickup.voltage(x, 0.0), 0.0, 0.0, "stationary flux produces no emf");
}
void pickup_intermodulation() {
    rhodes::PolynomialPickup pickup;
    require(pickup.configure(0.4, 1000.0, 0.0), "configure pickup");
    constexpr double x1 = 0.0001, x2 = -0.00003, v1 = 0.02, v2 = -0.01;
    const double joint = *pickup.voltage(x1 + x2, v1 + v2);
    const double isolated = *pickup.voltage(x1, v1) + *pickup.voltage(x2, v2);
    near(joint - isolated, -2000.0 * (x1 * v2 + x2 * v1), 1e-17, "cross terms preserved");
    require(std::abs(joint - isolated) > 0.001, "test must discriminate separate nonlinearities");
}
void network_loading() {
    rhodes::SyntheticResistiveHarp harp;
    require(harp.configure(180.0, 1425.0), "configure equal source and load resistance");
    near(harp.source_resistance_ohm(), 1425.0, 1e-12, "73 branches remain electrically connected");
    std::array<double, rhodes::key_count> sources{};
    sources[4] = 1.0;
    near(*harp.voltage(sources), 1.0 / 6.0, 1e-16, "two silent parallel pickups load active source");
    sources[5] = 1.0;
    sources[6] = 1.0;
    near(*harp.voltage(sources), 0.5, 1e-16, "equal triple source group");
    sources = {};
    sources[0] = 1.0;
    near(*harp.voltage(sources), 0.125, 1e-16, "first group contains four branches");
}
void invalid_input() {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    auto mode = oscillator();
    const auto before = mode.state();
    require(!mode.configure({440.0, 1.0, 0.001}, nan), "reject NaN rate");
    require(!mode.configure({22000.0, 1.0, 0.001}, 48000.0), "reject out-of-domain frequency");
    require(!mode.configure({440.0, 1.0, 0.0}, 48000.0), "reject zero mass");
    require(!mode.set_decay(1e20), "reject non-underdamped coefficient");
    require(!mode.set_state({inf, 0.0}), "reject infinite state");
    require(!mode.add_impulse(nan), "reject NaN impulse");
    near(mode.state().displacement_m, before.displacement_m, 0.0, "rejected primitive updates atomic");
    near(mode.state().velocity_m_per_s, before.velocity_m_per_s, 0.0, "rejected primitive momentum preserved");
    rhodes::PolynomialPickup pickup;
    require(!pickup.configure(1.0, nan, 1.0), "reject invalid pickup coefficient");
    require(!pickup.voltage(nan, 1.0), "reject invalid pickup input");
    require(!pickup.voltage(1e308, 1.0), "report pickup overflow");
    rhodes::SyntheticResistiveHarp harp;
    require(!harp.configure(-1.0, 1.0), "reject negative resistance");
    require(!harp.configure(180.0, 0.0), "reject zero load");
    std::array<double, rhodes::key_count> sources{};
    sources[72] = nan;
    require(!harp.voltage(sources), "reject invalid source even when inactive key");
    rhodes::ResearchEngine engine;
    require(engine.note_on(40, 0.5), "initial strike");
    const auto initial = *engine.modal_state(40, 0);
    require(!engine.prepare(7999.0), "reject sample rate below domain");
    require(!engine.prepare(inf), "reject infinite sample rate");
    require(!engine.note_on(73, 0.5), "reject bad key");
    require(!engine.note_on(40, nan), "reject NaN intensity");
    require(!engine.note_on(40, 1.01), "reject intensity outside domain");
    near(engine.modal_state(40, 0)->velocity_m_per_s, initial.velocity_m_per_s, 0.0, "rejected engine update atomic");
    require(!engine.modal_state(0, 3), "reject bad mode query");
}
void silence() {
    rhodes::ResearchEngine engine;
    std::array<double, 512> block{};
    for (int i = 0; i < 10; ++i) {
        require(engine.process(block), "process startup silence");
        require(std::all_of(block.begin(), block.end(), [](double x) { return x == 0.0; }), "quiet startup");
    }
    require(engine.note_on(40, 0.5), "strike");
    require(engine.process(block), "process note");
    require(std::any_of(block.begin(), block.end(), [](double x) { return x != 0.0; }), "excited signal exists");
    engine.reset();
    require(engine.process(block), "process after reset");
    require(std::all_of(block.begin(), block.end(), [](double x) { return x == 0.0; }), "explicit reset silent");
}
void block_partition() {
    rhodes::ResearchEngine whole, split;
    require(whole.note_on(17, 0.8) && split.note_on(17, 0.8), "same strikes");
    std::array<double, 1024> a{}, b{};
    require(whole.process(a), "process one block");
    constexpr std::array<std::size_t, 5> sizes{1, 7, 64, 131, 821};
    std::size_t offset = 0;
    for (const auto size : sizes) {
        require(split.process(std::span<double>{b.data() + offset, size}), "partitioned process");
        offset += size;
    }
    require(a == b, "same samples across arbitrary block partitions");
    require(whole.note_off(17) && split.note_off(17), "same release timestamp");
    require(whole.process(a), "process released note");
    for (std::size_t i = 0; i < b.size(); ++i) {
        require(split.process(std::span<double>{b.data() + i, 1}), "single-sample blocks");
    }
    require(a == b, "release invariant to block partitions");
}
void pedal_release() {
    rhodes::ResearchEngine sustained, released;
    sustained.set_sustain(true);
    require(sustained.note_on(30, 0.6) && released.note_on(30, 0.6), "strikes");
    require(sustained.note_off(30) && released.note_off(30), "releases");
    std::array<double, 4800> block{};
    require(sustained.process(block), "sustained render");
    require(released.process(block), "damped render");
    require(sustained.mechanical_energy_joules() > 100.0 * released.mechanical_energy_joules(),
            "pedal must preserve surrogate free decay");
    const auto before = *sustained.modal_state(30, 0);
    sustained.set_sustain(false);
    near(sustained.modal_state(30, 0)->displacement_m, before.displacement_m, 0.0, "pedal preserves state");
    const double energy = sustained.mechanical_energy_joules();
    require(sustained.process(block), "render pedal up");
    require(sustained.mechanical_energy_joules() < energy * 0.01, "pedal up applies surrogate loss");
}
void supported_rates() {
    for (const double rate : {32000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        rhodes::ResearchEngine engine;
        require(engine.prepare(rate), "prepare supported rate");
        require(engine.note_on(0, 1.0) && engine.note_on(72, 1.0), "endpoint strikes");
        std::array<double, 1024> output{};
        require(engine.process(output), "endpoint rendering finite");
        require(std::all_of(output.begin(), output.end(), [](double x) { return std::isfinite(x); }),
                "finite samples at supported rates");
    }
}
using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 13> tests{{
    {"exact_solution", exact_solution}, {"energy_loss", energy_loss},
    {"energy_conservation", energy_conservation}, {"retrigger", retrigger},
    {"pickup_derivative", pickup_derivative}, {"pickup_intermodulation", pickup_intermodulation},
    {"network_loading", network_loading}, {"invalid_input", invalid_input},
    {"silence", silence}, {"block_partition", block_partition},
    {"pedal_release", pedal_release}, {"supported_rates", supported_rates},
    {"all", nullptr}
}};
}

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    try {
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (test != nullptr && (selected == "all" || selected == name)) {
                test();
                std::cout << "PASS " << name << '\n';
                found = true;
            }
        }
        require(found, "unknown test name");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
