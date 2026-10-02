// GEO0 vertex full-field dump (check whether material is used as baked vertex color)
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "meshoptimizer.h"

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

int main(int argc,char** argv){
  if(argc<2) return 1;
  FILE* f=fopen(argv[1],"rb"); if(!f) return 1;
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> d(sz); if(fread(d.data(),1,sz,f)!=(size_t)sz) return 1; fclose(f);
  if(sz<140 || rd32(d.data())!=0x304C564C){ printf("bad header\n"); return 1; }
  const uint8_t* toc=d.data()+8;
  uint32_t n=rd32(toc);
  uint32_t goff=0, glen=0;
  for(uint32_t i=0;i<n;i++){
    const uint8_t* s=toc+4+i*12;
    if(s[0]=='G'&&s[1]=='E'&&s[2]=='O'&&s[3]=='0'){ goff=rd32(s+4); glen=rd32(s+8); }
  }
  if(!glen){ printf("no GEO0\n"); return 1; }
  const uint8_t* g=d.data()+goff;
  uint32_t idxCount=rd32(g), vtxCount=rd32(g+4);
  uint32_t p=20;
  uint32_t cs=rd32(g+p); p+=4;
  std::vector<uint8_t> dec(vtxCount*36);
  int r=meshopt_decodeVertexBuffer(dec.data(),vtxCount,36,g+p,cs);
  if(r){ printf("decode fail %d\n",r); return 1; }
  printf("vtx=%u stride36 decoded ok\n",vtxCount);
 // full stats: material four-channel distribution + height correlation
  long long cnt[256]={0};
  double hsum[256]={0}, hcnt[256]={0};
  int sampN=0;
  for(uint32_t i=0;i<vtxCount && i<200000;i++){
    const uint8_t* v=dec.data()+i*36;
    float y; memcpy(&y,v+4,4);
    uint8_t m0=v[16], m1=v[17], m2=v[18], m3=v[19];
    if(i<8){
      int8_t nx=v[12],ny=v[13],nz=v[14];
      printf("v%u pos=(%.2f,%.2f,%.2f) n=(%d,%d,%d) mat=(%u,%u,%u,%u) w=(%u,%u,%u,%u) in2=(%u,%u,%u,%u) in3=(%u,%u,%u,%u) in4=(%u,%u,%u,%u)\n",
        i, *(float*)(v+0), y, *(float*)(v+8), nx,ny,nz, m0,m1,m2,m3, v[20],v[21],v[22],v[23],
        v[24],v[25],v[26],v[27], v[28],v[29],v[30],v[31], v[32],v[33],v[34],v[35]);
    }
    cnt[m0]++;
    if(m0<256){ hsum[m0]+=y; hcnt[m0]++; }
    sampN++;
  }
 printf("== mat[0] channel distribution (before 12)==\n");
  int shown=0;
  for(int k=0;k<256;k++) if(cnt[k]){ printf("  %d: %lld\n",k,cnt[k]); if(++shown>=12) break; }
 printf("== mat[0]->average height (before 12)==\n");
  shown=0;
  for(int k=0;k<256;k++) if(hcnt[k]>0){ printf("  %d: h=%.1f\n",k,hsum[k]/hcnt[k]); if(++shown>=12) break; }
  return 0;
}
