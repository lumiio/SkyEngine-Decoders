// sky_bin_loader.cpp — 真实 Sky 二进制格式加载器实现
#include "sky_bin_loader.h"
#include <meshoptimizer.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>

namespace sky {

namespace {
bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { if (err) *err = "cannot open " + path; return false; }
  f.seekg(0, std::ios::end);
  size_t n = (size_t)f.tellg();
  f.seekg(0);
  out.resize(n);
  f.read((char*)out.data(), n);
  return true;
}
uint32_t u32le(const uint8_t* d) { uint32_t v; std::memcpy(&v, d, 4); return v; }
uint32_t u32be(const uint8_t* d) { return (uint32_t(d[0])<<24)|(uint32_t(d[1])<<16)|(uint32_t(d[2])<<8)|d[3]; }
uint16_t u16le(const uint8_t* d) { uint16_t v; std::memcpy(&v, d, 2); return v; }
float f32le(const uint8_t* d) { float f; std::memcpy(&f, d, 4); return f; }
bool is_node_name(const std::string& s) {
  if (s.empty() || s.size() > 31) return false;
  if (!(isalpha((unsigned char)s[0]) || s[0]=='_')) return false;
  for (char c : s) if (!(isalnum((unsigned char)c) || c=='_' || c=='.' || c=='-')) return false;
  return true;
}
} // namespace

// ---------- .mesh ----------
bool probe_sky_mesh(const std::string& path, SkyMeshHeader& out, std::string* err) {
  std::vector<uint8_t> d;
  if (!read_file(path, d, err)) return false;
  if (d.size() < 0x60) { if (err) *err = "too small"; return false; }
  // 0x00 = u32le 名字长度；0x04 起名字（后 pad）
  {
    uint32_t nl = u32le(d.data());
    if (nl > 0 && nl < 0x40) out.name.assign((const char*)d.data()+4, nl);
    else out.name.assign((const char*)d.data(), 32);
  }
  size_t z = out.name.find('\0');
  if (z != std::string::npos) out.name = out.name.substr(0, z);
  out.num_lods = u32le(d.data()+0x44);
  out.flags    = u32le(d.data()+0x48);
  out.field_0x4c = u32le(d.data()+0x4c);
  // 段表：0x50 起 4 组 [u32 0][u16le 相对偏移]，末项 0x7f80 = 哨兵（so 实测：段表=偏移非大小）
  for (int i = 0; i < 4; ++i) out.seg[i] = u16le(d.data()+0x52+i*4);
  // 节点/骨骼名提取：0x60+seg0 起为结构化 blob（名字+姿态矩阵+层级，变长）。
  // 用滑动窗口提取合法名字（3-31 字符，字母开头），而非固定步进。
  {
    size_t start = 0x60 + out.seg[0];
    size_t limit = std::min(d.size(), start + 0x20000);
    for (size_t o = start; o + 3 <= limit && out.nodes.size() < 512; ++o) {
      unsigned char c0 = d[o];
      if (!(isalpha(c0) || c0=='_')) continue;
      size_t e = o;
      while (e < limit && e-o < 32 && (isalnum(d[e]) || d[e]=='_' || d[e]=='.' || d[e]=='-')) e++;
      if (e - o >= 3) {
        std::string nm((const char*)d.data()+o, e-o);
        out.nodes.push_back(nm);
        o = e;
      }
    }
  }
  // 压缩标志：名字带 Zip*/Strip* → 数据区为量化/压缩
  out.is_compressed = (out.name.find("Zip") != std::string::npos ||
                       out.name.find("Strip") != std::string::npos ||
                       out.flags == 1);
  // StripAnim 变体：段1（0x70+seg[0] 起）为内嵌 animpack
  // （so 0x18d5294 + 22 样本：段1 头部 u32 = 124 骨架/0/30 帧/0x5a102）
  {
    size_t s1 = 0x70 + out.seg[0];
    if (s1 + 0x10 <= d.size()) {
      uint32_t sk = u32le(d.data()+s1+0x2e), ac = u32le(d.data()+s1+0x32),
               fr = u32le(d.data()+s1+0x36), ds = u32le(d.data()+s1+0x3a);
      out.has_anim = (sk == 124 && ac == 0 && (fr == 30 || fr == 24));
      (void)ds;
    }
  }
  return true;
}

