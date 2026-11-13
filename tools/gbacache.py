#!/usr/bin/env python3
"""Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). MP3 -> GAC v4 (independently restartable 1024-byte blocks).
Requires Python 3.11+, numpy and ffmpeg (PATH, next to this program, or --ffmpeg).
Existing .gac files are skipped unless --overwrite; MP3s are deleted only with
--replace, and only after their GAC has been written and verified.
"""
import argparse, os, pathlib, queue, re, shutil, struct, subprocess, sys, tempfile, threading, zlib
from array import array
import gac4
HEADER=struct.Struct('<8sHH13I')
BLOCK=gac4.BLOCK
RATES=(32768,44151)
ART_BYTES=(8192,2048)   # 64x64 covers (gbamp3 v1.8+); 32x32 from earlier converters
MAX_TOTAL=0x7fffffff    # frames per file the player accepts (player/src/gac4.c gac_header)
Cancelled=gac4.Cancelled

class TooLong(ValueError):
    """The audio does not fit in one GAC the player accepts."""

def max_blocks(art_bytes=0):
    """Most blocks the player accepts (gac_header: blocks <= (0x7fffffff - seek) / 1024 / 2,
    about 1 GiB of audio; the file then stays under 2 GiB)."""
    return (0x7fffffff-(576+art_bytes))//BLOCK//2

def _hours(frames,rate): return frames/rate/3600

def _too_long(rate,channels,frames_per_block=None,duration=None):
    """The 'too long' message: the longest file at this rate and layout (frame limit,
    or the block limit at the frames per block measured so far)."""
    limit=MAX_TOTAL
    if frames_per_block: limit=min(limit,int(max_blocks(8192)*frames_per_block))
    layout='stereo' if channels==2 else 'mono'
    text=f'too long for gbamp3: about {_hours(limit,rate):.1f} hours max at this rate ({rate} Hz {layout})'
    if duration: text+=f'; this file is about {_hours(duration,rate):.1f} hours. Split it into shorter files'
    return TooLong(text)

def run(cmd,check=False):
    """Run ffmpeg quietly; on Windows without flashing a console window."""
    flags=getattr(subprocess,'CREATE_NO_WINDOW',0)
    return subprocess.run(cmd,capture_output=True,stdin=subprocess.DEVNULL,creationflags=flags,check=check)

def checksum(data):
    value=2166136261
    for b in data: value=((value^b)*16777619)&0xffffffff
    return value

def metadata(tags):
    """GACMETA3: UTF-8 title/artist/album/album artist/genre plus track and disc
    numbers (the GBA folds case/accents itself when it builds its library)."""
    def number(key):
        v=str(tags.get(key,'')).strip()
        try: a,_,b=v.partition('/'); return max(0,min(int(a or 0),65535)),max(0,min(int(b or 0),65535))
        except ValueError: return 0,0
    def field(value,size):
        # The player accepts no control characters (C0, DEL, C1): drop them.
        s=''.join(c for c in str(value) if not (ord(c)<32 or 127<=ord(c)<160) or c in '\t\n\r')
        s=' '.join(s.split())
        raw=s.encode('utf-8')[:size-1]
        while raw:  # never cut a UTF-8 sequence
            try: raw.decode('utf-8'); break
            except UnicodeDecodeError: raw=raw[:-1]
        return raw.ljust(size,b'\0')
    track,track_total=number('track'); disc,disc_total=number('disc')
    if not track_total: track_total=number('tracktotal')[0] or number('totaltracks')[0]
    if not disc_total: disc_total=number('disctotal')[0] or number('totaldiscs')[0]
    body=(b'GACMETA3'+struct.pack('<4H',track,track_total,disc,disc_total)+field(tags.get('title',''),128)+field(tags.get('artist',''),96)
          +field(tags.get('album',''),96)+field(tags.get('album_artist',tags.get('albumartist','')),96)+field(tags.get('genre',''),64))
    assert len(body)==496
    return body.ljust(512,b'\0')

def cover(src,ffmpeg):
    """Optional image -> exactly 64x64 RGB555 little endian (8192 bytes;
    32x32 before gbamp3 v1.8, still played)."""
    raw=run([ffmpeg,'-nostdin','-v','error','-i',str(src),'-map','0:v:0','-frames:v','1','-vf','scale=64:64:force_original_aspect_ratio=decrease:flags=area,pad=64:64:(ow-iw)/2:(oh-ih)/2','-f','rawvideo','-pix_fmt','rgb24','pipe:1'],check=True).stdout
    if len(raw)!=64*64*3: raise ValueError('cover must decode to one image')
    return b''.join(struct.pack('<H',(raw[i]>>3)|((raw[i+1]>>3)<<5)|((raw[i+2]>>3)<<10)) for i in range(0,len(raw),3))

