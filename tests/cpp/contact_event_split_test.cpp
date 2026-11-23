#include "rhodes/contact_reference.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

namespace c = rhodes::contact;
namespace {
int failures = 0;
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; ++failures; }
}
bool same(const c::Reference& a, const c::Reference& b) {
    return a.initialized() == b.initialized() && a.config() == b.config() &&
        a.settings() == b.settings() && a.state() == b.state() &&
        a.observation() == b.observation() && a.ledger() == b.ledger() &&
        a.diagnostics() == b.diagnostics() && a.events() == b.events() &&
        a.intervals() == b.intervals();
}
c::Configuration simultaneous_config() {
    c::Configuration cfg;
    cfg.modes = {c::Mode{1, 0, 0}, c::Mode{.5, 0, 0}};
    cfg.hammer_mass_kg = 1;
    cfg.hammer_projection = {1, 0}; cfg.damper_projection = {0, 1};
    cfg.hammer = {true, 20000, 0, 1}; cfg.damper = {true, 20000, 0, 1};
    return cfg;
}
c::State simultaneous_state() {
    c::State s; s.v_m_per_s = {-.5, 1}; s.hammer_v_m_per_s = .5; return s;
}
c::Settings options() {
    c::Settings s; s.maximum_step_s = 2e-4; s.event_time_tolerance_s = 2.5e-9;
    return s;
}
void simultaneous_landing() {
    c::Reference r;
    require(bool(r.initialize(simultaneous_config(), simultaneous_state(), options())), "initialize simultaneous contacts");
    require(bool(r.advance(.016)), "advance simultaneous contacts");
    require(r.events().size() == 4, "exactly two onsets and two separations");
    if (r.events().size() != 4) { return; }
    const auto& h = r.events()[2]; const auto& d = r.events()[3];
    require(h.contact == c::ContactId::hammer && d.contact == c::ContactId::damper &&
        h.kind == c::EventKind::separation && d.kind == c::EventKind::separation,
        "deterministic simultaneous contact order");
    require(h.time_s == d.time_s && h.bracket_begin_s == d.bracket_begin_s &&
        h.bracket_end_s == d.bracket_end_s, "symmetric contacts retain identical brackets");
    require(std::abs(h.time_s - std::numbers::pi / 200) <= options().event_time_tolerance_s,
        "simultaneous event agrees with analytic relative oscillators");
    double distance = 1;
    for (const auto& i : r.intervals()) { distance = std::min(distance, std::abs(i.end_s - h.time_s)); }
    require(distance <= options().event_time_tolerance_s, "separation lands at an accepted endpoint within event resolution");
}
c::Configuration clamp_config() {
    auto cfg = simultaneous_config(); cfg.hammer.enabled = false;
    cfg.damper_projection = {0, 0}; cfg.damper = {true, 1000, 2000, 1};
    cfg.damper_motion.kind = c::TrajectoryKind::sinusoidal;
    cfg.damper_motion.offset_m = -.01; cfg.damper_motion.amplitude_m = .001;
    cfg.damper_motion.angular_frequency_rad_per_s = 1000;
    return cfg;
}
void clamp_crossings() {
    c::Reference r;
    require(bool(r.initialize(clamp_config(), {}, options())), "initialize prescribed clamp fixture");
    require(bool(r.advance(.0065)), "advance prescribed clamp fixture");
    require(r.events().size() == 2, "exactly one clamp entry and exit");
    if (r.events().size() != 2) { return; }
    require(r.events()[0].kind == c::EventKind::clamp_enter && r.events()[1].kind == c::EventKind::clamp_exit,
        "active to clipped to active sequence");
    require(std::abs(r.events()[0].time_s - 7 * std::numbers::pi / 6000) <= options().event_time_tolerance_s &&
        std::abs(r.events()[1].time_s - 11 * std::numbers::pi / 6000) <= options().event_time_tolerance_s,
        "clamp event times match prescribed analytic factor zeros");
    require(r.events()[0].gap_m > .009 && r.events()[1].gap_m > .009, "clamp roots remain compressed");
}
void rollback() {
    for (int limit = 0; limit != 4; ++limit) {
        auto s = options(); c::Failure failure;
        if (limit == 0) { s.maximum_trials = 328; failure = c::Failure::trial_limit; }
        else if (limit == 1) { s.maximum_rhs_evaluations = 6950; failure = c::Failure::evaluation_limit; }
        else if (limit == 2) { s.maximum_event_rows = 3; failure = c::Failure::event_limit; }
        else { s.maximum_interval_rows = 10; failure = c::Failure::history_limit; }
        c::Reference r;
        require(bool(r.initialize(simultaneous_config(), simultaneous_state(), s)), "initialize work-limited reference");
        auto control = r; const auto result = r.advance(.016);
        require(result.status == c::Status::budget_exhausted && result.failure == failure,
            "typed work-limit exhaustion");
        require(result.attempted.accepted_steps > 0 && same(r, control), "whole-call rollback after partial progress");
        require(bool(r.advance(1e-4)) && bool(control.advance(1e-4)) && same(r, control),
            "deterministic continuation also preserves hidden controller and sums");
    }
    auto s = options(); s.minimum_step_s = 1e-7;
    c::State initial; initial.time_s = 7 * std::numbers::pi / 6000 - 5e-8;
    c::Reference r;
    require(bool(r.initialize(clamp_config(), initial, s)), "initialize near-floor crossing");
    const auto control = r; const auto result = r.advance(1e-6);
    require(result.status == c::Status::numerical_failure && result.failure == c::Failure::minimum_progress && same(r, control),
        "near-floor crossing fails explicitly with complete rollback");
}
}
int main() {
    simultaneous_landing(); clamp_crossings(); rollback();
    if (failures == 0) { std::cout << "contact event split checks passed\n"; }
    return failures == 0 ? 0 : 1;
}
