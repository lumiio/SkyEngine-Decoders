// SkyEngine KTX1 texture loader implementation (DXT1/DXT5 software decode)
#include "sky_ktx_loader.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace sky {

static inline uint8_t clamp_u8(int v){ return v<0?0:(v>255?255:(uint8_t)v); }

// DXT5 blockdecode（16B/block，4x4 pixel）
static void dxt5_block(const uint8_t* blk, uint8_t* out /*16 RGBA*/, bool alpha_bg){
  int a0=blk[0], a1=blk[1];
  int al[8];
  al[0]=a0; al[1]=a1;
  al[2]=(2*a0+a1)/3; al[3]=(a0+2*a1)/3;
  if(a0>a1){ al[4]=(4*a0+a1)/5; al[5]=(4*a1+a0)/5; al[6]=0; al[7]=255; }
  else     { al[4]=0; al[5]=255; }
  uint16_t c0=(uint16_t)blk[8]|((uint16_t)blk[9]<<8);
  uint16_t c1=(uint16_t)blk[10]|((uint16_t)blk[11]<<8);
  int pal[4][3];
  for(int i=0;i<2;i++){
    uint16_t v=i?c1:c0;
    pal[i][0]=((v>>11)&31)*255/31;
    pal[i][1]=((v>>5)&63)*255/63;
    pal[i][2]=(v&31)*255/31;
  }
  if(c0>c1){
    for(int k=0;k<3;k++){ pal[2][k]=(2*pal[0][k]+pal[1][k])/3; pal[3][k]=(pal[0][k]+2*pal[1][k])/3; }
  }else{
    for(int k=0;k<3;k++){ pal[2][k]=(pal[0][k]+pal[1][k])/2; pal[3][k]=0; }
  }
  uint64_t aind=0;
  for(int i=0;i<6;i++) aind|=(uint64_t)blk[2+i]<<(8*i);
  for(int j=0;j<16;j++){
    int ai=(int)((aind>>(3*j))&7);
    if(ai>7) ai=7;
    int ci=(blk[8+(j>>2)]>>((j&3)*2))&3;
    int a=al[ai];
    uint8_t* o=out+j*4;
    if(alpha_bg){
      o[0]=clamp_u8(255*(255-a)/255 + pal[ci][0]*a/255);
      o[1]=clamp_u8(255*(255-a)/255 + pal[ci][1]*a/255);
      o[2]=clamp_u8(255*(255-a)/255 + pal[ci][2]*a/255);
    }else{
      o[0]=(uint8_t)pal[ci][0]; o[1]=(uint8_t)pal[ci][1]; o[2]=(uint8_t)pal[ci][2];
    }
    o[3]=(uint8_t)a;
  }
}

// DXT1 blockdecode（8B/block）
static void dxt1_block(const uint8_t* blk, uint8_t* out, bool alpha_bg){
  uint16_t c0=(uint16_t)blk[0]|((uint16_t)blk[1]<<8);
  uint16_t c1=(uint16_t)blk[2]|((uint16_t)blk[3]<<8);
  int pal[4][3];
  for(int i=0;i<2;i++){
    uint16_t v=i?c1:c0;
    pal[i][0]=((v>>11)&31)*255/31;
    pal[i][1]=((v>>5)&63)*255/63;
    pal[i][2]=(v&31)*255/31;
  }
  if(c0>c1){
    for(int k=0;k<3;k++){ pal[2][k]=(2*pal[0][k]+pal[1][k])/3; pal[3][k]=(pal[0][k]+2*pal[1][k])/3; }
  }else{
    for(int k=0;k<3;k++){ pal[2][k]=(pal[0][k]+pal[1][k])/2; pal[3][k]=0; }
  }
  for(int j=0;j<16;j++){
    int ci=(blk[4+(j>>2)]>>((j&3)*2))&3;
    uint8_t* o=out+j*4;
    if(alpha_bg && c0<=c1 && ci==3){ o[0]=o[1]=o[2]=0; o[3]=0; }
    else{
      o[0]=(uint8_t)pal[ci][0]; o[1]=(uint8_t)pal[ci][1]; o[2]=(uint8_t)pal[ci][2];
      o[3]=255;
    }
  }
}

