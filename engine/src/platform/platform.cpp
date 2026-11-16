#include "valgo/platform/window.h"

#include "valgo/core/log.h"

#if defined(VALGO_PLATFORM_WINDOWS)
#include <windows.h>

#include <objbase.h>
#else
#include <unistd.h>
#endif

namespace valgo::platform {
namespace {

constexpr const char* kCategory = "platform";
bool g_initialised = false;

}  // namespace

std::unique_ptr<IWindow> CreateHeadlessWindow(const WindowDesc& desc);
#if defined(VALGO_PLATFORM_WINDOWS)
std::unique_ptr<IWindow> CreateWin32Window(const WindowDesc& desc);
#endif

void InitializePlatform() {
    if (g_initialised) {
        return;
    }
    g_initialised = true;

#if defined(VALGO_PLATFORM_WINDOWS)
    // Per-monitor DPI awareness keeps the client area pixel-exact on high DPI
    // displays; the function is resolved dynamically to avoid a hard dependency
    // on a recent Windows SDK at runtime.
    using SetDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        auto set_dpi = reinterpret_cast<SetDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_dpi != nullptr) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
            set_dpi(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
        }
    }
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
        LogWarn(kCategory, "CoInitializeEx failed (HRESULT {})", static_cast<u32>(com_result));
    }
    timeBeginPeriod(1);
#endif
    LogInfo(kCategory, "platform initialised");
}

void ShutdownPlatform() {
    if (!g_initialised) {
        return;
    }
    g_initialised = false;
#if defined(VALGO_PLATFORM_WINDOWS)
    timeEndPeriod(1);
    CoUninitialize();
#endif
    LogInfo(kCategory, "platform shut down");
}

std::unique_ptr<IWindow> CreateWindow(const WindowDesc& desc) {
    InitializePlatform();
    if (desc.headless) {
        return CreateHeadlessWindow(desc);
    }
#if defined(VALGO_PLATFORM_WINDOWS)
    std::unique_ptr<IWindow> window = CreateWin32Window(desc);
    if (window == nullptr) {
        LogWarn(kCategory, "failed to create a Win32 window, continuing headless");
        WindowDesc headless = desc;
        headless.headless = true;
        return CreateHeadlessWindow(headless);
    }
    return window;
#else
    LogInfo(kCategory, "no windowing system available on this platform; using a headless window");
    WindowDesc headless = desc;
    headless.headless = true;
    return CreateHeadlessWindow(headless);
#endif
}

}  // namespace valgo::platform

// =============================================================================
// Native key code mapping (platform specific)
// =============================================================================
namespace valgo::input {

#if defined(VALGO_PLATFORM_WINDOWS)
Key KeyFromNativeCode(u32 native_code) {
    // Letters and digits map through their ASCII values.
    if (native_code >= 'A' && native_code <= 'Z') {
        return static_cast<Key>(static_cast<u16>(Key::A) + (native_code - 'A'));
    }
    if (native_code >= '0' && native_code <= '9') {
        return static_cast<Key>(static_cast<u16>(Key::Num0) + (native_code - '0'));
    }
    switch (native_code) {
        case VK_SPACE: return Key::Space;
        case VK_ESCAPE: return Key::Escape;
        case VK_RETURN: return Key::Enter;
        case VK_TAB: return Key::Tab;
        case VK_BACK: return Key::Backspace;
        case VK_LSHIFT: return Key::LeftShift;
        case VK_RSHIFT: return Key::RightShift;
        case VK_LCONTROL: return Key::LeftControl;
        case VK_RCONTROL: return Key::RightControl;
        case VK_LMENU: return Key::LeftAlt;
        case VK_RMENU: return Key::RightAlt;
        case VK_SHIFT: {
            const UINT scan = MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC);
            return (GetKeyState(VK_LSHIFT) & 0x8000) != 0 || scan == 0x2A ? Key::LeftShift : Key::RightShift;
        }
        case VK_CONTROL: {
            return (GetKeyState(VK_LCONTROL) & 0x8000) != 0 ? Key::LeftControl : Key::RightControl;
        }
        case VK_MENU: {
            return (GetKeyState(VK_LMENU) & 0x8000) != 0 ? Key::LeftAlt : Key::RightAlt;
        }
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_F1: return Key::F1;
        case VK_F2: return Key::F2;
        case VK_F3: return Key::F3;
        case VK_F4: return Key::F4;
        case VK_F5: return Key::F5;
        case VK_F6: return Key::F6;
        case VK_F7: return Key::F7;
        case VK_F8: return Key::F8;
        case VK_F9: return Key::F9;
        case VK_F10: return Key::F10;
        case VK_F11: return Key::F11;
        case VK_F12: return Key::F12;
        default: break;
    }
    return Key::Unknown;
}
#else
Key KeyFromNativeCode(u32) { return Key::Unknown; }
#endif

MouseButton MouseButtonFromNativeCode(u32 native_code) {
    switch (native_code) {
        case 0: return MouseButton::Left;
        case 1: return MouseButton::Right;
        case 2: return MouseButton::Middle;
        default: break;
    }
    return MouseButton::Left;
}

}  // namespace valgo::input
