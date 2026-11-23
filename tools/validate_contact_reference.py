#!/usr/bin/env python3
"""Independent numerical audit of the frozen synthetic contact fixtures.

This optional developer tool needs NumPy and SciPy; neither is a runtime or
base CTest dependency.  It never imports, executes, or links candidate solver
code.  Candidate samples are data.  Radau, closed forms, and independently
assembled matrix exponentials are the oracles.

Input JSON is {"runs": [{"fixture": str, "maximum_step_s": float,
"event_tolerance_s": float, "samples": [...], "events": [...]}]}.
Repeat --candidate to combine a refinement sweep. CSV rows use the same scalar
names, q0_m/q1_m, v0_m_per_s/v1_m_per_s, and run metadata on every row. A CSV
without event/accepted-interval diagnostics cannot establish complete acceptance.
Every sample includes time_s, q_m[2], v_m_per_s[2], hammer_position_m,
hammer_velocity_m_per_s, hammer_force_n, damper_force_n, hammer_impulse_ns,
damper_impulse_ns, modal_loss_j, hammer_loss_j, damper_loss_j,
actuator_work_j, launch_work_j, and energy_j. Event rows include contact
(hammer/damper), kind (onset/separation/clamp_on/clamp_off/grazing/relaunch),
time_s, bracket_lo_s and bracket_hi_s. Grazing rows also need
gap_uncertainty_m. Full acceptance additionally requires accepted-interval
diagnostics and independently located peaks; missing data are reported as
incomplete, never silently treated as passing.
Relaunch snapshots additionally identify time, requested velocity, success,
failure code and work, with before/after states at that same time. Missing
snapshot evidence is incomplete; an explicit list must cover every frozen action.

All constants and budgets below are from CONTACT_ACCEPTANCE_20261123 and
W1-A005/A007/A012. They are engineering fixtures, not Rhodes measurements.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import sys
from dataclasses import dataclass, replace
from functools import lru_cache
from pathlib import Path
from typing import Any, Callable


STEP_LEVELS = (20e-6, 10e-6, 5e-6, 2.5e-6)
EXTRA_STEP_LEVELS = (1.25e-6, 0.625e-6)
EVENT_LEVELS = (10e-9, 5e-9, 2.5e-9)
# These are the emitted settings of the original eight-run sweep, including
# every local error control. They define slots; observed minima never do.
LOCAL_SCALES = (1.0, 0.1, 0.01)
OUTPUT_STEP = 20e-6
LOCAL_TOLERANCES = {"relative_tolerance": 1e-8, "absolute_tolerance": 1e-12,
                    "velocity_absolute_tolerance": 1e-9,
                    "energy_absolute_tolerance": 1e-13,
                    "impulse_absolute_tolerance": 1e-13}
RUN_SETTING_FIELDS = ("maximum_step_s", "event_tolerance_s", "output_step_s",
                      *LOCAL_TOLERANCES)
BUDGETS = {
    "position": (1e-8, 1e-4), "velocity": (1e-5, 1e-4),
    "force": (1e-4, 2e-4), "impulse": (1e-8, 1e-4),
    "time": (1e-7, 1e-4), "momentum": (5e-9, 1e-6),
    "constitutive": (1e-10, 1e-10),
}
SCALE_BUDGETS = {"position": (1e-11, 1e-8), "velocity": (1e-8, 1e-8),
                 "force": (1e-7, 1e-8), "impulse": (1e-10, 1e-8),
                 "energy": (1e-10, 1e-8), "voltage": (1e-7, 1e-8),
                 "time": (1e-9, 1e-8)}
MU = 0.0016
OMEGA = math.sqrt(20000 / MU)
SEPARATION = math.pi / OMEGA
GRAZE_TIME = 0.0010073


def numerics() -> tuple[Any, Any, Any, Any]:
    try:
        import numpy as np
        from scipy.integrate import quad, solve_ivp
        from scipy.linalg import expm
    except ImportError as exc:
        raise RuntimeError("Optional independent audit requires NumPy and SciPy; "
                           "they are not dependencies of the reference CTests.") from exc
    return np, solve_ivp, expm, quad


@dataclass(frozen=True)
class Fixture:
    name: str
    mass: tuple[float, float] = (0.002, 0.001)
    frequency: tuple[float, float] = (200.0, 650.0)
    beta: tuple[float, float] = (1.0, 3.0)
    bh: tuple[float, float] = (1.0, 0.35)
    bd: tuple[float, float] = (1.0, -0.25)
    pickup: tuple[float, float] = (1.0, 0.2)
    hammer_mass: float = 0.008
    kh: float = 20000.0
    ch: float = 0.0
    kd: float = 0.0
    cd: float = 0.0
    clearance: float = 0.0
    q: tuple[float, float] = (0.0, 0.0)
    v: tuple[float, float] = (0.0, 0.0)
    x: float = -0.0002
    vh: float = 1.0
    duration: float = 0.020
    face: str = "constant"
    face_offset: float = 0.0
    face_velocity: float = 0.0
    face_amplitude: float = 0.0
    face_omega: float = 0.0
    energy_scale: float = 0.004
    relaunch: tuple[tuple[float, float], ...] = ()
    scaled: bool = False

    def motion(self, time: float) -> tuple[float, float]:
        if self.face == "sinusoidal":
            return (self.face_offset + self.face_amplitude * math.cos(self.face_omega * time),
                    -self.face_amplitude * self.face_omega * math.sin(self.face_omega * time))
        return self.face_offset + self.face_velocity * time, self.face_velocity

    def stiffness(self) -> tuple[float, float]:
        return tuple(m * (2 * math.pi * f) ** 2
                     for m, f in zip(self.mass, self.frequency))

    def damping(self) -> tuple[float, float]:
        return tuple(2 * m * b for m, b in zip(self.mass, self.beta))


def fixtures() -> dict[str, Fixture]:
    base = Fixture("supported_impact")
    free = replace(base, name="conservative_impact", frequency=(0.0, 650.0),
                   beta=(0.0, 0.0), bh=(1.0, 0.0), x=0.0, duration=0.002)
    out = {base.name: base, free.name: free}
    variants = [
        replace(free, name="damped_impact", ch=40000),
        replace(free, name="forced_clamp", ch=40000, x=0.0001, vh=-1,
                duration=0.0002, energy_scale=0.0041),
        replace(base, name="supported_conservative", beta=(0.0, 0.0)),
        replace(base, name="separated_oscillator", x=-0.002, vh=0,
                q=(0.00008, -0.00002), v=(0.2, -0.1), energy_scale=0.0001),
        replace(base, name="evolved_relaunch",
                relaunch=((0.0037, 1.0),), energy_scale=0.005),
    ]
    for item in variants:
        out[item.name] = item
    preload = replace(base, name="preload_equilibrium", x=-0.002, vh=0,
                      kd=12000, cd=30000, face_offset=-0.00005,
                      energy_scale=0.00001)
    k = preload.stiffness()
    compliance = sum(b * b / kj for b, kj in zip(preload.bd, k))
    delta = -preload.face_offset / (1 + preload.kd * compliance)
    force = preload.kd * delta
    equilibrium = tuple(-b * force / kj for b, kj in zip(preload.bd, k))
    preload = replace(preload, q=equilibrium)
    out[preload.name] = preload
    for sign, suffix in ((1, "plus"), (-1, "minus")):
        perturb = (equilibrium[0] + sign * 1e-6, equilibrium[1])
        p = replace(preload, name="preload_" + suffix, q=perturb)
        out[p.name] = p
        p = replace(p, name="preload_linear_" + suffix, cd=0)
        out[p.name] = p
    for sign, name in ((1, "withdraw_damper"), (-1, "advance_damper")):
        p = replace(preload, name=name, face="linear", face_velocity=sign * 0.02,
                    duration=0.0001, energy_scale=0.0001)
        out[name] = p
    p = replace(preload, name="moving_damper", q=(0.0, 0.0), face="sinusoidal",
                face_offset=0.00015, face_amplitude=0.00025,
                face_omega=2 * math.pi / 0.012, duration=0.024, energy_scale=0.0001)
    out[p.name] = p
    for sign, suffix in ((1, "plus"), (-1, "minus")):
        q = (0.00008, -0.00002)
        p = replace(base, name="relaunch_phase_" + suffix, q=q,
                    v=(sign * 0.2, -sign * 0.1),
                    x=sum(b * value for b, value in zip(base.bh, q)) + 0.00005,
                    vh=-0.3, ch=40000, relaunch=((0.0, 1.0),),
                    duration=0.002, energy_scale=0.005)
        out[p.name] = p
    for shift, suffix in ((-1e-6, "before"), (0.0, "at"), (1e-6, "after")):
        p = replace(free, name="event_" + suffix,
                    relaunch=((SEPARATION + shift, 1.0),), energy_scale=0.005)
        out[p.name] = p
    omega = 2 * math.pi * 200
    for shift, suffix in ((-1e-8, "miss"), (0.0, "tangent"), (1e-8, "contact")):
        p = replace(base, name="grazing_" + suffix, bh=(1.0, 0.0),
                    beta=(0.0, 0.0), q=(-1e-4 * math.cos(omega * GRAZE_TIME), 0.0),
                    v=(-1e-4 * omega * math.sin(omega * GRAZE_TIME), 0.0),
                    x=-1e-4 + shift, vh=0, duration=GRAZE_TIME + 30e-6,
                    energy_scale=0.0001)
        out[p.name] = p
    for name in ("supported_impact", "moving_damper", "evolved_relaunch"):
        p = out[name]
        scale = (2.0, -3.0)
        p = replace(p, name=name + "_scaled", scaled=True,
                    mass=tuple(m / s**2 for m, s in zip(p.mass, scale)),
                    bh=tuple(b / s for b, s in zip(p.bh, scale)),
                    bd=tuple(b / s for b, s in zip(p.bd, scale)),
                    pickup=tuple(b / s for b, s in zip(p.pickup, scale)),
                    q=tuple(q * s for q, s in zip(p.q, scale)),
                    v=tuple(v * s for v, s in zip(p.v, scale)))
        out[p.name] = p
    return out


def law(delta: float, speed: float, stiffness: float, damping: float) -> tuple[float, float, float]:
    """Independent p=1 force, stored contact energy, and physical loss power."""
    if delta <= 0 or stiffness == 0:
        return 0.0, 0.0, 0.0
    elastic = stiffness * delta
    raw = elastic + damping * delta * speed
    if raw <= 0:
        return 0.0, elastic * delta / 2, -elastic * speed
    return raw, elastic * delta / 2, damping * delta * speed * speed


def observe(f: Fixture, time: float, y: Any) -> dict[str, Any]:
    q, v = y[:2], y[2:4]
    gap_h = y[4] - sum(b * a for b, a in zip(f.bh, q)) - f.clearance
    speed_h = y[5] - sum(b * a for b, a in zip(f.bh, v))
    face, face_speed = f.motion(time)
    gap_d = sum(b * a for b, a in zip(f.bd, q)) - face
    speed_d = sum(b * a for b, a in zip(f.bd, v)) - face_speed
    fh, uh, dh = law(gap_h, speed_h, f.kh, f.ch)
    fd, ud, dd = law(gap_d, speed_d, f.kd, f.cd)
    k, c = f.stiffness(), f.damping()
    acceleration = [(-kj * qi - cj * vi + bh * fh - bd * fd) / m
                    for kj, cj, qi, vi, bh, bd, m in zip(k, c, q, v, f.bh, f.bd, f.mass)]
    hammer_acceleration = -fh / f.hammer_mass
    background = sum(cj * vi**2 for cj, vi in zip(c, v))
    energy = sum((m * vi**2 + kj * qi**2) / 2
                 for m, kj, qi, vi in zip(f.mass, k, q, v)) + f.hammer_mass * y[5]**2 / 2 + uh + ud
    x = sum(b * a for b, a in zip(f.pickup, q))
    speed = sum(b * a for b, a in zip(f.pickup, v))
    energy_rate = (sum(m * vi * ai + kj * qi * vi
                       for m, vi, ai, kj, qi in zip(f.mass, v, acceleration, k, q))
                   + f.hammer_mass * y[5] * hammer_acceleration
                   + (f.kh * max(0.0, gap_h) * speed_h if f.kh else 0.0)
                   + (f.kd * max(0.0, gap_d) * speed_d if f.kd else 0.0))
    return {
        "time_s": float(time), "q_m": list(map(float, q)), "v_m_per_s": list(map(float, v)),
        "hammer_position_m": float(y[4]), "hammer_velocity_m_per_s": float(y[5]),
        "hammer_force_n": fh, "damper_force_n": fd,
        "hammer_gap_m": gap_h, "damper_gap_m": gap_d,
        "hammer_relative_velocity_m_per_s": speed_h,
        "damper_relative_velocity_m_per_s": speed_d,
        "energy_j": energy, "modal_loss_j": float(y[6]),
        "hammer_loss_j": float(y[7]), "damper_loss_j": float(y[8]),
        "actuator_work_j": float(y[9]), "launch_work_j": float(y[10]),
        "hammer_impulse_ns": float(y[11]), "damper_impulse_ns": float(y[12]),
        "modal_acceleration_m_per_s2": acceleration,
        "hammer_acceleration_m_per_s2": hammer_acceleration,
        "instantaneous_power_residual_w": energy_rate + background + dh + dd + fd * face_speed,
        "actuator_power_w": -fd * face_speed,
        "pickup_voltage_v": -(0.4 + 1600 * x + 1200000 * x*x) * speed,
    }


def initial_vector(f: Fixture) -> Any:
    np, _, _, _ = numerics()
    return np.array([*f.q, *f.v, f.x, f.vh, 0, 0, 0, 0, 0, 0, 0], dtype=float)


def rhs(f: Fixture) -> Callable[[float, Any], Any]:
    np, _, _, _ = numerics()
    def evaluate(t: float, y: Any) -> Any:
        o = observe(f, t, y)
        face, face_speed = f.motion(t)
        del face
        _, _, dh = law(o["hammer_gap_m"], o["hammer_relative_velocity_m_per_s"], f.kh, f.ch)
        _, _, dd = law(o["damper_gap_m"], o["damper_relative_velocity_m_per_s"], f.kd, f.cd)
        return np.array([y[2], y[3], *o["modal_acceleration_m_per_s2"], y[5],
                         o["hammer_acceleration_m_per_s2"],
                         sum(c * v*v for c, v in zip(f.damping(), y[2:4])), dh, dd,
                         -o["damper_force_n"] * face_speed, 0,
                         o["hammer_force_n"], o["damper_force_n"]])
    return evaluate


@dataclass
class Oracle:
    fixture: Fixture
    method: str
    state: Callable[[float], Any]
    events: list[dict[str, Any]]
    evaluation_count: int = 0
    # Exact left/right states already produced by the independent action solver.
    # Capturing them avoids sampling across a right-continuous velocity jump.
    relaunch_states: tuple[tuple[float, Any, Any], ...] = ()

    def sample(self, time: float) -> dict[str, Any]:
        return observe(self.fixture, time, self.state(time))


def exact_free(f: Fixture) -> Oracle:
    np, _, _, quad = numerics()
    initial = initial_vector(f)
    name = f.name
    def trajectory(t: float) -> Any:
        y = initial.copy()
        if name == "conservative_impact":
            s = min(t, SEPARATION)
            delta = math.sin(OMEGA*s) / OMEGA
            relative = math.cos(OMEGA*s)
            y[4], y[0] = 0.8*s + 0.2*delta, 0.8*s - 0.8*delta
            y[5], y[2] = 0.8 + 0.2*relative, 0.8 - 0.8*relative
            y[4] += y[5] * (t-s)
            y[0] += y[2] * (t-s)
            y[11] = MU * (1-relative)
        elif name == "forced_clamp":
            y[4] = 1e-4 - t
            delta = max(0.0, 1e-4-t)
            # Integral of the physical clamped-branch loss -k*delta*delta_dot.
            y[7] = 10000 * (1e-8-delta*delta)
        else:
            def scalar(j: int, time: float) -> tuple[float, float]:
                w = 2*math.pi*f.frequency[j]
                beta = f.beta[j]
                wd = math.sqrt(w*w-beta*beta)
                a, b = f.q[j], (f.v[j]+beta*f.q[j])/wd
                sine, cosine = math.sin(wd*time), math.cos(wd*time)
                env = math.exp(-beta*time)
                q = env*(a*cosine+b*sine)
                return q, -beta*q+env*wd*(-a*sine+b*cosine)
            for j in range(2):
                y[j], y[j+2] = scalar(j, t)
            y[4] += f.vh*t
            y[6] = sum(quad(lambda s, j=j: f.damping()[j]*scalar(j, s)[1]**2,
                            0, t, epsabs=1e-15, epsrel=1e-12, limit=250)[0]
                       for j in range(2))
        return np.array(y)
    events: list[dict[str, Any]] = []
    if name == "conservative_impact":
        events = [{"contact": "hammer", "kind": "onset", "time_s": 0.0},
                  {"contact": "hammer", "kind": "separation", "time_s": SEPARATION}]
    elif name == "forced_clamp":
        events = [{"contact": "hammer", "kind": "clamp_on", "time_s": 0.0},
                  {"contact": "hammer", "kind": "separation", "time_s": 1e-4}]
    return Oracle(f, "piecewise analytic" if name != "separated_oscillator" else "scalar oscillator + quadrature",
                  trajectory, events)


def exact_supported(f: Fixture) -> Oracle:
    """Piecewise affine matrix exponential for conservative supported impact."""
    np, _, expm, _ = numerics()
    from scipy.optimize import brentq
    initial = initial_vector(f)
    segments: list[tuple[float, float, Any, Any]] = []
    events: list[dict[str, Any]] = []
    t0, active = 0.0, False
    y0 = np.r_[initial[:6], 0.0, 1.0]  # impulse and affine constant
    def matrix(contact: bool) -> Any:
        a = np.zeros((8, 8))
        a[0, 2] = a[1, 3] = a[4, 5] = 1
        for j in range(2):
            a[j+2, j] = -(2*math.pi*f.frequency[j])**2
            a[j+2, j+2] = -2*f.beta[j]
        if contact:
            gap = np.array([-f.bh[0], -f.bh[1], 0, 0, 1, 0, 0, -f.clearance])
            for j in range(2):
                a[j+2] += f.bh[j]*f.kh/f.mass[j]*gap
            a[5] -= f.kh/f.hammer_mass*gap
            a[6] += f.kh*gap
        return a
    while t0 < f.duration-1e-15:
        a = matrix(active)
        def gap(dt: float) -> float:
            z = expm(a*dt)@y0
            return float(z[4]-np.dot(f.bh, z[:2])-f.clearance)
        previous = min(1e-12, f.duration-t0)
        gp = gap(previous)
        root = None
        # A 2.5 us independent scan excludes the initial root and brackets each
        # linear branch before a separately solved zero; no candidate stages.
        for dt in np.arange(2.5e-6, f.duration-t0+2.5e-6, 2.5e-6):
            dt = min(float(dt), f.duration-t0)
            value = gap(dt)
            if (active and gp > 0 >= value) or (not active and gp < 0 <= value):
                root = brentq(gap, previous, dt, xtol=1e-15, rtol=1e-14)
                break
            previous, gp = dt, value
            if dt >= f.duration-t0:
                break
        end = f.duration if root is None else t0+root
        segments.append((t0, end, y0.copy(), a))
        if root is None:
            break
        y0 = expm(a*root)@y0
        active = not active
        t0 = end
        events.append({"contact": "hammer", "kind": "onset" if active else "separation", "time_s": t0})
        if len(events) > 1000:
            raise RuntimeError("Independent expm branch budget exhausted")
    @lru_cache(maxsize=20000)
    def trajectory(t: float) -> Any:
        for start, end, z, a in segments:
            if t <= end+1e-15:
                value = expm(a*(t-start))@z
                y = initial.copy()
                y[:6] = value[:6]
                y[11] = value[6]
                return y
        raise ValueError("Time outside independent trajectory")
    return Oracle(f, "independent branch matrix exponential", trajectory, events)


def exact_relaunch_impact(f: Fixture) -> Oracle:
    """Exact relative oscillator and centre-of-mass motion across action events.

    Geometric roots at an external action time are processed first. The oracle
    uses a closed-form zero, so roundoff in an integration trajectory cannot
    choose a different event order at the deliberately exact separation tie.
    """
    np, _, _, _ = numerics()
    y = initial_vector(f)
    start = 0.0
    active = True
    events = [{"contact": "hammer", "kind": "onset", "time_s": 0.0}]
    segments: list[tuple[float, float, Any, bool]] = []
    relaunch_states: list[tuple[float, Any, Any]] = []
    def propagate(state: Any, dt: float, compressed: bool, root: bool = False) -> Any:
        result = state.copy()
        delta = state[4]-state[0]
        speed = state[5]-state[2]
        centre = 0.8*state[4]+0.2*state[0]
        centre_speed = 0.8*state[5]+0.2*state[2]
        if compressed:
            cosine, sine = math.cos(OMEGA*dt), math.sin(OMEGA*dt)
            compression = delta*cosine+speed*sine/OMEGA
            relative = -delta*OMEGA*sine+speed*cosine
        else:
            compression, relative = delta+speed*dt, speed
        if root:
            compression = 0.0  # Exact value at the independently solved zero.
        centre += centre_speed*dt
        result[4], result[0] = centre+0.2*compression, centre-0.8*compression
        result[5], result[2] = centre_speed+0.2*relative, centre_speed-0.8*relative
        if compressed:
            result[11] += MU*(speed-relative)
        return result
    for end, launch in [*f.relaunch, (f.duration, None)]:
        while start < end-1e-15:
            delta, speed = y[4]-y[0], y[5]-y[2]
            crossing = math.inf
            if active:
                phase = math.atan2(max(0.0, delta)*OMEGA, speed)
                crossing = (math.pi-phase)/OMEGA
            elif speed > 0 and delta < 0:
                crossing = -delta/speed
            remaining = end-start
            hits_root = crossing <= remaining+4*math.ulp(end)
            dt = min(crossing, remaining) if hits_root else remaining
            segments.append((start, start+dt, y.copy(), active))
            y = propagate(y, dt, active, hits_root)
            start += dt
            if hits_root:
                active = not active
                events.append({"contact": "hammer", "kind": "onset" if active else "separation", "time_s": start})
            if dt <= 0:
                raise RuntimeError("Analytic action oracle lost progress")
        if launch is not None:
            before = y.copy()
            y[10] += f.hammer_mass*(launch**2-y[5]**2)/2
            y[5] = launch
            relaunch_states.append((end, before, y.copy()))
            events.append({"contact": "hammer", "kind": "relaunch", "time_s": end})
            if not active and y[4] == y[0] and y[5] > y[2]:
                active = True
                events.append({"contact": "hammer", "kind": "onset", "time_s": end})
    def trajectory(time: float) -> Any:
        for begin, end, state, compressed in reversed(segments):
            if begin-1e-15 <= time <= end+1e-15:
                return propagate(state, time-begin, compressed)
        raise ValueError("Time outside analytic action trajectory")
    return Oracle(f, "piecewise analytic impact with exact action ordering", trajectory, events,
                  relaunch_states=tuple(relaunch_states))


def radau_oracle(f: Fixture, *, tight: bool = True) -> Oracle:
    np, solve_ivp, _, _ = numerics()
    segments: list[tuple[float, float, Any]] = []
    events: list[dict[str, Any]] = []
    relaunch_states: list[tuple[float, Any, Any]] = []
    y, start, nfev = initial_vector(f), 0.0, 0
    def geometric(contact: str) -> Callable[[float, Any], float]:
        def event(t: float, state: Any) -> float:
            if contact == "damper" and not f.kd:
                return 1.0
            return observe(f, t, state)[contact+"_gap_m"]
        return event
    def clamping(contact: str) -> Callable[[float, Any], float]:
        def event(t: float, state: Any) -> float:
            k, c = (f.kh, f.ch) if contact == "hammer" else (f.kd, f.cd)
            if not k or not c:
                return 1.0
            return k+c*observe(f, t, state)[contact+"_relative_velocity_m_per_s"]
        return event
    event_functions = [geometric("hammer"), geometric("damper"),
                       clamping("hammer"), clamping("damper")]
    launches = list(f.relaunch)
    if launches and launches[0][0] == 0:
        _, velocity = launches.pop(0)
        before = y.copy()
        y[10] += f.hammer_mass*(velocity**2-y[5]**2)/2
        y[5] = velocity
        relaunch_states.append((0.0, before, y.copy()))
        events.append({"contact": "hammer", "kind": "relaunch", "time_s": 0.0})
    for end, launch_velocity in [*launches, (f.duration, None)]:
        if end <= start:
            continue
        atol = np.array([1e-16, 1e-16, 1e-13, 1e-13, 1e-16, 1e-13,
                         1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16])
        solution = solve_ivp(rhs(f), (start, end), y, method="Radau", dense_output=True,
                             rtol=1e-12 if tight else 1e-10,
                             atol=atol if tight else atol*10,
                             max_step=2.5e-6 if tight else 5e-6, events=event_functions)
        if not solution.success or abs(solution.t[-1]-end) > 1e-15:
            raise RuntimeError("Independent Radau failed: "+solution.message)
        nfev += solution.nfev
        segments.append((start, end, solution.sol))
        for event_index, roots in enumerate(solution.t_events):
            contact = "hammer" if event_index % 2 == 0 else "damper"
            for root in roots:
                time = float(root)
                o = observe(f, time, solution.sol(time))
                speed = o[contact+"_relative_velocity_m_per_s"]
                if event_index < 2:
                    kind = "onset" if speed > 1e-10 else "separation" if speed < -1e-10 else "grazing"
                else:
                    if o[contact+"_gap_m"] <= 0:
                        continue
                    left, right = max(start, time-1e-10), min(end, time+1e-10)
                    kind = "clamp_on" if event_functions[event_index](right, solution.sol(right)) < event_functions[event_index](left, solution.sol(left)) else "clamp_off"
                events.append({"contact": contact, "kind": kind, "time_s": time})
        y = solution.y[:, -1].copy()
        start = end
        if launch_velocity is not None:
            before = y.copy()
            y[10] += f.hammer_mass*(launch_velocity**2-y[5]**2)/2
            y[5] = launch_velocity
            relaunch_states.append((start, before, y.copy()))
            events.append({"contact": "hammer", "kind": "relaunch", "time_s": start})
    # A tangent has no sign change: solve the independent free turning point
    # explicitly. Tiny integration excursions must never reclassify it as a hit.
    if f.name == "grazing_tangent":
        events = [{"contact": "hammer", "kind": "grazing", "time_s": GRAZE_TIME}]
    events.sort(key=lambda item: item["time_s"])
    deduplicated: list[dict[str, Any]] = []
    for e in events:
        if not deduplicated or any(e[k] != deduplicated[-1][k] for k in ("contact", "kind")) or abs(e["time_s"]-deduplicated[-1]["time_s"]) > 1e-12:
            deduplicated.append(e)
    @lru_cache(maxsize=20000)
    def trajectory(t: float) -> Any:
        # Right-continuous external velocity events, with continuous positions.
        for index in range(len(segments)-1, -1, -1):
            begin, end, solution = segments[index]
            if begin-1e-15 <= t <= end+1e-15:
                result = solution(t).copy()
                if index == len(segments)-1 and t == end and launches and launches[-1][0] == t:
                    result = y.copy()
                return result
        raise ValueError("Time outside independent Radau trajectory")
    return Oracle(f, "SciPy Radau (independently assembled)", trajectory, deduplicated, nfev,
                  tuple(relaunch_states))


@lru_cache(maxsize=64)
def oracle_for(name: str, tight: bool = True) -> Oracle:
    f = fixtures()[name]
    if name in {"conservative_impact", "forced_clamp", "separated_oscillator"}:
        return exact_free(f)
    if name == "supported_conservative":
        return exact_supported(f)
    if name.startswith("event_"):
        return exact_relaunch_impact(f)
    return radau_oracle(f, tight=tight)


def tolerance(kind: str, value: float, *, factor: float = 1.0,
              energy_scale: float = 0.004, structural: bool = False) -> float:
    if kind == "energy" and not structural:
        return factor*(2e-9+5e-5*energy_scale)
    a, r = (SCALE_BUDGETS if structural else BUDGETS)[kind]
    return factor*(a+r*abs(value))


def fields(sample: dict[str, Any], scaled: bool = False) -> dict[str, tuple[float, str]]:
    out: dict[str, tuple[float, str]] = {}
    for key, kind in (("q_m", "position"), ("v_m_per_s", "velocity")):
        for j, value in enumerate(sample[key]):
            out[f"{key}[{j}]"] = (float(value)/(2.0, -3.0)[j] if scaled else float(value), kind)
    for key, kind in (("hammer_position_m", "position"), ("hammer_velocity_m_per_s", "velocity"),
                      ("hammer_force_n", "force"), ("damper_force_n", "force"),
                      ("hammer_impulse_ns", "impulse"), ("damper_impulse_ns", "impulse"),
                      ("modal_loss_j", "energy"), ("hammer_loss_j", "energy"),
                      ("damper_loss_j", "energy"), ("actuator_work_j", "energy"),
                      ("launch_work_j", "energy"), ("energy_j", "energy")):
        out[key] = (float(sample[key]), kind)
    for key, kind in (("hammer_gap_m", "position"), ("damper_gap_m", "position"),
                      ("pickup_voltage_v", "voltage")):
        if key in sample:
            out[key] = (float(sample[key]), kind)
    return out


def compare_samples(actual: dict[str, Any], expected: dict[str, Any], f: Fixture,
                    *, factor: float = 1.0, structural: bool = False,
                    actual_scaled: bool = False) -> tuple[dict[str, float], list[str]]:
    ratios, failures = {}, []
    for key, (value, kind) in fields(actual, actual_scaled).items():
        if kind == "voltage" and not structural:
            continue
        reference = fields(expected)[key][0]
        budget = tolerance(kind, reference, factor=factor, energy_scale=f.energy_scale, structural=structural)
        ratio = abs(value-reference)/budget
        # Failed numerical evidence must still produce serializable output.
        # An overflowed ratio is a failure, never a finite comparison result.
        if math.isfinite(ratio):
            ratios[key] = ratio
        if not math.isfinite(value) or not math.isfinite(ratio) or ratio > 1:
            failures.append(f"{key}: {value:.17g} vs {reference:.17g}, budget {budget:.6g}")
    return ratios, failures


def finite_number(value: Any) -> bool:
    """A JSON number usable by the floating-point audit, excluding booleans."""
    if type(value) not in (int, float):
        return False
    try:
        return math.isfinite(value)
    except OverflowError:  # Arbitrarily large JSON integers cannot become doubles.
        return False


def validate_run_input(run: Any) -> list[str]:
    """Check candidate structure/domains without importing a numerical backend.

    Missing optional evidence remains an audit incompleteness. Present malformed
    evidence fails here, before indexing vectors, evaluating physics, sorting
    refinement metadata, or copying candidate events into a JSON report.
    """
    failures: list[str] = []
    if not isinstance(run, dict):
        return ["Candidate run must be an object"]
    name = run.get("fixture")
    if not isinstance(name, str) or name not in fixtures():
        return ["Unknown frozen fixture"]
    duration = fixtures()[name].duration

    def number(value: Any, where: str, *, positive: bool = False,
               nonnegative: bool = False) -> bool:
        if not finite_number(value):
            failures.append(where + " must be a finite number")
            return False
        if positive and value <= 0:
            failures.append(where + " must be positive")
            return False
        if nonnegative and value < 0:
            failures.append(where + " must be nonnegative")
            return False
        return True

    # Unknown numeric extensions may be ignored scientifically, but cannot
    # carry NaN/Inf into the candidate-event dictionaries retained in a report.
    def finite_json(value: Any, where: str) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                if not isinstance(key, str):
                    failures.append(where + " object keys must be strings")
                finite_json(child, where + "." + str(key))
        elif isinstance(value, list):
            for index, child in enumerate(value):
                finite_json(child, f"{where}[{index}]")
        elif type(value) in (float, int) and not finite_number(value):
            failures.append(where + " must be finite")
        elif value is not None and type(value) not in (str, bool, int, float):
            failures.append(where + " must contain only JSON values")

    finite_json(run, "run")
    for key in ("maximum_step_s", "event_tolerance_s"):
        number(run.get(key), key, positive=True)
    for key in ("relative_tolerance", "absolute_tolerance", "velocity_absolute_tolerance",
                "energy_absolute_tolerance", "impulse_absolute_tolerance", "output_step_s"):
        if key in run:
            number(run[key], key, positive=True)
    if "success" in run and type(run["success"]) is not bool:
        failures.append("success must be a boolean")
    if "failure_code" in run and (type(run["failure_code"]) is not int or run["failure_code"] < 0):
        failures.append("failure_code must be a nonnegative integer")
    if "source" in run and not isinstance(run["source"], str):
        failures.append("source must be a string")

    scalar_fields = ("hammer_position_m", "hammer_velocity_m_per_s", "hammer_force_n",
                     "damper_force_n", "hammer_impulse_ns", "damper_impulse_ns",
                     "modal_loss_j", "hammer_loss_j", "damper_loss_j", "actuator_work_j",
                     "launch_work_j", "energy_j")
    optional_scalars = ("hammer_acceleration_m_per_s2", "hammer_gap_m", "damper_gap_m",
                        "hammer_relative_velocity_m_per_s", "damper_relative_velocity_m_per_s",
                        "hammer_clipped_loss_j", "damper_clipped_loss_j", "pickup_voltage_v",
                        "instantaneous_power_residual_w", "actuator_power_w")

    def sample_values(sample: Any, where: str) -> bool:
        if not isinstance(sample, dict):
            failures.append(where + " must be an object")
            return False
        for key in ("q_m", "v_m_per_s", "modal_acceleration_m_per_s2"):
            if key == "modal_acceleration_m_per_s2" and key not in sample:
                continue
            value = sample.get(key)
            if not isinstance(value, list) or len(value) != 2:
                failures.append(where + "." + key + " must contain exactly two components")
            else:
                for index, component in enumerate(value):
                    number(component, f"{where}.{key}[{index}]")
        for key in scalar_fields:
            number(sample.get(key), where + "." + key)
        for key in optional_scalars:
            if key in sample:
                number(sample[key], where + "." + key)
        return True

    samples = run.get("samples", [])
    if not isinstance(samples, list):
        failures.append("samples must be an array")
    else:
        previous = None
        for index, sample in enumerate(samples):
            if not sample_values(sample, f"samples[{index}]"):
                continue
            time = sample.get("time_s")
            if (not finite_number(time) or time < 0 or time > duration + 1e-12 or
                    (previous is not None and time <= previous)):
                failures.append("Sample times must be finite, strictly increasing, and within the frozen duration")
            if finite_number(time):
                previous = time

    events = run.get("events", [])
    if not isinstance(events, list):
        failures.append("events must be an array")
    else:
        for index, event in enumerate(events):
            where = f"events[{index}]"
            if not isinstance(event, dict):
                failures.append(where + " must be an object")
                continue
            for key in ("contact", "kind"):
                if not isinstance(event.get(key), str):
                    failures.append(where + "." + key + " must be a string")
            time = event.get("time_s")
            if number(time, where + ".time_s") and not 0 <= time <= duration + 1e-12:
                failures.append(where + ".time_s lies outside the frozen duration")
            for key in ("bracket_lo_s", "bracket_hi_s"):
                if key in event and not finite_number(event[key]):
                    failures.append("invalid event bracket or width above declared tolerance: " + where + "." + key)
            if "gap_uncertainty_m" in event:
                number(event["gap_uncertainty_m"], where + ".gap_uncertainty_m", nonnegative=True)
            if "work_j" in event:
                number(event["work_j"], where + ".work_j")

    diagnostics = run.get("diagnostics", {})
    if not isinstance(diagnostics, dict):
        failures.append("diagnostics must be an object")
    else:
        for key in ("max_abs_cumulative_residual_j", "sum_abs_interval_residual_j"):
            if key not in diagnostics:
                continue
            if not finite_number(diagnostics[key]):
                failures.append(key + " exceeds frozen energy budget: value must be finite")
            elif diagnostics[key] < 0:
                failures.append(key + " must be nonnegative")
        for key in ("maximum_power_residual_w", "initial_energy_j",
                    "minimum_accepted_step_s", "maximum_accepted_step_s"):
            if key in diagnostics:
                number(diagnostics[key], "diagnostics." + key, nonnegative=True)
        for key in ("accepted_steps", "rejected_steps", "rhs_evaluations", "analytic_free_evaluations",
                    "event_subdivisions", "root_iterations", "grazing_candidates"):
            if key in diagnostics and (type(diagnostics[key]) is not int or diagnostics[key] < 0):
                failures.append("diagnostics." + key + " must be a nonnegative integer")
        if "sampled_maxima" in diagnostics:
            maxima = diagnostics["sampled_maxima"]
            if not isinstance(maxima, dict):
                failures.append("diagnostics.sampled_maxima must be an object")
            else:
                for key, value in maxima.items():
                    if key != "scope":
                        number(value, "diagnostics.sampled_maxima." + key, nonnegative=True)

    snapshots = run.get("relaunch_snapshots", [])
    if not isinstance(snapshots, list):
        failures.append("relaunch_snapshots must be an array")
    else:
        for index, snapshot in enumerate(snapshots):
            where = f"relaunch_snapshots[{index}]"
            if not isinstance(snapshot, dict):
                failures.append(where + " must be an object")
                continue
            for side in ("before", "after"):
                value = snapshot.get(side)
                if sample_values(value, where + "." + side) and "time_s" in value:
                    time = value["time_s"]
                    if number(time, where + "." + side + ".time_s") and not 0 <= time <= duration + 1e-12:
                        failures.append(where + "." + side + ".time_s lies outside the frozen duration")
            for key in ("time_s", "requested_velocity_m_per_s", "work_j"):
                if key in snapshot:
                    number(snapshot[key], where + "." + key)
            if ("time_s" in snapshot and finite_number(snapshot["time_s"]) and
                    not 0 <= snapshot["time_s"] <= duration + 1e-12):
                failures.append(where + ".time_s lies outside the frozen duration")
            if "success" in snapshot and type(snapshot["success"]) is not bool:
                failures.append(where + ".success must be a boolean")
            if "failure_code" in snapshot and (type(snapshot["failure_code"]) is not int or
                                               snapshot["failure_code"] < 0):
                failures.append(where + ".failure_code must be a nonnegative integer")
    return failures


def validate_action_evidence(run: dict[str, Any]) -> tuple[list[str], list[str]]:
    """Check declared success and instantaneous actions after input preflight.

    This check is backend-free. It uses the frozen action schedule and the
    existing event-work budget, without trusting a candidate's success flag or
    its duplicated work/energy fields as an independent reference.
    """
    failures: list[str] = []
    missing: list[str] = []
    if run.get("success") is False:
        failures.append("Candidate reported explicit failure before completing the requested interval; code="+
                        str(run.get("failure_code", "unspecified")))
    if run.get("failure_code", 0) != 0:
        failures.append("Candidate reported nonzero failure_code=" + str(run["failure_code"]))
    fixture = fixtures()[run["fixture"]]
    if "relaunch_snapshots" not in run:
        if fixture.relaunch:
            missing.append("before/after relaunch snapshots for exact state preservation")
        return failures, missing
    snapshots = run["relaunch_snapshots"]
    if len(snapshots) != len(fixture.relaunch):
        failures.append("relaunch snapshot count differs from frozen action count")
    for index, (snapshot, (time, velocity)) in enumerate(zip(snapshots, fixture.relaunch)):
        where = f"relaunch_snapshots[{index}]"
        before, after = snapshot["before"], snapshot["after"]
        for key in ("time_s", "requested_velocity_m_per_s", "success", "failure_code", "work_j"):
            if key not in snapshot:
                missing.append(where + "." + key)
        if snapshot.get("success") is False:
            failures.append(where + " reports explicit action failure")
        if snapshot.get("failure_code", 0) != 0:
            failures.append(where + " reports nonzero failure_code=" + str(snapshot["failure_code"]))
        if "time_s" in snapshot and abs(snapshot["time_s"] - time) > tolerance("time", time):
            failures.append(where + " time differs from frozen action")
        if ("requested_velocity_m_per_s" in snapshot and
                abs(snapshot["requested_velocity_m_per_s"] - velocity) > tolerance("velocity", velocity)):
            failures.append(where + " requested velocity differs from frozen action")
        if ("requested_velocity_m_per_s" in snapshot and
                after["hammer_velocity_m_per_s"] != snapshot["requested_velocity_m_per_s"]):
            failures.append(where + " final hammer velocity does not equal the requested value")
        for side, sample in (("before", before), ("after", after)):
            if "time_s" not in sample:
                missing.append(where + "." + side + ".time_s")
            elif "time_s" in snapshot and sample["time_s"] != snapshot["time_s"]:
                failures.append(where + "." + side + " time differs from instantaneous action time")
        for key in ("q_m", "v_m_per_s", "hammer_position_m", "modal_loss_j", "hammer_loss_j",
                    "damper_loss_j", "actuator_work_j", "hammer_impulse_ns", "damper_impulse_ns"):
            if before[key] != after[key]:
                failures.append(where + " relaunch changed preserved " + key)
        for key in ("hammer_clipped_loss_j", "damper_clipped_loss_j"):
            if key in before and key in after and before[key] != after[key]:
                failures.append(where + " relaunch changed preserved " + key)
        try:
            kinetic_work = fixture.hammer_mass * (after["hammer_velocity_m_per_s"]**2 -
                                                  before["hammer_velocity_m_per_s"]**2) / 2
        except OverflowError:
            failures.append(where + " kinetic work overflows")
            continue
        if not math.isfinite(kinetic_work):
            failures.append(where + " kinetic work is non-finite")
            continue
        # This is the predeclared event-work budget already used below for the
        # total launch ledger, applied to each instantaneous action identity.
        budget = 1e-12 + 1e-10 * abs(kinetic_work)
        quantities = {"launch ledger jump": after["launch_work_j"] - before["launch_work_j"],
                      "stored energy jump": after["energy_j"] - before["energy_j"]}
        if "work_j" in snapshot:
            quantities["declared work"] = snapshot["work_j"]
        for label, value in quantities.items():
            if not math.isfinite(value) or abs(value - kinetic_work) > budget:
                failures.append(where + " " + label + " disagrees with hammer kinetic work")
    return failures, missing


def normalize_sample(s: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(s, dict):
        raise ValueError("Every candidate sample must be an object")
    s = dict(s)
    aliases = {"hammer_x_m": "hammer_position_m", "hammer_v_m_per_s": "hammer_velocity_m_per_s",
               "background_loss_j": "modal_loss_j", "pedal_work_j": "actuator_work_j"}
    for old, new in aliases.items():
        if new not in s and old in s:
            s[new] = s[old]
    def component(key: str) -> float:
        value = s[key]
        if not finite_number(value):
            raise ValueError("Legacy component " + key + " must be a finite number")
        return float(value)
    if "q_m" not in s:
        s["q_m"] = [component("q0_m"), component("q1_m")]
    if "v_m_per_s" not in s:
        s["v_m_per_s"] = [component("v0_m_per_s"), component("v1_m_per_s")]
    return s


def read_runs(path: Path) -> list[dict[str, Any]]:
    if path.suffix.lower() != ".csv":
        with path.open(encoding="utf-8") as handle:
            data = json.load(handle)
        if not isinstance(data, dict):
            raise ValueError("Candidate JSON root must be an object")
        runs = data["runs"] if "runs" in data else [data]
    else:
        grouped: dict[tuple[Any, ...], dict[str, Any]] = {}
        with path.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                settings = {key: float(row[key]) for key in RUN_SETTING_FIELDS
                            if row.get(key) not in (None, "")}
                # Missing legacy metadata stays absent, so it cannot establish
                # a refinement slot. Distinct local settings never share rows.
                group = (row["fixture"], *(settings.get(key) for key in RUN_SETTING_FIELDS))
                run = grouped.setdefault(group, {"fixture": row["fixture"], **settings,
                                                  "samples": [], "events": []})
                run["samples"].append({k: float(v) for k, v in row.items()
                                       if k not in {"fixture", *RUN_SETTING_FIELDS} and v != ""})
        runs = list(grouped.values())
    if not isinstance(runs, list):
        raise ValueError("Candidate runs must be an array")
    for index, run in enumerate(runs):
        if not isinstance(run, dict) or not isinstance(run.get("samples", []), list):
            raise ValueError(f"Candidate run {index} must be an object with a sample array")
        run["samples"] = [normalize_sample(sample) for sample in run.get("samples", [])]
        run["source"] = str(path)
        failures = validate_run_input(run)
        if failures:
            raise ValueError(f"Malformed candidate run {index}: " + "; ".join(failures[:12]))
    return runs


def canonical_events(run: dict[str, Any]) -> list[dict[str, Any]]:
    aliases = {"contact_onset": "onset", "contact_separation": "separation",
               "clamp_enter": "clamp_on", "clamp_exit": "clamp_off",
               "force_clamp": "clamp_on", "force_unclamp": "clamp_off",
               "hammer_relaunch": "relaunch"}
    result = []
    for value in run.get("events", []):
        e = dict(value)
        e["kind"] = aliases.get(e["kind"], e["kind"])
        if e["kind"] == "relaunch":
            e["contact"] = "hammer"
        if e["kind"] in {"initial", "initialized"}:
            continue
        result.append(e)
    return result


def audit_run(run: dict[str, Any]) -> dict[str, Any]:
    failures = validate_run_input(run)
    name = run.get("fixture") if isinstance(run, dict) else None
    if failures:
        return {"fixture": name if isinstance(name, str) else None, "status": "fail",
                "failures": failures[:80], "failure_count": len(failures), "missing": []}
    action_failures, missing = validate_action_evidence(run)
    failures.extend(action_failures)
    worst: dict[str, float] = {}
    samples = run.get("samples", [])
    if not samples:
        return {"fixture": name, "status": "fail" if failures else "incomplete",
                "failures": failures, "failure_count": len(failures), "missing": missing + ["trajectory samples"]}
    if failures:
        return {"fixture": name, "status": "fail", "failures": failures[:80],
                "failure_count": len(failures), "missing": missing}
    oracle = oracle_for(name)
    f = oracle.fixture
    times = [float(s["time_s"]) for s in samples]
    if any(not math.isfinite(t) or t < 0 or t > f.duration+1e-12 for t in times) or any(b <= a for a, b in zip(times, times[1:])):
        failures.append("Sample times must be finite, strictly increasing, and within the frozen duration")
        return {"fixture": name, "status": "fail", "failures": failures, "missing": []}
    if abs(times[0]) > 1e-15 or abs(times[-1]-f.duration) > 1e-12:
        missing.append("complete trajectory including t=0 and the frozen final time")
    previous_balance, sum_abs, max_cumulative, max_power, max_force_law = 0.0, 0.0, 0.0, 0.0, 0.0
    initial_energy = observe(f, 0, initial_vector(f))["energy_j"]
    for s in samples:
        time = float(s["time_s"])
        try:
            reference = oracle.sample(time)
            ratios, errors = compare_samples(s, reference, f)
            for key, ratio in ratios.items():
                worst[key] = max(worst.get(key, 0.0), ratio)
            failures.extend(f"t={time:.12g}: {error}" for error in errors[:5])
            y = [*s["q_m"], *s["v_m_per_s"], s["hammer_position_m"], s["hammer_velocity_m_per_s"],
                 s["modal_loss_j"], s["hammer_loss_j"], s["damper_loss_j"], s["actuator_work_j"],
                 s["launch_work_j"], s["hammer_impulse_ns"], s["damper_impulse_ns"]]
        except (KeyError, TypeError, ValueError) as exc:
            missing.append("required sample field: "+str(exc))
            break
        try:
            independent = observe(f, time, y)
        except OverflowError:
            failures.append(f"t={time:.12g}: candidate state overflows independent physics")
            continue
        if any(not finite_number(value) for observation in independent.values()
               for value in (observation if isinstance(observation, list) else [observation])):
            failures.append(f"t={time:.12g}: candidate state produces non-finite independent physics")
            continue
        for contact in ("hammer", "damper"):
            force = float(s[contact+"_force_n"])
            expected = independent[contact+"_force_n"]
            residual = abs(force-expected)
            max_force_law = max(max_force_law, residual)
            if residual > tolerance("constitutive", expected):
                failures.append(f"t={time:.12g}: independent {contact} force-law residual {residual:.6g} N")
            if expected == 0 and force != 0:
                failures.append(f"t={time:.12g}: separated/clamped {contact} force is not exactly zero")
        if abs(float(s["energy_j"])-independent["energy_j"]) > tolerance("energy", independent["energy_j"], energy_scale=f.energy_scale):
            failures.append(f"t={time:.12g}: exported energy disagrees with independently assembled storage")
        loss = sum(float(s[k]) for k in ("modal_loss_j", "hammer_loss_j", "damper_loss_j"))
        balance = float(s["energy_j"])-initial_energy+loss-float(s["actuator_work_j"])-float(s["launch_work_j"])
        next_sum = sum_abs + abs(balance-previous_balance)
        if not math.isfinite(balance) or not math.isfinite(next_sum):
            failures.append(f"t={time:.12g}: candidate energy ledger overflows")
            continue
        sum_abs = next_sum
        previous_balance = balance
        max_cumulative = max(max_cumulative, abs(balance))
        for key in ("modal_loss_j", "hammer_loss_j", "damper_loss_j"):
            if float(s[key]) < -1e-18:
                failures.append(f"t={time:.12g}: negative accumulated physical loss {key}")
        max_power = max(max_power, abs(independent["instantaneous_power_residual_w"]))
        if "modal_acceleration_m_per_s2" in s and "hammer_acceleration_m_per_s2" in s:
            # Substituting exported acceleration makes this a check of candidate
            # dynamics, not just an algebraic identity of our own force law.
            residual = independent["instantaneous_power_residual_w"]
            residual += sum(m*v*(a-b) for m, v, a, b in zip(f.mass, s["v_m_per_s"], s["modal_acceleration_m_per_s2"], independent["modal_acceleration_m_per_s2"]))
            residual += f.hammer_mass*s["hammer_velocity_m_per_s"]*(s["hammer_acceleration_m_per_s2"]-independent["hammer_acceleration_m_per_s2"])
            if not math.isfinite(residual):
                failures.append(f"t={time:.12g}: candidate instantaneous power residual is non-finite")
            else:
                max_power = max(max_power, abs(residual))
        if name in {"conservative_impact", "damped_impact", "forced_clamp"}:
            momentum = f.hammer_mass*s["hammer_velocity_m_per_s"]+f.mass[0]*s["v_m_per_s"][0]
            target = f.hammer_mass*f.vh
            if abs(momentum-target) > tolerance("momentum", target):
                failures.append(f"t={time:.12g}: free-mass momentum error")
    if not all("modal_acceleration_m_per_s2" in s and "hammer_acceleration_m_per_s2" in s for s in samples):
        missing.append("exported acceleration for independent instantaneous power residual")
    cumulative_limit = 2e-9+5e-5*f.energy_scale
    interval_limit = 4e-9+2e-4*f.energy_scale
    if max_cumulative > cumulative_limit or sum_abs > interval_limit:
        failures.append("sample-interval/cumulative energy budget exceeded")
    diagnostics = run.get("diagnostics", {})
    for key, limit in (("max_abs_cumulative_residual_j", cumulative_limit), ("sum_abs_interval_residual_j", interval_limit)):
        if key not in diagnostics:
            missing.append("accepted-interval "+key)
        elif not math.isfinite(float(diagnostics[key])) or float(diagnostics[key]) > limit:
            failures.append(key+" exceeds frozen energy budget")
    actual_events = canonical_events(run)
    expected_events = oracle.events
    if [(e["contact"], e["kind"]) for e in actual_events] != [(e["contact"], e["kind"]) for e in expected_events]:
        failures.append("event order/classification differs from independent oracle: candidate="+
                        str([(e["contact"], e["kind"]) for e in actual_events])+" oracle="+
                        str([(e["contact"], e["kind"]) for e in expected_events]))
    for actual, expected in zip(actual_events, expected_events):
        time = float(actual["time_s"])
        if abs(time-expected["time_s"]) > tolerance("time", expected["time_s"]):
            failures.append("event timing differs from independent root")
        if "bracket_lo_s" not in actual or "bracket_hi_s" not in actual:
            missing.append("event root bracket")
        else:
            lo, hi = float(actual["bracket_lo_s"]), float(actual["bracket_hi_s"])
            if not all(math.isfinite(x) for x in (lo, hi)) or not lo <= time <= hi or hi-lo > float(run["event_tolerance_s"])*(1+1e-9):
                failures.append("invalid event bracket or width above declared tolerance")
        if name.startswith("grazing") and actual["kind"] != "relaunch":
            if "gap_uncertainty_m" not in actual:
                missing.append("grazing event gap uncertainty")
            elif not 0 <= float(actual["gap_uncertainty_m"]) < 1e-12:
                failures.append("grazing gap uncertainty exceeds 1e-12 m")
    if name == "grazing_contact":
        target = oracle.sample(f.duration)["hammer_impulse_ns"]
        value = float(samples[-1]["hammer_impulse_ns"])
        if value <= 0 or abs(value-target) > 1e-11+0.005*abs(target):
            failures.append("short-contact positive impulse/tight discrepancy gate failed")
    if name in {"grazing_miss", "grazing_tangent"} and float(samples[-1]["hammer_impulse_ns"]) != 0:
        failures.append("miss/tangent must have exactly zero hammer impulse")
    if f.relaunch:
        work = oracle.sample(f.duration)["launch_work_j"]
        if abs(float(samples[-1]["launch_work_j"])-work) > 1e-12+1e-10*abs(work):
            failures.append("launch work exceeds frozen event-work budget")
        for index, (snapshot, (time, before_state, after_state)) in enumerate(zip(
                run.get("relaunch_snapshots", []), oracle.relaunch_states)):
            oracle_work = f.hammer_mass * (after_state[5]**2 - before_state[5]**2) / 2
            if "work_j" in snapshot and abs(snapshot["work_j"] - oracle_work) > 1e-12+1e-10*abs(oracle_work):
                failures.append(f"relaunch_snapshots[{index}].work_j differs from independent action work")
            for side, reference_state in (("before", before_state), ("after", after_state)):
                reference = observe(f, time, reference_state)
                _, errors = compare_samples(snapshot[side], reference, f)
                failures.extend(f"relaunch_snapshots[{index}].{side}: {error}" for error in errors)
                ledger = reference["launch_work_j"]
                if abs(snapshot[side]["launch_work_j"] - ledger) > 1e-12+1e-10*abs(ledger):
                    failures.append(f"relaunch_snapshots[{index}].{side} launch ledger differs from independent action work")
        if len(oracle.relaunch_states) != len(f.relaunch):
            raise RuntimeError("Independent oracle omitted frozen action states")
    # The current export contains sampled diagnostic maxima only. A complete
    # future extrema audit must bracket stationary points independently; the
    # mere presence of a dictionary named "peaks" is not that evidence.
    missing.append("independently located peak compression and force (extrema audit not yet implemented)")
    return {"fixture": name, "source": run.get("source"), "method": oracle.method,
            "maximum_step_s": run["maximum_step_s"], "event_tolerance_s": run["event_tolerance_s"],
            "status": "fail" if failures else "incomplete" if missing else "pass",
            "worst_budget_fraction": worst, "maximum_sample_cumulative_residual_j": max_cumulative,
            "sum_absolute_sample_interval_residual_j": sum_abs,
            "maximum_instantaneous_power_residual_w": max_power,
            "maximum_constitutive_residual_n": max_force_law,
            "oracle_events": expected_events, "candidate_events": actual_events,
            "failures": failures[:80], "failure_count": len(failures), "missing": sorted(set(missing))}


def compare_runs(coarse: dict[str, Any], fine: dict[str, Any], *, factor: float = 1.0,
                 structural: bool = False) -> tuple[dict[str, float], list[str]]:
    f = fixtures()[fine["fixture"]]
    lookup = {round(float(s["time_s"]), 14): s for s in fine["samples"]}
    worst: dict[str, float] = {}
    errors: list[str] = []
    common = 0
    for sample in coarse["samples"]:
        time = round(float(sample["time_s"]), 14)
        if time not in lookup:
            continue
        reference = lookup[time]
        ratios, failures = compare_samples(sample, reference, f, factor=factor,
                                          structural=structural, actual_scaled=structural)
        common += 1
        for key, ratio in ratios.items():
            worst[key] = max(worst.get(key, 0.0), ratio)
        errors.extend(failures[:3])
    if common < math.floor(f.duration/20e-6):
        errors.append("Missing physical-time common 20 us waveform grid")
    a, b = canonical_events(coarse), canonical_events(fine)
    if [(e["contact"], e["kind"]) for e in a] != [(e["contact"], e["kind"]) for e in b]:
        errors.append("event classification/order changes under refinement")
    else:
        ratios = [abs(float(x["time_s"])-float(y["time_s"]))/tolerance("time", float(y["time_s"]), factor=factor, structural=structural)
                  for x, y in zip(a, b)]
        worst["event_time_s"] = max(ratios, default=0)
        if worst["event_time_s"] > 1:
            errors.append("event timing refinement exceeds budget")
    return worst, errors


def refinement_settings(step: float, event: float, scale: float = 1.0) -> tuple[float, ...]:
    """Complete declared solver/output settings in RUN_SETTING_FIELDS order."""
    return (step, event, OUTPUT_STEP, *(value * scale for value in LOCAL_TOLERANCES.values()))


def refinement_step_levels(extra_step_levels: int = 0) -> tuple[float, ...]:
    """An operator-declared bounded policy; booleans are not integer policies."""
    if type(extra_step_levels) is not int or extra_step_levels not in (0, 1, 2):
        raise ValueError("extra_step_levels must be an integer choice of 0, 1, or 2")
    return STEP_LEVELS + EXTRA_STEP_LEVELS[:extra_step_levels]


def refinement_slots(extra_step_levels: int = 0) -> dict[tuple[float, ...], list[str]]:
    """Retain the original eight slots, then add only the declared extension.

    An extension includes baseline step runs and the three frozen event/local
    levels at its own finest step. Depth two does not reuse depth one's extra
    event/local axes. Original slot insertion order is deliberately unchanged.
    """
    steps = refinement_step_levels(extra_step_levels)
    slots: dict[tuple[float, ...], list[str]] = {}
    axes = [
        ("maximum_step", [refinement_settings(h, EVENT_LEVELS[-1]) for h in STEP_LEVELS]),
        ("event_tolerance", [refinement_settings(STEP_LEVELS[-1], eta) for eta in EVENT_LEVELS]),
        ("local_tolerance", [refinement_settings(STEP_LEVELS[-1], EVENT_LEVELS[-1], scale)
                             for scale in LOCAL_SCALES]),
    ]
    if extra_step_levels:
        axes.extend([
            ("maximum_step", [refinement_settings(h, EVENT_LEVELS[-1]) for h in steps[len(STEP_LEVELS):]]),
            ("event_tolerance", [refinement_settings(steps[-1], eta) for eta in EVENT_LEVELS]),
            ("local_tolerance", [refinement_settings(steps[-1], EVENT_LEVELS[-1], scale)
                                 for scale in LOCAL_SCALES]),
        ])
    for axis, settings in axes:
        for slot in settings:
            slots.setdefault(slot, []).append(axis)
    return slots


def refinement_slot_id(name: str, settings: tuple[float, ...]) -> str:
    scale = settings[3] / LOCAL_TOLERANCES["relative_tolerance"]
    return f"{name}:h={settings[0]:.12g}:eta={settings[1]:.12g}:local={scale:.12g}"


def refinement_matrix(extra_step_levels: int = 0, names: list[str] | None = None) -> dict[str, Any]:
    """Hash the declared complete settings and fixtures, never observed minima."""
    slots = refinement_slots(extra_step_levels)
    names = sorted(set(fixtures() if names is None else names))
    settings = [{"fixture": name, "settings": dict(zip(RUN_SETTING_FIELDS, slot))}
                for name in names for slot in sorted(slots)]
    payload = json.dumps(settings, sort_keys=True, separators=(",", ":"),
                         allow_nan=False).encode("utf-8")
    return {"id": f"contact-refinement-20261123-extra-{extra_step_levels}",
            "settings_sha256": hashlib.sha256(payload).hexdigest(),
            "fixtures": names, "fixture_count": len(names),
            "slots_per_fixture": len(slots), "run_count": len(settings)}


def refinement_policy(extra_step_levels: int = 0) -> dict[str, Any]:
    steps = refinement_step_levels(extra_step_levels)
    return {"extra_step_levels": extra_step_levels,
            "maximum_step_levels_s": list(steps), "event_levels_s": list(EVENT_LEVELS),
            "local_tolerance_scales": list(LOCAL_SCALES),
            "local_tolerances_at_scale_one": dict(LOCAL_TOLERANCES),
            "output_step_s": OUTPUT_STEP, "original_finest_step_s": STEP_LEVELS[-1],
            "selected_finest_step_s": steps[-1],
            "acceptance_source": "docs/specs/CONTACT_ACCEPTANCE_20261123.md",
            "extension_source": "docs/specs/CONTACT_REFINEMENT_EXTENSION_20261123.md"}


def select_refinement_runs(runs: list[dict[str, Any]], extra_step_levels: int = 0) -> tuple[
        dict[str, dict[tuple[float, ...], dict[str, Any]]], list[str], list[str], list[dict[str, Any]]]:
    """Select frozen slots after preflight, rejecting conflicting same-slot data.

    A source path is provenance, not scientific content. Everything else in a
    run participates in its canonical hash. Exact repeats retain all sources;
    their representative is deterministic and never chosen by input order.
    """
    declared = sorted(refinement_slots(extra_step_levels))
    groups: dict[tuple[str, tuple[float, ...]], list[dict[str, Any]]] = {}
    by_name: dict[str, dict[tuple[float, ...], dict[str, Any]]] = {}
    missing, failures, selection = [], [], []
    for run in runs:
        name = run["fixture"]
        by_name.setdefault(name, {})
        absent = [key for key in RUN_SETTING_FIELDS if key not in run]
        if absent:
            missing.append(name + ": complete refinement settings (missing " + ", ".join(absent) + ")")
            continue
        actual = tuple(float(run[key]) for key in RUN_SETTING_FIELDS)
        matches = [slot for slot in declared if all(
            math.isclose(value, expected, rel_tol=1e-12, abs_tol=0)
            for value, expected in zip(actual, slot))]
        if len(matches) != 1:
            label = json.dumps(dict(zip(RUN_SETTING_FIELDS, actual)), sort_keys=True)
            failures.append(name + ": " + ("undeclared" if not matches else "ambiguous") +
                            " refinement settings " + label)
            continue
        groups.setdefault((name, matches[0]), []).append(run)
    for (name, settings), copies in sorted(groups.items()):
        variants: dict[str, list[dict[str, Any]]] = {}
        for run in copies:
            scientific = {key: value for key, value in run.items() if key != "source"}
            payload = json.dumps(scientific, sort_keys=True, separators=(",", ":"),
                                 ensure_ascii=False, allow_nan=False).encode("utf-8")
            digest = hashlib.sha256(payload).hexdigest()
            variants.setdefault(digest, []).append(run)
        slot_id = refinement_slot_id(name, settings)
        selection.append({"slot": slot_id, "settings": dict(zip(RUN_SETTING_FIELDS, settings)),
                          "selected": len(variants) == 1,
                          "variants": [{"scientific_content_sha256": digest, "copies": len(values),
                                        "sources": sorted({run["source"] for run in values if "source" in run})}
                                       for digest, values in sorted(variants.items())]})
        if len(variants) != 1:
            failures.append(name + ": conflicting duplicate refinement slot " + slot_id +
                            " (scientific content hashes " + ", ".join(sorted(variants)) + ")")
            continue
        by_name[name][settings] = min(copies, key=lambda run: run.get("source", ""))
    return by_name, sorted(set(missing)), sorted(set(failures)), selection


def check_refinement_selection(by_name: dict[str, dict[tuple[float, ...], dict[str, Any]]],
                               missing: list[str], failures: list[str],
                               selection: list[dict[str, Any]],
                               steps: tuple[float, ...]) -> dict[str, Any]:
    # Copy mutable diagnostics so evaluating the original matrix cannot inherit
    # an extension's numerical failures, or overwrite its frozen outcome.
    missing, failures = list(missing), list(failures)
    comparisons: dict[str, Any] = {}
    if failures:
        return {"status": "fail", "failures": failures, "missing": missing,
                "comparisons": {}, "selection": selection}
    for name, group in sorted(by_name.items()):
        # Hold local and event tolerances fixed on the h axis.
        levels = [group.get(refinement_settings(step, EVENT_LEVELS[-1])) for step in steps]
        if any(r is None for r in levels):
            count = "four" if steps == STEP_LEVELS else str(len(steps))
            missing.append(name+": "+count+" maximum-step refinements")
        else:
            diffs = []
            for coarse, fine in zip(levels, levels[1:]):
                delta, errors = compare_runs(coarse, fine)
                diffs.append(delta)
                # The original two-successive-comparison rule applies at the
                # declared finest step. Every earlier difference is retained.
                if len(diffs) >= len(levels) - 2:
                    failures.extend(name+": "+e for e in errors[:10])
            # Roundoff resolution is computed in physical units, separately
            # from the frozen observable budget. No acceptance budget widens.
            plateaus = []
            for key, last in diffs[-1].items():
                previous = diffs[-2].get(key, 0.0)
                if key == "event_time_s":
                    reference_scale = fixtures()[name].duration
                    resolution = 128*math.ulp(reference_scale)/BUDGETS["time"][0]
                else:
                    values = [fields(s)[key] for s in levels[-1]["samples"] if key in fields(s)]
                    kind = values[0][1]
                    declared_scale = {"position": 1e-3, "velocity": 1.0, "force": 10.0,
                                      "impulse": 0.004, "energy": fixtures()[name].energy_scale,
                                      "voltage": 1.0}[kind]
                    reference_scale = max([declared_scale, *[abs(value) for value, _ in values]])
                    floor = tolerance(kind, 0.0, energy_scale=fixtures()[name].energy_scale)
                    resolution = 128*math.ulp(reference_scale)/floor
                resolvable = max(last, previous) > resolution
                if resolvable and last > 0.8*previous:
                    failures.append(f"{name}: final refinement for {key} not decreasing (fractions {previous:.5g}, {last:.5g})")
                elif not resolvable:
                    plateaus.append(key)
            comparisons[name+":step"] = {"differences_as_budget_fractions": diffs, "below_resolution": plateaus}
        event_runs = [group.get(refinement_settings(steps[-1], eta)) for eta in EVENT_LEVELS]
        if any(r is None for r in event_runs):
            missing.append(name+": three event-bracket refinements")
        else:
            comparisons[name+":event"] = []
            for coarse, fine in zip(event_runs, event_runs[1:]):
                delta, errors = compare_runs(coarse, fine, factor=0.1)
                comparisons[name+":event"].append(delta)
                failures.extend(name+": event tightening: "+e for e in errors[:10])
        local = [group.get(refinement_settings(steps[-1], EVENT_LEVELS[-1], scale))
                 for scale in LOCAL_SCALES]
        if any(r is None for r in local):
            missing.append(name+": three separate local-integration tolerances (nonlinear solve axis inapplicable to RK4)")
        else:
            for coarse, fine in zip(local, local[1:]):
                delta, errors = compare_runs(coarse, fine, factor=0.1)
                comparisons.setdefault(name+":local", []).append(delta)
                failures.extend(name+": local tightening: "+e for e in errors[:10])
    for name in ("supported_impact", "moving_damper", "evolved_relaunch"):
        settings = refinement_settings(steps[-1], EVENT_LEVELS[-1])
        fine_a = by_name.get(name+"_scaled", {}).get(settings)
        fine_b = by_name.get(name, {}).get(settings)
        if fine_a is None or fine_b is None:
            missing.append(name+": signed coordinate rescaling")
        else:
            delta, errors = compare_runs(fine_a, fine_b, structural=True)
            comparisons[name+":scaling"] = delta
            failures.extend(name+": coordinate invariance: "+e for e in errors[:10])
    missing.extend("absent fixture: "+name for name in fixtures() if name not in by_name)
    return {"status": "fail" if failures else "incomplete" if missing else "pass",
            "failures": failures, "missing": sorted(set(missing)), "comparisons": comparisons,
            "selection": selection,
            "nonlinear_solve_refinement": "inapplicable: explicit RK4; separate local integration axis required"}


def extended_refinement_report(extra_step_levels: int, original: dict[str, Any],
                               extended: dict[str, Any]) -> dict[str, Any]:
    """Retain independently named results and explicit missing matrix slots."""
    for depth, result in ((0, original), (extra_step_levels, extended)):
        selected = {entry["slot"] for entry in result.get("selection", []) if entry["selected"]}
        missing_slots = [{"slot": refinement_slot_id(name, slot), "fixture": name,
                          "settings": dict(zip(RUN_SETTING_FIELDS, slot))}
                         for name in sorted(fixtures()) for slot in sorted(refinement_slots(depth))
                         if refinement_slot_id(name, slot) not in selected]
        result["matrix"] = refinement_matrix(depth)
        result["missing_slots"] = missing_slots
        if missing_slots and depth:
            # Retained original event/local slots are required even though the
            # extension compares those axes at its newly declared finest step.
            result["missing"] = sorted(set(result["missing"] + [
                "missing refinement slot: " + slot["slot"] for slot in missing_slots]))
            if result["status"] == "pass":
                result["status"] = "incomplete"
    return {**extended, "policy": refinement_policy(extra_step_levels),
            "original_frozen_result": original, "extended_result": extended}


def refinement_gates(runs: list[dict[str, Any]], extra_step_levels: int = 0) -> dict[str, Any]:
    steps = refinement_step_levels(extra_step_levels)
    # Partition only the scope of the separately reported original gate. Every
    # selected run still passes the complete-settings selector. Unidentifiable
    # step metadata is retained conservatively so it cannot hide a bad original
    # run; identifiable finer evidence must not alter the original's outcome.
    original_runs = [run for run in runs if not isinstance(run, dict) or
                     not finite_number(run.get("maximum_step_s")) or
                     any(math.isclose(run["maximum_step_s"], step, rel_tol=1e-12, abs_tol=0)
                         for step in STEP_LEVELS)] if extra_step_levels else []
    malformed = sorted({f"{run.get('fixture', '<unknown fixture>') if isinstance(run, dict) else '<invalid run>'}: {failure}"
                        for run in runs for failure in validate_run_input(run)})
    contradictory = [] if malformed else sorted({f"{run['fixture']}: {failure}" for run in runs
                                                 for failure in validate_action_evidence(run)[0]})
    if malformed or contradictory:
        result = {"status": "fail", "failures": (malformed or contradictory)[:80],
                  "missing": [], "comparisons": {}}
        if extra_step_levels:
            return extended_refinement_report(extra_step_levels, refinement_gates(original_runs), result)
        return result
    by_name, missing, failures, selection = select_refinement_runs(runs, extra_step_levels)
    result = check_refinement_selection(by_name, missing, failures, selection, steps)
    if not extra_step_levels:
        return result
    original_slots = refinement_slots()
    if missing or failures:
        # Re-check only original-grid evidence when input diagnostics need
        # attribution. A conflicting finer duplicate fails the extension while
        # leaving the original result bound to its own evidence.
        original = refinement_gates(original_runs)
    else:
        original_groups = {name: {slot: run for slot, run in group.items() if slot in original_slots}
                           for name, group in by_name.items() if any(slot in original_slots for slot in group)}
        original_selection = [entry for entry in selection
                              if tuple(entry["settings"][key] for key in RUN_SETTING_FIELDS) in original_slots]
        original = check_refinement_selection(original_groups, [], [], original_selection, STEP_LEVELS)
    return extended_refinement_report(extra_step_levels, original, result)


def require_self_check(condition: bool, description: str) -> None:
    """Oracle validation must run even when Python assertion removal is enabled."""
    if not condition:
        raise RuntimeError("Oracle self-check failed: " + description)


def self_check() -> dict[str, Any]:
    np, _, expm, quad = numerics()
    from scipy.optimize import brentq
    checks: dict[str, Any] = {}
    f = fixtures()["conservative_impact"]
    exact = exact_free(f)
    end = exact.sample(f.duration)
    require_self_check(abs(end["hammer_impulse_ns"]-0.0032) < 1e-17,
                       "conservative impact hammer impulse must be within 1e-17 N s of 0.0032 N s")
    require_self_check(abs(end["hammer_velocity_m_per_s"]-0.6) < 1e-15,
                       "conservative impact hammer velocity must be within 1e-15 m/s of 0.6 m/s")
    require_self_check(abs(end["v_m_per_s"][0]-1.6) < 1e-15,
                       "conservative impact modal velocity must be within 1e-15 m/s of 1.6 m/s")
    require_self_check(abs(end["energy_j"]-0.004) < 1e-17,
                       "conservative impact energy must be within 1e-17 J of 0.004 J")
    checks["conservative_analytic"] = {"separation_s": SEPARATION, "impulse_ns": end["hammer_impulse_ns"]}
    phi = lambda w: 2*w-math.log1p(2*w)
    rebound = brentq(lambda w: phi(w)-phi(1), -0.499999999, -1e-12, xtol=1e-15)
    damped = radau_oracle(fixtures()["damped_impact"])
    end = damped.sample(damped.fixture.duration)
    measured = end["hammer_velocity_m_per_s"]-end["v_m_per_s"][0]
    require_self_check(abs(measured-rebound) < 1e-9,
                       "damped impact rebound must be within 1e-9 m/s of the scalar root")
    expected_loss = MU*(1-rebound*rebound)/2
    require_self_check(abs(end["hammer_loss_j"]-expected_loss) < 1e-12,
                       "damped impact loss must be within 1e-12 J of the scalar energy balance")
    checks["damped_radau_vs_scalar"] = {"relative_rebound_m_per_s": measured, "analytic_m_per_s": rebound,
                                         "loss_j": end["hammer_loss_j"], "rhs_evaluations": damped.evaluation_count}
    clipped = exact_free(fixtures()["forced_clamp"]).sample(0.0002)
    require_self_check(clipped["hammer_impulse_ns"] == 0 and abs(clipped["hammer_loss_j"]-0.0001) < 1e-18,
                       "forced clamp must have zero impulse and loss within 1e-18 J of 0.0001 J")
    checks["forced_clamp"] = {"loss_j": clipped["hammer_loss_j"]}
    # Independent always-compressed matrix exponential with modal damping.
    p = fixtures()["preload_linear_plus"]
    mass, stiffness, damping = np.diag(p.mass), np.diag(p.stiffness()), np.diag(p.damping())
    b = np.array(p.bd)
    ke = stiffness+p.kd*np.outer(b, b)
    equilibrium = np.linalg.solve(ke, p.kd*b*p.face_offset)
    a = np.block([[np.zeros((2, 2)), np.eye(2)], [-np.linalg.solve(mass, ke), -np.linalg.solve(mass, damping)]])
    z0 = np.r_[np.array(p.q)-equilibrium, p.v]
    z = expm(a*p.duration)@z0
    oracle = radau_oracle(p)
    end = oracle.sample(p.duration)
    require_self_check(np.max(np.abs(np.array(end["q_m"])-equilibrium-z[:2])) < 2e-14,
                       "preload position must be within 2e-14 m of the matrix exponential")
    require_self_check(np.max(np.abs(np.array(end["v_m_per_s"])-z[2:])) < 2e-11,
                       "preload velocity must be within 2e-11 m/s of the matrix exponential")
    loss = quad(lambda t: float((expm(a*t)@z0)[2:]@damping@(expm(a*t)@z0)[2:]),
                0, p.duration, epsabs=1e-18, epsrel=1e-11)[0]
    require_self_check(abs(loss-end["modal_loss_j"]) < 1e-16,
                       "preload modal loss must be within 1e-16 J of independent quadrature")
    energy = float(z0[:2]@ke@z0[:2]/2)
    lower_gap = float(b@equilibrium-p.face_offset-math.sqrt(2*energy*(b@np.linalg.solve(ke, b))))
    require_self_check(lower_gap > 0,
                       "preload energy bound does not certify strictly positive compression")
    checks["preload_expm_vs_radau"] = {"equilibrium_q_m": equilibrium.tolist(),
                                        "certified_minimum_compression_m": lower_gap,
                                        "quadrature_loss_j": loss,
                                        "radau_loss_j": end["modal_loss_j"]}
    coarse = radau_oracle(fixtures()["preload_plus"], tight=False)
    fine = radau_oracle(fixtures()["preload_plus"], tight=True)
    differences = [float(np.max(np.abs(coarse.state(float(t))-fine.state(float(t)))))
                   for t in np.linspace(0, p.duration, 1001)]
    require_self_check(all(math.isfinite(value) for value in differences),
                       "independent Radau refinement differences must be finite at every sample")
    delta = max(differences)
    require_self_check(delta < 1e-10,
                       "independent Radau refinement must differ by less than 1e-10 in state or ledger coordinates")
    checks["independent_radau_refinement"] = {"maximum_state_or_ledger_difference": delta,
                                             "coarse_maximum_step_s": 5e-6, "fine_maximum_step_s": 2.5e-6}
    return {"status": "pass", "scope": "oracle self-check only; no candidate acceptance", "checks": checks}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--candidate", type=Path, action="append", default=[], help="candidate JSON/CSV; repeat for sweeps")
    parser.add_argument("--output", type=Path, help="write machine-readable audit result")
    parser.add_argument("--self-check", action="store_true", help="validate independent analytic/Radau/expm oracles")
    parser.add_argument("--list-fixtures", action="store_true")
    parser.add_argument("--extra-step-levels", type=int, choices=(0, 1, 2), default=0,
                        help="explicit uniform refinement extension; default 0 audits the original frozen grid")
    args = parser.parse_args()
    if args.list_fixtures:
        print(json.dumps({name: {"duration_s": f.duration, "energy_scale_j": f.energy_scale}
                          for name, f in fixtures().items()}, indent=2))
        return 0
    if not args.self_check and not args.candidate:
        parser.error("specify --candidate, --self-check, or --list-fixtures")
    try:
        report: dict[str, Any] = {"scope": "frozen p=1 synthetic reference; no measured fidelity claim",
                                  "acceptance_source": "docs/specs/CONTACT_ACCEPTANCE_20261123.md",
                                  "refinement_policy": refinement_policy(args.extra_step_levels)}
        if args.self_check:
            report["oracle_self_check"] = self_check()
        if args.candidate:
            runs = [run for path in args.candidate for run in read_runs(path)]
            report["runs"] = [audit_run(run) for run in runs]
            report["refinement"] = refinement_gates(runs, args.extra_step_levels)
            statuses = [r["status"] for r in report["runs"]]+[report["refinement"]["status"]]
            report["status"] = "fail" if "fail" in statuses else "incomplete" if "incomplete" in statuses else "pass"
        else:
            report["status"] = "pass"
    except (ValueError, KeyError, TypeError, OSError, RuntimeError, AssertionError) as exc:
        report = {"status": "fail", "error": str(exc) or type(exc).__name__}
    payload = json.dumps(report, indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(payload+"\n", encoding="utf-8")
        print(json.dumps({"status": report["status"], "report": str(args.output)}))
    else:
        print(payload)
    return 1 if report["status"] == "fail" else 2 if report["status"] == "incomplete" else 0


if __name__ == "__main__":
    sys.exit(main())
