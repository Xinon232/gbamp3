"""Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GAC v4 codec: per-block adaptive LPC, Huffman-coded
residuals, a variable number of frames per 1024-byte block. See GAC_FORMAT.md.

A block holds a 6-byte header (frame count, LPC orders, side flag, initial
step index per stream), 8-bit reflection coefficients and 16-bit history
samples per stream, then an LSB-first bitstream of units. A unit is 64 frames: two 32-sample mid
subblocks and, in the stereo layout, one 32-sample half-rate side subblock.
A subblock starts with one header code (step index change and residual table,
an all-zero subblock, or an escape to an absolute step index) and, unless it
is all zero, 32 residual codes q:
    pred = (sum(c[k] * h[k]) + 2048) >> 12     (32-bit sum; h[0] = previous sample)
    x    = clamp16(pred + q * STEP[step])
Every code is at most 10 bits long and every 10-bit pattern is a code, so a
decoder reads each code with one table lookup and any byte pattern decodes.
Reads past the block end return zero bits.

The encoder tries several step sizes per subblock closed-loop and keeps the
one with the lowest squared error + LAMBDA * bits (one fixed slope, so bits go
where they reduce the error most). Block histories are the source signal's own
samples, so blocks encode independently and in parallel.
"""
import numpy as np
import gac_native as _native


def native_available():
    """Whether this process has loaded the optional compatible native helper."""
    return _native.LIB is not None

BLOCK = 1024
UNIT = 64                    # frames per unit
SUB = 32                     # samples per subblock
MAX_ORDER = 8
MAX_FRAMES = 8192            # frames per block (128 units)
HEADER = 6                   # fixed block header bytes
LUT_BITS = 10                # longest code; decode tables have 2^10 entries
ESC_BITS = 12                # residual escape: raw signed 12-bit q follows
SYMBOLS = 24                 # single tables: |q| <= 24 have their own code
PAIR = 7                     # pair tables: two residuals |q| <= 7 per code
TABLES = 4                   # residual tables: 0, 1 pairs; 2, 3 single residuals
PAIR_TABLES = 2
QMAX = (1 << (ESC_BITS - 1)) - 1
DELTA = 6                    # step index changes -6..6 have their own header code
IDX_BITS = 6                 # header escape: raw absolute step index follows

# Step sizes: index 0 is unused (a zero subblock has its own header code);
# 1..63 are 2^(2 + (i-1)/4), about 1.5 dB apart.
STEP = [0] + [int(round(2 ** (2 + (i - 1) / 4))) for i in range(1, 64)]

# Residual tables: p(0) = z, p(+-k) = (1-z)/2 * (1-t) * t^(k-1). Computed from
# these two parameters per table; the values span the residual spreads the
# encoder produces. Tables 0 and 1 code pairs (p = p(a) p(b)): a single code
# cannot be shorter than one bit, and most residuals are small.
RESIDUAL_MODEL = ((0.70, 0.25), (0.50, 0.38), (0.30, 0.55), (0.13, 0.75))

# Subblock header model: step index change d (-DELTA..DELTA), residual table,
# zero subblock, escape. p(d) = 0.38 for d = 0, else 0.105 * 0.6^(|d|-1).
HEADER_TABLE_P = (0.40, 0.36, 0.20, 0.04)
HEADER_ZERO_P = 0.07
HEADER_ESC_P = 0.004
ZERO_SYMBOL = (2 * DELTA + 1) * TABLES
ESC_SYMBOL = ZERO_SYMBOL + 1          # + table


def _limited_lengths(weights, limit):
    """Optimal length-limited prefix code lengths (package-merge). Deterministic."""
    n = len(weights)
    leaves = sorted(((w, (i,)) for i, w in enumerate(weights)), key=lambda x: (x[0], x[1]))
    packages = list(leaves)
    for _ in range(limit - 1):
        merged = [(packages[k][0] + packages[k + 1][0], packages[k][1] + packages[k + 1][1])
                  for k in range(0, len(packages) - 1, 2)]
        packages = sorted(leaves + merged, key=lambda x: (x[0], len(x[1]), x[1]))
    lengths = [0] * n
    for _, syms in packages[:2 * n - 2]:
        for s in syms:
            lengths[s] += 1
    return lengths


def _code(p, value_of):
    """Canonical LSB-first code for probabilities p. Returns (lengths, codes,
    decode table); a table entry is length << 8 | value_of(symbol) & 255."""
    total = sum(p)
    lengths = _limited_lengths([max(x / total, 1e-12) for x in p], LUT_BITS)
    assert sum(2.0 ** -l for l in lengths) == 1.0      # complete: every pattern decodes
    order = sorted(range(len(p)), key=lambda s: (lengths[s], s))
    codes = [0] * len(p)
    code, prev = 0, lengths[order[0]]
    for s in order:
        code <<= lengths[s] - prev
        prev = lengths[s]
        codes[s] = int(format(code, f'0{prev}b')[::-1], 2)
        code += 1
    lut = [0] * (1 << LUT_BITS)
    for s, (l, c) in enumerate(zip(lengths, codes)):
        for hi in range(1 << (LUT_BITS - l)):
            lut[c | (hi << l)] = (l << 8) | (value_of(s) & 255)
    assert all(lut)
    return lengths, codes, lut


def _laplace(z, t, q):
    return z if q == 0 else (1 - z) / 2 * (1 - t) * t ** (abs(q) - 1)


