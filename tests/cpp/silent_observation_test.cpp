#include "rhodes/research_engine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_TEST_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_TEST_MXCSR 0
#endif

#if defined(__SSE2_MATH__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || \
    (defined(__x86_64__) && defined(__SSE2__))
#define RHODES_TEST_SCALAR_SSE2 1
#else
#define RHODES_TEST_SCALAR_SSE2 0
#endif

#if defined(_MSC_VER)
#define RHODES_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define RHODES_TEST_NOINLINE __attribute__((noinline))
#else
#define RHODES_TEST_NOINLINE
#endif

namespace {
using Engine = rhodes::ResearchEngine;
using Plan = rhodes::FractionalObservationPlan;
using Nodes = std::array<double, rhodes::observation_node_count>;
using Keys = std::array<bool, rhodes::key_count>;
using Motion = std::array<std::array<rhodes::ModalState, rhodes::modes_per_key>,
                          rhodes::key_count>;
constexpr Nodes nodes{0.03125, 0.15625, 0.375, 0.625, 0.8125, 0.984375};
constexpr std::array<double, 4> rates{32000.0, 48000.0, 96000.0, 192000.0};
constexpr std::array<double, 3> ratios{1.0, 2.73, 5.41};
constexpr std::array<double, 3> free_loss{0.8, 3.0, 7.0};
constexpr std::array<double, 3> damped_loss{30.0, 35.0, 45.0};
constexpr std::array<double, 3> couplings{1.0, 0.45, 0.2};
constexpr std::uint64_t sign_mask = 1ULL << 63U;
[[maybe_unused]] constexpr unsigned ftz = 1U << 15U;
[[maybe_unused]] constexpr unsigned daz = 1U << 6U;
[[maybe_unused]] constexpr unsigned rounding_mask = 0x6000U;
[[maybe_unused]] constexpr unsigned status_mask = 0x3fU;
std::size_t checks = 0;
std::size_t analytic_observations = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }
void same(double actual, double expected, std::string_view message) {
    require(bits(actual) == bits(expected), message);
}

Motion motion(const Engine& engine) {
    Motion result{};
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto state = engine.modal_state(key, mode);
            require(state.has_value(), "modal state unavailable");
            result[key][mode] = *state;
        }
    }
    return result;
}

void same_motion(const Engine& engine, const Motion& expected) {
    const auto actual = motion(engine);
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            same(actual[key][mode].displacement_m, expected[key][mode].displacement_m,
                 "mechanical displacement bits changed");
            same(actual[key][mode].velocity_m_per_s, expected[key][mode].velocity_m_per_s,
                 "mechanical velocity bits changed");
        }
    }
}

void advance(Engine& engine, std::size_t frames) {
    std::array<double, 127> output{};
    while (frames != 0) {
        const auto count = std::min(frames, output.size());
        require(engine.process(std::span<double>{output}.first(count)), "process failed");
        frames -= count;
    }
}

struct Fixture {
    // The prepared plan is about 84 KiB. Keep engines/plans off the native
    // Windows default stack, including in copy/assignment fixtures.
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    std::unique_ptr<Plan> plan = std::make_unique<Plan>();
    Keys struck{}, held{};
    bool sustain = false;

    explicit Fixture(double rate) {
        require(engine->prepare(rate), "fixture rate rejected");
        require(engine->prepare_observation_plan(nodes, *plan), "fixture nodes rejected");
    }
    void strike(std::size_t key, double intensity) {
        require(engine->note_on(key, intensity), "valid strike rejected");
        held[key] = (bits(intensity) & ~sign_mask) != 0;
        struck[key] = struck[key] || held[key];
    }
    void release(std::size_t key) {
        require(engine->note_off(key), "valid release rejected");
        held[key] = false;
    }
    void pedal(bool down) { engine->set_sustain(down); sustain = down; }
    void all_off() { engine->all_notes_off(); held.fill(false); }
    void reset() {
        engine->reset();
        struck.fill(false);
        held.fill(false);
        sustain = false;
    }
};

