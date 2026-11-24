#!/usr/bin/env python3
"""Verify preserved bytes/observations; optionally reconstruct all logical files.

This checks retained evidence without launching a DAW, loading a module, or
rerunning a test. Requires Python 3.11+ and Pillow for independent RGB checks.
"""
from __future__ import annotations

import argparse
import ast
from collections import Counter
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tarfile
import xml.etree.ElementTree as ET

from PIL import Image, ImageChops


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, explanation):
    if not condition:
        raise ValueError(explanation)


def safe_relative(name):
    path = PurePosixPath(name)
    require(not path.is_absolute() and '..' not in path.parts and '\\' not in name,
            'Unsafe member/path: ' + name)
    return path


def read_members(path, needed, *, normalized=False):
    result = {}
    seen = set()
    with tarfile.open(path, mode='r|xz') as archive:
        for member in archive:
            safe_relative(member.name)
            require(member.name not in seen, 'Duplicate member: ' + member.name)
            seen.add(member.name)
            require(member.isfile(), 'Non-regular archive member: ' + member.name)
            if normalized:
                require((member.uid, member.gid, member.mtime, member.mode) == (0, 0, 0, 0o644),
                        'Unexpected container metadata: ' + member.name)
            if member.name in needed:
                stream = archive.extractfile(member)
                require(stream is not None, 'Unreadable member: ' + member.name)
                result[member.name] = stream.read()
    require(set(result) == needed, 'Missing archive members: ' + str(needed - set(result)))
    if normalized:
        require(seen == needed, 'Unlisted new archive members: ' + str(seen - needed))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--historical-dir', type=Path,
                        help='W2 directory; default is sibling photo-editor-native-20261124')
    parser.add_argument('--output-dir', type=Path,
                        help='Reconstruct all logical files into a NEW directory; originals remain unchanged')
    parser.add_argument('--json-output', type=Path, help='Write verification report to this path')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    manifest_bytes = (here / 'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    historical = args.historical_dir or here.parent / 'photo-editor-native-20261124'
    require(args.output_dir is None or not args.output_dir.exists(),
            '--output-dir must not already exist')

    # SHA256SUMS is optional during initial staging, present in the final delivery.
    checksum_path = here / 'SHA256SUMS'
    delivery_checked = 0
    if checksum_path.exists():
        for line in checksum_path.read_text().splitlines():
            expected, name = line.split('  ', 1)
            safe_relative(name)
            require(sha((here / name).read_bytes()) == expected, 'Delivery mismatch: ' + name)
            delivery_checked += 1

    archive = here / manifest['archive']['name']
    old_archive = historical / Path(manifest['historical_archive']['path']).name
    require(sha(archive.read_bytes()) == manifest['archive']['sha256'], 'Final archive checksum mismatch')
    require(archive.stat().st_size == manifest['archive']['bytes'], 'Final archive length mismatch')
    require(sha(old_archive.read_bytes()) == manifest['historical_archive']['sha256'], 'W2 archive checksum mismatch')
    require(sha((historical / 'manifest.json').read_bytes()) == manifest['historical_archive']['manifest_sha256'],
            'W2 manifest checksum mismatch')
    old_manifest = json.loads((historical / 'manifest.json').read_bytes())
    old_entries = {old_manifest['archive_root'] + '/' + item['path']: item for item in old_manifest['files']}

    wanted = {kind: {item['storage']['member'] for item in manifest['files'] if item['storage']['kind'] == kind}
              for kind in ['archive', 'historical_archive']}
    stores = {'archive': read_members(archive, wanted['archive'], normalized=True),
              'historical_archive': read_members(old_archive, wanted['historical_archive'])}
    contents = {}
    by_original = {}
    entries = {}
    for item in manifest['files']:
        path = item['path']
        safe_relative(path)
        require(path not in contents, 'Duplicate logical path: ' + path)
        store = item['storage']
        data = stores[store['kind']][store['member']]
        require(sha(data) == item['sha256'] == store['member_sha256'], 'Payload mismatch: ' + path)
        require(len(data) == item['bytes'], 'Payload length mismatch: ' + path)
        require(not data.startswith(b'\x7fELF') and not path.endswith(('.so', '.o', '.a', '.dll', '.exe')),
                'Unexpected binary/object payload: ' + path)
        require(item['source_bindings'] and all(binding in manifest['source_bindings']
                                              for binding in item['source_bindings']),
                'Missing source binding: ' + path)
        if store['kind'] == 'historical_archive':
            require(store['archive_sha256'] == manifest['historical_archive']['sha256'],
                    'Unpinned historical reference: ' + path)
            old = old_entries[store['member']]
            require(old['sha256'] == item['sha256'] and old['bytes'] == item['bytes'],
                    'Historical reference differs from historical manifest: ' + path)
        contents[path] = data
        entries[path] = item
        by_original.setdefault(item['original_path'], []).append(item)
    expected_payload = ''.join(item['sha256'] + '  ' + item['path'] + '\n' for item in manifest['files'])
    require((here / 'payload.sha256').read_text() == expected_payload, 'Logical checksums differ from manifest')

    def js(path):
        return json.loads(contents[path])

    def check_retained_input(original, expected):
        candidates = by_original.get(original, [])
        require(any(item['sha256'] == expected['sha256'] and item['bytes'] == expected['bytes']
                    for item in candidates), 'Recorded final input not preserved: ' + original)

    source_binding_checks = 0
    for binding_name, binding in manifest['source_bindings'].items():
        for item in binding.get('source_inputs', []):
            if 'retained_as' in item:
                retained = entries[item['retained_as']]
                require((retained['sha256'], retained['bytes']) == (item['sha256'], item['bytes']),
                        'Source-binding mismatch: ' + binding_name + ': ' + item['path'])
                source_binding_checks += 1
            else:
                require(item.get('retention') in ['hash_only_binary_omitted', 'hash_only_historical_input'],
                        'Unexplained missing source input: ' + item['path'])
                if binding_name.endswith('-final'):
                    require(item['retention'] == 'hash_only_binary_omitted',
                            'Final non-binary source input missing: ' + item['path'])

    capture_images = {}
    capture_rows = []
    for item in manifest['files']:
        if 'capture' not in item:
            continue
        pic = Image.open(io.BytesIO(contents[item['path']])).convert('RGB')
        expected = item['capture']
        require(pic.size == (expected['width'], expected['height']), 'Capture dimensions mismatch: ' + item['path'])
        require(sha(pic.tobytes()) == expected['rgb_sha256'], 'Capture RGB mismatch: ' + item['path'])
        capture_images[item['path']] = pic
        capture_rows.append({'path': item['path'], 'file_sha256': item['sha256'],
                             'rgb_sha256': expected['rgb_sha256'], 'width': pic.width, 'height': pic.height,
                             'storage': item['storage']['kind']})

    def difference(a, b):
        require(a.size == b.size, 'Pixel comparison sizes differ')
        delta = ImageChops.difference(a, b)
        r, g, blue = delta.split()
        any_channel = ImageChops.lighter(ImageChops.lighter(r, g), blue)
        return {'pixels': a.width * a.height,
                'different_pixels': a.width * a.height - any_channel.histogram()[0],
                'bbox': delta.getbbox()}

    callback_counts = {}
    check_counts = {}
    capture_count = 0
    raw_capture_metadata_checked = 0
    for name, expected_count, expected_callbacks in [('sdk-native', 27, 63), ('focus-motion', 20, 28)]:
        record = js('final/' + name + '/outcome.json')
        counts = Counter(item['status'] for item in record['checks'])
        require(counts == Counter({'pass': expected_count}), name + ' checks differ')
        check_counts[name] = dict(counts)
        require(record['result']['exit_code'] == 0 and record['result']['status'] == 'pass', name + ' failed result')
        lines = [line for line in contents['final/' + name + '/editorhost.log'].decode().splitlines()
                 if re.match(r'^(beginEdit|performEdit|endEdit) called ', line)]
        require(lines == [item['raw'] for item in record['host_callbacks']], name + ' callbacks differ from raw log')
        require(len(lines) == expected_callbacks, name + ' callback count differs')
        callback_counts[name] = len(lines)
        for original in record['source_files']:
            if original['path'].endswith('.so') or original['path'].endswith('/editorhost-accepting'):
                continue
            check_retained_input(original['path'], original)
        for capture in record['captures'].values():
            logical = 'final/' + name + '/' + Path(capture['path']).name
            item = entries[logical]
            require((item['sha256'], item['bytes'], item['capture']) ==
                    (capture['sha256'], capture['bytes'], {key: capture[key] for key in ['width', 'height', 'rgb_sha256']}),
                    'Capture differs from original outcome: ' + logical)
            raw_capture_metadata_checked += 1
        capture_count += len(record['captures'])

    sdk = js('final/sdk-native/outcome.json')
    idle = capture_images['final/sdk-native/idle-1536x1152.png']
    full_size_comparisons = {}
    for name in ['source/final/assets/reference/rhodes-suitcase-reference.webp',
                 'source/final/plugins/vst3/resources/rhodes-suitcase.png']:
        photo = Image.open(io.BytesIO(contents[name])).convert('RGB')
        result = difference(photo, idle)
        require(result['different_pixels'] == 0 and result['pixels'] == 1769472,
                'Full-size reference mismatch: ' + name)
        full_size_comparisons[name] = result
    require(sdk['full_size_idle_comparison']['exact'] and sdk['full_size_idle_comparison']['different_pixels'] == 0,
            'Recorded full-size assertion differs')

    reaper = js('final/reaper/run.json')
    counts = Counter(item['status'] for item in reaper['checks'])
    require(counts == Counter({'pass': 21}) and reaper['completed'], 'REAPER result differs')
    check_counts['reaper'] = dict(counts)
    require(reaper['before_sources'] == reaper['after_sources'], 'REAPER source inputs drifted')
    require(reaper['before_modules'] == reaper['after_modules'], 'REAPER modules drifted')
    for source, item in reaper['before_sources'].items():
        matching = entries['source/final/' + source]
        require((matching['sha256'], matching['bytes']) == (item['sha256'], item['bytes']),
                'REAPER source snapshot differs: ' + source)
    require(reaper['cold_initial_focus'] == 1, 'Cold PointerRoot baseline absent')
    checks = {item['name']: item for item in reaper['checks']}
    volume = checks['native_volume_updates_host_parameter']
    require(volume['before'] == 0.5 and volume['changed_indices'] == [0]
            and abs(volume['held'] - 0.7) < 1e-6 and volume['after'] == volume['held'],
            'First cold Volume result differs')
    loss = checks['volume_stops_changing_after_focusout_before_release']
    require(loss['before_loss'] == loss['after_loss'] == loss['after_held_motion'], 'Held movement after loss changed Volume')
    require(loss['direct_endEdit_observed'] is False, 'Unexpected direct REAPER endEdit claim')
    require('audio device closed' in checks['keyboard_focusout_clears_visual_hold']['scope'], 'Missing closed-device scope')
    preflight = js('final/reaper/preflight.json')
    baseline = contents['harness/reaper-assertion-baseline/probe_reaper.py']
    final_probe = contents['final/reaper/probe_reaper.py']
    require(sha(baseline) == preflight['source_probe_sha256'] and sha(final_probe) == preflight['new_probe_sha256'],
            'REAPER assertion preflight source hashes differ')

    def assertion_calls(code):
        return [ast.dump(node, include_attributes=False) for node in ast.walk(ast.parse(code))
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'check']

    require(assertion_calls(baseline) == assertion_calls(final_probe), 'REAPER assertion-call ASTs changed')
    require(b'LD_PRELOAD' not in final_probe, 'Unexpected diagnostic preload in final REAPER probe')
    provenance = js('provenance/reaper-acquisition.json')
    require(provenance['files']['reaper_linux_x86_64/REAPER/reaper']['sha256'] == reaper['host']['sha256'],
            'REAPER executable identity differs from acquisition provenance')
    reaper_pixels = {}
    for suffix in ['focus-volume-after-loss', 'keyboard-focus-loss', 'final-native-editor']:
        result = difference(capture_images['final/reaper/captures/idle-native-editor.png'],
                            capture_images['final/reaper/captures/' + suffix + '.png'])
        require(result['different_pixels'] == 0 and result['pixels'] == 786432, 'REAPER cleanup pixels differ: ' + suffix)
        reaper_pixels[suffix] = result
    for capture in reaper['captures'].values():
        item = entries['final/reaper/captures/' + Path(capture['path']).name]
        require((item['sha256'], item['bytes'], item['capture']) ==
                (capture['sha256'], capture['bytes'], {key: capture[key] for key in ['width', 'height', 'rgb_sha256']}),
                'REAPER capture differs from original record')
        raw_capture_metadata_checked += 1
    capture_count += len(reaper['captures'])
    require(capture_count == raw_capture_metadata_checked == len(capture_images) == 42, 'Capture count differs')

    standalone = contents['standalone/w3-003-repository-focus-test-run03.log'].decode().splitlines()
    require(sum(line.startswith('PASS ') for line in standalone) == 64
            and not any(line.startswith('FAIL ') for line in standalone), 'Standalone result differs')
    ctest = js('ctest/w3-003-ctest-run03-result.json')
    suite = ET.fromstring(contents['ctest/w3-003-ctest-run03.xml'])
    testcases = suite.findall('.//testcase')
    selection = contents['ctest/w3-003.txt'].decode().splitlines()
    require(len(testcases) == len(selection) == 32 and sorted(selection) == sorted(ctest['requested'])
            == sorted(test.attrib['name'] for test in testcases), 'Assigned CTest selection differs')
    require(not suite.findall('.//failure') and not suite.findall('.//error') and not suite.findall('.//skipped'),
            'Assigned CTest failure/error/skip present')
    require(ctest['exit'] == 0 and ctest['modules_before'] == ctest['modules_after'], 'Assigned CTest result/module drift')
    evidence = js('provenance/w3-003-evidence.json')
    require(ctest['modules_before'] == evidence['modules'], 'Final module bindings differ')
    require({path: item['sha256'] for path, item in reaper['before_modules'].items()} == evidence['modules'],
            'REAPER module bindings differ from final worker record')
    for source, item in evidence['sources'].items():
        require(entries['source/final/' + source]['sha256'] == item['sha256'], 'Final source differs from worker record')
    failed_attempt_counts = {}
    for directory in ['w3-003-reaper', 'w3-003-reaper-run02', 'w3-003-reaper-trace']:
        record = js('historical/' + directory + '/run.json')
        counts = Counter(item['status'] for item in record['checks'])
        require(counts == Counter({'pass': 20, 'fail': 1}), 'Historical REAPER failure record differs')
        failed_attempt_counts[directory] = dict(counts)
    motion_failed = js('historical/w3-003-focus-motion/outcome.json')
    counts = Counter(item['status'] for item in motion_failed['checks'])
    require(counts == Counter({'pass': 18, 'fail': 2}), 'Historical SDK motion failure record differs')
    failed_attempt_counts['w3-003-focus-motion'] = dict(counts)

    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=False)
        for name, data in contents.items():
            dest = args.output_dir.joinpath(*safe_relative(name).parts)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        (args.output_dir / 'payload.sha256').write_text(expected_payload)
        (args.output_dir / 'manifest.json').write_bytes(manifest_bytes)

    report = {
        'status': 'pass', 'verification_kind': 'Preservation integrity and independent recomputation from retained records; no host/test rerun',
        'manifest_sha256': sha(manifest_bytes), 'archive_sha256': manifest['archive']['sha256'],
        'archive_bytes': manifest['archive']['bytes'], 'logical_files_verified': len(contents),
        'logical_bytes_verified': sum(map(len, contents.values())), 'unique_archive_members_verified': len(stores['archive']),
        'historical_members_verified': len(stores['historical_archive']),
        'source_bindings_verified': source_binding_checks,
        'capture_file_and_rgb_records_verified': capture_count,
        'capture_counts': manifest['counts'], 'native_check_counts': check_counts,
        'callback_raw_sequences_verified': callback_counts, 'standalone_passing_assertions': 64,
        'assigned_ctests': {'selected': 32, 'passed': 32, 'skipped': 0, 'union_count_note': 'Supplemental replay, not extra tests in the W3 union'},
        'reaper_assertion_call_asts_unchanged': True, 'reaper_final_probe_diagnostic_preload': False,
        'cold_initial_focus': reaper['cold_initial_focus'], 'cold_first_volume': volume,
        'full_size_pixel_comparisons': full_size_comparisons, 'reaper_cleanup_pixel_comparisons': reaper_pixels,
        'failed_attempt_counts': failed_attempt_counts, 'captures': capture_rows,
        'scope': 'Linux/X11/Xvfb without WM; SDK no audio loop; REAPER device closed; no direct REAPER endEdit or processor note-off observation',
    }
    if args.json_output:
        args.json_output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key not in ['captures', 'cold_first_volume']}, indent=2))
    if delivery_checked:
        print('Delivery checksum entries verified:', delivery_checked)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, tarfile.TarError) as error:
        print('Verification failed:', error, file=sys.stderr)
        sys.exit(1)
