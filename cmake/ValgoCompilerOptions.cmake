# =============================================================================
# Shared compile/link settings.
# =============================================================================
include_guard(GLOBAL)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-fno-omit-frame-pointer)
endif()

# Position independent code is not needed for the static engine library.
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Debug builds keep engine assertions active; release builds rely on the
# validation layers/backends plus the automated test suite.
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_definitions(VALGO_RELEASE_BUILD=1)
endif()
