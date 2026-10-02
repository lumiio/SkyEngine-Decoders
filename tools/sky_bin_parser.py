#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sky_bin_parser.py — Sky: Children of the Light 二进制资产格式解析器（逆向研究）

依据：
  - .bin 结构要点（meshopt 索引编码 / BstBaked.meshes /
    LightBakeJob / CloudVoxelBakeJob / u_probeSh* / u_atmosphere* 等）
  - Bin.zip / MainStreet.zip 真实资产逐字节逆向
  - 公开格式资料（Ogre-like mesh、Journey 衍生 TGCL）

用法：
  python3 sky_bin_parser.py <file.mesh | BstBaked.meshes | Objects.level.bin>

输出：JSON 格式的结构报告（stdout）。
仅供个人格式研究与学习，不随引擎分发游戏资产。
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
    """在 [start,end) 内按 stride 扫描节点名字表（32B 名字 + 数据）。"""
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
    # 0x40 起：num_lods / flags / 数据段表（大端小值 + half-inf 哨兵）
    r['u32_0x40_0x50'] = [u32(d, o) for o in range(0x40, 0x50, 4)]
    seg = [struct.unpack('>I', d[o:o+4])[0] for o in range(0x50, 0x60, 4)]
    r['data_seg_table'] = seg          # 如 [20388, 26805, 27889, 0x7f80(half-inf)]
    r['num_lods'] = u32(d, 0x44)
    # 姿态区 float 流起点（0x60，若 0x5c 为 half+inf 哨兵）
    if d[0x5e:0x60] == b'\x80\x7f':
        r['pose_float_start'] = 0x60
        fs = [round(f32(d, 0x60+i*12), 3) for i in range(min(8, (n-0x60)//12))]
        r['pose_float_sample'] = fs
    # 节点名字表（骨骼/子网格名，132B 条目）
    for start in (0x5002, 0x856f):
        names = scan_names(d, start, min(n, start+0x9000))
        if names:
            r.setdefault('node_tables', {})[hex(start)] = {
                'count': len(names),
                'sample': [x[1] for x in names[:8]],
            }
    # 压缩数据区识别（高熵区起点）
    # 高熵判定：>60% 字节不在 [0x20,0x7e]
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
    # tag 段
    def tag(o):
        t = d[o:o+4].decode('latin1'); v = u32(d, o+4)
        return t, v
    r['GEO0'] = {'off': u32(d, 0x10), 'val': u32(d, 0x14)}
    r['LOD0'] = {'off': u32(d, 0x1c)}
    r['METREU'] = {'off': u32(d, 0x2b) if False else None}
    # 场景 AABB（0x70-0x84 六个 float）
    aabb = [round(f32(d, 0x70+i*4), 2) for i in range(6)]
    r['scene_aabb'] = {'min': aabb[:3], 'max': aabb[3:6]}
    # 0x88 起网格块尺寸表
    sizes = [u32(d, 0x88+i*4) for i in range(6)]
    r['block_sizes_0x88'] = sizes
    # 尾部目录表（最后 64B）
    tail = [u32(d, n-64+i*4) for i in range(16)]
    r['tail_catalog'] = tail
    # 尾部索引流（u16，值 ~6000 级 → 真实三角形索引）
    idx = [struct.unpack('<H', d[o:o+2])[0] for o in range(n-0x100, n-0x64, 2)]
    r['tail_index_sample'] = idx[:20]
    r['tail_index_minmax'] = [min(idx), max(idx)]
    # 网格目录条目（与头部值一致的偏移）
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
    # 字符串表定位：第一个 >=0x10000 且 < n 的偏移（=0xf784）
    strtab = next((v for v in hdr if 0x10000 <= v < n), None)
    r['string_table_start'] = hex(strtab) if strtab else None
    if strtab:
        strs = re.findall(rb'[\x20-\x7e]{4,}', d[strtab:strtab+0x8000])
        r['string_count_sample'] = len(strs)
        r['string_sample'] = [s.decode('latin1') for s in strs[:15]]
    # Lua 脚本字符串关键词统计
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
