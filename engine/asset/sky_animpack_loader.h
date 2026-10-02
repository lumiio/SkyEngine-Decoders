// sky_animpack_loader.h - .animpack animation package loader
// format (confirmed across samples):
// 0x00 u32 name length + name (pad 0x40)
// 0x44 u32 skeleton node count (124 = full charRig skeleton)
// 0x48 u32 active (animated) node count
// 0x4c u32 frame count (30/24 fps)
// 0x50 u32 version/flag (low byte=2; high bits = per-node data)
// 0x54 node table: 124 entries x 132B = [64B name record][64B bind transform (4x float4)][4B extra]
// after node table: framedata region (plain floats) + sampledata stream (LZ4/quantized, version 2)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sky {

struct AnimNode {
  std::string name;// node name (may carry "skeleton:node" prefix)
  float bind[16];// 64B bind transform (4x float4)
  uint32_t extra;// 4B extra field
};

struct AnimPack {
  std::string name;// package name (MemAP03Poses etc.)
  uint32_t skeleton_count = 0;// skeleton node count (124)
  uint32_t active_count = 0;// active node count
  uint32_t frame_count = 0;// frame count
  uint32_t version_flags = 0;// low byte = version (2)
  std::vector<AnimNode> nodes;// node table (skeleton_count entries)
  size_t data_start = 0;// framedata region offset
  std::vector<uint8_t> payload;// framedata + sample stream (raw)
};

// parse .animpack (header + node table only; payload kept raw for sample decode)
bool probe_animpack(const std::string& path, AnimPack& out, std::string* err);

} // namespace sky
