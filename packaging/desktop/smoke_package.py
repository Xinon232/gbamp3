# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Invoke the actual packaged binary, outside the checkout, and validate evidence."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from bundle_ffmpeg import sha256


def read_success(path: Path) -> dict:
    evidence = json.loads(path.read_text(encoding='utf-8'))
    if evidence.get('success') is not True:
        raise ValueError('GUI smoke did not explicitly report success')
    packaging = evidence.get('packaging', {})
    if packaging.get('frozen') is not True or packaging.get('native_available') is not True:
        raise ValueError('Self-test must exercise the frozen binary with native acceleration')
    return evidence


def smoke(executable: Path, output: Path, timeout: int = 600) -> dict:
    executable = executable.resolve(strict=True)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    result = output / 'converter-smoke.json'
    result.unlink(missing_ok=True)
    (output / 'frozen-binary-verification.json').unlink(missing_ok=True)
    env = dict(os.environ)
    for key in ('PYTHONPATH', 'PYTHONHOME', 'FFMPEG', 'IMAGEIO_FFMPEG_EXE',
                'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'TCL_LIBRARY', 'TK_LIBRARY'):
        env.pop(key, None)
    # No ambient Python or FFmpeg on PATH: only OS utilities needed for Windows.
    env['PATH'] = str(Path(env.get('SYSTEMROOT', 'C:/Windows')) / 'System32') if os.name == 'nt' else '/nonexistent'
    env['GBACACHE_SETTINGS_PATH'] = str(output / 'isolated-settings.json')
    before = sha256(executable)
    with tempfile.TemporaryDirectory(prefix='gac-smoke-cwd-') as cwd:
        completed = subprocess.run([str(executable), '--self-test', str(output)],
                                   cwd=cwd, env=env, capture_output=True, timeout=timeout)
    (output / 'binary-stdout.txt').write_bytes(completed.stdout)
    (output / 'binary-stderr.txt').write_bytes(completed.stderr)
    if completed.returncode:
        details = output / 'converter-smoke-error.txt'
        raise RuntimeError(f'Frozen self-test exit {completed.returncode}: '
                           + (details.read_text() if details.exists() else completed.stderr.decode(errors='replace')))
    evidence = read_success(result)
    if sha256(executable) != before:
        raise ValueError('Executable changed during smoke')
    ffmpeg = Path(evidence['packaging']['ffmpeg']).resolve(strict=True)
    bundle_root = executable.parent.parent if executable.parent.name == 'MacOS' else executable.parent
    if not ffmpeg.is_relative_to(bundle_root):
        raise ValueError(f'Self-test used FFmpeg outside the package: {ffmpeg}')
    if sha256(ffmpeg) != evidence['packaging']['ffmpeg_sha256']:
        raise ValueError('Bundled FFmpeg hash differs from binary self-test')
    record = {'executable': str(executable), 'sha256': before, 'exit_code': completed.returncode,
              'native_available': True, 'ffmpeg_sha256': sha256(ffmpeg),
              'working_directory': 'temporary directory outside checkout', 'timeout_seconds': timeout}
    (output / 'frozen-binary-verification.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path, nargs='?')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if args.build_dir:
        info = json.loads((args.build_dir / 'build-info.json').read_text())
        args.executable = Path(info['executable'])
        args.output_dir = args.build_dir / 'smoke'
    if not args.executable or not args.output_dir:
        parser.error('use --build-dir or EXECUTABLE --output-dir')
    print(json.dumps(smoke(args.executable, args.output_dir, args.timeout), indent=2))