def embedded_cover(src,ffmpeg):
    """The MP3's own embedded picture, when it has one; otherwise None."""
    try: return cover(src,ffmpeg)
    except (subprocess.CalledProcessError,ValueError): return None

def container_head(rate,channels,total,firsts,meta,art=None):
    """Everything of a GAC v4 file before its blocks: 64-byte header, GACMETA3
    metadata, optional 64x64 art (32x32 accepted), seek table (u32 first frame
    of each block), zero padding to a 512-byte boundary. firsts: the first
    frame of each block."""
    if rate not in RATES or not 0<total<=0x7fffffff: raise ValueError('rate must be 32768 or 44151')
    if channels not in (1,2): raise ValueError('channels must be 1 or 2')
    if meta is None or len(meta)!=512: raise ValueError('invalid metadata length')
    if art is not None and len(art) not in ART_BYTES: raise ValueError('invalid cover')
    count=len(firsts)
    if not count: raise ValueError('invalid blocks')
    if count>max_blocks(len(art) if art is not None else 0): raise ValueError('cache exceeds the player limit (about 1 GiB of audio)')
    seek_at=576+len(art) if art is not None else 576
    table=array('I',firsts)
    if table.itemsize!=4: table=array('L',firsts)
    if sys.byteorder!='little': table.byteswap()
    table=table.tobytes()
    audio=(seek_at+len(table)+511)//512*512
    if audio+count*BLOCK>0x7fffffff: raise ValueError('cache exceeds 2 GiB')
    head=HEADER.pack(b'GBCACHE\0',4,64,3,rate,channels,BLOCK,count,total,seek_at,audio,512,
                     len(art) if art is not None else 0,checksum(meta),checksum(art) if art is not None else 0,checksum(table))
    body=head+meta+(art or b'')+table
    return body+bytes(audio-len(body))

def container(rate,channels,total,firsts,blocks,meta,art=None):
    """Complete GAC v4 file: container_head(), then the 1024-byte blocks."""
    if len(firsts)!=len(blocks) or any(len(b)!=BLOCK for b in blocks): raise ValueError('invalid blocks')
    return container_head(rate,channels,total,firsts,meta,art)+b''.join(blocks)

def build(pcm,rate,meta,art=None,channels=2):
    """Complete .gac bytes from (n, 2) signed16 stereo PCM."""
    if rate not in RATES: raise ValueError('rate must be 32768 or 44151')
    blocks,firsts,total,_=gac4.encode(pcm,channels,diagnostics=False)
    return container(rate,channels,total,firsts,blocks,meta,art)

def read_tags(src,ffmpeg):
    result=run([ffmpeg,'-nostdin','-v','error','-i',str(src),'-map_metadata','0','-f','ffmetadata','pipe:1'])
    if result.returncode: raise ValueError('ffmpeg failed: '+result.stderr[:4096].decode(errors='replace'))
    tags={};line='';escaped=False
    for c in result.stdout.decode('utf-8',errors='replace'):
        if escaped: line+=c if c!='\n' else ' ';escaped=False
        elif c=='\\': escaped=True
        elif c=='\n':
            if '=' in line and not line.startswith((';','#','[')):
                k,v=line.split('=',1);tags.setdefault(k.lower(),v)
            line=''
        else: line+=c
    tags.setdefault('title',src.stem)
    return tags

class Skipped(ValueError):
    """The .gac already exists and overwrite is off: nothing was done."""

def find_ffmpeg(value='ffmpeg'):
    """Resolve the ffmpeg executable: an existing path, a command on PATH, or
    ffmpeg(.exe) next to this program (the Windows package bundles one)."""
    value=(value or '').strip().strip('"')
    if value:
        p=pathlib.Path(value).expanduser()
        if p.is_file(): return str(p)
        if p.is_dir():
            for c in (p/'ffmpeg.exe',p/'ffmpeg',p/'bin'/'ffmpeg.exe',p/'bin'/'ffmpeg'):
                if c.is_file(): return str(c)
        found=shutil.which(value)
        if found: return found
        if value.lower() not in ('ffmpeg','ffmpeg.exe'): return None   # an explicit path that does not exist
    here=[pathlib.Path(sys.executable).parent,pathlib.Path(__file__).resolve().parent]
    if getattr(sys,'frozen',False): here.insert(0,pathlib.Path(getattr(sys,'_MEIPASS',here[0])))
    for d in here:
        for c in (d/'ffmpeg.exe',d/'ffmpeg'):
            if c.is_file(): return str(c)
    return None

def check_gac(path):
    """Strict current-format read-back verification, without reading all audio into RAM."""
    with pathlib.Path(path).open('rb') as f:
        return _read_gac(f)['size']

