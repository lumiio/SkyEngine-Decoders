// SkyEngine BstBaked.meshes decoder
// file: LVL0 magic + version + TOC(0x64) + pad + maxPos + minPos + section data
// GEO0 section: 5x u32 counts + u32 compSize + meshopt vertex stream (stride=36) + u8 indices + chunk table + subchunk table
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "meshoptimizer.h"

namespace sky {

struct Vec3f { float x,y,z; };

struct MeshData {
  std::vector<Vec3f> pos;// decoded vertices
  std::vector<uint8_t> vmat;// vertex material ID (mat[0])
 std::vector<float> uv0,uv1,uv2;// in2/in3/in4 channel(u16 little-endian→0..1)
  std::vector<uint8_t> idx;// u8 local indices
  std::vector<uint32_t> vtxStart, idxStart, subStart;
  std::vector<uint16_t> idxCount;
  std::vector<uint8_t> vtxCount, subCount;
  std::vector<Vec3f> mn, mx;   // chunk AABB
  uint32_t indexCount=0, vertexCount=0, chunkCount=0, cloudChunkCount=0, subchunkCount=0;
  bool ok=false;
};

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0]|(p[1]<<8)); }

// parse BstBaked.meshes，decode GEO0 section
bool decode_bst_baked(const std::vector<uint8_t>& buf, MeshData& out){
  if(buf.size()<140) return false;
  uint32_t magic=rd32(buf.data());
  if(magic!=0x304C564C){ printf("magic mismatch %08x\n",magic); return false; }
  uint32_t ver=rd32(buf.data()+4);
  printf("version 0x%x\n",ver);
 // TOC：0x64 byte（u32 count + n×12）
  const uint8_t* toc=buf.data()+8;
  uint32_t n=rd32(toc);
  printf("TOC segments: %u\n",n);
  struct Seg { std::string name; uint32_t off,len; };
  std::vector<Seg> segs;
  for(uint32_t i=0;i<n;i++){
    const uint8_t* s=toc+4+i*12;
    char nm[5]={ (char)s[0],(char)s[1],(char)s[2],(char)s[3],0 };
    Seg sg; sg.name=nm; sg.off=rd32(s+4); sg.len=rd32(s+8);
    segs.push_back(sg);
    printf("  [%s] off=%u len=%u\n",nm,sg.off,sg.len);
  }
  const Seg* geo=nullptr;
  for(auto& sg:segs) if(sg.name=="GEO0") geo=&sg;
  if(!geo){ printf("no GEO0\n"); return false; }
  if(geo->off+geo->len>buf.size()){ printf("GEO0 out of range\n"); return false; }
  const uint8_t* g=buf.data()+geo->off;
  uint32_t gs=geo->len;
  out.indexCount=rd32(g); out.vertexCount=rd32(g+4); out.chunkCount=rd32(g+8);
  out.cloudChunkCount=rd32(g+12); out.subchunkCount=rd32(g+16);
  printf("GEO0: idx=%u vtx=%u chunk=%u cloud=%u sub=%u (len=%u)\n",
         out.indexCount,out.vertexCount,out.chunkCount,out.cloudChunkCount,out.subchunkCount,gs);
  uint32_t p=20;
 // vertex stream
  if(out.vertexCount){
    uint32_t cs=rd32(g+p); p+=4;
    if(p+cs>geo->off+gs){ printf("vtx stream OOB cs=%u\n",cs); return false; }
    printf("vertex stream compressed=%u\n",cs);
    std::vector<uint8_t> decoded(out.vertexCount*36);
    int r=meshopt_decodeVertexBuffer(decoded.data(),out.vertexCount,36,g+p,cs);
    printf("meshopt_decodeVertexBuffer -> %d\n",r);
    if(r!=0){ printf("meshopt decode FAILED\n"); return false; }
    out.pos.resize(out.vertexCount);
    out.vmat.resize(out.vertexCount);
    out.uv0.resize(out.vertexCount*2);
    out.uv1.resize(out.vertexCount*4); // in3 RGBA8 = official vertex color 1
    out.uv2.resize(out.vertexCount*4); // in4 RGBA8 = official vertex color 2
    for(uint32_t i=0;i<out.vertexCount;i++){
      const uint8_t* v=decoded.data()+i*36;
      memcpy(&out.pos[i],v,12); // f32 xyz
 out.vmat[i]=v[16];// material[0] = material ID
    out.uv0[i*2] =(float)(v[24])/255.f;// in2.r = baked light (official RGBA8 grayscale)
      out.uv0[i*2+1]= (float)(v[25])/255.f;
      out.uv1[i*4]  = (float)(v[28])/255.f;
      out.uv1[i*4+1]= (float)(v[29])/255.f;
      out.uv1[i*4+2]= (float)(v[30])/255.f;
      out.uv1[i*4+3]= (float)(v[31])/255.f;
      out.uv2[i*4]  = (float)(v[32])/255.f;
      out.uv2[i*4+1]= (float)(v[33])/255.f;
      out.uv2[i*4+2]= (float)(v[34])/255.f;
      out.uv2[i*4+3]= (float)(v[35])/255.f;
    }
    p+=cs;
  }
 // index
  if(p+out.indexCount>gs){ printf("idx OOB\n"); return false; }
  out.idx.assign(g+p,g+p+out.indexCount); p+=out.indexCount;
  printf("idx read %u (p=%u)\n",out.indexCount,p);
  // chunk table
  uint32_t totalChunks=out.chunkCount+out.cloudChunkCount;
  printf("chunk records: %u\n",totalChunks);
  out.idxStart.resize(totalChunks); out.vtxStart.resize(totalChunks); out.subStart.resize(totalChunks);
  out.idxCount.resize(totalChunks); out.vtxCount.resize(totalChunks); out.subCount.resize(totalChunks);
  out.mn.resize(totalChunks); out.mx.resize(totalChunks);
  for(uint32_t i=0;i<totalChunks;i++){
    out.idxStart[i]=rd32(g+p); out.vtxStart[i]=rd32(g+p+4); out.subStart[i]=rd32(g+p+8);
    out.idxCount[i]=rd16(g+p+12); out.vtxCount[i]=g[p+14]; out.subCount[i]=g[p+15];
    memcpy(&out.mn[i],g+p+16,12); memcpy(&out.mx[i],g+p+28,12);
    p+=56; // 3u32+u16+u8+u8+Vec3+Vec3+4u32 = 12+4+24+16=56
    if(p>gs){ printf("chunk OOB at %u\n",i); return false; }
  }
  printf("chunks parsed ok (p=%u)\n",p);
  out.ok=true;
  return true;
}

