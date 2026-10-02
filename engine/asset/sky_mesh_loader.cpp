// sky_mesh_loader.cpp - TGC .mesh loader implementation (base variant)
#include "sky_mesh_loader.h"
#include "../core/math.h"
#include <lz4.h>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace sky {

namespace {

struct Buffer {
  std::vector<uint8_t> d;
  const uint8_t* data() const { return d.data(); }
  size_t size() const { return d.size(); }
};

bool read_file(const std::string& path, Buffer& out, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { if (err) *err = "cannot open " + path; return false; }
  f.seekg(0, std::ios::end);
  size_t n = (size_t)f.tellg();
  f.seekg(0);
  out.d.resize(n);
  f.read((char*)out.d.data(), n);
  return true;
}

float half_to_float(uint16_t h) {
  uint32_t sign = (h & 0x8000u) << 16;
  uint32_t exp  = (h >> 10) & 0x1fu;
  uint32_t frac = h & 0x3ffu;
  uint32_t bits;
  if (exp == 0) {
    if (frac == 0) bits = sign;
    else {
  // non-normalized: normalize to normalized
      exp = 127 - 15 + 1;
      while ((frac & 0x400u) == 0) { frac <<= 1; exp--; }
      frac &= 0x3ffu;
      bits = sign | (exp << 23) | (frac << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7f800000u | (frac << 13);
  } else {
    bits = sign | ((exp - 15 + 127) << 23) | (frac << 13);
  }
  float f; std::memcpy(&f, &bits, 4); return f;
}

} // namespace

bool probe_sky_mesh(const std::string& path, MeshInfo& out, std::string* err) {
  Buffer b;
  if (!read_file(path, b, err)) return false;
  const uint8_t* d = b.data();
  if (b.size() < 0x5c) { if (err) *err="file too small"; return false; }
  uint32_t name_len = 0; std::memcpy(&name_len, d, 4);
  out.name.assign((const char*)d+4, name_len);
  size_t p = out.name.find('\0');
  if (p != std::string::npos) out.name = out.name.substr(0, p);
  std::memcpy(&out.variant_flags, d+0x48, 4);
  std::memcpy(&out.uncompressed, d+0x56, 4);
 // LZ4 decompress
  size_t src_len = b.size() - 0x5a;
  const char* src = (const char*)d + 0x5a;
  std::vector<char> dest(out.uncompressed);
  int ret = LZ4_decompress_safe(src, dest.data(), (int)src_len, (int)out.uncompressed);
  if (ret <= 0) { if (err) *err = "LZ4 decompress failed"; return false; }
  const uint8_t* u = (const uint8_t*)dest.data();
  out.compressed = (uint32_t)src_len;
  std::memcpy(&out.shared, u+0x74, 4);
  std::memcpy(&out.total,  u+0x78, 4);
  std::memcpy(&out.point,  u+0x80, 4);
  std::memcpy(&out.uv_count, u+0x84, 4);
  std::memcpy(out.aabb, u+0x04, 24);
  return true;
}


// ---- Strip variants (flags=1: CompOcc/ZipPos/ZipUvs/StripNorm/StripAnim) ----
// measured layout: 0x5C sentinel | 0x60 AABB(6f) | 0x90 attribute stream(16f) | 0xD0 shared u32
// | 0xD4..0x11F section table/sentinel | 0x120 index strip stream (u16, values<shared) | 0x1F0 ZipPos fixed-point positions
std::shared_ptr<RenderMesh> load_sky_mesh_strip(const std::string& path, std::string* err) {
  Buffer b;
  if (!read_file(path, b, err)) return nullptr;
  const uint8_t* d = b.data();
  uint32_t shared = 0; std::memcpy(&shared, d+0xd0, 4);
  if (shared == 0 || shared > 200000) { if (err) *err="bad shared count"; return nullptr; }
  if (b.size() < 0x1f0 + (size_t)shared*6) { if (err) *err="ZipPos stream exceeds file"; return nullptr; }
  float mn[3], mx[3];
  std::memcpy(mn, d+0x60, 12); std::memcpy(mx, d+0x6c, 12);
    // ZipPos vertices @0x1F0 (918 x 3 x u16 fixed-point)
  auto mesh = std::make_shared<RenderMesh>();
  mesh->positions.resize(shared);
  mesh->colors.assign(shared, Color(1,1,1));
  mesh->uvs.resize(shared);
  for (uint32_t i = 0; i < shared; ++i) {
    uint16_t q[3];
    std::memcpy(q, d + 0x1f0 + i*6, 6);
    mesh->positions[i] = Vec3(mn[0] + (q[0]/65535.f)*(mx[0]-mn[0]),
                              mn[1] + (q[1]/65535.f)*(mx[1]-mn[1]),
                              mn[2] + (q[2]/65535.f)*(mx[2]-mn[2]));
  }
    // index strip @0x120 (consecutive <shared u16 sequence)
  std::vector<uint16_t> seq;
  size_t off = 0x120;
  while (off + 2 <= b.size()) {
    uint16_t v; std::memcpy(&v, d+off, 2);
    if (v >= shared) break;
    seq.push_back(v); off += 2;
  }
  if (seq.size() < 3) { if (err) *err="strip too short"; return nullptr; }
  for (size_t i = 0; i + 2 < seq.size(); ++i) {
    uint16_t a=seq[i], c2=seq[i+1], c=seq[i+2];
    if (a==c2 || c2==c || a==c) continue;
    if (i % 2 == 0) mesh->indices.push_back(a), mesh->indices.push_back(c2), mesh->indices.push_back(c);
    else            mesh->indices.push_back(c2), mesh->indices.push_back(a), mesh->indices.push_back(c);
  }
  mesh->uvs.assign(shared, Vec2(0.5f,0.5f));
  return mesh;
}
std::shared_ptr<RenderMesh> load_sky_mesh(const std::string& path, std::string* err) {
  Buffer b;
  if (!read_file(path, b, err)) return nullptr;
  const uint8_t* d = b.data();
  if (b.size() < 0x5c) { if (err) *err="file too small"; return nullptr; }
  uint32_t flags = 0; std::memcpy(&flags, d+0x48, 4);
  if (flags == 1) return load_sky_mesh_strip(path, err);
  if (flags != 0 && flags != 0x100) { if (err) *err = "variant mesh not supported yet (flags=" + std::to_string(flags) + ")"; return nullptr; }
  uint32_t usize = 0; std::memcpy(&usize, d+0x56, 4);
  if (usize == 0 || usize > 100000000) { if (err) *err="bad decompressed size"; return nullptr; }
  size_t src_len = b.size() - 0x5a;
  const char* src = (const char*)d + 0x5a;
  std::vector<char> dest(usize);
  int ret = LZ4_decompress_safe(src, dest.data(), (int)src_len, (int)usize);
  if (ret <= 0) { if (err) *err="LZ4 decompress failed"; return nullptr; }
  const uint8_t* u = (const uint8_t*)dest.data();

  uint32_t shared=0, total=0, uv_count=0;
  std::memcpy(&shared, u+0x74, 4);
  std::memcpy(&total,  u+0x78, 4);
  std::memcpy(&uv_count, u+0x84, 4);
  if (shared == 0 || shared > 200000) { if (err) *err="bad shared count"; return nullptr; }
  if (total > 10000000) { if (err) *err="bad total count"; return nullptr; }
  if (0xb3 + (size_t)shared*16 + (size_t)shared*4 > dest.size()) { if (err) *err="stream exceeds buffer"; return nullptr; }

  auto mesh = std::make_shared<RenderMesh>();
  mesh->positions.resize(shared);
  mesh->uvs.resize(shared);
  for (uint32_t i = 0; i < shared; ++i) {
    float p[3];
    std::memcpy(p, u + 0xb3 + i*16, 12);
    mesh->positions[i] = Vec3(p[0], p[1], p[2]);
  }
    // UV: normal region (shared x 4B) after 4B/entry (2x half)
  size_t off = 0xb3 + (size_t)shared*16 + (size_t)shared*4;
  size_t got = 0;
  while (got < uv_count && off + 4 <= dest.size()) {
    uint16_t hu, hv; std::memcpy(&hu, u+off, 2); std::memcpy(&hv, u+off+2, 2);
    float uu = half_to_float(hu), vv = half_to_float(hv);
    if (uu != uu || vv != vv) break;
    if (got < (size_t)shared) mesh->uvs[got] = Vec2(uu, vv);
    off += 4; got++;
  }
    // indices: scan forward for consecutive valid triples
  size_t face_count = total / 3;
  size_t toff = off;
  bool found = false;
  for (size_t cand = off; cand + face_count*6 <= dest.size(); cand += 2) {
    bool ok = true;
    for (size_t t = 0; t < face_count; ++t) {
      uint16_t a,b,c;
      std::memcpy(&a, u+cand+t*6, 2); std::memcpy(&b, u+cand+t*6+2, 2); std::memcpy(&c, u+cand+t*6+4, 2);
      if (a >= shared || b >= shared || c >= shared) { ok = false; break; }
    }
    if (ok) { toff = cand; found = true; break; }
  }
  if (!found) { if (err) *err="index buffer not found"; return nullptr; }
  mesh->indices.reserve(face_count*3);
  for (size_t t = 0; t < face_count; ++t) {
    uint16_t a,b,c;
    std::memcpy(&a, u+toff+t*6, 2); std::memcpy(&b, u+toff+t*6+2, 2); std::memcpy(&c, u+toff+t*6+4, 2);
    mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
  }
  mesh->colors.assign(shared, Color(1,1,1));
  return mesh;
}

} // namespace sky
