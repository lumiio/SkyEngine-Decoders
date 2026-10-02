#include <cstdio>
#include <vector>
#include <cstdint>
#include <string>
#include "meshoptimizer.h"
static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
int main(int argc,char**argv){
  FILE* f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> buf(sz); fread(buf.data(),1,sz,f); fclose(f);
  uint32_t n=rd32(buf.data()+8);
  uint32_t lod0off=0,lod0len=0;
  for(uint32_t i=0;i<n;i++){
    const uint8_t* s=buf.data()+12+i*12;
    if(s[0]=='L'&&s[1]=='O'&&s[2]=='D'&&s[3]=='0'){ lod0off=rd32(s+4); lod0len=rd32(s+8); }
  }
  const uint8_t* d=buf.data()+lod0off;
 // record 2 data @0x2d5; meshopt vertex stream @0x36d (0xa0 start)
  uint32_t vtxpos=0x36d;
 printf("brute: vtx@%u, stride 4/8/12/16/24/32/36\n",vtxpos);
  for(uint32_t stride : {4u,8u,12u,16u,24u,32u,36u}){
    for(uint32_t V=1;V<=300;V++){
      std::vector<uint8_t> dec(V*stride);
      for(uint32_t bs=40;bs<1500 && vtxpos+bs<=lod0len;bs++){
        int r=meshopt_decodeVertexBuffer(dec.data(),V,stride,d+vtxpos,bs);
 if(r==0){ printf(" hit! stride=%u V=%u bsz=%u\n",stride,V,bs);}
      }
    }
  }
  printf("done\n");
  return 0;
}
