# =============================================================================
# Valgo engine library
#
# Public headers live in engine/include/valgo. Nothing in this library may
# include a header from games/. The engine is the product; games are consumers.
#
# Layer rules (enforced by review, documented in docs/ARCHITECTURE.md):
#   core      -> depends on the standard library only
#   math      -> core
#   rhi       -> core, math
#   platform  -> core, input
#   input     -> core
#   scene     -> core, math, render (mesh descriptions)
#   assets    -> core, math, rhi, render
#   physics   -> core, math
#   render    -> core, math, rhi, scene
#   app/game  -> everything above
# =============================================================================
add_library(valgo_engine STATIC)

target_sources(valgo_engine PRIVATE
    src/app/application.cpp

    src/assets/asset_manager.cpp
    src/assets/mesh_library.cpp
    src/assets/obj_loader.cpp

    src/core/assert.cpp
    src/core/file.cpp
    src/core/format.cpp
    src/core/image.cpp
    src/core/log.cpp
    src/core/random.cpp
    src/core/time.cpp

    src/input/input.cpp

    src/math/math.cpp

    src/physics/character_motor.cpp
    src/physics/physics_world.cpp

    src/platform/platform.cpp
    src/platform/headless_window.cpp

    src/render/camera.cpp
    src/render/overlay2d.cpp
    src/render/renderer.cpp

    src/rhi/rhi.cpp

    src/scene/scene.cpp
)

# Software RHI backend: always available. It is the CI/test reference backend.
if(VALGO_ENABLE_SOFTWARE_RHI)
    target_sources(valgo_engine PRIVATE
        src/rhi/software/software_rhi.cpp
    )
endif()

# Direct3D 12 backend (Windows only).
if(VALGO_ENABLE_D3D12)
    target_sources(valgo_engine PRIVATE
        src/platform/win32_window.cpp
        src/rhi/d3d12/d3d12_device.cpp
        src/rhi/d3d12/d3d12_shaders.cpp
    )
    target_link_libraries(valgo_engine PRIVATE d3d12 dxgi dxguid d3dcompiler winmm)
endif()

if(VALGO_PLATFORM_WINDOWS AND NOT VALGO_ENABLE_D3D12)
    # A software-only Windows build still needs the Win32 window.
    target_sources(valgo_engine PRIVATE src/platform/win32_window.cpp)
    target_link_libraries(valgo_engine PRIVATE winmm)
endif()

target_include_directories(valgo_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(valgo_engine
    PUBLIC
        VALGO_PLATFORM_NAME="${VALGO_PLATFORM_NAME}"
    PRIVATE
        VALGO_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
)

valgo_configure_target(valgo_engine)
