"""Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Reference for player/src/jpeg_dc.c: baseline
JPEG -> 64x64 RGB555 cover (v1.8; 32x32 from the DC coefficients before).
Each 8x8 block gives the means of its four 4x4 quarters, from the DC
coefficient and the AC coefficients of horizontal and vertical frequencies
1, 3, 5, 7 (the others average to zero over a half block); the quarter
means are box-filtered by area into a grid that fits the picture into 64x64
with the aspect ratio kept, centred on black. Pictures whose grid cells span
16 pixels or more (1024+ pixels) use the block means (DC) as units instead.

The integer arithmetic (prediction clamp, coefficient clamp, quarter means,
colour conversion, rounding, area weights) is identical to the C decoder;
tests/test_jpeg_dc.py requires both to give the same 8192 bytes. cover(data) raises JpegError for anything the
GBA skips (progressive, arithmetic, 12-bit, CMYK, > 1600 px, > 256 KB ...).
"""
import struct

MAX_BYTES = 256 * 1024
MAX_SIDE = 1600
SIDE = 64
# Mean of the DCT basis over a half block, x 1024 (frequency 0, 1, 3, 5, 7).
MEAN_W = (1024, 928, -326, 218, -185)
NATURAL = (0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
           35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63)
_SLOT8 = (0, 1, None, 2, None, 3, None, 4)
ZSLOT = tuple(None if _SLOT8[n & 7] is None or _SLOT8[n >> 3] is None else _SLOT8[n & 7] * 5 + _SLOT8[n >> 3] for n in NATURAL)


class JpegError(Exception):
    pass


def _clamp8(v):
    return 0 if v < 0 else 2040 if v > 2040 else v


class _Huff:
    def __init__(self, counts, vals):
        self.codes = {}
        code = 0
        k = 0
        for length in range(1, 17):
            for _ in range(counts[length - 1]):
                if code >= 1 << length:
                    raise JpegError('bad Huffman table')
                self.codes[(length, code)] = vals[k]
                code += 1
                k += 1
            code <<= 1


class _Bits:
    def __init__(self, data, pos):
        self.d = data
        self.p = pos
        self.acc = 0
        self.n = 0
        self.marker = None
        self.eof = False

    def _byte(self):
        if self.marker is not None:
            return 0
        if self.p >= len(self.d):
            self.eof = True
            return 0
        b = self.d[self.p]
        self.p += 1
        if b == 0xFF:
            while True:
                if self.p >= len(self.d):
                    self.eof = True
                    self.marker = 0x100
                    return 0
                c = self.d[self.p]
                self.p += 1
                if c != 0xFF:
                    break
            if c == 0:
                return 0xFF
            self.marker = c
            return 0
        return b

    def bit(self):
        if not self.n:
            self.acc = self._byte()
            self.n = 8
        self.n -= 1
        return (self.acc >> self.n) & 1

    def bits(self, n):
        v = 0
        for _ in range(n):
            v = v << 1 | self.bit()
        return v

    def decode(self, h):
        code = 0
        for length in range(1, 17):
            code = code << 1 | self.bit()
            if (length, code) in h.codes:
                return h.codes[(length, code)]
        raise JpegError('bad Huffman code')

    def restart(self):
        self.n = 0
        if self.marker is None:
            for _ in range(16):
                if self.p >= len(self.d):
                    raise JpegError('no restart marker')
                b = self.d[self.p]
                self.p += 1
                if b == 0xFF:
                    while self.p < len(self.d) and self.d[self.p] == 0xFF:
                        self.p += 1
                    if self.p >= len(self.d):
                        raise JpegError('no restart marker')
                    c = self.d[self.p]
                    self.p += 1
                    if c:
                        self.marker = c
                        break
        if self.marker is None or not 0xD0 <= self.marker <= 0xD7:
            raise JpegError('no restart marker')
        self.marker = None


