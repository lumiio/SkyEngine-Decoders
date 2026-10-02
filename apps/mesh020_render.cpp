// Sky.mesh v0x20 parserenderer（independentimplementation）
// usage:mesh020_render <file.mesh> [out.ppm] [tex.ktx] [anim.animpack] [frame]
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <lz4.h>
#include "../engine/asset/sky_animpack_decoder.h"

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static float rdf(const uint8_t* p){ uint32_t u=rd32(p); float f; memcpy(&f,&u,4); return f; }
static float half(uint16_t h){ uint16_t s=h&0x8000?1:0; int e=(h>>10)&0x1f; int m=h&0x3ff;
  if(e==0) return (s?-1.f:1.f)*(m/1024.0f)*std::pow(2.0f,-14);
  if(e==31) return m?NAN:(s?-INFINITY:INFINITY);
  return (s?-1.f:1.f)*(1.0f+m/1024.0f)*std::pow(2.0f,(float)(e-15)); }

struct MeshFile {
  uint32_t uniqueVtx=0, vtxTotal=0, triCount=0; bool wideIdx=false;
  bool animated=false, hasNormals=false, hasExtraIndexData=false;
  uint32_t posCompressedFlag=0, uvCompressedFlag=0, reservedFlag3=0;
  std::vector<float> pos, norm, uv0;
  struct Bone { std::string name; int parent; float mat[16]; };
  std::vector<Bone> bones;
  std::vector<std::vector<std::pair<int,float>>> skin; // bone,weight
  std::vector<uint32_t> idx;
  float aabbMin[3], aabbMax[3];
  bool ok=false;
};