// ---------- BstBaked ----------
bool probe_bst_baked(const std::string& path, BstBakedHeader& out, std::string* err) {
  std::vector<uint8_t> d;
  if (!read_file(path, d, err)) return false;
  if (d.size() < 0x100) { if (err) *err = "too small"; return false; }
  if (std::memcmp(d.data(), "LVL0", 4) != 0) { if (err) *err = "bad magic"; return false; }
  // 魔数 "LVL0"（4B）+ u32 版本（=0x3d 61，写入器 0x181c888 movk #0x3d lsl32）+ u32 块数
  out.version = u32le(d.data()+4);
  out.block_count = u32le(d.data()+8);
  // 0x0c 起块表 [4B 标签][u32 偏移][u32 大小]
  for (uint32_t i = 0; i < out.block_count && 0x0c+i*12+12 <= d.size(); ++i) {
    BstBlock b;
    std::memcpy(b.tag, d.data()+0x0c+i*12, 4);
    b.offset = u32le(d.data()+0x10+i*12);
    b.size   = u32le(d.data()+0x14+i*12);
    out.blocks.push_back(b);
  }
  out.geo_off = u32le(d.data()+0x10);
  out.geo_val = u32le(d.data()+0x14);
  out.lod_val = u32le(d.data()+0x20);
  for (int i = 0; i < 3; ++i) {
    out.aabb_min[i] = f32le(d.data()+0x70+i*4);
    out.aabb_max[i] = f32le(d.data()+0x70+12+i*4);
  }
  for (int i = 0; i < 6; ++i) out.block_sizes[i] = u32le(d.data()+0x88+i*4);
  for (int i = 0; i < 16; ++i) out.tail_catalog[i] = u32le(d.data()+d.size()-64+i*4);
  // 尾部索引流：目录表前 0x400B 区间内最长的连续 <20000 u16 序列（值 ~5900-6000）
  {
    size_t s = d.size()-0x400, e = d.size()-64;
    size_t run_s = 0, run_len = 0, best_s = 0, best_len = 0;
    for (size_t o = s; o + 2 <= e; o += 2) {
      uint16_t v = u16le(d.data()+o);
      if (v < 20000) { if (run_len == 0) run_s = o; run_len++; }
      else { if (run_len > best_len) { best_s = run_s; best_len = run_len; } run_len = 0; }
    }
    if (run_len > best_len) { best_s = run_s; best_len = run_len; }
    if (best_len > 50)
      for (size_t i = 0; i < best_len; ++i)
        out.tail_indices.push_back(u16le(d.data()+best_s+i*2));
  }
  return true;
}

// ---------- TGCL ----------
bool probe_tgcl_level(const std::string& path, TgclHeader& out, std::string* err) {
  std::vector<uint8_t> d;
  if (!read_file(path, d, err)) return false;
  if (d.size() < 0x100 || std::memcmp(d.data(), "TGCL", 4) != 0) { if (err) *err = "bad magic"; return false; }
  out.version = u32le(d.data()+4);
  for (size_t o = 8; o + 4 <= 0x80; o += 4) out.seg_table.push_back(u32le(d.data()+o));
  // 字符串表定位：段表含多个 null 分隔字符串段（0x14de5 与 0xf784 均命中）；
  // 取首个命中（0x14de5=Lua/脚本名表；0xf784=对象字段表——由调用方按序解释）
  for (uint32_t v : out.seg_table) {
    if (v < 0x10000 || v >= d.size()) continue;
    size_t nuls = 0, ascii_chars = 0;
    for (size_t o = v; o < v + 128 && o < d.size(); ++o) {
      if (d[o] == 0) nuls++;
      else if (d[o] >= 0x20 && d[o] < 0x7f) ascii_chars++;
      else break;
    }
    if (nuls >= 4 && ascii_chars >= 32) { out.string_table_start = v; break; }
  }
  if (out.string_table_start) {
    size_t o = out.string_table_start;
    while (o < d.size() && out.string_count < 200000) {
      if (d[o] == 0) { out.string_count++; o++; continue; }
      size_t e = o;
      while (e < d.size() && d[e] != 0 && e-o < 128) e++;
      if (e - o >= 3 && out.strings_sample.size() < 24)
        out.strings_sample.emplace_back((const char*)d.data()+o, e-o);
      o = e + 1;
    }
  }
  return true;
}

// ---------- meshoptimizer 解码（so 同源） ----------
int sky_decode_vertex_buffer(void* dest, size_t vertex_count, size_t vertex_size,
                             const uint8_t* data, size_t data_size) {
  return meshopt_decodeVertexBuffer(dest, vertex_count, vertex_size, data, data_size);
}
int sky_decode_index_buffer(void* dest, size_t index_count, size_t index_size,
                            const uint8_t* data, size_t data_size) {
  return meshopt_decodeIndexBuffer(dest, index_count, index_size, data, data_size);
}

} // namespace sky
