// skeleton animation render: per-frame bone global position + parent-child lines
#include "../engine/asset/sky_animpack_decoder.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
#include <cstdlib>
using namespace sky;
static const int W=900,H=700;
static float zb[W*H]; static uint8_t fb[W*H*3];
static void put(int x,int y,float z,int r,int g,int b){
  if(x<0||x>=W||y<0||y>=H)return;
  size_t o=(size_t)y*W+x; if(z<zb[o]){zb[o]=z;fb[o*3]=r;fb[o*3+1]=g;fb[o*3+2]=b;}
}
static void line(int x0,int y0,float z0,int x1,int y1,float z1,int r,int g,int b){
  int dx=abs(x1-x0),dy=abs(y1-y0),steps=std::max(dx,dy);
  if(steps==0){put(x0,y0,z0,r,g,b);return;}
  for(int i=0;i<=steps;i++){ float t=(float)i/steps;
    put(x0+(int)round(t*(x1-x0)),y0+(int)round(t*(y1-y0)),z0+(z1-z0)*t,r,g,b); }
}
static void matmul(float* a,float* b,float* out){ // 4x4 a×b
  for(int r=0;r<4;r++)for(int c=0;c<4;c++){ float s=0; for(int k=0;k<4;k++)s+=a[r*4+k]*b[k*4+c]; out[r*4+c]=s; }
}
static void sqt_to_mat(const AnimSQT& sq, float* m){
  float x=sq.q[0],y=sq.q[1],z=sq.q[2],w=sq.q[3];
  m[0]=1-2*(y*y+z*z); m[1]=2*(x*y-w*z);   m[2]=2*(x*z+w*y);   m[3]=0;
  m[4]=2*(x*y+w*z);   m[5]=1-2*(x*x+z*z); m[6]=2*(y*z-w*x);   m[7]=0;
  m[8]=2*(x*z-w*y);   m[9]=2*(y*z+w*x);   m[10]=1-2*(x*x+y*y);m[11]=0;
  m[12]=sq.t[0]; m[13]=sq.t[1]; m[14]=sq.t[2]; m[15]=1;
 for(int i=0;i<3;i++){ for(int j=0;j<3;j++)m[j*4+i]*=sq.s[i];} // scale column
}
int main(int argc,char**argv){
  if(argc<2){printf("usage: %s animpack [frame]\n",argv[0]);return 1;}
  AnimPackDecoded ap;
  if(!decode_animpack(argv[1],ap)){printf("decode fail\n");return 2;}
 printf("decoded:%s bones=%u sets=%u frames=%u..%u(%uframe)data=%zu\n",
    ap.name.c_str(),ap.boneCount,ap.clip.sets.size(),ap.minFrame,ap.maxFrame,ap.frameCount,ap.frameData.size()/10);
  int frame=argc>2?atoi(argv[2]):0;
  printf("render start frame=%d\n",frame); fflush(stdout);
 // per-frame global matrix
  std::vector<float> gmat(ap.boneCount*16);
  std::vector<float> pos(ap.boneCount*3);
  for(uint32_t bi=0;bi<ap.boneCount;bi++){
    AnimSQT sq;
    const float* fd=&ap.frameData[((size_t)bi*ap.frameCount+frame)*10];
    if(std::isfinite(fd[0])||std::isfinite(fd[3])||std::isfinite(fd[7])){
      for(int j=0;j<3;j++)sq.s[j]=std::isfinite(fd[j])?fd[j]:(bi<ap.clip.baseSqt.size()?ap.clip.baseSqt[bi].s[j]:1.f);
      for(int j=0;j<4;j++)sq.q[j]=std::isfinite(fd[3+j])?fd[3+j]:(bi<ap.clip.baseSqt.size()?ap.clip.baseSqt[bi].q[j]:(j==3?1.f:0.f));
      for(int j=0;j<3;j++)sq.t[j]=std::isfinite(fd[7+j])?fd[7+j]:(bi<ap.clip.baseSqt.size()?ap.clip.baseSqt[bi].t[j]:0.f);
    } else if(bi<ap.clip.baseSqt.size()){ sq=ap.clip.baseSqt[bi]; }
    else { sq.s[0]=sq.s[1]=sq.s[2]=1; sq.q[0]=sq.q[1]=sq.q[2]=0; sq.q[3]=1; sq.t[0]=sq.t[1]=sq.t[2]=0; }
    float lm[16]; sqt_to_mat(sq,lm);
    float* gm=&gmat[bi*16];
    if(ap.bones[bi].parent>=0){
      float* pm=&gmat[ap.bones[bi].parent*16];
      matmul(pm,lm,gm);
    } else { memcpy(gm,lm,64); }
    pos[bi*3]=gm[12]; pos[bi*3+1]=gm[13]; pos[bi*3+2]=gm[14];
  }
  printf("gmat done pos0=%.3f,%.3f,%.3f pos50=%.3f,%.3f,%.3f\n",pos[0],pos[1],pos[2],pos[150],pos[151],pos[152]); fflush(stdout);
  // AABB
  float mnx=1e9,mny=1e9,mnz=1e9,mxx=-1e9,mxy=-1e9,mxz=-1e9;
  for(uint32_t bi=0;bi<ap.boneCount;bi++){ float x=pos[bi*3],y=pos[bi*3+1],z=pos[bi*3+2];
    mnx=fmin(mnx,x);mny=fmin(mny,y);mnz=fmin(mnz,z);mxx=fmax(mxx,x);mxy=fmax(mxy,y);mxz=fmax(mxz,z);}
  float cx=(mnx+mxx)/2,cy=(mny+mxy)/2,cz=(mnz+mxz)/2;
  printf("aabb mn=%.3f,%.3f,%.3f mx=%.3f,%.3f,%.3f\n",mnx,mny,mnz,mxx,mxy,mxz); fflush(stdout);
  float scl=fmin((W-80)/fmax(mxx-mnx,1e-6f),(H-80)/fmax(mxy-mny,1e-6f));
  if(scl>400)scl=400;
  for(int y=0;y<H;y++){ float t=(float)y/H;
    for(int x=0;x<W;x++){ size_t o=((size_t)y*W+x)*3;
      fb[o]=(int)((0.15+0.2*t)*255);fb[o+1]=(int)((0.18+0.15*t)*255);fb[o+2]=(int)((0.25+0.1*t)*255); zb[(size_t)y*W+x]=1e30f; } }
  printf("aabb done scl=%f\n",scl); fflush(stdout);
 // parent-child lines
  for(uint32_t bi=0;bi<ap.boneCount;bi++){
    int p=ap.bones[bi].parent; if(p<0)continue;
    float x0=(pos[bi*3]-cx)*scl+W/2, y0=H/2-(pos[bi*3+1]-cy)*scl;
    float x1=(pos[p*3]-cx)*scl+W/2, y1=H/2-(pos[p*3+1]-cy)*scl;
    float z0=0.5f+(pos[bi*3+2]-mnz)/(mxz-mnz+1e-6f)*0.5f;
    float z1=0.5f+(pos[p*3+2]-mnz)/(mxz-mnz+1e-6f)*0.5f;
    line((int)x0,(int)y0,z0,(int)x1,(int)y1,z1,120,200,255);
  }
 // bone points
  for(uint32_t bi=0;bi<ap.boneCount;bi++){
    float x=(pos[bi*3]-cx)*scl+W/2, y=H/2-(pos[bi*3+1]-cy)*scl;
    float z=0.5f+(pos[bi*3+2]-mnz)/(mxz-mnz+1e-6f)*0.5f;
    for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++) if(dx*dx+dy*dy<=5)
      put((int)x+dx,(int)y+dy,z,255,140,80);
  }
  char out[256]; snprintf(out,256,"/tmp/anim_f%02d.png",frame);
 // save PPM
  char pp[256]; snprintf(pp,256,"/tmp/anim_f%02d.ppm",frame);
  FILE* pf=fopen(pp,"wb"); fprintf(pf,"P6\n%d %d\n255\n",W,H); fwrite(fb,1,W*H*3,pf); fclose(pf);
  printf("wrote %s\n",pp);
  return 0;
}
