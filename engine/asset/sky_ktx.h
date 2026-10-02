// Sky KTX modified-header decoder
// header: magic 12B + glInternalFormat@28 + width@36 + height@40 + bytesOfKeyValueData@60
// data:offset=64+kvSize,before 4B=imageSize,its after =pixel
// supports: 0x1908/0x8058/0x8C43 RGBA8, 0x1907/0x8051 RGB8, 0x83F0 DXT1, 0x83F3 DXT5, 0x8DBB/0x8DBC BC4, 0x8E8C/0x8E8D BC7, ETC2 family
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sky {
struct KtxImage { uint32_t format=0; int w=0,h=0; std::vector<uint8_t> rgba; bool ok=false; };

inline uint32_t krd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

inline bool decode_ktx(const std::string& path, KtxImage& out){
  FILE* f=fopen(path.c_str(),"rb"); if(!f) return false;
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> d(sz); if(fread(d.data(),1,sz,f)!=(size_t)sz){fclose(f);return false;} fclose(f);
  if(sz<64 || d[0]!=0xAB || d[1]!=0x4B || d[2]!=0x54 || d[3]!=0x58) return false;
  out.format=krd32(&d[28]); out.w=(int)krd32(&d[36]); out.h=(int)krd32(&d[40]);
  uint32_t kv=krd32(&d[60]);
  size_t doff=64+kv; if(doff+4>(size_t)sz) return false;
  uint32_t isz=krd32(&d[doff]); doff+=4;
  if(doff+isz>(size_t)sz || out.w<=0 || out.h<=0 || out.w>16384 || out.h>16384) return false;
  const uint8_t* data=&d[doff];
  // currently RGBA8/RGB8 uncompressed implemented; compressed formats need extension (BC/ETC/ASTC decode via community tools)
  if(out.format==0x1908 || out.format==0x8058 || out.format==0x8C43){
    if(isz < (size_t)out.w*out.h*4) return false;
    out.rgba.assign(data, data+(size_t)out.w*out.h*4);
    out.ok=true; return true;
  }
  if(out.format==0x1907 || out.format==0x8051){
    if(isz < (size_t)out.w*out.h*3) return false;
    out.rgba.resize((size_t)out.w*out.h*4);
    for(size_t i=0;i<(size_t)out.w*out.h;i++){ out.rgba[i*4]=data[i*3]; out.rgba[i*4+1]=data[i*3+1]; out.rgba[i*4+2]=data[i*3+2]; out.rgba[i*4+3]=255; }
    out.ok=true; return true;
  }
    return false;// compressed format: BC/ETC decode pending
}
} // namespace sky
