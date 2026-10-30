# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Extract the final archive, read back hashes, then run its frozen GUI again."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from bundle_ffmpeg import sha256
from smoke_package import smoke


def check_manifest(package: Path) -> None:
    package = package.resolve()
    rows = (package / 'SHA256SUMS').read_text().splitlines()
    if not rows:
        raise ValueError('Empty checksum manifest')
    for row in rows:
        digest, name = row.split('  ', 1)
        path = (package / name).resolve()
        if not path.is_relative_to(package):
            raise ValueError(f'Checksum path outside package: {name}')
        if sha256(path) != digest:
            raise ValueError(f'Package hash mismatch: {name}')


def verify(build_dir: Path) -> dict:
    build_dir = build_dir.resolve()
    info = json.loads((build_dir / 'build-info.json').read_text())
    artifacts = build_dir / 'artifacts'
    check_manifest(artifacts)
    extension = '.tar.gz' if info['target'].startswith('linux') else '.zip'
    name = f"gbacache-{info['target']}{extension}"
    archive = artifacts / name
    digest = sha256(archive)
    source = artifacts / info['ffmpeg']['source_bundle']
    if sha256(source) != info['ffmpeg']['source_bundle_sha256']:
        raise ValueError('Source bundle hash mismatch')
    with tempfile.TemporaryDirectory(prefix='gac-extracted-') as folder:
        destination = Path(folder)
        if name.endswith('.tar.gz'):
            with tarfile.open(archive) as stream:
                stream.extractall(destination, filter='data')
        elif sys.platform == 'darwin':
            subprocess.run(['ditto', '-x', '-k', str(archive), str(destination)], check=True)
        else:
            shutil.unpack_archive(archive, destination)
        package = destination / Path(info['package_root']).name
        check_manifest(package)
        relative_executable = Path(info['executable']).relative_to(Path(info['package_root']))
        executable = package / relative_executable
        if sha256(executable) != info['executable_sha256']:
            raise ValueError('Extracted executable differs from tested executable')
        evidence = smoke(executable, build_dir / 'archive-smoke')
        check_manifest(package)
    evidence['archive_sha256'] = digest
    evidence['archive'] = name
    (build_dir / 'archive-smoke/archive-verification.json').write_text(json.dumps(evidence, indent=2) + '\n')
    return evidence


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    print(json.dumps(verify(parser.parse_args().build_dir), indent=2))
