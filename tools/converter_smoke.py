# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Real GUI self-test used by every frozen desktop package.

Only file-picker responses and replacement approval are automated. Conversion,
folder discovery, validation, worker threads, Tk callbacks, settings and FFmpeg
are production code. Run under xvfb-run on headless Linux. No Python subprocess
is used, so the same entry works inside PyInstaller on Windows and macOS.
"""
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import tkinter as tk

import converter_settings
import gac4
import gbacache
import gbacache_gui


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@contextmanager
def dialog_response(name, value):
    """Supply a user's file-picker answer, never replace conversion code."""
    original = getattr(gbacache_gui.filedialog, name)
    setattr(gbacache_gui.filedialog, name, lambda **unused: value)
    try:
        yield
    finally:
        setattr(gbacache_gui.filedialog, name, original)


def run(output_dir):
    output = Path(output_dir).expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = output / 'converter-smoke.json'
    # A failed rerun must not leave an old success marker behind.
    report.unlink(missing_ok=True)
    if not getattr(gac4, 'native_available', lambda: False)():
        raise RuntimeError('Packaged self-test requires the native GAC encoder; fallback does not qualify.')
    ffmpeg = gbacache.find_ffmpeg(os.environ.get('FFMPEG', 'ffmpeg'))
    if not ffmpeg:
        raise RuntimeError('Packaged self-test could not locate FFmpeg.')
    workspace = Path(tempfile.mkdtemp(prefix='gui-smoke-', dir=output))
    previous_settings = os.environ.get('GBACACHE_SETTINGS_PATH')
    os.environ['GBACACHE_SETTINGS_PATH'] = str(workspace / 'preferences.json')
    root = app = None
    checks = []
    artifacts = {}
    confirmations = []
    heartbeat = [0]
    errors = []
    try:
        fixture = workspace / 'synthetic.mp3'
        subprocess.run([ffmpeg, '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
                        'aevalsrc=0.2*sin(2*PI*440*t)|0.15*sin(2*PI*733*t):s=44100:d=0.24',
                        '-metadata', 'title=Synthetic GUI smoke', '-metadata', 'artist=GAC Converter test',
                        '-c:a', 'libmp3lame', '-q:a', '0', str(fixture)],
                       capture_output=True, check=True, timeout=30,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        fixture_hash = digest(fixture)

        def open_gui():
            nonlocal root, app
            root = tk.Tk()
            root.report_callback_exception = lambda kind, error, tb: errors.append(str(error))
            app = gbacache_gui.ConverterGUI(root)
            # Only the user's approval is supplied; every batch still calls the
            # same confirmation boundary, after production collision preflight.
            def approve(count, direction):
                confirmations.append({'count': count, 'direction': direction})
                return True
            app.confirm_replace = approve
            root.update()

        def pump(until, timeout=120, during=None):
            deadline = time.monotonic() + timeout
            while not until():
                root.update()
                heartbeat[0] += 1
                if during is not None:
                    during()
                require(not errors, f'Tk callback error: {errors}')
                if time.monotonic() > deadline:
                    raise TimeoutError(f'GUI self-test timeout: {app.status.get()}')
                time.sleep(.005)
            root.update()
            require(not errors, f'Tk callback error: {errors}')

        def batch(inputs, *, direction='mp3_to_gac', mode='beside', recursive=True,
                  rate=32768, destination=None, expected=(1, 0, 0), during=None):
            app.clear_button.invoke()
            if app.direction.get() != direction:
                app.direction_button.invoke()
            for source in inputs:
                if source.is_dir():
                    with dialog_response('askdirectory', str(source)):
                        app.folder_button.invoke()
                else:
                    with dialog_response('askopenfilenames', (str(source),)):
                        app.files_button.invoke()
            app.scope_radios[int(recursive)].invoke()
            app.rate_radios[int(rate == 44151)].invoke()
            app.output_radios[{'replace': 0, 'beside': 1, 'folder': 2}[mode]].invoke()
            if destination is not None:
                with dialog_response('askdirectory', str(destination)):
                    app.output_button.invoke()
            app.convert_button.invoke()
            require(app.busy, f'Batch did not start: {app.status.get()}')
            before = heartbeat[0]
            pump(lambda: not app.busy, during=during)
            require(heartbeat[0] > before, 'Tk event loop did not run during batch')
            if expected is not None:
                result = app.last_result
                require(result is not None, f'No batch result: {app.status.get()}')
                actual = tuple(result[key] for key in ('succeeded', 'skipped', 'failed'))
                require(actual == expected, f'Batch result {actual}, expected {expected}: {app.log.get("1.0", "end")}')
            return app.last_result

        def copy_song(relative):
            target = workspace / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(fixture, target)
            return target

        def verify_gac(path, rate, channels=2):
            gbacache.check_gac(path)
            header = gbacache.HEADER.unpack_from(path.read_bytes())
            require((header[1], header[4], header[5]) == (4, rate, channels), f'Wrong GAC header: {header}')
            artifacts[str(path.relative_to(workspace))] = digest(path)

        def verify_mp3(path):
            decoded = subprocess.run([ffmpeg, '-nostdin', '-v', 'error', '-i', str(path),
                                      '-map', '0:a:0', '-f', 's16le', '-acodec', 'pcm_s16le', 'pipe:1'],
                                     capture_output=True, check=True, timeout=30,
                                     creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            require(len(decoded.stdout) > 1000, f'Empty or undecodable MP3: {path}')
            tags = gbacache.read_tags(path, ffmpeg)
            require(tags.get('title') == 'Synthetic GUI smoke', f'Lost title: {tags}')
            artifacts[str(path.relative_to(workspace))] = digest(path)

        open_gui()
        require(not app.inputs and app.channels.get() == 2 and app.rate.get() == 32768,
                'Incorrect fresh-launch defaults')
        require(not app.advanced.winfo_ismapped(), 'Advanced must start collapsed')
        require(all(r.instate(['disabled']) for r in app.scope_radios), 'Folder scope enabled without folders')
        # Both forward rates; reverse beside and folder; both directions skip.
        for rate in (32768, 44151):
            source = copy_song(f'rate-{rate}/tone.mp3')
            batch([source], rate=rate)
            gac = source.with_suffix('.gac')
            verify_gac(gac, rate)
            require(digest(source) == fixture_hash, 'Forward modified original MP3')
            prior = digest(gac)
            batch([source], rate=rate, expected=(0, 1, 0))
            require(digest(gac) == prior and digest(source) == fixture_hash, 'Skip changed files')
            batch([gac], direction='gac_to_mp3', expected=(0, 1, 0))
            destination = workspace / f'reverse-{rate}'
            batch([gac], direction='gac_to_mp3', mode='folder', destination=destination)
            verify_mp3(destination / 'tone.mp3')
            require(digest(gac) == prior, 'Reverse copy changed GAC')
        checks.extend(['forward_both_rates', 'forward_beside', 'forward_skip', 'reverse_folder', 'reverse_skip'])

        # Nested folders; both scope radios and preserved relative structure.
        for recursive in (False, True):
            top = copy_song(f'tree-{recursive}/Music/top.mp3')
            nested = copy_song(f'tree-{recursive}/Music/Album/deep.mp3')
            folder = top.parent
            destination = workspace / f'tree-out-{recursive}'
            batch([folder], mode='folder', destination=destination, recursive=recursive,
                  expected=(2 if recursive else 1, 0, 0))
            verify_gac(destination / 'Music' / 'top.gac', 32768)
            deep_gac = destination / 'Music' / 'Album' / 'deep.gac'
            require(deep_gac.exists() == recursive, 'Wrong forward folder scope')
            require(digest(top) == fixture_hash and digest(nested) == fixture_hash, 'Folder conversion modified sources')
            reverse_destination = workspace / f'tree-reverse-{recursive}'
            batch([destination / 'Music'], direction='gac_to_mp3', mode='folder',
                  destination=reverse_destination, recursive=recursive, expected=(2 if recursive else 1, 0, 0))
            verify_mp3(reverse_destination / 'Music' / 'top.mp3')
            require((reverse_destination / 'Music' / 'Album' / 'deep.mp3').exists() == recursive, 'Wrong reverse folder scope')
        checks.extend(['forward_folder', 'folder_scope_both_directions', 'nested_structure'])

        # Replacement in each direction, with explicit repeated confirmation.
        source = copy_song('replace/tone.mp3')
        batch([source], mode='replace')
        gac = source.with_suffix('.gac')
        require(not source.exists(), 'Forward replacement did not remove validated source')
        verify_gac(gac, 32768)
        batch([gac], direction='gac_to_mp3', mode='replace')
        require(not gac.exists(), 'Reverse replacement did not remove validated source')
        verify_mp3(source)
        require(len(confirmations) == 2, 'Replacement must confirm both directions')
        checks.extend(['forward_replace', 'reverse_replace'])

        # Real reverse beside: source GAC from a folder output has no adjacent MP3.
        lone = workspace / 'reverse-beside' / 'alone.gac'
        lone.parent.mkdir()
        shutil.copyfile(workspace / 'rate-32768' / 'tone.gac', lone)
        batch([lone], direction='gac_to_mp3')
        verify_mp3(lone.with_suffix('.mp3'))
        require(lone.exists(), 'Reverse beside removed original')
        checks.append('reverse_beside')

        # Replacement skip safety in BOTH directions.
        source = copy_song('replace-skip/tone.mp3')
        gac = source.with_suffix('.gac')
        gac.write_bytes(b'existing destination must survive')
        batch([source], mode='replace', expected=(0, 1, 0))
        require(digest(source) == fixture_hash and gac.read_bytes() == b'existing destination must survive', 'Unsafe replacement skip')
        # Existing MP3 should be skipped even for an invalid source GAC.
        batch([lone], direction='gac_to_mp3', mode='replace', expected=(0, 1, 0))
        require(lone.exists(), 'Reverse replacement skip removed source')
        checks.append('replace_skip_preserves_sources')

        # Invalid inputs remain present, produce no destination, and are visible.
        for extension, direction in (('mp3', 'mp3_to_gac'), ('gac', 'gac_to_mp3')):
            bad = workspace / f'bad-{extension}.{extension}'
            bad.write_bytes(b'not audio')
            batch([bad], direction=direction, mode='replace', expected=(0, 0, 1))
            require(bad.read_bytes() == b'not audio', 'Invalid source removed')
            require(not bad.with_suffix('.gac' if extension == 'mp3' else '.mp3').exists(), 'Invalid output published')
        require('ERROR:' in app.log.get('1.0', 'end'), 'Failure not visible')
        checks.append('bad_input_both_directions')

        # Collision validation happens before any conversion or deletion.
        one, two = copy_song('collision/one/same.mp3'), copy_song('collision/two/same.mp3')
        collision_out = workspace / 'collision-output'
        batch([one, two], mode='folder', destination=collision_out, expected=None)
        require(app.last_result is None and 'ERROR:' in app.log.get('1.0', 'end'), 'Collision was not rejected')
        require(digest(one) == fixture_hash and digest(two) == fixture_hash, 'Collision changed source')
        require(not (collision_out / 'same.gac').exists(), 'Collision wrote an output')
        checks.append('collision_preflight')

        # Actual Stop callback while the first real song is running. The short
        # general fixture can finish before Tk's 30 ms event poll on fast hosts;
        # use a separate longer real MP3 here, without delaying conversion code.
        stop_two = copy_song('stop/second.mp3')
        stop_one = stop_two.with_name('first.mp3')
        subprocess.run([ffmpeg, '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
                        'aevalsrc=0.2*sin(2*PI*440*t)|0.15*sin(2*PI*733*t):s=44100:d=30',
                        '-c:a', 'libmp3lame', '-q:a', '0', str(stop_one)],
                       capture_output=True, check=True, timeout=30,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        stop_sent = [False]
        def stop_current():
            if app.current_song.get().startswith('File 1/2:') and not stop_sent[0]:
                app.stop_button.invoke()
                stop_sent[0] = True
        result = batch([stop_one, stop_two], expected=(1, 0, 0), during=stop_current)
        require(stop_sent[0] and result['stopped'] and result['remaining'] == 1, 'Stop did not finish exactly the current song')
        verify_gac(stop_one.with_suffix('.gac'), 32768)
        require(not stop_two.with_suffix('.gac').exists(), 'Stop started another song')
        checks.append('stop_after_current_song')

        # Persist every ordinary preference, but never the queue or a running job.
        app.output_radios[0].invoke()
        app.recursive.set(False)
        app.rate_radios[1].invoke()
        app.channels.set(1)
        app.quality.set(3)
        app.ffmpeg_mode.set('manual')
        app.ffmpeg.set(ffmpeg)
        app.output_dir.set(str(workspace / 'remembered'))
        app.direction_button.invoke()
        pump(lambda: app._save_id is None)
        saved = converter_settings.load()
        require(saved['output_mode'] == 'replace' and saved['direction'] == 'gac_to_mp3', 'Preferences not auto-saved')
        app.request_close()
        open_gui()
        require({key: var.get() for key, var in app.variables.items()} == saved, 'Preferences did not restore')
        require(not app.inputs and not app.busy, 'Launch restored a queue/job')
        before = len(confirmations)
        batch([lone], direction='gac_to_mp3', mode='replace', expected=(0, 1, 0))
        require(len(confirmations) == before + 1, 'Remembered Replace bypassed confirmation')
        checks.append('automatic_settings_restore_reconfirmation')
        require(digest(fixture) == fixture_hash, 'Fixture changed')
        result = dict(success=True, native=True, checks=checks, artifacts=artifacts,
                      confirmations=confirmations, event_loop_ticks=heartbeat[0],
                      workspace=str(workspace), ffmpeg=str(ffmpeg),
                      frozen=bool(getattr(sys, 'frozen', False)), tk=str(root.tk.call('info', 'patchlevel')))
        app.request_close()
        app = root = None
        temporary_report = workspace / 'success.json'
        temporary_report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        os.replace(temporary_report, report)
        require(json.loads(report.read_text(encoding='utf-8')) == result, 'Report read-back differs')
        return result
    finally:
        if app is not None:
            if app.busy:
                app.request_stop()
                # Never destroy Tk while a production worker is still active.
                deadline = time.monotonic() + 120
                while app.busy and time.monotonic() < deadline:
                    root.update()
                    time.sleep(.01)
            if not app.busy:
                app.request_close()
        if previous_settings is None:
            os.environ.pop('GBACACHE_SETTINGS_PATH', None)
        else:
            os.environ['GBACACHE_SETTINGS_PATH'] = previous_settings


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_dir')
    args = parser.parse_args()
    print(json.dumps(run(args.output_dir), indent=2))