// ================= LOD0 sectiondecode =================
// LOD0 section = record stream. Record 1 layout:
// u32 name len + name + 9B header + u32 V + u32 I + meshopt vertex stream (stride=8, 4x u16) + index stream
// vertex projection (c3 projection, render confirms ring + diamond tip):
// theta = u0/65536 x 2pi (azimuth), R = u2/65536 (radius), y = u3/65536 (height)
//   x = cosθ×R, z = sinθ×R
// index stream: 4B header + 33 x 4B triangle groups (after 3-byte mod V = vertex indices) + AABB table
struct LOD0Record {
  std::string name;
  uint32_t v=0, i=0;
 std::vector<float> x,y,z;// worldcoordinate
  std::vector<std::array<uint16_t,3>> tris;
};

static std::vector<LOD0Record> decode_lod0(const std::vector<uint8_t>& buf, uint32_t off, uint32_t len){
  std::vector<LOD0Record> out;
  const uint8_t* d=buf.data()+off;
  uint32_t p=6;// skip section header 6B
  while(p<len){
    LOD0Record rec;
    uint32_t nameLen=rd32(d+p); p+=4;
    if(nameLen>256 || p+nameLen>len) break;
    rec.name.assign((const char*)d+p, nameLen); p+=nameLen;
  // header 9B (record 1) or 12B (record 2 variant)
    uint32_t hdr=9;
    if(p+12<=len && d[p+9]==0 && d[p+10]==0 && d[p+11]==0) hdr=12;
  // locate with V/I: header immediately followed by u32 V + u32 I
    p+=hdr;
    if(p+8>len) break;
    rec.v=rd32(d+p); rec.i=rd32(d+p+4); p+=8;
 // meshopt vertex stream：stride=8,decode as 4×u16
    uint32_t vsLen = rec.v*8;
    std::vector<uint8_t> vtx(rec.v*8);
    int r=meshopt_decodeVertexBuffer(vtx.data(), rec.v, 8, d+p, vsLen);
    p+=vsLen;
    if(r!=0){
      printf("  [%s] meshopt vtx FAILED r=%d (v=%u i=%u)\n", rec.name.c_str(), r, rec.v, rec.i);
      break;
    }
    const float pi=3.14159265358979f;
    rec.x.resize(rec.v); rec.y.resize(rec.v); rec.z.resize(rec.v);
    for(uint32_t i=0;i<rec.v;i++){
      const uint8_t* vv=vtx.data()+i*8;
      uint16_t u0=rd16(vv), u1=rd16(vv+2), u2=rd16(vv+4), u3=rd16(vv+6);
      float th=u0/65536.0f*2.0f*pi;
      float R=u2/65536.0f;
      rec.x[i]=cosf(th)*R; rec.z[i]=sinf(th)*R; rec.y[i]=u3/65536.0f;
    }
  // index stream: 4B header + n x 4B triangle groups
    uint32_t idxLen = rec.i*3/2 + 4;// 33x4+4=136 first half; index stream length inferred from len
    uint32_t remain = len - p;
  // index stream = first half 136B (incl. header) + second half AABB table
    if(remain>=136){
      const uint8_t* id=d+p;
      uint32_t triGroups=(136-4)/4;
      for(uint32_t g=0;g<triGroups;g++){
        const uint8_t* gg=id+4+g*4;
        std::array<uint16_t,3> t;
        t[0]=gg[1]%rec.v; t[1]=gg[2]%rec.v; t[2]=gg[3]%rec.v;
        rec.tris.push_back(t);
      }
    }
    printf("  [%s] V=%u I=%u vtxOK tris=%zu\n", rec.name.c_str(), rec.v, rec.i, rec.tris.size());
    out.push_back(std::move(rec));
  // forward: index stream remaining length (up to next record)
    p += 136;// index stream first half (header + triangle groups)
    if(p+4>len) break;
  // second-half AABB table length = up to next record name len u32 (valid name len and p+4+len<=len)
    uint32_t probe=rd32(d+p);
    while(p<len && !(probe>0 && probe<256 && p+4+probe<=len)){
      p+=1; if(p+4>len) break; probe=rd32(d+p);
    }
  }
  return out;
}

} // namespace sky
