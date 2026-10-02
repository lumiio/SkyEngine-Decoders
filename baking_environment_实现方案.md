# SkyEngine 烘焙与环境引擎 — 实现方案（基于 libBootloader.so 反汇编证据）

本文件说明如何在 SkyEngine 中还原《光·遇》`libBootloader.so` 反汇编揭示的**光照烘焙**与**环境渲染**两大系统。
所有实现为原创，算法取自行业通用方法（Enlighten 风格 lightmap 烘焙、球谐光照探针、解析大气、3D 噪声体积云），
仅**架构与数据流**对齐反汇编证据，未复制 TGC 任何专有代码/资产。

## 一、反汇编证据 → 实现映射

### 1. 光照烘焙（LightBakeJob）

| libBootloader.so 证据 | 实现 |
|---|---|
| `LightBakeJob` / `LightBake` / `tag_LightBake` | `engine/render/bake.cpp`：`LightmapBaker::bake()` |
| `"Calculating direct lighting ..."` | `direct_at()`：太阳/环境/点光直接辐照 + 阴影 |
| `"Calculating bounce lighting (%u/%u) ..."` | `bounce_at()`：法线半球光线 × `bounce_rays`，命中 light-cache 累加 |
| `"Number of waves of bounce light baked into spherical harmonics"` | `bounce_waves` 字段 + 探针 SH 投影（2 阶 9 系数） |
| `u_probeShR0/G0/B0/R1/G1/B1/u_probeShRGB2` | `SH9`：Y00/Y1±/Y20/Y21±/Y22± 共 9 系数 |
| `lightProbeAABB` / `"Spread light probes in a box aligned to XYZ"` | `ProbeGrid`：对齐 XYZ 的三维探针盒 |
| `"Increases the density of probes..."` | `ProbeGrid.nx/ny/nz` 可调 |
| `"Whether this mesh casts shadows onto other meshes during the light bake."` | 体素遮挡场（`VoxelField`），向光方向步进判阴影 |
| `sunShadowed` / `"hardness of the shadow"` | `direct_at()` 的 `sh` 系数 |
| `"How much light land absorb. Higher number means darker land."` | `LightMaterial.absorb`（吸收率） |
| `kMaterial_CliffLight/GrassLight/SandLight` | `LightMaterial.category` 0..4 |
| `"Direct Light: %f" / "AmbSun Light: %f" / "Point Light: %f"` | `BakeLightType::kDirectional/kAmbSun/kPoint` |
| `u_averageProbeColor` / `u_probeExposure` | `BakedLighting.average_probe/exposure` |
| `Logging texture saver info: ... isHDR` | lightmap 以 float3 HDR 存储，`save_png` 为调试输出 |

**烘焙数据流（Enlighten 风格）：**
```
BakeMesh(顶点+uv+材质) → 体素化(面积自适应采样) → direct light cache
→ uv 空间光栅化 → 逐 texel: direct(含阴影) + bounce×albedo → lightmap(float3)
→ 探针: 球面 SH 采样 → 9 系数 × 3 通道 → bake.bin(序列化)
```

### 2. 环境引擎（Environment）

| libBootloader.so 证据 | 实现 |
|---|---|
| `AtmosphereTex` / `u_atmosphereSample` / `u_atmosphereDensity(Max/Min)` / `u_atmosphereSunColor` | `Atmosphere::sample()`：解析 rayleigh+mie 近似 |
| `"Atmospheric Color: (%f,%f,%f,%f) / (%f,%f)"` | `AtmosphereParams` 调试日志口径 |
| `CloudNoise3D` / `CloudFluffy.vol` | `vnoise3()/fbm3()`：整数哈希 3D 值噪声（跨平台确定性） |
| `CloudVoxelBakeJob` / `CloudPackedAmb0/1/2Grid` | `CloudField::init()` 烘焙 3 层 8³ 环境光网格 |
| `u_cloudAmb0..2` | `CloudField::ambient()` 三线性采样加权 |
| `u_cloudWindScrollSpace` / `u_cloudDepth` / `halfResClouds` | 风平流 `time` / 云层高度包络 / 2 步 raymarch |
| `fogScatterParams` / `u_fogScatterBrightness` / `"Volume fog scattering switch."` | `VolumeFog::scatter()`：Henyey-Greenstein 前向散射 |
| `godLight` / `LightShaft_02` | 屏幕空间太阳辉光（`pow(sd_dot,24)` 光锥） |

## 二、关键实现细节

1. **体素化（BuildField）**：三角形按面积自适应重心采样（`min(96, max(8, area/cell²))`），
   水平面（n.y>0.5）向下填充柱子形成高度场实体，保证太阳遮挡射线可靠命中。
2. **阴影判定**：从表面点沿**向光方向**步进（先抬升 0.5 cell 作为 shadow bias），
   命中遮挡体素则 `sh=0`；该方向起初写反（沿背光方向）导致全场景"无阴影"，测试已锁定。
3. **uv 光栅化烘焙**：对每个三角形在 uv 空间求包围盒，逐 texel 重心插值出世界位置与法线，
   再求辐照——保证 lightmap 全覆盖（顶点级投影会留下空洞）。
4. **SH 探针**：球面黄金角序列 64 方向采样入射辐照 → 投影 2 阶 SH；运行时按法线三线性插值
   8 邻近探针（动态角色/浮石用）。
5. **网格绕序**：所有 mesh 统一逆时针（法线朝外），否则 ndl=0 导致烘焙全黑——测试锁定。

## 三、验证结果（可复现）

```bash
cmake -B build -G Ninja . && ninja -C build
./build/test_all        # 60 passed, 0 failed（含 bake/env 6 项新增）
./build/skyengine_demo  # 烘焙 + 环境渲染 → sky_render.png / lightmap.png / bake.bin
```

- 单元测试：烘焙确定性、lightmap 阳/阴对比（墙体投影）、探针三线性、序列化往返、
  大气天顶蓝>地平线、云密度/环境网格范围、雾散射为正、环境确定性。
- 烘焙统计（演示场景）：lightmap 64×64，65.4% texel 受太阳直射，max 辐照 ≈35（HDR）；
  108 个 SH 探针。
- 渲染帧 960×540@20：大气渐变天空 + 3D 噪声体积云 + godLight 辉光 + 体积雾散射 +
  烘焙地形（阳面亮/阴面柔）。

## 四、与"一模一样"的边界说明

- 反汇编只能给出**符号、字符串、结构、流程**，不能给出 TGC 的着色器源码与具体参数；
  本实现用行业通用算法填充行为层（与官方美术数值无关）。
- 未包含任何 TGC 版权代码/美术/音频/数值；引擎可自由用于个人学习与开发。
- 如需更进一步对齐，可继续：Vulkan/GL 真实 GPU 后端（烘焙纹理采样、体积云 compute）、
  FMOD、Android 构建（见 README Roadmap）。
