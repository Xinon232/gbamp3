# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Create release-candidate archives only after the exact frozen binary passes."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import subprocess
from bundle_ffmpeg import sha256
from smoke_package import read_success


def write_artifact_manifest(artifacts: Path, app: Path, backend: dict) -> None:
    source = artifacts / backend['source_bundle']
    if sha256(source) != backend['source_bundle_sha256']:
        raise ValueError('Release source bundle hash mismatch')
    (artifacts / 'SHA256SUMS').write_text(''.join(
        f'{sha256(path)}  {path.name}\n' for path in (app, source)))


def package_verified(build_dir: Path) -> Path:
    build_dir = build_dir.resolve()
    info = json.loads((build_dir / 'build-info.json').read_text())
    package = Path(info['package_root'])
    executable = Path(info['executable'])
    evidence = build_dir / 'smoke'
    read_success(evidence / 'converter-smoke.json')
    verification = json.loads((evidence / 'frozen-binary-verification.json').read_text())
    if verification['exit_code'] != 0 or verification['sha256'] != sha256(executable):
        raise ValueError('Only the tested unchanged executable can be archived')
    if verification['sha256'] != info['executable_sha256']:
        raise ValueError('Executable changed after build')
    package_evidence = package / 'VERIFICATION'
    package_evidence.mkdir(exist_ok=True)
    for name in ('converter-smoke.json', 'frozen-binary-verification.json'):
        shutil.copy2(evidence / name, package_evidence / name)
    # Hash symlink targets and preserve symlinks in macOS/Linux archives.
    hashes = {str(path.relative_to(package)).replace('\\', '/'): sha256(path)
              for path in sorted(package.rglob('*')) if path.is_file()
              and path.name != 'SHA256SUMS'}
    (package / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in hashes.items()))
    artifacts = build_dir / 'artifacts'
    artifacts.mkdir(exist_ok=True)
    target = info['target']
    base = artifacts / f'gbacache-{target}'
    if target.startswith('macos'):
        archive = base.with_suffix('.zip')
        subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent',
                        str(package), str(archive)], check=True)
    else:
        fmt = 'gztar' if target.startswith('linux') else 'zip'
        archive = Path(shutil.make_archive(str(base), fmt, root_dir=package.parent, base_dir=package.name))
    write_artifact_manifest(artifacts, archive, info['ffmpeg'])
    # Read back member names: missing executable/notices is never a release asset.
    if archive.name.endswith('.tar.gz'):
        import tarfile
        with tarfile.open(archive) as stream:
            names = stream.getnames()
    else:
        import zipfile
        with zipfile.ZipFile(archive) as stream:
            names = stream.namelist()
    for required in (str(executable.relative_to(package.parent)).replace('\\', '/'),
                     f'{package.name}/LICENSES/FFmpeg-LICENSE-and-build.txt',
                     f'{package.name}/VERIFICATION/converter-smoke.json'):
        if required not in names:
            raise ValueError(f'Archive is missing {required}')
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    print(package_verified(parser.parse_args().build_dir))
