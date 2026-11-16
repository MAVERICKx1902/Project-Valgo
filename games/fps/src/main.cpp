// =============================================================================
// Project Valgo — tactical FPS entry point.
//
// The game parses its own flags, then hands control to the engine application
// host. All rendering, windowing, asset and physics plumbing is engine work.
// =============================================================================
#include <memory>
#include <string>

#include "valgo/app/application.h"
#include "valgo/game/game_main.h"
#include "valgo_fps/fps_game.h"

namespace {

struct FpsCommandLine {
    bool scripted_demo = false;
    bool show_hud = true;
    bool show_debug = false;
    bool respawn_targets = true;
};

FpsCommandLine ParseFpsArguments(int argc, char** argv) {
    FpsCommandLine options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i] != nullptr ? argv[i] : "";
        if (argument == "--demo") {
            options.scripted_demo = true;
        } else if (argument == "--no-hud") {
            options.show_hud = false;
        } else if (argument == "--debug-draw") {
            options.show_debug = true;
        } else if (argument == "--no-respawn") {
            options.respawn_targets = false;
        } else if (argument == "--help" || argument == "-h") {
            std::printf("\nFPS options:\n"
                        "  --demo           Deterministic scripted input (used for CI capture)\n"
                        "  --no-hud         Hide the heads-up display\n"
                        "  --debug-draw     Draw collider bounds and the aim ray\n"
                        "  --no-respawn     Training targets stay down\n"
                        "\nControls: WASD move, Shift sprint, Ctrl crouch, Q/E lean, Space jump,\n"
                        "          LMB fire, R reload.\n");
        }
    }
    return options;
}

}  // namespace

int ValgoGameMain(int argc, char** argv) {
    const FpsCommandLine command_line = ParseFpsArguments(argc, argv);

    valgo_fps::FpsGameOptions options;
    options.scripted_demo = command_line.scripted_demo;
    options.show_hud = command_line.show_hud;
    options.show_debug_overlay = command_line.show_debug;
    options.target_respawn = command_line.respawn_targets;

    valgo::app::ApplicationDesc description;
    description.title = "Project Valgo | Tactical FPS";
    description.width = 1280;
    description.height = 720;
    description.resizable = true;
    description.log_level = valgo::LogLevel::Info;
    description.fixed_step_seconds = 1.0 / 60.0;

    const valgo::app::ApplicationResult result = valgo::app::RunApplication(
        std::make_unique<valgo_fps::FpsGame>(options), description, argc, argv);
    return result.exit_code;
}

VALGO_IMPLEMENT_GAME_MAIN()
