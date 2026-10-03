// SkyEngine BstBaked.meshes 解码器
// 文件：LVL0 magic + version + TOC(0x64) + pad + maxPos + minPos + 段数据
// GEO0 段：5×u32 计数 + u32 compSize + meshopt 顶点流(stride=36) + u8 索引 + chunk 表 + subchunk 表
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
  std::vector<Vec3f> pos;      // 解码顶点
  std::vector<uint8_t> vmat;   // 顶点材质 ID (mat[0])
  std::vector<float> uv0, uv1, uv2; // in2/in3/in4 通道(u16 小端→0..1)
  std::vector<uint8_t> idx;    // u8 局部索引
  std::vector<uint32_t> vtxStart, idxStart, subStart;
  std::vector<uint16_t> idxCount;
  std::vector<uint8_t> vtxCount, subCount;
  std::vector<Vec3f> mn, mx;   // chunk AABB
  uint32_t indexCount=0, vertexCount=0, chunkCount=0, cloudChunkCount=0, subchunkCount=0;
  bool ok=false;
};

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0]|(p[1]<<8)); }

// 解析 BstBaked.meshes，解码 GEO0 段
bool decode_bst_baked(const std::vector<uint8_t>& buf, MeshData& out){
  if(buf.size()<140) return false;
  uint32_t magic=rd32(buf.data());
  if(magic!=0x304C564C){ printf("magic mismatch %08x\n",magic); return false; }
  uint32_t ver=rd32(buf.data()+4);
  printf("version 0x%x\n",ver);
  // TOC：0x64 字节（u32 count + n×12）
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
  // 顶点流
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
    out.uv1.resize(out.vertexCount*2);
    out.uv2.resize(out.vertexCount*2);
    for(uint32_t i=0;i<out.vertexCount;i++){
      const uint8_t* v=decoded.data()+i*36;
      memcpy(&out.pos[i],v,12); // f32 xyz
      out.vmat[i]=v[16];       // material[0] = 材质 ID
      out.uv0[i*2]  = (float)(v[24])/255.f;   // in2.r = 烘焙光照（官方 RGBA8 灰度）
      out.uv0[i*2+1]= (float)(v[25])/255.f;
      out.uv1.resize(out.vertexCount*4);      // in3 RGBA8 = 官方颜色1
      out.uv2.resize(out.vertexCount*4);      // in4 RGBA8 = 官方颜色2
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
  // 索引
  if(p+out.indexCount>gs){ printf("idx OOB\n"); return false; }
  out.idx.assign(g+p,g+p+out.indexCount); p+=out.indexCount;
  printf("idx read %u (p=%u)\n",out.indexCount,p);
  // chunk 表
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

// ================= LOD0 段解码 =================
// LOD0 段 = 记录流。记录1 布局：
//   u32 名长 + 名字 + 9B 头 + u32 V + u32 I + meshopt 顶点流(stride=8, 4×u16) + 索引进来了
// 顶点投影（c3 投影，渲染确证环+钻石尖）：
//   θ = u0/65536×2π（方位角）, R = u2/65536（半径）, y = u3/65536（高度）
//   x = cosθ×R, z = sinθ×R
// 索引进来了：4B 头 + 33×4B 三角形组（后 3 字节 mod V = 顶点索引） + AABB 表
struct LOD0Record {
  std::string name;
  uint32_t v=0, i=0;
  std::vector<float> x, y, z;          // 世界坐标
  std::vector<std::array<uint16_t,3>> tris;
};

static std::vector<LOD0Record> decode_lod0(const std::vector<uint8_t>& buf, uint32_t off, uint32_t len){
  std::vector<LOD0Record> out;
  const uint8_t* d=buf.data()+off;
  uint32_t p=6; // 跳过段头 6B
  while(p<len){
    LOD0Record rec;
    uint32_t nameLen=rd32(d+p); p+=4;
    if(nameLen>256 || p+nameLen>len) break;
    rec.name.assign((const char*)d+p, nameLen); p+=nameLen;
    // 头 9B（记录1）或 12B（记录2 变体）
    uint32_t hdr=9;
    if(p+12<=len && d[p+9]==0 && d[p+10]==0 && d[p+11]==0) hdr=12;
    // 用 V/I 定位：头后紧跟 u32 V + u32 I
    p+=hdr;
    if(p+8>len) break;
    rec.v=rd32(d+p); rec.i=rd32(d+p+4); p+=8;
    // meshopt 顶点流：stride=8, 解码为 4×u16
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
    // 索引进来了：4B 头 + n×4B 三角形组
    uint32_t idxLen = rec.i*3/2 + 4; // 33×4+4=136 前半；索引进来了长由 len 推断
    uint32_t remain = len - p;
    // 索引进来了 = 前半 136B（含头）+ 后半 AABB 表
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
    // 前进：索引进来了剩余长度（到下一记录）
    p += 136; // 索引进来了前半（头+三角形组）
    if(p+4>len) break;
    // 后半 AABB 表长度 = 直到下一个名长 u32（前 4B 为有效名长且 p+4+名长<=len）
    uint32_t probe=rd32(d+p);
    while(p<len && !(probe>0 && probe<256 && p+4+probe<=len)){
      p+=1; if(p+4>len) break; probe=rd32(d+p);
    }
  }
  return out;
}

} // namespace sky
