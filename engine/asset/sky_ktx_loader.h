// SkyEngine KTX1 texture loader - Sky real textures (CloudFarTex.ktx etc.)
// based on: 91/*.ktx measured headers (KTX1: identifier "«KTX 11»\r\n\x1a\n" + glInternalFormat 0x9279=DXT5)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sky {

struct KtxTexture {
  int width = 0, height = 0;
  int mip_levels = 0;
  uint32_t internal_format = 0;      // 0x9279 = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
 std::vector<uint8_t> rgba;// decode after RGBA（width*height*4）
};

// load KTX1 file (DXT1/DXT5 support), return true on success
bool load_ktx(const std::string& path, KtxTexture& out);
bool load_ktx(const uint8_t* data, size_t size, KtxTexture& out);

// bilinear sample (u,v in [0,1)), return RGBA
void ktx_sample(const KtxTexture& t, float u, float v, float& r, float& g, float& b, float& a);

} // namespace sky
