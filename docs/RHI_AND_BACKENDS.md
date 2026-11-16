# RHI and Backends

The Rendering Hardware Interface is the only way anything in Valgo touches the
GPU. It is deliberately small: a device, swap chains, buffers, textures, command
lists, and an enum of engine shader programs.

## Why two backends

The shipping target is **Windows + Direct3D 12**. Continuous validation runs on
Linux CI with no GPU, and the milestone checklist requires screenshots and
regression checks on every commit. A mock would make those checks worthless, so
the second backend is a **real rasteriser**: it consumes the same command lists,
constants and pipelines and produces comparable images.

```
renderer.cpp ─► rhi::CommandList ─┬─► d3d12_device.cpp   (GPU, Windows)
                                  └─► software_rhi.cpp   (CPU, everywhere)
```

## Shader programs

Both backends implement exactly these programs; there is no per-backend special
casing in the renderer.

| Program | Purpose | Notes |
| --- | --- | --- |
| `LitMesh` | directional light + hemisphere ambient + camera fill + optional texture + fog | two-sided normals |
| `UnlitMesh` | flat colour, emissive supported | muzzle flashes, markers |
| `DebugLine` | 3D coloured line list with depth test | packed RGBA vertex colour |
| `Sky` | full-screen gradient + sun glow | procedural, no geometry |
| `Screen2D` | screen-space triangles, alpha blended | HUD and overlay text |

## Constant layout

| Buffer | Register | Size | Contents |
| --- | --- | --- | --- |
| `DrawConstants` | b0 root constants | 96 B | world matrix, tint/opacity, unlit/texture/emissive/tiling |
| `FrameConstants` | b1 root CBV | 432 B | view-projection, view, projection, inverse VP, camera, light, ambient, fog, viewport, clear, sky colours |

`FrameConstants` is asserted to stay tightly packed; D3D12 cannot pass it as root
constants (128-byte limit / 64 DWORD budget), which is why it is a root CBV.

## Colour pipeline

1. Game code authors colours in sRGB (`Material::MakeSolid`, `RenderSettings`).
2. The renderer converts them to linear with `math::SrgbToLinear` while filling
   constants.
3. Lighting and fog happen in linear space.
4. The target is an sRGB format:
   * D3D12 encodes in hardware on write and blends in linear space;
   * the software backend applies the same transfer function in `StorePixel`.
5. Packed 8-bit vertex colours (debug lines, 2D overlay) travel as sRGB and are
   decoded at the start of their shader/pixel path, in both backends.

`tests/unit/test_rhi_software.cpp` asserts the sRGB encode of a known clear
colour, and `tests/integration/test_headless_frame.cpp` asserts rendering is
deterministic across runs.

## Software backend notes

* RGBA8 colour + float32 depth, single-threaded, no SIMD.
* Per-triangle near-plane clipping in view space, then screen-space edge-function
  rasterisation with perspective-correct interpolation of `1/w`.
* Opaque/transparent split and sorting are the renderer's job, identical on both
  backends.
* Textures: nearest sampling with wrap; sRGB texture formats are decoded to
  linear before lighting.

## D3D12 backend notes

* Three frames in flight; each slot owns an allocator, a command list, a
  `FrameConstants` upload buffer and a fence value. Acquiring a slot waits on its
  fence, so no global stall is needed to present.
* Root signature: b0 root constants, b1 root CBV, t0 descriptor table (base
  colour, slot 0 = white texture), static linear wrap sampler.
* Shaders are compiled at startup from one embedded HLSL library with
  `D3DCompile` (debug flags when validation is on). Offline DXIL compilation and
  a shader cache are milestone 2 work.
* Geometry lives in committed UPLOAD heaps for now; transparent per-frame vertex
  buffers are owned by the command list and freed after the slot fence completes.
* Swap chain: `DXGI_SWAP_EFFECT_FLIP_DISCARD`, `B8G8R8A8_UNORM_SRGB`, sRGB RTVs,
  D32_FLOAT depth, `ReadPixels` copies into a READBACK heap and converts BGRA→RGBA.

### Status

The D-Linux CI cannot compile D3D12 code, so the Windows backend is *not* covered
by CI yet. It is guarded by `VALGO_RHI_HAS_D3D12` and fails gracefully (device
creation returns `nullptr`, `rhi::Device::Create` falls back to software), which
keeps Linux builds and tests green. **Its first Windows compile is an explicit
checklist item** before milestone 2 work starts; see `docs/MILESTONES.md`.

## Adding a shader program

1. Add the enum value and its name in `rhi.h` (and in `ShaderProgramName`).
2. Implement it in the software rasteriser.
3. Implement the HLSL entries and register them in `d3d12_shaders.cpp`.
4. Add the vertex layout to the D3D12 pipeline builder.
5. Add a renderer-level test that proves it draws.
