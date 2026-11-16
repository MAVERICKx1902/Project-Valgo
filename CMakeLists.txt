# =============================================================================
# Project Valgo — top level build
#
# Two clearly separated products live in this repository:
#   engine/  : the reusable, game-agnostic native engine (primary product)
#   games/   : games that consume the engine through its public headers only
#
# The engine never includes anything from games/. The games never link against
# engine internals: they only see ${PROJECT_SOURCE_DIR}/engine/include.
# =============================================================================
cmake_minimum_required(VERSION 3.24)

project(ProjectValgo
    VERSION 0.1.0
    DESCRIPTION "Valgo: a modular native game engine and its first game, a tactical FPS"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ValgoPlatform)
include(ValgoCompilerOptions)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

option(VALGO_BUILD_ENGINE "Build the engine" ON)
option(VALGO_BUILD_GAMES "Build the games that consume the engine" ON)
option(VALGO_BUILD_TESTS "Build the engine + game test suites" ON)
option(VALGO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

valgo_print_configuration()

if(VALGO_BUILD_ENGINE)
    add_subdirectory(engine)
endif()

if(VALGO_BUILD_GAMES)
    add_subdirectory(games)
endif()

if(VALGO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "Project Valgo ${PROJECT_VERSION}")
message(STATUS "  platform            : ${VALGO_PLATFORM_NAME}")
message(STATUS "  rendering backend   : ${VALGO_RHI_BACKEND_NAME}")
message(STATUS "  build engine        : ${VALGO_BUILD_ENGINE}")
message(STATUS "  build games         : ${VALGO_BUILD_GAMES}")
message(STATUS "  build tests         : ${VALGO_BUILD_TESTS}")
message(STATUS "  output directory    : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