struct ExpectedVoltage { long double value = 0.0L, scale = 0.0L; };

// Amplitude-form closed solution, from public boundary states and the declared
// synthetic constants. This does not read the private excitation bit/matrices
// or use observe(), PolynomialPickup, or SyntheticResistiveHarp as its oracle.
// Known event history chooses the keys; small amplitudes are never discarded.
ExpectedVoltage expected_voltage(const Fixture& fixture, double fraction) {
    constexpr std::array<long double, 3> weights{1.0L, 0.3L, 0.08L};
    const long double t = static_cast<long double>(fraction) /
                          fixture.engine->sample_rate_hz();
    ExpectedVoltage result;
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        if (!fixture.struck[key]) { continue; }
        const long double frequency = 440.0L *
            std::exp2((static_cast<long double>(key) - 41.0L) / 12.0L);
        long double x = 0.0L, v = 0.0L, velocity_scale = 0.0L;
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto state = fixture.engine->modal_state(key, mode);
            require(state.has_value(), "reference state unavailable");
            const long double beta = (fixture.held[key] || fixture.sustain)
                ? free_loss[mode] : damped_loss[mode];
            const long double omega = 2.0L * std::numbers::pi_v<long double> *
                                      frequency * ratios[mode];
            const long double wd = std::sqrt(omega * omega - beta * beta);
            const long double a = state->displacement_m;
            const long double b = (state->velocity_m_per_s + beta * a) / wd;
            const long double c = std::cos(wd * t), s = std::sin(wd * t);
            const long double envelope = std::exp(-beta * t);
            x += weights[mode] * envelope * (a * c + b * s);
            const long double first = (b * wd - beta * a) * c;
            const long double second = (a * wd + beta * b) * s;
            v += weights[mode] * envelope * (first - second);
            velocity_scale += weights[mode] * envelope *
                              (std::abs(first) + std::abs(second));
        }
        const long double gradient = 0.4L + 1600.0L * x + 1200000.0L * x * x;
        // Retain the complete four-branch / 23 three-branch source resistance,
        // even for a single moving pickup. The divider reduces to 12000/12019.
        const long double gain = 12000.0L / (12019.0L * (key < 4 ? 4.0L : 3.0L));
        result.value -= gradient * v * gain;
        result.scale += std::abs(gradient) * velocity_scale * gain;
    }
    return result;
}

void check_voltage(const Fixture& fixture) {
    const auto before = motion(*fixture.engine);
    const double energy = fixture.engine->mechanical_energy_joules();
    const auto cell = fixture.engine->observe_cell(*fixture.plan);
    require(cell.has_value(), "valid observation cell rejected");
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        const auto scalar = fixture.engine->observe(*fixture.plan, node);
        require(scalar.has_value(), "valid scalar observation rejected");
        const auto expected = expected_voltage(fixture, nodes[node]);
        // A relative, motion-scaled bound also checks tails far below 1e-13 V;
        // an absolute audio-sized tolerance would let silent retirement pass.
        const long double tolerance = 2.0e-11L * expected.scale +
            64.0L * static_cast<long double>(std::numeric_limits<double>::denorm_min());
        for (const double actual : {*scalar, (*cell)[node]}) {
            require(std::isfinite(actual) &&
                    std::abs(static_cast<long double>(actual) - expected.value) <= tolerance,
                    "observation disagrees with independent loaded voltage");
            ++analytic_observations;
        }
    }
    same_motion(*fixture.engine, before);
    same(fixture.engine->mechanical_energy_joules(), energy,
         "observation changed mechanical energy");
}