def cover(data):
    """JPEG bytes -> 8192 bytes (64x64 little-endian RGB555 values)."""
    data = bytes(data)
    if len(data) > MAX_BYTES:
        raise JpegError('picture too large')
    if data[:2] != b'\xff\xd8':
        raise JpegError('not a JPEG')
    p = 2
    q = {}
    huff = {}
    frame = None
    restart = 0
    rgb = False
    while True:
        if p >= len(data) or data[p] != 0xFF:
            raise JpegError('marker expected')
        while p < len(data) and data[p] == 0xFF:
            p += 1
        if p >= len(data):
            raise JpegError('truncated')
        m = data[p]
        p += 1
        if m in (0xD8, 0xD9):
            raise JpegError('no scan')
        if m == 0x01 or 0xD0 <= m <= 0xD7:
            continue
        if p + 2 > len(data):
            raise JpegError('truncated')
        length = struct.unpack_from('>H', data, p)[0]
        if length < 2 or p + length > len(data):
            raise JpegError('truncated segment')
        seg = data[p + 2:p + length]
        p += length
        if m == 0xDB:
            i = 0
            while i < len(seg):
                pq, tq = seg[i] >> 4, seg[i] & 15
                n = 129 if pq else 65
                if pq > 1 or tq > 3 or i + n > len(seg):
                    raise JpegError('bad DQT')
                t = [struct.unpack_from('>H', seg, i + 1 + 2 * k)[0] if pq else seg[i + 1 + k] for k in range(64)]
                if min(t) <= 0:
                    raise JpegError('bad DQT')
                q[tq] = t
                i += n
        elif m == 0xC4:
            i = 0
            while i < len(seg):
                if i + 17 > len(seg):
                    raise JpegError('bad DHT')
                tc, th = seg[i] >> 4, seg[i] & 15
                counts = seg[i + 1:i + 17]
                total = sum(counts)
                if tc > 1 or th > 3 or total > 256 or i + 17 + total > len(seg):
                    raise JpegError('bad DHT')
                huff[(tc, th)] = _Huff(counts, seg[i + 17:i + 17 + total])
                i += 17 + total
        elif m in (0xC0, 0xC1):
            if frame is not None or len(seg) < 6:
                raise JpegError('bad SOF')
            prec, h, w, nc = seg[0], *struct.unpack_from('>HH', seg, 1), seg[5]
            if prec != 8 or not h or not w:
                raise JpegError('unsupported precision')
            if w > MAX_SIDE or h > MAX_SIDE:
                raise JpegError('picture too wide or tall')
            if nc not in (1, 3):
                raise JpegError('unsupported components')
            if len(seg) != 6 + 3 * nc:
                raise JpegError('bad SOF')
            comps = []
            for i in range(nc):
                cid, hv, tq = seg[6 + 3 * i:9 + 3 * i]
                hs, vs = hv >> 4, hv & 15
                if not (1 <= hs <= 2 and 1 <= vs <= 2):
                    raise JpegError('unsupported sampling')
                if tq > 3:
                    raise JpegError('bad SOF')
                comps.append({'id': cid, 'h': hs, 'v': vs, 'tq': tq})
            if nc == 1:
                comps[0]['h'] = comps[0]['v'] = 1
            if nc == 3 and [c['id'] for c in comps] == [ord('R'), ord('G'), ord('B')]:
                rgb = True
            frame = (w, h, comps)
        elif m == 0xDD:
            if len(seg) != 2:
                raise JpegError('bad DRI')
            restart = struct.unpack('>H', seg)[0]
        elif m == 0xEE and len(seg) >= 12:
            if seg[:5] == b'Adobe' and seg[11] == 0:
                rgb = True
        elif 0xC2 <= m <= 0xCF and m not in (0xC4, 0xC8):
            raise JpegError('unsupported JPEG process')
        elif m == 0xDA:
            if frame is None:
                raise JpegError('scan before frame')
            w, h, comps = frame
            if not seg or seg[0] != len(comps):
                raise JpegError('unsupported scan')
            if len(seg) != 4 + 2 * len(comps):
                raise JpegError('bad SOS')
            used = set()
            for i in range(len(comps)):
                cid, t = seg[1 + 2 * i], seg[2 + 2 * i]
                k = next((k for k, c in enumerate(comps) if c['id'] == cid), None)
                if k is None or k in used:
                    raise JpegError('bad SOS')
                used.add(k)
                c = comps[k]
                c['td'], c['ta'] = t >> 4, t & 15
                if (0, c['td']) not in huff or (1, c['ta']) not in huff or c['tq'] not in q:
                    raise JpegError('missing table')
            ss, se, a = seg[-3:]
            if ss != 0 or se != 63 or a != 0:
                raise JpegError('unsupported scan')
            return _scan(data, p, w, h, comps, q, huff, restart, rgb)


