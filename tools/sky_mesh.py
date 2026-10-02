#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sky_mesh.py — Sky: Children of the Light (.mesh) 二进制格式解析器

格式依据：
  * 对 1903 个 .mesh 样本的字节级统计验证

格式总览（小端）:
  [0x00] u32  name_len
  [0x04] char name[name_len]
  [0x44] 数据区
     mesh 基础型（0x48==0，已实测锁定）:
       0x44 u32 num_lods(=1)
       0x48 u32 variant_flags(0=基础, 1=Strip* 变体, 0x100=NightShelf 类)
       0x4C u32 0x00010000
       0x50 u32 (u16 0 + u16 段大小)
       0x56 u32 uncompressed_size
       0x5A LZ4_block 压缩数据 [-> EOF]
     解压后:
       0x00 u32 0x7F800000 哨兵(+inf)
       0x04 float[6] AABB(min,max)         0x1C 处第二份
       0x34 float[16] 固定属性流(UV 采样/通道信息)
       0x74 u32 shared_vertex_count
       0x78 u32 total_vertex_count (=三角形×3)
       0x7C u32 0
       0x80 u32 point_count
       0x84 u32 uv_count
       0x88/0x8C/0x90 u32 0
       0x94 u32 0x00010001
       0x98..0xB2 padding(0)
       0xB3 顶点缓冲: shared × 16B = (f32 x,y,z, 哨兵 0x008000FF)
       顶点后: 法线 shared × 4B = (s8 nx,ny,nz + pad)
       法线后: UV uv_count × 4B = (half u, half v)
       索引区: 从 UV 区后滑动定位 total//3 个 (u16×3)，值 < shared
