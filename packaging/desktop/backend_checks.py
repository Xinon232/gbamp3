# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Fail-closed backend policy and real synthetic codec/filter qualification."""
from __future__ import annotations
import json
from pathlib import Path
import re
import subprocess
import wave
from bundle_ffmpeg import host_target, sha256


def check_policy(config: str, license_text: str) -> None:
    license_text = ' '.join(license_text.split())
    for flag in ('--disable-gpl', '--disable-version3', '--disable-nonfree',
                 '--disable-autodetect', '--disable-network', '--enable-libmp3lame', '--enable-zlib'):
        if flag not in config:
            raise ValueError(f'Missing backend policy: {flag}')
    forbidden = ('--enable-gpl', '--enable-version3', '--enable-nonfree')
    external = set(re.findall(r'--enable-(lib[^\s]+)', config))
    if any(flag in config for flag in forbidden) or external != {'libmp3lame'}:
        raise ValueError('Unexpected GPL/nonfree/external library configuration')
    if 'GNU Lesser General Public License' not in license_text or 'version 2.1' not in license_text:
        raise ValueError('Backend must identify LGPL 2.1 or later')


def check_dependencies(target: str, text: str) -> None:
    if target.startswith('linux'):
        allowed = {'linux-vdso.so.1', 'libc.so.6', 'libm.so.6', 'libpthread.so.0',
                   'libdl.so.2', 'librt.so.1', 'ld-linux-x86-64.so.2'}
        libraries = {Path(line.strip().split()[0]).name for line in text.splitlines() if line.strip()}
        if not libraries or not libraries <= allowed or 'not found' in text:
            raise ValueError(f'Unexpected Linux dependency: {libraries - allowed}')
    elif target.startswith('windows'):
        libraries = {s.lower() for s in re.findall(r'DLL Name:\s*(\S+)', text)}
        allowed = {'kernel32.dll', 'ucrtbase.dll', 'msvcrt.dll', 'advapi32.dll',
                   'user32.dll', 'shell32.dll', 'ole32.dll', 'bcrypt.dll'}
        unexpected = {s for s in libraries if s not in allowed and not s.startswith('api-ms-win-crt-')}
        if not libraries or unexpected:
            raise ValueError(f'Unexpected Windows dependency: {unexpected}')
    else:
        libraries = [line.strip().split(' (')[0] for line in text.splitlines()[1:] if line.strip()]
        if not libraries or any(not s.startswith(('/usr/lib/', '/System/Library/')) for s in libraries):
            raise ValueError(f'Unexpected macOS dependency: {libraries}')


def qualify(ffmpeg: Path, output: Path, dependencies: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    commands = []
    def run(*args):
        command = [str(ffmpeg.resolve()), '-hide_banner', '-y', *map(str, args)]
        result = subprocess.run(command, cwd=output, capture_output=True, text=True, timeout=90)
        commands.append({'command': command, 'exit_code': result.returncode,
                         'stdout': result.stdout, 'stderr': result.stderr})
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(f'Backend check failed: {args}\n{result.stderr}')
        return result.stdout + result.stderr
    listings = {flag: run(flag) for flag in ('-version', '-buildconf', '-L', '-encoders',
                                          '-decoders', '-formats', '-filters', '-protocols')}
    check_policy(listings['-buildconf'], listings['-L'])
    check_dependencies(host_target(), dependencies.read_text())
    required = {'-encoders': ('libmp3lame', 'png', 'mjpeg', 'pcm_s16le', 'rawvideo'),
                '-decoders': ('mp3', 'png', 'mjpeg', 'pcm_s16le', 'pcm_f64le'),
                '-formats': ('mp3', 'wav', 'ffmetadata', 'framehash', 'rawvideo'),
                '-filters': ('aresample', 'apad', 'atrim', 'scale', 'pad', 'sine', 'color', 'aevalsrc')}
    for flag, names in required.items():
        tokens = set(listings[flag].split())
        for name in names:
            if name not in tokens:
                raise ValueError(f'Missing backend capability: {name}')
    run('-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100:duration=1',
        '-ac', '2', '-c:a', 'libmp3lame', '-q:a', '0', '-metadata', 'title=SourceBuildTest', 'tone.mp3')
    run('-f', 'lavfi', '-i',
        'aevalsrc=0.2*sin(2*PI*440*t)|0.15*sin(2*PI*733*t):s=44100:d=0.24',
        '-c:a', 'libmp3lame', '-q:a', '0', 'stereo.mp3')
    for rate in (32768, 44151):
        run('-i', 'tone.mp3', '-vn', '-af', f'aresample={rate},apad,atrim=duration=2',
            '-ac', '2', '-c:a', 'pcm_s16le', f'{rate}.wav')
        with wave.open(str(output / f'{rate}.wav')) as wav:
            if (wav.getnchannels(), wav.getframerate(), wav.getnframes()) != (2, rate, rate * 2):
                raise ValueError('PCM sample count/rate/channel mismatch')
    run('-i', 'tone.mp3', '-map_metadata', '0', '-f', 'ffmetadata', 'metadata.txt')
    if 'title=SourceBuildTest' not in (output / 'metadata.txt').read_text():
        raise ValueError('MP3 metadata lost')
    run('-i', '32768.wav', '-c:a', 'pcm_s16le', '-f', 'framehash', 'audio.framehash')
    run('-f', 'lavfi', '-i', 'color=c=red:s=64x48:d=0.1', '-frames:v', '1', '-c:v', 'png', 'cover.png')
    run('-i', 'cover.png', '-frames:v', '1', '-c:v', 'mjpeg', 'cover.jpg')
    for kind in ('png', 'jpg'):
        run('-i', 'tone.mp3', '-i', f'cover.{kind}', '-map', '0:a', '-map', '1:v',
            '-c', 'copy', '-id3v2_version', '3', '-disposition:v', 'attached_pic', f'{kind}.mp3')
        run('-i', f'{kind}.mp3', '-map', '0:v:0', '-frames:v', '1', '-vf',
            'scale=32:32:force_original_aspect_ratio=decrease,pad=32:32:(ow-iw)/2:(oh-ih)/2',
            '-pix_fmt', 'rgb24', '-c:v', 'rawvideo', '-f', 'rawvideo', f'{kind}.rgb')
        if (output / f'{kind}.rgb').stat().st_size != 3072:
            raise ValueError('Cover dimensions mismatch')
    run('-i', 'cover.png', '-vf', 'scale=32:32', '-c:v', 'rawvideo', '-f', 'framehash', 'cover.framehash')
    for name in ('cover.framehash', 'audio.framehash'):
        if not any(line and not line.startswith('#') for line in (output / name).read_text().splitlines()):
            raise ValueError('No framehash data')
    result = {'success': True, 'target': host_target(), 'binary_sha256': sha256(ffmpeg),
              'checks': ['V0 encode', 'MP3 decode', 'stereo PCM samples at both GAC rates',
                         'metadata', 'PNG/JPEG attached pictures', 'scale/pad/RGB', 'framehash',
                         'configuration/license', 'system-only imports'], 'listings': listings}
    (output / 'backend-verification.json').write_text(json.dumps(result, indent=2) + '\n')
    return result
