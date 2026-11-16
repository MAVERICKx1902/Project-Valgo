// =============================================================================
// Project Valgo — platform window abstraction.
//
// Implementations
//   Win32Window    : real Windows application window (Win32, no third party).
//   HeadlessWindow : no OS window; used by CI, tests and offline capture.
//
// The window produces input snapshots and owns the native handle that the RHI
// uses to create a swap chain. The software backend also asks the window to
// blit its framebuffer so a GPU-less Windows machine can still run the game.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "valgo/core/types.h"
#include "valgo/input/input.h"

namespace valgo::platform {

struct WindowDesc {
    std::string title = "Valgo";
    u32 width = 1280;
    u32 height = 720;
    bool resizable = true;
    bool headless = false;
    bool borderless = false;
};

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool IsValid() const = 0;
    virtual bool IsHeadless() const = 0;

    virtual u32 width() const = 0;
    virtual u32 height() const = 0;

    /// Native handle (HWND on Windows). nullptr for headless windows.
    virtual void* native_handle() const = 0;

    virtual void SetTitle(std::string_view title) = 0;
    virtual void SetCursorVisible(bool visible) = 0;
    virtual void SetRelativeMouseMode(bool enabled) = 0;

    /// Processes pending OS events, updating the input state.
    virtual void PollEvents() = 0;

    /// Current input snapshot for this frame.
    virtual input::InputState& input() = 0;

    /// True when the user asked to close the window (or the app requested exit).
    virtual bool ShouldClose() const = 0;
    virtual void RequestClose() = 0;

    /// Presents a CPU framebuffer (software backend). No-op for headless
    /// windows and not used by the D3D12 backend.
    virtual void BlitFramebuffer(const u8* rgba_pixels, u32 width, u32 height) = 0;

    /// Sets a directory relative to the executable for asset lookups.
    virtual void SetAssetRoot(std::string_view root) = 0;
};

std::unique_ptr<IWindow> CreateWindow(const WindowDesc& desc);

/// Initialises platform services (DPI awareness, COM, timers). Safe to call
/// multiple times. Must be called before creating a window.
void InitializePlatform();

/// Shuts platform services down.
void ShutdownPlatform();

}  // namespace valgo::platform
