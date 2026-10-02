#!/usr/bin/env python3
# SkyEngine ETC2/EAC decoder v4 (independent implementation, per Khronos ETC2/EAC public spec)
# usage: etc2_decode.py <in.ktx> <out.png>
import struct, sys
from PIL import Image

# ETC luminance modifier table (per spec, 8 groups x 4 values: c+d, c-d pairs)
MODIFIER_TABLE = [
    [2, 8, -2, -8], [5, 17, -5, -17], [9, 29, -9, -29], [13, 42, -13, -42],
    [18, 60, -18, -60], [24, 80, -24, -80], [33, 106, -33, -106], [47, 183, -47, -183],
]
# T/H mode color distance table
DISTANCE_TABLE = [3, 6, 11, 16, 23, 32, 41, 64]


def clamp8(v):
    return 0 if v < 0 else (255 if v > 255 else v)


def widen_bits(v, n):
    """Extend an n-bit component to 8 bits (n=4/5/6/7)."""
    if n == 4: return (v << 4) | v
    if n == 5: return (v << 3) | (v >> 2)
    if n == 6: return (v << 2) | (v >> 4)
    if n == 7: return (v << 1) | (v >> 6)
    return v


def signed3(v):
    """3-bit signed value (two's complement)."""
    return v - 8 if v >= 4 else v


def selector_bits(word_lo, i):
    """2-bit color index of pixel i in a block (from lower half of the 64-bit index word)."""
    return (((word_lo >> (16 + i)) & 1) << 1) | ((word_lo >> i) & 1)


def fill_etc1_block(c0, c1, t0, t1, flip, word_lo):
    """ETC1 base fill: flip=1 row partition (top 2 rows / bottom 2 rows), flip=0 column partition."""
    px = [[0, 0, 0] for _ in range(16)]
    for y in range(4):
        for x in range(4):
            i = x * 4 + y          # index bit order: column-first
            idx = selector_bits(word_lo, i)
            if flip:
                col = c0 if y < 2 else c1
                tbl = t0 if y < 2 else t1
            else:
                col = c0 if x < 2 else c1
                tbl = t0 if x < 2 else t1
            m = MODIFIER_TABLE[tbl][idx]
            p = y * 4 + x          # output pixel order: row-first
            px[p] = [clamp8(col[0] + m), clamp8(col[1] + m), clamp8(col[2] + m)]
    return px


def decode_individual(hi, lo, flip):
    """Individual mode: each color channel has its own 4 bits."""
    r0 = widen_bits((hi >> 28) & 0xf, 4)
    g0 = widen_bits((hi >> 20) & 0xf, 4)
    b0 = widen_bits((hi >> 12) & 0xf, 4)
    r1 = widen_bits((hi >> 24) & 0xf, 4)
    g1 = widen_bits((hi >> 16) & 0xf, 4)
    b1 = widen_bits((hi >> 8) & 0xf, 4)
    t0 = (hi >> 5) & 7
    t1 = (hi >> 2) & 7
    return fill_etc1_block([r0, g0, b0], [r1, g1, b1], t0, t1, flip, lo)


def decode_delta(hi, lo, flip):
    """Differential mode: 5-bit base + 3-bit delta."""
    r5 = (hi >> 27) & 0x1f
    g5 = (hi >> 19) & 0x1f
    b5 = (hi >> 11) & 0x1f
    dr = signed3((hi >> 24) & 7)
    dg = signed3((hi >> 16) & 7)
    db = signed3((hi >> 8) & 7)
    c0 = [widen_bits(r5, 5), widen_bits(g5, 5), widen_bits(b5, 5)]
    c1 = [widen_bits((r5 + dr) & 0x1f, 5), widen_bits((g5 + dg) & 0x1f, 5), widen_bits((b5 + db) & 0x1f, 5)]
    t0 = (hi >> 5) & 7
    t1 = (hi >> 2) & 7
    return fill_etc1_block(c0, c1, t0, t1, flip, lo)


