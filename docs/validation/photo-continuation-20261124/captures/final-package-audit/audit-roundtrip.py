#!/usr/bin/env python3
"""Read back one explicit reconstruction; do not replay or modify evidence."""
import collections
import hashlib
import json
from pathlib import Path
import zipfile

from PIL import Image

ROOT = Path('/home/agent/rhoades-9a04c9726e31/w4-066')
SOURCE = Path('/home/agent/rhoades-9a04c9726e31/w4-061')
RESTORED = ROOT / 'reconstructed'
ARCHIVE = ROOT / 'final-native-pixels.zip'
SIDECAR = ROOT / 'final-native-pixels.manifest.json'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def load(path):
    return json.loads(path.read_text())


def fingerprint(path):
    stat = path.stat()
    return {'sha256': sha(path.read_bytes()), 'bytes': stat.st_size,
            'mtime_ns': stat.st_mtime_ns}


def verify_binding(row, path_key, size_key='bytes'):
    path = Path(row[path_key])
    raw = path.read_bytes()
    return {'path': str(path), 'sha256': sha(raw), 'bytes': len(raw),
            'match': sha(raw) == row['sha256'] and len(raw) == row[size_key]}


before = load(ROOT / 'source-input-before.json')
manifest = load(SIDECAR)
assert manifest['source_root'] == str(SOURCE)
assert manifest['producer']['sha256'] == before['tool']['sha256']
image_rows = []
raw_rows = []
rgb_bytes = 0
for entry in manifest['images']:
    original_path = SOURCE / entry['path']
    restored_path = RESTORED / entry['path']
    original_raw = original_path.read_bytes()
    restored_raw = restored_path.read_bytes()
    with Image.open(original_path) as original, Image.open(restored_path) as restored:
        original_rgb = original.convert('RGB').tobytes()
        restored_rgb = restored.convert('RGB').tobytes()
        match = (original.size == restored.size == (entry['width'], entry['height'])
                 and original_rgb == restored_rgb
                 and sha(restored_rgb) == entry['rgb_sha256']
                 and sha(original_raw) == entry['original_file_sha256'])
        image_rows.append({'path': entry['path'], 'dimensions': list(restored.size),
                           'original_mode': original.mode, 'restored_mode': restored.mode,
                           'rgb_bytes': len(restored_rgb), 'rgb_sha256': sha(restored_rgb),
                           'original_png_sha256': sha(original_raw),
                           'restored_png_sha256': sha(restored_raw),
                           'encoded_bytes_equal': original_raw == restored_raw,
                           'match': match})
        rgb_bytes += len(restored_rgb)

for entry in manifest['files']:
    original = (SOURCE / entry['path']).read_bytes()
    restored = (RESTORED / entry['path']).read_bytes()
    raw_rows.append({'path': entry['path'], 'bytes': len(restored),
                     'sha256': sha(restored),
                     'match': original == restored
                     and sha(restored) == entry['original_file_sha256'],
                     'elf': restored.startswith(b'\x7fELF')})

source_rows = []
for entry in before['files']:
    actual = fingerprint(SOURCE / entry['path'])
    source_rows.append({'path': entry['path'], 'match': all(
        actual[key] == entry[key] for key in ('sha256', 'bytes', 'mtime_ns'))})
before_names = {entry['path'] for entry in before['files']}
now_names = {path.relative_to(SOURCE).as_posix()
             for path in SOURCE.rglob('*') if path.is_file()}
tool_actual = fingerprint(Path(before['tool']['path']))
tool_unchanged = all(tool_actual[key] == before['tool'][key]
                     for key in ('sha256', 'bytes', 'mtime_ns'))

inputs = load(SOURCE / 'input-manifest.json')
input_rows = [verify_binding(row, 'copy') for row in inputs['copied_inputs']]
runtime_rows = [verify_binding(row, 'path') for row in inputs['external_runtime_inputs']]
outcome = load(RESTORED / 'native-run01/outcome.json')
outcome_rows = [verify_binding(row, 'path') for row in outcome['source_files']]
output_manifest = load(SOURCE / 'output-manifest.json')
output_rows = []
for entry in output_manifest['files']:
    raw = (SOURCE / entry['relative_path']).read_bytes()
    output_rows.append({'path': entry['relative_path'], 'match':
                        sha(raw) == entry['sha256'] and len(raw) == entry['bytes']})

