# =============================================================================
# Games.
#
# Games are *consumers* of the engine: they include engine public headers and
# link valgo_engine. They must never include engine/src or rely on engine
# internals, and the engine must never reference these targets.
# =============================================================================

# -----------------------------------------------------------------------------
# Project Valgo | Tactical FPS — the engine's first game and proof of concept.
# The gameplay code lives in a library so that automated tests can drive the
# exact same module the executable runs.
# -----------------------------------------------------------------------------
add_library(valgo_fps_lib STATIC
    fps/src/fps_game.cpp
    fps/src/player_controller.cpp
    fps/src/test_map.cpp
    fps/src/weapon.cpp
)

target_include_directories(valgo_fps_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/fps/include
    ${CMAKE_CURRENT_SOURCE_DIR}/fps/src
)

target_link_libraries(valgo_fps_lib PUBLIC valgo_engine)
valgo_configure_target(valgo_fps_lib)

add_executable(valgo_fps fps/src/main.cpp)
target_link_libraries(valgo_fps PRIVATE valgo_fps_lib)
valgo_configure_target(valgo_fps)

if(WIN32)
    set_target_properties(valgo_fps PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Copy game assets next to the executable for standalone runs.
add_custom_command(TARGET valgo_fps POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/fps/assets"
        "$<TARGET_FILE_DIR:valgo_fps>/assets"
    COMMENT "Staging FPS assets"
)

# -----------------------------------------------------------------------------
# Second validation game (different genre, no FPS code): proves the engine is
# genuinely reusable. See docs/MILESTONES.md, milestone 6.
# -----------------------------------------------------------------------------
add_library(valgo_orbit_lib STATIC
    orbit/src/orbit_game.cpp
)
target_include_directories(valgo_orbit_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/orbit/include
)
target_link_libraries(valgo_orbit_lib PUBLIC valgo_engine)
valgo_configure_target(valgo_orbit_lib)

add_executable(valgo_orbit orbit/src/main.cpp)
target_link_libraries(valgo_orbit PRIVATE valgo_orbit_lib)
valgo_configure_target(valgo_orbit)

if(WIN32)
    set_target_properties(valgo_orbit PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
