// LOD0 段全量解码: 记录流 -> OBJ
#include <cstdio>
#include <vector>
#include <cstdint>
#include <string>
#include "sky_bst_decoder.h"
#include "meshoptimizer.h"

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

int main(int argc,char**argv){
  if(argc<2){ printf("usage: %s <BstBaked.meshes>\n",argv[0]); return 1; }
  FILE* f=fopen(argv[1],"rb");
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> buf(sz); fread(buf.data(),1,sz,f); fclose(f);
  // TOC
  uint32_t n=rd32(buf.data()+8);
  struct Seg { std::string name; uint32_t off,len; };
  std::vector<Seg> segs;
  for(uint32_t i=0;i<n;i++){
    const uint8_t* s=buf.data()+12+i*12;
    char nm[5]={ (char)s[0],(char)s[1],(char)s[2],(char)s[3],0 };
    segs.push_back({nm,rd32(s+4),rd32(s+8)});
    printf("[%s] off=%u len=%u\n",nm,segs.back().off,segs.back().len);
  }
  for(auto& sg:segs) if(sg.name=="LOD0"){
    printf("=== LOD0 decode ===\n");
    auto recs=sky::decode_lod0(buf,sg.off,sg.len);
    printf("records: %zu\n",recs.size());
    FILE* o=fopen("lod0_all.obj","w");
    uint32_t base=1;
    for(auto& r:recs){
      fprintf(o,"o %s\n",r.name.c_str());
      for(size_t i=0;i<r.x.size();i++) fprintf(o,"v %.6f %.6f %.6f\n",r.x[i],r.y[i],r.z[i]);
      for(auto& t:r.tris) fprintf(o,"f %u %u %u\n",base+t[0],base+t[1],base+t[2]);
      base+=(uint32_t)r.x.size();
    }
    fclose(o);
    printf("obj written, total vtx=%u\n",base-1);
  }
  return 0;
}
