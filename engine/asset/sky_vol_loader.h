// SkyEngine .vol 体积纹理加载器 —— 光遇体积云噪声（CloudNoise.vol 等）
// 依据：123/Tex3D/*.vol 实测（"VOLU" 魔数 + 版本 4 + 64B 文件名 + 4096B 头 + N^3 字节数据）
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sky {

struct VolTexture {
  int size = 0;                    // 边长 N（32/64/128）
  std::string name;                // 内部文件名（如 "VoronoiBallNoise.vol"）
  std::vector<uint8_t> data;       // N^3 字节（0-255）
};

bool load_vol(const std::string& path, VolTexture& out);
bool load_vol(const uint8_t* data, size_t size, VolTexture& out);

// 三线性采样（x,y,z in [0,1)），返回 0..1
float vol_sample(const VolTexture& v, float x, float y, float z);

} // namespace sky