void check_copy(const Fixture& fixture) {
    Fixture copied{fixture.engine->sample_rate_hz()};
    copied.engine = std::make_unique<Engine>(*fixture.engine);
    copied.struck = fixture.struck;
    copied.held = fixture.held;
    copied.sustain = fixture.sustain;
    *copied.plan = *fixture.plan;
    check_voltage(copied);
    Fixture assigned{fixture.engine->sample_rate_hz()};
    require(assigned.engine->note_on(1, 0.2), "assignment seed failed");
    *assigned.engine = *fixture.engine;
    assigned.struck = fixture.struck;
    assigned.held = fixture.held;
    assigned.sustain = fixture.sustain;
    *assigned.plan = *fixture.plan;
    check_voltage(assigned);
    std::array<double, 31> a{}, b{};
    require(copied.engine->process(a) && assigned.engine->process(b), "copy continuation failed");
    for (std::size_t i = 0; i < a.size(); ++i) { same(a[i], b[i], "copy/assignment future differs"); }
    same_motion(*copied.engine, motion(*assigned.engine));
    check_voltage(copied);
    check_voltage(assigned);
}

void plan_validation() {
    for (const double rate : rates) {
        Fixture fixture{rate};
        const auto invalid = std::make_unique<Plan>();
        Fixture other{rate == 48000.0 ? 96000.0 : 48000.0};
        fixture.pedal(true); // Even pedal-down silence must validate first.
        advance(*fixture.engine, 19);
        const auto before = motion(*fixture.engine);
        for (const auto* plan : {invalid.get(), other.plan.get()}) {
            require(!fixture.engine->observe_cell(*plan), "silent engine accepted invalid/mismatched cell");
            for (std::size_t node = 0; node < nodes.size(); ++node) {
                require(!fixture.engine->observe(*plan, node),
                        "silent engine accepted invalid/mismatched scalar plan");
            }
        }
        for (const auto node : {nodes.size(), std::numeric_limits<std::size_t>::max()}) {
            require(!fixture.engine->observe(*fixture.plan, node), "silent engine accepted invalid node");
        }
        for (const double bad : {0.0, -0.0, -0.1, 1.0,
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()}) {
            auto fractions = nodes;
            fractions[0] = bad;
            require(!fixture.engine->prepare_observation_plan(fractions, *fixture.plan),
                    "silent engine accepted invalid fractions");
            check_voltage(fixture); // Failed preparation retains the valid plan.
        }
        auto duplicate = nodes;
        duplicate[3] = duplicate[2];
        require(!fixture.engine->prepare_observation_plan(duplicate, *fixture.plan),
                "duplicate silent-plan nodes accepted");
        same_motion(*fixture.engine, before);
        fixture.reset();
        check_voltage(fixture); // Existing plan survives an explicit reset.
        require(fixture.engine->prepare(other.engine->sample_rate_hz()), "reprepare rejected");
        require(!fixture.engine->observe_cell(*fixture.plan) &&
                !fixture.engine->observe(*fixture.plan, 0), "stale plan survived a rate change");
        require(fixture.engine->prepare_observation_plan(nodes, *fixture.plan), "new plan rejected");
        check_voltage(fixture);
    }
}

void network() {
    for (const double rate : rates) {
        for (const std::size_t key : {0U, 3U, 4U, 36U, 72U}) {
            Fixture fixture{rate};
            advance(*fixture.engine, 47);
            fixture.strike(key, 0.73);
            require(std::abs(expected_voltage(fixture, nodes.front()).value) > 1.0e-5L,
                    "single-pickup loading fixture is too small");
            check_voltage(fixture);
            advance(*fixture.engine, 17);
            check_voltage(fixture);
        }
        Fixture chord{rate};
        for (const std::size_t key : {0U, 3U, 4U, 5U, 36U, 37U, 71U, 72U}) {
            chord.strike(key, 0.2 + 0.01 * static_cast<double>(key));
        }
        check_voltage(chord);
        advance(*chord.engine, 97);
        check_voltage(chord);
    }
}

