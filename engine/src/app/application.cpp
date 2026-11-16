#include "valgo/app/application.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "valgo/core/file.h"
#include "valgo/core/image.h"
#include "valgo/core/time.h"
#include "valgo/game/game_main.h"
#include "valgo/platform/window.h"

#if defined(VALGO_PLATFORM_WINDOWS)
#include <windows.h>
#endif

#ifndef VALGO_PROJECT_ROOT
#define VALGO_PROJECT_ROOT ""
#endif

namespace valgo::app {
namespace {

constexpr const char* kCategory = "app";
constexpr u32 kCaptureLastFrame = 0xFFFFFFFFu;

bool ParseU32(const char* text, u32& out_value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    out_value = static_cast<u32>(value);
    return true;
}

LogLevel ParseLogLevel(std::string_view text, bool& valid) {
    valid = true;
    if (text == "trace") return LogLevel::Trace;
    if (text == "debug") return LogLevel::Debug;
    if (text == "info") return LogLevel::Info;
    if (text == "warn") return LogLevel::Warn;
    if (text == "error") return LogLevel::Error;
    valid = false;
    return LogLevel::Info;
}

rhi::GraphicsApi ParseApi(std::string_view text, bool& valid) {
    valid = true;
    if (text == "auto") return rhi::GraphicsApi::Auto;
    if (text == "d3d12") return rhi::GraphicsApi::D3D12;
    if (text == "software") return rhi::GraphicsApi::Software;
    valid = false;
    return rhi::GraphicsApi::Auto;
}

std::string DefaultAssetRoot() {
    if (std::strlen(VALGO_PROJECT_ROOT) > 0) {
        return std::string(VALGO_PROJECT_ROOT);
    }
    const std::string executable_dir = ExecutableDirectory();
    if (!executable_dir.empty()) {
        return executable_dir;
    }
    return std::string(".");
}

bool WriteScreenshot(rhi::SwapChain& swap_chain, const std::string& path) {
    std::vector<u8> rgba;
    if (!swap_chain.ReadPixels(rgba)) {
        LogWarn(kCategory, "screenshot '{}' skipped: the {} backend cannot read pixels back yet",
                path, rhi::GraphicsApiName(swap_chain.format() == rhi::Format::Unknown
                                               ? rhi::GraphicsApi::Software
                                               : rhi::GraphicsApi::Software));
        return false;
    }
    Image image;
    image.width = swap_chain.width();
    image.height = swap_chain.height();
    image.pixels = std::move(rgba);
    if (!image.SavePng(path)) {
        LogError(kCategory, "failed to write screenshot '{}'", path);
        return false;
    }
    LogInfo(kCategory, "screenshot written: {} ({}x{})", path, image.width, image.height);
    return true;
}

}  // namespace

CommandLineOptions ParseCommandLine(int argc, char** argv) {
    CommandLineOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i] != nullptr ? argv[i] : "";
        auto next = [&](const char* name) -> const char* {
            if (i + 1 < argc) {
                return argv[i + 1];
            }
            LogWarn(kCategory, "missing value for {}", name);
            return nullptr;
        };

        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--width") {
            u32 value = 0;
            if (const char* text = next("--width"); text != nullptr && ParseU32(text, value)) {
                options.width = value;
                ++i;
            }
        } else if (argument == "--height") {
            u32 value = 0;
            if (const char* text = next("--height"); text != nullptr && ParseU32(text, value)) {
                options.height = value;
                ++i;
            }
        } else if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--deterministic") {
            options.deterministic = true;
        } else if (argument == "--api") {
            if (const char* text = next("--api"); text != nullptr) {
                bool valid = false;
                const rhi::GraphicsApi api = ParseApi(text, valid);
                if (valid) {
                    options.api = api;
                    options.api_explicitly_set = true;
                } else {
                    LogWarn(kCategory, "unknown rendering API '{}'", text);
                }
                ++i;
            }
        } else if (argument == "--frames") {
            u32 value = 0;
            if (const char* text = next("--frames"); text != nullptr && ParseU32(text, value)) {
                options.max_frames = value;
                ++i;
            }
        } else if (argument == "--capture") {
            if (const char* text = next("--capture"); text != nullptr) {
                options.captures.emplace_back(kCaptureLastFrame, std::string(text));
                ++i;
            }
        } else if (argument == "--capture-at") {
            if (const char* text = next("--capture-at"); text != nullptr) {
                const std::string spec(text);
                const usize separator = spec.find(':');
                if (separator != std::string::npos) {
                    u32 frame = 0;
                    if (ParseU32(spec.substr(0, separator).c_str(), frame)) {
                        options.captures.emplace_back(frame, spec.substr(separator + 1));
                    }
                } else {
                    LogWarn(kCategory, "--capture-at expects FRAME:PATH, got '{}'", spec);
                }
                ++i;
            }
        } else if (argument == "--no-vsync") {
            options.vsync = false;
        } else if (argument == "--log-level") {
            if (const char* text = next("--log-level"); text != nullptr) {
                bool valid = false;
                const LogLevel level = ParseLogLevel(text, valid);
                if (valid) {
                    options.log_level = level;
                    options.log_level_set = true;
                } else {
                    LogWarn(kCategory, "unknown log level '{}'", text);
                }
                ++i;
            }
        } else if (argument == "--log-file") {
            if (const char* text = next("--log-file"); text != nullptr) {
                options.log_file = text;
                ++i;
            }
        } else if (argument == "--asset-root") {
            if (const char* text = next("--asset-root"); text != nullptr) {
                options.asset_root = text;
                ++i;
            }
        } else {
            options.extra_args.push_back(argument);
        }
    }
    return options;
}

