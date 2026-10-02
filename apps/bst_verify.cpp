// 验证 BstBaked 解码器：直接解码 DayHubCave 并输出统计 + 线框渲染
#include "../engine/asset/sky_bst_decoder.h"
#include "../engine/render/png_writer.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

using namespace sky;

static int W=960,H=720;
static std::vector<uint8_t> fb(W*H*3);
static void px(int x,int y,uint8_t r,uint8_t g,uint8_t b){
  if(x<0||x>=W||y<0||y>=H)return;
  size_t o=((size_t)y*W+x)*3; fb[o]=r;fb[o+1]=g;fb[o+2]=b;
}
static void line(int x0,int y0,int x1,int y1,uint8_t r,uint8_t g,uint8_t b){
  int st=std::max(std::abs(x1-x0),std::abs(y1-y0))+1;
  for(int i=0;i<=st;i++){ float t=(float)i/st; px((int)(x0+(x1-x0)*t),(int)(y0+(y1-y0)*t),r,g,b); }
}

static bool proj(const Vec3f& eye,const Vec3f& look,const Vec3f& up,const Vec3f& p,float& sx,float& sy){
  Vec3f f{look.x-eye.x,look.y-eye.y,look.z-eye.z};
  float fl=sqrtf(f.x*f.x+f.y*f.y+f.z*f.z); if(fl<1e-9f)return false;
  f.x/=fl;f.y/=fl;f.z/=fl;
  Vec3f r{f.y*up.z-f.z*up.y, f.z*up.x-f.x*up.z, f.x*up.y-f.y*up.x};
  float rl=sqrtf(r.x*r.x+r.y*r.y+r.z*r.z); if(rl<1e-9f)return false;
  r.x/=rl;r.y/=rl;r.z/=rl;
  Vec3f u{r.y*f.z-r.z*f.y, r.z*f.x-r.x*f.z, r.x*f.y-r.y*f.x};
  Vec3f d{p.x-eye.x,p.y-eye.y,p.z-eye.z};
  float dz=d.x*f.x+d.y*f.y+d.z*f.z; if(dz<=0.1f)return false;
  float dx=d.x*r.x+d.y*r.y+d.z*r.z;
  float dy=d.x*u.x+d.y*u.y+d.z*u.z;
  float s=H*0.5f/tanf(55.f*0.5f*3.14159265f/180.f);
  sx=W*0.5f+dx*s/dz; sy=H*0.5f-dy*s/dz;
  return true;
}

int main(int argc,char** argv){
  std::string path=argc>1?argv[1]:"BstBaked.meshes";
  std::string outp=argc>2?argv[2]:"bst_render.png";
  FILE* f=fopen(path.c_str(),"rb"); if(!f){printf("open fail\n");return 1;}
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> buf(sz); if(fread(buf.data(),1,sz,f)!=(size_t)sz){printf("read fail\n");return 1;}
  fclose(f);
  MeshData m;
  if(!decode_bst_baked(buf,m)){ printf("decode FAILED\n"); return 2; }
  // 统计
  Vec3f mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
  for(auto& p:m.pos){ mn.x=std::min(mn.x,p.x);mn.y=std::min(mn.y,p.y);mn.z=std::min(mn.z,p.z);
                      mx.x=std::max(mx.x,p.x);mx.y=std::max(mx.y,p.y);mx.z=std::max(mx.z,p.z); }
  printf("decoded %u verts, %u idx, %u chunks\n",m.vertexCount,m.indexCount,m.chunkCount+m.cloudChunkCount);
  printf("AABB [%.1f,%.1f,%.1f]-[%.1f,%.1f,%.1f]\n",mn.x,mn.y,mn.z,mx.x,mx.y,mx.z);
  // 渲染：投影所有三角形（用真实位置 + 索引）
  for(size_t i=0;i<fb.size();i++)fb[i]=0xE8;
  Vec3f c{(mn.x+mx.x)*0.5f,(mn.y+mx.y)*0.5f,(mn.z+mx.z)*0.5f};
  float r=0; for(int i=0;i<3;i++)r=std::max(r,mx.x-mn.x); r=sqrtf(r*r*3)*0.5f;
  Vec3f eye{c.x+r*0.7f,c.y+r*0.4f,c.z+r*1.2f}, look=c, up{0,1,0};
  // 用全局索引绘制：把 u8 局部索引按 chunk 偏移组装
  // 简化：直接按 (chunk.idxStart + i) 索引 localIndices，再用 chunk.vtxStart 偏置
  int tri=0, drawn=0;
  for(uint32_t ci=0;ci<m.chunkCount;ci++){
    uint32_t base=m.vtxStart[ci];
    for(uint32_t i=0;i+2<m.idxCount[ci];i+=3){
      uint32_t a=base+m.idx[m.idxStart[ci]+i];
      uint32_t b=base+m.idx[m.idxStart[ci]+i+1];
      uint32_t c=base+m.idx[m.idxStart[ci]+i+2];
      tri++;
      if(a>=m.vertexCount||b>=m.vertexCount||c>=m.vertexCount) continue;
      float s0x,s0y,s1x,s1y,s2x,s2y;
      if(!proj(eye,look,up,m.pos[a],s0x,s0y))continue;
      if(!proj(eye,look,up,m.pos[b],s1x,s1y))continue;
      if(!proj(eye,look,up,m.pos[c],s2x,s2y))continue;
      uint8_t col=(uint8_t)(140+30*(ci%5));
      line((int)s0x,(int)s0y,(int)s1x,(int)s1y,col,col,255);
      line((int)s1x,(int)s1y,(int)s2x,(int)s2y,col,col,255);
      line((int)s2x,(int)s2y,(int)s0x,(int)s0y,col,col,255);
      drawn++;
    }
  }
  printf("tri total=%d drawn=%d\n",tri,drawn);
  if(!write_png(outp,W,H,fb.data())){printf("save fail\n");return 2;}
  printf("wrote %s\n",outp.c_str());
  return 0;
}