void lifecycle() {
    for (const double rate : rates) {
        Fixture fixture{rate};
        fixture.pedal(true);
        advance(*fixture.engine, 31);
        fixture.strike(72, 0.71); // First strike follows silent modal advancement.
        advance(*fixture.engine, 83);
        check_voltage(fixture);
        check_copy(fixture);
        auto before = motion(*fixture.engine);
        fixture.release(72);
        same_motion(*fixture.engine, before);
        check_voltage(fixture);
        fixture.all_off();
        advance(*fixture.engine, 211);
        check_voltage(fixture); // Unheld, sustained motion is still observable.
        before = motion(*fixture.engine);
        fixture.pedal(false);
        same_motion(*fixture.engine, before);
        advance(*fixture.engine, 13);
        check_voltage(fixture); // Released, damped motion must also remain.

        before = motion(*fixture.engine);
        fixture.strike(72, 0.43);
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto state = fixture.engine->modal_state(72, mode);
            require(state.has_value(), "retrigger state unavailable");
            same(state->displacement_m, before[72][mode].displacement_m,
                 "retrigger reset moving displacement");
            require(std::abs(state->velocity_m_per_s - before[72][mode].velocity_m_per_s -
                             0.06 * 0.43 * couplings[mode]) < 1.0e-16,
                    "retrigger discarded velocity instead of adding impulse");
        }
        check_voltage(fixture);
        for (const double zero : {0.0, -0.0}) {
            before = motion(*fixture.engine);
            fixture.strike(72, zero);
            same_motion(*fixture.engine, before);
            check_voltage(fixture);
            advance(*fixture.engine, 17);
        }
        check_copy(fixture);

        if (rate == rates.front()) {
            advance(*fixture.engine, 32000);
            const auto tail = expected_voltage(fixture, nodes.front());
            require(std::abs(tail.value) > 1.0e-20L && std::abs(tail.value) < 1.0e-13L,
                    "long released tail does not exercise small surviving motion");
            check_voltage(fixture);
        }

        fixture.reset();
        same_motion(*fixture.engine, Motion{});
        check_voltage(fixture);
        fixture.strike(4, 1.0e-200);
        advance(*fixture.engine, 19);
        fixture.release(4);
        check_voltage(fixture); // Normal but extremely quiet strike, no threshold.
        require(std::abs(expected_voltage(fixture, nodes[2]).value) > 1.0e-205L,
                "tiny-strike fixture is zero");
        require(fixture.engine->prepare(rate), "same-rate reprepare failed");
        fixture.struck.fill(false);
        fixture.held.fill(false);
        fixture.sustain = false;
        same_motion(*fixture.engine, Motion{});
        check_voltage(fixture);
        advance(*fixture.engine, 29);
        fixture.strike(0, 0.5);
        check_voltage(fixture);
    }
}

void transactions() {
    for (const bool ringing : {false, true}) {
        Fixture fixture{48000.0};
        if (ringing) {
            fixture.strike(3, 0.8);
            fixture.strike(72, 0.61);
            advance(*fixture.engine, 41);
            fixture.release(3);
            fixture.pedal(true);
        }
        const auto before = motion(*fixture.engine);
        const double energy = fixture.engine->mechanical_energy_joules();
        const auto untouched = std::make_unique<Engine>(*fixture.engine);
        for (const double intensity : {-1.0, -std::numeric_limits<double>::denorm_min(),
                std::nextafter(1.0, 2.0), std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()}) {
            for (const std::size_t key : {3U, 36U, 72U}) {
                require(!fixture.engine->note_on(key, intensity), "invalid intensity accepted");
            }
        }
        for (const auto key : {rhodes::key_count, std::numeric_limits<std::size_t>::max()}) {
            require(!fixture.engine->note_on(key, 0.7) && !fixture.engine->note_off(key),
                    "invalid key transaction accepted");
        }
        for (const double rate : {0.0, 31999.0, 192001.0,
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()}) {
            require(!fixture.engine->prepare(rate), "invalid prepare accepted");
        }
        require(fixture.engine->process({}), "empty process rejected");
        same_motion(*fixture.engine, before);
        same(fixture.engine->mechanical_energy_joules(), energy, "rejection changed energy");
        same(fixture.engine->sample_rate_hz(), 48000.0, "rejection changed rate");
        check_voltage(fixture);
        std::array<double, 23> actual{}, expected{};
        require(fixture.engine->process(actual) && untouched->process(expected),
                "rejection continuation failed");
        for (std::size_t i = 0; i < actual.size(); ++i) {
            same(actual[i], expected[i], "rejected transaction changed future damping/output");
        }
        same_motion(*fixture.engine, motion(*untouched));
        fixture.strike(36, 0.53); // Previously rejected notes must not damage first strike.
        check_voltage(fixture);
    }
}

