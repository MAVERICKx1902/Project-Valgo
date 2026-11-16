// =============================================================================
// Project Valgo — Orbit Runner entry point (second validation game).
// =============================================================================
#include <cstdio>
#include <memory>
#include <string>

#include "valgo/app/application.h"
#include "valgo/game/game_main.h"
#include "valgo_orbit/orbit_game.h"

int ValgoGameMain(int argc, char** argv) {
    valgo_orbit::OrbitGameOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i] != nullptr ? argv[i] : "";
        if (argument == "--demo") {
            options.scripted_demo = true;
        } else if (argument == "--help" || argument == "-h") {
            std::printf("\nOrbit Runner options:\n  --demo   Deterministic scripted input\n");
        }
    }

    valgo::app::ApplicationDesc description;
    description.title = "Valgo | Orbit Runner";
    description.width = 1280;
    description.height = 720;
    description.log_level = valgo::LogLevel::Info;
    description.fixed_step_seconds = 1.0 / 60.0;

    const valgo::app::ApplicationResult result =
        valgo::app::RunApplication(std::make_unique<valgo_orbit::OrbitGame>(options), description, argc,
                                   argv);
    return result.exit_code;
}

VALGO_IMPLEMENT_GAME_MAIN()
