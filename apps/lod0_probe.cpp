// brute-force probe of LOD0 record vertex stream length: meshopt_decodeVertexBuffer r=0 means exact
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
  printf("LOD0 off=%u len=%u\n",lod0off,lod0len);
  const uint8_t* d=buf.data()+lod0off;
 // record 1: 6B section header + u32 name len + name 16 + 9B header + V/I
  uint32_t p=6;
  uint32_t nameLen=rd32(d+p); p+=4;
  std::string name((const char*)d+p,nameLen); p+=nameLen;
 p+=9;// header
  uint32_t V=rd32(d+p); uint32_t I=rd32(d+p+4); p+=8;
 printf("record 1: '%s' V=%u I=%u vtx@%u\n",name.c_str(),V,I,p);
  std::vector<uint8_t> dec(V*8);
  for(uint32_t bs=50;bs<1000;bs++){
    int r=meshopt_decodeVertexBuffer(dec.data(),V,8,d+p,bs);
 if(r==0){ printf(" vertex stream length hit: %u (r=0)\n",bs);p+=bs;break;}
    if(bs==291 && r!=0) printf("  bsz=291 r=%d\n",r);
  }
 // index stream = second half up to record 2 name len
 printf(" index stream@%u len=%u\n",p,lod0len-p);
 // record 2: name len
  uint32_t p2=p;
 uint32_t nl2=rd32(d+p2);printf(" record 2 name len=%u name='%s'\n",nl2,(char*)d+p2+4);
  return 0;
}
