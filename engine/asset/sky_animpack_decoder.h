// SkyEngine .animpack full decoder
// header: version@0x00 + name 64B@0x04 + boneCount@0x44 + boneDefsFlag@0x48 + refSqtFlag@0x4C
// + compression u8@0x50 + nameTableSize@0x51 + bone table@0x55 (132B x boneCount)
// section:refSQT(40B×boneCount)+ LZ4 block[totalSize+decompSize+data] -> clipData
// clipData: 6x u32 header (sets=h[0]) + SQT table (40B x boneCount) + keyframe sets
// keyframe set: f1/f2/flags + bbox (24B, v>8) + quant extra1/2 (24B, v>=11) + perBoneFlags + channels
// main channel (bit3=s, bit4=q, bit5=t): single value @f1; child channels (bit0=s, bit1=q, bit2=t): per-frame f1..f2
// quat compress(comp=2):4×u16 ->(r-32768)/32767;trans i16(flags&1):3×u16 -> extra1+r/65535×extra2
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <lz4.h>

namespace sky {

struct AnimSQT { float s[3]; float q[4]; float t[3]; };

struct AnimBone {
  std::string name;
  float bind[16];
  int parent = -1;
};

struct KeySet {
  uint32_t f1 = 0, f2 = 0, flags = 0;
  float quantMin[3] = {0,0,0}, quantScale[3] = {0,0,0};
  std::vector<uint8_t> perBoneFlags;
};

struct AnimClip {
  uint32_t header[6] = {0};
  std::vector<AnimSQT> baseSqt;
  std::vector<KeySet> sets;
};

struct AnimPackDecoded {
  uint32_t version = 0;
  std::string name;
  uint32_t boneCount = 0, boneDefsFlag = 0, refSqtFlag = 0, compression = 0;
  std::vector<AnimBone> bones;
  AnimClip clip;
  std::vector<float> frameData;
  uint32_t minFrame = 0, maxFrame = 0, frameCount = 0;
  bool ok = false;
};

inline uint32_t ard32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
inline uint16_t ard16(const uint8_t* p){ return (uint16_t)(p[0]|(p[1]<<8)); }
inline float ardf(const uint8_t* p){ uint32_t u=ard32(p); float f; std::memcpy(&f,&u,4); return f; }

inline bool decode_animpack(const std::string& path, AnimPackDecoded& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(sz); if (fread(d.data(), 1, sz, f) != (size_t)sz) { fclose(f); return false; }
  fclose(f);
  if (sz < 0x55) return false;
  out.version = ard32(d.data());
  out.name = (const char*)&d[4];
  out.boneCount = ard32(&d[0x44]);
  out.boneDefsFlag = ard32(&d[0x48]);
  out.refSqtFlag = ard32(&d[0x4c]);
  out.compression = d[0x50];
  if (out.boneCount == 0 || out.boneCount > 4096) return false;
  uint32_t boneOff = 0x55;
  out.bones.resize(out.boneCount);
  for (uint32_t i = 0; i < out.boneCount; i++) {
    const uint8_t* b = d.data() + boneOff + i * 132;
    AnimBone& bo = out.bones[i];
    bo.name.assign((const char*)b, 64);
    size_t e = bo.name.find('\0'); if (e != std::string::npos) bo.name = bo.name.substr(0, e);
    std::memcpy(bo.bind, b + 64, 64);
    int rp = (int)ard32(b + 128);
    bo.parent = rp > 0 ? rp - 1 : -1;
  }
  uint32_t off = boneOff + out.boneCount * 132;
  if (out.refSqtFlag > 0 && out.boneDefsFlag > 0 && off + (size_t)out.boneCount * 40 <= d.size())
    off += out.boneCount * 40;
  if (out.compression == 0 || off + 8 > d.size()) return false;
  uint32_t ts = ard32(&d[off]), ds = ard32(&d[off + 4]);
  if (ts == 0 || off + 8 + ts > d.size()) return false;
  std::vector<uint8_t> dec(ds);
  int r = LZ4_decompress_safe((const char*)&d[off + 8], (char*)dec.data(), ts, ds);
  if (r <= 0) return false;
  dec.resize(r);
  AnimClip& clip = out.clip;
  for (int i = 0; i < 6 && i * 4 + 4 <= r; i++) clip.header[i] = ard32(&dec[i * 4]);
  uint32_t bc = out.boneCount;
  uint32_t p = 24;
  clip.baseSqt.resize(bc);
  for (uint32_t i = 0; i < bc && p + 40 <= dec.size(); i++) {
    const uint8_t* s = dec.data() + p;
    for (int j = 0; j < 3; j++) clip.baseSqt[i].s[j] = ardf(s + j * 4);
    for (int j = 0; j < 4; j++) clip.baseSqt[i].q[j] = ardf(s + 12 + j * 4);
    for (int j = 0; j < 3; j++) clip.baseSqt[i].t[j] = ardf(s + 28 + j * 4);
    p += 40;
  }
  uint32_t numSets = clip.header[0];
  if (numSets == 0 || numSets > 4096) return false;
  uint32_t sp = p;
  uint32_t mn = 0xFFFFFFFF, mx = 0;
  for (uint32_t si = 0; si < numSets; si++) {
    if (sp + 12 > dec.size()) break;
    KeySet ks;
    ks.f1 = ard32(&dec[sp]); ks.f2 = ard32(&dec[sp + 4]); ks.flags = ard32(&dec[sp + 8]);
    sp += 12;
    if (out.version > 8) sp += 24;
    if (out.version >= 11) { for (int j = 0; j < 3; j++) ks.quantMin[j] = ardf(&dec[sp + j * 4]); for (int j = 0; j < 3; j++) ks.quantScale[j] = ardf(&dec[sp + 12 + j * 4]); sp += 24; }
    if (sp + bc > dec.size()) break;
    ks.perBoneFlags.assign(dec.begin() + sp, dec.begin() + sp + bc); sp += bc;
    if (ks.f1 < mn) mn = ks.f1; if (ks.f2 > mx) mx = ks.f2;
    bool i16 = (out.compression == 2) && (ks.flags & 1);
    int quatSz = (out.compression == 2) ? 8 : 16, transSz = i16 ? 6 : 12, scaleSz = 12;
    int sc = 0, qc = 0, tc = 0, s2 = 0, q2 = 0, t2 = 0;
    for (uint32_t bi = 0; bi < bc; bi++) { uint8_t f = ks.perBoneFlags[bi];
      if (f & 8) sc++; if (f & 16) qc++; if (f & 32) tc++;
      if (f & 1) s2++; if (f & 2) q2++; if (f & 4) t2++; }
    int fc = (ks.f2 >= ks.f1) ? (int)(ks.f2 - ks.f1 + 1) : 1;
    sp += (uint32_t)(sc * scaleSz + qc * quatSz + tc * transSz) + (uint32_t)fc * (s2 * scaleSz + q2 * quatSz + t2 * transSz);
    clip.sets.push_back(ks);
  }
  if (mn == 0xFFFFFFFF) return false;
  out.minFrame = mn; out.maxFrame = mx;
  out.frameCount = mx - mn + 1;
  out.frameData.assign((size_t)bc * out.frameCount * 10, std::nanf(""));
  uint32_t q = p;
  auto setf = [&](int fi, uint32_t bi, const float* s, const float* qv, const float* tv) {
    if (fi < 0 || fi >= (int)out.frameCount) return;
    float* dst = &out.frameData[((size_t)bi * out.frameCount + fi) * 10];
    if (s) { dst[0] = s[0]; dst[1] = s[1]; dst[2] = s[2]; }
    if (qv) { dst[3] = qv[0]; dst[4] = qv[1]; dst[5] = qv[2]; dst[6] = qv[3];
      float l = std::sqrt(dst[3]*dst[3]+dst[4]*dst[4]+dst[5]*dst[5]+dst[6]*dst[6]);
      if (l > 1e-9f) { dst[3]/=l; dst[4]/=l; dst[5]/=l; dst[6]/=l; } }
    if (tv) { dst[7] = tv[0]; dst[8] = tv[1]; dst[9] = tv[2]; }
  };
  for (uint32_t si = 0; si < clip.sets.size(); si++) {
    const KeySet& ks = clip.sets[si];
    if (q + 12 > dec.size()) break;
    q += 12;
    if (out.version > 8) q += 24;
    if (out.version >= 11) q += 24;
    const std::vector<uint8_t>& pbf = ks.perBoneFlags;
    bool i16 = (out.compression == 2) && (ks.flags & 1);
    int quatSz = (out.compression == 2) ? 8 : 16, transSz = i16 ? 6 : 12, scaleSz = 12;
    int fc = (ks.f2 >= ks.f1) ? (int)(ks.f2 - ks.f1 + 1) : 1;
    int mfi = (int)(ks.f1 - out.minFrame); if (mfi < 0) mfi = 0;
    for (uint32_t bi = 0; bi < bc; bi++) { if (!(pbf[bi] & 8)) continue; float s[3] = { ardf(&dec[q]), ardf(&dec[q+4]), ardf(&dec[q+8]) }; q += 12; setf(mfi, bi, s, nullptr, nullptr); }
    for (uint32_t bi = 0; bi < bc; bi++) {
      if (!(pbf[bi] & 16)) continue;
      float qv[4];
      if (out.compression == 2) { for (int j = 0; j < 4; j++) qv[j] = (ard16(&dec[q + j * 2]) - 32768) / 32767.0f; q += 8; }
      else { for (int j = 0; j < 4; j++) qv[j] = ardf(&dec[q + j * 4]); q += 16; }
      setf(mfi, bi, nullptr, qv, nullptr);
    }
    for (uint32_t bi = 0; bi < bc; bi++) {
      if (!(pbf[bi] & 32)) continue;
      float tv[3];
      if (i16) { for (int j = 0; j < 3; j++) tv[j] = ks.quantMin[j] + (ard16(&dec[q + j * 2]) / 65535.0f) * ks.quantScale[j]; q += 6; }
      else { for (int j = 0; j < 3; j++) tv[j] = ardf(&dec[q + j * 4]); q += 12; }
      setf(mfi, bi, nullptr, nullptr, tv);
    }
    for (int fi = 0; fi < fc; fi++) {
      int fi2 = (int)(ks.f1 + fi) - (int)out.minFrame;
      for (uint32_t bi = 0; bi < bc; bi++) { if (!(pbf[bi] & 1)) continue; float s[3] = { ardf(&dec[q]), ardf(&dec[q+4]), ardf(&dec[q+8]) }; q += 12; setf(fi2, bi, s, nullptr, nullptr); }
      for (uint32_t bi = 0; bi < bc; bi++) {
        if (!(pbf[bi] & 2)) continue;
        float qv[4];
        if (out.compression == 2) { for (int j = 0; j < 4; j++) qv[j] = (ard16(&dec[q + j * 2]) - 32768) / 32767.0f; q += 8; }
        else { for (int j = 0; j < 4; j++) qv[j] = ardf(&dec[q + j * 4]); q += 16; }
        setf(fi2, bi, nullptr, qv, nullptr);
      }
      for (uint32_t bi = 0; bi < bc; bi++) {
        if (!(pbf[bi] & 4)) continue;
        float tv[3];
        if (i16) { for (int j = 0; j < 3; j++) tv[j] = ks.quantMin[j] + (ard16(&dec[q + j * 2]) / 65535.0f) * ks.quantScale[j]; q += 6; }
        else { for (int j = 0; j < 3; j++) tv[j] = ardf(&dec[q + j * 4]); q += 12; }
        setf(fi2, bi, nullptr, nullptr, tv);
      }
    }
  }
  out.ok = true;
  return true;
}

} // namespace sky