def _quads(dc, co):
    """The four 4x4 means (v8 scale) of a block: top-left, top-right,
    bottom-left, bottom-right."""
    if not co:
        return [dc + 1024] * 4
    dc = max(-4095, min(4095, dc))
    F = [[0] * 5 for _ in range(5)]
    for slot, v in co.items():
        F[slot // 5][slot % 5] = v
    F[0][0] = dc
    L, R = [], []
    for v in range(5):
        e = F[0][v] * 1024
        o = sum(F[u][v] * MEAN_W[u] for u in range(1, 5))
        L.append((e + o + 512) >> 10)
        R.append((e - o + 512) >> 10)
    out = [0] * 4
    for h, X in enumerate((L, R)):
        e = X[0] * 1024
        o = sum(X[v] * MEAN_W[v] for v in range(1, 5))
        out[h] = ((e + o + 512) >> 10) + 1024
        out[2 + h] = ((e - o + 512) >> 10) + 1024
    return out


def _scan(data, p, w, h, comps, q, huff, restart, rgb):
    hmax = max(c['h'] for c in comps)
    vmax = max(c['v'] for c in comps)
    if w >= h:
        gw, gh = SIDE, max(1, (SIDE * h + w // 2) // w)
    else:
        gh, gw = SIDE, max(1, (SIDE * w + h // 2) // h)
    ox, oy = (SIDE - gw) // 2, (SIDE - gh) // 2
    us = 3 if w >= 16 * gw and h >= 16 * gh else 2
    acc = [[0, 0, 0] for _ in range(SIDE * SIDE)]
    b = _Bits(data, p)
    mx, my = -(-w // (8 * hmax)), -(-h // (8 * vmax))
    pred = [0] * len(comps)
    count = 0
    for y in range(my):
        for x in range(mx):
            if restart and count and count % restart == 0:
                b.restart()
                pred = [0] * len(comps)
            count += 1
            quads = []
            for i, c in enumerate(comps):
                vals = {}
                qt = q[c['tq']]
                for v in range(c['v']):
                    for hh in range(c['h']):
                        t = b.decode(huff[(0, c['td'])])
                        if t > 11:
                            raise JpegError('bad DC')
                        diff = b.bits(t) if t else 0
                        if t and diff < 1 << (t - 1):
                            diff += 1 - (1 << t)
                        pred[i] = max(-2048, min(2048, pred[i] + diff))
                        co = {}
                        k = 1
                        while k < 64:
                            rs = b.decode(huff[(1, c['ta'])])
                            r, sz = rs >> 4, rs & 15
                            if not sz:
                                if r != 15:
                                    break
                                k += 16
                                continue
                            if sz > 10:
                                raise JpegError('bad AC')
                            val = b.bits(sz)
                            k += r
                            if k < 64 and us == 2 and ZSLOT[k] is not None:
                                if val < 1 << (sz - 1):
                                    val += 1 - (1 << sz)
                                co[ZSLOT[k]] = max(-4095, min(4095, val * qt[k]))
                            k += 1
                        vals[(v, hh)] = _quads(pred[i] * qt[0], co)
                quads.append(vals)
            if b.eof:
                raise JpegError('truncated data')
            for uy in range((8 * vmax) >> us):
                for ux in range((8 * hmax) >> us):
                    px, py = x * hmax * 8 + (ux << us), y * vmax * 8 + (uy << us)
                    if px >= w or py >= h:
                        continue
                    v8 = []
                    qx, qy = (ux << us) >> 2, (uy << us) >> 2
                    for i, c in enumerate(comps):
                        cu, cv = qx * c['h'] // hmax, qy * c['v'] // vmax
                        v8.append(quads[i][(cv >> 1, cu >> 1)][(cv & 1) * 2 + (cu & 1)])
                    _put(acc, px, py, 1 << us, v8, w, h, gw, gh, ox, oy, len(comps), rgb)
    area = w * h
    out = bytearray(SIDE * SIDE * 2)
    for yy in range(gh):
        for xx in range(gw):
            i = (oy + yy) * SIDE + ox + xx
            r, g, bl = ((acc[i][k] + area // 2) // area for k in range(3))
            struct.pack_into('<H', out, 2 * i, (r >> 3) | (g >> 3) << 5 | (bl >> 3) << 10)
    return bytes(out)


def _put(acc, px, py, u, v8, w, h, gw, gh, ox, oy, nc, rgb):
    if nc == 1:
        r = g = b = _clamp8(v8[0])
    elif rgb:
        r, g, b = (_clamp8(v) for v in v8)
    else:
        y, cb, cr = _clamp8(v8[0]), _clamp8(v8[1]) - 1024, _clamp8(v8[2]) - 1024
        r = _clamp8(y + ((91881 * cr + 32768) >> 16))
        g = _clamp8(y + ((-22554 * cb - 46802 * cr + 32768) >> 16))
        b = _clamp8(y + ((116130 * cb + 32768) >> 16))
    rgb8 = ((r + 4) >> 3, (g + 4) >> 3, (b + 4) >> 3)
    x1, y1 = min(px + u, w), min(py + u, h)
    X0, X1, Y0, Y1 = px * gw, x1 * gw, py * gh, y1 * gh
    iy = Y0 // h
    while iy < gh and iy * h < Y1:
        oyy = min((iy + 1) * h, Y1) - max(iy * h, Y0)
        ix = X0 // w
        while ix < gw and ix * w < X1:
            wt = (min((ix + 1) * w, X1) - max(ix * w, X0)) * oyy
            a = acc[(oy + iy) * SIDE + ox + ix]
            for k in range(3):
                a[k] += rgb8[k] * wt
            ix += 1
        iy += 1


if __name__ == '__main__':
    import sys
    sys.stdout.buffer.write(cover(open(sys.argv[1], 'rb').read()))