static bool decodeMesh020(const std::string& path, MeshFile& m, std::string& err){
  FILE* f=fopen(path.c_str(),"rb"); if(!f){err="open fail";return false;}
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> raw(sz); if(fread(raw.data(),1,sz,f)!=(size_t)sz){fclose(f);err="read";return false;} fclose(f);
  if(sz<0x58){err="too small";return false;}
  m.animated = raw[0x48]!=0;
  // payload region: 0x4e holds the compressed block descriptor (isCompressed + csz + usz), followed by data
  const int payloadBase = 0x4e;
  int compressedFlag = (int)rd32(&raw[payloadBase]);
  uint32_t csz=rd32(&raw[payloadBase+4]), usz=rd32(&raw[payloadBase+8]);
  if(csz==0 || usz==0 || payloadBase+12+csz>(size_t)sz){err="bad bounds";return false;}
  const uint8_t* src=&raw[payloadBase+12];
  std::vector<uint8_t> dec;
  const uint8_t* d;
  if(compressedFlag!=0){ dec.resize(usz);
    int r=LZ4_decompress_safe((const char*)src,(char*)dec.data(),(int)csz,(int)usz);
    if(r<=0){ err="lz4 fail"; return false; } d=dec.data(); }
  else d=src;
  const uint8_t* bb=d;
  int p=4;
  auto rdv3=[&](int o,float* v){ v[0]=rdf(bb+o);v[1]=rdf(bb+o+4);v[2]=rdf(bb+o+8); };
  // dual AABB + quantization range (used by position/UV compression)
  float aabbA[3],aabbB[3],aabbA2[3],aabbB2[3];
  rdv3(p,aabbA); p+=12; rdv3(p,aabbB); p+=12; rdv3(p,aabbA2); p+=12; rdv3(p,aabbB2); p+=12;
  float quantMin[8],quantMax[8];
  for(int i=0;i<8;i++)quantMin[i]=rdf(bb+p+i*4); p+=32;
  for(int i=0;i<8;i++)quantMax[i]=rdf(bb+p+i*4); p+=32;
  m.uniqueVtx=rd32(bb+p); p+=4;
  m.vtxTotal=rd32(bb+p); p+=4;
  m.wideIdx=rd32(bb+p)!=0; p+=4;
  // next five int fields have unlabeled semantics; preserved in occurrence order
  uint32_t extraFieldA=rd32(bb+p); p+=4;
  uint32_t extraFieldB=rd32(bb+p); p+=4;
  uint32_t extraFieldC=rd32(bb+p); p+=4;
  uint32_t extraFieldD=rd32(bb+p); p+=4;
  uint32_t extraFieldE=rd32(bb+p); p+=4;
  m.hasNormals=bb[p]!=0; p+=1;
  m.hasExtraIndexData=bb[p]!=0; p+=1;
  p+=1;
  m.posCompressedFlag=rd32(bb+p); p+=4;
  m.uvCompressedFlag=rd32(bb+p); p+=4;
  m.reservedFlag3=rd32(bb+p); p+=4;
  p+=0x10;
  m.triCount=m.vtxTotal/3;
  int idxUnit=m.wideIdx?4:2;
  std::vector<float> verts; verts.reserve(m.uniqueVtx*3);
  // vertex stream (uncompressed): 16 bytes per vertex (xyz f32 + reserved)
  if(m.posCompressedFlag==0){
    for(uint32_t i=0;i<m.uniqueVtx;i++){
      int off=p+i*16;
      verts.push_back(rdf(bb+off)); verts.push_back(rdf(bb+off+4)); verts.push_back(rdf(bb+off+8));
    }
    p+=m.uniqueVtx*16;
  }
  if(m.hasNormals) p+=m.uniqueVtx*4;
  // UV stream (uncompressed): half-float u/v
  if(m.uvCompressedFlag==0){
    for(uint32_t i=0;i<m.uniqueVtx;i++){
      int base=p+i*16;
      m.uv0.push_back(half(rd16(bb+base))); m.uv0.push_back(half(rd16(bb+base+2)));
    }
    p+=m.uniqueVtx*16;
  }
  // skinning: per vertex 4x (bone index, weight) 1 byte each
  if(m.animated){
    for(uint32_t i=0;i<m.uniqueVtx;i++){
      int off=p+i*8;
      std::vector<std::pair<int,float>> row;
      for(int j=0;j<4;j++){
        int bi=bb[off+j]&0xff, wi=bb[off+4+j]&0xff;
        if(bi>0&&wi>0) row.push_back({bi-1, wi/255.0f});
      }
      m.skin.push_back(row);
    }
    p+=m.uniqueVtx*8;
  }
 // triangleindex
  for(uint32_t i=0;i<m.triCount;i++){
    uint32_t a,b,c;
    if(m.wideIdx){ a=rd32(bb+p);b=rd32(bb+p+4);c=rd32(bb+p+8); p+=12; }
    else { a=rd16(bb+p);b=rd16(bb+p+2);c=rd16(bb+p+4); p+=6; }
    m.idx.push_back(a); m.idx.push_back(b); m.idx.push_back(c);
  }
  // trailing streams: skipped by int fields and index unit
  if(m.hasExtraIndexData) p+=m.vtxTotal*idxUnit;
  if(extraFieldA>0) p+=m.uniqueVtx*idxUnit;
  if(extraFieldB>0) p+=m.uniqueVtx*idxUnit;
  if(extraFieldC>0) p+=(int)extraFieldC*idxUnit;
  if(extraFieldD>0) p+=(int)extraFieldD*4;
  if(extraFieldE>0) p+=(int)extraFieldE*(m.wideIdx?8:4);
  p+=(int)m.triCount*4;
  // vertex stream (quantized): 32-bit packed 3x10bit, dequantized by aabbA2/B2
  if(m.posCompressedFlag>0){
    float ax=aabbA2[0],ay=aabbA2[1],az=aabbA2[2];
    float sx=aabbB2[0]-ax,sy=aabbB2[1]-ay,sz=aabbB2[2]-az;
    for(uint32_t i=0;i<m.uniqueVtx;i++){
      uint32_t packed=rd32(bb+p+i*4);
      int qz=(int)(packed&0x3ff), qy=(int)((packed>>10)&0x3ff), qx=(int)((packed>>20)&0x3ff);
      verts.push_back(ax+(qx/1023.0f)*sx); verts.push_back(ay+(qy/1023.0f)*sy); verts.push_back(az+(qz/1023.0f)*sz);
    }
    p+=m.uniqueVtx*4 + m.uniqueVtx;
  }
  // UV stream (quantized): 4B (hi8/lo8 component pairs), dequantized by quantMin/Max
  if(m.uvCompressedFlag>0){
    float uvMinU=quantMin[0],uvMinV=quantMin[1],uvSizeU=quantMax[0]-uvMinU,uvSizeV=quantMax[1]-uvMinV;
    for(uint32_t i=0;i<m.uniqueVtx;i++){
      int off=p+i*4;
      int uHi=bb[off]&0xff,vHi=bb[off+1]&0xff,uLo=bb[off+2]&0xff,vLo=bb[off+3]&0xff;
      float u=uvMinU+(((uHi<<8)|uLo)/65535.0f)*uvSizeU;
      float v=uvMinV+(((vHi<<8)|vLo)/65535.0f)*uvSizeV;
      m.uv0.push_back(u); m.uv0.push_back(v);
    }
    p+=m.uniqueVtx*4;
  }
  m.pos=verts;
  m.aabbMin[0]=aabbA[0];m.aabbMin[1]=aabbA[1];m.aabbMin[2]=aabbA[2];
  m.aabbMax[0]=aabbB[0];m.aabbMax[1]=aabbB[1];m.aabbMax[2]=aabbB[2];
  // embedded bone section in trailer: marker + section name + [boneCount][0][nameField] + 132B x boneCount records
  if(m.animated && sz-(payloadBase+12+csz) >= 85){
    const uint8_t* s=&raw[payloadBase+12+csz]; size_t slen=sz-(payloadBase+12+csz);
    int mk=(int)rd32(s);
    if(mk==11||mk==9||mk==0x1c){
    size_t o=4; while(o<slen && s[o]!=0) o++; o++; // section name
      int bc=0; size_t bco=(size_t)-1;
      size_t lim=std::min(o+0x100, slen-12);
      for(size_t sc=o;sc<lim;sc++){
        int v1=(int)rd32(s+sc); int v2=(int)rd32(s+sc+4); int v3=(int)rd32(s+sc+8);
        if(v1>=1&&v1<=500&&v2==0&&v3>=20&&v3<=64){bc=v1;bco=sc;break;}
      }
      if(bc>0&&bco!=(size_t)-1){
 size_t bs=bco+12+5;// boneCount+0+nameField + 1Bflag + 4B hash
        for(int i=0;i<bc;i++){
          size_t bo=bs+i*132;
          if(bo+132>slen) break;
          char nm[65]; memcpy(nm,s+bo,64); nm[64]=0;
          std::string name(nm); size_t e=name.find('\0'); if(e!=std::string::npos)name=name.substr(0,e);
          float mat[16]; for(int j=0;j<16;j++) mat[j]=rdf(s+bo+64+j*4);
          int pi=(int)rd32(s+bo+128); if(pi>0) pi=pi-1; else pi=-1;
          m.bones.push_back({name,pi,{mat[0],mat[1],mat[2],mat[3],mat[4],mat[5],mat[6],mat[7],mat[8],mat[9],mat[10],mat[11],mat[12],mat[13],mat[14],mat[15]}});
        }
      }
    }
  }
  m.ok=true; return true;
}