with zipfile.ZipFile(ARCHIVE) as zipped:
    sidecar_matches = zipped.read('manifest.json') == SIDECAR.read_bytes()
    zip_uncompressed_payload_bytes = sum(row.file_size for row in zipped.infolist())

ok = (len(image_rows) == 67 and len(raw_rows) == 134
      and all(row['match'] for row in image_rows + raw_rows + source_rows
              + input_rows + runtime_rows + outcome_rows + output_rows)
      and not any(row['elf'] for row in raw_rows)
      and before_names == now_names and tool_unchanged and sidecar_matches)
summary = {
    'result': 'pass' if ok else 'fail',
    'explicit_roundtrip_invocations': 1,
    'images': len(image_rows), 'recorded_captures': len(outcome['captures']),
    'held_white_key_images': sum('white-held-midi-' in row['path'] for row in image_rows),
    'rgb_bytes_compared': rgb_bytes,
    'raw_files': len(raw_rows), 'raw_file_bytes': sum(row['bytes'] for row in raw_rows),
    'original_png_byte_streams_reproduced': sum(row['encoded_bytes_equal'] for row in image_rows),
    'source_files_unchanged': sum(row['match'] for row in source_rows),
    'source_file_set_unchanged': before_names == now_names,
    'source_original_total_bytes': before['total_bytes'],
    'packing_tool_unchanged': tool_unchanged,
    'input_copied_bindings_verified': sum(row['match'] for row in input_rows),
    'input_runtime_bindings_verified': sum(row['match'] for row in runtime_rows),
    'outcome_source_bindings_verified': sum(row['match'] for row in outcome_rows),
    'output_manifest_bindings_verified': sum(row['match'] for row in output_rows),
    'output_manifest_unlisted_present_paths': sorted(now_names -
        {entry['relative_path'] for entry in output_manifest['files']}),
    'outcome_checks_by_status': dict(collections.Counter(row['status'] for row in outcome['checks'])),
    'native_host_returncode': outcome['host_returncode'],
    'actual_verifier_wrapper_exit_code': load(SOURCE / 'native-process.json')['actual_subprocess_exit_code'],
    'outcome_sha256': sha((RESTORED / 'native-run01/outcome.json').read_bytes()),
    'input_manifest_sha256': sha((RESTORED / 'input-manifest.json').read_bytes()),
    'output_manifest_sha256': sha((RESTORED / 'output-manifest.json').read_bytes()),
    'evidence_summary_sha256': sha((RESTORED / 'evidence-summary.json').read_bytes()),
    'archive_bytes': ARCHIVE.stat().st_size,
    'archive_sha256': sha(ARCHIVE.read_bytes()),
    'manifest_bytes': SIDECAR.stat().st_size,
    'manifest_sha256': sha(SIDECAR.read_bytes()),
    'sidecar_matches_archive_manifest': sidecar_matches,
    'selected_source_bytes': sum(row['original_file_size_bytes']
        for row in manifest['images'] + manifest['files']),
    'zip_uncompressed_payload_bytes': zip_uncompressed_payload_bytes,
    'producer': manifest['producer'],
    'scope': manifest['scope'],
}
summary['archive_reduction_percent'] = 100 * (1 - summary['archive_bytes'] / summary['selected_source_bytes'])
audit = {'summary': summary, 'image_readback': image_rows, 'raw_readback': raw_rows,
         'source_unchanged': source_rows, 'copied_input_bindings': input_rows,
         'external_runtime_bindings': runtime_rows, 'outcome_source_bindings': outcome_rows,
         'output_manifest_bindings': output_rows,
         'source_paths_added': sorted(now_names - before_names),
         'source_paths_removed': sorted(before_names - now_names)}
(ROOT / 'roundtrip-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
(ROOT / 'roundtrip-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
raise SystemExit(0 if ok else 1)
