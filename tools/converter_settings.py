# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Small, version-tolerant per-user preferences. No queue or job state is stored."""
import json
import os
from pathlib import Path
import sys
import tempfile

DEFAULTS = {
    'direction': 'mp3_to_gac', 'output_mode': 'beside', 'output_dir': '',
    'recursive': True, 'rate': 32768, 'channels': 2,
    'ffmpeg_mode': 'auto', 'ffmpeg_path': '', 'quality': 0,
}
CHOICES = {
    'direction': ('mp3_to_gac', 'gac_to_mp3'),
    'output_mode': ('replace', 'beside', 'folder'),
    'rate': (32768, 44151), 'channels': (1, 2),
    'ffmpeg_mode': ('auto', 'manual'), 'quality': tuple(range(10)),
}


def settings_path():
    override = os.environ.get('GBACACHE_SETTINGS_PATH')
    if override:
        return Path(override).expanduser()
    if sys.platform == 'win32':
        base = Path(os.environ.get('APPDATA') or Path.home() / 'AppData' / 'Roaming')
    elif sys.platform == 'darwin':
        base = Path.home() / 'Library' / 'Application Support'
    else:
        base = Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config')
    return base / 'GAC-Converter' / 'settings.json'


def normalized(values):
    result = DEFAULTS.copy()
    if not isinstance(values, dict):
        return result
    for key, default in DEFAULTS.items():
        value = values.get(key, default)
        if type(value) is type(default) and (key not in CHOICES or value in CHOICES[key]):
            result[key] = value
    return result


def load():
    try:
        return normalized(json.loads(settings_path().read_text(encoding='utf-8')))
    except (OSError, ValueError):
        return DEFAULTS.copy()


def save(values):
    """Publish a complete preferences file atomically; report I/O errors to caller."""
    path = settings_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix='.settings-', suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(normalized(values), stream, ensure_ascii=False, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
