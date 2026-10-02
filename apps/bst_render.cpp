// SkyEngine BstBaked 真实渲染：z-buffer 实心 + 高度着色（光遇暖色调）+ 主区域聚焦
#include "../engine/asset/sky_bst_decoder.h"
#include "../engine/render/png_writer.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace sky;

static const int W=1024,H=768;
static std::vector<float> zb(W*H,1e30f);
static std::vector<uint8_t> fb(W*H*3);

static bool proj(const Vec3f& eye,const Vec3f& look,const Vec3f& up,float fov,const Vec3f& p,float& sx,float& sy,float& sz){
  Vec3f f{look.x-eye.x,look.y-eye.y,look.z-eye.z};
  float fl=sqrtf(f.x*f.x+f.y*f.y+f.z*f.z); if(fl<1e-9f)return false;
  f.x/=fl;f.y/=fl;f.z/=fl;
  Vec3f r{f.y*up.z-f.z*up.y, f.z*up.x-f.x*up.z, f.x*up.y-f.y*up.x};
  float rl=sqrtf(r.x*r.x+r.y*r.y+r.z*r.z); if(rl<1e-9f)return false;
  r.x/=rl;r.y/=rl;r.z/=rl;
  Vec3f u{r.y*f.z-r.z*f.y, r.z*f.x-r.x*f.z, r.x*f.y-r.y*f.x};
  Vec3f d{p.x-eye.x,p.y-eye.y,p.z-eye.z};
  float dz=d.x*f.x+d.y*f.y+d.z*f.z; if(dz<=0.05f)return false;
  float dx=d.x*r.x+d.y*r.y+d.z*r.z;
  float dy=d.x*u.x+d.y*u.y+d.z*u.z;
  float s=H*0.5f/tanf(fov*0.5f);
  sx=W*0.5f+dx*s/dz; sy=H*0.5f-dy*s/dz; sz=dz;
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
  Vec3f mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
  for(auto& p:m.pos){ mn.x=std::min(mn.x,p.x);mn.y=std::min(mn.y,p.y);mn.z=std::min(mn.z,p.z);
                      mx.x=std::max(mx.x,p.x);mx.y=std::max(mx.y,p.y);mx.z=std::max(mx.z,p.z); }
  // 天空渐变底
  for(int y=0;y<H;y++){
    float t=(float)y/H;
    int r=(int)((0.72f+0.20f*t)*255.f), g=(int)((0.82f+0.10f*t)*255.f), b=(int)((0.92f+0.03f*t)*255.f);
    for(int x=0;x<W;x++){ size_t o=((size_t)y*W+x)*3; fb[o]=r;fb[o+1]=g;fb[o+2]=b; }
  }
  // 相机：聚焦主区域（中心 + 上倾俯视）
  Vec3f c{(mn.x+mx.x)*0.5f,(mn.y+mx.y)*0.5f,(mn.z+mx.z)*0.5f};
  float r=0; for(int i=0;i<3;i++)r=std::max(r,mx.x-mn.x); r=sqrtf(r*r*3)*0.5f;
  // 相机模式：默认斜视；argc>3: A=鸟瞰 T=近景
  Vec3f eye, look;
  if (argc>3 && argv[3][0]=='A') { eye={c.x, c.y+r*0.95f, c.z}; look={c.x,c.y,c.z}; }
  else if (argc>3 && argv[3][0]=='T') { eye={c.x+r*0.35f, c.y+r*0.10f, c.z+r*0.40f}; look={c.x,c.y,c.z}; }
  else { eye={c.x+r*0.62f, c.y+r*0.38f, c.z+r*1.05f}; look={c.x,c.y+r*0.06f,c.z}; }
  Vec3f up{0,1,0};
  if (argc>3 && argv[3][0]=='A') up={0,0,-1};
  float fov=50.f*3.14159265f/180.f;
  Vec3f ldir{0.35f,0.85f,0.4f}; // 光遇黄昏暖光
  // 高度着色：低=暖沙，高=云白
  auto hcolor=[&](float y)->int{
    float t=std::min(1.f,std::max(0.f,(y-mn.y)/((mx.y-mn.y)*0.75f)));
    int r=(int)((0.86f-0.30f*t)*255.f), g=(int)((0.76f-0.14f*t)*255.f), b=(int)((0.62f-0.10f*t)*255.f);
    return (r<<16)|(g<<8)|b;
  };
  // 距离雾
  float camdist=sqrtf((eye.x-c.x)*(eye.x-c.x)+(eye.y-c.y)*(eye.y-c.y)+(eye.z-c.z)*(eye.z-c.z));
  int drawn=0;
  for(uint32_t ci=0;ci<m.chunkCount+m.cloudChunkCount;ci++){
    uint32_t base=m.vtxStart[ci];
    uint32_t ic=m.idxCount[ci];
    for(uint32_t i=0;i+2<ic;i+=3){
      uint32_t a=base+m.idx[m.idxStart[ci]+i];
      uint32_t b=base+m.idx[m.idxStart[ci]+i+1];
      uint32_t c=base+m.idx[m.idxStart[ci]+i+2];
      if(a>=m.vertexCount||b>=m.vertexCount||c>=m.vertexCount) continue;
      Vec3f A=m.pos[a],B=m.pos[b],C=m.pos[c];
      Vec3f e1{B.x-A.x,B.y-A.y,B.z-A.z}, e2{C.x-A.x,C.y-A.y,C.z-A.z};
      Vec3f n{e1.y*e2.z-e1.z*e2.y, e1.z*e2.x-e1.x*e2.z, e1.x*e2.y-e1.y*e2.x};
      float nl=sqrtf(n.x*n.x+n.y*n.y+n.z*n.z); if(nl<1e-9f)continue;
      n.x/=nl;n.y/=nl;n.z/=nl;
      float ndl=n.x*ldir.x+n.y*ldir.y+n.z*ldir.z;
      Vec3f pc{(A.x+B.x+C.x)/3.f,(A.y+B.y+C.y)/3.f,(A.z+B.z+C.z)/3.f};
      float df=sqrtf((pc.x-eye.x)*(pc.x-eye.x)+(pc.y-eye.y)*(pc.y-eye.y)+(pc.z-eye.z)*(pc.z-eye.z));
      float fog=std::min(1.f,std::max(0.f,(df-camdist*0.5f)/(camdist*0.7f)));
      int hc=hcolor((A.y+B.y+C.y)/3.f);
      float lam=0.4f+0.6f*std::max(0.f,ndl);
      int cr=(int)(((hc>>16)&255)*lam*(1.f-fog)+0.85f*fog*255.f);
      int cg=(int)(((hc>>8)&255)*lam*(1.f-fog)+0.80f*fog*255.f);
      int cb=(int)((hc&255)*lam*(1.f-fog)+0.75f*fog*255.f);
      float s0x,s0y,s0z,s1x,s1y,s1z,s2x,s2y,s2z;
      if(!proj(eye,look,up,fov,A,s0x,s0y,s0z))continue;
      if(!proj(eye,look,up,fov,B,s1x,s1y,s1z))continue;
      if(!proj(eye,look,up,fov,C,s2x,s2y,s2z))continue;
      int minx=(int)std::max(0.f,std::min({s0x,s1x,s2x})), maxx=(int)std::min((float)W-1,std::max({s0x,s1x,s2x}));
      int miny=(int)std::max(0.f,std::min({s0y,s1y,s2y})), maxy=(int)std::min((float)H-1,std::max({s0y,s1y,s2y}));
      float area=(s1x-s0x)*(s2y-s0y)-(s1y-s0y)*(s2x-s0x);
      if(fabsf(area)<1e-9f)continue;
      if(area<0)area=-area;
      for(int y=miny;y<=maxy;y++){
        float py=y+0.5f;
        for(int x=minx;x<=maxx;x++){
          float px=x+0.5f;
          float w0=((s1x-s0x)*(py-s0y)-(s1y-s0y)*(px-s0x))/area;
          float w1=((s2x-s1x)*(py-s1y)-(s2y-s1y)*(px-s1x))/area;
          float w2=1.f-w0-w1;
          if(w0<-1e-4f||w1<-1e-4f||w2<-1e-4f)continue;
          float z=w0*s0z+w1*s1z+w2*s2z;
          size_t p=(size_t)y*W+x;
          if(z<zb[p]){ zb[p]=z; fb[p*3]=cr; fb[p*3+1]=cg; fb[p*3+2]=cb; }
        }
      }
      drawn++;
    }
  }
  printf("drawn %d triangles\n",drawn);
  if(!write_png(outp,W,H,fb.data())){printf("save fail\n");return 2;}
  printf("wrote %s\n",outp.c_str());
  return 0;
}
