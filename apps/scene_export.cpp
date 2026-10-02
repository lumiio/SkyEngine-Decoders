// scene_export: level geometry export to JSON (decimated, for interactive HTML preview)
// usage: scene_export <instances.txt> <meshDir1,meshDir2> <out.json> [bst.meshes] [texDir] [decimate]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include "../engine/asset/sky_mesh_loader.h"
using namespace sky;

struct Instance { std::string name; float mat[16]; std::string tex; std::string tex2; float col[3]; bool hasCol=false; float lit=0; };
struct Water { float x,y,z,sx,sz; int render=1; };

static const char* MESH_VARIANTS[] = {
  "", "_CompOcc", "_ZipPos", "_ZipUvs", "_StripAnim_CompOcc_ZipPos_ZipUvs_StripNorm",
  "_StripAnim", "_CompOcc_ZipPos", "_CompOcc_ZipUvs", "_ZipPos_ZipUvs", "_StripAnim_CompOcc_ZipPos_ZipUvs",
  "_StripAnim_CompOcc_ZipUvs", "_CompOcc_ZipPos_ZipUvs_StripNorm", "_StripNorm", "_ZipPos_StripNorm"
};

static std::shared_ptr<RenderMesh> loadMeshData(const std::string& name, const std::string& d1, const std::string& d2){
  // try both flat (meshes/xxx.mesh) and Bin/-prefixed layouts
  const std::string prefixes[2] = {"", "Bin/"};
  for(const std::string& pre : prefixes){
    for(const char* v:MESH_VARIANTS){
      std::string p = pre + name + v + ".mesh";
      std::string err;
      if(d1.size()){ auto m=load_sky_mesh(d1+"/"+p, &err); if(m) return m; }
      if(d2.size()){ auto m=load_sky_mesh(d2+"/"+p, &err); if(m) return m; }
    }
  }
  return nullptr;
}

int main(int argc,char**argv){
 if(argc<4){ fprintf(stderr,"usage: scene_export <instances.txt> <meshDirs> <out.json> [bst.meshes] [texDir] [decimate]\n");return 1;}
 FILE* f=fopen(argv[1],"r");if(!f){ fprintf(stderr,"manifest open failed\n");return 1;}
  std::vector<Instance> insts; std::vector<Water> waters;
  std::string curName,curTex,curTex2; float curMat[16]; float curCol[3]={1,1,1}; bool curHasCol=false; float curLit=0;
  char line[2048];
  while(fgets(line,sizeof(line),f)){
    if(line[0]=='[' && strchr(line,']')){
      char* lb=strchr(line,']'); char* nm=lb+1; while(*nm==' '||*nm=='\t') nm++;
      char* bar=strchr(nm,'|'); if(bar) *bar=0;
      char* sp=strchr(nm,' '); if(sp) *sp=0;
      size_t l=strlen(nm); while(l&&isspace((unsigned char)nm[l-1])) nm[--l]=0;
      curName=nm;
      const char* tx=strstr(line,"tex="); curTex=tx?(tx+4):"White";
      char* tb=strchr((char*)curTex.c_str(),'|'); if(tb)*tb=0;
      while(curTex.size()&&isspace((unsigned char)curTex.back())) curTex.pop_back();
      const char* t2=strstr(line,"tex2=");
      if(t2){ curTex2=t2+5; char* t2b=strchr((char*)curTex2.c_str(),'|'); if(t2b)*t2b=0; while(curTex2.size()&&isspace((unsigned char)curTex2.back())) curTex2.pop_back(); }
      else curTex2.clear();
      const char* cl=strstr(line,"col="); curHasCol=false;
      if(cl && sscanf(cl+4,"%f,%f,%f",&curCol[0],&curCol[1],&curCol[2])==3){ curHasCol=true; }
      const char* lt=strstr(line,"lit="); curLit=lt?atof(lt+4):0;
    } else if(strstr(line,"mat=[") && !curName.empty()){
      char* p=strchr(line,'['); char* e=strchr(line,']');
      if(p&&e){ *e=0; p++;
        char* tok=strtok(p,","); int n=0;
        while(tok&&n<16){ curMat[n++]=atof(tok); tok=strtok(nullptr,","); }
        if(n>=12){ Instance in; in.name=curName; in.tex=curTex; in.tex2=curTex2; in.hasCol=curHasCol; in.lit=curLit; if(curHasCol){in.col[0]=curCol[0];in.col[1]=curCol[1];in.col[2]=curCol[2];} memcpy(in.mat,curMat,64); insts.push_back(in); }
      }
    } else if(strncmp(line,"WATER ",6)==0){
      Water w; if(sscanf(line+6,"%f %f %f %f %f %d",&w.x,&w.y,&w.z,&w.sx,&w.sz,&w.render)>=5){ waters.push_back(w); }
    }
  }
  fclose(f);
 // mesh dirs
  std::string dirs=argv[2]; size_t comma=dirs.find(',');
  std::string d1=dirs.substr(0,comma), d2=comma==std::string::npos?"":dirs.substr(comma+1);
 // decimate
  int skip=argc>=7?atoi(argv[6]):4;
  FILE* out=fopen(argv[3],"w");
 if(!out){ fprintf(stderr,"outputfail\n");return 1;}
  fprintf(out,"{\"name\":\"%s\",\"tri\":[", argv[3]);
  long long count=0; bool first=true;
 // instance geometry (transformed to world)
  long long vi=0;
  for(size_t ii=0;ii<insts.size();ii++){
    const Instance& in=insts[ii];
    auto m=loadMeshData(in.name, d1, d2);
    if(!m || m->positions.empty()) continue;
    if((vi++)%skip) continue;
 // transform: row-major mat = M(mat[0..15]); p' = M * p
    const float* M=in.mat;
 // extractrotate/scale 3x3 + translate
    float r0c0=M[0],r0c1=M[1],r0c2=M[2], r1c0=M[4],r1c1=M[5],r1c2=M[6], r2c0=M[8],r2c1=M[9],r2c2=M[10];
    float tx=M[12],ty=M[13],tz=M[14];
 // color
    int cr=200,cg=200,cb=200;
    if(in.hasCol){ cr=(int)(255*in.col[0]); cg=(int)(255*in.col[1]); cb=(int)(255*in.col[2]); }
    for(size_t t=0;t+2<m->indices.size();t+=3){
      uint32_t i0=m->indices[t],i1=m->indices[t+1],i2=m->indices[t+2];
      if(i0>=m->positions.size()||i1>=m->positions.size()||i2>=m->positions.size()) continue;
      if(!first) fprintf(out,",");
      first=false; count++;
      float px[3],py[3],pz[3];
      uint32_t idxs[3]={i0,i1,i2};
      for(int k=0;k<3;k++){
        const Vec3& v=m->positions[idxs[k]];
        px[k]=r0c0*v.x+r0c1*v.y+r0c2*v.z+tx;
        py[k]=r1c0*v.x+r1c1*v.y+r1c2*v.z+ty;
        pz[k]=r2c0*v.x+r2c1*v.y+r2c2*v.z+tz;
      }
      fprintf(out,"[%.1f,%.1f,%.1f,%d,%d,%d,%.1f,%.1f,%.1f,%d,%d,%d,%.1f,%.1f,%.1f,%d,%d,%d]",px[0],py[0],pz[0],cr,cg,cb,px[1],py[1],pz[1],cr,cg,cb,px[2],py[2],pz[2],cr,cg,cb);
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
 printf("export %lld triangle -> %s\n",count,argv[3]);
  return 0;
}
