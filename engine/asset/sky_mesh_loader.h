// sky_mesh_loader.h - Sky: Children of the Light .mesh resource loader
// format notes: TGC .mesh (base variant, variant_flags==0)
// self-contained: depends only on core/math.h (Vec2/Vec3/Color), not on the render backend
#pragma once
#include "../core/math.h"
#include <memory>
#include <string>
#include <vector>

namespace sky {

// decoded mesh output (CPU-side, renderer-agnostic)
struct RenderMesh {
  std::vector<Vec3> positions;
  std::vector<Color> colors;      // per-vertex color
  std::vector<uint32_t> indices;
  std::vector<Vec2> uvs;          // optional lightmap UV
  bool baked=false;
};

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
