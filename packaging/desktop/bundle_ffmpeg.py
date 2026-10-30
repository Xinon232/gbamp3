# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Build a minimal native backend from verified, optionally offline source archives."""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
from pathlib import Path
import struct
import urllib.request
import os
import shutil
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
RECIPE_NAMES = ('bundle_ffmpeg.py', 'backend-build.sh', 'backend_checks.py',
                'ffmpeg-lock.json', 'BACKEND-SOURCE.md', 'compiler-notices.json',
                'GCC-COPYING3.txt', 'GCC-COPYING.RUNTIME.txt', 'MinGW-w64-COPYING.txt')


def binary_architectures(data: bytes) -> set[str]:
    """Read ELF, PE, Mach-O (including universal) headers, not wheel labels."""
    machines = {62: 'x64', 183: 'arm64', 0x8664: 'x64', 0xaa64: 'arm64',
                0x01000007: 'x64', 0x0100000c: 'arm64'}
    try:
        if data[:4] == b'\x7fELF':
            endian = '<' if data[5] == 1 else '>'
            machine = struct.unpack_from(endian + 'H', data, 18)[0]
        elif data[:2] == b'MZ':
            offset = struct.unpack_from('<I', data, 60)[0]
            if data[offset:offset + 4] != b'PE\0\0':
                raise ValueError('Invalid PE signature')
            machine = struct.unpack_from('<H', data, offset + 4)[0]
        elif data[:4] in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf'):
            endian = '<' if data[:4] == b'\xcf\xfa\xed\xfe' else '>'
            machine = struct.unpack_from(endian + 'I', data, 4)[0]
        elif data[:4] in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
            count = struct.unpack_from('>I', data, 4)[0]
            stride = 32 if data[3] == 0xbf else 20
            return {machines[struct.unpack_from('>I', data, 8 + stride * i)[0]]
                    for i in range(count)}
        else:
            raise ValueError('Unknown executable format')
        return {machines[machine]}
    except (KeyError, IndexError, struct.error) as exc:
        raise ValueError('Unsupported or truncated executable architecture') from exc


def host_target() -> str:
    arch = {'x86_64': 'x64', 'amd64': 'x64', 'arm64': 'arm64', 'aarch64': 'arm64'}
    system = {'Linux': 'linux', 'Windows': 'windows', 'Darwin': 'macos'}
    return system[platform.system()] + '-' + arch[platform.machine().lower()]


