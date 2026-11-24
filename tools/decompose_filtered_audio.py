#!/usr/bin/env python3
"""Retain one frozen model-2 sampled/continuous error decomposition.

This optional offline diagnostic does not change any audio path or acceptance
gate. --plan and --help work without NumPy/SciPy. Actual work runs in a bounded
child process and retains the existing comparison, raw arrays and failed gates.
Exit codes match the original validator: 0 selected comparisons pass, 1 a
comparison fails, 2 reference unresolved, 3 operationally incomplete.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys

import validate_filtered_audio as reference

ROOT = Path(__file__).resolve().parents[1]
CASE_ID = 'r08-chord-all73-u10'
CASE_SHA256 = '579b3844d0fd32ef8fa9706f189b5d6255c0c93b2218e64d235ed4b233cbc4bc'
BOOKKEEPING_LIMIT_VOLTS = 1e-12
SCOPE = ('One predeclared synthetic model-2 diagnostic; no fitted alignment, '
         'gain, omitted onset, alias-only interpretation, physical fidelity '
         'or RT-001 completion. Existing comparisons and limits are unchanged.')
SOURCE_PATHS = tuple(dict.fromkeys((*reference.SOURCE_PATHS,
    'tools/filtered_error_bridge.py', 'tools/decompose_filtered_audio.py')))


def frozen_case():
    matches = [case for case in reference.build_cases('full') if case.id == CASE_ID]
    if len(matches) != 1 or reference.object_hash(matches[0].document()) != CASE_SHA256:
        raise ValueError('The frozen diagnostic case changed; review its protocol explicitly')
    return matches[0]


def make_plan():
    case = frozen_case()
    body = {'schema_version': 1, 'kind': 'filtered-error-decomposition',
            'scope': SCOPE, 'case': case.document(), 'case_sha256': CASE_SHA256,
            'gates': reference.GATES,
            'bookkeeping_limit_volts': BOOKKEEPING_LIMIT_VOLTS,
            'identity': 'P-C=(P-S)+(S-N)+(N-T)+(T-C)',
            'terms': {'P': 'production', 'S': 'independent sampled/FIR',
                      'N': 'sampled numerator with continuous DC normalization',
                      'T': 'event-aware one-sided composite trapezoids',
                      'C': 'continuous Q64'},
            'reference_orders': list(reference.ORDERS),
            'reset_epochs': reference.reset_epochs(case.frames, case.events),
            'latency_frames': reference.LATENCY_FRAMES,
            'probe_timeout_seconds': 120}
    return {**body, 'plan_sha256': reference.object_hash(body)}


def source_snapshot():
    return {name: reference.file_hash(ROOT / name) for name in SOURCE_PATHS}


def _write(path, value):
    reference.write_exclusive(path, reference.canonical_bytes(value) + b'\n')


def _git_identity():
    try:
        return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                                       text=True, timeout=10).strip()
    except (OSError, subprocess.SubprocessError):
        return None


def run_diagnostic(probe: Path, output: Path):
    """Run inside the deadline-enforced worker; refuse existing output paths."""
    np, scipy = reference.numerical_libraries()
    import filtered_error_bridge as bridge
    case = frozen_case()
    probe = probe.resolve(strict=True)
    if not probe.is_file():
        raise ValueError('Probe must be a regular executable file')
    before = source_snapshot()
    binary_before = reference.file_hash(probe)
    reference.create_output(output)
    _write(output / 'plan.json', make_plan())
    manifest = {'schema_version': 1, 'scope': SCOPE,
                'started_at_utc': datetime.now(timezone.utc).isoformat(),
                'argv': sys.argv, 'git_head': _git_identity(),
                'versions': {'python': sys.version, 'numpy': np.__version__,
                             'scipy': scipy.__version__, 'platform': platform.platform()},
                'source_sha256_before': before,
                'source_snapshot_sha256': reference.object_hash(before),
                'probe_path': str(probe), 'probe_sha256_before': binary_before,
                'binary_source_binding': 'Identities only; consult the separately retained build record.',
                'array_encoding': 'IEEE binary64; hashes cover C-order little-endian float64 bytes'}
    _write(output / 'manifest.json', manifest)
    result = {'schema_version': 1, 'scope': SCOPE, 'diagnostic_complete': False,
              'case_id': CASE_ID, 'case_sha256': CASE_SHA256,
              'manifest_sha256': reference.file_hash(output / 'manifest.json')}
    exit_code = 3
    try:
        row = reference.execute_case(probe, output, case, reference.ReferenceBank(),
                                     manifest['source_snapshot_sha256'])
        _write(output / 'case.json', row)
        with np.load(output / row['raw_arrays_path'], allow_pickle=False) as stored:
            original = {name: stored[name].copy() for name in stored.files}
        right, left, events = bridge.one_sided_source(case)
        factor = reference.oversampling_factor(case.sample_rate_hz)
        arrays, metrics = bridge.decompose_arrays(
            original['production_volts'], original['same_rate_volts'],
            original['continuous_q64_volts'], right, left, factor,
            reference.reset_epochs(case.frames, case.events),
            reference.quadrature(64).normalizer,
            sample_rate_hz=case.sample_rate_hz, event_rows=events)
        arrays = {**arrays, 'source_right_volts': right, 'source_left_volts': left}
        if any(not np.all(np.isfinite(values)) for values in arrays.values()):
            raise ArithmeticError('Nonfinite decomposition array')
        source_difference = float(np.max(np.abs(right - original['source_internal_volts'])))
        closure = float(np.max(np.abs(arrays['decomposition_residual_volts'])))
        attribution_residuals = {name: float(np.max(np.abs(arrays[name]))) for name in (
            'endpoint_event_direct_residual_volts',
            'endpoint_event_attribution_residual_volts',
            'event_contribution_residual_volts',
            'event_jump_attribution_residual_internal_volts')}
        with (output / 'raw' / 'decomposition.npz').open('xb') as stream:
            np.savez_compressed(stream, **arrays)
        _write(output / 'events.json', events)
        result.update(metrics=metrics, comparison_a=row['comparison_a'],
                      comparison_b=row['comparison_b'],
                      reference_refinement=row['reference_refinement'],
                      source_reconstruction_error_volts=source_difference,
                      decomposition_closure_error_volts=closure,
                      attribution_closure_errors_volts=attribution_residuals,
                      array_sha256={name: reference.array_hash(values)
                                    for name, values in arrays.items()},
                      array_shapes={name: list(values.shape) for name, values in arrays.items()},
                      raw_npz_sha256=reference.file_hash(output / 'raw' / 'decomposition.npz'),
                      original_case_sha256=reference.file_hash(output / 'case.json'),
                      events_sha256=reference.file_hash(output / 'events.json'))
        if max(source_difference, closure, *attribution_residuals.values()) > BOOKKEEPING_LIMIT_VOLTS:
            raise ArithmeticError('Independent bookkeeping identity exceeds its fixed limit')
        after = source_snapshot()
        binary_after = reference.file_hash(probe)
        result.update(source_sha256_after=after, probe_sha256_after=binary_after)
        if before != after or binary_before != binary_after:
            raise RuntimeError('Source or probe changed during the diagnostic')
        result['diagnostic_complete'] = True
        if row['comparison_a']['status'] == 'fail' or row['comparison_b']['status'] == 'fail':
            exit_code = 1
        elif not row['reference_refinement']['resolved']:
            exit_code = 2
        else:
            exit_code = 0
    except Exception as error:
        result['error'] = f'{type(error).__name__}: {error}'
    result.update(exit_code=exit_code, finished_at_utc=datetime.now(timezone.utc).isoformat())
    _write(output / 'summary.json', result)
    print(json.dumps({'case_id': CASE_ID, 'diagnostic_complete': result['diagnostic_complete'],
                      'exit_code': exit_code, 'summary': str(output / 'summary.json')}))
    return exit_code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', action='store_true')
    parser.add_argument('--probe', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--timeout-seconds', type=int, default=600)
    parser.add_argument('--_worker', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.plan:
        print(json.dumps(make_plan(), indent=2, allow_nan=False))
        return 0
    if not args.probe or not args.output_dir:
        parser.error('--probe and --output-dir are required unless --plan is used')
    if not 1 <= args.timeout_seconds <= 1800:
        parser.error('--timeout-seconds must be in 1..1800')
    try:
        if args._worker:
            return run_diagnostic(args.probe, args.output_dir)
        if args.output_dir.exists() or args.output_dir.is_symlink():
            raise ValueError(f'Refusing existing output path: {args.output_dir}')
        command = [sys.executable, str(Path(__file__).resolve()), '--_worker',
                   '--probe', str(args.probe.resolve()), '--output-dir', str(args.output_dir.resolve())]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   start_new_session=(os.name == 'posix'))
        try:
            stdout, stderr = process.communicate(timeout=args.timeout_seconds)
        except subprocess.TimeoutExpired:
            if os.name == 'posix':
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()
            stdout, stderr = process.communicate()
            if args.output_dir.is_dir():
                _write(args.output_dir / 'timeout.json', {'status': 'incomplete',
                       'reason': 'worker wall deadline exhausted',
                       'timeout_seconds': args.timeout_seconds, 'command': command})
            sys.stderr.write('Decomposition worker exceeded its declared deadline.\n')
            return 3
        sys.stdout.buffer.write(stdout)
        sys.stderr.buffer.write(stderr)
        return process.returncode if process.returncode in (0, 1, 2, 3) else 3
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Decomposition incomplete: {error}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())
