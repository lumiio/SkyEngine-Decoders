// sky_mesh_loader.h - Sky: Children of the Light .mesh resource loader
// format notes: TGC .mesh (base variant, variant_flags==0)
#pragma once
#include "../render/backend.h"
#include <memory>
#include <string>
#include <vector>

namespace sky {

// load TGC .mesh file (base variant, variant_flags==0)
// returns render mesh; nullptr on failure with error message
std::shared_ptr<RenderMesh> load_sky_mesh(const std::string& path, std::string* err=nullptr);
std::shared_ptr<RenderMesh> load_sky_mesh_strip(const std::string& path, std::string* err=nullptr);

// mesh stats info (for verification/docs)
struct MeshInfo {
  std::string name;
  uint32_t shared=0, total=0, point=0, uv_count=0, variant_flags=0;
  float aabb[6] = {0,0,0,0,0,0};
  uint32_t compressed=0, uncompressed=0;
};
bool probe_sky_mesh(const std::string& path, MeshInfo& out, std::string* err=nullptr);

} // namespace sky
