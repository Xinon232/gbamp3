# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Include exact bundled Tcl/Tk, numpy and PyInstaller redistribution notices."""
from pathlib import Path
import sys,tkinter as tk,urllib.request
out=Path(sys.argv[1]);root=tk.Tk();root.withdraw()
versions={'tcl':root.tk.eval('info patchlevel'),'tk':root.tk.eval('package require Tk')}
root.destroy()
for name,version in versions.items():
    tag='core-'+version.replace('.','-')
    url=f'https://raw.githubusercontent.com/tcltk/{name}/{tag}/license.terms'
    data=urllib.request.urlopen(url,timeout=30).read()
    assert b'copyright' in data.lower()
    (out/(name+'-'+version+'-license.terms')).write_bytes(data)
url='https://raw.githubusercontent.com/pyinstaller/pyinstaller/v6.16.0/COPYING.txt'
data=urllib.request.urlopen(url,timeout=30).read()
assert b'exception' in data.lower()
(out/'PyInstaller-COPYING.txt').write_bytes(data)
# numpy (BSD-3-Clause, with bundled component notices) ships inside the executable.
import importlib.metadata as md
dist=md.distribution('numpy');found=0
for f in dist.files or ():
    if 'LICENSE' in f.name.upper() or 'COPYING' in f.name.upper() or 'licenses' in f.parts:
        data=f.locate().read_bytes()
        (out/('numpy-'+dist.version+'-'+f.name)).write_bytes(data);found+=1
assert found, 'numpy license files missing from the installed distribution'
versions['numpy']=dist.version
print('Runtime notices retained',versions)
# ffmpeg.exe (from imageio-ffmpeg) is bundled next to the converter: keep its own
# license statement (ffmpeg -L), build configuration and a source pointer.
import subprocess,imageio_ffmpeg
exe=imageio_ffmpeg.get_ffmpeg_exe()
lic=subprocess.run([exe,'-hide_banner','-L'],capture_output=True,text=True).stdout
ver=subprocess.run([exe,'-version'],capture_output=True,text=True).stdout
assert 'General Public License' in lic or 'Lesser General Public License' in lic, 'ffmpeg license text missing'
(out/'FFmpeg-LICENSE.txt').write_text(lic+'\n\n'+ver+'\n\nffmpeg.exe is an unmodified build distributed by imageio-ffmpeg '
    +imageio_ffmpeg.__version__+' (https://github.com/imageio/imageio-ffmpeg). FFmpeg source code for this version: '
    'https://ffmpeg.org/download.html and https://git.ffmpeg.org/ffmpeg.git\n')
print('FFmpeg notice retained:',ver.splitlines()[0])
