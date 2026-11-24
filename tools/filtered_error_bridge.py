"""Offline sampled/continuous error bookkeeping for the independent reference.

This module imports only Python's standard library until a numerical function
is called. NumPy and SciPy remain optional offline dependencies of
``validate_filtered_audio``. No production DSP, serialized filter coefficients,
coefficient generator, probe execution, file publication, or CLI is used here.

All voltages and model parameters are synthetic. The telescoping identity and
its closure are bookkeeping checks, not alias-only or instrument-fidelity gates.
"""
from __future__ import annotations

from bisect import bisect_left
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any, Iterable


_SPEC = importlib.util.spec_from_file_location(
    "rhodes_filtered_audio_shared_for_bridge",
    Path(__file__).with_name("validate_filtered_audio.py"))
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("Cannot load the independent filtered-audio reference")
reference = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = reference
_SPEC.loader.exec_module(reference)

Case, Event = reference.Case, reference.Event
numerical_libraries = reference.numerical_libraries
SUPPORT_FRAMES = reference.SUPPORT_FRAMES


def _factor(value: int) -> int:
    if type(value) is not int or value not in (1, 2, 4):
        raise ValueError("Internal factor must be 1, 2 or 4")
    return value


def _positive(value: float, name: str) -> float:
    if isinstance(value, (bool, str, bytes)):
        raise ValueError(f"{name} must be finite and positive")
    try:
        result = float(value)
    except (ValueError, TypeError, OverflowError) as exc:
        raise ValueError(f"{name} must be finite and positive") from exc
    if not math.isfinite(result) or result <= 0.0:
        raise ValueError(f"{name} must be finite and positive")
    return result


def _vector(value: Any, name: str) -> Any:
    np, _ = numerical_libraries()
    try:
        if np.iscomplexobj(value):
            raise ValueError(f"{name} must be real")
        result = np.asarray(value, dtype=np.float64)
    except (ValueError, TypeError, OverflowError) as exc:
        raise ValueError(f"{name} must be a finite real vector") from exc
    if result.ndim != 1 or not result.size or not np.all(np.isfinite(result)):
        raise ValueError(f"{name} must be a nonempty finite real vector")
    return result


def _epochs(value: Iterable[tuple[int, int]], frames: int) -> tuple[tuple[int, int], ...]:
    try:
        result = tuple(tuple(pair) for pair in value)
    except TypeError as exc:
        raise ValueError("Epochs must partition the host-frame record") from exc
    cursor = 0
    for pair in result:
        if (len(pair) != 2 or any(type(item) is not int for item in pair)
                or pair[0] != cursor or not pair[0] < pair[1] <= frames):
            raise ValueError("Epochs must be contiguous integer [start, stop) pairs")
        cursor = pair[1]
    if not result or cursor != frames:
        raise ValueError("Epochs must partition the complete host-frame record")
    return result