// ---------- render ----------
static void render(const MeshFile& m, const char* out, const char* texPath=nullptr, const float* animWorld=nullptr){
  int W=900,H=700;
  // KTX texture (Sky-modified header: fmt@28 w@36 h@40 kv@60 data@64+kv+4)
  std::vector<uint8_t> tex; int tw=0,th=0;
  if(texPath){
    FILE* tf=fopen(texPath,"rb"); if(tf){
      fseek(tf,0,SEEK_END); long tz=ftell(tf); fseek(tf,0,SEEK_SET);
      std::vector<uint8_t> td(tz); fread(td.data(),1,tz,tf); fclose(tf);
      if(tz>64){ tw=(int)rd32(&td[36]); th=(int)rd32(&td[40]);
        int kv=(int)rd32(&td[60]); size_t doff=64+kv;
        if(doff+4<=(size_t)tz){ int isz=(int)rd32(&td[doff]);
          if(doff+4+isz<=tz){ tex.assign(td.begin()+doff+4, td.begin()+doff+4+isz); } } }
      printf("tex %s: %dx%d bytes=%zu\n",texPath,tw,th,tex.size());
    }
  }
  auto sample=[&](float u,float v,int* px,int* py){
    int x=(int)(u*tw); if(x<0)x=0; if(x>=tw)x=tw-1;
    int y=(int)((1.0f-v)*th); if(y<0)y=0; if(y>=th)y=th-1;
    *px=x; *py=y;
  };
  std::vector<float> zb(W*H,1e30f);
  std::vector<uint8_t> fb(W*H*3, 0);
 // bind-pose skinning:skinned = Σ w × world × inv(bind)× pos
  std::vector<float> vpos=m.pos;
  if(!m.bones.empty() && !m.skin.empty()){
    int nb=(int)m.bones.size();
    std::vector<float> ibm(nb*16);
    for(int i=0;i<nb;i++){
      const float* M=m.bones[i].mat;
      // 4x4 inverse (adjugate approx: translation part only, rotation assumed orthonormal)
      float d=M[0]*M[5]*M[10]+M[4]*M[9]*M[2]+M[8]*M[1]*M[6]-M[8]*M[5]*M[2]-M[4]*M[1]*M[10]-M[0]*M[9]*M[6];
      if(fabs(d)<1e-9f){ memcpy(&ibm[i*16],M,64); continue; }
      float invd=1.0f/d;
      float r00=M[0],r01=M[1],r02=M[2],r10=M[4],r11=M[5],r12=M[6],r20=M[8],r21=M[9],r22=M[10];
      float tx=M[12],ty=M[13],tz=M[14];
      // rotation part transposed (orthonormal assumption)
      float inv[16];
      inv[0]=r00; inv[1]=r10; inv[2]=r20; inv[3]=0;
      inv[4]=r01; inv[5]=r11; inv[6]=r21; inv[7]=0;
      inv[8]=r02; inv[9]=r12; inv[10]=r22; inv[11]=0;
      inv[12]=-(r00*tx+r01*ty+r02*tz);
      inv[13]=-(r10*tx+r11*ty+r12*tz);
      inv[14]=-(r20*tx+r21*ty+r22*tz);
      inv[15]=1;
      for(int c=0;c<3;c++){
        float l=sqrt(inv[c*4]*inv[c*4]+inv[c*4+1]*inv[c*4+1]+inv[c*4+2]*inv[c*4+2]);
        if(l>1e-9){ inv[c*4]/=l; inv[c*4+1]/=l; inv[c*4+2]/=l; }
      }
      memcpy(&ibm[i*16],inv,64);
    }
    vpos.assign(m.uniqueVtx*3,0);
    for(uint32_t i=0;i<m.uniqueVtx;i++){
      float px=m.pos[i*3],py=m.pos[i*3+1],pz=m.pos[i*3+2];
      float acc[3]={0,0,0};
      for(auto&bw:m.skin[i]){
        int b=bw.first; if(b<0||b>=nb) continue;
        float w=bw.second;
        const float* B= animWorld? &animWorld[b*16] : m.bones[b].mat;
        const float* I=&ibm[b*16];
        float tmp[3];
        tmp[0]=I[0]*px+I[4]*py+I[8]*pz+I[12];
        tmp[1]=I[1]*px+I[5]*py+I[9]*pz+I[13];
        tmp[2]=I[2]*px+I[6]*py+I[10]*pz+I[14];
        acc[0]+=w*(B[0]*tmp[0]+B[4]*tmp[1]+B[8]*tmp[2]+B[12]);
        acc[1]+=w*(B[1]*tmp[0]+B[5]*tmp[1]+B[9]*tmp[2]+B[13]);
        acc[2]+=w*(B[2]*tmp[0]+B[6]*tmp[1]+B[10]*tmp[2]+B[14]);
      }
      vpos[i*3]=acc[0]; vpos[i*3+1]=acc[1]; vpos[i*3+2]=acc[2];
    }
  }
  // compute normals (flat)
  std::vector<float> n(m.pos.size(),0);
  for(size_t t=0;t<m.idx.size();t+=3){
    uint32_t i0=m.idx[t],i1=m.idx[t+1],i2=m.idx[t+2];
    float ax=vpos[i0*3],ay=vpos[i0*3+1],az=vpos[i0*3+2];
    float bx=vpos[i1*3],by=vpos[i1*3+1],bz=vpos[i1*3+2];
    float cx=vpos[i2*3],cy=vpos[i2*3+1],cz=vpos[i2*3+2];
    float ux=bx-ax,uy=by-ay,uz=bz-az, vx=cx-ax,vy=cy-ay,vz=cz-az;
    float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
    for(int k=0;k<3;k++){ n[i0*3+k]+= (k==0?nx:k==1?ny:nz); n[i1*3+k]+=(k==0?nx:k==1?ny:nz); n[i2*3+k]+=(k==0?nx:k==1?ny:nz);} }
  // AABB
  float mnx=1e30f,mny=1e30f,mnz=1e30f,mxx=-1e30f,mxy=-1e30f,mxz=-1e30f;
  for(size_t i=0;i<vpos.size();i+=3){ mnx=fmin(mnx,vpos[i]);mny=fmin(mny,vpos[i+1]);mnz=fmin(mnz,vpos[i+2]);mxx=fmax(mxx,vpos[i]);mxy=fmax(mxy,vpos[i+1]);mxz=fmax(mxz,vpos[i+2]);}
  float cx=(mnx+mxx)/2,cy=(mny+mxy)/2,cz=(mnz+mxz)/2;
  float scl=fmin((W-60)/fmax(mxx-mnx,1e-6f),(H-60)/fmax(mxy-mny,1e-6f));
  if(scl>300)scl=300;
  auto proj=[&](float x,float y,float z,int&sx,int&sy,float&sd){
    float rx=x-cx,ry=y-cy,rz=z-cz;
    sx=(int)(W/2+rx*scl); sy=(int)(H/2-ry*scl*0.9f+rz*scl*0.4f); sd=rz;
  };
  float lx=0.3f,ly=0.7f,lz=0.6f; float ll=sqrt(lx*lx+ly*ly+lz*lz); lx/=ll;ly/=ll;lz/=ll;
  for(size_t t=0;t<m.idx.size();t+=3){
    uint32_t i0=m.idx[t],i1=m.idx[t+1],i2=m.idx[t+2];
    int x0,y0,x1,y1,x2,y2; float d0,d1,d2;
    proj(vpos[i0*3],vpos[i0*3+1],vpos[i0*3+2],x0,y0,d0);
    proj(vpos[i1*3],vpos[i1*3+1],vpos[i1*3+2],x1,y1,d1);
    proj(vpos[i2*3],vpos[i2*3+1],vpos[i2*3+2],x2,y2,d2);
    float cr=(float)((x1-x0)*(y2-y0)-(y1-y0)*(x2-x0));
 if(cr<=0)continue;// CCW cull
    float nx=n[i0*3],ny=n[i0*3+1],nz=n[i0*3+2];
    float nl=sqrt(nx*nx+ny*ny+nz*nz); if(nl>1e-9){nx/=nl;ny/=nl;nz/=nl;}
    float dif=fmax(0.f,nx*lx+ny*ly+nz*lz);
    float amb=0.28f;
    int tr=235,tg=210,tb=190;
    if(!tex.empty() && (size_t)i0<m.uv0.size()/2){
      float u=m.uv0[i0*2], v=m.uv0[i0*2+1];
      int px,py; sample(u,v,&px,&py);
      size_t to=((size_t)py*tw+px)*4;
      if(to+3<tex.size()){ tr=tex[to]; tg=tex[to+1]; tb=tex[to+2]; }
    }
    float shi=std::min(255.f,(amb+0.85f*dif)*255.f);
    int sh=(int)shi; sh=std::min(sh,255);
    int miny=std::max(0,std::min(std::min(y0,y1),y2)), maxy=std::min(H-1,std::max(std::max(y0,y1),y2));
    if(miny>maxy) continue;
    for(int y=miny;y<=maxy;y++){
      float xs[6]; int cnt=0;
      auto inter=[&](int ax,int ay,float az,int bx,int by,float bz){
        if(ay==by) return;
        if((ay<=y&&by>y)||(by<=y&&ay>y)){
          float t=(float)(y-ay)/(by-ay);
          xs[cnt++]=ax+t*(bx-ax);
        }};
      inter(x0,y0,d0,x1,y1,d1); inter(x1,y1,d1,x2,y2,d2); inter(x2,y2,d2,x0,y0,d0);
      if(cnt<2) continue;
      std::sort(xs,xs+cnt);
      for(int k=0;k+1<cnt;k+=2){
        int xa=std::max(0,(int)xs[k]), xb=std::min(W-1,(int)xs[k+1]);
        for(int x=xa;x<=xb;x++){
          size_t o=(size_t)y*W+x;
          if(d0<zb[o]){ zb[o]=d0;
            fb[o*3]=(int)(tr*sh/255.0f); fb[o*3+1]=(int)(tg*sh/255.0f); fb[o*3+2]=(int)(tb*sh/255.0f);
          }
        }
      }
    }
  }
 // backgroundgradient
  for(int y=0;y<H;y++){ float t=(float)y/H;
    for(int x=0;x<W;x++){ size_t o=((size_t)y*W+x)*3;
      if(zb[(size_t)y*W+x]>1e29f){ fb[o]=(int)((0.10+0.10*t)*255); fb[o+1]=(int)((0.13+0.10*t)*255); fb[o+2]=(int)((0.20+0.10*t)*255);} } }
  FILE* f=fopen(out,"wb"); fprintf(f,"P6\n%d %d\n255\n",W,H); fwrite(fb.data(),1,fb.size(),f); fclose(f);
  printf("rendered %s: vtx=%u tri=%u aabb=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f)\n",out,m.uniqueVtx,m.triCount,mnx,mny,mnz,mxx,mxy,mxz);
}

