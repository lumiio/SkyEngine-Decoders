// SkyEngine 全场景合成渲染器 v4
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <cctype>
#include <cstdint>

#include <lz4.h>
#include "../engine/asset/sky_bst_decoder.h"
#include "../engine/render/png_writer.h"
using namespace sky;
static uint32_t s_rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t s_rd16(const uint8_t* p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static float s_rdf(const uint8_t* p){ uint32_t u=s_rd32(p); float f; std::memcpy(&f,&u,4); return f; }

struct Mesh020 {
  uint32_t sharedVertices=0, totalVertices=0, faceCount=0; bool isIdx32=false;
  bool animated=false, loadMeshNorms=false, loadInfo2=false;
  uint32_t skipMeshPos=0, skipUvs=0, flag3=0;
  std::vector<float> pos, norm, uv0;
  struct Bone { std::string name; int parent; float mat[16]; };
  std::vector<Bone> bones;
  std::vector<std::vector<std::pair<int,float>>> skin; // bone,weight
  std::vector<uint32_t> idx;
  float aabbMin[3], aabbMax[3];
  bool ok=false;
};

static float half(uint16_t h){ uint16_t s=h&0x8000?1:0; int e=(h>>10)&0x1f; int m=h&0x3ff;
  if(e==0) return (s?-1.f:1.f)*(m/1024.0f)*std::pow(2.0f,-14);
  if(e==31) return m?NAN:(s?-INFINITY:INFINITY);
  return (s?-1.f:1.f)*(1.0f+m/1024.0f)*std::pow(2.0f,(float)(e-15)); }

static bool parse020(const std::string& path, Mesh020& m, std::string& err){
  FILE* f=fopen(path.c_str(),"rb"); if(!f){err="open fail";return false;}
  fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> raw(sz); if(fread(raw.data(),1,sz,f)!=(size_t)sz){fclose(f);err="read";return false;} fclose(f);
  if(sz<0x58){err="too small";return false;}
  m.animated = raw[0x48]!=0;
  const int payloadOffset = 0x4e;
  int isCompressed = (int)s_rd32(&raw[payloadOffset]);
  uint32_t csz=s_rd32(&raw[payloadOffset+4]), usz=s_rd32(&raw[payloadOffset+8]);
  if(csz==0 || usz==0 || payloadOffset+12+csz>(size_t)sz){err="bad bounds";return false;}
  const uint8_t* src=&raw[payloadOffset+12];
  std::vector<uint8_t> dec;
  const uint8_t* d;
  if(isCompressed!=0){ dec.resize(usz);
    int r=LZ4_decompress_safe((const char*)src,(char*)dec.data(),(int)csz,(int)usz);
    if(r<=0){ err="lz4 fail"; return false; } d=dec.data(); }
  else d=src;
  const uint8_t* bb=d; size_t blen=csz;
  int p=4;
  auto rdv3=[&](int o,float* v){ v[0]=s_rdf(bb+o);v[1]=s_rdf(bb+o+4);v[2]=s_rdf(bb+o+8); };
  float aabbA[3],aabbB[3],aabbA2[3],aabbB2[3];
  rdv3(p,aabbA); p+=12; rdv3(p,aabbB); p+=12; rdv3(p,aabbA2); p+=12; rdv3(p,aabbB2); p+=12;
  float quantMin[8],quantMax[8];
  for(int i=0;i<8;i++)quantMin[i]=s_rdf(bb+p+i*4); p+=32;
  for(int i=0;i<8;i++)quantMax[i]=s_rdf(bb+p+i*4); p+=32;
  m.sharedVertices=s_rd32(bb+p); p+=4;
  m.totalVertices=s_rd32(bb+p); p+=4;
  m.isIdx32=s_rd32(bb+p)!=0; p+=4;
  uint32_t numPoints=s_rd32(bb+p); p+=4;
  uint32_t prop11=s_rd32(bb+p); p+=4;
  uint32_t prop12=s_rd32(bb+p); p+=4;
  uint32_t prop13=s_rd32(bb+p); p+=4;
  uint32_t prop14=s_rd32(bb+p); p+=4;
  m.loadMeshNorms=bb[p]!=0; p+=1;
  m.loadInfo2=bb[p]!=0; p+=1;
  p+=1;
  m.skipMeshPos=s_rd32(bb+p); p+=4;
  m.skipUvs=s_rd32(bb+p); p+=4;
  m.flag3=s_rd32(bb+p); p+=4;
  p+=0x10;
  m.faceCount=m.totalVertices/3;
  int idxUnit=m.isIdx32?4:2;
  std::vector<float> verts; verts.reserve(m.sharedVertices*3);
  if(m.skipMeshPos==0){
    for(uint32_t i=0;i<m.sharedVertices;i++){
      int off=p+i*16;
      verts.push_back(s_rdf(bb+off)); verts.push_back(s_rdf(bb+off+4)); verts.push_back(s_rdf(bb+off+8));
    }
    p+=m.sharedVertices*16;
  }
  if(m.loadMeshNorms) p+=m.sharedVertices*4;
  if(m.skipUvs==0){
    for(uint32_t i=0;i<m.sharedVertices;i++){
      int base=p+i*16;
      m.uv0.push_back(half(s_rd16(bb+base))); m.uv0.push_back(half(s_rd16(bb+base+2)));
    }
    p+=m.sharedVertices*16;
  }
  if(m.animated){
    for(uint32_t i=0;i<m.sharedVertices;i++){
      int off=p+i*8;
      std::vector<std::pair<int,float>> row;
      for(int j=0;j<4;j++){
        int bi=bb[off+j]&0xff, wi=bb[off+4+j]&0xff;
        if(bi>0&&wi>0) row.push_back({bi-1, wi/255.0f});
      }
      m.skin.push_back(row);
    }
    p+=m.sharedVertices*8;
  }
  for(uint32_t i=0;i<m.faceCount;i++){
    uint32_t a,b,c;
    if(m.isIdx32){ a=s_rd32(bb+p);b=s_rd32(bb+p+4);c=s_rd32(bb+p+8); p+=12; }
    else { a=s_rd16(bb+p);b=s_rd16(bb+p+2);c=s_rd16(bb+p+4); p+=6; }
    m.idx.push_back(a); m.idx.push_back(b); m.idx.push_back(c);
  }
  if(m.loadInfo2) p+=m.totalVertices*idxUnit;
  if(numPoints>0) p+=m.sharedVertices*idxUnit;
  if(prop11>0) p+=m.sharedVertices*idxUnit;
  if(prop12>0) p+=(int)prop12*idxUnit;
  if(prop13>0) p+=(int)prop13*4;
  if(prop14>0) p+=(int)prop14*(m.isIdx32?8:4);
  p+=(int)m.faceCount*4;
  if(m.skipMeshPos>0){
    float ax=aabbA2[0],ay=aabbA2[1],az=aabbA2[2];
    float sx=aabbB2[0]-ax,sy=aabbB2[1]-ay,sz=aabbB2[2]-az;
    for(uint32_t i=0;i<m.sharedVertices;i++){
      uint32_t packed=s_rd32(bb+p+i*4);
      int qz=(int)(packed&0x3ff), qy=(int)((packed>>10)&0x3ff), qx=(int)((packed>>20)&0x3ff);
      verts.push_back(ax+(qx/1023.0f)*sx); verts.push_back(ay+(qy/1023.0f)*sy); verts.push_back(az+(qz/1023.0f)*sz);
    }
    p+=m.sharedVertices*4 + m.sharedVertices;
  }
  if(m.skipUvs>0){
    float uvMinU=quantMin[0],uvMinV=quantMin[1],uvSizeU=quantMax[0]-uvMinU,uvSizeV=quantMax[1]-uvMinV;
    for(uint32_t i=0;i<m.sharedVertices;i++){
      int off=p+i*4;
      int uHi=bb[off]&0xff,vHi=bb[off+1]&0xff,uLo=bb[off+2]&0xff,vLo=bb[off+3]&0xff;
      float u=uvMinU+(((uHi<<8)|uLo)/65535.0f)*uvSizeU;
      float v=uvMinV+(((vHi<<8)|vLo)/65535.0f)*uvSizeV;
      m.uv0.push_back(u); m.uv0.push_back(v);
    }
    p+=m.sharedVertices*4;
  }
  m.pos=verts;
  m.aabbMin[0]=aabbA[0];m.aabbMin[1]=aabbA[1];m.aabbMin[2]=aabbA[2];
  m.aabbMax[0]=aabbB[0];m.aabbMax[1]=aabbB[1];m.aabbMax[2]=aabbB[2];
  // 内嵌骨骼(文件尾) — 0x20 段: marker=11 + name NUL + [boneCount u32][0 u32][nameField u32] + 骨骼 132B×boneCount
  if(m.animated && sz-(payloadOffset+12+csz) >= 85){
    const uint8_t* s=&raw[payloadOffset+12+csz]; size_t slen=sz-(payloadOffset+12+csz);
    int mk=(int)s_rd32(s);
    if(mk==11||mk==9||mk==0x1c){
      size_t o=4; while(o<slen && s[o]!=0) o++; o++; // 段名
      int bc=0; size_t bco=(size_t)-1;
      size_t lim=std::min(o+0x100, slen-12);
      for(size_t sc=o;sc<lim;sc++){
        int v1=(int)s_rd32(s+sc); int v2=(int)s_rd32(s+sc+4); int v3=(int)s_rd32(s+sc+8);
        if(v1>=1&&v1<=500&&v2==0&&v3>=20&&v3<=64){bc=v1;bco=sc;break;}
      }
      if(bc>0&&bco!=(size_t)-1){
        size_t bs=bco+12+5; // boneCount+0+nameField + 1B标志 + 4B hash
        // 骨骼 0 起点 = bs+1（跳过 1B 标志）+ 4B hash？ — 实际: bone0 名紧跟 nameField 后 5B
        // 用名字特征定位: 从 bs 起逐记录 132B, 名字在记录头 64B
        for(int i=0;i<bc;i++){
          size_t bo=bs+i*132;
          if(bo+132>slen) break;
          char nm[65]; memcpy(nm,s+bo,64); nm[64]=0;
          std::string name(nm); size_t e=name.find('\0'); if(e!=std::string::npos)name=name.substr(0,e);
          float mat[16]; for(int j=0;j<16;j++) mat[j]=s_rdf(s+bo+64+j*4);
          int pi=(int)s_rd32(s+bo+128); if(pi>0) pi=pi-1; else pi=-1;
          m.bones.push_back({name,pi,{mat[0],mat[1],mat[2],mat[3],mat[4],mat[5],mat[6],mat[7],mat[8],mat[9],mat[10],mat[11],mat[12],mat[13],mat[14],mat[15]}});
        }
      }
    }
  }
  m.ok=true; return true;
}

struct Instance { std::string name; float mat[16]; std::string tex; std::string tex2; float col[3]; bool hasCol=false; float lit=0; bool alphaBlend=false; };
struct Water { float x,y,z,sx,sz; int render=1; };
struct Tex { int w=0,h=0; std::vector<uint8_t> rgb; };
static bool loadPPM(const char* path, Tex& t){
  FILE* f=fopen(path,"rb"); if(!f) return false;
  char hdr[256]; if(!fgets(hdr,sizeof(hdr),f)){ fclose(f); return false; }
  if(hdr[0]!='P'||hdr[1]!='6'){ fclose(f); return false; }
  int w=0,h=0,d=0;
  if(fscanf(f,"%d %d %d",&w,&h,&d)!=3){ fclose(f); return false; }
  fgetc(f);
  std::vector<uint8_t> raw(w*h*3);
  if(fread(raw.data(),1,raw.size(),f)!=raw.size()){ fclose(f); return false; }
  fclose(f); t.w=w; t.h=h; t.rgb=std::move(raw); return true;
}
static inline void sampleTex(const Tex& t, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b){
  if(t.w<=0||t.rgb.empty()){ r=g=b=255; return; }
  double uu=fmod((double)u,1.0); if(uu<0) uu+=1.0;
  double vv=fmod((double)v,1.0); if(vv<0) vv+=1.0;
  // 双线性: 4 像素加权平均 (平滑 ETC2 色噪)
  double xf=uu*t.w-0.5, yf=vv*t.h-0.5;
  int x0=(int)floor(xf), y0=(int)floor(yf);
  double fx=xf-x0, fy=yf-y0;
  if(x0<0) x0+=t.w; if(x0>=t.w) x0-=t.w;
  int x1=x0+1; if(x1>=t.w) x1=0;
  if(y0<0) y0+=t.h; if(y0>=t.h) y0-=t.h;
  int y1=y0+1; if(y1>=t.h) y1=0;
  auto A=[&](int xx,int yy,int ch){ return t.rgb[((size_t)yy*t.w+xx)*3+ch]; };
  double w00=(1-fx)*(1-fy), w10=fx*(1-fy), w01=(1-fx)*fy, w11=fx*fy;
  r=(uint8_t)(A(x0,y0,0)*w00+A(x1,y0,0)*w10+A(x0,y1,0)*w01+A(x1,y1,0)*w11);
  g=(uint8_t)(A(x0,y0,1)*w00+A(x1,y0,1)*w10+A(x0,y1,1)*w01+A(x1,y1,1)*w11);
  b=(uint8_t)(A(x0,y0,2)*w00+A(x1,y0,2)*w10+A(x0,y1,2)*w01+A(x1,y1,2)*w11);
}

static bool parseMeshCompat(const std::string& path, Mesh020& m){
  std::string err; return parse020(path, m, err);
}


struct EnvData{
  float tintTop[3]={0.0707f,0.2505f,1.0f}, tintMidTop[3]={0.0707f,0.6549f,1.0f};
  float tintMidBot[3]={1.0f,0.3876f,0.0707f}, tintBot[3]={1.0f,0.0707f,0.0707f};
  float sunAz=-10.f, sunEl=60.f, sunInt=1.f, sunColor[3]={1.f,0.88f,0.79f};
  float fogNear=0.f, fogFar=2250.f; int fogEnabled=1;
  float fogTintTop[3]={0.06f,0.21f,0.84f}, fogTintMidBot[3]={0.38f,1.0f,0.90f};
  float fogLumRed=0.35f; bool has=false;
};
static EnvData gEnv;
static void writeOut(const char* path, const std::vector<unsigned char>& fb, int W, int H){
  if(path && (strstr(path,".bmp")||strstr(path,".BMP"))){
    FILE* f=fopen(path,"wb"); if(!f) return;
    unsigned bf=54+W*H*3, bih=40, z=0;
    unsigned short t0=0x4D42, p1=1, p2=24;
    fwrite(&t0,2,1,f); fwrite(&bf,4,1,f); fwrite(&z,4,1,f); fwrite(&z,4,1,f);
    fwrite(&bih,4,1,f); fwrite(&W,4,1,f); fwrite(&H,4,1,f);
    fwrite(&p1,2,1,f); fwrite(&p2,2,1,f);
    fwrite(&z,4,1,f); fwrite(&z,4,1,f);
    int pp=(W*3+3)&~3; fwrite(&pp,4,1,f); fwrite(&z,4,1,f); fwrite(&z,4,1,f); fwrite(&z,4,1,f); fwrite(&z,4,1,f);
    std::vector<unsigned char> row(pp,0);
    for(int y=H-1;y>=0;y--){
      for(int x=0;x<W;x++){ row[x*3+0]=fb[(y*W+x)*3+2]; row[x*3+1]=fb[(y*W+x)*3+1]; row[x*3+2]=fb[(y*W+x)*3+0]; }
      fwrite(row.data(),1,pp,f);
    }
    fclose(f);
  } else {
    FILE* f=fopen(path,"wb"); if(!f) return;
    fprintf(f,"P6\n%d %d\n255\n",W,H);
    fwrite(fb.data(),1,fb.size(),f); fclose(f);
  }
}
static void parseEnv(const char* path){
  FILE* f=fopen(path,"r"); if(!f) return;
  char line[256];
  while(fgets(line,sizeof(line),f)){
    float v[3]={0,0,0}; int n=0;
    if(sscanf(line,"tintTop=%f,%f,%f",&v[0],&v[1],&v[2])==3){gEnv.tintTop[0]=v[0];gEnv.tintTop[1]=v[1];gEnv.tintTop[2]=v[2];n=1;}
    else if(sscanf(line,"tintMidTop=%f,%f,%f",&v[0],&v[1],&v[2])==3){gEnv.tintMidTop[0]=v[0];gEnv.tintMidTop[1]=v[1];gEnv.tintMidTop[2]=v[2];n=1;}
    else if(sscanf(line,"tintMidBot=%f,%f,%f",&v[0],&v[1],&v[2])==3){gEnv.tintMidBot[0]=v[0];gEnv.tintMidBot[1]=v[1];gEnv.tintMidBot[2]=v[2];n=1;}
    else if(sscanf(line,"tintBot=%f,%f,%f",&v[0],&v[1],&v[2])==3){gEnv.tintBot[0]=v[0];gEnv.tintBot[1]=v[1];gEnv.tintBot[2]=v[2];n=1;}
    else if(sscanf(line,"sunColor=%f,%f,%f",&v[0],&v[1],&v[2])==3){gEnv.sunColor[0]=v[0];gEnv.sunColor[1]=v[1];gEnv.sunColor[2]=v[2];n=1;}
    else if(sscanf(line,"fogTintTop=%f,%f,%f",&v[0],&v[1],&v[2])==3){gEnv.fogTintTop[0]=v[0];gEnv.fogTintTop[1]=v[1];gEnv.fogTintTop[2]=v[2];n=1;}
    else if(sscanf(line,"fogTintMidBot=%f,%f,%f",&v[0],&v[1],&v[2])==3){gEnv.fogTintMidBot[0]=v[0];gEnv.fogTintMidBot[1]=v[1];gEnv.fogTintMidBot[2]=v[2];n=1;}
    else if(sscanf(line,"sunAngleXZ=%f",&v[0])==1){gEnv.sunAz=v[0];n=1;}
    else if(sscanf(line,"sunAngleY=%f",&v[0])==1){gEnv.sunEl=v[0];n=1;}
    else if(sscanf(line,"sunInt=%f",&v[0])==1){gEnv.sunInt=v[0];n=1;}
    else if(sscanf(line,"fogNear=%f",&v[0])==1){gEnv.fogNear=v[0];n=1;}
    else if(sscanf(line,"fogFar=%f",&v[0])==1){gEnv.fogFar=v[0];n=1;}
    else if(sscanf(line,"fogEnabled=%d",&gEnv.fogEnabled)==1){n=1;}
    else if(sscanf(line,"fogLumRed=%f",&v[0])==1){gEnv.fogLumRed=v[0];n=1;}
  }
  fclose(f);
  gEnv.has=true;
}

int main(int argc, char** argv){
  // 烘焙光照可视化模式 (任意参数含 --bakevis): 全场景明暗图
  bool bakevis=false;
  for(int a=1;a<argc;a++) if(!strcmp(argv[a],"--bakevis")) bakevis=true;
  for(int a=1;a+1<argc;a++) if(!strcmp(argv[a],"--env")){ parseEnv(argv[a+1]); break; }
  const char* dumpPath=nullptr;
  for(int a=1;a+1<argc;a++) if(!strcmp(argv[a],"--dump")){ dumpPath=argv[a+1]; break; }
  // 模型预览模式: scene_compose --preview <mesh> <out.ppm> [scale]
  if(argc>=4 && strcmp(argv[1],"--preview")==0){
    Mesh020 m; std::string err;
    if(!parse020(argv[2],m,err)){ fprintf(stderr,"解析失败: %s\n",err.c_str()); return 1; }
    float scale = argc>4?atof(argv[4]):1.0f;
    // 可选贴图: --preview <mesh> <out.ppm> <scale> <texDir> <texName>
    Tex pt; bool hasTex=false;
    if(argc>=7){
      std::string td=argv[5]; if(loadPPM((td+"/"+argv[6]+".ppm").c_str(),pt)) hasTex=true;
    }
    int W=1536,H=1536;
    std::vector<uint8_t> fb(W*H*3,24), zb(W*H,255);
    auto rast=[&](const float* a,const float* b,const float* c,int ca,int cb,int cc,
                  float ua,float va,float ub,float vb,float uc,float vc){
      float mnx2=fmin(a[0],fmin(b[0],c[0])), mny2=fmin(a[1],fmin(b[1],c[1]));
      float mxx2=fmax(a[0],fmax(b[0],c[0])), mxy2=fmax(a[1],fmax(b[1],c[1]));
      int x0=(int)floor(mnx2),x1=(int)ceil(mxx2),y0=(int)floor(mny2),y1=(int)ceil(mxy2);
      for(int y=std::max(0,y0);y<=std::min(H-1,y1);y++){
        for(int x=std::max(0,x0);x<=std::min(W-1,x1);x++){
          float px=x+0.5f,py=y+0.5f;
          float den=((b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]));
          if(fabs(den)<1e-9) continue;
          float w0=((b[1]-c[1])*(px-c[0])+(c[0]-b[0])*(py-c[1]))/den;
          float w1=((c[1]-a[1])*(px-c[0])+(a[0]-c[0])*(py-c[1]))/den;
          float w2=1-w0-w1;
          if(w0<-0.001||w1<-0.001||w2<-0.001) continue;
          float zd=w0*a[2]+w1*b[2]+w2*c[2];
          int o=y*W+x;
          if(zd<zb[o]){
            zb[o]=zd;
            int r=210,g=196,b=182;
            if(hasTex){
              float uu=w0*ua+w1*ub+w2*uc, vv=w0*va+w1*vb+w2*vc;
              uint8_t tr,tg,tb; sampleTex(pt,uu,vv,tr,tg,tb);
              r=tr; g=tg; b=tb;
            }
            float diff=0.5f+0.5f*fmax(0.f,w0*ca+w1*cb+w2*cc);
            if(!(diff>=0.f&&diff<=1.f)) diff=0.6f;  // NaN 防护
            fb[o*3]=(uint8_t)(r*diff); fb[o*3+1]=(uint8_t)(g*diff); fb[o*3+2]=(uint8_t)(b*diff);
          }
        }
      }
    };
    // 归一化: 世界坐标→屏幕 (模型居中, 缩放适配)
    float mnx=1e9,mny=1e9,mnz=1e9,mxx=-1e9,mxy=-1e9,mxz=-1e9;
    for(size_t i=0;i<m.pos.size();i+=3){
      mnx=fmin(mnx,m.pos[i]);mny=fmin(mny,m.pos[i+1]);mnz=fmin(mnz,m.pos[i+2]);
      mxx=fmax(mxx,m.pos[i]);mxy=fmax(mxy,m.pos[i+1]);mxz=fmax(mxz,m.pos[i+2]);
    }
    float cx=(mnx+mxx)/2,cy=(mny+mxy)/2,cz=(mnz+mxz)/2;
    float extent=fmax(mxx-mnx,fmax(mxy-mny,mxz-mnz));
    float s=(W*0.62f)/extent*scale;
    // 透视投影: 相机斜上方看向中心
    float camX=cx, camY=cy+extent*0.9f, camZ=cz+extent*1.6f;
    float fx=cx-camX, fy=cy-camY, fz=cz-camZ; float fl=sqrt(fx*fx+fy*fy+fz*fz); fx/=fl;fy/=fl;fz/=fl;
    float ux=0,uy=1,uz=0;
    float rx=uy*fz-uz*fy, ry=uz*fx-ux*fz, rz=ux*fy-uy*fx; fl=sqrt(rx*rx+ry*ry+rz*rz); rx/=fl;ry/=fl;rz/=fl;
    float vx=fy*rz-fz*ry, vy=fz*rx-fx*rz, vz=fx*ry-fy*rx;
    float focal=W*1.15f;
    float nx2=0.5f,ny2=0.8f,nz2=0.3f; fl=sqrt(nx2*nx2+ny2*ny2+nz2*nz2); nx2/=fl;ny2/=fl;nz2/=fl;
    for(size_t t=0;t+2<m.idx.size();t+=3){
      uint32_t i0=m.idx[t],i1=m.idx[t+1],i2=m.idx[t+2];
      if(i0>=m.pos.size()/3||i1>=m.pos.size()/3||i2>=m.pos.size()/3) continue;
      auto P=[&](uint32_t i,float* out){
        float wx=m.pos[i*3]-camX, wy=m.pos[i*3+1]-camY, wz=m.pos[i*3+2]-camZ;
        float xc=wx*rx+wy*ry+wz*rz, yc=wx*vx+wy*vy+wz*vz, zc=wx*fx+wy*fy+wz*fz;
        if(zc<0.01f) zc=0.01f;
        out[0]=xc/zc*focal + W*0.5f;
        out[1]=-yc/zc*focal + H*0.5f;
        out[2]=zc;
      };
      float a[3],b[3],c[3]; P(i0,a);P(i1,b);P(i2,c);
      int ca=0,cb=0,cc=0;
      if(m.norm.size()>=3){ ca=(int)(fmax(0.f,m.norm[i0*3]*nx2+m.norm[i0*3+1]*ny2+m.norm[i0*3+2]*nz2)*255);
        cb=(int)(fmax(0.f,m.norm[i1*3]*nx2+m.norm[i1*3+1]*ny2+m.norm[i1*3+2]*nz2)*255);
        cc=(int)(fmax(0.f,m.norm[i2*3]*nx2+m.norm[i2*3+1]*ny2+m.norm[i2*3+2]*nz2)*255); }
      float ua=0,va=0,ub=0,vb=0,uc=0,vc=0;
      if(m.uv0.size()>=3){ ua=m.uv0[i0*2];va=m.uv0[i0*2+1];ub=m.uv0[i1*2];vb=m.uv0[i1*2+1];uc=m.uv0[i2*2];vc=m.uv0[i2*2+1]; }
      rast(a,b,c,ca,cb,cc,ua,va,ub,vb,uc,vc);
    }
    if(!hasTex){ // 无贴图时画线框
    auto drawLine=[&](float x0,float y0,float x1,float y1){
      int steps=(int)(fmax(fabs(x1-x0),fabs(y1-y0)))+1;
      for(int k=0;k<=steps;k++){
        float t=steps?(float)k/steps:0;
        int x=(int)(x0+t*(x1-x0)), y=(int)(y0+t*(y1-y0));
        if(x>=0&&x<W&&y>=0&&y<H){ int o=y*W+x; fb[o*3]=255; fb[o*3+1]=255; fb[o*3+2]=255; }
      }
    };
    std::map<std::pair<uint32_t,uint32_t>,int> edgeCnt;
    for(size_t t=0;t+2<m.idx.size();t+=3){
      uint32_t i0=m.idx[t],i1=m.idx[t+1],i2=m.idx[t+2];
      if(i0>=m.pos.size()/3||i1>=m.pos.size()/3||i2>=m.pos.size()/3) continue;
      auto E=[&](uint32_t a,uint32_t b){ std::pair<uint32_t,uint32_t> e=a<b?std::make_pair(a,b):std::make_pair(b,a); edgeCnt[e]++; };
      E(i0,i1);E(i1,i2);E(i2,i0);
    }
    for(auto&e:edgeCnt){
      uint32_t a=e.first.first,b=e.first.second;
      auto P2=[&](uint32_t i,float* out){
        float wx=m.pos[i*3]-camX, wy=m.pos[i*3+1]-camY, wz=m.pos[i*3+2]-camZ;
        float xc=wx*rx+wy*ry+wz*rz, yc=wx*vx+wy*vy+wz*vz, zc=wx*fx+wy*fy+wz*fz;
        if(zc<0.01f) zc=0.01f;
        out[0]=xc/zc*focal + W*0.5f; out[1]=-yc/zc*focal + H*0.5f;
      };
      float a2[2],b2[2]; P2(a,a2);P2(b,b2);
      drawLine(a2[0],a2[1],b2[0],b2[1]);
    }
    }
    writeOut(argv[3], fb, W, H);
    printf("模型预览: %s 顶点=%zu 三角=%zu 范围=%.2f",argv[2],m.pos.size()/3,m.idx.size()/3,extent);
    long long nanN=0,nanU=0;
    for(auto x:m.norm) if(std::isnan(x)) nanN++;
    for(auto x:m.uv0) if(std::isnan(x)) nanU++;
    double umin=1e9,umax=-1e9,vmin=1e9,vmax=-1e9;
    for(size_t i=0;i+1<m.uv0.size();i+=2){ umin=fmin(umin,m.uv0[i]);umax=fmax(umax,m.uv0[i]); vmin=fmin(vmin,m.uv0[i+1]);vmax=fmax(vmax,m.uv0[i+1]); }
    printf(" NaN: norm=%lld uv=%lld  uv范围 u[%.2f,%.2f] v[%.2f,%.2f]\n",nanN,nanU,umin,umax,vmin,vmax);
    return 0;
  }
  if(argc<4){ fprintf(stderr,"用法: scene_compose <instances.txt> <meshDir> <out.ppm> [bst.meshes] [texDir]\n"); return 1; }
  const char* instPath=argv[1];
  std::string meshDir=argv[2];
  const char* outPath=argv[3];
  const char* bstPath=argc>4?argv[4]:nullptr;
  const char* texDir=argc>5?argv[5]:nullptr;
  std::vector<Instance> insts;
  std::vector<Water> waters;
  std::map<int,std::string> matTex;
  FILE* f=fopen(instPath,"r");
  if(!f){ perror("inst"); return 1; }
  char line[2048];
  std::string curName, curTex, curTex2; float curMat[16]; float curCol[3]={1,1,1}; bool curHasCol=false; float curLit=0; bool curAlpha=false;
  while(fgets(line,sizeof(line),f)){
    if(line[0]=='[' && strchr(line,']')){
      char* lb=strchr(line,']');
      char* nm=lb+1; while(*nm==' '||*nm=='\t') nm++;
      char* bar=strchr(nm,'|');
      if(bar) *bar=0;
      char* sp=strchr(nm,' ');
      if(sp) *sp=0;
      size_t l=strlen(nm); while(l&&isspace((unsigned char)nm[l-1])) nm[--l]=0;
      curName=nm;
      const char* tx=strstr(line,"tex=");
      curTex = tx ? (tx+4) : "White";
      char* tb=strchr((char*)curTex.c_str(),'|'); if(tb) *tb=0;
      while(curTex.size()&&isspace((unsigned char)curTex.back())) curTex.pop_back();
      const char* t2=strstr(line,"tex2=");
      if(t2){ curTex2=t2+5; char* t2b=strchr((char*)curTex2.c_str(),'|'); if(t2b) *t2b=0; while(curTex2.size()&&isspace((unsigned char)curTex2.back())) curTex2.pop_back(); }
      else curTex2.clear();
      const char* cl=strstr(line,"col=");
      curHasCol=false;
      if(cl){ if(sscanf(cl+4,"%f,%f,%f",&curCol[0],&curCol[1],&curCol[2])==3){ curHasCol=true; if(curCol[0]<0)curCol[0]=0; if(curCol[1]<0)curCol[1]=0; if(curCol[2]<0)curCol[2]=0; } }
      const char* lt=strstr(line,"lit=");
      curLit = lt ? atof(lt+4) : 0;
      const char* sd=strstr(line,"shader=");
      curAlpha = sd && (strstr(sd,"Alpha")||strstr(sd,"LitAlpha")||strstr(sd,"Cham"));
    } else if(strstr(line,"mat=[") && !curName.empty()){
      char* p=strchr(line,'['); char* e=strchr(line,']');
      if(p&&e){ *e=0; p++;
        char* tok=strtok(p,",");
        int n=0;
        while(tok&&n<16){ curMat[n++]=atof(tok); tok=strtok(nullptr,","); }
        if(n>=12){ Instance in; in.name=curName; in.tex=curTex; in.tex2=curTex2; in.hasCol=curHasCol; in.lit=curLit; in.alphaBlend=curAlpha; if(curHasCol){in.col[0]=curCol[0];in.col[1]=curCol[1];in.col[2]=curCol[2];} memcpy(in.mat,curMat,64); insts.push_back(in); }
      }
    } else if(strncmp(line,"WATER ",6)==0){
      Water w;
      if(sscanf(line+6,"%f %f %f %f %f %d",&w.x,&w.y,&w.z,&w.sx,&w.sz,&w.render)>=5){ waters.push_back(w); }
    } else if(strncmp(line,"MAT ",4)==0){
      char* t[8]={}; int nt=0;
      char* tok=strtok(line+4," \t\n");
      while(tok&&nt<8){ t[nt++]=tok; tok=strtok(nullptr," \t\n"); }
      if(nt>=2){ int mid=atoi(t[0]); matTex[mid]=nt>=2?t[1]:""; }
    }
  }
  fclose(f);
  printf("实例数: %zu\n", insts.size());
  // 贴图缓存
  std::map<std::string,Tex> texCache;
  if(texDir){
    for(auto&in:insts){
      if(texCache.count(in.tex)) continue;
      Tex t; if(loadPPM((std::string(texDir)+"/"+in.tex+".ppm").c_str(),t)) texCache[in.tex]=std::move(t);
      else texCache[in.tex]=Tex{1,1,{255,255,255}};
    }
    printf("贴图加载: %zu 张\n", texCache.size());
  }

  // GEO0 地形
  MeshData terr; std::vector<float> terrVerts; std::vector<uint32_t> terrIdx;
  if(bstPath){
    FILE* bf=fopen(bstPath,"rb");
    if(bf){
      fseek(bf,0,SEEK_END); long bsz=ftell(bf); fseek(bf,0,SEEK_SET);
      std::vector<uint8_t> bb(bsz); if(fread(bb.data(),1,bsz,bf)!=(size_t)bsz){fclose(bf); bstPath=nullptr;}
      fclose(bf);
      if(bstPath && decode_bst_baked(bb,terr)){
        for(auto&p:terr.pos){ terrVerts.push_back(p.x); terrVerts.push_back(p.y); terrVerts.push_back(p.z); }
        for(uint32_t ci=0;ci<terr.chunkCount+terr.cloudChunkCount;ci++){
          uint32_t base=terr.vtxStart[ci];
          for(uint32_t i=0;i+2<terr.idxCount[ci];i+=3){
            terrIdx.push_back(base+terr.idx[terr.idxStart[ci]+i]);
            terrIdx.push_back(base+terr.idx[terr.idxStart[ci]+i+1]);
            terrIdx.push_back(base+terr.idx[terr.idxStart[ci]+i+2]);
          }
        }
        printf("GEO0 地形: %zu 顶点 %zu 三角形\n", terrVerts.size()/3, terrIdx.size()/3);
        // vmat 材质分布统计
        std::map<int,int> vh;
        for(uint8_t v:terr.vmat) vh[v]++;
        printf("  vmat分布: ");
        for(auto&kv:vh) printf("%d=%d ", kv.first, kv.second);
        printf("\n");
      }
    }
  }
  std::map<std::string, Mesh020> cache;
  auto loadMesh=[&](const std::string& name)->const Mesh020*{
    auto it=cache.find(name);
    if(it!=cache.end()) return &it->second;
    Mesh020 m;
    std::vector<std::string> dirs;
    { size_t s=0; std::string d=meshDir; while((s=d.find(','))!=std::string::npos){ dirs.push_back(d.substr(0,s)); d=d.substr(s+1);} dirs.push_back(d); }
    const char* variants[]={"","_CompOcc","_ZipPos","_ZipUvs","_StripAnim_CompOcc_ZipPos_ZipUvs_StripNorm","_StripAnim","_CompOcc_ZipPos"};
    for(auto&dir:dirs){
      for(auto v:variants){
        std::string p=dir+"/"+name+v+".mesh";
        if(parseMeshCompat(p,m) && m.ok){ cache[name]=m; return &cache[name]; }
      }
    }
    static Mesh020 empty; empty.ok=false; cache[name]=empty; return &cache[name];
  };

  float mnx=1e30f,mny=1e30f,mnz=1e30f,mxx=-1e30f,mxy=-1e30f,mxz=-1e30f;
  int loaded=0, missing=0;
  std::vector<std::vector<float>> instVerts;
  std::vector<std::vector<uint32_t>> instIdx;
  std::vector<std::vector<float>> instUvs;
  for(auto&in:insts){
    const Mesh020* m=loadMesh(in.name);
    if(!m || !m->ok){ missing++; if(missing<=200) printf("  缺失: %s\n", in.name.c_str()); continue; }
    loaded++;
    const float* M=in.mat;
    // 3x3 行列式 (检测镜像实例)
    float det = M[0]*(M[4]*M[8]-M[5]*M[7]) - M[3]*(M[1]*M[8]-M[2]*M[7]) + M[6]*(M[1]*M[5]-M[2]*M[4]);
    bool mirrored = det<0;
    std::vector<float> verts(m->pos.size());
    for(size_t i=0;i<m->pos.size();i+=3){
      float x=m->pos[i],y=m->pos[i+1],z=m->pos[i+2];
      // 4x4 行主序: tx = r0·v + m03 ...
      float tx=M[0]*x+M[1]*y+M[2]*z+M[3];
      float ty=M[4]*x+M[5]*y+M[6]*z+M[7];
      float tz=M[8]*x+M[9]*y+M[10]*z+M[11];
      verts[i]=tx; verts[i+1]=ty; verts[i+2]=tz;
      mnx=fmin(mnx,tx);mny=fmin(mny,ty);mnz=fmin(mnz,tz);
      mxx=fmax(mxx,tx);mxy=fmax(mxy,ty);mxz=fmax(mxz,tz);
    }
    instVerts.push_back(std::move(verts));
    instIdx.push_back(m->idx);
    instUvs.push_back(m->uv0);
  }
  for(size_t i=0;i<terrVerts.size();i+=3){
    mnx=fmin(mnx,terrVerts[i]);mny=fmin(mny,terrVerts[i+1]);mnz=fmin(mnz,terrVerts[i+2]);
    mxx=fmax(mxx,terrVerts[i]);mxy=fmax(mxy,terrVerts[i+1]);mxz=fmax(mxz,terrVerts[i+2]);
  }
  printf("网格加载: %d 成功, %d 缺失; 场景 AABB=(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n", loaded, missing, mnx,mny,mnz,mxx,mxy,mxz);
  if(!instVerts.size() && terrVerts.empty()) return 1;

  // --- 交互预览导出模式: scene_compose <清单> <dirs> <out.json> <bst> <texDir> --export [skip] ---
  if(argc>=7 && !strcmp(argv[6],"--export")){
    int skip = argc>=8 ? atoi(argv[7]) : 4;
    FILE* out=fopen(argv[3],"w");
    if(!out){ fprintf(stderr,"导出失败\n"); return 1; }
    fprintf(out,"{\"name\":\"%s\",\"tri\":[", argv[3]);
    bool first=true; long long cnt=0;
    for(size_t ii=0;ii<instVerts.size();ii++){
      if(ii%skip) continue;
      const auto& vp=instVerts[ii]; const auto& ip=instIdx[ii];
      int cr=200,cg=200,cb=200;
      if(insts[ii].hasCol){ cr=(int)(255*insts[ii].col[0]); cg=(int)(255*insts[ii].col[1]); cb=(int)(255*insts[ii].col[2]); }
      for(size_t t=0;t+2<ip.size();t+=3){
        uint32_t i0=ip[t],i1=ip[t+1],i2=ip[t+2];
        if(i0*3+2>=vp.size()||i1*3+2>=vp.size()||i2*3+2>=vp.size()) continue;
        if(!first) fprintf(out,","); first=false; cnt++;
        fprintf(out,"[%.1f,%.1f,%.1f,%d,%d,%d,%.1f,%.1f,%.1f,%d,%d,%d,%.1f,%.1f,%.1f,%d,%d,%d]",
          vp[i0*3],vp[i0*3+1],vp[i0*3+2],cr,cg,cb,
          vp[i1*3],vp[i1*3+1],vp[i1*3+2],cr,cg,cb,
          vp[i2*3],vp[i2*3+1],vp[i2*3+2],cr,cg,cb);
      }
    }
    fprintf(out,"],\"water\":[");
    bool wf=true;
    for(auto&w:waters){
      if(!w.render) continue;
      if(!wf) fprintf(out,","); wf=false;
      fprintf(out,"[%.1f,%.1f,%.1f,%.1f,%.1f]",w.x,w.y,w.z,w.sx,w.sz);
    }
    fprintf(out,"]}");
    fclose(out);
    printf("导出 %lld 三角形 -> %s\n", cnt, argv[3]);
    return 0;
  }

  int W=1920,H=1080;
  // 主体范围: 10%~90% 分位 (忽略离群点)
  auto pct=[&](std::vector<float>&vals,float q){
    std::sort(vals.begin(),vals.end());
    return vals[(size_t)(vals.size()*q)];
  };
  std::vector<float> xs,ys,zs;
  for(size_t i=0;i<instVerts.size();i++) for(size_t j=0;j<instVerts[i].size();j+=3){ xs.push_back(instVerts[i][j]); ys.push_back(instVerts[i][j+1]); zs.push_back(instVerts[i][j+2]); }
  for(size_t i=0;i<terrVerts.size();i+=3){ xs.push_back(terrVerts[i]); ys.push_back(terrVerts[i+1]); zs.push_back(terrVerts[i+2]); }
  float qmnx=pct(xs,0.08f),qmnz=pct(zs,0.08f),qmxx=pct(xs,0.92f),qmxz=pct(zs,0.92f);
  float qmny=pct(ys,0.05f),qmxy=pct(ys,0.95f);
  float cx=(qmnx+qmxx)/2,cy=(qmny+qmxy)/2,cz=(qmnz+qmxz)/2;
  float ext=fmax(qmxx-qmnx,fmax(qmxy-qmny,qmxz-qmnz))+1e-6f;
  float dist=ext*0.42f;   // 近景玩家视角 (光遇实际视野), 原 ext*2.0 全景太碎
  float ca=0.9f;
  float eyeX=cx+dist*sin(ca)*0.6f, eyeY=cy+dist*0.42f, eyeZ=cz+dist*cos(ca)*0.6f;
  float lookX=cx,lookY=cy+ext*0.12f,lookZ=cz;
  // --cam=x,y,z,yaw,pitch 近景模式 (看草丛/细节)
  for(int a=1;a<argc;a++) if(!strncmp(argv[a],"--cam=",6)){
    float v[5];
    if(sscanf(argv[a]+6,"%f,%f,%f,%f,%f",&v[0],&v[1],&v[2],&v[3],&v[4])==5){
      eyeX=v[0]; eyeY=v[1]; eyeZ=v[2];
      lookX=eyeX+cosf(v[4])*sinf(v[3]); lookY=eyeY+sinf(v[4]); lookZ=eyeZ+cosf(v[4])*cosf(v[3]);
    }
  }
  float fw[3]={lookX-eyeX,lookY-eyeY,lookZ-eyeZ}; float fl=sqrt(fw[0]*fw[0]+fw[1]*fw[1]+fw[2]*fw[2]); fw[0]/=fl;fw[1]/=fl;fw[2]/=fl;
  float up[3]={0,1,0};
  float rt[3]={up[1]*fw[2]-up[2]*fw[1],up[2]*fw[0]-up[0]*fw[2],up[0]*fw[1]-up[1]*fw[0]};
  float rl=sqrt(rt[0]*rt[0]+rt[1]*rt[1]+rt[2]*rt[2]); rt[0]/=rl;rt[1]/=rl;rt[2]/=rl;
  float u2[3]={fw[1]*rt[2]-fw[2]*rt[1],fw[2]*rt[0]-fw[0]*rt[2],fw[0]*rt[1]-fw[1]*rt[0]};
  float fov=0.9f;
  std::vector<double> zb(W*H,1e30);
  long long trisTotal=0, trisDrawn=0, trisCulled=0;
  std::vector<uint8_t> fb(W*H*3,0);
  static int dumpCnt=0;
  // 天空渐变 (官方 EnvNode tint 四段: 顶->地平线, ×3 显示增益)
  for(int y=0;y<H;y++){
    float tt=1.0f-(float)y/H; // 1=顶, 0=地平线
    float r0,g0,b0;
    if(gEnv.has){
      const float* a; const float* b2;
      if(tt>0.75f){ float k=(tt-0.75f)/0.25f; a=gEnv.tintMidTop; b2=gEnv.tintTop; r0=(a[0]+(b2[0]-a[0])*k); g0=(a[1]+(b2[1]-a[1])*k); b0=(a[2]+(b2[2]-a[2])*k); }
      else if(tt>0.25f){ float k=(tt-0.25f)/0.5f; a=gEnv.tintMidBot; b2=gEnv.tintMidTop; r0=(a[0]+(b2[0]-a[0])*k); g0=(a[1]+(b2[1]-a[1])*k); b0=(a[2]+(b2[2]-a[2])*k); }
      else { float k=tt/0.25f; a=gEnv.tintBot; b2=gEnv.tintMidBot; r0=(a[0]+(b2[0]-a[0])*k); g0=(a[1]+(b2[1]-a[1])*k); b0=(a[2]+(b2[2]-a[2])*k); }
      // 官方 tint 数值即官方天空色, 不增益 (×3/×1.5 均会饱和偏色)
      int r=(int)(r0*255.f), g=(int)(g0*255.f), b=(int)(b0*255.f);
      if(r>255)r=255; if(g>255)g=255; if(b>255)b=255;
      for(int x=0;x<W;x++){ size_t o=((size_t)y*W+x)*3; fb[o]=r;fb[o+1]=g;fb[o+2]=b; }
    } else {
      int r=(int)((0.30f+0.50f*tt)*255.f), g=(int)((0.36f+0.40f*tt)*255.f), b=(int)((0.52f+0.32f*tt)*255.f);
      for(int x=0;x<W;x++){ size_t o=((size_t)y*W+x)*3; fb[o]=r;fb[o+1]=g;fb[o+2]=b; }
    }
  }
  auto project=[&](float x,float y,float z,int&sx,int&sy,double&sd)->bool{
    float vx=rt[0]*(x-eyeX)+rt[1]*(y-eyeY)+rt[2]*(z-eyeZ);
    float vy=u2[0]*(x-eyeX)+u2[1]*(y-eyeY)+u2[2]*(z-eyeZ);
    float vz=fw[0]*(x-eyeX)+fw[1]*(y-eyeY)+fw[2]*(z-eyeZ);
    if(vz<0.05f) return false;
    float px=vx/(vz*fov), py=vy/(vz*fov);
    sx=(int)(W/2+px*W/2); sy=(int)(H/2-py*H/2);
    sd=vz;
    return sx>=0&&sx<W&&sy>=0&&sy<H;
  };
  int pal[][3]={{200,160,120},{120,200,140},{160,140,220},{220,200,110},{140,180,220},{210,140,180},{180,220,150},{150,150,150}};
  // 雾: 官方 EnvNode cheapFog 数值 (fogNear/far + fogTint 蓝绿, lumRed 降亮)
  float fogN=dist*0.22f, fogF=dist*1.05f;
  int fogR=185,fogG=198,fogB=228;
  if(gEnv.has){
    fogN=gEnv.fogNear; fogF=gEnv.fogFar;
    if(gEnv.fogFar>0){ fogR=(int)(gEnv.fogTintMidBot[0]*1.0f*(1.f-gEnv.fogLumRed)*255.f); fogG=(int)(gEnv.fogTintMidBot[1]*1.0f*(1.f-gEnv.fogLumRed)*255.f); fogB=(int)(gEnv.fogTintMidBot[2]*1.0f*(1.f-gEnv.fogLumRed)*255.f); }
    if(fogR>255)fogR=255; if(fogG>255)fogG=255; if(fogB>255)fogB=255;
  }
  // 地形颜色 = 官方 GEO0 顶点色 (in3 RGBA8, 按材质均值) × 烘焙光 (in2.r)
  float in3mean[256][3]={0}; int in3cnt[256]={0};
  for(size_t i=0;i<terrVerts.size()/3;i++){
    uint8_t m=terr.vmat.size()>i?terr.vmat[i]:18;
    if(terr.uv1.size()>i*4+2){ in3mean[m][0]+=terr.uv1[i*4]; in3mean[m][1]+=terr.uv1[i*4+1]; in3mean[m][2]+=terr.uv1[i*4+2]; in3cnt[m]++; }
  }
  {
    float lx=0.35f,ly=0.85f,lz=0.4f;
    if(gEnv.has){ float az=gEnv.sunAz*0.0174533f, el=gEnv.sunEl*0.0174533f; lx=cosf(az)*cosf(el); ly=sinf(el); lz=sinf(az)*cosf(el); }
    float ll=sqrt(lx*lx+ly*ly+lz*lz); lx/=ll;ly/=ll;lz/=ll;
    for(size_t t=0;t+2<terrIdx.size();t+=3){
      uint32_t i0=terrIdx[t],i1=terrIdx[t+1],i2=terrIdx[t+2];
      if(i0>=terrVerts.size()/3||i1>=terrVerts.size()/3||i2>=terrVerts.size()/3) continue;
      int x0,y0,x1,y1,x2,y2; double d0,d1,d2;
      if(!project(terrVerts[i0*3],terrVerts[i0*3+1],terrVerts[i0*3+2],x0,y0,d0)) continue;
      if(!project(terrVerts[i1*3],terrVerts[i1*3+1],terrVerts[i1*3+2],x1,y1,d1)) continue;
      if(!project(terrVerts[i2*3],terrVerts[i2*3+1],terrVerts[i2*3+2],x2,y2,d2)) continue;
      float ax=terrVerts[i0*3],ay=terrVerts[i0*3+1],az=terrVerts[i0*3+2],bx=terrVerts[i1*3],by=terrVerts[i1*3+1],bz=terrVerts[i1*3+2],cx2=terrVerts[i2*3],cy2=terrVerts[i2*3+1],cz2=terrVerts[i2*3+2];
      float ux=bx-ax,uy=by-ay,uz=bz-az,vx=cx2-ax,vy=cy2-ay,vz=cz2-az;
      float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
      float nl=sqrt(nx*nx+ny*ny+nz*nz); if(nl>1e-9){nx/=nl;ny/=nl;nz/=nl;}
      float dif=fmax(0.f,nx*lx+ny*ly+nz*lz);
      uint8_t mid = terr.vmat.size()>i0 ? terr.vmat[i0] : 18;
  // 官方烘焙光: GEO0 顶点 in2.r (RGBA8, 0..1)
      float lightB = 0.7f;
      if(terr.uv0.size()>(size_t)(i0*2)) lightB = terr.uv0[i0*2];
      int sh=(int)(lightB*255); sh=std::min(sh,255);
      float cr=1.f,cg=1.f,cb=1.f;
      if(in3cnt[mid]>0){ cr=in3mean[mid][0]/in3cnt[mid]; cg=in3mean[mid][1]/in3cnt[mid]; cb=in3mean[mid][2]/in3cnt[mid]; }
      int tr=(int)(cr*255)*sh/255, tg=(int)(cg*255)*sh/255, tb=(int)(cb*255)*sh/255;
      if(bakevis){ int gv=(int)((lightB*1.8f-0.6f)*255); if(gv<0)gv=0; if(gv>255)gv=255; tr=tg=tb=gv; }
      if(dumpPath && dumpCnt<200000){
        static FILE* df=nullptr;
        if(!df){ df=fopen(dumpPath,"w"); if(df) fprintf(df,"lb,lb2,gi,gi2,dif,vmat\n"); }
        float lb2=0.5f, gi2=0.5f;
        if(terr.uv0.size()>(size_t)(i0*2+1)) lb2=terr.uv0[i0*2+1];
        if(terr.uv1.size()>(size_t)(i0*2+1)) gi2=terr.uv1[i0*2+1];
        if(df){ fprintf(df,"%.4f,%.4f,%.4f,%.4f,%.4f,%d\n",lightB,lb2,gi2,gi2,dif,mid); dumpCnt++; }
      }
      int miny=std::max(0,std::min(std::min(y0,y1),y2)), maxy=std::min(H-1,std::max(std::max(y0,y1),y2));
      // 材质→官方贴图 (triplanar 投影)
      const Tex* terrTex2=nullptr;
      // 官方材质绑定: 从 LevelMaterial 读取 diffuse 贴图 (数据驱动, 每关自动)
      auto mbit=matTex.find(mid);
      if(mbit!=matTex.end() && !mbit->second.empty()){
        auto tit2=texCache.find(mbit->second);
        if(tit2!=texCache.end()) terrTex2=&tit2->second;
      }
          const float tscale=0.006f;
      for(int y=miny;y<=maxy;y++){
        float xs2[6]; int cnt=0; float zs2[6]; float ws2[18];
        auto inter=[&](int ax2,int ay2,float az2,float awx,float awy,float awz,int bx2,int by2,float bz2,float bwx,float bwy,float bwz){
          if(ay2==by2) return;
          float tt=(float)(y-ay2)/(by2-ay2);
          if(tt>=0&&tt<=1){
            xs2[cnt]=ax2+tt*(bx2-ax2); zs2[cnt]=az2+tt*(bz2-az2);
            ws2[cnt*3]=awx+tt*(bwx-awx); ws2[cnt*3+1]=awy+tt*(bwy-awy); ws2[cnt*3+2]=awz+tt*(bwz-awz);
            cnt++;
          }
        };
        inter(x0,y0,d0,ax,ay,az,x1,y1,d1,bx,by,bz); inter(x1,y1,d1,bx,by,bz,x2,y2,d2,cx2,cy2,cz2); inter(x2,y2,d2,cx2,cy2,cz2,x0,y0,d0,ax,ay,az);
        if(cnt<2) continue;
        if(xs2[0]>xs2[1]){
          float tt=xs2[0];xs2[0]=xs2[1];xs2[1]=tt; tt=zs2[0];zs2[0]=zs2[1];zs2[1]=tt;
          for(int k=0;k<3;k++){ float t2=ws2[k]; ws2[k]=ws2[3+k]; ws2[3+k]=t2; }
        }
        int xa=std::max(0,(int)xs2[0]), xb=std::min(W-1,(int)xs2[1]);
        for(int x=xa;x<=xb;x++){
          float tt=(xs2[1]>xs2[0])?(x-xs2[0])/(xs2[1]-xs2[0]):0;
          float zd=zs2[0]+tt*(zs2[1]-zs2[0]);
          int o=y*W+x;
          if(zd<zb[o]){ zb[o]=zd;
            int fr=tr,fg=tg,fbt=tb;
            if(terrTex2){
              float wwx=ws2[0]+tt*(ws2[3]-ws2[0]), wwy=ws2[1]+tt*(ws2[4]-ws2[1]), wwz=ws2[2]+tt*(ws2[5]-ws2[2]);
              float uu,vv;
              if(fabs(ny)>=fabs(nx)&&fabs(ny)>=fabs(nz)){ uu=wwx*tscale; vv=wwz*tscale; }
              else if(fabs(nx)>=fabs(nz)){ uu=wwy*tscale; vv=wwz*tscale; }
              else { uu=wwx*tscale; vv=wwy*tscale; }
              uint8_t qr,qg,qb; sampleTex(*terrTex2,uu,vv,qr,qg,qb);
              fr=(int)(tr*qr/255); fg=(int)(tg*qg/255); fbt=(int)(tb*qb/255);
            }
            float fog=(zd-fogN)/(fogF-fogN); if(fog<0)fog=0; if(fog>1)fog=1; if(bakevis) fog=0;
            if(mid==80) fog*=0.45f; // 云材质: 雾影响减半, 保持云白亮层次
            fb[o*3]=(uint8_t)(fr*(1-fog)+fogR*fog);
            fb[o*3+1]=(uint8_t)(fg*(1-fog)+fogG*fog);
            fb[o*3+2]=(uint8_t)(fbt*(1-fog)+fogB*fog);
          }
        }
      }
    }
  }
  size_t idx=0;
  std::vector<bool> mirroredList;
  for(auto&in:insts){
    const float* M=in.mat;
    float det = M[0]*(M[4]*M[8]-M[5]*M[7]) - M[3]*(M[1]*M[8]-M[2]*M[7]) + M[6]*(M[1]*M[5]-M[2]*M[4]);
    mirroredList.push_back(det<0);
  }
  for(size_t ii=0;ii<insts.size();ii++){
    if(idx>=instVerts.size()) break;
    const auto& vp=instVerts[idx]; const auto& ip=instIdx[idx]; const auto& uv0 = idx<instUvs.size()?instUvs[idx]:std::vector<float>();
    const Tex* tex = nullptr;
    if(!insts[ii].tex.empty()){ auto it=texCache.find(insts[ii].tex); if(it!=texCache.end()) tex=&it->second; }
    if(!tex && !insts[ii].tex2.empty()){ auto it=texCache.find(insts[ii].tex2); if(it!=texCache.end()) tex=&it->second; }
    idx++;
    int pc=ii%8;
    float lx=0.3f,ly=0.7f,lz=0.6f;
    bool mir = mirroredList[ii]; float ll=sqrt(lx*lx+ly*ly+lz*lz); lx/=ll;ly/=ll;lz/=ll;
    for(size_t t=0;t+2<ip.size();t+=3){
      uint32_t i0=ip[t],i1=ip[t+1],i2=ip[t+2];
      if(i0>=vp.size()/3||i1>=vp.size()/3||i2>=vp.size()/3) continue;
      int x0,y0,x1,y1,x2,y2; double d0,d1,d2;
      if(!project(vp[i0*3],vp[i0*3+1],vp[i0*3+2],x0,y0,d0)) continue;
      if(!project(vp[i1*3],vp[i1*3+1],vp[i1*3+2],x1,y1,d1)) continue;
      if(!project(vp[i2*3],vp[i2*3+1],vp[i2*3+2],x2,y2,d2)) continue;
      trisTotal++;
      float cr=(float)((x1-x0)*(y2-y0)-(y1-y0)*(x2-x0));
      bool cw = cr<=0;
      if(mir ? !cw : cw){ trisCulled++; continue; }
      trisDrawn++;
      float ax=vp[i0*3],ay=vp[i0*3+1],az=vp[i0*3+2],bx=vp[i1*3],by=vp[i1*3+1],bz=vp[i1*3+2],cx2=vp[i2*3],cy2=vp[i2*3+1],cz2=vp[i2*3+2];
      float ux=bx-ax,uy=by-ay,uz=bz-az,vx=cx2-ax,vy=cy2-ay,vz=cz2-az;
      float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
      float nl=sqrt(nx*nx+ny*ny+nz*nz); if(nl>1e-9){nx/=nl;ny/=nl;nz/=nl;}
      float dif=fmax(0.f,nx*lx+ny*ly+nz*lz);
      float amb=0.30f; int sh=(int)((amb+0.7f*dif)*255); sh=std::min(sh,255);
      float ua=0,ub=0,uc=0,va=0,vb=0,vc=0;
      if(tex && uv0.size()>=(size_t)(i0*2+2) && uv0.size()>=(size_t)(i1*2+2) && uv0.size()>=(size_t)(i2*2+2)){
        ua=uv0[i0*2]; va=uv0[i0*2+1]; ub=uv0[i1*2]; vb=uv0[i1*2+1]; uc=uv0[i2*2]; vc=uv0[i2*2+1];
      }
      int miny=std::max(0,std::min(std::min(y0,y1),y2)), maxy=std::min(H-1,std::max(std::max(y0,y1),y2));
      for(int y=miny;y<=maxy;y++){
        float xs[6]; int cnt=0; float zs[6]; float us[6], vs[6];
        auto inter=[&](int ax2,int ay2,float az2,float au,float av,int bx2,int by2,float bz2,float bu,float bv){
          if(ay2==by2) return;
          float t=(float)(y-ay2)/(by2-ay2);
          if(t>=0&&t<=1){ xs[cnt]=ax2+t*(bx2-ax2); zs[cnt]=az2+t*(bz2-az2); us[cnt]=au+t*(bu-au); vs[cnt]=av+t*(bv-av); cnt++; }
        };
        inter(x0,y0,d0,ua,va,x1,y1,d1,ub,vb); inter(x1,y1,d1,ub,vb,x2,y2,d2,uc,vc); inter(x2,y2,d2,uc,vc,x0,y0,d0,ua,va);
        if(cnt<2) continue;
        if(xs[0]>xs[1]){ float t=xs[0];xs[0]=xs[1];xs[1]=t; t=zs[0];zs[0]=zs[1];zs[1]=t; t=us[0];us[0]=us[1];us[1]=t; t=vs[0];vs[0]=vs[1];vs[1]=t; }
        int xa=std::max(0,(int)xs[0]), xb=std::min(W-1,(int)xs[1]);
        for(int x=xa;x<=xb;x++){
          float tt=(xs[1]>xs[0])?(x-xs[0])/(xs[1]-xs[0]):0;
          float zd=zs[0]+tt*(zs[1]-zs[0]);
          int o=y*W+x;
          if(zd<zb[o]){
            zb[o]=zd;
            float fog=(zd-fogN)/(fogF-fogN); if(fog<0)fog=0; if(fog>1)fog=1; if(bakevis) fog=0;
            int cr,cg,cb;
            if(tex){
              float uu=us[0]+tt*(us[1]-us[0]), vv=vs[0]+tt*(vs[1]-vs[0]);
              uint8_t tr,tg,tb; sampleTex(*tex,uu,vv,tr,tg,tb);
              if(insts[ii].hasCol){
                tr=(uint8_t)(tr*insts[ii].col[0]); tg=(uint8_t)(tg*insts[ii].col[1]); tb=(uint8_t)(tb*insts[ii].col[2]);
              }
              if(insts[ii].lit>0){
                float lb=1.f+insts[ii].lit*0.8f;
                tr=(uint8_t)(tr*lb); tg=(uint8_t)(tg*lb); tb=(uint8_t)(tb*lb);
                if(tr>255)tr=255; if(tg>255)tg=255; if(tb>255)tb=255;
              }
              cr=tr*sh/255; cg=tg*sh/255; cb=tb*sh/255;
              if(bakevis){ int gv=sh; cr=cg=cb=gv; }
            } else {
              cr=pal[pc][0]; cg=pal[pc][1]; cb=pal[pc][2];
              if(insts[ii].hasCol){
                cr=(int)(255*insts[ii].col[0]); cg=(int)(255*insts[ii].col[1]); cb=(int)(255*insts[ii].col[2]);
                if(cr>255)cr=255; if(cg>255)cg=255; if(cb>255)cb=255;
              }
              cr=cr*sh/255; cg=cg*sh/255; cb=cb*sh/255;
              if(bakevis){ int gv=sh; cr=cg=cb=gv; }
            }
            if(insts[ii].alphaBlend){
              // 半透明云/发光体: 与背景混合
              float a=0.5f;
              float fr2=cr*(1-fog)+fogR*fog, fg2=cg*(1-fog)+fogG*fog, fb2=cb*(1-fog)+fogB*fog;
              fb[o*3]=(uint8_t)(fb[o*3]*(1-a)+fr2*a);
              fb[o*3+1]=(uint8_t)(fb[o*3+1]*(1-a)+fg2*a);
              fb[o*3+2]=(uint8_t)(fb[o*3+2]*(1-a)+fb2*a);
            } else {
              fb[o*3]=(uint8_t)(cr*(1-fog)+fogR*fog);
              fb[o*3+1]=(uint8_t)(cg*(1-fog)+fogG*fog);
              fb[o*3+2]=(uint8_t)(cb*(1-fog)+fogB*fog);
            }
          }
        }
      }
    }
  }
  // === 官方水面 (Water 节点: 半透明青绿平面, 每关数据驱动) ===
  const Tex* waterTex=nullptr;
  { auto it=texCache.find("WaterfallTex"); if(it!=texCache.end()) waterTex=&it->second; }
  for(auto&w:waters){
    if(!w.render) continue;
    float ax=w.x-w.sx, az=w.z-w.sz, bx=w.x+w.sx, bz=w.z-w.sz, cx2=w.x+w.sx, cz2=w.z+w.sz, dx=w.x-w.sx, dz=w.z+w.sz;
    int x0,y0,x1,y1,x2,y2,x3,y3; double d0,d1,d2,d3;
    if(!project(ax,w.y,az,x0,y0,d0)) continue;
    if(!project(bx,w.y,bz,x1,y1,d1)) continue;
    if(!project(cx2,w.y,cz2,x2,y2,d2)) continue;
    if(!project(dx,w.y,dz,x3,y3,d3)) continue;
    int tris[2][3]={{0,1,2},{0,2,3}};
    for(int pass=0;pass<2;pass++){
      int vx[3],vy[3]; float vd[3],vwx[3],vwz[3];
      for(int k=0;k<3;k++){
        int idx=tris[pass][k];
        int wx2[4]={x0,x1,x2,x3}, wy2[4]={y0,y1,y2,y3};
        float wd2[4]={d0,d1,d2,d3}, wwx2[4]={ax,bx,cx2,dx}, wwz2[4]={az,bz,cz2,dz};
        vx[k]=wx2[idx]; vy[k]=wy2[idx]; vd[k]=wd2[idx]; vwx[k]=wwx2[idx]; vwz[k]=wwz2[idx];
      }
      int miny=std::max(0,std::min(std::min(vy[0],vy[1]),vy[2])), maxy=std::min(H-1,std::max(std::max(vy[0],vy[1]),vy[2]));
      for(int y=miny;y<=maxy;y++){
        float xs2[6]; float zs2[6]; float wsx[6]; float wsz[6]; int cnt=0;
        auto inter=[&](int ax2,int ay2,float az2,float awx,float awz,int bx2,int by2,float bz2,float bwx,float bwz){
          if(ay2==by2) return;
          float tt=(float)(y-ay2)/(by2-ay2);
          if(tt>=0&&tt<=1){
            xs2[cnt]=ax2+tt*(bx2-ax2); zs2[cnt]=az2+tt*(bz2-az2);
            wsx[cnt]=awx+tt*(bwx-awx); wsz[cnt]=awz+tt*(bwz-awz);
            cnt++;
          }
        };
        inter(vx[0],vy[0],vd[0],vwx[0],vwz[0],vx[1],vy[1],vd[1],vwx[1],vwz[1]);
        inter(vx[1],vy[1],vd[1],vwx[1],vwz[1],vx[2],vy[2],vd[2],vwx[2],vwz[2]);
        inter(vx[2],vy[2],vd[2],vwx[2],vwz[2],vx[0],vy[0],vd[0],vwx[0],vwz[0]);
        if(cnt<2) continue;
        if(xs2[0]>xs2[1]){
          float t=xs2[0];xs2[0]=xs2[1];xs2[1]=t; t=zs2[0];zs2[0]=zs2[1];zs2[1]=t;
          t=wsx[0];wsx[0]=wsx[1];wsx[1]=t; t=wsz[0];wsz[0]=wsz[1];wsz[1]=t;
        }
        int xa=std::max(0,(int)xs2[0]), xb=std::min(W-1,(int)xs2[1]);
        for(int x=xa;x<=xb;x++){
          float tt=(xs2[1]>xs2[0])?(x-xs2[0])/(xs2[1]-xs2[0]):0;
          float zd=zs2[0]+tt*(zs2[1]-zs2[0]);
          int o=y*W+x;
          if(zd<zb[o]){
            zb[o]=zd;
            // 官方水色 (WaterfallTex 主色) + 贴图采样调制
            int wr=105,wg=145,wb=134;
            if(waterTex){
              float wu=(wsx[0]+tt*(wsx[1]-wsx[0]))*0.06f, wv=(wsz[0]+tt*(wsz[1]-wsz[0]))*0.06f;
              uint8_t tr,tg,tb; sampleTex(*waterTex,wu,wv,tr,tg,tb);
              wr=(int)(wr*tr/255); wg=(int)(wg*tg/255); wb=(int)(wb*tb/255);
            }
            // 半透明水面: 混合已有背景
            float a=0.55f;
            fb[o*3]=(uint8_t)(fb[o*3]*(1-a)+wr*a);
            fb[o*3+1]=(uint8_t)(fb[o*3+1]*(1-a)+wg*a);
            fb[o*3+2]=(uint8_t)(fb[o*3+2]*(1-a)+wb*a);
          }
        }
      }
    }
  }
  writeOut(outPath, fb, W, H);
  printf("三角形: 总%lld 剔除%lld 绘制%lld\n", trisTotal, trisCulled, trisDrawn);
  printf("输出 %s (%dx%d)\n", outPath, W, H);
  return 0;
}
