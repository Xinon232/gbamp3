# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Legacy Windows workflow entry point; v3 smoke delegates to the shared GUI suite.

The old three-positional-argument self-test CLI is retained for windows-converter.yml.
Release-candidate packages use packaging/desktop/launcher.py and require native code.
"""
import json
import os
from pathlib import Path
import sys
import tkinter as tk
from gbacache_gui import ConverterGUI
import gbacache


def self_test(source, ffmpeg, folder):
    """Compatibility adapter: use synthetic fixtures, not the old GUI's attributes."""
    from converter_smoke import run
    folder = Path(folder).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    result = folder / 'windows-smoke.json'
    result.unlink(missing_ok=True)
    os.environ['GBACACHE_SETTINGS_PATH'] = str(folder / 'settings.json')
    if ffmpeg == 'auto':
        # The inherited workflow deliberately places FFmpeg beside its onefile EXE.
        executable = Path(sys.executable).parent / 'ffmpeg.exe'
        if not executable.is_file():
            raise FileNotFoundError('Legacy Windows package is missing adjacent ffmpeg.exe')
        os.environ['FFMPEG'] = str(executable)
        os.environ['PATH'] = str(executable.parent) + os.pathsep + os.environ.get('PATH', '')
    else:
        executable = gbacache.find_ffmpeg(ffmpeg)
        if not executable:
            raise FileNotFoundError(ffmpeg)
        os.environ['FFMPEG'] = str(executable)
    import gac4
    if gac4.native_available():
        report = run(folder)
        if not isinstance(report, dict) or not (folder / 'converter-smoke.json').is_file():
            raise RuntimeError('Shared GUI smoke did not write success JSON')
    else:
        # Old windows-converter.yml does not build a native helper. Keep its
        # basic Python-fallback GUI smoke working, but NEVER qualify a v3 release.
        import hashlib
        import struct
        import subprocess
        import time
        fixture = folder / 'legacy-synthetic.mp3'
        subprocess.run([str(executable), '-nostdin', '-v', 'error', '-y', '-f', 'lavfi',
                        '-i', 'sine=frequency=440:duration=0.08', '-ac', '2',
                        '-c:a', 'libmp3lame', str(fixture)], check=True, timeout=30,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        root = tk.Tk()
        try:
            app = ConverterGUI(root)
            app.add_inputs([fixture])
            app.rate.set(44151)
            app.output_mode.set('beside')
            app.start_conversion()
            deadline = time.monotonic() + 90
            while app.busy and time.monotonic() < deadline:
                root.update()
                time.sleep(.01)
            if app.busy:
                raise TimeoutError('Legacy GUI smoke timed out')
            blob = fixture.with_suffix('.gac').read_bytes()
            if blob[:10] != b'GBCACHE\0\x04\0' or struct.unpack_from('<I', blob, 16)[0] != 44151:
                raise ValueError('Legacy GUI smoke produced wrong format/rate')
            report = {'legacy_gui_conversion': 'PASS', 'native_available': False,
                      'v3_release_qualified': False, 'rate': 44151,
                      'output_sha256': hashlib.sha256(blob).hexdigest()}
        finally:
            root.destroy()
    result.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--self-test':
        if len(sys.argv) != 5:
            raise ValueError('Legacy CLI: --self-test SOURCE auto OUTPUT_DIR')
        self_test(*sys.argv[2:5])
        return
    root = tk.Tk()
    ConverterGUI(root)
    root.mainloop()


if __name__ == '__main__':
    main()
