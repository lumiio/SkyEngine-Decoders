// sky_bin_loader.h - real Sky binary format loader (byte-level reverse engineering + so meshopt evidence)
// covers: *.mesh (new header + section table + node table), BstBaked.meshes (LVL0 = v3 container), Objects.level.bin (TGCL)
// compression layer: meshoptimizer (so symbols meshopt_decodeVertexBuffer/decodeIndexBuffer)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sky {

// ---------- .mesh single file ----------
struct SkyMeshHeader {
 std::string name;// 0x00 name（32B pad）
  uint32_t num_lods = 0;       // 0x44
  uint32_t flags = 0;// 0x48 (1=has bones/animation, 0=static frame)
  uint32_t field_0x4c = 0;     // 0x4c（0x00010000）
  uint32_t seg[4] = {0,0,0,0};// 0x50 section table: 4x [u32 0][u16 relative offset]; section k+1 = section k + seg[k]; 0x7f80=sentinel
  std::vector<std::string> nodes;// node/bone name table (132B/entry: 32B name + 100B data)
  size_t pose_start = 0x60;// pose/bind float region start
  bool is_compressed = false;// name with Zip/Strip flag -> data region quantized/compressed
  bool has_anim = false;// section1 = embedded animpack (StripAnim variant)
};
bool probe_sky_mesh(const std::string& path, SkyMeshHeader& out, std::string* err);

// ---------- BstBaked.meshes (LevelData container, baked meshes) ----------
struct BstBlock { char tag[4]; uint32_t offset = 0, size = 0; };
struct BstBakedHeader {
  uint32_t version = 0;// u32 at 0x05 (=0x3d 61, so writer movk #0x3d lsl32)
  uint32_t block_count = 0;// u32 at 0x09 (=3)
  std::vector<BstBlock> blocks;// block table from 0x0d [4B tag][u32 offset][u32 size]
 uint32_t geo_off = 0,geo_val = 0;// GEO0 section
  uint32_t lod_val = 0;// LOD0 anchor
 float aabb_min[3],aabb_max[3];// 0x70 sceneAABB
  uint32_t block_sizes[6];           // 0x88
  uint32_t tail_catalog[16];// trailer 64B physical catalog
  std::vector<uint16_t> tail_indices;// tail triangle index stream (u16)
  size_t data_start = 0xa8;// compressed mesh data stream start
};
bool probe_bst_baked(const std::string& path, BstBakedHeader& out, std::string* err);

// ---------- Objects.level.bin（TGCL） ----------
struct TgclHeader {
  uint32_t version = 0;              // 0x04
  std::vector<uint32_t> seg_table;// section offset/size table from 0x08
  size_t string_table_start = 0;     // =0xf784
  size_t string_count = 0;           // 64,029
  std::vector<std::string> strings_sample;
};
bool probe_tgcl_level(const std::string& path, TgclHeader& out, std::string* err);

// ---------- meshoptimizer decode wrapper (so same-source) ----------
// return 0=success (same meshopt convention; -1/-2/-3 interpreted by caller per format docs)
int sky_decode_vertex_buffer(void* dest, size_t vertex_count, size_t vertex_size,
                             const uint8_t* data, size_t data_size);
int sky_decode_index_buffer(void* dest, size_t index_count, size_t index_size,
                            const uint8_t* data, size_t data_size);

} // namespace sky
