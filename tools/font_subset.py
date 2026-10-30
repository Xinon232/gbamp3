#!/usr/bin/env python3
"""Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Keep only the SuperFW font-pack blocks gbamp3 uses:
Latin-1 Supplement + Latin Extended-A/B (U+0080-024F) and Greek/Cyrillic
(U+0370-04FF). ASCII comes from the renderer's embedded table.
Glyph data is unchanged SuperFW data (GPL-3.0-or-later, UNSCII/Unifont)."""
import struct, sys
KEEP = ((0x80, 0x24f), (0x370, 0x4ff))
src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
magic, ver, n, size = struct.unpack_from('<2sBBI', d, 0)
assert magic == b'FO' and ver == 1
blocks = [struct.unpack_from('<4I', d, 8 + 16*i) for i in range(n)]
base = 8 + 16*n
ends = [b[3] for b in blocks[1:]] + [size]
keep = [(b, d[base+b[3]:base+e]) for b, e in zip(blocks, ends) if (b[0], b[1]) in KEEP]
assert len(keep) == len(KEEP), 'expected blocks missing'
out, data = bytearray(), bytearray()
for (s, e, f, _), payload in keep:
    out += struct.pack('<4I', s, e, f, len(data)); data += payload
    while len(data) % 4: data += b'\0'
total = 8 + len(out) + len(data)
blob = struct.pack('<2sBBI', b'FO', 1, len(keep), total) + out + data
open(dst, 'wb').write(blob)
print(f'{dst}: {len(keep)} blocks, {len(blob)} bytes')
