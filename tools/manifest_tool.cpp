// SkyEngine manifest tool: generate manifest from resource directory
#include "../engine/resource/manifest.h"
#include "../engine/core/log.h"
#include <cstdio>
#include <fstream>

int main(int argc,char** argv){
  if(argc<2){
    std::printf("usage: manifest_tool <assets_dir> [out_file]\n"
 " out_file default assets.manifest\n");
    return 1;
  }
  std::string dir=argv[1];
  std::string out=argc>2? argv[2] : "assets.manifest";
  sky::Manifest m=sky::build_manifest(dir);
  std::ofstream f(out);
  f<<m.serialize();
  sky::log_msg(sky::LOG_INFO,"manifest","built %zu entries -> %s",m.size(),out.c_str());
  return 0;
}
