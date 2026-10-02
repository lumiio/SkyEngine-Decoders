// LOD0 记录1 最终渲染器: 57顶点(meshopt stride=8) + s8acc 索引进来了(全流s8累加mod57)
// 用法: lod0_key_s8 [mode 0鸟瞰/1侧视/2近景] [out.ppm]
#include <cstdio>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstring>
#include "meshoptimizer.h"
static const int W=900,H=720;
static uint8_t img[H][W*3]; static float zb[H][W];
static void px(int x,int y,float z,uint8_t r,uint8_t g,uint8_t b){ if(x<0||y<0||x>=W||y>=H)return; if(z<zb[y][x]){zb[y][x]=z;img[y][x*3]=r;img[y][x*3+1]=g;img[y][x*3+2]=b;} }
static void tri(float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,uint8_t r,uint8_t g,uint8_t b){
  int minx=std::max(0,(int)fminf(fminf(x0,x1),x2)), maxx=std::min(W-1,(int)fmaxf(fmaxf(x0,x1),x2));
  int miny=std::max(0,(int)fminf(fminf(y0,y1),y2)), maxy=std::min(H-1,(int)fmaxf(fmaxf(y0,y1),y2));
  float den=(x1-x0)*(y2-y0)-(y1-y0)*(x2-x0); if(fabsf(den)<1e-9)return;
  for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++){
    float a=((x1-x0)*(y-y0)-(y1-y0)*(x-x0))/den, b=((x2-x0)*(y-y0)-(y2-y0)*(x-x0))/den, c=1-a-b;
    if(a<-0.01||b<-0.01||c<-0.01)continue; float z=a*z1+b*z2+c*z0; px(x,y,z,r,g,b);
  }
}
static void line(int x0,int y0,int x1,int y1,uint8_t r,uint8_t g,uint8_t b){
  int dx=abs(x1-x0),dy=abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
  for(int i=0;i<2000;i++){ px(x0,y0,0,r,g,b); if(x0==x1&&y0==y1)break; int e2=2*err; if(e2>-dy){err-=dy;x0+=sx;} if(e2<dx){err+=dx;y0+=sy;} }
}
int main(int argc,char**argv){
  FILE* f=fopen(argv[1],"rb");
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> buf(sz); fread(buf.data(),1,sz,f); fclose(f);
  const uint8_t* lod0=buf.data()+3790208;
  uint16_t vtx[57*4];
  int r=meshopt_decodeVertexBuffer(vtx,57,8,lod0+0x2b,291);
  if(r!=0){printf("meshopt r=%d\n",r);return 1;}
  const uint8_t* S=lod0+0x14e; int L=362;
  int cur=0; std::vector<int> idx;
  for(int i=0;i<L;i++){ int d=S[i]; if(d>=0x80)d-=256; cur=(cur+d)%57; if(cur<0)cur+=57; idx.push_back(cur); }
  bool cv[57]={0}; for(int i:idx)cv[i]=1; int cc=0; for(int i=0;i<57;i++)cc+=cv[i];
  struct V{float x,y,z;}; std::vector<V> vs(57);
  for(int i=0;i<57;i++){ float th=(vtx[i*4+0]/65536.0f)*6.2831853f, RR=vtx[i*4+2]/65535.0f, yy=vtx[i*4+3]/65535.0f; vs[i].x=cosf(th)*RR; vs[i].y=yy; vs[i].z=sinf(th)*RR; }
  memset(img,0,sizeof(img)); for(int y=0;y<H;y++)for(int x=0;x<W;x++){zb[y][x]=1e9f;img[y][x*3]=16;img[y][x*3+1]=18;img[y][x*3+2]=26;}
  int mode=argc>1?atoi(argv[1]):0; float vx=1.0f,vz=0.3f; if(mode==1){vx=0.85f;vz=-0.3f;} if(mode==2){vx=0.5f;vz=0.9f;}
  float scale=430.0f;
  for(int pass=0;pass<2;pass++)for(size_t t=0;t+2<idx.size();t+=3){
    float px_[3],py_[3],pz_[3];
    for(int k=0;k<3;k++){ V v=vs[idx[t+k]]; px_[k]=W/2.0f+(v.x*vx-v.z*vz)*scale; py_[k]=H/2.0f+(-v.y*scale+(v.x*vz+v.z*vx)*scale*0.35f); pz_[k]=v.x*vz+v.z*vx+v.y*0.5f; }
    if(pass==0){ int ci=(t/3)%3; uint8_t cr=ci==0?230:ci==1?140:80, cg=ci==0?130:ci==1?180:190, cb=ci==0?70:ci==1?210:240; tri(px_[0],py_[0],pz_[0],px_[1],py_[1],pz_[1],px_[2],py_[2],pz_[2],cr,cg,cb); }
    else for(int k=0;k<3;k++){int a=k,b=(k+1)%3; line((int)px_[a],(int)py_[a],(int)px_[b],(int)py_[b],255,255,255);}
  }
  const char* out=argc>2?argv[2]:"lod0_key_s8.ppm";
  FILE* o=fopen(out,"wb"); fprintf(o,"P6\n%d %d\n255\n",W,H); fwrite(img,1,sizeof(img),o); fclose(o);
  printf("saved %s | meshopt r=%d idx=%zu 覆盖%d/57 三角%zu\n",out,r,idx.size(),cc,idx.size()/3);
  return 0;
}
