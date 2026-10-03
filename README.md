# that-sky-Decoders

Decoder toolchain for the client-side binary asset formats of *Sky: Children of the Light* (format research project).

Contains only self-written decoding / rendering code. No Thatgamecompany (TGC) assets are included — no official meshes, textures, baked data, animation packs, or binaries.

## Decoders

### Core — `engine/asset/`

| File | Format | Notes |
|---|---|---|
| `sky_bst_decoder.h` | `BstBaked.meshes` | LVL0 / GEO0 / LOD0 / METR sections; GEO0 vertex material id + in2/in3/in4 (baked light + official vertex colors) |
| `sky_mesh_loader.{h,cpp}` | `.mesh` | 0x4e payload, LZ4, vertex/index streams, compressed variants (`_CompOcc` / `_ZipPos` / `_ZipUvs` / `_StripAnim`), bones |
| `sky_animpack_decoder.h` | `.animpack` | skeleton hierarchy, animation tracks, creature tracks |
| `sky_animpack_loader.{h,cpp}` | `.animpack` | package parsing + track instantiation |
| `sky_bin_loader.{h,cpp}` | `Objects.level.bin` | level object stream loading |
| `sky_ktx_loader.{h,cpp}` | KTX textures | texture container parsing |
| `sky_vol_loader.{h,cpp}` | volumetric data | voxel / volume containers |

### LOD0 quantized vertex streams — `apps/lod0_*`

| File | Notes |
|---|---|
| `lod0_decode.cpp` | full LOD0 decode → OBJ |
| `lod0_idx_meshopt.cpp` | meshopt index decode |
| `lod0_key.cpp` / `lod0_key_p89.cpp` / `lod0_key_s8.cpp` | quantization key recovery (three parameter families) |
| `lod0_probe.cpp` … `lod0_probe4.cpp` | decode probes (field-boundary location) |
| `lod0_render.cpp` | LOD0 render verification |

### Verification renderers — `apps/`

| File | Notes |
|---|---|
| `geo_vtx_dump.cpp` | GEO0 vertex export |
| `mesh020_render.cpp` | single `.mesh` render (scanline rasterizer) |
| `bst_render.cpp` / `bst_verify.cpp` | BstBaked render / verify |
| `anim_render.cpp` / `skin_anim_render.cpp` | animpack render / skinning (`final = Σ w·world[b]·inv(bind[b])·pos`) |
| `scene_compose.cpp` | full scene composer (level instances → world matrices → baked light → PPM/BMP) |
| `scene_export.cpp` | decoded triangles → JSON |

### Scripts — `tools/`

| File | Notes |
|---|---|
| `etc2_decode.py` | ETC2 texture decode |
| `extract_level.js` | level instance extraction (16-elem matrix / tex / col / lit / cloud / Water / MAT) |
| `extract_materials.js` | material binding extraction |
| `manifest_tool.cpp` | resource manifest tool |
| `gen_preview_html.py` | preview page generation |
| `sky_bin_parser.py` | `.bin` parser |
| `sky_disasm.py` | disassembly analysis helper |
| `sky_mesh.py` | `.mesh` parser |

## Format notes

### `.mesh`
- Compressed payload at `0x4e`: `isCompressed` + `csz` + `usz`, LZ4
- Vertex stream variants (position / UV compression, normal strips); variant flag in filename suffix
- Skinning: `final = Σ w·world[b]·inv(bind[b])·pos`

### `BstBaked.meshes`
- Sections: LVL0 (indices) → GEO0 (vertices / materials / baked data) → LOD0 (quantized vertices) → METR (AABB + material table)
- GEO0 vertex stream (stride 36): `pos (f32×3)` + `normal (R8G8B8A8_SNORM)` + `material[4]+weights[4] (u8×8)` + `in2 / in3 / in4 (R8G8B8A8_UNORM)`
- `in2` = baked light (grayscale); `in3` / `in4` = official per-vertex colors (Grass `in3≈(121,159,121)`, Cliff `in3≈(66,87,120)`)
- Light: `shF = 0.15 + 0.85·in2.r`

### `Objects.level.bin`
- Instances: 16-elem row-major matrix + texture ref + color + self-lit + cloud flag
- WATER nodes: position + half-extents; MAT rows: material id → texture name

### `.animpack`
- Skeleton hierarchy + animation tracks (timeline / keyframes), creature tracks

### EnvNode
- Sky four-segment tint gradient (Top / MidTop / MidBot / Bot), sun angle, fog near/far + fog tint, cloud texture

## Build

Third-party dependencies (not vendored, obtain separately):
- [meshoptimizer](https://github.com/zeux/meshoptimizer)
- [lz4](https://github.com/lz4/lz4)

Linux (example: `scene_compose`):

```bash
g++ -O2 -I third_party/meshoptimizer/src \
    -o scene_compose apps/scene_compose.cpp \
    third_party/meshoptimizer/src/*.cpp third_party/lz4/lz4.c
```

Windows cross-compile (zig cc + mingw-w64):

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