int main(int argc,char**argv){
  if(argc<2){printf("usage: mesh020_render <file.mesh> [out.ppm] [tex.ktx] [anim.animpack] [frame]\n");return 1;}
  MeshFile m; std::string e;
  if(!decodeMesh020(argv[1],m,e)){printf("parse fail: %s\n",e.c_str());return 2;}
  printf("mesh: shared=%u total=%u faces=%u idx32=%d anim=%d norms=%d info2=%d skinRows=%zu uv=%zu bones=%zu\n",
    m.uniqueVtx,m.vtxTotal,m.triCount,m.wideIdx?1:0,m.animated?1:0,m.hasNormals?1:0,m.hasExtraIndexData?1:0,m.skin.size(),m.uv0.size()/2,m.bones.size());
  for(size_t i=0;i<m.bones.size()&&i<8;i++) printf("  bone[%zu] %s parent=%d\n",i,m.bones[i].name.c_str(),m.bones[i].parent);
  const char* out=argc>2?argv[2]:"/tmp/mesh020.ppm";
  const char* tex=argc>3?argv[3]:nullptr;
  const char* animPath=argc>4?argv[4]:nullptr;
  int frame=argc>5?atoi(argv[5]):0;
  std::vector<float> animWorld;
  if(animPath){
    sky::AnimPackDecoded ap;
    if(sky::decode_animpack(animPath, ap) && ap.ok){
      if(frame<(int)ap.minFrame) frame=(int)ap.minFrame;
      if(frame>(int)ap.maxFrame) frame=(int)ap.maxFrame;
      int fi=frame-(int)ap.minFrame;
 // DFS computeworldmatrix:local SQT × parentworld
      std::vector<int> order; order.reserve(ap.boneCount);
      for(uint32_t i=0;i<ap.boneCount;i++) order.push_back(i);
      std::sort(order.begin(),order.end(),[&](int a,int b){
        auto depth=[&](int x){ int d=0; while(ap.bones[x].parent>=0&&d<64){x=ap.bones[x].parent;d++;} return d; };
        return depth(a)<depth(b);
      });
      animWorld.assign(ap.boneCount*16,0);
      for(uint32_t oi=0;oi<ap.boneCount;oi++){
        int bi=order[oi];
        const float* sd=&ap.frameData[((size_t)bi*ap.frameCount+fi)*10];
        float s[3]={sd[0],sd[1],sd[2]}, q[4]={sd[3],sd[4],sd[5],sd[6]}, t[3]={sd[7],sd[8],sd[9]};
        for(int k=0;k<3;k++) if(std::isnan(s[k])) s[k]=1.f;
        for(int k=0;k<4;k++) if(std::isnan(q[k])) q[k]=(k==3)?1.f:0.f;
        for(int k=0;k<3;k++){ if(std::isnan(t[k])) t[k]=0.f; if(fabsf(t[k])>1e6f) t[k]=0.f; }
 // SQT->matrix(T·R·S)
        float x=q[0],y=q[1],z=q[2],w=q[3];
        float x2=x+x,y2=y+y,z2=z+z;
        float xx=x*x2,yy=y*y2,zz=z*z2,xy=x*y2,xz=x*z2,yz=y*z2,wx=w*x2,wy=w*y2,wz=w*z2;
        float local[16]={
          (1-(yy+zz))*s[0],(xy+wz)*s[0],(xz-wy)*s[0],0,
          (xy-wz)*s[1],(1-(xx+zz))*s[1],(yz+wx)*s[1],0,
          (xz+wy)*s[2],(yz-wx)*s[2],(1-(xx+yy))*s[2],0,
          t[0],t[1],t[2],1};
        int par=ap.bones[bi].parent;
        if(par>=0){
          const float* pw=&animWorld[par*16];
          for(int i=0;i<4;i++) for(int j=0;j<4;j++){
            float acc=0; for(int k=0;k<4;k++) acc+=pw[i*4+k]*local[k*4+j];
            animWorld[bi*16+i*4+j]=acc;
          }
        } else memcpy(&animWorld[bi*16],local,64);
      }
      // mesh bone name -> animpack bone index mapping (name match)
      auto stripRig=[&](const std::string& s){ return s.rfind("Rig:",0)==0? s.substr(4) : s; };
      std::vector<int> boneMap(m.bones.size(),-1);
      for(size_t i=0;i<m.bones.size();i++){
        std::string mn=stripRig(m.bones[i].name);
        for(uint32_t j=0;j<ap.boneCount;j++)
          if(mn==stripRig(ap.bones[j].name)){ boneMap[i]=(int)j; break; }
      }
      int matched=0; for(size_t i=0;i<m.bones.size();i++) if(boneMap[i]>=0) matched++;
 printf("anim %s frame=%d:%u bones,meshbonematch %d/%zu\n",animPath,frame,ap.boneCount,matched,m.bones.size());
      std::vector<float> mapped(m.bones.size()*16);
      for(size_t i=0;i<m.bones.size();i++){
        if(boneMap[i]>=0) memcpy(&mapped[i*16],&animWorld[boneMap[i]*16],64);
        else memcpy(&mapped[i*16],m.bones[i].mat,64);
      }
      animWorld.swap(mapped);
    }
  }
  render(m,out,tex, animWorld.empty()?nullptr:animWorld.data());
  return 0;
}
