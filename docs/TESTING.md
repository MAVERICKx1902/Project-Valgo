# Testing

The test suite is dependency-free and runs headless on any machine. It is the
reason the engine can be developed in parallel with its first game without
building on broken functionality.

## Layout

```
tests/framework/test_framework.{h,cpp}   registry, expectations, filter, main
tests/framework/engine_harness.{h,cpp}   headless engine stack for integration tests
tests/unit/                              engine + gameplay unit tests
tests/integration/                       full-frame and reuse tests
```

`valgo_tests` links the engine, the FPS gameplay library and the Orbit Runner
library, so a single process can exercise all three without a window.

## Framework

```cpp
VALGO_TEST("suite", "name") { ... }
VALGO_CHECK(condition);
VALGO_CHECK_MSG(condition, "what broke");
VALGO_CHECK_EQ(a, b);
VALGO_CHECK_NEAR(actual, expected, tolerance);
VALGO_REQUIRE(condition);   // stops the test
```

Expectations record failures and continue, so one broken assertion does not hide
the rest. `--suite=<name>`, `--filter=<substring>` and `--list` select tests.

## Suites and what they protect

| Suite | Covers |
| --- | --- |
| `core` | formatting, logging (levels, ring buffer, sinks), PCG random, files, PNG encoding + CRC, frame timer, stopwatch |
| `math` | vector/quaternion/matrix conventions, AABB and ray primitives, camera projection round trips, frustum |
| `physics` | raycasts (closest hit, layers, range), motor grounding, walking speed, step-up, walls, jumps, landing transitions |
| `scene` | entity lifetime and generation reuse, components, hierarchy transforms, reparenting, clear |
| `assets` | primitive topology/normals/bounds, OBJ parsing (quads, missing normals, malformed), asset cache, textures |
| `rhi` | software device, fallback, buffers, textures, clear colour sRGB encoding, rasterisation, resize |
| `renderer` | transform bounds, draw stats, frustum culling, debug geometry lifetime, overlay text, scene lights |
| `gameplay` | controller movement/stance/jump/lean/damage, weapon cadence/reload, FPS module via engine harness, hit registration |
| `integration` | full frame render + determinism + resize + screenshot IO, engine-without-game, two games on one engine |

## Determinism

* Simulation runs on a fixed step; tests drive `EngineHarness::StepFrame` with
  explicit step counts, never wall-clock time.
* `rendering_is_deterministic_across_runs` renders the same scene twice in fresh
  harnesses and compares image checksums.
* Scripted demo drivers (`--demo` in both games) are deterministic, which is what
  makes the CI smoke tests meaningful.

## Assets in tests

Tests need real files (OBJ meshes). `VALGO_PROJECT_ROOT` is defined for the test
target by CMake, so tests read repository assets directly and never depend on the
working directory.

## Adding a test

1. Pick the suite that owns the behaviour (engine behaviour → engine suite;
   gameplay rules → `gameplay`).
2. Test the failure mode that motivated the test, not only the happy path.
3. If it needs a game, drive it through `EngineHarness` and `IGameModule`; never
   construct engine internals.
4. Keep runtime reasonable: the whole suite budget is ~60 s on CI, and heavy
   integration runs use small viewports (160×90).