def sha256(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def source_inputs(directory: Path, offline: bool = False) -> dict:
    lock = json.loads((HERE / 'ffmpeg-lock.json').read_text())
    directory.mkdir(parents=True, exist_ok=True)
    for item in lock['sources'].values():
        path = directory / item['filename']
        if not path.exists():
            if offline:
                raise FileNotFoundError(f'Offline source missing: {path}')
            with urllib.request.urlopen(item['url'], timeout=120) as response:
                path.write_bytes(response.read())
        if sha256(path) != item['sha256']:
            raise ValueError(f'Source SHA256 mismatch: {path}; remove bad input explicitly')
    return lock


def acquire(target: str, output: Path, archives: Path | None = None,
            offline: bool = False) -> dict:
    if target != host_target() or target not in ('linux-x64', 'windows-x64', 'macos-x64', 'macos-arm64'):
        raise ValueError('Only native source builds are supported')
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    archives = (archives or output / 'archives').resolve()
    lock = source_inputs(archives, offline)
    work = output / 'build-state'
    if work.exists():
        raise FileExistsError(f'Use a fresh backend output directory: {work}')
    work.mkdir()
    for name, item in lock['sources'].items():
        with tarfile.open(archives / item['filename']) as archive:
            archive.extractall(work / name, filter='data')
        nested, = (work / name).iterdir()
        # Remove only the tarball's top-level directory; preserve source contents.
        for child in nested.iterdir():
            shutil.move(str(child), str(work / name / child.name))
        nested.rmdir()
    env = dict(os.environ)
    bash = env.get('FFMPEG_BASH', 'bash')
    script = str(HERE / 'backend-build.sh')
    if os.name == 'nt':
        import shlex
        env.update(MSYSTEM='UCRT64', CHERE_INVOKING='1')
        cmd = [bash, '--login', '-c', 'exec bash ' + shlex.quote(script.replace('\\', '/'))
               + ' ' + shlex.quote(str(work).replace('\\', '/'))]
    else:
        cmd = [bash, script, str(work)]
    subprocess.run(cmd, env=env, check=True)
    executable = output / ('ffmpeg.exe' if target.startswith('windows') else 'ffmpeg')
    architectures = binary_architectures(executable.read_bytes())
    if architectures != {target.split('-')[1]}:
        raise ValueError(f'FFmpeg architecture mismatch: {architectures}')
    from backend_checks import qualify
    qualify(executable, output / 'qualification', work / 'logs/dependencies.txt')
    notices = output / 'source-notices'
    notices.mkdir()
    compiler_notices = json.loads((HERE / 'compiler-notices.json').read_text())
    for name, item in compiler_notices.items():
        if sha256(HERE / name) != item['sha256']:
            raise ValueError(f'Compiler notice hash mismatch: {name}')
        shutil.copy2(HERE / name, notices / name)
    if (work / 'toolchain-notices').exists():
        shutil.copytree(work / 'toolchain-notices', notices / 'toolchain')
    # Preserve all top-level notices and source files containing additional license
    # terms. The full unmodified upstream archives additionally retain every notice.
    for name in lock['sources']:
        source = work / name
        dest = notices / name
        dest.mkdir()
        for p in source.iterdir():
            if p.is_file() and (p.name.upper().startswith(('COPYING', 'LICENSE', 'AUTHORS', 'README'))):
                shutil.copy2(p, dest / p.name)
        if name == 'ffmpeg':
            for relative in ('libavcodec/jfdctfst.c', 'libavcodec/jfdctint_template.c',
                             'libavcodec/jrevdct.c'):
                p = source / relative
                if p.exists():
                    shutil.copy2(p, dest / p.name)
        if name == 'zlib':
            shutil.copy2(source / 'zlib.h', dest / 'zlib.h')
    recipe_names = RECIPE_NAMES
    provenance = {'target': target, 'executable': executable.name,
                  'binary_sha256': sha256(executable), 'architectures': sorted(architectures),
                  'sources': lock['sources'], 'patches': [],
                  'recipe_sha256': {name: sha256(HERE / name) for name in recipe_names},
                  'host': platform.platform(), 'source_bundle': f'ffmpeg-source-{target}.tar.gz'}
    (output / 'ffmpeg-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    bundle = output / provenance['source_bundle']
    top = f'ffmpeg-source-{target}'
    with tarfile.open(bundle, 'w:gz') as archive:
        for name in recipe_names:
            archive.add(HERE / name, arcname=f'{top}/recipe/{name}')
        for item in lock['sources'].values():
            archive.add(archives / item['filename'], arcname=f'{top}/archives/{item["filename"]}')
        for name in ('build-state', 'source-notices', 'qualification', 'ffmpeg-provenance.json'):
            archive.add(output / name, arcname=f'{top}/{name}')
    # Read back included archive bytes, not just a successful tar write.
    with tarfile.open(bundle) as archive:
        for item in lock['sources'].values():
            with archive.extractfile(f'{top}/archives/{item["filename"]}') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != item['sha256']:
                    raise ValueError('Source bundle readback hash mismatch')
    provenance['source_bundle_sha256'] = sha256(bundle)
    (output / 'ffmpeg-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    return provenance


def read_backend(directory: Path) -> dict:
    info = json.loads((directory / 'ffmpeg-provenance.json').read_text())
    if info['target'] != host_target():
        raise ValueError('Backend must be native')
    for key, digest in (('executable', 'binary_sha256'), ('source_bundle', 'source_bundle_sha256')):
        path = directory / info[key]
        if not path.resolve().is_relative_to(directory.resolve()) or sha256(path) != info[digest]:
            raise ValueError(f'Backend {key} hash mismatch')
    expected_recipes = set(RECIPE_NAMES)
    if set(info['recipe_sha256']) != expected_recipes:
        raise ValueError('Incomplete recipe hashes')
    for name, digest in info['recipe_sha256'].items():
        if sha256(HERE / name) != digest:
            raise ValueError(f'Backend recipe hash mismatch: {name}; rebuild backend')
    record = json.loads((directory / 'qualification/backend-verification.json').read_text())
    if record.get('success') is not True or record['binary_sha256'] != info['binary_sha256']:
        raise ValueError('Backend qualification missing or mismatched')
    if info['sources'] != json.loads((HERE / 'ffmpeg-lock.json').read_text())['sources']:
        raise ValueError('Backend sources differ from source lock')
    return info


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default=host_target())
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--archives', type=Path)
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    print(json.dumps(acquire(args.target, args.output_dir, args.archives, args.offline), indent=2))