// Only addition needs rounding-sensitive execution for the zero oracle. Bit
// manipulation gives exact signs of products with finite nonzero coefficients.
RHODES_TEST_NOINLINE double runtime_add(double left, double right) {
    volatile double a = left, b = right;
    return a + b;
}

double signed_product(double zero, bool negative) {
    require((bits(zero) & ~sign_mask) == 0, "zero oracle received moving state");
    return std::bit_cast<double>(bits(zero) ^ (negative ? sign_mask : 0ULL));
}

struct RestoreEnvironment {
    std::fenv_t saved{};
#if RHODES_TEST_MXCSR
    unsigned saved_mxcsr = _mm_getcsr();
#endif
    RestoreEnvironment() { require(std::fegetenv(&saved) == 0, "cannot save fenv"); }
    ~RestoreEnvironment() {
        (void)std::fesetenv(&saved);
#if RHODES_TEST_MXCSR
        _mm_setcsr(saved_mxcsr);
#endif
    }
};

#if RHODES_TEST_MXCSR
[[maybe_unused]] unsigned supported_controls() {
    alignas(16) std::array<unsigned char, 512> saved{};
#if defined(_MSC_VER)
    _fxsave(saved.data());
#else
    asm volatile("fxsave %0" : "=m"(saved));
#endif
    std::uint32_t mask = 0;
    std::memcpy(&mask, saved.data() + 28, sizeof(mask));
    // Architectural fallback when the FXSAVE mask field reports zero.
    return mask == 0 ? 0xffbfU : mask;
}
#endif

struct ZeroFixture {
    Fixture fixture;
    using Bank = std::array<std::array<rhodes::ModalOscillator, rhodes::modes_per_key>,
                            rhodes::key_count>;
    std::unique_ptr<Bank> independent_modes = std::make_unique<Bank>();
    using Signs = std::array<std::array<std::array<std::array<bool,
        rhodes::observation_node_count>, rhodes::modes_per_key>, 2>, rhodes::key_count>;
    Signs negative_velocity_diagonal{};

