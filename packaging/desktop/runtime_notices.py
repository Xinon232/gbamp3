# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Collect redistribution notices for the actual frozen runtime, fail closed."""
from __future__ import annotations
import importlib.metadata as md
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tkinter as tk
import urllib.request
from bundle_ffmpeg import sha256


def collect(output: Path, ffmpeg: Path, provenance_dir: Path, source: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source / 'LICENSES', output / 'project', dirs_exist_ok=True)
    shutil.copy2(source / 'NOTICES.md', output / 'PROJECT-NOTICES.md')
    root = tk.Tk()
    root.title('GAC Converter packaging Tk probe')
    root.update()
    versions = {'python': platform.python_version(), 'tcl': root.tk.eval('info patchlevel'),
                'tk': root.tk.eval('package require Tk')}
    root.destroy()
    fetched = []

    def fetch(url: str, name: str):
        with urllib.request.urlopen(url, timeout=60) as response:
            content = response.read()
        if b'copyright' not in content.lower():
            raise ValueError(f'License did not contain a copyright notice: {url}')
        destination = output / name
        destination.write_bytes(content)
        fetched.append({'url': url, 'path': name, 'sha256': sha256(destination)})

    for name in ('tcl', 'tk'):
        tag = 'core-' + versions[name].replace('.', '-')
        fetch(f'https://raw.githubusercontent.com/tcltk/{name}/{tag}/license.terms',
              f'{name}-{versions[name]}-license.terms')
    fetch(f'https://raw.githubusercontent.com/python/cpython/v{versions["python"]}/LICENSE',
          'Python-LICENSE.txt')
    # Keep distribution-relative paths; basename flattening loses bundled-library notices.
    for package in ('numpy', 'pyinstaller', 'setuptools', 'packaging', 'altgraph',
                    'pyinstaller-hooks-contrib'):
        dist = md.distribution(package)
        versions[package] = dist.version
        count = 0
        for item in dist.files or ():
            if item.name.upper().startswith(('LICENSE', 'COPYING')) or 'licenses' in item.parts:
                src = Path(item.locate())
                if not src.is_file():
                    continue
                dst = output / package / Path(*[part for part in item.parts if part != '..'])
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
                count += 1
        if not count:
            raise FileNotFoundError(f'{package} license files absent from distribution')
    shutil.copytree(provenance_dir / 'source-notices', output / 'backend-source', dirs_exist_ok=True)
    qualification = output / 'backend-qualification'
    qualification.mkdir(exist_ok=True)
    for path in (provenance_dir / 'qualification').glob('*.json'):
        shutil.copy2(path, qualification / path.name)
    shutil.copy2(provenance_dir / 'build-state/logs/dependencies.txt', output / 'backend-dependencies.txt')
    shutil.copy2(Path(__file__).with_name('BACKEND-SOURCE.md'), output)
    shutil.copy2(provenance_dir / 'ffmpeg-provenance.json', output)
    texts = {}
    for flag in ('-version', '-buildconf', '-L'):
        result = subprocess.run([str(ffmpeg), '-hide_banner', flag], check=True,
                                capture_output=True, text=True, timeout=30)
        texts[flag] = result.stdout + result.stderr
    from backend_checks import check_policy
    check_policy(texts['-buildconf'], texts['-L'])
    (output / 'FFmpeg-LICENSE-and-build.txt').write_text('\n\n'.join(texts.values()), encoding='utf-8')
    shutil.copy2(Path(__file__).with_name('THIRD-PARTY-SOURCES.txt'), output)
    (output / 'runtime-provenance.json').write_text(json.dumps({
        'versions': versions, 'license_downloads': fetched,
        'ffmpeg_sha256': sha256(ffmpeg), 'ffmpeg_version': texts['-version'].splitlines()[0],
    }, indent=2) + '\n', encoding='utf-8')
    return versions
