#include "meshoptimizer.h"
// LOD0 record 1 full render: 57 vertices (meshopt stride=8) + 33 triangles (4B groups)
// software rasterization: cylinder projection theta-R-y, triangle fill
#include <cstdio>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstring>
static const int W=900,H=720;
static uint8_t img[H][W*3];
static float zb[H][W];
static void clr(){ memset(img,0,sizeof(img)); for(int y=0;y<H;y++)for(int x=0;x<W;x++){zb[y][x]=1e9f; img[y][x*3+0]=24;img[y][x*3+1]=26;img[y][x*3+2]=34;} }
static void px(int x,int y,float z,uint8_t r,uint8_t g,uint8_t b){ if(x<0||y<0||x>=W||y>=H)return; if(z<zb[y][x]){zb[y][x]=z;img[y][x*3]=r;img[y][x*3+1]=g;img[y][x*3+2]=b;} }
static void tri(float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,uint8_t r,uint8_t g,uint8_t b){
  // bbox
  int minx=std::max(0,(int)fminf(fminf(x0,x1),x2)); int maxx=std::min(W-1,(int)fmaxf(fmaxf(x0,x1),x2));
  int miny=std::max(0,(int)fminf(fminf(y0,y1),y2)); int maxy=std::min(H-1,(int)fmaxf(fmaxf(y0,y1),y2));
  for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++){
    float a=((x1-x0)*(y-y0)-(y1-y0)*(x-x0))/((x1-x0)*(y2-y0)-(y1-y0)*(x2-x0));
    float b=((x2-x0)*(y-y0)-(y2-y0)*(x-x0))/((x2-x0)*(y1-y0)-(y2-y0)*(x1-x0));
    float c=1-a-b; if(a<-0.01||b<-0.01||c<-0.01)continue;
    float z=a*z1+b*z2+c*z0; px(x,y,z,r,g,b);
  }
}
static void line(int x0,int y0,int x1,int y1,uint8_t r,uint8_t g,uint8_t b){
  int dx=abs(x1-x0),dy=abs(y1-y0); int sx=x0<x1?1:-1, sy=y0<y1?1:-1; int err=dx-dy;
  for(int i=0;i<2000;i++){ px(x0,y0,0,r,g,b); if(x0==x1&&y0==y1)break; int e2=2*err; if(e2>-dy){err-=dy;x0+=sx;} if(e2<dx){err+=dx;y0+=sy;} }
}
static void save(const char* p){
  FILE* f=fopen(p,"wb"); fprintf(f,"P6\n%d %d\n255\n",W,H); fwrite(img,1,sizeof(img),f); fclose(f);
}
int main(int argc,char**argv){
  FILE* f=fopen(argv[1],"rb");
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> buf(sz); fread(buf.data(),1,sz,f); fclose(f);
 // LOD0 section
  const uint8_t* lod0=buf.data()+3790208;
 const uint8_t* vstream=lod0+0x2b;// vertex stream
 const uint8_t* idx=lod0+0x14e;// index stream
 // meshopt vertex decode stride=8 bsz=291
  uint16_t vtx[57*4];
  int r=meshopt_decodeVertexBuffer(vtx,57,8,vstream,291);
  if(r!=0){ printf("meshopt r=%d\n",r); return 1; }
  // AABB
  float mn[3]={-346.1f,-13.0f,-398.3f}, mx[3]={359.2f,529.1f,372.7f};
 // vertex: u0=theta u2=R u3=y (cylinder projection)
  struct V{ float x,y,z; };
  std::vector<V> vs(57);
  for(int i=0;i<57;i++){
    float th=(vtx[i*4+0]/65536.0f)*6.2831853f;
    float R=vtx[i*4+2]/65535.0f;
    float yy=vtx[i*4+3]/65535.0f;
    vs[i].x=cosf(th)*R; vs[i].y=yy; vs[i].z=sinf(th)*R;
  }
 // 33 triangle
  int tris[33][3];
  for(int i=0;i<33;i++){
    tris[i][0]=idx[4+i*4+1]%57; tris[i][1]=idx[4+i*4+2]%57; tris[i][2]=idx[4+i*4+3]%57;
  }
  float view[2]={1.0f,0.3f};
 int mode=argc>1?atoi(argv[1]):0;// 0=bird's-eye 1=side 2=close-up
  if(mode==1){view[0]=0.85f;view[1]=-0.3f;} if(mode==2){view[0]=0.5f;view[1]=0.9f;}
  float scale=430.0f;
 for(int pass=0;pass<2;pass++){ // 0=fill 1=wireframe
    for(int t=0;t<33;t++){
      float px_[3],py_[3],pz_[3];
      for(int k=0;k<3;k++){
        V v=vs[tris[t][k]];
        px_[k]=W/2.0f+ (v.x*view[0]-v.z*view[1])*scale;
        py_[k]=H/2.0f+ (-v.y*scale + (v.x*view[1]+v.z*view[0])*scale*0.35f);
        pz_[k]=v.x*view[1]+v.z*view[0]+v.y*0.5f;
      }
      if(pass==0){
        int ci=t%3; uint8_t cr=ci==0?235:ci==1?120:70, cg=ci==0?120:ci==1?170:180, cb=ci==0?60:ci==1?200:235;
        tri(px_[0],py_[0],pz_[0],px_[1],py_[1],pz_[1],px_[2],py_[2],pz_[2],cr,cg,cb);
      } else {
        for(int k=0;k<3;k++){ int a=k,b=(k+1)%3; line((int)px_[a],(int)py_[a],(int)px_[b],(int)py_[b],255,255,255); }
      }
    }
  }
  const char* names[]={"key_top.png","key_side.png","key_near.png"};
  save(names[mode]);
 printf("saved %s | vertices 57 tris 33 meshopt_r=%d\n",names[mode],r);
 // index stream value stats
  int cnt[57]={0}; for(int i=0;i<33;i++){cnt[tris[i][0]]++;cnt[tris[i][1]]++;cnt[tris[i][2]]++;}
  int used=0; for(int i=0;i<57;i++) if(cnt[i])used++;
 printf("index stream uses vertices %d/57\n",used);
  return 0;
}