def _sources(right: Any, left: Any, factor: int,
             epochs: Iterable[tuple[int, int]], normalizer: float) -> tuple[Any, ...]:
    factor = _factor(factor)
    normalizer = _positive(normalizer, "Continuous kernel normalizer")
    right, left = _vector(right, "Right source"), _vector(left, "Left source")
    if right.shape != left.shape or len(right) % factor:
        raise ValueError("Source vectors must have equal length divisible by factor")
    epochs = _epochs(epochs, len(right) // factor)
    # The left side of an epoch is zero history, not the physical old epoch.
    # Copy before canonicalizing so callers' arrays are never changed.
    left = left.copy()
    for start, _ in epochs:
        left[start * factor] = 0.0
    return right, left, factor, epochs, normalizer


def _loaded_state(q: Any, v: Any, key: int) -> float:
    """The reference's local nonlinear pickup, then its fixed loaded branch."""
    np, _ = numerical_libraries()
    model = reference.MODEL
    observation = np.asarray(model["pickup_observation"])
    position, speed = float(np.sum(q * observation)), float(np.sum(v * observation))
    a1, a2, a3 = model["flux_polynomial_coefficients"]
    pickup = -(a1 + 2.0 * a2 * position + 3.0 * a3 * position * position) * speed
    return pickup * (model["loaded_harp_gain_numerator"]
                     / model["loaded_harp_gain_denominator"] / (4 if key < 4 else 3))


def _state_before(trajectory: Any, offset: int) -> tuple[Any, Any]:
    """Propagate the strictly preceding analytic segment; never use epsilon."""
    np, _ = numerical_libraries()
    index = bisect_left([segment.start_frame for segment in trajectory.segments], offset) - 1
    if index < 0:
        return np.zeros(3), np.zeros(3)
    segment = trajectory.segments[index]
    return reference.modal_solution(segment.q0, segment.v0, trajectory.omega0,
                                    segment.beta, (offset - segment.start_frame) / trajectory.rate)


def one_sided_source(case: Any) -> tuple[Any, Any, list[dict[str, Any]]]:
    """Return ``(right, left, event_rows)`` on the case's exact internal grid.

    ``right[i]`` and ``left[i]`` are loaded volts at host time ``i/L``. At an
    event, each key's left state is obtained by ``modal_solution`` from its
    strictly preceding ``KeyTrajectory`` segment. At other grid points the
    already evaluated right value is reused. Every reset epoch (including time
    zero) has left value zero. Each local nonlinear pickup is loaded before
    summing keys; no finite-difference limit or production path is used.

    One metadata row is retained per input event, in producer order. Its
    ``jump_volts`` is the effective source jump attributed to that row.
    ``physical_jump_volts`` records the physical instantaneous change. At an
    offset with resets, all rows through the last reset have effective jump
    zero: their history was erased. Later strikes contribute from a zero epoch
    baseline. Thus reset erasure is not mislabelled as a negative old-source
    impulse. ``internal_index`` and ``jump_volts`` feed ``endpoint_attribution``.
    """
    np, _ = numerical_libraries()
    # Revalidate duck-typed inputs using the unchanged independent contract.
    checked = Case(case.id, case.family, case.sample_rate_hz, case.frames,
                   tuple(case.keys), case.intensity, tuple(case.events))
    factor = reference.oversampling_factor(checked.sample_rate_hz)
    positions = np.arange(checked.frames * factor, dtype=np.float64) / factor
    right, left = np.zeros(len(positions)), np.zeros(len(positions))
    epochs = reference.reset_epochs(checked.frames, checked.events)
    epoch_starts = {start for start, _ in epochs}
    trajectories = {}
    for key in checked.keys:
        trajectory = reference.KeyTrajectory(checked.sample_rate_hz, key, checked.events)
        trajectories[key] = trajectory
        local_right = trajectory.loaded_volts(positions)
        local_left = local_right.copy()
        for segment in trajectory.segments:
            offset = segment.start_frame
            if offset in epoch_starts:
                local_left[offset * factor] = 0.0
            else:
                q, v = _state_before(trajectory, offset)
                local_left[offset * factor] = _loaded_state(q, v, key)
        right += local_right
        left += local_left

    rows: list[dict[str, Any]] = []
    cursor = 0
    coupling = np.asarray(reference.MODEL["strike_coupling"])
    maximum_change = reference.MODEL["maximum_velocity_change_m_per_s"]
    while cursor < len(checked.events):
        offset = checked.events[cursor].offset
        stop = cursor + 1
        while stop < len(checked.events) and checked.events[stop].offset == offset:
            stop += 1
        resets = [index for index in range(cursor, stop) if checked.events[index].type == "reset"]
        last_reset = resets[-1] if resets else -1
        states = {key: _state_before(trajectory, offset) for key, trajectory in trajectories.items()}
        loaded = {key: _loaded_state(*states[key], key) for key in checked.keys}
        epoch_start = next(start for start, end in epochs if start <= offset < end)
        for index in range(cursor, stop):
            event = checked.events[index]
            before = sum(loaded.values())
            physical_jump = 0.0
            if event.type == "reset":
                physical_jump = -before
                states = {key: (np.zeros(3), np.zeros(3)) for key in checked.keys}
                loaded = {key: 0.0 for key in checked.keys}
            elif event.type == "on" and event.value > 0.0:
                q, v = states[event.key]
                states[event.key] = q, v + maximum_change * event.value * coupling
                new_loaded = _loaded_state(*states[event.key], event.key)
                physical_jump = new_loaded - loaded[event.key]
                loaded[event.key] = new_loaded
            rows.append({
                "event_index": index, "offset_host_frames": offset,
                "internal_index": offset * factor, "type": event.type,
                "key": event.key, "value": float(event.value),
                "epoch_start_frame": epoch_start,
                "discarded_by_same_time_reset": index < last_reset,
                "physical_source_before_volts": before,
                "physical_source_after_volts": sum(loaded.values()),
                "physical_jump_volts": physical_jump,
                "jump_volts": physical_jump if index > last_reset else 0.0,
            })
        cursor = stop
    if not np.all(np.isfinite(right)) or not np.all(np.isfinite(left)):
        raise ArithmeticError("Nonfinite one-sided independent source")
    return right, left, rows


def trapezoid_reference(right: Any, left: Any, factor: int,
                        epochs: Iterable[tuple[int, int]], normalizer: float) -> Any:
    """Return host-frame T using exact interior-facing delay-cell endpoints.

    With ``h=1/L``, ``K[m]=kernel(m*h)`` and ``M=96*L``, each cell contributes
    ``h/(2*Z) * (K[m]*left[n*L-m] + K[m+1]*right[n*L-m-1])``.
    Histories are independently truncated at each contiguous ``[start, stop)``
    epoch, then sampled at phase ``[::L]``. Epoch-left values are interpreted as
    zero even if the supplied left vector retains a physical pre-reset value.
    Nonzero support endpoints are included. Inputs are not modified.
    """
    np, _ = numerical_libraries()
    right, left, factor, epochs, normalizer = _sources(right, left, factor, epochs, normalizer)
    taps = reference.kernel(np.arange(SUPPORT_FRAMES * factor + 1) / factor)
    right_taps = np.concatenate((np.zeros(1), taps[1:]))
    output = np.zeros(len(right) // factor)
    scale = 0.5 / factor / normalizer
    for start, stop in epochs:
        a, b = start * factor, stop * factor
        lpart = np.convolve(left[a:b], taps[:-1], mode="full")[:b - a]
        rpart = np.convolve(right[a:b], right_taps, mode="full")[:b - a]
        output[start:stop] = ((lpart + rpart) * scale)[::factor]
    if not np.all(np.isfinite(output)):
        raise ArithmeticError("Nonfinite trapezoid reference")
    return output


def endpoint_attribution(right: Any, left: Any, factor: int,
                         epochs: Iterable[tuple[int, int]], normalizer: float,
                         event_rows: Iterable[dict[str, Any]] | None = None
                         ) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Split N-T into finite-support endpoints and individual source jumps.

    Returns ``(arrays, rows)``. ``event_contributions_volts`` has shape
    ``(len(rows), host_frames)``; each row multiplies its effective jump by
    ``h*K[0]/Z`` at current time, ``h*K[m]/(2*Z)`` for interior delays, and zero
    at the far support endpoint or after the next reset. The far endpoint is
    already counted using its right-sided source in ``support_delayed_endpoint_volts``.

    Provided rows need finite ``jump_volts`` and integer ``internal_index``;
    their order and additional metadata are retained. With ``None``, one row
    per nonzero grid jump is synthesized. Actual input events, including rows
    with zero jump, should be supplied to preserve the unchanged event windows.
    Grid-jump versus row-attribution residuals remain explicit, so omitted rows
    or rounding differences cannot silently disappear from closure.
    """
    np, _ = numerical_libraries()
    right, left, factor, epochs, normalizer = _sources(right, left, factor, epochs, normalizer)
    frames, support = len(right) // factor, SUPPORT_FRAMES * factor
    taps = reference.kernel(np.arange(support + 1) / factor)
    scale, jumps = 0.5 / factor / normalizer, right - left
    if event_rows is None:
        event_rows = [{"event_index": None, "internal_index": int(index),
                       "jump_volts": float(jumps[index]), "type": "grid_jump"}
                      for index in np.flatnonzero(jumps)]
    rows = []
    for source_row in event_rows:
        if not isinstance(source_row, dict):
            raise ValueError("Event attribution rows must be dictionaries")
        row = dict(source_row)
        index, jump = row.get("internal_index"), row.get("jump_volts")
        if type(index) is not int or not 0 <= index < len(right):
            raise ValueError("Event internal_index must be an integer inside the source record")
        if isinstance(jump, (bool, str, bytes)):
            raise ValueError("Event jump_volts must be finite")
        try:
            jump = float(jump)
        except (ValueError, TypeError, OverflowError) as exc:
            raise ValueError("Event jump_volts must be finite") from exc
        if not math.isfinite(jump):
            raise ValueError("Event jump_volts must be finite")
        offset = index / factor
        if "offset_host_frames" in row and row["offset_host_frames"] != offset:
            raise ValueError("Event offset_host_frames disagrees with internal_index")
        start, stop = next(pair for pair in epochs if pair[0] * factor <= index < pair[1] * factor)
        row.update(internal_index=index, jump_volts=jump, offset_host_frames=offset,
                   epoch_start_frame=start, epoch_stop_frame_exclusive=stop)
        rows.append(row)

    current = scale * taps[0] * left[::factor]
    delayed, current_jumps, interior_jumps = (np.zeros(frames) for _ in range(3))
    for start, stop in epochs:
        a, b = start * factor, stop * factor
        current_jumps[start:stop] = 2.0 * scale * taps[0] * jumps[a:b:factor]
        interior_taps = np.concatenate((np.zeros(1), taps[1:-1]))
        interior_jumps[start:stop] = (
            np.convolve(jumps[a:b], interior_taps, mode="full")[:b - a:factor] * scale)
        if stop - start > SUPPORT_FRAMES:
            delayed[start + SUPPORT_FRAMES:stop] = (
                scale * taps[-1] * right[a:(stop - SUPPORT_FRAMES) * factor:factor])

    contributions, assigned_jumps = np.zeros((len(rows), frames)), np.zeros(len(right))
    for row_index, row in enumerate(rows):
        index, jump = row["internal_index"], row["jump_volts"]
        assigned_jumps[index] += jump
        first = (index + factor - 1) // factor
        stop = min(row["epoch_stop_frame_exclusive"], (index + support - 1) // factor + 1)
        output_indices = np.arange(first, stop)
        delay_indices = output_indices * factor - index
        weights = scale * taps[delay_indices]
        weights = np.where(delay_indices == 0, 2.0 * weights, weights)
        contributions[row_index, output_indices] = jump * weights
    contribution_sum = np.sum(contributions, axis=0)
    support_endpoints = current + delayed
    grid_events = current_jumps + interior_jumps
    arrays = {
        "source_jump_internal_volts": jumps,
        "support_current_endpoint_volts": current,
        "support_delayed_endpoint_volts": delayed,
        "support_endpoint_volts": support_endpoints,
        "event_current_endpoint_volts": current_jumps,
        "event_interior_volts": interior_jumps,
        "event_grid_contribution_volts": grid_events,
        "event_contributions_volts": contributions,
        "event_contributions_sum_volts": contribution_sum,
        "event_jump_attribution_residual_internal_volts": jumps - assigned_jumps,
        "event_contribution_residual_volts": grid_events - contribution_sum,
        "endpoint_event_direct_volts": support_endpoints + grid_events,
        "endpoint_event_attribution_volts": support_endpoints + contribution_sum,
    }
    if any(not np.all(np.isfinite(values)) for values in arrays.values()):
        raise ArithmeticError("Nonfinite endpoint/event attribution")
    return arrays, rows


def decompose_arrays(production: Any, same_rate: Any, continuous: Any,
                     right: Any, left: Any, factor: int,
                     epochs: Iterable[tuple[int, int]], normalizer: float, *,
                     sample_rate_hz: float = 1.0,
                     event_rows: Iterable[dict[str, Any]] | None = None
                     ) -> tuple[dict[str, Any], dict[str, Any]]:
    """Return retained arrays and metrics for P-C=(P-S)+(S-N)+(N-T)+(T-C).

    ``production``, ``same_rate`` and ``continuous`` are equal-sized host-frame
    P, S and Q64 C vectors. ``right``/``left`` contain exactly ``L`` internal
    samples per host frame. ``normalizer`` is Q64's existing Z, never a fitted
    gain. N is returned as ``normalization_volts = S*D/Z``, with
    ``D=(1/L)*fsum(kernel(m/L), m=0..96L)``. T is ``trapezoid_volts``.

    Returned arrays include copies of all five input vectors, the four named
    errors, their sum, P-C, telescoping residual, all endpoint/event attribution
    arrays, and separate algebraic/attribution residuals. Event matrices use
    event rows by output frames. Metrics reuse the independent auditor's peak,
    maximum frame/time, signed mean and RMS definitions; they cover the whole
    record, original inclusive [event,event+96] windows, their union/complement,
    each event contribution, and frame 816 when present. Internal-grid residual
    metrics use ``sample_rate_hz*L``. No acceptance limits or fitted alignment
    are introduced, and closure does not change any original comparison gate.
    """
    np, _ = numerical_libraries()
    right, left, factor, epochs, normalizer = _sources(right, left, factor, epochs, normalizer)
    rate = _positive(sample_rate_hz, "Host sample rate")
    production = _vector(production, "Production output")
    same_rate = _vector(same_rate, "Same-rate reference")
    continuous = _vector(continuous, "Continuous reference")
    frames = len(right) // factor
    if any(len(values) != frames for values in (production, same_rate, continuous)):
        raise ValueError("Every host-frame vector must match the internal source length/factor")
    taps = reference.kernel(np.arange(SUPPORT_FRAMES * factor + 1) / factor)
    discrete_normalizer = math.fsum(float(value) for value in taps) / factor
    normalized = same_rate * (discrete_normalizer / normalizer)
    trapezoid = trapezoid_reference(right, left, factor, epochs, normalizer)
    attributed, rows = endpoint_attribution(right, left, factor, epochs, normalizer, event_rows)
    implementation, normalization = production - same_rate, same_rate - normalized
    endpoint, integration = normalized - trapezoid, trapezoid - continuous
    total, summed = production - continuous, implementation + normalization + endpoint + integration
    arrays = {
        "production_volts": production.copy(), "same_rate_volts": same_rate.copy(),
        "continuous_q64_volts": continuous.copy(), "source_internal_volts": right.copy(),
        "source_left_internal_volts": left.copy(), "normalization_volts": normalized,
        "trapezoid_volts": trapezoid,
        "implementation_error_volts": implementation,
        "normalization_error_volts": normalization,
        "endpoint_event_error_volts": endpoint,
        "cell_integration_error_volts": integration,
        "total_error_volts": total, "decomposition_sum_volts": summed,
        "decomposition_residual_volts": total - summed,
        **attributed,
        "endpoint_event_direct_residual_volts": endpoint - attributed["endpoint_event_direct_volts"],
        "endpoint_event_attribution_residual_volts": endpoint - attributed["endpoint_event_attribution_volts"],
    }
    if any(not np.all(np.isfinite(values)) for values in arrays.values()):
        raise ArithmeticError("Nonfinite decomposed array")
    zero = np.zeros(frames)
    host_arrays = {name: values for name, values in arrays.items()
                   if values.ndim == 1 and not name.endswith("internal_volts")}

    def region(indices: Any = None) -> dict[str, Any]:
        return {name: reference.error_metrics(values, zero, rate, indices)
                for name, values in host_arrays.items()}

    union, windows = np.zeros(frames, dtype=bool), []
    for offset in sorted({row["offset_host_frames"] for row in rows}):
        start, stop = math.ceil(offset), min(frames, math.floor(offset + SUPPORT_FRAMES) + 1)
        union[start:stop] = True
        windows.append({"start_frame": start, "stop_frame_inclusive": stop - 1,
                        "event_indices": [row.get("event_index") for row in rows
                                          if row["offset_host_frames"] == offset],
                        "metrics": region(np.arange(start, stop))})
    for index, row in enumerate(rows):
        row["metrics"] = reference.error_metrics(attributed["event_contributions_volts"][index], zero, rate)
    metrics = {
        "oversampling_factor": factor, "sample_rate_hz": rate,
        "internal_step_host_frames": 1.0 / factor, "support_host_frames": SUPPORT_FRAMES,
        "discrete_normalizer": discrete_normalizer, "continuous_normalizer": normalizer,
        "discrete_to_continuous_normalizer_ratio": discrete_normalizer / normalizer,
        "arrays": region(), "event_rows": rows, "event_windows": windows,
        "event_window_union": region(np.flatnonzero(union)),
        "outside_event_window_union": region(np.flatnonzero(~union)),
        "frame_816": ({name: float(values[816]) for name, values in host_arrays.items()}
                      if frames > 816 else None),
        "internal_jump_attribution_residual": reference.error_metrics(
            attributed["event_jump_attribution_residual_internal_volts"], np.zeros(len(right)), rate * factor),
        "scope": "Synthetic offline bookkeeping; closure is not an alias-only or fidelity acceptance gate.",
    }
    return arrays, metrics