    explicit ZeroFixture(double rate) : fixture{rate} {
        for (std::size_t key = 0; key < rhodes::key_count; ++key) {
            const double frequency = 440.0 *
                std::exp2((static_cast<double>(key) - 41.0) / 12.0);
            for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
                auto& oscillator = (*independent_modes)[key][mode];
                require(oscillator.configure({frequency * ratios[mode], damped_loss[mode],
                                             0.001}, rate) &&
                        oscillator.set_decay(free_loss[mode]) &&
                        oscillator.set_decay(damped_loss[mode]), "zero-mode fixture rejected");
                for (std::size_t regime = 0; regime < 2; ++regime) {
                    const long double beta = regime == 0 ? damped_loss[mode] : free_loss[mode];
                    const long double omega = 2.0L * std::numbers::pi_v<long double> *
                                              frequency * ratios[mode];
                    const long double wd = std::sqrt(omega * omega - beta * beta);
                    for (std::size_t node = 0; node < nodes.size(); ++node) {
                        const long double angle = wd * nodes[node] / rate;
                        // All fixture phases lie strictly in (0, pi), making
                        // the velocity/displacement coefficient negative.
                        require(angle > 0.0L && angle < std::numbers::pi_v<long double>,
                                "zero oracle phase left its declared interval");
                        const long double diagonal = std::cos(angle) - beta / wd * std::sin(angle);
                        require(std::abs(diagonal) > 1.0e-5L,
                                "zero oracle coefficient too close to a sign boundary");
                        negative_velocity_diagonal[key][regime][mode][node] = diagonal < 0.0L;
                    }
                }
            }
        }
    }

    void reset() {
        fixture.reset();
        for (auto& key : *independent_modes) {
            for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
                key[mode].reset();
                require(key[mode].set_decay(damped_loss[mode]), "reference reset loss failed");
            }
        }
    }
    void pedal(bool down) {
        fixture.pedal(down);
        for (auto& key : *independent_modes) {
            for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
                require(key[mode].set_decay(down ? free_loss[mode] : damped_loss[mode]),
                        "reference loss switch failed");
            }
        }
    }
    void step() {
        advance(*fixture.engine, 1);
        for (std::size_t key = 0; key < rhodes::key_count; ++key) {
            for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
                auto& reference = (*independent_modes)[key][mode];
                require(reference.advance(), "reference zero mode failed");
                const auto actual = fixture.engine->modal_state(key, mode);
                require(actual.has_value(), "zero state unavailable");
                same(actual->displacement_m, reference.state().displacement_m,
                     "silent modal advancement changed displacement sign");
                same(actual->velocity_m_per_s, reference.state().velocity_m_per_s,
                     "silent modal advancement changed velocity sign");
            }
        }
    }

    double expected_zero(std::size_t node) const {
        double network_zero = 0.0;
        const std::size_t regime = fixture.sustain ? 1U : 0U;
        for (std::size_t key = 0; key < rhodes::key_count; ++key) {
            double velocity_zero = 0.0;
            for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
                const auto state = (*independent_modes)[key][mode].state();
                const double v = runtime_add(signed_product(state.displacement_m, true),
                    signed_product(state.velocity_m_per_s,
                        negative_velocity_diagonal[key][regime][mode][node]));
                // All observation weights are positive; multiplication
                // preserves each zero sign. The zero-displacement pickup
                // gradient is positive, so its voltage is precisely -v.
                velocity_zero = runtime_add(velocity_zero, v);
            }
            // Positive parallel-group divisors and load gain also preserve
            // zero signs. Retain all 73 ordered network additions.
            network_zero = runtime_add(network_zero, signed_product(velocity_zero, true));
        }
        return network_zero;
    }

    void check() const {
        const auto before = motion(*fixture.engine);
        const int rounding = std::fegetround();
#if RHODES_TEST_MXCSR
        const unsigned controls = _mm_getcsr() & ~status_mask;
#endif
        const auto cell = fixture.engine->observe_cell(*fixture.plan);
        require(cell.has_value(), "silent FP cell rejected");
        for (std::size_t node = 0; node < nodes.size(); ++node) {
            const auto scalar = fixture.engine->observe(*fixture.plan, node);
            require(scalar.has_value(), "silent FP scalar rejected");
            const double expected = expected_zero(node);
            same(*scalar, expected, "scalar signed zero differs from independent arithmetic");
            same((*cell)[node], expected, "batched signed zero differs from independent arithmetic");
        }
        same_motion(*fixture.engine, before);
        require(std::fegetround() == rounding, "observation changed fenv rounding");
#if RHODES_TEST_MXCSR
        require((_mm_getcsr() & ~status_mask) == controls, "observation changed MXCSR controls");
#endif
    }
};

struct Rounding { int value; const char* name; unsigned mxcsr; };
[[maybe_unused]] constexpr Rounding rounding_modes[]{
#if defined(FE_TONEAREST)
    {FE_TONEAREST, "nearest", 0U},
#endif
#if defined(FE_DOWNWARD)
    {FE_DOWNWARD, "downward", 0x2000U},
#endif
#if defined(FE_UPWARD)
    {FE_UPWARD, "upward", 0x4000U},
#endif
#if defined(FE_TOWARDZERO)
    {FE_TOWARDZERO, "toward-zero", 0x6000U},
#endif
};

[[maybe_unused]] bool zero_rounding_available(unsigned expected) {
    return std::signbit(runtime_add(0.0, -0.0)) == (expected == 0x2000U);
}

[[maybe_unused]] void exercise_zero_fixture(ZeroFixture& zero) {
    zero.reset();
    zero.check();
    zero.step();
    zero.check();
    zero.step();
    zero.pedal(true);
    zero.check();
    zero.step();
    zero.step();
    zero.pedal(false);
    zero.check();
    zero.step();
    zero.check();
}

