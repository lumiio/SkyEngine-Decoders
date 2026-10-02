// SkyEngine .vol volume texture loader - Sky volume cloud noise (CloudNoise.vol etc.)
// based on: 123/Tex3D/*.vol measured ("VOLU" magic + version 4 + 64B filename + 4096B header + N^3 byte data)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sky {

struct VolTexture {
  int size = 0;// side length N (32/64/128)
  std::string name;// internal filename (e.g. "VoronoiBallNoise.vol")
 std::vector<uint8_t> data;// N^3 byte（0-255）
};

bool load_vol(const std::string& path, VolTexture& out);
bool load_vol(const uint8_t* data, size_t size, VolTexture& out);

// trilinear sample (x,y,z in [0,1)), returns 0..1
float vol_sample(const VolTexture& v, float x, float y, float z);

} // namespace sky
