#!/usr/bin/env python3
# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Build the optional desktop kernel. MSVC Developer Prompt or CC/cc is required.
No build is attempted on import; source installs work without a compiler.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def library_name():
    return 'gac_native.dll' if sys.platform == 'win32' else 'libgac_native.dylib' if sys.platform == 'darwin' else 'libgac_native.so'


def build(output_dir):
    dest = Path(output_dir).resolve()
    dest.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).with_name('gac_native.c').resolve()
    cc = shlex.split(os.environ['CC']) if os.environ.get('CC') else [
        'cl' if sys.platform == 'win32' and shutil.which('cl') else 'cc']
    if not shutil.which(cc[0]):
        raise RuntimeError('C compiler not found: use an MSVC Developer Prompt or set CC')
    with tempfile.TemporaryDirectory(prefix='gac-build-', dir=dest) as tmp:
        target = Path(tmp)/library_name()
        if Path(cc[0]).stem.lower() in ('cl', 'clang-cl'):
            cmd = cc + ['/nologo', '/O2', '/fp:precise', '/LD', '/std:c11', str(source), '/link', '/OUT:'+str(target)]
        else:
            cmd = cc + ['-O3', '-std=c11', '-fno-fast-math', '-Wall', '-Wextra', '-Werror']
            cmd += ['-dynamiclib', '-fPIC'] if sys.platform == 'darwin' else ['-shared'] + ([] if sys.platform == 'win32' else ['-fPIC'])
            cmd += [str(source), '-o', str(target), '-lm']
        subprocess.run(cmd, cwd=tmp, check=True)
        os.replace(target, dest/library_name())
    return dest/library_name()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=Path(__file__).parent)
    print(build(parser.parse_args().output_dir))
