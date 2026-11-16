# =============================================================================
# Automated validation.
#
#   valgo_tests   : engine + game correctness (unit + integration, headless)
#   CTest targets : convenience wrappers, including binary smoke runs
# =============================================================================
add_executable(valgo_tests
    framework/test_framework.cpp
    framework/engine_harness.cpp

    unit/test_core.cpp
    unit/test_math.cpp
    unit/test_physics.cpp
    unit/test_scene.cpp
    unit/test_assets.cpp
    unit/test_rhi_software.cpp
    unit/test_renderer.cpp
    unit/test_fps_gameplay.cpp

    integration/test_headless_frame.cpp
    integration/test_engine_reuse.cpp
)

target_include_directories(valgo_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/framework
    ${CMAKE_SOURCE_DIR}/games/fps/src
)

target_link_libraries(valgo_tests PRIVATE valgo_engine valgo_fps_lib valgo_orbit_lib)

# Tests locate repository assets (OBJ files) relative to the project root.
target_compile_definitions(valgo_tests PRIVATE VALGO_PROJECT_ROOT="${PROJECT_SOURCE_DIR}")
valgo_configure_target(valgo_tests)

add_test(NAME valgo_unit_tests COMMAND valgo_tests)
add_test(NAME valgo_engine_suite COMMAND valgo_tests --suite=engine)
add_test(NAME valgo_gameplay_suite COMMAND valgo_tests --suite=gameplay)

# Smoke test: run the real FPS application headless for a few deterministic
# frames and write a screenshot. This is the "launch the application" step of
# the milestone checklist, automated.
add_test(NAME valgo_fps_headless_smoke
    COMMAND valgo_fps --headless --deterministic --width 320 --height 180
            --frames 90 --capture ${CMAKE_CURRENT_BINARY_DIR}/smoke_frame.png
            --log-level info
)
set_tests_properties(valgo_fps_headless_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "screenshot written"
    TIMEOUT 120
)

# Demo playback determinism: the scripted demo must fire shots and hit targets.
add_test(NAME valgo_fps_demo_hits
    COMMAND valgo_fps --headless --demo --deterministic --width 320 --height 180
            --frames 420 --log-level info
)
set_tests_properties(valgo_fps_demo_hits PROPERTIES
    PASS_REGULAR_EXPRESSION "hits, [1-9]"
    TIMEOUT 180
)
