#include "rhodes/contact_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Synthetic SI fixtures. The independent root check bisects the Cartesian
// harmonic velocity, rather than using the production phase/period formula.
// Every assertion remains active under NDEBUG.
namespace {
namespace c = rhodes::contact;
std::size_t checks = 0;
constexpr std::array<double, 5> steps{20e-6, 10e-6, 5e-6, 2.5e-6, 1.25e-6};

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
void success(c::Result result, std::string_view message) {
    require(static_cast<bool>(result), std::string{message} + " failure=" +
            std::to_string(static_cast<int>(result.failure)));
}
c::Settings settings(double step = steps.front()) {
    c::Settings result;
    result.maximum_step_s = step;
    result.event_time_tolerance_s = 2.5e-9;
    return result;
}

struct Fixture {
    c::Configuration config{};
    c::State initial{};
    std::size_t mode = 0;
    double first_elapsed = 0.0;
    double omega = 0.0;

    Fixture(double frequency, double projection, double origin, double first,
            std::size_t selected_mode = 0, bool fixed_other_mode = false)
        : mode(selected_mode), first_elapsed(first),
          omega(2.0 * std::numbers::pi * frequency) {
        config.modes = {{{0.002, 701.0, 0.0}, {0.001, 701.0, 0.0}}};
        config.modes[mode].frequency_hz = frequency;
        config.hammer_projection = {};
        config.hammer_projection[mode] = projection;
        initial.time_s = origin;
        constexpr double amplitude = 1e-4;
        const double signed_amplitude = std::copysign(amplitude, projection);
        initial.q_m[mode] = -signed_amplitude * std::cos(omega * first);
        initial.v_m_per_s[mode] = -signed_amplitude * omega * std::sin(omega * first);
        // Choose a representable clearance on the nonpositive side of the
        // harmonic envelope, with the same public-precision radius. This never
        // asks production to round a positive envelope down to a tangent.
        long double radius = 0.0L;
        bool rounded_outward = false;
        for (int attempt = 0; attempt < 32; ++attempt) {
            radius = std::hypot(static_cast<long double>(initial.q_m[mode]),
                static_cast<long double>(initial.v_m_per_s[mode]) / omega);
            if (static_cast<long double>(static_cast<double>(radius)) >= radius) {
                rounded_outward = true;
                break;
            }
            initial.q_m[mode] = std::nextafter(initial.q_m[mode], 0.0);
        }
        require(rounded_outward, "construct nonpositive tangent envelope");
        initial.hammer_x_m = -std::abs(projection) * static_cast<double>(radius);
        const std::size_t other = 1 - mode;
        if (fixed_other_mode) {
            // An exact binary static displacement exercises the constant term
            // in the certificate without adding a second projected oscillator.
            require(first == 0.0, "fixed-mode fixture starts at an exact maximum");
            initial.q_m[mode] = -std::copysign(std::ldexp(1.0, -14), projection);
            initial.v_m_per_s[mode] = 0.0;
            config.modes[other].frequency_hz = 0.0;
            config.hammer_projection[other] = 0.5;
            initial.q_m[other] = std::ldexp(1.0, -16);
            initial.hammer_x_m = config.hammer_projection[other] * initial.q_m[other] -
                std::abs(projection * initial.q_m[mode]);
        } else {
            // Spectator motion is unprojected at the hammer and remains free.
            initial.q_m[other] = 0.000025;
            initial.v_m_per_s[other] = 0.03;
        }
    }

    long double period() const {
        return 2.0L * std::numbers::pi_v<long double> / omega;
    }

    long double independent_maximum(std::size_t index) const {
        if (first_elapsed == 0.0 && index == 0) { return initial.time_s; }
        const long double predicted = first_elapsed + static_cast<long double>(index) * period();
        long double lo = std::max(0.0L, predicted - period() / 8.0L);
        long double hi = predicted + period() / 8.0L;
        const auto rate = [this](long double elapsed) {
            const long double angle = static_cast<long double>(omega) * elapsed;
            return static_cast<long double>(config.hammer_projection[mode]) *
                (static_cast<long double>(omega) * initial.q_m[mode] * std::sin(angle) -
                 static_cast<long double>(initial.v_m_per_s[mode]) * std::cos(angle));
        };
        require(rate(lo) > 0.0L && rate(hi) < 0.0L, "independent maximum sign bracket");
        for (int iteration = 0; iteration < 128; ++iteration) {
            const long double mid = lo + (hi - lo) / 2.0L;
            if (!(mid > lo && mid < hi)) { break; }
            if (rate(mid) > 0.0L) { lo = mid; }
            else { hi = mid; }
        }
        return static_cast<long double>(initial.time_s) + lo + (hi - lo) / 2.0L;
    }

