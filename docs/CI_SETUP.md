# Continuous Integration

The workflow below is ready to use. It is checked in as documentation rather than
as `.github/workflows/ci.yml` because the automation identity that produced this
commit is not allowed to create workflow files:

```
refusing to allow a GitHub App to create or update workflow
`.github/workflows/ci.yml` without `workflows` permission
```

To enable it, copy the YAML block into `.github/workflows/ci.yml` (or paste it in
the GitHub UI) and push from an account with `workflows` permission.

## What it runs

* **Linux headless** — configure, build, the full CTest suite, and two headless
  captures (`valgo_fps`, `valgo_orbit`) uploaded as artifacts. This is the
  authoritative regression gate: it exercises the engine, both games and the
  software rasteriser with no GPU.
* **Windows (MSVC)** — configure and build with the Direct3D 12 backend enabled
  and run the test suite. The software backend keeps the executable smoke tests
  meaningful on GPU-less runners; a dedicated D3D12 device test is added once the
  backend has had its first Windows compile (see `docs/MILESTONES.md`).

## Workflow

```yaml
name: CI

on:
  push:
    branches: ["**"]
  pull_request:

jobs:
  linux-headless:
    name: Linux headless (GCC, full test suite)
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4

      - name: Install toolchain
        run: |
          sudo apt-get update
          sudo apt-get install -y ninja-build g++ cmake

      - name: Configure
        run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVALGO_BUILD_GAMES=ON

      - name: Build (warnings are errors in CI)
        run: cmake --build build --parallel

      - name: Unit and integration tests
        run: ctest --test-dir build --output-on-failure

      - name: Headless capture artifact
        run: |
          ./build/bin/valgo_fps --headless --deterministic --width 640 --height 360 \
            --frames 120 --capture build/ci_frame.png --log-level info
          ./build/bin/valgo_orbit --headless --demo --deterministic --width 640 --height 360 \
            --frames 240 --capture build/ci_orbit.png --log-level info

      - uses: actions/upload-artifact@v4
        with:
          name: headless-captures
          path: |
            build/ci_frame.png
            build/ci_orbit.png
          if-no-files-found: error

  windows-d3d12:
    name: Windows (MSVC, Direct3D 12)
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4

      - name: Configure
        run: cmake -S . -B build -A x64 -DVALGO_BUILD_GAMES=ON

      - name: Build
        run: cmake --build build --config Debug --parallel

      - name: Unit and integration tests
        run: ctest --test-dir build --build-config Debug --output-on-failure

      # The software backend runs on the GitHub runner, so the executable smoke
      # tests are meaningful even without a GPU. D3D12 device creation is
      # exercised by a dedicated test once the backend has had its first
      # Windows validation pass (see docs/MILESTONES.md).
```
