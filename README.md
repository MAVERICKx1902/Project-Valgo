# Project Valgo

A proprietary, native C++20 game engine — built in parallel with its first game
so that every engine system is proven by real gameplay.

The engine is the product. The tactical FPS in `games/fps` is its first consumer
and its proof of concept; `games/orbit` is a second, deliberately different genre
that keeps the engine honest about being reusable.

## What exists today (milestone 1)

* **Engine** (`engine/`) — a dependency-free static library: core utilities,
  math, input, Win32 + headless platform layer, a Rendering Hardware Interface
  with **Direct3D 12** and a real **software rasteriser** backend, scene graph,
  physics world with a character motor, asset manager with procedural primitives
  and an OBJ importer, renderer with frustum culling, lit shading, fog, sky, debug
  lines and a HUD overlay, plus the application host with fixed-step simulation,
  command-line parsing and screenshot capture.
* **Tactical FPS** (`games/fps`) — first-person controller (walk/sprint/crouch/
  lean/jump), hitscan weapon with recoil, spread and reload, a training range with
  cover, a crouch wall, step-up platforms and six human-scale dummies, HUD, and a
  deterministic 16-second scripted demo.
* **Orbit Runner** (`games/orbit`) — top-down arena collector using the same
  engine services through the same `IGameModule` interface, with no FPS code.
* **Tests** (`tests/`) — 90 tests, ~16k assertions, fully headless, driven by CTest.

## Quick start

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVALGO_BUILD_GAMES=ON
cmake --build build
ctest --test-dir build --output-on-failure

./build/bin/valgo_fps                                        # play (window)
./build/bin/valgo_fps --headless --demo --deterministic \
    --frames 400 --capture range.png                         # verify (CI)
./build/bin/valgo_orbit --demo                               # second genre
```

Details: [`docs/BUILDING.md`](docs/BUILDING.md).

## Documentation

| Document | Contents |
| --- | --- |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | layers, engine/game boundary, frame model, limitations |
| [`docs/RHI_AND_BACKENDS.md`](docs/RHI_AND_BACKENDS.md) | RHI contract, both backends, shader programs, colour pipeline |
| [`docs/TESTING.md`](docs/TESTING.md) | test framework, suites, determinism, how to add tests |
| [`docs/MILESTONES.md`](docs/MILESTONES.md) | milestone scope, status, checklist, next actions |
| [`docs/BUILDING.md`](docs/BUILDING.md) | requirements, options, CLI reference, troubleshooting |

## Ground rules

1. The engine never depends on game code; games only use public engine headers.
2. No FPS-specific logic in the engine — a missing capability becomes a general
   engine feature, validated in the engine before the game uses it.
3. Nothing is built on broken functionality: build, test, launch, inspect and
   document before moving on.
4. Reusability is proven, not assumed — every engine feature must survive being
   used by a game in a different genre.
