# =============================================================================
# Platform + rendering backend detection.
#
# The engine targets Windows/Direct3D 12 as its shipping configuration. Because
# continuous validation must run on headless Linux CI as well, the Rendering
# Hardware Interface (RHI) has a second, fully functional "Software" backend.
# That backend is not a stub: it rasterises the engine's real shader programs,
# which lets the exact same renderer, scene and game code be tested and
# screenshot-verified on any machine (see docs/RHI_AND_BACKENDS.md).
# =============================================================================
include_guard(GLOBAL)

if(WIN32)
    set(VALGO_PLATFORM_WINDOWS TRUE)
    set(VALGO_PLATFORM_NAME "windows")
    add_compile_definitions(VALGO_PLATFORM_WINDOWS=1)
elseif(UNIX AND NOT APPLE)
    set(VALGO_PLATFORM_LINUX TRUE)
    set(VALGO_PLATFORM_NAME "linux")
    add_compile_definitions(VALGO_PLATFORM_LINUX=1)
else()
    message(FATAL_ERROR "Project Valgo currently supports Windows and Linux (headless) hosts.")
endif()

# -----------------------------------------------------------------------------
# Direct3D 12 backend: the default and only GPU backend in this milestone.
# -----------------------------------------------------------------------------
option(VALGO_ENABLE_D3D12 "Build the Direct3D 12 RHI backend (Windows only)" ${VALGO_PLATFORM_WINDOWS})
if(VALGO_ENABLE_D3D12 AND NOT VALGO_PLATFORM_WINDOWS)
    message(STATUS "VALGO_ENABLE_D3D12 forced OFF: Direct3D 12 requires a Windows host.")
    set(VALGO_ENABLE_D3D12 OFF)
endif()

# -----------------------------------------------------------------------------
# Software backend: real rasteriser used for tests, CI and headless capture.
# -----------------------------------------------------------------------------
option(VALGO_ENABLE_SOFTWARE_RHI "Build the portable software rasteriser backend" ON)

if(VALGO_ENABLE_D3D12)
    add_compile_definitions(VALGO_RHI_HAS_D3D12=1)
endif()
if(VALGO_ENABLE_SOFTWARE_RHI)
    add_compile_definitions(VALGO_RHI_HAS_SOFTWARE=1)
endif()

if(VALGO_PLATFORM_WINDOWS AND VALGO_ENABLE_D3D12)
    set(VALGO_RHI_BACKEND_NAME "Direct3D 12 (default) + Software")
else()
    set(VALGO_RHI_BACKEND_NAME "Software (headless)")
endif()

function(valgo_print_configuration)
    message(STATUS "Valgo platform detection: ${VALGO_PLATFORM_NAME}")
endfunction()

# Convenience: apply the shared warning/introspection settings to a target.
function(valgo_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        if(VALGO_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-unused-parameter)
        if(VALGO_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