"""
import lz4.block, struct, sys, os

class SkyMesh:
    def __init__(self, name, shared, total, point, uv, aabb, verts, normals, uvs, tris,
                 variant_flags=0, idx_offset=None, nrm_offset=None, uv_offset=None):
        self.name=name; self.shared=shared; self.total=total; self.point=point; self.uv=uv
        self.aabb=aabb; self.verts=verts; self.normals=normals
        self.uvs=uvs; self.tris=tris; self.variant_flags=variant_flags
        self.idx_offset=idx_offset; self.nrm_offset=nrm_offset; self.uv_offset=uv_offset
    def summary(self):
        return (f"{self.name}: V={self.shared} T={self.total//3} uv={self.uv} "
                f"aabb=({self.aabb[0]:.2f},{self.aabb[1]:.2f},{self.aabb[2]:.2f}).."
                f"({self.aabb[3]:.2f},{self.aabb[4]:.2f},{self.aabb[5]:.2f})")
    def to_obj(self):
        out=[f"o {self.name}"]
        for v in self.verts: out.append(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}")
        if self.normals:
            for n in self.normals: out.append(f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}")
        for u in self.uvs: out.append(f"vt {u[0]:.6f} {u[1]:.6f}")
        for i,t in enumerate(self.tris): out.append(f"f {t[0]+1} {t[1]+1} {t[2]+1}")
        return "\n".join(out)

def h2f(h):
    s=h; e=(s>>10)&31; f=s&1023
    if e==0: return (f/1024.0)*(2**-14)*(-1 if s&0x8000 else 1)
    if e==31: return float('nan')
    return (1+f/1024.0)*(2**(e-15))*(-1 if s&0x8000 else 1)

def _find_index(buf, shared, tris, start):
    """先找三角形列表（3×u16/tri），再找三角形条带（strip: 序列 v0..vn，每新顶点成 tri 并交替绕序）"""
    L=len(buf)
    for off in range(start, L-tris*6, 2):
        ok=True
        for t in range(tris):
            if off+t*6+6>L: ok=False; break
            a,b,c=struct.unpack_from('<HHH',buf,off+t*6)
            if a>=shared or b>=shared or c>=shared: ok=False; break
        if ok: return ('list', off)
    # 条带: 需要 tris+2 个顶点值（开环），或 tris+1（退化头）
    need=tris+3
    for off in range(start, L-need*2, 2):
        seq=struct.unpack_from('<%dH'%need,buf,off)
        if any(v>=shared for v in seq): continue
        # 解码条带三角形（绕序交替）
        ok=True
        for i in range(tris):
            a,b,c=seq[i],seq[i+1],seq[i+2]
            if a==b or b==c or a==c: ok=False; break
        if ok: return ('strip', off)
    return None

def _decode_strip(seq, tris):
    out=[]
    for i in range(tris):
        if i%2==0: out.append((seq[i],seq[i+1],seq[i+2]))
        else:      out.append((seq[i+1],seq[i],seq[i+2]))  # 保持绕序一致
    return out

def read_mesh(path):
    d=open(path,'rb').read()
    name_len=struct.unpack('<I',d[0:4])[0]
    name=d[4:4+name_len].split(b'\0')[0].decode(errors='replace')
    flags=struct.unpack('<I',d[0x48:0x4c])[0]
    usize=struct.unpack('<I',d[0x56:0x5a])[0]
    if flags!=0:
        raise NotImplementedError(f"variant flags={flags:#x} 暂未支持: {name}")
    buf=lz4.block.decompress(d[0x5a:], uncompressed_size=usize)
    shared=struct.unpack('<I',buf[0x74:0x78])[0]
    total=struct.unpack('<I',buf[0x78:0x7c])[0]
    point=struct.unpack('<I',buf[0x80:0x84])[0]
    uv_n=struct.unpack('<I',buf[0x84:0x88])[0]
    aabb=struct.unpack_from('<6f',buf,0x04)
    # 顶点: 16B stride (x,y,z + 0x008000FF 哨兵)
    vs=0xb3
    verts=[]; nan=0
    for i in range(shared):
        x,y,z,s=struct.unpack_from('<4f',buf,vs+i*16)
        if x!=x or y!=y or z!=z: nan+=1
        verts.append((x,y,z))
    # 法线: shared × 4B (s8×3 + pad)
    noff=vs+shared*16
    normals=[]
    for i in range(shared):
        nx,ny,nz,pad=struct.unpack_from('<4b',buf,noff+i*4)
        normals.append((nx/127.0, ny/127.0, nz/127.0))
    # UV: uv_n × 4B (half×2)
    uvo=noff+shared*4
    uvs=[]; bad=0
    for i in range(uv_n):
        if uvo+i*4+4>len(buf): break
        a,b=struct.unpack_from('<HH',buf,uvo+i*4)
        u,v=h2f(a),h2f(b)
        if u!=u or v!=v or abs(u)>1e4 or abs(v)>1e4: bad+=1
        uvs.append((u,v))
    # 索引: UV 区后滑动（列表或条带）
    tris=[]
    kind,toff=_find_index(buf, shared, total//3, uvo+uv_n*4)
    if toff is None:
        raise ValueError(f"index buffer not found: {name}")
    if kind=='strip':
        need=total//3+3
        seq=struct.unpack_from('<%dH'%need,buf,toff)
        tris=_decode_strip(seq,total//3)
    else:
        for t in range(total//3):
            a,b,c=struct.unpack_from('<HHH',buf,toff+t*6)
            tris.append((a,b,c))
    return SkyMesh(name, shared, total, point, uv_n, aabb, verts, normals, uvs, tris,
                   flags, toff, noff, uvo)

def main():
    if len(sys.argv)<2:
        print("usage: sky_mesh.py <file.mesh> [out.obj]"); return 1
    m=read_mesh(sys.argv[1])
    print(m.summary())
    if len(sys.argv)>2:
        open(sys.argv[2],'w').write(m.to_obj())
        print(f"OBJ -> {sys.argv[2]}")
    return 0

if __name__=='__main__':
    sys.exit(main())
