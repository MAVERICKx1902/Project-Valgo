# Building and Running

## Requirements

| | Windows (shipping) | Linux (validation) |
| --- | --- | --- |
| Compiler | Visual Studio 2022 (MSVC 19.3x), C++20 | GCC 12+ or Clang 15+ |
| Build system | CMake 3.24+, Ninja or MSBuild | CMake 3.24+, Ninja |
| Graphics | Direct3D 12 capable GPU, Windows 10 1903+ | none (software rasteriser) |

There are no third-party dependencies. No package manager, no vendored
libraries: the engine, the games and the test framework build with a compiler and
CMake alone.

## Configure and build

```bash
# Windows (Developer PowerShell)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

# Linux (headless validation)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `VALGO_BUILD_ENGINE` | ON | build the `valgo_engine` static library |
| `VALGO_BUILD_GAMES` | OFF | build `valgo_fps` and `valgo_orbit` + stage assets |
| `VALGO_BUILD_TESTS` | ON | build `valgo_tests` and register CTest entries |
| `VALGO_ENABLE_D3D12` | ON on Windows | compile the Direct3D 12 backend |
| `VALGO_ENABLE_SOFTWARE_RHI` | ON | compile the software rasteriser |
| `VALGO_WARNINGS_AS_ERRORS` | OFF | promote warnings in CI |

## Run

```bash
./build/bin/valgo_fps                  # windowed (D3D12 on Windows, GDI blit on Linux)
./build/bin/valgo_fps --headless --demo --deterministic --frames 400 \
    --capture /tmp/range.png          # scripted capture, no display needed
./build/bin/valgo_orbit --demo         # second validation game
```

Application CLI (shared by every game through the engine host):

```
--width N --height N
--headless
--api auto|d3d12|software
--frames N
--capture PATH  --capture-at FRAME:PATH
--deterministic
--no-vsync
--log-level trace|debug|info|warn|error
--log-file PATH
--asset-root PATH
--help
```

Unrecognised arguments are forwarded to the game, which is how `--demo` reaches
`valgo_fps` and `valgo_orbit`.

## Test

```bash
ctest --test-dir build --output-on-failure
./build/bin/valgo_tests                     # every suite
./build/bin/valgo_tests --suite=physics     # one suite
./build/bin/valgo_tests --filter=landing    # substring filter
./build/bin/valgo_tests --list
```

CTest entries:

| Test | What it proves |
| --- | --- |
| `valgo_unit_tests` | every unit + integration test |
| `valgo_engine_suite` | engine-only suites (no game code) |
| `valgo_gameplay_suite` | FPS gameplay + game module behaviour |
| `valgo_fps_headless_smoke` | the real executable starts, renders and writes a screenshot |
| `valgo_fps_demo_hits` | the scripted demo still fires and hits a target |

## Helper scripts

```bash
tools/scripts/build.sh          # configure + build (Linux/CI defaults)
tools/scripts/test.sh           # ctest with a readable summary
tools/scripts/capture.sh out.png  # deterministic screenshot of the FPS range
```

## Troubleshooting

* **`valgo_fps` opens a window but the image is black on Linux**: there is no
  GPU path; the software backend blits through GDI/Wayland only in a desktop
  session. Use `--headless --capture` on servers.
* **`--api d3d12` fails on Windows**: the log names the HRESULT. The device falls
  back to software only when `--api auto` is used, so a hard failure is visible
  instead of silent.
* **`assets/meshes/*.obj` not found**: the game target copies `games/fps/assets`
  next to the executable after build; running from a fresh directory without that
  copy needs `--asset-root <repo>/games/fps/assets`.