def _rename_noreplace(src, dst):
    """Atomic no-clobber commit, including filesystems without hard links.

    Never fall back to POSIX rename's overwrite behavior. If a filesystem lacks
    both primitives, fail safely and leave the source untouched.
    """
    import ctypes
    import errno
    if os.name == 'nt':
        os.rename(src,dst)  # Windows rename fails if dst exists.
        return
    try:
        os.link(src,dst)
        src.unlink()
        return
    except FileExistsError:
        raise
    except OSError as e:
        if e.errno not in (errno.EPERM,errno.EOPNOTSUPP,errno.ENOSYS,errno.EXDEV,errno.EACCES):
            raise
    libc = ctypes.CDLL(None,use_errno=True)
    if sys.platform.startswith('linux') and hasattr(libc,'renameat2'):
        fn = libc.renameat2
        fn.argtypes = [ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
        fn.restype = ctypes.c_int
        result = fn(-100,os.fsencode(src),-100,os.fsencode(dst),1)  # RENAME_NOREPLACE
    elif sys.platform == 'darwin' and hasattr(libc,'renamex_np'):
        fn = libc.renamex_np
        fn.argtypes = [ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint]
        fn.restype = ctypes.c_int
        result = fn(os.fsencode(src),os.fsencode(dst),4)  # RENAME_EXCL
    else:
        raise OSError(errno.ENOTSUP,'filesystem lacks safe no-clobber publication')
    if result:
        error = ctypes.get_errno()
        raise OSError(error,os.strerror(error),str(dst))


def _destination_check(dst, overwrite):
    if dst.is_symlink():
        raise ValueError(f'output is a symbolic link: {dst}')
    if dst.exists():
        if not dst.is_file(): raise ValueError(f'output is not a regular file: {dst}')
        if not overwrite: raise Skipped(f'{dst.name} already exists (skipped)')
    if dst.parent.is_dir():
        for other in dst.parent.iterdir():
            if other.name != dst.name and other.name.casefold() == dst.name.casefold():
                raise Skipped(f'{other.name} already exists (case collision, skipped)')


def publish(dst,data,overwrite=False,validator=None):
    """Unique same-directory staging; optional validation BEFORE atomic commit.

    data is bytes, a path (copied in chunks) or a function that writes the
    content to the open binary file it is given. Neither existing .part files nor
    another process's destination are overwritten without explicit overwrite.
    """
    dst = pathlib.Path(dst)
    _destination_check(dst,overwrite)
    fd,name = tempfile.mkstemp(prefix='.gbacache-',suffix='.part',dir=dst.parent)
    tmp = pathlib.Path(name)
    try:
        with os.fdopen(fd,'wb') as f:
            if isinstance(data,pathlib.Path):
                with data.open('rb') as source: shutil.copyfileobj(source,f,1024*1024)
            elif callable(data): data(f)
            else: f.write(data)
            f.flush();os.fsync(f.fileno())
        if validator is not None: validator(tmp)
        _destination_check(dst,overwrite)
        if overwrite: os.replace(tmp,dst)
        else:
            try: _rename_noreplace(tmp,dst)
            except FileExistsError: raise Skipped(f'{dst.name} already exists (skipped)') from None
    finally:
        try: tmp.unlink()
        except FileNotFoundError: pass


def _signature(path):
    st = pathlib.Path(path).stat()
    return st.st_dev,st.st_ino,st.st_size,st.st_mtime_ns,st.st_ctime_ns


def _unchanged(path, signature):
    if pathlib.Path(path).is_symlink() or _signature(path) != signature:
        raise ValueError(f'file changed during conversion; original kept: {path}')


def _prepare(src,dst,overwrite,replace):
    src,dst = pathlib.Path(src).absolute(),pathlib.Path(dst).absolute()
    if replace and src.is_symlink():
        raise ValueError('replacement of a symbolic-link input is unsafe')
    src = src.resolve()
    if not src.is_file(): raise ValueError(f'input not found: {src}')
    validate_pairs([(src,dst)])
    _destination_check(dst,overwrite)
    return src,dst,_signature(src)


def _spawn(cmd,**kwargs):
    """Start ffmpeg; on Windows without flashing a console window."""
    return subprocess.Popen(cmd,stdin=kwargs.pop('stdin',subprocess.DEVNULL),creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),**kwargs)

def _drain(stream,sink,limit=65536):
    """Thread body: read a pipe to its end, keeping its first `limit` bytes."""
    try:
        for chunk in iter(lambda: stream.read(65536),b''):
            if len(sink)<limit: sink+=chunk[:limit-len(sink)]
            elif not sink.endswith(b'...'): sink+=b'...'
    except (OSError,ValueError): pass

