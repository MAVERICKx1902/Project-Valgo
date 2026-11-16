# Project Valgo — Engine Architecture

Project Valgo is a proprietary, native C++20 game engine. The tactical FPS in
`games/fps` is its **first consumer and proof of concept**, not its definition.
The engine is the primary IP: every capability must be reusable by a game in a
different genre before it is considered done. `games/orbit` exists to keep that
honest.

## Non-negotiable rules

1. `engine/` never includes anything from `games/`.
2. `games/` consume the engine only through `engine/include/valgo/**` and
   `IGameModule`/`GameServices`. No game reaches into `engine/src`.
3. No gameplay concept (recoil, leaning, targets, ammo) may appear in the
   engine. If a game needs something, the engine grows a *general* capability
   (e.g. "physics raycast", not "bullet trace").
4. A missing engine capability is implemented and validated in the engine first,
   then integrated into the game.
5. Every subsystem ships with a demonstration and automated tests.

## Layers

```
core      -> standard library only        (types, math-free utilities, log, files, time, images)
math      -> core                         (vectors, quaternions, matrices, AABBs, colour space)
input     -> core                         (device-independent input snapshot)
platform  -> core, input                  (Win32 window, headless window)
rhi       -> core, math                   (device/swap chain/buffers/textures/command lists)
scene     -> core, math                   (entities, components, hierarchy)
physics   -> core, math                   (colliders, sweeps, raycasts, character motor)
assets    -> core, math, rhi, render      (procedural primitives, OBJ import, GPU upload)
render    -> core, math, rhi, scene       (camera, renderer, overlay, stats)
game/app  -> everything above             (module interface, application host loop)
```

Dependency direction is one-way. `scene` depends on nothing but math; `render`
depends on `scene` (it reads components) but never the reverse, which is what
lets gameplay, physics and rendering evolve independently.

## Engine / game boundary

`IGameModule` has three calls:

```cpp
void OnStartup(GameServices&);
void OnUpdate(const GameFrame&, GameServices&);
void OnShutdown(GameServices&);
```

`GameServices` is the whole engine surface a game gets: window, RHI device,
swap chain, renderer, scene, physics world, asset manager, input, camera and the
viewport size. It is a plain struct of pointers owned by the application; the
engine never knows which game module it is hosting.

`VALGO_IMPLEMENT_GAME_MAIN()` + `valgo::app::RunApplication(module, desc, argc,
argv)` provide the entry point, command-line parsing, the fixed-step host loop,
screenshot capture and shutdown. Two executables (`valgo_fps`, `valgo_orbit`)
use exactly the same host.

## Frame model

* Wall-clock deltas are turned into a **fixed simulation step** (default 1/60 s)
  by `core::FrameTimer`. Movement, collision and weapon timing are therefore
  deterministic and reproducible — a prerequisite for automated tests and, later,
  for authoritative networked play.
* `fixed_steps` is clamped, so a hitch cannot cause a simulation spiral.
* Rendering happens once per frame, after all simulation steps for that frame.
* The renderer clears per-frame debug geometry and the 2D overlay in
  `BeginFrame()`, so games re-submit them every update.

## Rendering model

* A single RHI with two backends: **Direct3D 12** (shipping, Windows default) and
  a **software rasteriser** (CI, headless capture, GPU-less machines). Both
  implement the same five shader programs, see `docs/RHI_AND_BACKENDS.md`.
* The renderer owns no game state: it walks `MeshRendererComponent`s, culls by
  frustum, sorts opaque/transparent, and emits `GeometryDraw` calls.
* Colours authored by games are **sRGB**. The renderer converts them to linear
  for lighting; backends encode the final image when the target format is sRGB.
  `math::SrgbToLinear` / `LinearToSrgb` are the single definition of the transfer
  function used by both backends and the HLSL library.

## Memory and lifetime

* The scene is a slot map of entities (index + generation, so stale handles are
  detected) with archetype-free component storage keyed by `typeid`.
* Every entity owns a `TransformComponent` and a `NameComponent` by contract,
  removing a large class of null checks.
* GPU resources are RAII (`rhi::Buffer`, `rhi::Texture`, swap chain), owned by
  the asset manager or the renderer, never by game code.

## Known limitations (tracked, not forgotten)

| Area | Limitation | Milestone |
| --- | --- | --- |
| Rendering | No tone mapping; sRGB targets only, no HDR pipeline | M2 |
| Rendering | Textures are nearest/linear sampled, no mip chain generated on the CPU | M2 |
| Rendering | Software backend ignores texture tiling (`params.w`) | M2 |
| Rendering | D3D12 buffers are committed UPLOAD heaps; no GPU-local staging ring | M2 |
| Rendering | Software backend is single-threaded | M3 |
| Scene | No serialisation yet | M7 |
| Physics | AABB colliders only, no dynamic rigid bodies, no capsules | M3 |
| Physics | Kinematic moving colliders must be re-added by the owner | M3 |
| Assets | OBJ only; glTF, skeletal meshes and animations arrive with M4 | M4 |
| Audio | Not implemented | M4 |
| Networking | Not implemented | M6 |