def _residual_tables():
    """Pair tables: symbol (a+PAIR)*(2*PAIR+1) + b+PAIR, then escape; the decode
    table value is (a & 15) << 4 | (b & 15), escape 0x88 (a = b = -8).
    Single tables: symbol q+SYMBOLS, then escape; value q & 255, escape 0x80."""
    out = []
    for i, (z, t) in enumerate(RESIDUAL_MODEL):
        if i < PAIR_TABLES:
            v = range(-PAIR, PAIR + 1)
            p = [_laplace(z, t, a) * _laplace(z, t, b) for a in v for b in v]
            p.append(max(1 - sum(p), 1e-9))
            n = 2 * PAIR + 1
            out.append(_code(p, lambda s: 0x88 if s == n * n else ((s // n - PAIR) & 15) << 4 | ((s % n - PAIR) & 15)))
        else:
            p = [_laplace(z, t, q) for q in range(-SYMBOLS, SYMBOLS + 1)]
            p.append((1 - z) * t ** SYMBOLS)
            out.append(_code(p, lambda s: s - SYMBOLS if s <= 2 * SYMBOLS else -128))
    return out


def _header_table():
    p = []
    for d in range(-DELTA, DELTA + 1):
        pd = 0.38 if d == 0 else 0.105 * 0.6 ** (abs(d) - 1)
        p += [pd * pt for pt in HEADER_TABLE_P]
    p.append(HEADER_ZERO_P)
    p += [HEADER_ESC_P * pt for pt in HEADER_TABLE_P]
    return _code(p, lambda s: s)


TABLE = _residual_tables()
HEADER_CODE = _header_table()
_LEN = [np.array(t[0], np.int64) for t in TABLE]
_CODE = [np.array(t[1], np.int64) for t in TABLE]


def _residual_codes(q, table):
    """Code indices and extra raw bits for residuals q (..., SUB) with one table:
    returns (symbols (..., n), raw values (..., n, 2), raw bit counts (..., n, 2))."""
    if table < PAIR_TABLES:
        a, b = q[..., 0::2], q[..., 1::2]
        n = 2 * PAIR + 1
        esc = (np.abs(a) > PAIR) | (np.abs(b) > PAIR)
        sym = np.where(esc, n * n, (np.clip(a, -PAIR, PAIR) + PAIR) * n + np.clip(b, -PAIR, PAIR) + PAIR)
        raw = np.stack([a, b], -1) & ((1 << ESC_BITS) - 1)
        rawn = np.repeat(np.where(esc, ESC_BITS, 0)[..., None], 2, -1)
    else:
        esc = np.abs(q) > SYMBOLS
        sym = np.where(esc, 2 * SYMBOLS + 1, np.clip(q, -SYMBOLS, SYMBOLS) + SYMBOLS)
        raw = np.stack([q & ((1 << ESC_BITS) - 1), np.zeros_like(q)], -1)
        rawn = np.stack([np.where(esc, ESC_BITS, 0), np.zeros_like(q)], -1)
    return sym, raw, rawn


def _residual_bits(q):
    """Bits of residual blocks q (..., SUB) under every table: (..., TABLES)."""
    out = np.empty(q.shape[:-1] + (TABLES,), np.int64)
    n = 2 * PAIR + 1
    a, b = q[..., 0::2], q[..., 1::2]
    pesc = (np.abs(a) > PAIR) | (np.abs(b) > PAIR)
    psym = np.where(pesc, n * n, (np.minimum(np.maximum(a, -PAIR), PAIR) + PAIR) * n
                    + np.minimum(np.maximum(b, -PAIR), PAIR) + PAIR)
    praw = pesc.sum(-1) * (2 * ESC_BITS)
    sesc = np.abs(q) > SYMBOLS
    ssym = np.where(sesc, 2 * SYMBOLS + 1, np.minimum(np.maximum(q, -SYMBOLS), SYMBOLS) + SYMBOLS)
    sraw = sesc.sum(-1) * ESC_BITS
    for t in range(TABLES):
        out[..., t] = _LEN[t][psym if t < PAIR_TABLES else ssym].sum(-1) + (praw if t < PAIR_TABLES else sraw)
    return out
_HLEN = np.array(HEADER_CODE[0], np.int64)
_HCODE = np.array(HEADER_CODE[1], np.int64)

# Reflection coefficients are stored as bytes j (signed): k = sin(j/127 * pi/2),
# Q15. REFLECT[j & 255] holds k; the step-up below turns k into Q12 predictor
# coefficients with integer arithmetic only (identical on the GBA).
REFLECT = [int(round(32767 * np.sin(((j - 256 if j > 127 else j) / 127) * np.pi / 2))) if j != 128 else -32767
           for j in range(256)]


def _clamp16(v):
    return max(-32768, min(32767, v))


def step_up(codes):
    """Reflection coefficient bytes -> Q12 direct-form coefficients."""
    a = []
    for j in codes:
        k = REFLECT[j & 255]
        a = [_clamp16(a[i] - ((k * a[len(a) - 1 - i] + 16384) >> 15)) for i in range(len(a))] + [(k + 4) >> 3]
    return a


# Side lowpass before 2:1 decimation, as in v3 (31-tap Hamming-windowed sinc).
_n = np.arange(31) - 15
_LP = 0.45 * np.sinc(0.45 * _n) * (0.54 + 0.46 * np.cos(np.pi * _n / 15))
_LP /= _LP.sum()


class Params:
    """Encoder tuning for the stereo layout, fixed by the converter (there is
    no quality setting). Chosen on a 21-item corpus so that SNR, segmental
    SNR and A-weighted SNR after the 8-bit output stage average at least v3's
    (GAC_FORMAT.md, decision record)."""
    lam = 9000.0                    # squared error per bit
    deadzone = 0.2                  # rounding offset toward zero
    offsets = (-6, -4, -2, 0, 2, 4, 6)  # step indices tried around the open-loop estimate
    floor = 14                      # smallest step index (coding noise far under the 8-bit output)
    side_weight = 2.0               # a side sample spans two frames and enters L and R
    lookahead = 2048                # frames of source analysed for a block's LPC
    segment = 1536                  # units per independently encoded segment


class MonoParams(Params):
    """Mono layout (mid only)."""
    lam = 15000.0
    floor = 20


# ---------------------------------------------------------------- encoder ---

def _reflection(x, order):
    """Levinson-Durbin on a Hann-windowed segment with a small lag window;
    returns the reflection coefficients k[0..order-1] of x[n] ~ sum a[k] x[n-1-k]."""
    x = np.asarray(x, np.float64)
    ks = np.zeros(order)
    if len(x) <= order or not np.any(x):
        return ks
    w = x * np.hanning(len(x) + 2)[1:-1]
    r = np.array([np.dot(w[:len(w) - k], w[k:]) for k in range(order + 1)])
    r[0] *= 1.0 + 1e-6
    r[1:] *= np.exp(-0.5 * (0.006 * np.arange(1, order + 1)) ** 2)
    a = np.zeros(order)
    e = r[0]
    for i in range(order):
        k = min(max((r[i + 1] - np.dot(a[:i], r[i:0:-1])) / e, -0.9999), 0.9999)
        a[:i] = a[:i] - k * a[:i][::-1]
        a[i] = k
        ks[i] = k
        e *= 1 - k * k
        if e <= 0:
            break
    return ks


def _choose(x, hist):
    """LPC order, reflection coefficient bytes and Q12 coefficients for one
    stream from a look-ahead window x (hist: MAX_ORDER samples before it,
    newest first), judged by the open-loop residual with the stored
    coefficients; a tap costs 3 header bytes."""
    n = len(x)
    full = np.concatenate([hist[::-1].astype(np.float64), x.astype(np.float64)])
    ks = _reflection(x, MAX_ORDER)
    codes = [int(round(np.arcsin(k) / (np.pi / 2) * 127)) & 255 for k in ks]
    best = None
    for order in (1, 2, 4, 6, 8):
        c = np.array(step_up(codes[:order]), np.float64)
        pred = np.zeros(n)
        for k in range(order):
            pred += c[k] * full[MAX_ORDER - 1 - k:MAX_ORDER - 1 - k + n]
        e = x - np.floor((pred + 2048) / 4096)
        cost = n * 0.5 * np.log2(float(np.dot(e, e)) / max(n, 1) + 1.0) + 24 * order
        if best is None or cost < best[0]:
            best = (cost, order)
    order = best[1]
    return order, codes[:order], step_up(codes[:order])


def _header_symbol(idx, cur, tab):
    """Header symbol of a subblock with step index idx (0 = zero subblock)
    after step index cur. Arrays broadcast."""
    d = idx - cur
    sym = np.where(np.abs(d) <= DELTA, (d + DELTA) * TABLES + tab, ESC_SYMBOL + tab)
    return np.where(idx == 0, ZERO_SYMBOL, sym)


def _header_bits(idx, cur, tab):
    sym = _header_symbol(idx, cur, tab)
    return _HLEN[sym] + np.where(sym >= ESC_SYMBOL, IDX_BITS, 0)


_STEPS = np.array(STEP, np.float64)


def _subblocks(t, src, h, c, cur, first, weight, params, native=True):
    """Rate-distortion coding of one subblock for N streams in parallel.
    t (N, SUB) targets; src (N, MAX_ORDER) source samples before t, newest
    first (open-loop step estimate); h (N, MAX_ORDER) decoded history, newest
    first; c (N, MAX_ORDER) Q12 coefficients (zero beyond the order); cur (N,)
    step index before this subblock, replaced by the estimate where first.
    Returns step index (0 = zero subblock), table, q, x, bits, the start step
    index and the new history."""
    N = len(t)
    offs = np.array(params.offsets, np.int64)
    K = len(offs)
    full = np.concatenate([src[:, ::-1], t], 1).astype(np.float64)
    pred = np.zeros(t.shape)
    for k in range(MAX_ORDER):
        pred += c[:, k:k + 1] * full[:, MAX_ORDER - 1 - k:MAX_ORDER - 1 - k + SUB]
    res = t - np.floor((pred + 2048) / 4096)
    base = np.round((np.log2(np.sqrt((res * res).mean(1)) + 1e-9) - 2) * 4 + 1).astype(np.int64)
    cur = np.where(first, np.clip(base, params.floor, 63), cur)
    idx = np.clip(base[:, None] + offs[None, :], params.floor, 63)          # (N, K)
    step = _STEPS[idx]
    # Decoded history as float64: every product and sum is an exact integer.
    hk = np.empty((N, K, MAX_ORDER + SUB))
    hk[:, :, :MAX_ORDER] = h[:, None, ::-1]
    cr = c[:, None, ::-1].astype(np.float64)
    qk = np.empty((N, K, SUB))
    tf = t.astype(np.float64)
    dz = 0.5 - params.deadzone
    if native and native_available():
        _native.inner(tf, c, step, dz, hk, qk)
    else:
        for j in range(SUB):
            acc = np.einsum('nkt,nkt->nk', hk[:, :, j:j + MAX_ORDER], np.broadcast_to(cr, (N, K, MAX_ORDER))) + 2048
            acc = np.mod(acc + 2147483648.0, 4294967296.0) - 2147483648.0     # 32-bit wrap
            pj = np.floor(acc / 4096)
            v = (tf[:, j:j + 1] - pj) / step
            qj = np.minimum(np.maximum(np.copysign(np.floor(np.abs(v) + dz), v), -QMAX), QMAX)
            qk[:, :, j] = qj
            hk[:, :, MAX_ORDER + j] = np.minimum(np.maximum(pj + qj * step, -32768), 32767)
    xk = hk[:, :, MAX_ORDER:]
    d = ((xk - tf[:, None, :]) ** 2).sum(2)
    qi = qk.astype(np.int64)
    tb = _residual_bits(qi)
    best_tab = tb.argmin(2)
    live = np.any(qi, 2)
    fidx = np.where(live, idx, 0)
    bits = _header_bits(fidx, cur[:, None], best_tab) + np.where(live, tb.min(2), 0)
    k = (d * weight[:, None] + params.lam * bits).argmin(1)
    r = np.arange(N)
    x = xk[r, k].astype(np.int64)
    newh = np.concatenate([x[:, ::-1], h], 1)[:, :MAX_ORDER]
    return fidx[r, k], best_tab[r, k], qi[r, k], x, bits[r, k], cur, newh


def _split_ms(pcm, channels):
    pcm = np.asarray(pcm, np.int64)
    mid = (pcm[:, 0] + pcm[:, 1]) >> 1
    if channels == 1:
        return mid, None
    side = (pcm[:, 0] - pcm[:, 1]) >> 1
    # Sample k represents frame 2k+1 so the player can interpolate causally.
    filt = np.convolve(np.concatenate([side, np.zeros(16, np.int64)]), _LP, 'same')
    return mid, np.clip(np.round(filt[1:len(side):2]), -32768, 32767).astype(np.int64)


class Cancelled(Exception):
    """Raised by the encoder when its cancel callback returns True."""


# Segments coded together in one group (wider groups vectorize better). A
# group holds its own PCM, mid/side signals and coding state only (about 1.5 MB
# per segment), so the memory a file needs does not grow with its length.
# Segments are coded independently, so the result does not depend on the
# group size (tests/test_converter_stream.py).
GROUP_SEGMENTS = 64


def _side_window(pcm, w0, total, k0, k1, piece=1 << 18):
    """Side samples k0..k1-1 of the stereo layout (as _split_ms) from PCM
    frames starting at frame w0 (pcm: (n, 2) frames w0.. as far as needed).
    Each sample is the 31-tap lowpass at frame 2k+1. Every piece of the
    window keeps 16 frames on each side (it starts at frame 0 at the start of
    the file and ends with the 16 zero frames after its end, as _split_ms),
    so every value is computed exactly as on the whole file."""
    out = np.zeros(max(k1 - k0, 0), np.int32)
    for a in range(k0, k1, piece):
        b = min(a + piece, k1)
        f0 = max(0, 2 * a + 1 - 31)
        f1 = min(2 * b + 32, total + 16)
        x = np.zeros(f1 - f0)
        n = min(f1, total) - f0
        p = pcm[f0 - w0:f0 - w0 + n]
        x[:n] = (p[:, 0].astype(np.int32) - p[:, 1]) >> 1
        filt = np.convolve(x, _LP, 'same')
        out[a - k0:b - k0] = np.clip(np.round(filt[2 * a + 1 - f0:2 * b - f0:2]), -32768, 32767)
    return out


class _Stream:
    """Coding state of the segments of one group for one stream (mid, or
    half-rate side). x (int32) holds the padded signal (MAX_ORDER zeros, the signal,
    zeros to the end of the last unit) from padded position base on: the
    group's samples, MAX_ORDER before them and the look-ahead after them."""
    def __init__(self, x, base, u0, units, per_unit, S, diagnostics=True):
        self.x = x
        self.base = base
        self.u0 = u0
        self.per_unit = per_unit
        self.h = np.zeros((S, MAX_ORDER), np.int64)
        self.c = np.zeros((S, MAX_ORDER), np.int64)
        self.cur = np.zeros(S, np.int64)
        self.first = np.ones(S, bool)
        subs = per_unit // SUB
        self.q = np.zeros((units, per_unit), np.int16)      # |q| <= QMAX
        self.rec = np.zeros((units, per_unit), np.int64) if diagnostics else None
        self.idx = np.zeros((units, subs), np.int8)
        self.tab = np.zeros((units, subs), np.int8)

    def at(self, u, j=0):
        """(targets, source history) of subblock j of units u."""
        p = u * self.per_unit + j * SUB + MAX_ORDER - self.base
        t = self.x[p[:, None] + np.arange(SUB)[None, :]]
        src = self.x[p[:, None] - 1 - np.arange(MAX_ORDER)[None, :]]
        return t, src

    def setup(self, s, a, frames_ahead):
        """LPC and history for a block of segment s starting at sample a."""
        a -= self.base
        hist = self.x[a + MAX_ORDER - 1 - np.arange(MAX_ORDER)]
        order, refl, coef = _choose(self.x[a + MAX_ORDER:a + MAX_ORDER + frames_ahead], hist)
        self.h[s] = hist
        self.c[s] = 0
        self.c[s, :order] = coef
        self.first[s] = True
        return {'order': order, 'refl': refl, 'hist': hist[:order].tolist()}


def _code_group(M, Sd, seg_start, seg_end, params, native, report, cancel):
    """Code the segments seg_start..seg_end (unit ranges) of one group; every
    segment is coded on its own (rows of the arrays below), in parallel.
    Returns the blocks (dicts) in no particular order."""
    stereo = Sd is not None
    S = len(seg_start)
    u = seg_start.copy()
    used = np.zeros(S, np.int64)
    count = np.zeros(S, np.int64)
    cap = np.zeros(S, np.int64)
    open_block = [None] * S
    blocks = []
    mw = np.ones(S)
    sw = np.full(S, params.side_weight)

    def finish(s):
        b = open_block[s]
        b['units'] = int(count[s])
        blocks.append(b)
        open_block[s] = None
        count[s] = 0
        used[s] = 0

    while True:
        if cancel is not None and cancel():
            raise Cancelled('cancelled')
        if report is not None:
            report(int((u - seg_start).sum()))
        act = np.nonzero(u < seg_end)[0]
        if not len(act):
            break
        for s in act:
            if open_block[s] is None:
                a = int(u[s])
                b = {'start': a, 'mid': M.setup(s, a * UNIT, params.lookahead)}
                taps = b['mid']['order']
                if stereo:
                    b['side'] = Sd.setup(s, a * UNIT // 2, params.lookahead // 2)
                    taps += b['side']['order']
                head = HEADER + (taps + 1) // 2 * 2 + 2 * taps
                cap[s] = (BLOCK - head) * 8
                open_block[s] = b
        ua = u[act]
        # Mid subblock 0 and the side subblock are independent: code them together.
        tm, srcm = M.at(ua, 0)
        if stereo:
            ts, srcs = Sd.at(ua, 0)
            r1 = _subblocks(np.concatenate([tm, ts]), np.concatenate([srcm, srcs]),
                            np.concatenate([M.h[act], Sd.h[act]]), np.concatenate([M.c[act], Sd.c[act]]),
                            np.concatenate([M.cur[act], Sd.cur[act]]), np.concatenate([M.first[act], Sd.first[act]]),
                            np.concatenate([mw[act], sw[act]]), params, native)
            n = len(act)
            m0 = [v[:n] for v in r1]
            s0 = [v[n:] for v in r1]
        else:
            m0 = _subblocks(tm, srcm, M.h[act], M.c[act], M.cur[act], M.first[act], mw[act], params, native)
        tm1, srcm1 = M.at(ua, 1)
        cur1 = np.where(m0[0] > 0, m0[0], m0[5])
        m1 = _subblocks(tm1, srcm1, m0[6], M.c[act], cur1, np.zeros(len(act), bool), mw[act], params, native)
        bits = m0[4] + m1[4] + (s0[4] if stereo else 0)
        fits = (count[act] == 0) | (used[act] + bits <= cap[act])
        assert np.all(bits[count[act] == 0] <= cap[act][count[act] == 0]), 'one unit cannot overflow a block'
        ok = act[fits]
        uo = u[ok] - M.u0
        f = fits
        for st, parts in ((M, (m0, m1)), (Sd, (s0,)) if stereo else (None, ())):
            if st is None:
                continue
            for j, r in enumerate(parts):
                idx, tab, q, x, _, init, newh = [v[f] for v in r]
                st.idx[uo, j] = idx
                st.tab[uo, j] = tab
                st.q[uo, j * SUB:(j + 1) * SUB] = q
                if st.rec is not None:
                    st.rec[uo, j * SUB:(j + 1) * SUB] = x
                if j == 0:
                    for s_, i_ in zip(ok[st.first[ok]], init[st.first[ok]]):
                        open_block[s_]['mid' if st is M else 'side']['init'] = int(i_)
            last = parts[-1]
            st.h[ok] = last[6][f]
            cur = parts[0][5][f]
            for r in parts:
                cur = np.where(r[0][f] > 0, r[0][f], cur)
            st.cur[ok] = cur
            st.first[ok] = False
        used[ok] += bits[f]
        count[ok] += 1
        u[ok] += 1
        for s in act[~fits]:
            finish(s)
        for s in ok:
            if count[s] == MAX_FRAMES // UNIT or u[s] == seg_end[s]:
                finish(s)
    blocks.sort(key=lambda b: b['start'])
    return blocks


def _encode_group(window, w0, total, u0, u1, nunits, seg, channels, params, native=True,
                  diagnostics=False, report=None, cancel=None):
    """Encode units u0..u1 (whole segments of seg units; u1 = nunits for the
    last group) of a file of `total` frames. window: (n, 2) PCM frames from
    frame w0 on, covering frames u0 * UNIT - 64 (or 0) up to
    u1 * UNIT + params.lookahead + 64 (or the end of the file).
    Returns [(first frame, block bytes)] and, with diagnostics, the
    reconstruction per block (as encode())."""
    la = params.lookahead
    stereo = channels == 2
    units = u1 - u0
    # Mid: padded positions u0*UNIT .. (padded position i holds sample i - MAX_ORDER).
    mlo, mhi = u0 * UNIT, min(u1 * UNIT + MAX_ORDER + la, MAX_ORDER + nunits * UNIT)
    xm = np.zeros(mhi - mlo, np.int32)
    s0, s1 = max(mlo - MAX_ORDER, 0), min(mhi - MAX_ORDER, total)
    for a in range(s0, s1, 1 << 18):
        p = window[a - w0:min(a + (1 << 18), s1) - w0]
        xm[a - (mlo - MAX_ORDER):a - (mlo - MAX_ORDER) + len(p)] = (p[:, 0].astype(np.int32) + p[:, 1]) >> 1
    S = (units + seg - 1) // seg
    M = _Stream(xm, mlo, u0, units, UNIT, S, diagnostics)
    Sd = None
    if stereo:
        half = UNIT // 2
        slo, shi = u0 * half, min(u1 * half + MAX_ORDER + la // 2, MAX_ORDER + nunits * half)
        xs = np.zeros(shi - slo, np.int32)
        k0, k1 = max(slo - MAX_ORDER, 0), min(shi - MAX_ORDER, total // 2)
        xs[k0 - (slo - MAX_ORDER):k1 - (slo - MAX_ORDER)] = _side_window(window, w0, total, k0, k1)
        Sd = _Stream(xs, slo, u0, units, half, S, diagnostics)
    seg_start = np.arange(u0, u1, seg)
    seg_end = np.minimum(seg_start + seg, u1)
    blocks = _code_group(M, Sd, seg_start, seg_end, params, native, report, cancel)
    out, diag = [], []
    for b in blocks:
        a, n = b['start'], b['units']
        frames = min(n * UNIT, total - a * UNIT)
        la_ = a - u0
        silent = stereo and not Sd.idx[la_:la_ + n].any() and np.abs(Sd.x[MAX_ORDER + la_ * UNIT // 2:MAX_ORDER + (la_ + n) * UNIT // 2].astype(np.int64)).max() < 64
        data = _block_bytes(b, M, Sd if stereo and not silent else None, silent, frames)
        assert len(data) <= BLOCK
        out.append((a * UNIT, data))
        if not diagnostics:
            continue
        mid = M.rec[la_:la_ + n].reshape(-1)[:frames]
        side = None
        if stereo:
            if silent:
                side = (0, np.zeros((frames + 1) // 2, np.int64))
            else:
                side = (int(b['side']['hist'][0]), Sd.rec[la_:la_ + n].reshape(-1)[:(frames + 1) // 2])
        diag.append((mid, side))
    return out, diag


def _group_job(args):
    """One group in a worker process (StreamEncoder with workers > 1)."""
    return _encode_group(*args)[0]


class StreamEncoder:
    """GAC v4 encoder fed with PCM in pieces of any size (bounded memory:
    it keeps the frames of the groups being coded and the look-ahead after
    them). push() and finish() return the finished blocks in file order as
    (first frame, block bytes without the zero padding to BLOCK); the result
    equals encode() on the whole PCM.

    expected_frames (optional, an estimate) is used for progress only.
    cancel: callable; when it returns True the encoder raises Cancelled.
    workers > 1 codes groups in that many worker processes (spawned; the
    same function, so the same bytes); if they cannot run, the groups are
    coded in this process. Call close() (or use `with`) to stop them."""
    def __init__(self, channels, params=None, *, native=True, diagnostics=False, progress=None,
                 cancel=None, group=GROUP_SEGMENTS, expected_frames=None, workers=1):
        if channels not in (1, 2):
            raise ValueError('channels must be 1 or 2')
        self.channels = channels
        self.params = params if params is not None else Params if channels == 2 else MonoParams
        self.native, self.diagnostics = native, diagnostics
        self.progress, self.cancel = progress, cancel
        self.group = max(1, int(group))
        self.expected = expected_frames
        self.workers = 1 if diagnostics else max(1, int(workers))
        self.frames = 0                  # frames received
        self._parts = []                 # PCM not yet coded: frames self._start..self.frames
        self._start = 0
        self._u0 = 0                     # first unit of the next group
        self.diag = []
        self._last = -1
        self.done_units = 0
        self._pool = None
        self._pending = []               # [(future or None, job args, units)] in file order

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        """Stop the worker processes (at once; unfinished work is dropped)."""
        pool, self._pool = self._pool, None
        self._pending = []
        if pool is None:
            return
        terminate = getattr(pool, 'terminate_workers', None)
        if terminate is not None:
            try:
                terminate()
            except Exception:
                pass
        else:
            for proc in list((getattr(pool, '_processes', None) or {}).values()):
                try:
                    proc.terminate()
                except Exception:
                    pass
        pool.shutdown(wait=False, cancel_futures=True)

    def _need(self):
        """Frames after which the next (not last) group can be coded."""
        return (self._u0 + self.group * self.params.segment) * UNIT + self.params.lookahead + 64

    def _report(self, units, phase='Encoding GAC'):
        if self.progress is None:
            return
        total = max(self.expected or 0, self.frames, 1)
        percent = min(99, int(100 * units * UNIT / total))
        if percent != self._last:
            self._last = percent
            self.progress(percent / 100, phase)

    def _check(self):
        if self.cancel is not None and self.cancel():
            raise Cancelled('cancelled')

    def push(self, pcm):
        self._check()
        pcm = np.asarray(pcm)
        if pcm.ndim != 2 or pcm.shape[1] != 2:
            raise ValueError('PCM must have shape (frames, 2)')
        if len(pcm):
            self._parts.append(pcm)
            self.frames += len(pcm)
        out = []
        while self.frames >= self._need():
            out += self._code(last=False)
        return out

    def finish(self):
        """Code the rest; returns its blocks. Raises ValueError without audio."""
        if not self.frames:
            raise ValueError('no audio')
        out = self._code(last=True)
        while self._pending:
            out += self._collect()
        self.close()
        if self.progress is not None:
            self.progress(1.0, 'GAC encoded')
        return out

    def _start_pool(self):
        if self._pool is None and self.workers > 1:
            try:
                import multiprocessing
                from concurrent.futures import ProcessPoolExecutor
                self._pool = ProcessPoolExecutor(self.workers, mp_context=multiprocessing.get_context('spawn'))
            except Exception:
                self.workers = 1
        return self._pool

    def _inline(self, args, units):
        base = self.done_units
        report = None if self.progress is None else (lambda n: self._report(base + n))
        blocks, diag = _encode_group(*args, report=report, cancel=self.cancel)
        self.diag += diag
        self.done_units += units
        self._report(self.done_units)
        return blocks

    def _collect(self):
        """Blocks of the oldest pending group (waits for it)."""
        future, args, units = self._pending.pop(0)
        if future is not None:
            from concurrent.futures import wait
            while True:
                self._check()
                if wait([future], timeout=0.1)[0]:
                    break
            try:
                blocks = future.result()
            except Exception as error:
                from concurrent.futures.process import BrokenProcessPool
                if not isinstance(error, (BrokenProcessPool, OSError, RuntimeError)):
                    raise
                # Workers cannot run here: code the rest in this process.
                pending, self._pending = self._pending, []
                self.close()
                self.workers = 1
                self._pending = [(None, a, n) for _, a, n in [(future, args, units)] + pending]
                return self._collect()
            self.done_units += units
            self._report(self.done_units)
            return blocks
        return self._inline(args, units)

    def _code(self, last):
        p = self.params
        if last:
            nunits = (self.frames + UNIT - 1) // UNIT
            seg = max(1, min(p.segment, nunits))
            u1 = nunits
        else:
            # A group before the last sees the file as the frames received so
            # far: they reach past its look-ahead (_need), so nothing is cut.
            nunits = (self.frames + UNIT - 1) // UNIT
            seg = p.segment
            u1 = self._u0 + self.group * seg
        window = self._parts[0] if len(self._parts) == 1 else np.concatenate(self._parts)
        self._parts = [window]
        u0 = self._u0
        pool = self._pool if last else self._start_pool()
        if pool is not None:
            lo = max(0, u0 * UNIT - 64)
            hi = self.frames if last else min(self.frames, u1 * UNIT + p.lookahead + 64)
            job = (np.ascontiguousarray(window[lo - self._start:hi - self._start]), lo, self.frames,
                   u0, u1, nunits, seg, self.channels, p, self.native, False)
        else:
            job = (window, self._start, self.frames, u0, u1, nunits, seg, self.channels, p,
                   self.native, self.diagnostics)
        out = []
        if pool is not None:
            try:
                self._pending.append((pool.submit(_group_job, job), job, u1 - u0))
            except Exception:
                self.close()
                self.workers = 1
                self._pending.append((None, job, u1 - u0))
            while len(self._pending) > self.workers:
                out += self._collect()
        else:
            while self._pending:
                out += self._collect()
            out += self._inline(job, u1 - u0)
        self._u0 = u1
        keep = u1 * UNIT - 128
        if not last and keep > self._start:
            self._parts = [window[keep - self._start:]]
            self._start = keep
        if last:
            self._parts = []
        return out


def encode(pcm, channels, params=None, *, diagnostics=True, native=True, progress=None, group=GROUP_SEGMENTS,
           workers=1):
    """pcm: (n, 2) int16 stereo. channels: 2 = mid + half-rate side, 1 = mid only.
    Returns (blocks, first frame of each block, total frames, info); info
    'output' holds the decoder's (L, R) values (before the 8-bit output stage).

    The file is cut into segments of params.segment units that are coded in
    parallel. Within a segment, units go into the current block until the
    next one does not fit; that unit then starts the next block. A block's
    LPC comes from a fixed look-ahead window, so it never depends on where
    the block ends. StreamEncoder does the same in bounded memory."""
    if channels not in (1, 2):
        raise ValueError('channels must be 1 or 2')
    pcm = np.asarray(pcm)
    if pcm.ndim != 2 or pcm.shape[1] != 2:
        raise ValueError('PCM must have shape (frames, 2)')
    if not len(pcm):
        raise ValueError('no audio')
    if pcm.dtype != np.int16:
        pcm = np.asarray(pcm, np.int64)
    with StreamEncoder(channels, params, native=native, diagnostics=diagnostics, progress=progress,
                       group=group, expected_frames=len(pcm), workers=workers) as enc:
        pieces = enc.push(pcm) + enc.finish()
    total = enc.frames
    out = [data.ljust(BLOCK, b'\0') for _, data in pieces]
    firsts = [a for a, _ in pieces]
    fill = sum(len(data) for _, data in pieces)
    if not diagnostics:
        return out, firsts, total, {'fill': fill / (len(out) * BLOCK)}
    stereo = channels == 2
    m = np.concatenate([d[0] for d in enc.diag])
    if stereo:
        parts = []
        for (_, (prev, s)), a, z in zip(enc.diag, firsts, firsts[1:] + [total]):
            full = np.empty(2 * len(s), np.int64)
            full[1::2] = s
            full[0::2] = (np.concatenate([[prev], s[:-1]]) + s) >> 1
            parts.append(full[:z - a])
        d = np.concatenate(parts)
        lr = np.stack([m + d, m - d], 1)
    else:
        lr = np.stack([m, m], 1)
    return out, firsts, total, {'fill': fill / (len(out) * BLOCK), 'output': lr, 'mid': m}


def _pack_bits(values, lengths):
    """LSB-first packing of (value, length) pairs."""
    values = np.asarray(values, np.int64)
    lengths = np.asarray(lengths, np.int64)
    keep = lengths > 0
    values, lengths = values[keep], lengths[keep]
    if not len(values):
        return b''
    width = int(lengths.max())
    bits = (values[:, None] >> np.arange(width)[None, :]) & 1
    return np.packbits(bits[np.arange(width)[None, :] < lengths[:, None]].astype(np.uint8),
                       bitorder='little').tobytes()


# Code tables padded to one width, indexed [table, symbol].
_NSYM = max(len(t) for t in _LEN)
_LEN2 = np.array([np.pad(t, (0, _NSYM - len(t))) for t in _LEN], np.int64)
_CODE2 = np.array([np.pad(t, (0, _NSYM - len(t))) for t in _CODE], np.int64)
_SLOTS = 2 + 3 * SUB             # header code, step index escape, then (code, raw, raw) per residual code


def _stream_codes(st, a, n, init):
    """(values, lengths) of units a..a+n (indices into st's arrays) of stream st,
    as (n, subblocks, _SLOTS) arrays in bitstream order; unused slots have length 0."""
    subs = st.per_unit // SUB
    idx = st.idx[a:a + n].reshape(-1).astype(np.int64)
    tab = st.tab[a:a + n].reshape(-1).astype(np.int64)
    q = st.q[a:a + n].reshape(-1, SUB).astype(np.int64)
    m = len(idx)
    live = idx != 0
    # Step index before each subblock: the last nonzero one before it, else init.
    pos = np.maximum.accumulate(np.where(live, np.arange(m), -1))
    prev = np.concatenate([[-1], pos[:-1]])
    cur = np.where(prev >= 0, idx[np.maximum(prev, 0)], init)
    sym = _header_symbol(idx, cur, tab)
    vals = np.zeros((m, _SLOTS), np.int64)
    lens = np.zeros((m, _SLOTS), np.int64)
    vals[:, 0] = _HCODE[sym]
    lens[:, 0] = _HLEN[sym]
    vals[:, 1] = idx
    lens[:, 1] = np.where(sym >= ESC_SYMBOL, IDX_BITS, 0)
    for single in (False, True):
        rows = live & ((tab >= PAIR_TABLES) if single else (tab < PAIR_TABLES))
        if not rows.any():
            continue
        t = tab[rows]
        rs, raw, rawn = _residual_codes(q[rows], PAIR_TABLES if single else 0)
        k = rs.shape[1]
        v = np.concatenate([_CODE2[t[:, None], rs][..., None], raw], 2).reshape(len(t), 3 * k)
        l = np.concatenate([_LEN2[t[:, None], rs][..., None], rawn], 2).reshape(len(t), 3 * k)
        vals[rows, 2:2 + 3 * k] = v
        lens[rows, 2:2 + 3 * k] = l
    return vals.reshape(n, subs, _SLOTS), lens.reshape(n, subs, _SLOTS)


def _block_bytes(b, M, Sd, silent, frames):
    a, n = b['start'], b['units']
    streams = [('mid', M)] + ([('side', Sd)] if Sd is not None else [])
    om = b['mid']['order']
    so = b['side']['order'] if Sd is not None else 0
    head = bytearray(int(frames).to_bytes(2, 'little'))
    head.append(om | (so << 4))
    head.append(1 if silent else 0)
    head.append(b['mid']['init'])
    head.append(b['side']['init'] if Sd is not None else 0)
    for k, _ in streams:
        head += bytes(b[k]['refl'])
    if len(head) & 1:
        head.append(0)
    for k, _ in streams:
        head += np.asarray(b[k]['hist'], '<i2').tobytes()
    codes = [_stream_codes(st, a - st.u0, n, b[k]['init']) for k, st in streams]
    # Per unit: the mid subblocks, then the side subblock.
    vals = np.concatenate([c[0] for c in codes], 1)
    lens = np.concatenate([c[1] for c in codes], 1)
    return bytes(head) + _pack_bits(vals.reshape(-1), lens.reshape(-1))


# ------------------------------------------------- reference decoder --------

class BitReader:
    """LSB-first reader clamped to its block: bits past the end read as zero."""
    def __init__(self, data, pos):
        self.data, self.pos = data, pos * 8

    def peek(self, n):
        v = 0
        for i in range(n):
            p = self.pos + i
            if (p >> 3) < len(self.data):
                v |= ((self.data[p >> 3] >> (p & 7)) & 1) << i
        return v

    def bits(self, n):
        v = self.peek(n)
        self.pos += n
        return v

    def code(self, lut):
        e = lut[self.peek(LUT_BITS)]
        self.pos += e >> 8
        return e & 255


def _s16(b, o):
    v = b[o] | (b[o + 1] << 8)
    return v - 65536 if v & 32768 else v


def _decode_sub(r, state, out):
    """One subblock; state = [coefs, history (newest first), step index]."""
    coefs, hist, _ = state
    s = r.code(HEADER_CODE[2])
    zero = s == ZERO_SYMBOL
    tab = 0
    if s >= ESC_SYMBOL:
        tab = s - ESC_SYMBOL
        state[2] = min(max(r.bits(IDX_BITS), 1), 63)
    elif not zero:
        tab = s % TABLES
        state[2] = min(max(state[2] + s // TABLES - DELTA, 1), 63)
    step = STEP[state[2]]
    lut = TABLE[tab][2]
    qs = [0] * SUB
    if not zero:
        raw = lambda: (lambda v: v - ((v & (1 << (ESC_BITS - 1))) << 1))(r.bits(ESC_BITS))
        i = 0
        while i < SUB:
            v = r.code(lut)
            if tab < PAIR_TABLES:
                if v == 0x88:
                    qs[i] = raw()
                    qs[i + 1] = raw()
                else:
                    qs[i] = (v >> 4) - ((v & 0x80) >> 3)
                    qs[i + 1] = (v & 15) - ((v & 8) << 1)
                i += 2
            else:
                v = v - 256 if v & 128 else v
                qs[i] = raw() if v == -128 else v
                i += 1
    for q in qs:
        acc = 2048
        for c, h in zip(coefs, hist):
            acc += c * h
        acc = ((acc + (1 << 31)) & 0xffffffff) - (1 << 31)
        x = max(-32768, min(32767, (acc >> 12) + q * step))
        if hist:
            hist.pop()
            hist.insert(0, x)
        out.append(x)


def decode_block(block, channels, frames, mono_out=False):
    """Decode the first `frames` frames of a block. Returns [(L, R)] (stereo
    sums not clamped; the output quantizer saturates them) or, with mono_out
    or the mono layout, [M]."""
    b = bytes(block)
    if len(b) != BLOCK or not 0 < frames <= MAX_FRAMES:
        raise ValueError('bad block')
    om = min(b[2] & 15, MAX_ORDER)
    so = min(b[2] >> 4, MAX_ORDER)
    side = channels == 2 and not b[3] & 1
    if not side:
        so = 0
    pos = HEADER
    mc = step_up(b[pos:pos + om])
    sc = step_up(b[pos + om:pos + om + so])
    pos += (om + so + 1) // 2 * 2
    mh = [_s16(b, pos + 2 * k) for k in range(om)]
    pos += 2 * om
    sh = [_s16(b, pos + 2 * k) for k in range(so)]
    pos += 2 * so
    ms = [mc, mh, min(max(b[4], 1), 63)]
    ss = [sc, sh, min(max(b[5], 1), 63)]
    prev = sh[0] if sh else 0
    r = BitReader(b, pos)
    out = []
    for _ in range((frames + UNIT - 1) // UNIT):
        m = []
        _decode_sub(r, ms, m)
        _decode_sub(r, ms, m)
        s = []
        if side:
            _decode_sub(r, ss, s)
        if mono_out or channels == 1:
            out.extend(m)
            continue
        for i in range(UNIT):
            k = i >> 1
            v = s[k] if side else 0
            d = v if i & 1 else (prev + v) >> 1
            if i & 1:
                prev = v
            out.append((m[i] + d, m[i] - d))
    return out[:frames]


def decode_block_fast(block, channels, frames, mono_out=False):
    """Reference-equivalent signed32 PCM array; optional bounded native decoder."""
    b = bytes(block)
    if len(b) != BLOCK or channels not in (1, 2) or not 0 < frames <= MAX_FRAMES:
        raise ValueError('bad block')
    if not native_available():
        return np.asarray(decode_block(b, channels, frames, mono_out), dtype=np.int32)
    shape = (frames,) if mono_out or channels == 1 else (frames, 2)
    out = np.empty(shape, dtype=np.int32)
    if not _native.LIB.gac_decode(b, channels, frames, bool(mono_out), out):
        raise ValueError('native decoder rejected block')
    return out


def quantize8(values, error=0):
    """Player output: first-order error-feedback quantization to signed 8-bit.
    Returns (list, final error). Same as v3."""
    out = []
    for v in values:
        w = max(-32768, min(32639, v + error))
        q = (w + 128) >> 8
        error = w - (q << 8)
        out.append(q)
    return out, error


def c_tables():
    """C source of the decoder tables (player/src/gac4_tables.h)."""
    def rows(values, per=16):
        return ',\n'.join(' ' + ','.join(str(v) for v in values[i:i + per]) for i in range(0, len(values), per))
    out = ['/* Generated by tools/gac4.py c_tables(); tests/test_gac4.py checks it. Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). */',
           '#define GAC4_STEPS {' + ','.join(map(str, STEP)) + '}',
           f'#define GAC4_REFLECT {{\\\n' + ',\\\n'.join(' ' + ','.join(str(v) for v in REFLECT[i:i + 16]) for i in range(0, 256, 16)) + '}',
           '#define GAC4_HEADER_LUT {\\\n' + ',\\\n'.join(' ' + ','.join(str(v) for v in HEADER_CODE[2][i:i + 32]) for i in range(0, 1 << LUT_BITS, 32)) + '}']
    for t in range(TABLES):
        lut = TABLE[t][2]
        out.append(f'#define GAC4_LUT{t} {{\\\n' + ',\\\n'.join(' ' + ','.join(str(v) for v in lut[i:i + 32]) for i in range(0, 1 << LUT_BITS, 32)) + '}')
    return '\n'.join(out) + '\n'


def enc_c_tables():
    """C source of the encoder tables (player/src/gac_enc_tables.h): code
    lengths and LSB-first codes of the header and residual tables, the
    side lowpass in Q15, and the reflection byte rounding midpoints
    (|k| in Q30 at (j + 0.5) / 127 * pi / 2)."""
    def arr(name, typ, values, per=24, hot=False):
        return f'static const {typ} {name}[{len(values)}]' + (' GE_HOT' if hot else '') + '={\n' + ',\n'.join(' ' + ','.join(str(int(v)) for v in values[i:i + per]) for i in range(0, len(values), per)) + '};'
    out = ['/* Generated by tools/gac4.py enc_c_tables() (python3 tools/gac4.py --enc);',
           ' * tests/test_gac_enc.py checks it. Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GE_HOT: placement of the tables',
           ' * used per sample (IWRAM on the GBA). */',
           '#ifndef GE_HOT', '#define GE_HOT', '#endif',
           arr('GE_HLEN', 'uint8_t', HEADER_CODE[0], hot=True), arr('GE_HCODE', 'uint16_t', HEADER_CODE[1], hot=True)]
    for t in range(TABLES):
        out += [arr(f'GE_LEN{t}', 'uint8_t', TABLE[t][0], hot=True), arr(f'GE_CODE{t}', 'uint16_t', TABLE[t][1], hot=True)]
    out.append(arr('GE_LP', 'int16_t', [int(round(v * 32768)) for v in _LP], hot=True))
    out.append(arr('GE_KMID', 'int32_t', [int(round((1 << 30) * np.sin((j + 0.5) / 127 * np.pi / 2))) for j in range(127)]))
    out.append(arr('GE_HANN', 'uint16_t', [int(round(65535 * (0.5 - 0.5 * np.cos(2 * np.pi * (i + 1) / 2049)))) for i in range(1024)]))
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    import sys
    sys.stdout.write(enc_c_tables() if '--enc' in sys.argv else c_tables())
