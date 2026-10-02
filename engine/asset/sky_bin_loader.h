// sky_bin_loader.h — 真实 Sky 二进制格式加载器（依据字节级逆向 + so meshopt 证据）
// 覆盖：*.mesh（新头+段表+节点表）、BstBaked.meshes（LVL0= v3 容器）、Objects.level.bin（TGCL）
// 压缩层：meshoptimizer（so 符号 meshopt_decodeVertexBuffer/decodeIndexBuffer 实锤）
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sky {

// ---------- .mesh 单文件 ----------
struct SkyMeshHeader {
  std::string name;            // 0x00 名字（32B pad）
  uint32_t num_lods = 0;       // 0x44
  uint32_t flags = 0;          // 0x48（1=有骨骼/动画，0=静态帧）
  uint32_t field_0x4c = 0;     // 0x4c（0x00010000）
  uint32_t seg[4] = {0,0,0,0}; // 0x50 段表：4×[u32 0][u16 相对偏移]；段k+1@段k+seg[k]；0x7f80=哨兵
  std::vector<std::string> nodes;   // 节点/骨骼名字表（132B/条目：32B 名+100B 数据）
  size_t pose_start = 0x60;         // 姿态/绑定 float 区起点
  bool is_compressed = false;       // 名字带 Zip/Strip 标志 → 数据区为量化/压缩
  bool has_anim = false;            // 段1 = 内嵌 animpack（StripAnim 变体）
};
bool probe_sky_mesh(const std::string& path, SkyMeshHeader& out, std::string* err);

// ---------- BstBaked.meshes（LevelData 容器，烘焙网格） ----------
struct BstBlock { char tag[4]; uint32_t offset = 0, size = 0; };
struct BstBakedHeader {
  uint32_t version = 0;              // 0x05 处 u32（=0x3d 61，so 写入器 movk #0x3d lsl32）
  uint32_t block_count = 0;          // 0x09 处 u32（=3）
  std::vector<BstBlock> blocks;      // 0x0d 起块表 [4B 标签][u32 偏移][u32 大小]
  uint32_t geo_off = 0, geo_val = 0; // GEO0 段
  uint32_t lod_val = 0;              // LOD0 锚点
  float aabb_min[3], aabb_max[3];    // 0x70 场景包围盒
  uint32_t block_sizes[6];           // 0x88
  uint32_t tail_catalog[16];         // 文件尾 64B 物理目录
  std::vector<uint16_t> tail_indices;// 尾部三角形索引流（u16）
  size_t data_start = 0xa8;          // 压缩网格数据流起点
};
bool probe_bst_baked(const std::string& path, BstBakedHeader& out, std::string* err);

// ---------- Objects.level.bin（TGCL） ----------
struct TgclHeader {
  uint32_t version = 0;              // 0x04
  std::vector<uint32_t> seg_table;   // 0x08 起段偏移/大小表
  size_t string_table_start = 0;     // =0xf784
  size_t string_count = 0;           // 64,029
  std::vector<std::string> strings_sample;
};
bool probe_tgcl_level(const std::string& path, TgclHeader& out, std::string* err);

// ---------- meshoptimizer 解码封装（so 同源） ----------
// 返回 0=成功（同 meshopt 约定；-1/-2/-3 由调用方按格式文档解释）
int sky_decode_vertex_buffer(void* dest, size_t vertex_count, size_t vertex_size,
                             const uint8_t* data, size_t data_size);
int sky_decode_index_buffer(void* dest, size_t index_count, size_t index_size,
                            const uint8_t* data, size_t data_size);

} // namespace sky