    double finish() const {
        return initial.time_s + first_elapsed + static_cast<double>(2.25L * period());
    }
};

void force_free(const c::Reference& reference) {
    const auto& observation = reference.observation();
    const auto& ledger = reference.ledger();
    require(observation.hammer.force_n == 0.0 && observation.damper.force_n == 0.0,
            "certified motion has exact zero contact forces");
    require(ledger.hammer_impulse_ns == 0.0 && ledger.damper_impulse_ns == 0.0 &&
            ledger.total_loss_j() == 0.0 && ledger.total_work_j() == 0.0,
            "certified motion has exact zero impulses, losses and work");
    require(reference.diagnostics().sampled_maximum_hammer_force_n == 0.0 &&
            reference.diagnostics().sampled_maximum_damper_force_n == 0.0,
            "no sampled nonzero force at a certified tangent");
}

void check_events(const c::Reference& reference, const Fixture& fixture, std::size_t count = 3) {
    require(reference.events().size() == count, "exact tangent event count");
    require(reference.diagnostics().work.analytic_free_evaluations > 0,
            "test exercised the force-free certificate");
    for (std::size_t i = 0; i < count; ++i) {
        const auto& event = reference.events()[i];
        const long double expected = fixture.independent_maximum(i);
        require(event.contact == c::ContactId::hammer && event.kind == c::EventKind::grazing,
                "every retained root is a hammer tangent");
        require(static_cast<long double>(event.bracket_begin_s) <= expected &&
                expected <= static_cast<long double>(event.bracket_end_s),
                "outward bracket encloses independent Cartesian harmonic maximum");
        require(event.bracket_begin_s <= event.time_s && event.time_s <= event.bracket_end_s &&
                event.bracket_end_s - event.bracket_begin_s <=
                    reference.settings().event_time_tolerance_s,
                "event retains the unchanged time-bracket limit");
        require(event.bracket_gap_uncertainty_m <= reference.settings().event_gap_tolerance_m &&
                std::abs(event.gap_m) <= event.bracket_gap_uncertainty_m,
                "event retains the unchanged gap-bracket limit");
        if (!(fixture.first_elapsed == 0.0 && i == 0)) {
            require(event.before == c::Branch::separated && event.after == c::Branch::separated,
                    "tangent does not invent an active force branch");
            require(event.bracket_begin_s < event.bracket_end_s,
                    "noninitial tangent retains an outward enclosure");
        }
        if (i != 0) {
            require(reference.events()[i - 1].bracket_end_s < event.bracket_begin_s,
                    "successive periods have ordered disjoint enclosures");
        }
    }
    force_free(reference);
}

c::Reference run(const Fixture& fixture, double step) {
    c::Reference reference;
    success(reference.initialize(fixture.config, fixture.initial, settings(step)), "initialize tangent");
    success(reference.advance(fixture.finish() - reference.state().time_s), "advance multiple periods");
    check_events(reference, fixture);
    return reference;
}

void anchor_steps() {
    Fixture fixture{200.0, 1.0, 0.0, 0.0010073};
    // Freeze the original CLI fixture exactly, including its original clearance.
    fixture.initial.q_m = {-1e-4 * std::cos(fixture.omega * fixture.first_elapsed), 0.0};
    fixture.initial.v_m_per_s = {-1e-4 * fixture.omega * std::sin(fixture.omega * fixture.first_elapsed), 0.0};
    fixture.initial.hammer_x_m = -1e-4;
    std::vector<c::Event> expected;
    for (double step : steps) {
        const auto reference = run(fixture, step);
        if (expected.empty()) { expected = reference.events(); }
        else {
            require(reference.events() == expected,
                    "all five frozen step levels produce identical complete event rows");
        }
        require(std::abs(reference.events().front().time_s - 0.0010073) < 1e-17,
                "original tangent timestamp no longer follows nanosecond bisection steps");
    }
}

void anchor_geometry() {
    const std::array<Fixture, 4> fixtures{{
        {1123.0, -0.5, 0.375, 0.23 / 1123.0, 0},
        {87.5, 2.0, 31.25, 0.61 / 87.5, 1},
        {200.0, -2.0, 0.125, 0.37 / 200.0, 1},
        {1123.0, 2.0, 0.5, 0.0, 1, true}
    }};
    for (const auto& fixture : fixtures) {
        std::vector<c::Event> expected;
        for (double step : steps) {
            const auto reference = run(fixture, step);
            if (expected.empty()) { expected = reference.events(); }
            else {
                require(reference.events() == expected,
                        "signed projection, mode and origin do not reintroduce step dependence");
            }
        }
    }
}

void anchor_partitions() {
    Fixture fixture{1123.0, -0.5, 0.375, 0.23 / 1123.0, 1};
    for (double step : steps) {
        const auto whole = run(fixture, step);
        c::Reference divided;
        success(divided.initialize(fixture.config, fixture.initial, settings(step)), "partition initialize");
        const double first = whole.events()[0].time_s;
        const double second = whole.events()[1].time_s;
        const double third = whole.events()[2].time_s;
        for (double target : {first - 1e-12, first + 1e-12, second, second + 1e-8,
                              third - 1e-12, third + 1e-12, fixture.finish()}) {
            success(divided.advance(target - divided.state().time_s), "partition around tangent");
            force_free(divided);
        }
        check_events(divided, fixture);
        require(divided.events() == whole.events(),
                "caller partitions around and at roots preserve identical event rows");
        require(divided.state() == whole.state() && divided.observation() == whole.observation(),
                "analytic endpoint state remains identical after caller partitions");
    }
}

void anchor_boundaries() {
    const std::array<Fixture, 3> fixtures{{
        {200.0, 1.0, 0.0, 0.0010073},
        {1123.0, -0.5, 0.375, 0.23 / 1123.0, 1},
        {87.5, 2.0, 31.25, 0.61 / 87.5, 1}
    }};
    const double infinity = std::numeric_limits<double>::infinity();
    for (const auto& fixture : fixtures) {
        for (double step : steps) {
            const auto whole = run(fixture, step);
            const double center = whole.events().front().time_s;
            for (double target : {std::nextafter(center, -infinity), center,
                                  std::nextafter(center, infinity)}) {
                // Independent calls are necessary: public advance increments
                // are at least 1e-12 s, while these endpoints differ by one ulp.
                c::Reference reference;
                success(reference.initialize(fixture.config, fixture.initial, settings(step)),
                        "endpoint initialize");
                success(reference.advance(target - reference.state().time_s), "advance to root endpoint");
                require(reference.state().time_s == target, "requested endpoint retained exactly");
                require(reference.events().size() == (target < center ? 0U : 1U),
                        "bracket overlap alone must not emit a future timestamp");
                for (const auto& event : reference.events()) {
                    require(event.time_s <= reference.state().time_s && event == whole.events().front(),
                            "endpoint event is canonical and never in the future");
                }
                force_free(reference);
                success(reference.advance(fixture.finish() - reference.state().time_s),
                        "continue across root endpoint");
                check_events(reference, fixture);
                require(reference.events() == whole.events(),
                        "adjacent caller intervals neither lose nor duplicate endpoint events");
            }
        }
    }
}

void same(const c::Reference& actual, const c::Reference& expected) {
    require(actual.initialized() == expected.initialized() && actual.config() == expected.config() &&
            actual.settings() == expected.settings() && actual.state() == expected.state() &&
            actual.observation() == expected.observation() && actual.ledger() == expected.ledger() &&
            actual.diagnostics() == expected.diagnostics() && actual.events() == expected.events() &&
            actual.intervals() == expected.intervals(),
            "failure preserves all public state, diagnostics and histories");
}

void anchor_rollback() {
    Fixture fixture{200.0, 1.0, 0.375, 0.0010073};
    const double seed_time = fixture.initial.time_s + fixture.first_elapsed - 30e-6;
    const auto seed = [&](c::Reference& reference, const c::Settings& options) {
        success(reference.initialize(fixture.config, fixture.initial, options), "rollback initialize");
        success(reference.advance(seed_time - reference.state().time_s), "rollback seed history");
        require(!reference.intervals().empty() && reference.events().empty(), "nonempty pre-root history");
    };
    c::Reference completed;
    seed(completed, settings());
    const std::size_t prior_intervals = completed.intervals().size();
    const double duration = static_cast<double>(fixture.period()) + 60e-6;
    const auto crossing = completed.advance(duration);
    success(crossing, "unlimited two-root crossing");
    require(completed.events().size() == 2 && crossing.attempted.root_iterations >= 2,
            "budget fixture must locate two analytic maxima");
    for (const auto failure : {c::Failure::trial_limit, c::Failure::evaluation_limit,
                              c::Failure::event_limit, c::Failure::history_limit}) {
        auto options = settings();
        if (failure == c::Failure::trial_limit) {
            options.maximum_trials = crossing.attempted.trials - 1;
        } else if (failure == c::Failure::evaluation_limit) {
            options.maximum_rhs_evaluations = crossing.attempted.rhs_evaluations - 1;
        } else if (failure == c::Failure::event_limit) {
            options.maximum_event_rows = 1;
        } else {
            options.maximum_interval_rows = prior_intervals + crossing.attempted.accepted_steps - 1;
        }
        c::Reference reference;
        seed(reference, options);
        auto control = reference;
        const auto rejected = reference.advance(duration);
        require(rejected.status == c::Status::budget_exhausted && rejected.failure == failure,
                "analytic event call returns its exact exhausted budget");
        require(rejected.attempted.accepted_steps > 0 && rejected.attempted.root_iterations > 0,
                "failure occurs after temporary progress and analytic root work");
        require(rejected.attempted.trials <= options.maximum_trials &&
                rejected.attempted.rhs_evaluations <= options.maximum_rhs_evaluations,
                "analytic localization preserves common work caps");
        same(reference, control);
        success(reference.advance(60e-6), "continue after rejected root call");
        success(control.advance(60e-6), "unfailed control continuation");
        same(reference, control);
        require(reference.events().size() == 1, "successful continuation retains exactly one maximum");
        force_free(reference);
    }

    // Exhaust RHS while the narrow analytic enclosure itself is being replayed,
    // with a prior accepted interval. Derive the cap from observed work instead
    // of depending on the number of platform-specific bracket expansions.
    Fixture close{200.0, -0.5, 0.125, 42.7e-6};
    const auto short_seed = [&](c::Reference& reference, const c::Settings& options) {
        success(reference.initialize(close.config, close.initial, options), "close-root initialize");
        for (double dt : {2e-6, 4e-6, 8e-6, 16e-6}) {
            success(reference.advance(dt), "close-root seed and controller growth");
        }
    };
    c::Reference full;
    short_seed(full, settings());
    const auto single = full.advance(20e-6);
    success(single, "single-interval analytic root");
    require(single.attempted.accepted_steps == 1 && single.attempted.root_iterations > 0,
            "close fixture localizes a root in one accepted interval");
    auto limited = settings();
    limited.maximum_rhs_evaluations = single.attempted.rhs_evaluations - 1;
    c::Reference reference;
    short_seed(reference, limited);
    auto control = reference;
    const auto rejected = reference.advance(20e-6);
    require(rejected.status == c::Status::budget_exhausted &&
            rejected.failure == c::Failure::evaluation_limit &&
            rejected.attempted.root_iterations > 0 && rejected.attempted.accepted_steps == 0 &&
            rejected.attempted.rhs_evaluations == limited.maximum_rhs_evaluations,
            "analytic bracket replay obeys the exact RHS cap before interval commit");
    same(reference, control);
    success(reference.advance(5e-6), "short continuation after enclosure rollback");
    success(control.advance(5e-6), "control after enclosure rollback");
    same(reference, control);
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 5> tests{{
    {"anchor_steps", anchor_steps}, {"anchor_geometry", anchor_geometry},
    {"anchor_partitions", anchor_partitions}, {"anchor_boundaries", anchor_boundaries},
    {"anchor_rollback", anchor_rollback}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    bool found = false;
    bool passed = true;
    for (const auto& [name, test] : tests) {
        if (selected != "all" && selected != name) { continue; }
        found = true;
        checks = 0;
        try {
            test();
            std::cout << "PASS " << name << " checks=" << checks << '\n';
        } catch (const std::exception& error) {
            passed = false;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return found && passed && argc <= 2 ? 0 : 1;
}
