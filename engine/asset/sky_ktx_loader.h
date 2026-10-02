// SkyEngine KTX1 纹理加载器 —— 光遇真实纹理（CloudFarTex.ktx 等）
// 依据：91/*.ktx 实测头（KTX1：identifier "«KTX 11»\r\n\x1a\n" + glInternalFormat 0x9279=DXT5）
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sky {

struct KtxTexture {
  int width = 0, height = 0;
  int mip_levels = 0;
  uint32_t internal_format = 0;      // 0x9279 = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
  std::vector<uint8_t> rgba;         // 解码后 RGBA（width*height*4）
};

// 加载 KTX1 文件（DXT1/DXT5 支持），返回 true 成功
bool load_ktx(const std::string& path, KtxTexture& out);
bool load_ktx(const uint8_t* data, size_t size, KtxTexture& out);

// 双线性采样（u,v in [0,1)），返回 RGBA
void ktx_sample(const KtxTexture& t, float u, float v, float& r, float& g, float& b, float& a);

} // namespace sky