def decode_t_mode(hi, lo):
    """T mode: symmetric three colors + distance color."""
    h0 = (hi >> 24) & 0xff
    h1 = (hi >> 16) & 0xff
    h2 = (hi >> 8) & 0xff
    h3 = hi & 0xff
    r0 = widen_bits(((h0 & 0x18) >> 1) | (h0 & 0x3), 4)
    g0 = widen_bits(h1 >> 4, 4)
    b0 = widen_bits(h1 & 0xf, 4)
    r1 = widen_bits(h2 >> 4, 4)
    g1 = widen_bits(h2 & 0xf, 4)
    b1 = widen_bits(h3 >> 4, 4)
    dist = DISTANCE_TABLE[(((h3 >> 1) & 6) | (h3 & 1))]
    cols = [
        [r0, g0, b0],
        [clamp8(r1 + dist), clamp8(g1 + dist), clamp8(b1 + dist)],
        [r1, g1, b1],
        [clamp8(r1 - dist), clamp8(g1 - dist), clamp8(b1 - dist)],
    ]
    return scatter_colors(cols, lo)


def decode_h_mode(hi, lo):
    """H mode: two colors +/- distance."""
    h0 = (hi >> 24) & 0xff
    h1 = (hi >> 16) & 0xff
    h2 = (hi >> 8) & 0xff
    h3 = hi & 0xff
    r0 = widen_bits(h0 >> 3, 4)
    g0 = widen_bits(((h0 & 0x7) << 1) | (h1 >> 7), 4)
    b0 = widen_bits(((h1 & 0x8) >> 1) | ((h1 & 0x3) << 1) | (h2 >> 7), 4)
    r1 = widen_bits(h2 >> 3, 4)
    g1 = widen_bits(((h2 & 0x7) << 1) | (h3 >> 7), 4)
    b1 = widen_bits(h3 >> 3, 4)
    di = ((h3 & 0x4) >> 1) | (h3 & 1)
    base = 1 if ((r0 << 16) | (g0 << 8) | b0) >= ((r1 << 16) | (g1 << 8) | b1) else 0
    dist = DISTANCE_TABLE[di | (base << 2)]
    cols = [
        [clamp8(r0 + dist), clamp8(g0 + dist), clamp8(b0 + dist)],
        [clamp8(r0 - dist), clamp8(g0 - dist), clamp8(b0 - dist)],
        [clamp8(r1 + dist), clamp8(g1 + dist), clamp8(b1 + dist)],
        [clamp8(r1 - dist), clamp8(g1 - dist), clamp8(b1 - dist)],
    ]
    return scatter_colors(cols, lo)


def decode_planar(hi, lo):
    """Planar mode: three anchor colors, bilinear pixel interpolation."""
    h0 = (hi >> 24) & 0xff
    h1 = (hi >> 16) & 0xff
    h2 = (hi >> 8) & 0xff
    h3 = hi & 0xff
    l0 = (lo >> 24) & 0xff
    l1 = (lo >> 16) & 0xff
    l2 = (lo >> 8) & 0xff
    l3 = lo & 0xff
    r_o = widen_bits(h0 >> 1, 6)
    g_o = widen_bits(((h0 & 1) << 6) | (h1 >> 1), 7)
    b_o = widen_bits(((h1 & 1) << 5) | (h2 >> 2) | (h2 & 1), 6)
    r_h = widen_bits((h3 >> 3) | ((h3 & 0x4) >> 2), 6)
    g_h = widen_bits(((h3 & 0x3) << 5) | (l0 >> 3), 7)
    b_h = widen_bits(((l0 & 0x7) << 3) | (l1 >> 5), 6)
    r_v = widen_bits(((l1 & 0x1f) << 1) | (l2 >> 7), 6)
    g_v = widen_bits(l2 & 0x7f, 7)
    b_v = widen_bits(l3 >> 2, 6)
    px = [[0, 0, 0] for _ in range(16)]
    for y in range(4):
        for x in range(4):
            p = y * 4 + x
            px[p] = [
                clamp8((x * (r_h - r_o) + y * (r_v - r_o) + 4 * r_o + 2) >> 2),
                clamp8((x * (g_h - g_o) + y * (g_v - g_o) + 4 * g_o + 2) >> 2),
                clamp8((x * (b_h - b_o) + y * (b_v - b_o) + 4 * b_o + 2) >> 2),
            ]
    return px


