// sky_animpack_loader.h — .animpack 动画包加载器
// 格式（全部 so 确证）：
//   0x00  u32 名字长度 + 名字（pad 0x40）
//   0x44  u32 骨架节点数（124 = charRig 全骨架）
//   0x48  u32 活跃（动画）节点数
//   0x4c  u32 帧数（30/24 fps）
//   0x50  u32 版本/标志（低字节=2；高位=每节点数据）
//   0x54  节点表：124 条 × 132B = [64B 名字记录][64B 绑定变换(4×float4)][4B 附加]
//   节点表后：帧数据区（明文 float）+ 采样数据流（LZ4/量化，版本 2）
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sky {

struct AnimNode {
  std::string name;          // 节点名（可带 "骨架:节点" 前缀）
  float bind[16];            // 64B 绑定变换（4×float4）
  uint32_t extra;            // 4B 附加字段
};

struct AnimPack {
  std::string name;          // 包名（MemAP03Poses 等）
  uint32_t skeleton_count = 0;  // 骨架节点数（124）
  uint32_t active_count = 0;    // 活跃节点数
  uint32_t frame_count = 0;     // 帧数
  uint32_t version_flags = 0;   // 低字节=版本（2）
  std::vector<AnimNode> nodes;  // 节点表（skeleton_count 条）
  size_t data_start = 0;        // 帧数据区偏移
  std::vector<uint8_t> payload; // 帧数据 + 采样流（原样）
};

// 解析 .animpack（仅头 + 节点表；payload 原样保留供采样解码）
bool probe_animpack(const std::string& path, AnimPack& out, std::string* err);

} // namespace sky
