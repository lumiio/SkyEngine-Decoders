// sky_mesh_loader.h — Sky: Children of the Light .mesh 资源加载器
// 格式说明：TGC .mesh（基础型，variant_flags==0）
#pragma once
#include "../render/backend.h"
#include <memory>
#include <string>
#include <vector>

namespace sky {

// 加载 TGC .mesh 文件（基础型，variant_flags==0）
// 返回渲染网格；失败时返回 nullptr 并附带错误消息
std::shared_ptr<RenderMesh> load_sky_mesh(const std::string& path, std::string* err=nullptr);
std::shared_ptr<RenderMesh> load_sky_mesh_strip(const std::string& path, std::string* err=nullptr);

// 网格统计信息（供校验/文档）
struct MeshInfo {
  std::string name;
  uint32_t shared=0, total=0, point=0, uv_count=0, variant_flags=0;
  float aabb[6] = {0,0,0,0,0,0};
  uint32_t compressed=0, uncompressed=0;
};
bool probe_sky_mesh(const std::string& path, MeshInfo& out, std::string* err=nullptr);

} // namespace sky