bool fp_environment() {
#if defined(__FAST_MATH__) || (defined(_M_FP_FAST) && _M_FP_FAST)
    std::cout << "SKIP fp_environment: compiler fast-math does not preserve signed zero\n";
    return false;
#else
    RestoreEnvironment restore;
    require(std::fesetround(FE_TONEAREST) == 0, "nearest fixture preparation unavailable");
#if RHODES_TEST_MXCSR
    _mm_setcsr((_mm_getcsr() & ~(rounding_mask | ftz | daz | status_mask)) | 0x1f80U);
#endif
    std::array<std::unique_ptr<ZeroFixture>, rates.size()> fixtures;
    for (std::size_t i = 0; i < rates.size(); ++i) {
        fixtures[i] = std::make_unique<ZeroFixture>(rates[i]);
    }
    std::size_t environments = 0;
    for (const auto& fenv_mode : rounding_modes) {
        if (std::fesetround(fenv_mode.value) != 0) {
            std::cout << "SKIP fenv " << fenv_mode.name << ": rounding mode unavailable\n";
            continue;
        }
#if RHODES_TEST_MXCSR && RHODES_TEST_SCALAR_SSE2
        for (const auto& sse_mode : rounding_modes) {
            for (const unsigned flush : {0U, ftz, daz, ftz | daz}) {
                const unsigned requested = sse_mode.mxcsr | flush | 0x1f80U;
                if ((requested & ~supported_controls()) != 0U) {
                    std::cout << "SKIP MXCSR " << sse_mode.name << " flush=" << flush
                              << ": requested controls unavailable\n";
                    continue;
                }
                _mm_setcsr(requested);
                // Libraries differ in whether fegetround() reads x87, SSE,
                // or reports ambiguity after independent register changes.
                // Observe the actual scalar arithmetic and check each call
                // preserves the resulting controls; do not assume a reader.
                require(_mm_getcsr() == requested, "MXCSR request did not take effect");
                if (!zero_rounding_available(sse_mode.mxcsr)) {
                    std::cout << "SKIP fenv " << fenv_mode.name << "/MXCSR " << sse_mode.name
                              << ": compiler scalar arithmetic does not honor requested zero sign\n";
                    continue;
                }
                for (auto& fixture : fixtures) { exercise_zero_fixture(*fixture); }
                ++environments;
            }
        }
#else
        if (!zero_rounding_available(fenv_mode.mxcsr)) {
            std::cout << "SKIP fenv " << fenv_mode.name
                      << ": compiler scalar arithmetic does not honor requested zero sign\n";
            continue;
        }
        for (auto& fixture : fixtures) { exercise_zero_fixture(*fixture); }
        ++environments;
#endif
    }
#if !RHODES_TEST_MXCSR
    std::cout << "SKIP independent MXCSR/flush matrix: non-x86 target\n";
#elif !RHODES_TEST_SCALAR_SSE2
    std::cout << "SKIP independent MXCSR/flush matrix: compiler scalar binary64 SSE2 domain unavailable\n";
#endif
    std::cout << environments << " runtime rounding/flush environments, "
              << environments * fixtures.size() << " rate scenarios\n";
    return environments != 0;
#endif
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 4> tests{{
    {"plan_validation", plan_validation}, {"network", network},
    {"lifecycle", lifecycle}, {"transactions", transactions}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    bool found = false, ran = false;
    try {
        for (const auto& [name, test] : tests) {
            if (selected != "all" && selected != name) { continue; }
            found = ran = true;
            checks = analytic_observations = 0;
            test();
            std::cout << "PASS " << name << " (" << checks << " always-active checks, "
                      << analytic_observations << " independent voltage observations)\n";
        }
        if (selected == "all" || selected == "fp_environment") {
            found = true;
            checks = analytic_observations = 0;
            if (fp_environment()) {
                ran = true;
                std::cout << "PASS fp_environment (" << checks << " always-active checks)\n";
            }
        }
        require(found, "unknown silent-observation selector");
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
    return ran ? 0 : 77;
}
