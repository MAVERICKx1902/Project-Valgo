# Milestones and Current Status

The engine and the first game are built together. Each milestone ends with the
same checklist (see bottom), and nothing is built on broken functionality.

## M1 — Core, platform, scene, camera, RHI

**Status: implemented and validated headlessly; Windows/D3D12 compile pending.**

| Deliverable | State | Evidence |
| --- | --- | --- |
| C++20 build system, no third-party deps | done | `CMakeLists.txt`, `cmake/` |
| Core utilities (types, log, files, time, random, images/PNG) | done | `tests/unit/test_core.cpp` |
| Math library (vectors, quats, matrices, AABBs, colour space) | done | `tests/unit/test_math.cpp` |
| Platform window (Win32 + headless) | Win32 written, headless tested | `src/platform/` |
| RHI abstraction, 5 shader programs | done | `tests/unit/test_rhi_software.cpp` |
| Direct3D 12 backend | written, **not yet compiled on Windows** | `src/rhi/d3d12/` |
| Software rasteriser backend | done, reference backend | `src/rhi/software/` |
| Scene: entities, components, hierarchy | done | `tests/unit/test_scene.cpp` |
| Camera (first person, screen rays, frustum) | done | `tests/unit/test_math.cpp`, `render/camera` |
| Renderer: culling, lit shading, sky, debug lines, overlay | done | `tests/unit/test_renderer.cpp` |
| Application host: fixed step, CLI, capture | done | `tests/integration/test_headless_frame.cpp` |
| First game demonstrating it | done | `games/fps`, 13 shots / 9 hits scripted demo |
| Second genre proving reuse | done | `games/orbit`, `tests/integration/test_engine_reuse.cpp` |
| Automated test suite | 90 tests, ~16k checks | `ctest` |

## M2 — Rendering and environment

Planned work, in dependency order:

1. Windows compile + D3D12 validation pass (blocking, see below).
2. HDR/linear pipeline with tone mapping, and sRGB-only authoring removed from
   the renderer's fast path.
3. Texture pipeline: mip generation, tiling (`params.w`), DDS/PNG import.
4. Shadow maps (single cascade first, then 3–4 cascades) + light component
   extensions, demonstrated in the FPS map.
5. GPU-local geometry ring and staging uploads on D3D12; multithreaded software
   rasteriser tiles.
6. First real test map with authored materials, weapon placeholders (done in M1
   as procedural props, to be replaced by imported meshes).
7. Debug visualisation pass: wireframe, normals, overdraw, GPU timing on Windows.

## M3 — Physics and movement

* Dynamic rigid bodies, capsules, kinematic movers with built-in velocity (this
  removes the game-side workaround in `games/orbit`).
* Continuous collision for fast movers, contact materials/friction.
* Character controller improvements: slope handling, step-down, ladder/ledge
  detection, better air control.
* Movement demonstrably complete in the FPS: walk, run, jump, crouch, lean,
  slide, climb — each with automated tests.

## M4 — Assets and animation

* glTF import, skeletal meshes, animation clips and blending, animation events.
* Audio: mixer, 3D attenuation, streaming, footstep/weapon events.
* Replaces the procedural props in both games.

## M5 — Weapons and gameplay

* Gameplay framework: damage, hit zones, health, inventory, weapon definitions,
  recoil patterns, reload state machines, UI framework.
* All of it in `games/fps`, using engine physics queries and animation events.

## M6 — Multiplayer

* Networking abstraction, sessions, snapshot replication, client prediction,
  server reconciliation, dedicated server, room codes.

## M7 — Editor

* Native editor: viewport, hierarchy, inspector, asset browser, profiler, map
  authoring, lighting, play-in-editor.

## Immediate next actions

1. **Windows compile of the D3D12 backend** and a validation-layer pass. This is
   the only M1 deliverable not exercised by CI; it must be closed before M2.
2. View-model clipping fix at close range (M2 material/shadow work changes the
   near-plane story, so it is scheduled with it).
3. Camera polish: landing dip, lean roll and sprint sway already behave; add
   settings-driven sensitivity and FOV options.

## Milestone checklist (run at the end of every milestone)

```bash
cmake --build build                     # engine + games compile clean
ctest --test-dir build --output-on-failure
./build/bin/valgo_fps --headless --demo --deterministic --frames 400 --log-level info
./build/bin/valgo_fps --headless --frames 120 --capture /tmp/frame.png
./build/bin/valgo_orbit --headless --demo --deterministic --frames 400 --log-level info
```

Then: inspect the capture, read the logs for warnings, check the frame stats
(`fps`, draw calls, triangles), note regressions, update docs, commit.
