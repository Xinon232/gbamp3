# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Optional ctypes bridge. Load only adjacent/frozen-resource libraries, never PATH."""
import ctypes
import os
from pathlib import Path
import sys
import numpy as np


def _load():
    if os.environ.get('GBACACHE_DISABLE_NATIVE') == '1':
        return None
    name = 'gac_native.dll' if sys.platform == 'win32' else 'libgac_native.dylib' if sys.platform == 'darwin' else 'libgac_native.so'
    paths = [Path(__file__).resolve().parent]
    if getattr(sys, 'frozen', False):
        paths += [Path(getattr(sys, '_MEIPASS', paths[0])), Path(sys.executable).parent]
    for base in paths:
        try:
            lib = ctypes.CDLL(str(base/name))
            lib.gac_native_abi.restype = ctypes.c_int
            if lib.gac_native_abi() != 1:
                continue
            d = np.ctypeslib.ndpointer(dtype=np.float64, flags='C_CONTIGUOUS')
            i = np.ctypeslib.ndpointer(dtype=np.int64, flags='C_CONTIGUOUS')
            lib.gac_inner.argtypes = [ctypes.c_int64, ctypes.c_int64, d, i, d, ctypes.c_double, d, d]
            lib.gac_inner.restype = None
            out = np.ctypeslib.ndpointer(dtype=np.int32, flags='C_CONTIGUOUS')
            lib.gac_decode.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, out]
            lib.gac_decode.restype = ctypes.c_int
            return lib
        except (OSError, AttributeError):
            continue
    return None


LIB = _load()


def inner(tf, c, step, dz, hk, qk):
    LIB.gac_inner(len(tf), step.shape[1], np.ascontiguousarray(tf),
                  np.ascontiguousarray(c), np.ascontiguousarray(step), dz, hk, qk)