def scatter_colors(cols, word_lo):
    """Scatter 4 palette colors to 16 pixels by 2-bit index."""
    px = [[0, 0, 0] for _ in range(16)]
    for y in range(4):
        for x in range(4):
            i = x * 4 + y
            idx = selector_bits(word_lo, i)
            p = y * 4 + x
            px[p] = cols[idx][:]
    return px


def decode_rgb_block(hi, lo):
    """ETC2 RGB block: routed to five modes by diff/flip bits."""
    diff = (hi >> 1) & 1
    flip = hi & 1
    if diff == 0:
        return decode_individual(hi, lo, flip)
    r5 = (hi >> 27) & 0x1f
    g5 = (hi >> 19) & 0x1f
    b5 = (hi >> 11) & 0x1f
    dr = signed3((hi >> 24) & 7)
    dg = signed3((hi >> 16) & 7)
    db = signed3((hi >> 8) & 7)
    r_end = r5 + dr
    g_end = g5 + dg
    b_end = b5 + db
    if r_end < 0 or r_end > 31:
        return decode_t_mode(hi, lo)
    if g_end < 0 or g_end > 31:
        return decode_h_mode(hi, lo)
    if b_end < 0 or b_end > 31:
        return decode_planar(hi, lo)
    return decode_delta(hi, lo, flip)


def decode_eac_alpha(blk):
    """EAC 8-bit single-channel block: two base values + 6-value interpolation table + 3-bit index."""
    a0 = blk[0]
    a1 = blk[1]
    lut = [0] * 8
    lut[0] = a0
    lut[1] = a1
    if a0 > a1:
        for i in range(1, 7):
            lut[i + 1] = ((7 - i) * a0 + i * a1) // 7
    else:
        for i in range(1, 5):
            lut[i + 1] = ((5 - i) * a0 + i * a1) // 5
        lut[6] = 0
        lut[7] = 255
    bits = blk[2] | (blk[3] << 8) | (blk[4] << 16) | (blk[5] << 24) | (blk[6] << 32) | (blk[7] << 40)
    return [lut[(bits >> (i * 3)) & 7] for i in range(16)]


def decode_ktx(path):
    """KTX container parse + per-block ETC2/EAC decode."""
    data = open(path, 'rb').read()
    def u32_le(off):
        return struct.unpack('<I', data[off:off + 4])[0]
    fmt = u32_le(28)
    w = u32_le(36)
    h = u32_le(40)
    kv = u32_le(60)
    off = 64 + kv
    img_size = u32_le(off)
    payload = data[off + 4:off + 4 + img_size]
    has_alpha = fmt in (0x9278, 0x9279)   # ETC2 RGBA / SRGBA
    bx_count = (w + 3) // 4
    by_count = (h + 3) // 4
    img = Image.new('RGBA' if has_alpha else 'RGB', (w, h))
    px = img.load()
    p = 0
    for by in range(by_count):
        for bx in range(bx_count):
            alpha = None
            if has_alpha:
                alpha = decode_eac_alpha(payload[p:p + 8])
                p += 8
            hi = struct.unpack('>I', payload[p:p + 4])[0]    # high 32 bits (big-endian)
            lo = struct.unpack('>I', payload[p + 4:p + 8])[0]  # low 32 bits (big-endian)
            p += 8
            pix = decode_rgb_block(hi, lo)
            for i in range(16):
                ox = bx * 4 + (i >> 2)
                oy = by * 4 + (i & 3)
                if ox >= w or oy >= h:
                    continue
                c = pix[i]
                if has_alpha:
                    px[ox, oy] = (c[0], c[1], c[2], alpha[i])
                else:
                    px[ox, oy] = (c[0], c[1], c[2])
    return img


if __name__ == '__main__':
    img = decode_ktx(sys.argv[1])
    img.save(sys.argv[2])
    print(f'decode {sys.argv[1]} -> {sys.argv[2]}: {img.size} {img.mode}')
