# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Native-only PyInstaller builder. Never publishes or mutates player assets."""
from __future__ import annotations
import argparse
import importlib.metadata as md
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
from bundle_ffmpeg import acquire, read_backend, binary_architectures, host_target, sha256

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
NATIVE = {'linux': 'libgac_native.so', 'windows': 'gac_native.dll', 'macos': 'libgac_native.dylib'}


def preflight(source: Path):
    for name in ('build_native.py', 'gac4.py', 'gbacache.py', 'gbacache_gui.py',
                 'converter_settings.py', 'converter_smoke.py'):
        if not (source / 'tools' / name).is_file():
            raise FileNotFoundError(f'Missing integrated dependency tools/{name}; '
                                    'merge converter core + GUI before packaging')


def build(output: Path, source: Path = ROOT, backend_dir: Path | None = None) -> dict:
    preflight(source)
    target = host_target()
    system, arch = target.split('-')
    if target not in ('linux-x64', 'windows-x64', 'macos-x64', 'macos-arm64'):
        raise ValueError(f'Unsupported native build target: {target}')
    output = output.resolve()
    source = source.resolve()
    output.mkdir(parents=True, exist_ok=True)
    package = output / f'gbacache-{target}'
    if package.exists():
        raise FileExistsError(f'Use a fresh output directory; package already exists: {package}')
    # Fail on broken/unavailable Tk before any expensive downloads or freezing.
    import tkinter as tk
    root = tk.Tk()
    root.update()
    root.destroy()
    for line in (HERE / 'requirements.txt').read_text().splitlines():
        if not line or line.startswith('#') or ';' in line:
            continue
        name, version = line.split('==')
        if md.version(name) != version:
            raise ValueError(f'Install packaging/desktop/requirements.txt: expected {line}')
    ffmpeg_dir = backend_dir.resolve() if backend_dir else output / 'ffmpeg-input'
    provenance = read_backend(ffmpeg_dir) if backend_dir else acquire(target, ffmpeg_dir)
    artifacts = output / 'artifacts'
    artifacts.mkdir(exist_ok=True)
    shutil.copy2(ffmpeg_dir / provenance['source_bundle'], artifacts / provenance['source_bundle'])
    ffmpeg = ffmpeg_dir / provenance['executable']
    subprocess.run([str(ffmpeg), '-version'], check=True, timeout=30)
    native_dir = output / 'native'
    subprocess.run([sys.executable, str(source / 'tools/build_native.py'),
                    '--output-dir', str(native_dir)], check=True)
    native = native_dir / NATIVE[system]
    if arch not in binary_architectures(native.read_bytes()):
        raise ValueError('Native helper architecture mismatch')
    # ctypes loading is additionally required inside the actual frozen smoke.
    from runtime_notices import collect
    notices = output / 'notices'
    versions = collect(notices, ffmpeg, ffmpeg_dir, source)
    cmd = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir',
           '--windowed', '--noupx', '--name', 'GAC-Converter',
           '--distpath', str(output / 'dist'), '--workpath', str(output / 'pyinstaller'),
           '--specpath', str(output), '--paths', str(source / 'tools'),
           '--hidden-import', 'converter_smoke', '--hidden-import', 'converter_settings',
           # setuptools' vendored jaraco.context imports this namespace on Python 3.11.
           '--hidden-import', 'backports.tarfile',
           '--add-binary', str(native) + ':.', '--add-binary', str(ffmpeg) + ':.',
           '--add-data', str(notices) + ':LICENSES']
    if system == 'macos':
        cmd += ['--target-arch', 'arm64' if arch == 'arm64' else 'x86_64',
                '--osx-bundle-identifier', 'com.gbamp3.gac-converter']
    cmd += [str(HERE / 'launcher.py')]
    build_env = dict(os.environ)
    if system == 'linux':
        # Standalone CPython/uv can rely on executable RUNPATH for Tcl/Tk.
        # ldd on _tkinter alone cannot see it; expose the exact Python prefix
        # to analysis so those libraries are collected, not runtime dependencies.
        prefix_lib = str(Path(sys.base_prefix) / 'lib')
        build_env['LD_LIBRARY_PATH'] = prefix_lib + os.pathsep + build_env.get('LD_LIBRARY_PATH', '')
    subprocess.run(cmd, check=True, cwd=source, env=build_env)
    if system == 'macos':
        package.mkdir()
        app = package / 'GAC-Converter.app'
        shutil.copytree(output / 'dist/GAC-Converter.app', app, symlinks=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
        executable = app / 'Contents/MacOS/GAC-Converter'
        # Verify FFmpeg is standalone: no hidden Homebrew/install-prefix dependencies.
        dependencies = subprocess.check_output(['otool', '-L', str(ffmpeg)], text=True)
        for line in dependencies.splitlines()[1:]:
            library = line.strip().split(' (')[0]
            if not library.startswith(('/usr/lib/', '/System/Library/')):
                raise ValueError(f'FFmpeg has external non-system dependency: {library}')
    else:
        shutil.copytree(output / 'dist/GAC-Converter', package, symlinks=True)
        executable = package / ('GAC-Converter.exe' if system == 'windows' else 'GAC-Converter')
    if arch not in binary_architectures(executable.read_bytes()):
        raise ValueError('Frozen launcher architecture mismatch')
    shutil.copytree(notices, package / 'LICENSES', symlinks=True)
    shutil.copy2(HERE / 'README-DESKTOP.txt', package / 'README.txt')
    for filename in ('CACHE.md', 'CONTROLS.md', 'NOTICES.md', 'MODIFICATIONS.md'):
        shutil.copy2(source / filename, package / filename)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    inputs = {str(p.relative_to(source)): sha256(p) for p in sorted((source / 'tools').glob('*'))
              if p.suffix in ('.py', '.c', '.h')}
    packaging_inputs = {p.name: sha256(p) for p in sorted(HERE.iterdir()) if p.is_file()}
    dirty = subprocess.check_output(['git', 'status', '--porcelain', '--', 'tools'],
                                    cwd=source, text=True).strip()
    info = {'release_title': 'gbamp3 converter 3.1', 'target': target,
            'source_commit': commit, 'source_tools_dirty': bool(dirty),
            'source_file_sha256': inputs, 'packaging_file_sha256': packaging_inputs,
            'host': platform.platform(), 'runtime': versions,
            'package_root': str(package), 'executable': str(executable),
            'executable_sha256': sha256(executable), 'native_sha256_before_freeze': sha256(native),
            'ffmpeg': provenance, 'macos_signing': 'ad-hoc; NOT notarized' if system == 'macos' else None}
    (output / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    (package / 'BUILD-INFO.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    return info


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT,
                        help='Integrated checkout (defaults to this checkout)')
    parser.add_argument('--backend-dir', type=Path, help='Verified native source-build output')
    args = parser.parse_args()
    print(json.dumps(build(args.output_dir, args.source_root, args.backend_dir), indent=2))
