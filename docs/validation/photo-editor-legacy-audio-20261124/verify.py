#!/usr/bin/env python3
"""Check preserved bytes and replay frozen comparator functions on raw samples.

Python 3.11+, little-endian host, standard library only. No native executable,
plug-in, SDK, build, device, or archived command is launched. The two SHA-pinned
Python comparator snapshots define the original validation protocols.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
from itertools import product
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tarfile
from types import SimpleNamespace

REPORT_SHA = {
    'w4-003': '680b546fb7925272b899f551bd76fa6fec1ebbc42ae1f4f0bba96008cbb9dd44',
    'w4-001': 'a372fe9c746be94621842d38a867e58a30dd5a42d0725e4b4185af0740b6821a',
}
DRIVER_SHA = {
    'tested': 'dde69e127376fd6c24d63fc9e260d036151d30eb8ad73a1bd29e5def09e307d5',
    'before-fix': 'b0f97f7ee8ef989a16cd15ea92fc54939bbd9cf2677dd313cea377df50af8754',
}
PATCH_SHA = '4a48f29ef0ccc6e87b070a6bfea88bd9447e8d3182ed33097f8bfcb5b19ec5da'
HEAD = '3c619b74a0e60e5daf1debad6164603d8d43af5d'
COMMIT = '5f1aaf27b3c34325886fc352dc3430f5e240ae17'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def same(actual, expected, label):
    require(json.dumps(actual, sort_keys=True, allow_nan=False)
            == json.dumps(expected, sort_keys=True, allow_nan=False), label + ' differs')


def safe(name):
    require(isinstance(name, str) and re.fullmatch(r'[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*', name)
            and not any(p in {'.', '..'} for p in name.split('/')), 'Unsafe path: ' + repr(name))
    return PurePosixPath(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract-to', type=Path, help='Extract every original file into a NEW directory')
    parser.add_argument('--json-output', type=Path, help='Write a fresh verification record')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    require(args.extract_to is None or not args.extract_to.exists(), 'Extraction destination already exists')
    manifest_bytes = (here / 'manifest.json').read_bytes()
    require(len(manifest_bytes) <= 2_000_000, 'Manifest byte limit exceeded')
    manifest = json.loads(manifest_bytes)
    delivery_count = 0
    if (here / 'SHA256SUMS').exists():
        for line in (here / 'SHA256SUMS').read_text().splitlines():
            expected, name = line.split('  ', 1)
            safe(name)
            require(sha((here / name).read_bytes()) == expected, 'Delivery checksum: ' + name)
            delivery_count += 1
    archive = here / str(safe(manifest['archive']['name']))
    require(archive.stat().st_size == manifest['archive']['bytes'] <= 4_000_000, 'Archive size differs/exceeds 4 MB')
    require(sha(archive.read_bytes()) == manifest['archive']['sha256'], 'Archive checksum differs')
    entries = manifest['files']
    require(len(entries) == manifest['counts']['files'] <= 1600, 'File count differs/exceeds bound')
    contents, total = {}, 0
    with tarfile.open(archive, 'r|xz') as stream:
        for member in stream:
            safe(member.name)
            require(member.name not in contents and member.name in entries, 'Duplicate/unlisted member')
            require(member.isfile() and member.sparse is None and not member.pax_headers,
                    'Non-regular/sparse/extended-header member')
            require((member.uid, member.gid, member.mtime, member.mode, member.uname, member.gname)
                    == (0, 0, 0, 0o644, '', ''), 'Noncanonical member metadata')
            require(0 <= member.size <= 2_000_000, 'Member size limit exceeded')
            total += member.size
            require(total <= 40_000_000, 'Payload size limit exceeded')
            raw = stream.extractfile(member).read(member.size + 1)
            item = entries[member.name]
            require(len(raw) == member.size == item['bytes'] and sha(raw) == item['sha256'],
                    'Member identity differs: ' + member.name)
            require(not raw.startswith(b'\x7fELF') and Path(member.name).suffix
                    not in {'.so', '.o', '.a', '.dll', '.exe'}, 'Native executable/object payload')
            require(item['source_binding'] in manifest['source_bindings'], 'Unknown source binding')
            contents[member.name] = raw
    same(list(contents), sorted(entries), 'Archive member order/inventory')
    same(total, manifest['counts']['uncompressed_file_bytes'], 'Uncompressed bytes')
    checksums = ''.join(entries[name]['sha256'] + '  ' + name + '\n' for name in sorted(entries))
    same((here / 'payload.sha256').read_text(), checksums, 'Per-file checksum inventory')

    def js(name):
        return json.loads(contents[name])

    def driver(version):
        name = 'source/' + version + '/tools/compare_vst3_legacy.py'
        require(sha(contents[name]) == DRIVER_SHA[version], 'Frozen comparator identity differs')
        namespace = {'__name__': 'retained_comparator_definitions', '__file__': '/retained/' + name}
        # Pinned module initialization only defines its standard-library protocol;
        # __main__ is not entered. Only pure validation/comparison functions run.
        exec(compile(contents[name], '/retained/' + name, 'exec'), namespace)
        return SimpleNamespace(**namespace)

    results, reports, captures = {}, {}, {}
    for worker, version, expected_valid, expected_pairs, expected_groups in [
            ('w4-003', 'tested', 216, 144, 36), ('w4-001', 'before-fix', 72, 0, 12)]:
        module = driver(version)
        report_raw = contents[worker + '/comparison/report.json']
        require(sha(report_raw) == REPORT_SHA[worker], 'Original report identity differs')
        report = reports[worker] = json.loads(report_raw)
        require(sys.byteorder == report['environment']['byte_order'] == 'little', 'Little-endian verification required')
        require(report['observed_current_head'] == HEAD, 'Capture-time HEAD differs')
        prefix = worker + '/comparison/' + report['raw_directory'] + '/'
        require(contents[prefix + 'report.json'] == report_raw, 'Published and per-run report differ')
        raw_manifest = js(prefix + 'manifest.json')
        require(sha(contents[prefix + 'manifest.json']) == report['raw_manifest_sha256'], 'Raw manifest hash differs')
        require(len(raw_manifest['files']) == report['raw_manifest_file_count'] == 649, 'Raw manifest extent differs')
        require(module.raw_records_match_manifest(report['captures'], raw_manifest), 'Raw capture bindings differ')
        for name, item in raw_manifest['files'].items():
            require(entries[prefix + name]['sha256'] == item['sha256']
                    and entries[prefix + name]['bytes'] == item['bytes'], 'Raw manifest entry differs')
        same(js(prefix + 'commands.json'), report['captures'], 'Original native command records')
        expected_keys = set(product(module.MODES, module.VARIANTS, module.FORMATS,
                                    module.RATES, module.PARTITIONS, module.WORKLOADS))
        runs, errors, counts, ids = {}, Counter(), Counter(), set()
        for record in report['captures']:
            key = tuple(record[k] for k in ['mode', 'variant', 'format_bits', 'sample_rate_hz', 'partition', 'gain_workload'])
            require(key not in runs and record['capture_id'] not in ids, 'Duplicate fixture/capture ID')
            ids.add(record['capture_id'])
            require(record['exit_code'] == 0, 'Recorded native process failed')
            metadata = module.strict_json(contents[prefix + record['stdout_path']])
            audio = contents[prefix + record['trace_path']]
            require(contents[prefix + record['stderr_path']] == b'', 'Nonempty capture stderr')
            counts[(record['mode'], metadata['parameter_count'])] += 1
            try:
                metrics = module.validate_capture(metadata, audio, capture_id=record['capture_id'], mode=key[0],
                    variant=key[1], format_bits=key[2], rate=key[3], partition=key[4], workload=key[5])
            except module.ComparisonError as error:
                require(record['passed'] is False and record['error'] == str(error), 'Original failure differs')
                errors[str(error)] += 1
            else:
                require(record['passed'] is True and 'error' not in record, 'Original successful validation differs')
                for field, value in metrics.items():
                    same(record[field], value, worker + ' raw metric ' + field)
            runs[key] = (record, metadata, audio)
        same(sorted(runs), sorted(expected_keys), 'Full 216-capture matrix')
        same(sorted([*key, count] for key, count in counts.items()),
             [['baseline', 18, 72], ['default', 24, 72], ['explicit1', 24, 72]], 'Parameter inventory')
        cases = []
        for key in product(module.VARIANTS, module.FORMATS, module.RATES, module.PARTITIONS, module.WORKLOADS):
            pairs = [module.compare_pair(runs[('baseline',) + key], runs[(mode,) + key])
                     for mode in ('default', 'explicit1')]
            cases.append(dict(zip(['variant', 'format_bits', 'sample_rate_hz', 'partition', 'gain_workload'], key),
                              comparisons=pairs, passed=all(pair['passed'] for pair in pairs)))
        same(cases, report['cases'], 'Recomputed exact pair comparisons')
        dense = []
        for mode, variant, bits, rate in product(module.MODES, module.VARIANTS, module.FORMATS, module.RATES):
            selected = [runs[(mode, variant, bits, rate, partition, 'dense')] for partition in module.PARTITIONS]
            exact = all(item[0]['passed'] for item in selected) and len({item[2] for item in selected}) == 1
            dense.append({'mode': mode, 'variant': variant, 'format_bits': bits,
                          'sample_rate_hz': rate, 'three_partitions_exact': exact})
        same(dense, report['dense_partition_comparisons'], 'Recomputed dense partition groups')
        summary = report['summary']
        derived = {'valid_captures': sum(item[0]['passed'] for item in runs.values()),
                   'passed_pairs': sum(pair['passed'] for case in cases for pair in case['comparisons']),
                   'dense_partition_groups_passed': sum(group['three_partitions_exact'] for group in dense),
                   'paired_stereo_frames': len(cases) * 2 * module.TOTAL_FRAMES,
                   'loaded_callbacks_total': sum(item[0].get('callbacks', 0) for item in runs.values())}
        for field, value in derived.items():
            same(summary[field], value, worker + ' summary ' + field)
        same([derived['valid_captures'], derived['passed_pairs'], derived['dense_partition_groups_passed']],
             [expected_valid, expected_pairs, expected_groups], 'Expected original outcome')
        require(summary['complete'] is True and summary['modules_recorder_manifest_and_relevant_sources_unchanged'] is True,
                'Original complete/unchanged input record absent')
        same(report['status'], 'passed' if worker == 'w4-003' else 'failed', 'Original official gate status')
        same(dict(errors), {} if worker == 'w4-003' else {'invalid capture field: parameter_count': 144},
             'Original stale parameter-count failures')
        results[worker] = {'official_status': report['status'], **derived, 'validation_errors': dict(errors),
                           'raw_manifest_files_verified': 649, 'capture_ids_unique': len(ids)}
        captures[worker] = (runs, ids)

    passed, failed = reports['w4-003'], reports['w4-001']
    require(captures['w4-003'][1].isdisjoint(captures['w4-001'][1]), 'Capture IDs reused between runs')
    for field in ['modules', 'capture_executable', 'baseline_manifest', 'baseline_manifest_sha256']:
        same(passed[field], failed[field], 'Shared immutable input ' + field)
    changed = [name for name, value in passed['source_fingerprints'].items()
               if failed['source_fingerprints'].get(name) != value]
    same(changed, ['tools/compare_vst3_legacy.py'], 'Only relevant comparator source changed')
    for name, expected in passed['source_fingerprints'].items():
        require(sha(contents['source/tested/' + name]) == expected, 'Tested source snapshot differs: ' + name)
    preflight = js('w4-003/capture-preflight.json')
    same(preflight['source_fingerprints'], passed['source_fingerprints'], 'Capture preflight source freeze')
    same(preflight['modules'], passed['modules'], 'Capture preflight module freeze')
    require(sha(contents['w4-003/comparator-fix.patch']) == PATCH_SHA, 'Exact dirty patch differs')
    for name, expected in preflight['source_sha256'].items():
        require(sha(contents['source/tested/' + name]) == expected, 'Tested patch source differs')
    for name, expected in js('w4-003/preflight.json')['source_sha256'].items():
        require(sha(contents['source/before-fix/' + name]) == expected, 'Before-fix source differs')
    bridge = js('provenance/source-bridge.json')
    require(bridge['capture_time_head'] == HEAD and bridge['later_correction_commit'] == COMMIT
            and bridge['capture_time_worktree_clean'] is False
            and bridge['capture_time_patch_sha256'] == PATCH_SHA
            and bridge['later_commit_diff_equals_exact_tested_patch'] is True, 'Dirty/committed source bridge differs')
    inventory = js('w4-003/failed-comparison-before.json')
    require(len(inventory) == 652, 'Original failed-run inventory extent differs')
    for name, item in inventory.items():
        require(entries['w4-001/comparison/' + name]['sha256'] == item['sha256']
                and entries['w4-001/comparison/' + name]['bytes'] == item['bytes'], 'Prior failed evidence changed')
    diagnosis = js('w4-001/raw-gate-diagnosis.json')
    require(diagnosis['gate_report_sha256'] == REPORT_SHA['w4-001'], 'Failure diagnosis binding differs')
    old, new = captures['w4-001'][0], captures['w4-003'][0]
    require(all(old[key][2] == new[key][2] for key in old), 'Audio bytes changed between runs')
    seen = set()
    for row in diagnosis['pairs']:
        key = tuple(row[k] for k in ['variant', 'format_bits', 'sample_rate_hz', 'partition', 'gain_workload'])
        mode = row['candidate_mode']
        require((mode, *key) not in seen, 'Repeated diagnosis pair')
        seen.add((mode, *key))
        a, b = old[('baseline',) + key], old[(mode,) + key]
        differences = [field for field in a[1] if field not in module.MODE_DIFFERENCES and a[1][field] != b[1][field]]
        same(a[2] == b[2], row['audio_bytes_equal'], 'Supplemental failed-gate audio diagnosis')
        same(differences, row['legacy_metadata_differences'], 'Supplemental metadata diagnosis')
        same(row['parameter_counts'], {'baseline': a[1]['parameter_count'], 'candidate': b[1]['parameter_count']},
             'Supplemental parameter-count diagnosis')
    require(len(seen) == diagnosis['audio_pairs_equal'] == diagnosis['audio_pairs_total']
            == diagnosis['legacy_metadata_pairs_equal'] == 144, 'Supplemental diagnosis extent differs')
    require(sha(contents['provenance/baseline-bundle-manifest.json']) == passed['baseline_manifest_sha256'],
            'Baseline manifest input differs')
    full_baseline = passed['baseline_manifest']['full_diagnostics']
    require(sha(contents['w4-001/baseline-manifest.json']) == full_baseline['sha256'], 'Full baseline provenance differs')

    if args.extract_to:
        args.extract_to.mkdir(parents=True, exist_ok=False)
        for name, raw in contents.items():
            path = args.extract_to.joinpath(*safe(name).parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        (args.extract_to / 'payload.sha256').write_text(checksums)
    result = {'status': 'pass', 'kind': 'Lossless preservation and frozen-protocol recomputation only',
              'archive_sha256': manifest['archive']['sha256'], 'archive_bytes': archive.stat().st_size,
              'manifest_sha256': sha(manifest_bytes), 'files_verified': len(contents), 'payload_bytes': total,
              'runs': results, 'failed_run_unchanged_files': 652, 'all_216_cross_run_audio_files_identical': True,
              'all_success_capture_ids_fresh': True, 'tested_source_fingerprints_verified': 41,
              'later_commit_exact_patch_identity': COMMIT, 'native_execution_performed': False,
              'scope': 'Numerical digital equivalence only; no physical fidelity, playback-device audibility, DAW, deadline, or other-platform claim.'}
    if args.json_output:
        args.json_output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps(result, indent=2, sort_keys=True))
    if delivery_count:
        print('Delivery checksum entries verified:', delivery_count)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError) as error:
        print('Verification failed:', error, file=sys.stderr)
        sys.exit(1)