void PrintCommandLineHelp(std::string_view executable_name) {
    std::printf(
        "Usage: %s [engine options] [game options]\n"
        "\n"
        "Engine options:\n"
        "  --width N                Window width  (default 1280)\n"
        "  --height N               Window height (default 720)\n"
        "  --headless               Run without a window (software renderer + capture)\n"
        "  --api auto|d3d12|software Rendering backend selection\n"
        "  --frames N               Exit after N frames (0 = run until closed)\n"
        "  --capture PATH           Write a PNG screenshot on the final frame\n"
        "  --capture-at FRAME:PATH  Write a PNG screenshot at a specific frame\n"
        "  --deterministic          Exactly one fixed simulation step per frame\n"
        "  --no-vsync               Disable vertical sync\n"
        "  --log-level LEVEL        trace|debug|info|warn|error\n"
        "  --log-file PATH          Mirror logs into a file\n"
        "  --asset-root PATH        Asset root directory\n"
        "  --help                   Show this help\n",
        std::string(executable_name).c_str());
}

ApplicationResult RunApplication(std::unique_ptr<game::IGameModule> game, const ApplicationDesc& desc,
                                 int argc, char** argv) {
#if defined(VALGO_PLATFORM_WINDOWS)
    // GUI-subsystem executables have no console; borrow the parent's when the
    // game was launched from one so logs and --help remain visible.
    if (AttachConsole(ATTACH_PARENT_PROCESS) != 0) {
        std::FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
#endif

    ApplicationResult result;

    if (game == nullptr) {
        LogError(kCategory, "no game module supplied");
        result.exit_code = 1;
        return result;
    }

    CommandLineOptions options = ParseCommandLine(argc, argv);
    if (options.show_help) {
        PrintCommandLineHelp(game->name());
        return result;
    }

    // --- Logging ----------------------------------------------------------
    Logger& logger = Logger::Get();
    if (options.log_level_set) {
        logger.SetLevel(options.log_level);
    } else {
        logger.SetLevel(desc.log_level);
    }
    if (!options.log_file.empty()) {
        logger.AddFileSink(options.log_file);
    }

    LogInfo(kCategory, "=== {} ===", game->name());
    LogInfo(kCategory, "build: {} {} | {}", __DATE__, __TIME__,
            rhi::IsApiAvailable(rhi::GraphicsApi::D3D12) ? "D3D12 backend available"
                                                         : "software backend only");

    // --- Merge command line over application defaults ---------------------
    ApplicationDesc settings = desc;
    if (options.width != 0) {
        settings.width = options.width;
    }
    if (options.height != 0) {
        settings.height = options.height;
    }
    if (options.headless) {
        settings.headless = true;
    }
    if (options.deterministic) {
        settings.deterministic = true;
    }
    if (options.api_explicitly_set) {
        settings.api = options.api;
    }
    if (options.max_frames != 0) {
        settings.max_frames = options.max_frames;
    }
    settings.vsync = settings.vsync && options.vsync;
    if (!options.asset_root.empty()) {
        settings.asset_root = options.asset_root;
    }
    if (settings.asset_root.empty()) {
        settings.asset_root = DefaultAssetRoot();
    }

    // --- Platform + window -------------------------------------------------
    platform::InitializePlatform();
    platform::WindowDesc window_desc;
    window_desc.title = settings.title.empty() ? std::string(game->name()) : settings.title;
    window_desc.width = settings.width;
    window_desc.height = settings.height;
    window_desc.resizable = settings.resizable;
    window_desc.headless = settings.headless;

    std::unique_ptr<platform::IWindow> window = platform::CreateWindow(window_desc);
    if (window == nullptr) {
        LogError(kCategory, "failed to create a window");
        result.exit_code = 1;
        return result;
    }
    window->SetAssetRoot(settings.asset_root);

    // --- Rendering device --------------------------------------------------
    rhi::DeviceDesc device_desc;
    device_desc.api = settings.api;
    device_desc.application_name = std::string(game->name());
    device_desc.enable_validation = settings.enable_validation;

    rhi::DeviceInfo device_info;
    std::unique_ptr<rhi::Device> device = rhi::CreateFallbackDevice(device_desc, device_info);
    if (device == nullptr) {
        LogError(kCategory, "no rendering device available");
        window->RequestClose();
        return result;
    }
    result.backend_name = std::string(rhi::GraphicsApiName(device_info.api));

    rhi::NativeWindow native_window;
    native_window.window_handle = window->native_handle();
    native_window.width = window->width();
    native_window.height = window->height();
    native_window.headless = window->IsHeadless();

    rhi::SwapChainDesc swap_desc;
    swap_desc.width = window->width();
    swap_desc.height = window->height();
    swap_desc.format = rhi::Format::BGRA8_SRGB;
    swap_desc.buffer_count = 3;
    swap_desc.vsync = settings.vsync;

    std::unique_ptr<rhi::SwapChain> swap_chain = device->CreateSwapChain(native_window, swap_desc);
    if (swap_chain == nullptr) {
        LogError(kCategory, "failed to create a swap chain");
        return result;
    }

    // --- Engine services ---------------------------------------------------
    render::Renderer renderer(*device);
    scene::Scene scene;
    physics::PhysicsWorld physics_world;
    assets::AssetManager asset_manager(*device);
    asset_manager.SetAssetRoot(settings.asset_root);
    render::Camera camera;
    camera.SetPerspective(70.0f, static_cast<f32>(swap_chain->width()) /
                                     static_cast<f32>(std::max(swap_chain->height(), 1u)),
                          0.05f, 400.0f);

    game::GameServices services;
    services.window = window.get();
    services.device = device.get();
    services.swap_chain = swap_chain.get();
    services.renderer = &renderer;
    services.scene = &scene;
    services.physics = &physics_world;
    services.assets = &asset_manager;
    services.input = &window->input();
    services.camera = &camera;
    services.viewport_width = swap_chain->width();
    services.viewport_height = swap_chain->height();

    LogInfo(kCategory, "asset root: {}", settings.asset_root);
    LogInfo(kCategory, "viewport: {}x{} | headless: {} | deterministic: {}", services.viewport_width,
            services.viewport_height, window->IsHeadless() ? "yes" : "no",
            settings.deterministic ? "yes" : "no");

    game->OnStartup(services);

    // --- Frame loop --------------------------------------------------------
    FrameTimer timer(settings.fixed_step_seconds);
    Stopwatch run_stopwatch;
    Stopwatch fps_stopwatch;
    u32 frames_since_log = 0;
    u64 frame_index = 0;
    bool running = true;

    const bool software_presentation = device_info.software && !window->IsHeadless();

    while (running) {
        window->PollEvents();
        if (window->ShouldClose()) {
            break;
        }
        if (settings.max_frames != 0 && frame_index >= settings.max_frames) {
            break;
        }

        FrameContext timing;
        if (settings.deterministic) {
            timing.frame_index = frame_index;
            timing.delta_seconds = settings.fixed_step_seconds;
            timing.fixed_steps = 1;
            timing.fixed_step_seconds = static_cast<f32>(settings.fixed_step_seconds);
            timing.total_seconds = static_cast<f64>(frame_index) * settings.fixed_step_seconds;
            timing.interpolation_alpha = 0.0f;
        } else {
            timing = timer.Advance();
        }

        // Window resize -> swap chain resize.
        if (!window->IsHeadless() &&
            (window->width() != swap_chain->width() || window->height() != swap_chain->height())) {
            if (swap_chain->Resize(window->width(), window->height())) {
                LogDebug(kCategory, "resized to {}x{}", window->width(), window->height());
            }
        }

        services.viewport_width = swap_chain->width();
        services.viewport_height = swap_chain->height();
        services.input = &window->input();

        game::GameFrame frame;
        frame.frame_index = frame_index;
        frame.total_seconds = timing.total_seconds;
        frame.delta_seconds = static_cast<f32>(timing.delta_seconds);
        frame.fixed_steps = std::max(timing.fixed_steps, 1u);
        frame.fixed_step_seconds = static_cast<f32>(settings.fixed_step_seconds);

        renderer.BeginFrame();
        game->OnUpdate(frame, services);
        scene.UpdateWorldTransforms();

        camera.SetAspectRatio(static_cast<f32>(swap_chain->width()) /
                              static_cast<f32>(std::max(swap_chain->height(), 1u)));
        camera.SetClipPlanes(0.05f, 400.0f);

        if (!swap_chain->BeginFrame()) {
            LogError(kCategory, "swap chain BeginFrame failed");
            break;
        }
        renderer.Render(*swap_chain->command_list(), scene, camera, swap_chain->width(),
                        swap_chain->height());
        swap_chain->Present();

        // --- Presentation + capture --------------------------------------
        const bool will_exit =
            (settings.max_frames != 0 && frame_index + 1 >= settings.max_frames) || window->ShouldClose();

        if (software_presentation) {
            std::vector<u8> pixels;
            if (swap_chain->ReadPixels(pixels)) {
                window->BlitFramebuffer(pixels.data(), swap_chain->width(), swap_chain->height());
            }
        }

        for (const auto& [capture_frame, path] : options.captures) {
            const bool matches = (capture_frame == frame_index) ||
                                 (capture_frame == kCaptureLastFrame && will_exit);
            if (matches) {
                WriteScreenshot(*swap_chain, path);
            }
        }

        game->OnFrameEnd(frame, services);

        ++frame_index;
        ++frames_since_log;

        if (!will_exit && settings.vsync && software_presentation) {
            // The software backend has no vsync; pace it to ~60 Hz.
            const f64 elapsed = fps_stopwatch.Seconds();
            const f64 target = static_cast<f64>(frames_since_log) / 60.0;
            if (elapsed < target) {
                SleepMilliseconds(static_cast<u32>((target - elapsed) * 1000.0));
            }
        }

        if (fps_stopwatch.Seconds() >= 2.0) {
            const f64 fps = static_cast<f64>(frames_since_log) / fps_stopwatch.Seconds();
            LogInfo(kCategory, "frame {} | {} fps | draws {} | meshes {} | tris {} | lines {}", frame_index,
                    fps, renderer.stats().draw_calls, renderer.stats().meshes_drawn,
                    renderer.stats().triangles, renderer.stats().debug_lines);
            frames_since_log = 0;
            fps_stopwatch.Reset();
        }

        if (will_exit) {
            running = false;
        }
    }

    const f64 total_seconds = std::max(run_stopwatch.Seconds(), 1.0e-6);
    game->OnShutdown(services);
    device->WaitIdle();

    scene.Clear();
    platform::ShutdownPlatform();

    result.frames_rendered = frame_index;
    result.average_fps = static_cast<f64>(frame_index) / total_seconds;
    LogInfo(kCategory, "shutdown after {} frames in {}s ({} fps average)", frame_index,
            total_seconds, result.average_fps);
    return result;
}

}  // namespace valgo::app

#if defined(VALGO_PLATFORM_WINDOWS)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    return ValgoGameMain(__argc, __argv);
}
#endif