def _pcm_pieces(cmd,piece=1<<20,ahead=8):
    """ffmpeg's stdout in pieces of up to `piece` bytes, read ahead by a thread
    (at most `ahead` pieces), so decoding runs while the caller encodes. After
    the last piece, a failure exit or any error output raises ValueError.
    Closing the generator early stops ffmpeg."""
    proc=_spawn(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    errors=bytearray()
    pieces=queue.Queue(ahead)
    stop=threading.Event()
    def read():
        try:
            while not stop.is_set():
                data=proc.stdout.read(piece)
                while not stop.is_set():
                    try: pieces.put(data,timeout=.1); break
                    except queue.Full: pass
                if not data: return
        except BaseException as error:
            pieces.put(error)
    threads=[threading.Thread(target=read,daemon=True),threading.Thread(target=_drain,args=(proc.stderr,errors),daemon=True)]
    for t in threads: t.start()
    try:
        while True:
            data=pieces.get()
            if isinstance(data,BaseException): raise data
            if not data: break
            yield data
        proc.wait()
        threads[1].join()
        # Some FFmpeg decoding/probing errors can still accompany exit status zero.
        # With error-only logging, any stderr means we must not publish/delete.
        if proc.returncode or errors: raise ValueError('ffmpeg failed: '+bytes(errors[:4096]).decode(errors='replace'))
    finally:
        stop.set()
        if proc.poll() is None: proc.kill()
        proc.wait()
        for t in threads: t.join(5)
        for stream in (proc.stdout,proc.stderr):
            try: stream.close()
            except OSError: pass

def _probe_frames(src,ffmpeg,rate):
    """Estimated duration of src in frames at `rate` (from ffmpeg's report), or None."""
    result=run([ffmpeg,'-nostdin','-hide_banner','-i',str(src)])
    m=re.search(rb'Duration: (\d+):(\d\d):(\d\d(?:\.\d+)?)',result.stderr or b'')
    if not m: return None
    seconds=int(m[1])*3600+int(m[2])*60+float(m[3])
    return int(seconds*rate) or None

def _workers(expected):
    """Encoder processes for a file of about `expected` frames: one for short
    files (starting workers costs about a second), else up to 4 (GBACACHE_WORKERS)."""
    try: wanted=int(os.environ.get('GBACACHE_WORKERS','0'))
    except ValueError: wanted=0
    if wanted<=0: wanted=min(4,os.cpu_count() or 1)
    if not expected or expected<3*32*gac4.Params.segment*gac4.UNIT: return 1
    return max(1,wanted)

def convert(src,dst,ffmpeg,rate,art_source=None,channels=2,overwrite=False,replace=False,progress=None,cancel=None):
    """Convert one MP3. replace=True deletes the MP3 after the GAC is written
    and verified. Returns a one-line summary. Memory stays bounded for any
    length: ffmpeg's PCM is encoded while it is decoded and the blocks go to
    a temporary file next to the output. cancel: callable; when it returns
    True the conversion stops (Cancelled) and no file is changed. Audio that
    does not fit in one GAC raises TooLong, early when its length shows it."""
    import numpy as np
    if rate not in RATES or channels not in (1,2):
        raise ValueError('invalid output rate or channels')
    report = _Progress(progress)
    report(0,'Reading MP3 metadata')
    src,dst,signature = _prepare(src,dst,overwrite,replace)
    dst.parent.mkdir(parents=True,exist_ok=True)
    meta=metadata(read_tags(src,ffmpeg)); art=cover(art_source,ffmpeg) if art_source else embedded_cover(src,ffmpeg)
    if cancel is not None and cancel(): raise Cancelled('cancelled')
    expected=_probe_frames(src,ffmpeg,rate)
    if expected and expected>MAX_TOTAL*1.02: raise _too_long(rate,channels,duration=expected)
    report(.05,'Decoding MP3 with FFmpeg')
    cmd=[ffmpeg,'-nostdin','-v','error','-xerror','-err_detect','explode','-i',str(src),'-map','0:a:0','-vn','-ac','2','-ar',str(rate),'-f','s16le','-acodec','pcm_s16le','pipe:1']
    limit=max_blocks(len(art) if art else 0)
    workers=_workers(expected)
    firsts=array('I') if array('I').itemsize==4 else array('L')
    with tempfile.TemporaryFile(prefix='.gbacache-',suffix='.blocks',dir=dst.parent) as spool, \
         gac4.StreamEncoder(channels,diagnostics=False,progress=lambda f,m:report(.05+.85*f,m),cancel=cancel,
                            expected_frames=expected,workers=workers,group=32 if workers>1 else gac4.GROUP_SEGMENTS) as enc:
        def take(blocks):
            for first,data in blocks:
                firsts.append(first)
                spool.write(data.ljust(BLOCK,b'\0'))
            done=enc.done_units*gac4.UNIT
            if len(firsts)>limit: raise _too_long(rate,channels,done/len(firsts),expected)
            # Projected from the first tenth of the file (the block limit is
            # checked exactly as blocks are written; this only saves hours).
            if expected and firsts and done>=expected/10 and len(firsts)*expected/done>1.25*limit:
                raise _too_long(rate,channels,done/len(firsts),expected)
        rest=b''
        pieces=_pcm_pieces(cmd)
        try:
            for data in pieces:
                if rest: data,rest=rest+data,b''
                cut=len(data)//4*4
                if cut<len(data): data,rest=data[:cut],data[cut:]
                if enc.frames+cut//4>MAX_TOTAL: raise _too_long(rate,channels,duration=max(expected or 0,enc.frames+cut//4))
                take(enc.push(np.frombuffer(data,'<i2').reshape(-1,2)))
        finally:
            pieces.close()
        if rest: raise ValueError('ffmpeg returned incomplete PCM frames')
        if not enc.frames: raise ValueError('no audio decoded')
        take(enc.finish())
        total=enc.frames
        report(.9,'Writing GAC')
        head=container_head(rate,channels,total,firsts,meta,art)
        def write(f):
            f.write(head)
            spool.seek(0)
            shutil.copyfileobj(spool,f,1<<20)
        _unchanged(src,signature)
        if cancel is not None and cancel(): raise Cancelled('cancelled')
        report(.92,'Verifying GAC')
        publish(dst,write,overwrite,check_gac)
    output_signature = _signature(dst)
    size=check_gac(dst)
    layout='stereo' if channels==2 else 'mono'
    note=f'{src.name} -> {dst} ({total} frames, {rate} Hz {layout}, {size} bytes)'
    if replace:
        _unchanged(src,signature)
        _unchanged(dst,output_signature)
        src.unlink(); note+='; MP3 deleted'
    report(1,'Complete')
    return note

def _png(rgb,side):
    """Minimal 8-bit RGB PNG (no filter) of side x side pixels."""
    raw=b''.join(b'\0'+rgb[y*side*3:(y+1)*side*3] for y in range(side))
    chunk=lambda kind,data: struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',side,side,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b'')

def _metadata_tags(meta):
    if len(meta) != 512 or meta[:8] != b'GACMETA3' or any(meta[496:]):
        raise ValueError('invalid GAC metadata')
    tags = {}
    for key, at, size in (('title',16,128),('artist',144,96),('album',240,96),('album_artist',336,96),('genre',432,64)):
        field = meta[at:at+size]
        raw, sep, pad = field.partition(b'\0')
        if not sep or any(pad):
            raise ValueError('invalid GAC metadata padding')
        text = raw.decode('utf-8')
        if any(ord(c)<32 or 127<=ord(c)<160 for c in text):
            raise ValueError('invalid GAC metadata controls')
        tags[key] = text
    track, tracks, disc, discs = struct.unpack_from('<4H', meta, 8)
    if track: tags['track'] = str(track) + (f'/{tracks}' if tracks else '')
    if disc: tags['disc'] = str(disc) + (f'/{discs}' if discs else '')
    return tags


def _read_gac(f):
    """Validate the complete current v4 container, with bounded block reads.

    The format has no audio checksum: arbitrary payload bit patterns are legal.
    We validate its framing, not an integrity guarantee the format cannot supply.
    """
    size = os.fstat(f.fileno()).st_size
    head = f.read(64)
    if len(head) != 64:
        raise ValueError('GAC header is truncated')
    magic,version,hsize,codec,rate,channels,block,count,total,seek,audio,msize,asize,mcrc,acrc,scrc = HEADER.unpack(head)
    if magic == b'GBCACHE\0' and version in (1,2,3):
        raise ValueError('old GAC version: reconvert the original MP3')
    if (magic != b'GBCACHE\0' or version != 4 or hsize != 64 or codec != 3 or rate not in RATES
            or channels not in (1,2) or block != BLOCK or msize != 512 or asize not in (0,)+ART_BYTES
            or not 0 < total <= 0x7fffffff or not 0 < count <= min(total,max_blocks(asize))
            or seek != 576+asize or audio != (seek+count*4+511)//512*512
            or size > 0x7fffffff or audio+count*BLOCK != size):
        raise ValueError('invalid GAC header or file length')
    meta = f.read(512)
    if checksum(meta) != mcrc:
        raise ValueError('invalid GAC metadata checksum')
    tags = _metadata_tags(meta)
    art = f.read(asize)
    if (checksum(art) if art else 0) != acrc:
        raise ValueError('invalid GAC cover checksum')
    table = f.read(4*count)
    if checksum(table) != scrc:
        raise ValueError('invalid GAC seek checksum')
    import numpy as np
    starts = np.frombuffer(table,'<u4').astype(np.int64)
    sizes = np.diff(np.append(starts,total))
    if starts[0] != 0 or np.any(sizes <= 0) or np.any(sizes > gac4.MAX_FRAMES):
        raise ValueError('invalid GAC seek bounds')
    firsts = array('I') if array('I').itemsize == 4 else array('L')
    firsts.frombytes(table)
    if sys.byteorder != 'little': firsts.byteswap()
    # Each block's frame count, read 1024 blocks (1 MiB) at a time.
    for a in range(0,count,1024):
        f.seek(audio+a*BLOCK)
        chunk = f.read(min(1024,count-a)*BLOCK)
        n = np.frombuffer(chunk,'<u2')[::BLOCK//2]
        if len(n) != min(1024,count-a) or np.any(n != sizes[a:a+len(n)]):
            raise ValueError('GAC block frame count disagrees with seek table')
    return dict(size=size,rate=rate,channels=channels,total=total,audio=audio,firsts=firsts,tags=tags,art=art)


class _Progress:
    """At most 101 notifications per file; observational callbacks cannot abort I/O."""
    def __init__(self, callback):
        self.callback, self.last = callback, -1
    def __call__(self, fraction, message):
        percent = min(100,max(self.last,int(fraction*100)))
        if self.callback is not None and percent > self.last:
            self.last = percent
            try: self.callback(percent/100, message)
            except Exception: pass  # A closing GUI must not turn success into failure.


def _check_mp3(path, ffmpeg, expected_frames=None, channels=None, cancel=None):
    """Full decode of the MP3 (ffmpeg framehash, read as it comes: bounded
    memory for any length); checks duration, rate and layout."""
    proc = _spawn([ffmpeg,'-nostdin','-v','error','-xerror','-err_detect','explode','-f','mp3','-i',str(path),
                   '-map','0:a:0','-f','framehash','pipe:1'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    errors = bytearray()
    drain = threading.Thread(target=_drain,args=(proc.stderr,errors),daemon=True)
    drain.start()
    header, frames, actual = [], 0, 0
    try:
        for line in proc.stdout:
            line = line.rstrip(b'\r\n')
            if not line: continue
            if line.startswith(b'#'):
                if len(header) < 64: header.append(line)
                continue
            frames += 1
            actual += int(line.split(b',')[3])
            if cancel is not None and not frames % 4096 and cancel(): raise Cancelled('cancelled')
        proc.wait()
    finally:
        if proc.poll() is None: proc.kill()
        proc.wait()
        drain.join(5)
        proc.stdout.close(); proc.stderr.close()
    if proc.returncode or not frames:
        raise ValueError('MP3 output failed full decode verification: '+bytes(errors[:4096]).decode(errors='replace'))
    if expected_frames is not None:
        # FFmpeg/LAME can expose a minimum 47-sample first decoded frame for
        # sub-millisecond inputs despite gapless padding. Bound that exception;
        # ordinary tracks must still match the resampled duration to one sample.
        if not expected_frames-1 <= actual <= max(47,expected_frames+1):
            raise ValueError(f'MP3 output duration mismatch: {actual} != {expected_frames} samples')
        if b'#sample_rate 0: 44100' not in header:
            raise ValueError('MP3 output sample rate mismatch')
    if channels is not None:
        layout = b'mono' if channels == 1 else b'stereo'
        if b'#channel_layout_name 0: '+layout not in header:
            raise ValueError('MP3 output channel layout mismatch')
    return pathlib.Path(path).stat().st_size


def reverse_convert(src,dst,ffmpeg,quality=0,channels=None,overwrite=False,replace=False,progress=None,cancel=None):
    """Decode current GAC v4 to LAME VBR MP3 at 44100 Hz (V0 by default).

    GAC is lossy: this cannot recover the original MP3 or discarded detail.
    Signed32 mid/side sums are saturated to signed16, not quantized to GBA 8-bit.
    The decoded PCM goes to ffmpeg through a pipe, block by block (bounded
    memory, LAME encodes while the GAC is decoded). cancel: as convert().
    """
    import numpy as np
    if not isinstance(quality,int) or not 0 <= quality <= 9:
        raise ValueError('MP3 VBR quality must be 0..9 (V0 highest)')
    if channels not in (None,1,2):
        raise ValueError('channels must be 1, 2 or None')
    src,dst,signature = _prepare(src,dst,overwrite,replace)
    report = _Progress(progress)
    report(0,'Validating GAC')
    with src.open('rb') as f:
        info = _read_gac(f)
        # LAME/MP3 gapless delay leaves at least 47 decoded samples in the first
        # MPEG-1 audio frame (1152 - 576 encoder delay - 529 decoder delay).
        # Sub-millisecond caches are padded to this minimum, never silently empty.
        expected_frames = max(47,(info['total']*44100+info['rate']-1)//info['rate'])
        def verify(path):
            return _check_mp3(path,ffmpeg,expected_frames,channels or info['channels'],cancel=cancel)
        dst.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.gac-reverse-',dir=dst.parent) as temp:
            temp = pathlib.Path(temp)
            target = temp/'output.mp3'
            cmd = [ffmpeg,'-nostdin','-v','error','-y','-f','s16le','-ar',str(info['rate']),'-ac',str(info['channels']),'-i','pipe:0']
            if info['art']:
                pixels = np.frombuffer(info['art'],dtype='<u2').astype(np.uint32)
                rgb = np.stack([(pixels>>shift)&31 for shift in (0,5,10)],1)
                image = temp/'cover.png'   # PNG: the bundled FFmpeg has no PPM decoder
                side = 64 if len(info['art']) == 8192 else 32
                image.write_bytes(_png(((rgb*255+15)//31).astype(np.uint8).tobytes(),side))
                cmd += ['-i',str(image)]
            cmd += ['-map','0:a:0','-c:a','libmp3lame','-q:a',str(quality),'-ar','44100','-ac',str(channels or info['channels'])]
            if info['total'] < 64:
                # swresample can otherwise emit no PCM for sub-filter-length input.
                # Supply its tail explicitly, then trim to the resampled duration.
                cmd += ['-af',f'apad=whole_len=64,aresample=44100,atrim=end_sample={expected_frames}']
            if info['art']:
                cmd += ['-map','1:v:0','-c:v','png','-disposition:v:0','attached_pic']
            for key,value in info['tags'].items():
                if value: cmd += ['-metadata',f'{key}={value}']
            cmd += ['-id3v2_version','3','-f','mp3',str(target)]
            proc = _spawn(cmd,stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            errors = bytearray()
            drain = threading.Thread(target=_drain,args=(proc.stderr,errors),daemon=True)
            drain.start()
            try:
                firsts,total = info['firsts'],info['total']
                for i in range(len(firsts)):
                    a = firsts[i]; z = firsts[i+1] if i+1 < len(firsts) else total
                    f.seek(info['audio']+i*BLOCK)
                    values = gac4.decode_block_fast(f.read(BLOCK),info['channels'],z-a)
                    try: proc.stdin.write(np.clip(values,-32768,32767).astype('<i2').tobytes())
                    except (BrokenPipeError,OSError): break    # ffmpeg stopped: its error is reported below
                    if not i % 64:
                        if cancel is not None and cancel(): raise Cancelled('cancelled')
                        report(.05+.65*z/total,'Decoding GAC, encoding MP3 (LAME VBR)')
                try: proc.stdin.close()
                except OSError: pass
                report(.72,'Encoding MP3 (LAME VBR)')
                proc.wait()
            finally:
                if proc.poll() is None: proc.kill()
                proc.wait()
                try: proc.stdin.close()
                except OSError: pass
                drain.join(5)
                proc.stderr.close()
            if proc.returncode:
                raise ValueError('ffmpeg failed: '+bytes(errors[:4096]).decode(errors='replace'))
            report(.90,'Verifying MP3')
            _unchanged(src,signature)
            publish(dst,target,overwrite,verify)
            output_signature = _signature(dst)
    size = verify(dst)
    note = f'{src.name} -> {dst} (44100 Hz, V{quality}, {size} bytes)'
    if replace:
        _unchanged(src,signature)
        _unchanged(dst,output_signature)
        src.unlink()
        note += '; GAC deleted'
    report(1,'Complete')
    return note


def _identity(path):
    """Case-sensitive on Linux; also identify hard links to existing files."""
    p = pathlib.Path(path).resolve()
    try:
        st = p.stat()
        return ('inode', st.st_dev, st.st_ino)
    except FileNotFoundError:
        return ('path', os.path.normcase(str(p)))


def validate_pairs(pairs):
    """Reject all destructive batch aliases before the first output is written.

    Output names are case-folded even on Linux, for a future FAT/exFAT SD card.
    Input discovery is deliberately NOT case-folded on case-sensitive hosts.
    """
    pairs = [(pathlib.Path(a), pathlib.Path(b)) for a, b in pairs]
    sources = {_identity(a) for a, _ in pairs}
    source_names = {str(a.resolve()).casefold() for a, _ in pairs}
    outputs, output_ids = set(), set()
    for a, b in pairs:
        key, ident = str(b.resolve()).casefold(), _identity(b)
        if ident in sources or key in source_names:
            raise ValueError(f'output aliases an input: {b}')
        if key in outputs or ident in output_ids:
            raise ValueError(f'duplicate output (including case/aliases): {b}')
        outputs.add(key)
        output_ids.add(ident)


def collect(inputs,recursive=False,output_dir=None,direction='mp3_to_gac'):
    """(mp3, gac) pairs. Folders give their *.mp3 (all subfolders when
    recursive); with output_dir, folder structure below the chosen folder is
    kept, otherwise each .gac goes next to its MP3."""
    if direction not in ('mp3_to_gac', 'gac_to_mp3'):
        raise ValueError('unknown conversion direction')
    source_ext, target_ext = ('.mp3', '.gac') if direction == 'mp3_to_gac' else ('.gac', '.mp3')
    pairs=[];seen=set()
    for path in map(pathlib.Path,inputs):
        if path.is_dir():
            found=sorted((p for p in (path.rglob('*') if recursive else path.iterdir()) if p.is_file() and p.suffix.lower()==source_ext),key=lambda p:str(p).lower())
            for p in found:
                rel=p.relative_to(path).with_suffix(target_ext)
                pairs.append((p,(pathlib.Path(output_dir)/path.name/rel) if output_dir else p.with_suffix(target_ext)))
        elif path.suffix.lower() == source_ext:
            pairs.append((path,(pathlib.Path(output_dir)/(path.stem+target_ext)) if output_dir else path.with_suffix(target_ext)))
    out=[]
    for a,b in pairs:
        k=_identity(a)
        if k not in seen: seen.add(k);out.append((a,b))
    return out

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('inputs',type=pathlib.Path,nargs='+',help='MP3 file(s) or folders')
    ap.add_argument('-r','--recursive',action='store_true',help='include MP3s in all subfolders of the given folders')
    ap.add_argument('-o','--output-dir',type=pathlib.Path,help='default: next to each MP3 (folder structure is kept below given folders)')
    ap.add_argument('--replace',action='store_true',help='delete each MP3 after its GAC has been written and verified')
    ap.add_argument('--overwrite',action='store_true',help='replace existing .gac files (default: skip them)')
    ap.add_argument('--rate',type=int,default=32768,choices=RATES,help=(
        'samples/s/channel: 32768 (default) or 44151. 44151 is nominal: GBA timer '
        '380 plays 44150.568421 Hz (9.775 ppm slower).'))
    ap.add_argument('--mono',action='store_true',help='store one mid channel: about 30%% smaller than stereo GAC; plays mono')
    ap.add_argument('--cover',type=pathlib.Path,help='album image (or MP3 with embedded cover), preconverted to 64x64; default: the input MP3\'s own embedded picture, if any')
    ap.add_argument('--ffmpeg',default=os.environ.get('FFMPEG','ffmpeg'))
    args=ap.parse_args(); ffmpeg=find_ffmpeg(args.ffmpeg)
    if not ffmpeg: ap.error('ffmpeg not found: install ffmpeg or supply --ffmpeg /path/to/ffmpeg')
    pairs=collect(args.inputs,args.recursive,args.output_dir)
    if not pairs: ap.error('no input MP3 files')
    try: validate_pairs(pairs)
    except (OSError,ValueError) as e: ap.error(str(e))
    failed=False
    # Progress on a terminal (long files take minutes); Ctrl+C cancels safely.
    show=sys.stderr is not None and sys.stderr.isatty()
    for src,dst in pairs:
        def progress(fraction,message,name=src.name):
            sys.stderr.write(f'\r{name}: {int(fraction*100):3d}% {message}'[:79].ljust(79)); sys.stderr.flush()
        try: note=convert(src,dst,ffmpeg,args.rate,args.cover,1 if args.mono else 2,args.overwrite,args.replace,progress if show else None)
        except Skipped as e: note=f'SKIPPED: {e}'
        except (OSError,ValueError,subprocess.SubprocessError) as e:
            if show: sys.stderr.write('\r'+' '*79+'\r')
            print(f'ERROR: {src}: {e}',file=sys.stderr); failed=True; continue
        except KeyboardInterrupt:
            if show: sys.stderr.write('\r'+' '*79+'\r')
            print(f'CANCELLED: {src}: no files changed',file=sys.stderr); return 130
        if show: sys.stderr.write('\r'+' '*79+'\r')
        print(note)
    return int(failed)
if __name__=='__main__':
    import multiprocessing
    multiprocessing.freeze_support()
    sys.exit(main())
