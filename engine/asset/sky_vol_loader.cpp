// SkyEngine .vol 体积纹理加载器实现
#include "sky_vol_loader.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace sky {

bool load_vol(const uint8_t* data, size_t size, VolTexture& out){
  if(size<4096+8) return false;
  if(memcmp(data,"VOLU",4)!=0) return false;
  uint32_t ver;
  memcpy(&ver,data+4,4);
  char nm[65]; memset(nm,0,sizeof(nm));
  memcpy(nm,data+8,64);
  out.name=nm;
  size_t dsize=size-4096;
  int n=(int)roundf(powf((float)dsize,1.0f/3.0f));
  if((size_t)n*n*n!=dsize) return false;
  out.size=n;
  out.data.assign(data+4096,data+size);
  return true;
}

bool load_vol(const std::string& path, VolTexture& out){
  FILE* f=fopen(path.c_str(),"rb");
  if(!f) return false;
  fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> d((size_t)n);
  if(fread(d.data(),1,(size_t)n,f)!=(size_t)n){ fclose(f); return false; }
  fclose(f);
  return load_vol(d.data(),(size_t)n,out);
}

float vol_sample(const VolTexture& v,float x,float y,float z){
  if(v.size==0||v.data.empty()) return 0.f;
  int n=v.size;
  x-=floorf(x); y-=floorf(y); z-=floorf(z);
  float X=x*(float)n, Y=y*(float)n, Z=z*(float)n;
  int x0=(int)X%n, y0=(int)Y%n, z0=(int)Z%n;
  int x1=(x0+1)%n, y1=(y0+1)%n, z1=(z0+1)%n;
  float fx=X-(float)(int)X, fy=Y-(float)(int)Y, fz=Z-(float)(int)Z;
  auto g=[&](int a,int b,int c){ return v.data[((size_t)(c%n)*n+(b%n))*n+(a%n)]/255.f; };
  auto lerp=[&](float a,float b,float t){ return a+(b-a)*t; };
  float c00=lerp(g(x0,y0,z0),g(x1,y0,z0),fx);
  float c10=lerp(g(x0,y1,z0),g(x1,y1,z0),fx);
  float c01=lerp(g(x0,y0,z1),g(x1,y0,z1),fx);
  float c11=lerp(g(x0,y1,z1),g(x1,y1,z1),fx);
  return lerp(lerp(c00,c10,fy),lerp(c01,c11,fy),fz);
}

} // namespace sky
