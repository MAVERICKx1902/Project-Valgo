// =============================================================================
// Project Valgo — application host.
//
// Owns the window, rendering device, scene, physics world and asset manager, and
// runs the frame loop. Games provide an IGameModule and an ApplicationDesc; they
// do not create devices or pump messages themselves.
//
// Command line (shared by every Valgo game):
//   --width N --height N          window / offscreen resolution
//   --headless                    no window (software backend + capture)
//   --api auto|d3d12|software     rendering backend selection
//   --frames N                    run N frames then exit (0 = until closed)
//   --capture PATH                write a PNG screenshot on the final frame
//   --capture-at FRAME:PATH       write a PNG screenshot at a specific frame
//   --deterministic               exactly one fixed step per frame
//   --no-vsync                    disable vsync
//   --log-level trace|debug|info|warn|error
//   --log-file PATH               mirror logs to a file
//   --asset-root PATH             asset root directory
//   --help                        print this help
// Unknown arguments are forwarded to the game.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "valgo/core/log.h"
#include "valgo/game/game_module.h"
#include "valgo/rhi/rhi.h"

namespace valgo::app {

struct CommandLineOptions {
    bool show_help = false;
    std::string title;
    u32 width = 0;
    u32 height = 0;
    bool headless = false;
    rhi::GraphicsApi api = rhi::GraphicsApi::Auto;
    bool api_explicitly_set = false;
    bool vsync = true;
    bool deterministic = false;
    u32 max_frames = 0;
    std::vector<std::pair<u32, std::string>> captures;  ///< (frame, path); UINT32_MAX = last frame
    std::string log_file;
    LogLevel log_level = LogLevel::Info;
    bool log_level_set = false;
    std::string asset_root;
    std::vector<std::string> extra_args;
};

/// Parses the shared Valgo command line. Never fails the process: invalid values
/// are reported and ignored.
CommandLineOptions ParseCommandLine(int argc, char** argv);

void PrintCommandLineHelp(std::string_view executable_name = "game");

struct ApplicationDesc {
    std::string title = "Valgo";
    u32 width = 1280;
    u32 height = 720;
    bool resizable = true;
    bool headless = false;
    rhi::GraphicsApi api = rhi::GraphicsApi::Auto;
    bool vsync = true;
    bool deterministic = false;
    u32 max_frames = 0;
    std::string asset_root;
    LogLevel log_level = LogLevel::Info;
    bool enable_validation = true;
    f64 fixed_step_seconds = 1.0 / 60.0;
};

struct ApplicationResult {
    int exit_code = 0;
    u64 frames_rendered = 0;
    f64 average_fps = 0.0;
    std::string backend_name;
};

/// Runs the application until the game requests exit or `max_frames` is hit.
ApplicationResult RunApplication(std::unique_ptr<game::IGameModule> game, const ApplicationDesc& desc,
                                 int argc = 0, char** argv = nullptr);

}  // namespace valgo::app
