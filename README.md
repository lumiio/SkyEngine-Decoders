# SkyEngine Decoders

《光·遇》(Sky: Children of the Light) 客户端二进制格式的**解码器集合**（逆向工程研究项目）。

本仓库只包含**自行编写的解码/渲染代码**，不包含任何 Thatgamecompany（TGC）的版权资产——官方网格、贴图、烘焙数据、动画包、二进制本体均不在仓库内。

---

## 研究来源（统一标注）

以下为本仓库全部研究的来源依据。源码文件内不再逐处标注，统一在此说明：

| 来源 | 用途 |
|---|---|
| `libBootloader.so` 静态反汇编 | 启动链、烘焙光照公式（`shF`）、蒙皮矩阵公式、资源同步机制的交叉验证 |
| 官方 APK（国际服 v0.34）解包样本 | 各格式的字段布局、字节级样本统计、解码正确性验证 |
| 社区项目 SkyModelViewer（XianXiaoWei） | `.animpack` / 关卡解析的格式交叉验证（仅比对布局，代码独立实现） |
| 社区项目 that-sky-level（that-sky-game） | `Objects.level.bin` 反序列化的交叉验证 |
| vg-resource 社区（longbyte1 / cobrakyle） | `.mesh` 格式逆向早期依据 |

> 逆向研究中发现的公式、表、布局属于格式事实；代码实现均为本项目原创。

---

## 解码器清单

### 核心解码器 `engine/asset/`

| 文件 | 解码对象 | 要点 |
|---|---|---|
| `sky_bst_decoder.h` | `BstBaked.meshes` 烘焙地形 | LVL0 / GEO0 / LOD0 / METR 四段；GEO0 顶点材质 id + in2/in3/in4（烘焙光+官方顶点色） |
| `sky_mesh_loader.{h,cpp}` | `.mesh` 网格 | 头部 0x4e payload、LZ4 压缩、顶点/索引流、`_CompOcc` / `_ZipPos` / `_ZipUvs` / `_StripAnim` 压缩变体、骨骼 |
| `sky_animpack_decoder.h` | `.animpack` 动画包 | 骨架层级、动画轨道、生物轨道 |
| `sky_animpack_loader.{h,cpp}` | `.animpack` 加载 | 包解析 + 轨道实例化 |
| `sky_bin_loader.{h,cpp}` | `Objects.level.bin` | 关卡对象流加载 |
| `sky_ktx_loader.{h,cpp}` | KTX 纹理 | 纹理容器解析 |
| `sky_vol_loader.{h,cpp}` | 体积数据 | 体素/体积场容器 |

### LOD0 量化顶点流 `apps/lod0_*`

| 文件 | 说明 |
|---|---|
| `lod0_decode.cpp` | LOD0 段全量解码 → OBJ 输出 |
| `lod0_idx_meshopt.cpp` | 索引流 meshopt 解码 |
| `lod0_key.cpp` / `lod0_key_p89.cpp` / `lod0_key_s8.cpp` | 量化 key 逆向（三种量化参数族） |
| `lod0_probe.cpp` ~ `lod0_probe4.cpp` | 解码探针（字段边界定位） |
| `lod0_render.cpp` | LOD0 渲染验证 |

### 验证渲染器 `apps/`

| 文件 | 说明 |
|---|---|
| `geo_vtx_dump.cpp` | GEO0 顶点流导出 |
| `mesh020_render.cpp` | 单 `.mesh` 渲染 |
| `bst_render.cpp` / `bst_verify.cpp` | BstBaked 渲染 / 校验 |
| `anim_render.cpp` / `skin_anim_render.cpp` | 动画包渲染 / 蒙皮合成（`final = Σ w·world[b]·inv(bind[b])·pos`） |
| `scene_compose.cpp` | 全场景合成渲染器（关卡实例 → 世界矩阵 → 烘焙光照 → 输出 PPM/BMP） |
| `scene_export.cpp` | 解码三角形导出 JSON |

### 工具脚本 `tools/` 与根目录

