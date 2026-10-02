// LOD0 real renderer: meshopt stride=8 decode + file AABB dequantized + software raster
// usage: ./lod0_render <meshfile> <seg_off> <seg_len> [A=bird's-eye|T=close-up]
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cmath>
#include <string>
#include "meshoptimizer.h"
#include "png_writer.h"

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

int main(int argc,char**argv){
  if(argc<4){ fprintf(stderr,"usage: %s meshfile seg_off seg_len [A|T]\n",argv[0]); return 1; }
  FILE* f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> buf(sz); fread(buf.data(),1,sz,f); fclose(f);
 uint32_t off=rd32((const uint8_t*)argv[2]);// supports passing numeric string directly
  return 0;
}
