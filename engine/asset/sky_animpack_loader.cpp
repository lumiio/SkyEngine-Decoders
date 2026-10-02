// sky_animpack_loader.cpp - see sky_animpack_loader.h
#include "sky_animpack_loader.h"
#include <cstring>
#include <fstream>

namespace sky {

static bool read_u32(const uint8_t* p, uint32_t& v) {
  std::memcpy(&v, p, 4);
  return true;
}

bool probe_animpack(const std::string& path, AnimPack& out, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { if (err) *err = "open failed"; return false; }
  f.seekg(0, std::ios::end);
  std::streamoff sz = f.tellg();
  f.seekg(0, std::ios::beg);
  if (sz < 0x54) { if (err) *err = "too small"; return false; }
  std::vector<uint8_t> d((size_t)sz);
  f.read((char*)d.data(), sz);

 // 0x00 name
  uint32_t nl = 0;
  read_u32(&d[0], nl);
  if (nl > 0x1000 || 4u + nl > d.size()) { if (err) *err = "bad name len"; return false; }
  out.name.assign((const char*)&d[4], nl);

  // 4x u32 from 0x44
  read_u32(&d[0x44], out.skeleton_count);
  read_u32(&d[0x48], out.active_count);
  read_u32(&d[0x4c], out.frame_count);
  read_u32(&d[0x50], out.version_flags);

  // node table from 0x54: 132B per entry
  const size_t rec = 132;
  const size_t table_start = 0x54;
  if (out.skeleton_count == 0 || out.skeleton_count > 4096) { if (err) *err = "bad skeleton count"; return false; }
  if (table_start + (size_t)out.skeleton_count * rec > d.size()) { if (err) *err = "node table overflow"; return false; }
  out.nodes.reserve(out.skeleton_count);
  for (uint32_t i = 0; i < out.skeleton_count; ++i) {
    const uint8_t* r = &d[table_start + (size_t)i * rec];
    AnimNode n;
  // [64B name record]: leading 0x00 + name (NUL-terminated, <=63B)
    const char* nm = (const char*)&r[1];
    size_t len = strnlen(nm, 63);
    n.name.assign(nm, len);
  // [64B bind transform] 4x float4 (little-endian)
    std::memcpy(n.bind, &r[64], 64);
  // [4B extra]
    read_u32(&r[128], n.extra);
    out.nodes.push_back(n);
  }

  out.data_start = table_start + (size_t)out.skeleton_count * rec;
  out.payload.assign(d.begin() + (ptrdiff_t)out.data_start, d.end());
  return true;
}

} // namespace sky