| 文件 | 说明 |
|---|---|
| `tools/etc2_decode.py` | ETC2 贴图解码 |
| `tools/extract_level.js` | 关卡实例提取（矩阵 16 元素 / tex / col / lit / cloud / Water / MAT） |
| `tools/extract_materials.js` | 材质绑定提取 |
| `tools/manifest_tool.cpp` | 资源清单工具 |
| `tools/gen_preview_html.py` | 预览页生成 |
| `sky_bin_parser.py` | `.bin` 解析 |
| `sky_disasm.py` | 反汇编分析辅助 |
| `sky_mesh.py` | `.mesh` 解析 |

---

## 格式规格

### `.mesh`
- 头部 0x4e 处为压缩载荷：`isCompressed(csz, usz)`，LZ4 解压
- 顶点流分多个变体（位置压缩 / UV 压缩 / 法线条带），文件名后缀标识变体
- 骨骼蒙皮：`final = Σ w·world[b]·inv(bind[b])·pos`

### `BstBaked.meshes`
- 段结构：LVL0（索引）→ GEO0（顶点/材质/烘焙 uv）→ LOD0（量化顶点）→ METR（AABB + 材质表）
- GEO0 顶点材质 id（`vmat`）→ LevelMaterial 色表；烘焙光照 `lightB=uv0.u`、`giB=uv1.u`
- 光照公式：`shF = 0.15 + 0.85·lightB (+ 0.06·giB)`

### `Objects.level.bin`
- 对象实例：16 元素行主序矩阵 + 纹理引用 + 颜色 + 自发光 + 云标记
- WATER 节点：位置 + 半轴；MAT 行：材质 id → 贴图名

### `.animpack`
- 骨架层级 + 动画轨道（时间轴 / 关键帧），含生物轨道（非骨骼对象运动）

### 环境（EnvNode）
- 天空四段渐变 tint（Top / MidTop / MidBot / Bot）、太阳角、雾 near/far + 雾色、云纹理

---

## 依赖与编译

第三方依赖（均需自行获取，不在本仓库）：
- [meshoptimizer](https://github.com/zeux/meshoptimizer)
- [lz4](https://github.com/lz4/lz4)

Linux 编译（以 `scene_compose` 为例）：

```bash
g++ -O2 -I third_party/meshoptimizer/src \
    -o scene_compose apps/scene_compose.cpp \
    third_party/meshoptimizer/src/*.cpp third_party/lz4/lz4.c
```

Windows 交叉编译（zig cc + mingw-w64）：

```bash
zig cc -target x86_64-windows-gnu -O2 -stdlib=libstdc++ \
    -I third_party/meshoptimizer/src -I third_party/lz4 \
    -isystem <mingw>/x86_64-w64-mingw32/include \
    -isystem <mingw>/include/c++/14.2.0 \
    -isystem <mingw>/include/c++/14.2.0/x86_64-w64-mingw32 \
    -o SkyEngine.exe apps/scene_compose.cpp \
    third_party/meshoptimizer/src/*.cpp third_party/lz4/lz4.c \
    -lstdc++ -lwinpthread -lgcc
```

---

## 数据获取（请自备官方数据）

解码器需要官方数据作为输入，本仓库不提供。获取方式：

1. 获取光遇国际服 APK（Android，`com.tgc.sky`）
2. 解包 APK：`assets/Data/` 下包含 `meshes_pkg` / `initial_pkg` / `images_pkg` / `*.pkg` 等资源包
3. 解包后得到 `.mesh`（`Data/Meshes/Bin/`）、`BstBaked.meshes`（`Data/Levels/<关卡>/`）、`Objects.level.bin`、`.animpack`、KTX/ETC2 贴图

示例（Prairie_Village）：

```bash
./scene_compose levelmesh_full.txt meshes/ out.bmp \
    data/BstBaked.meshes tex/ --env data/env.txt
```

---

## 免责声明

- 本项目为**格式逆向研究**用途，仅供学习与技术交流
- 不包含、不分发任何 TGC 官方资源；解码器本身不构成对官方代码的复制
- 请勿将本仓库代码用于商业用途；请在具备官方数据合法来源的前提下使用
- 涉及的公式/布局为逆向研究结论，准确性以官方数据实际解码结果为准
