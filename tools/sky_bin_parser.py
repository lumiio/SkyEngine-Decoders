#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sky_bin_parser.py - Sky: Children of the Light binary asset format parser (reverse engineering)

basis:
- .bin structure notes (meshopt index encoding / BstBaked.meshes /
 LightBakeJob / CloudVoxelBakeJob / u_probeSh* / u_atmosphere* etc.）
- Bin.zip / MainStreet.zip real asset byte-level reverse engineering
- public format references (Ogre-like mesh, Journey-derived TGCL)

usage：
  python3 sky_bin_parser.py <file.mesh | BstBaked.meshes | Objects.level.bin>

output: JSON format structure report (stdout).
for personal format research and learning only; does not ship game assets with the engine.
"""
import json, struct, re, sys

def u32(d, o): return struct.unpack('<I', d[o:o+4])[0]
def f32(d, o): return struct.unpack('<f', d[o:o+4])[0]
def be16(d, o): return struct.unpack('>H', d[o:o+2])[0]

def cstr(d, o, maxlen=64):
    e = d.find(b'\x00', o, o+maxlen)
    if e < 0: e = o+maxlen
    return d[o:e].decode('latin1')

def scan_names(d, start, end, stride=132):
    """Scan node name table (32B name + data) in [start, end) by stride."""
    out = []
    o = start
    while o + stride <= end:
        name = cstr(d, o)
        if not name or not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', name):
            o += 1
            continue
        out.append((o, name))
        o += stride
    return out

def parse_mesh(path):
    d = open(path, 'rb').read(); n = len(d)
    r = {'type': 'mesh', 'file': path, 'size': n}
    name = d[:32].split(b'\x00')[0].decode('latin1')
    r['name'] = name
    # from 0x40: num_lods / flags / data section table (big-endian small values + half-inf sentinel)
    r['u32_0x40_0x50'] = [u32(d, o) for o in range(0x40, 0x50, 4)]
    seg = [struct.unpack('>I', d[o:o+4])[0] for o in range(0x50, 0x60, 4)]
    r['data_seg_table'] = seg  # e.g. [20388,26805,27889,0x7f80(half-inf)]
    r['num_lods'] = u32(d, 0x44)
    # pose region float stream start (0x60, if 0x5c is half+inf sentinel)
    if d[0x5e:0x60] == b'\x80\x7f':
        r['pose_float_start'] = 0x60
        fs = [round(f32(d, 0x60+i*12), 3) for i in range(min(8, (n-0x60)//12))]
        r['pose_float_sample'] = fs
  # node name table (bone/child mesh names, 132B entries)
    for start in (0x5002, 0x856f):
        names = scan_names(d, start, min(n, start+0x9000))
        if names:
            r.setdefault('node_tables', {})[hex(start)] = {
                'count': len(names),
                'sample': [x[1] for x in names[:8]],
            }
  # compressed data region detection (high-entropy start)
  # high-entropy rule: >60% bytes not in [0x20,0x7e]
    for o in range(0x60, n-256, 4):
        blk = d[o:o+256]
        ent = sum(1 for b in blk if not (0x20 <= b <= 0x7e)) / 256
        if ent > 0.6:
            r['compressed_region_start'] = hex(o)
            break
    return r

def parse_meshes_bundle(path):
    d = open(path, 'rb').read(); n = len(d)
    r = {'type': 'leveldata', 'file': path, 'size': n}
    r['magic'] = d[:5].decode('latin1')            # 'LVL0='
    r['version'] = u32(d, 8)
 # tag section
    def tag(o):
        t = d[o:o+4].decode('latin1'); v = u32(d, o+4)
        return t, v
    r['GEO0'] = {'off': u32(d, 0x10), 'val': u32(d, 0x14)}
    r['LOD0'] = {'off': u32(d, 0x1c)}
    r['METREU'] = {'off': u32(d, 0x2b) if False else None}
  # scene AABB (0x70-0x84, six floats)
    aabb = [round(f32(d, 0x70+i*4), 2) for i in range(6)]
    r['scene_aabb'] = {'min': aabb[:3], 'max': aabb[3:6]}
  # mesh block dimension table from 0x88
    sizes = [u32(d, 0x88+i*4) for i in range(6)]
    r['block_sizes_0x88'] = sizes
  # trailer catalog table (last 64B)
    tail = [u32(d, n-64+i*4) for i in range(16)]
    r['tail_catalog'] = tail
  # tail index stream (u16, values ~6000 level -> real triangle indices)
    idx = [struct.unpack('<H', d[o:o+2])[0] for o in range(n-0x100, n-0x64, 2)]
    r['tail_index_sample'] = idx[:20]
    r['tail_index_minmax'] = [min(idx), max(idx)]
  # mesh catalog entries (offsets consistent with header values)
    r['catalog_anchors'] = {
        '2551675': hex(d.find(struct.pack('<I', 2551675))),
        '464952': hex(d.find(struct.pack('<I', 464952))),
    }
    return r

def parse_level(path):
    d = open(path, 'rb').read(); n = len(d)
    r = {'type': 'level', 'file': path, 'size': n}
    r['magic'] = d[:4].decode('latin1')            # 'TGCL'
    r['version'] = u32(d, 4)
    hdr = [u32(d, o) for o in range(8, 0x80, 4)]
    r['header_table'] = hdr
  # string table location: first offset >=0x10000 and < n (=0xf784)
    strtab = next((v for v in hdr if 0x10000 <= v < n), None)
    r['string_table_start'] = hex(strtab) if strtab else None
    if strtab:
        strs = re.findall(rb'[\x20-\x7e]{4,}', d[strtab:strtab+0x8000])
        r['string_count_sample'] = len(strs)
        r['string_sample'] = [s.decode('latin1') for s in strs[:15]]
  # Lua script string keyword stats
    allstrs = re.findall(rb'[\x20-\x7e]{5,}', d)
    r['total_strings'] = len(allstrs)
    kw = ['mesh', 'light', 'cloud', 'transform', 'anim', 'script', 'spawn']
    r['keyword_hits'] = {k: sum(1 for s in allstrs if k.encode() in s.lower()) for k in kw}
    return r

if __name__ == '__main__':
    p = sys.argv[1]
    if p.endswith('.meshes') or 'BstBaked' in p:
        print(json.dumps(parse_meshes_bundle(p), ensure_ascii=False, indent=1))
    elif p.endswith('.level.bin') or p.endswith('level'):
        print(json.dumps(parse_level(p), ensure_ascii=False, indent=1))
    else:
        print(json.dumps(parse_mesh(p), ensure_ascii=False, indent=1))