bool load_ktx(const uint8_t* data, size_t size, KtxTexture& out){
  if(size<0x40) return false;
  // identifier: «KTX 11»\r\n\x1a\n
  static const uint8_t KTX1_MAGIC[12]={0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};
  if(memcmp(data,KTX1_MAGIC,12)!=0) return false;
  uint32_t glType,glInternal,glBase;
  memcpy(&glType,data+0x10,4);
  memcpy(&glInternal,data+0x1c,4);
  memcpy(&glBase,data+0x20,4);
  uint32_t pw,ph,pd,faces,mips,kv;
  memcpy(&pw,data+0x24,4); memcpy(&ph,data+0x28,4); memcpy(&pd,data+0x2c,4);
  memcpy(&faces,data+0x34,4); memcpy(&mips,data+0x38,4); memcpy(&kv,data+0x3c,4);
  if(pw==0||ph==0||faces==0) return false;
  out.width=(int)pw; out.height=(int)ph; out.mip_levels=(int)mips;
  out.internal_format=glInternal;
  size_t pos=0x40+kv;
  size_t lvl_size;
  memcpy(&lvl_size,data+pos,4); pos+=4;
  if(pos+lvl_size>size) return false;
  const uint8_t* lvl=data+pos;
  bool dxt5=(glInternal==0x9279);
  bool dxt1=(glInternal==0x83f1);
    if(!dxt5 && !dxt1)return false;// only DXT1/DXT5 supported (Sky KTX all DXT5)
  int w=(int)pw, h=(int)ph;
  out.rgba.resize((size_t)w*h*4);
  int bw=(w+3)/4, bh=(h+3)/4;
  size_t blk_size=dxt5?16:8;
  for(int by=0;by<bh;by++){
    for(int bx=0;bx<bw;bx++){
      size_t bo=((size_t)by*bw+bx)*blk_size;
      uint8_t px[16][4];
      if(dxt5) dxt5_block(lvl+bo,px[0],false);
      else     dxt1_block(lvl+bo,px[0],false);
      for(int j=0;j<16;j++){
        int x=bx*4+(j&3), y=by*4+(j>>2);
        if(x<w&&y<h){
          memcpy(&out.rgba[((size_t)y*w+x)*4],px[j],4);
        }
      }
    }
  }
  return true;
}

bool load_ktx(const std::string& path, KtxTexture& out){
  FILE* f=fopen(path.c_str(),"rb");
  if(!f) return false;
  fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> d(n);
  if(fread(d.data(),1,(size_t)n,f)!=(size_t)n){ fclose(f); return false; }
  fclose(f);
  return load_ktx(d.data(),(size_t)n,out);
}

void ktx_sample(const KtxTexture& t,float u,float v,float& r,float& g,float& b,float& a){
  if(t.width==0||t.height==0){ r=g=b=a=1.f; return; }
  u=u-(float)(int)u; if(u<0)u+=1.f;
  v=v-(float)(int)v; if(v<0)v+=1.f;
  float x=u*(float)t.width-0.5f, y=v*(float)t.height-0.5f;
  int x0=(int)floorf(x), y0=(int)floorf(y);
  float fx=x-x0, fy=y-y0;
  auto get=[&](int xi,int yi)->const uint8_t*{
    xi=(xi+t.width)%t.width; yi=(yi+t.height)%t.height;
    return &t.rgba[((size_t)yi*t.width+xi)*4];
  };
  const uint8_t* p00=get(x0,y0); const uint8_t* p10=get(x0+1,y0);
  const uint8_t* p01=get(x0,y0+1); const uint8_t* p11=get(x0+1,y0+1);
  auto mix=[&](int k){
    float top=p00[k]*(1-fx)+p10[k]*fx;
    float bot=p01[k]*(1-fx)+p11[k]*fx;
    return top*(1-fy)+bot*fy;
  };
  r=mix(0)/255.f; g=mix(1)/255.f; b=mix(2)/255.f; a=mix(3)/255.f;
}

} // namespace sky
