# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Portable desktop entry point; --self-test runs the real integrated GUI suite."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback


def configure_runtime() -> Path | None:
    if not getattr(sys, 'frozen', False):
        tools = Path(__file__).resolve().parents[2] / 'tools'
        if tools.is_dir():
            sys.path.insert(0, str(tools))
        return None
    root = Path(sys._MEIPASS)
    executable = root / ('ffmpeg.exe' if sys.platform == 'win32' else 'ffmpeg')
    if not executable.is_file():
        raise FileNotFoundError(f'Bundled FFmpeg missing: {executable}')
    os.environ['FFMPEG'] = str(executable)
    os.environ['PATH'] = str(root) + os.pathsep + os.environ.get('PATH', '')
    return executable


def self_test(output_dir: Path) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)
    result_file = output_dir / 'converter-smoke.json'
    result_file.unlink(missing_ok=True)  # a previous success must never pass a failed run
    os.environ['GBACACHE_SETTINGS_PATH'] = str(output_dir / 'settings.json')
    bundled = configure_runtime()
    import gac4
    import converter_smoke
    if not gac4.native_available():
        raise RuntimeError('Packaged self-test REQUIRES native acceleration')
    result = converter_smoke.run(output_dir)
    if not isinstance(result, dict) or not result_file.is_file():
        raise RuntimeError('GUI smoke did not return a dict and write success JSON')
    # Preserve GUI-owned coverage evidence, add independent packaging evidence.
    evidence = json.loads(result_file.read_text(encoding='utf-8'))
    evidence['packaging'] = {
        'frozen': bool(getattr(sys, 'frozen', False)),
        'native_available': True,
        'executable': str(Path(sys.executable).resolve()),
        'ffmpeg': str(bundled) if bundled else os.environ.get('FFMPEG'),
        'ffmpeg_sha256': hashlib.sha256(bundled.read_bytes()).hexdigest() if bundled else None,
    }
    result_file.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    return evidence


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description='gbamp3 converter 3.1 GAC Converter')
    parser.add_argument('--self-test', metavar='OUTPUT_DIR', type=Path)
    args = parser.parse_args(argv)
    try:
        configure_runtime()
        if args.self_test:
            self_test(args.self_test.resolve())
        else:
            import tkinter as tk
            from gbacache_gui import ConverterGUI
            root = tk.Tk()
            ConverterGUI(root)
            root.mainloop()
        return 0
    except Exception:
        details = traceback.format_exc()
        if args.self_test:
            args.self_test.mkdir(parents=True, exist_ok=True)
            (args.self_test / 'converter-smoke.json').unlink(missing_ok=True)
            (args.self_test / 'converter-smoke-error.txt').write_text(details, encoding='utf-8')
        else:
            from tkinter import messagebox
            messagebox.showerror('GAC Converter could not start', details)
        if sys.stderr is not None:
            sys.stderr.write(details)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
