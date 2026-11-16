// =============================================================================
// Project Valgo — Win32 window (Windows host).
//
// Owns the HWND the D3D12 swap chain presents into, translates Win32 messages
// into the engine's device-independent InputState, and provides the GDI blit
// path used when the software rasteriser drives a real window (GPU-less hosts
// and CI machines with a desktop session).
//
// Relative mouse mode uses raw input deltas so aiming does not depend on the
// cursor hitting the window edge and is not affected by mouse acceleration.
// =============================================================================
#if defined(VALGO_PLATFORM_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>

#include <string>
#include <vector>

#include "valgo/core/log.h"
#include "valgo/platform/window.h"

namespace valgo::platform {
namespace {

constexpr const char* kCategory = "platform.win32";
constexpr const wchar_t* kWindowClassName = L"ValgoWindowClass";

input::Key KeyFromVirtualKey(WPARAM virtual_key) {
    using input::Key;
    if (virtual_key >= 'A' && virtual_key <= 'Z') {
        return static_cast<Key>(static_cast<u16>(Key::A) + static_cast<u16>(virtual_key - 'A'));
    }
    if (virtual_key >= '0' && virtual_key <= '9') {
        return static_cast<Key>(static_cast<u16>(Key::Num0) + static_cast<u16>(virtual_key - '0'));
    }
    if (virtual_key >= VK_F1 && virtual_key <= VK_F12) {
        return static_cast<Key>(static_cast<u16>(Key::F1) + static_cast<u16>(virtual_key - VK_F1));
    }
    switch (virtual_key) {
        case VK_SPACE: return Key::Space;
        case VK_ESCAPE: return Key::Escape;
        case VK_RETURN: return Key::Enter;
        case VK_TAB: return Key::Tab;
        case VK_BACK: return Key::Backspace;
        case VK_SHIFT: return Key::LeftShift;
        case VK_LSHIFT: return Key::LeftShift;
        case VK_RSHIFT: return Key::RightShift;
        case VK_CONTROL: return Key::LeftControl;
        case VK_LCONTROL: return Key::LeftControl;
        case VK_RCONTROL: return Key::RightControl;
        case VK_MENU: return Key::LeftAlt;
        case VK_LMENU: return Key::LeftAlt;
        case VK_RMENU: return Key::RightAlt;
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        default: break;
    }
    return Key::Unknown;
}

class Win32Window final : public IWindow {
public:
    explicit Win32Window(const WindowDesc& desc) : width_(desc.width), height_(desc.height) {
        const HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        window_class.lpfnWndProc = &Win32Window::WindowProc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kWindowClassName;
        if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            LogError(kCategory, "RegisterClassExW failed ({})", static_cast<u32>(GetLastError()));
            return;
        }

        RECT rect{0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height)};
        const DWORD style = desc.borderless ? WS_POPUP | WS_VISIBLE
                                            : WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        if (!AdjustWindowRectEx(&rect, style, FALSE, 0)) {
            LogWarn(kCategory, "AdjustWindowRectEx failed");
        }

        const std::wstring wide_title(desc.title.begin(), desc.title.end());
        hwnd_ = CreateWindowExW(0, kWindowClassName, wide_title.c_str(), style, CW_USEDEFAULT,
                                CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                nullptr, nullptr, instance, this);
        if (hwnd_ == nullptr) {
            LogError(kCategory, "CreateWindowExW failed ({})", static_cast<u32>(GetLastError()));
            return;
        }

        // Raw input keeps mouse deltas independent of the cursor position.
        RAWINPUTDEVICE raw_mouse{};
        raw_mouse.usUsagePage = 0x01;  // generic desktop
        raw_mouse.usUsage = 0x02;      // mouse
        raw_mouse.dwFlags = 0;
        raw_mouse.hwndTarget = hwnd_;
        if (!RegisterRawInputDevices(&raw_mouse, 1, sizeof(raw_mouse))) {
            LogWarn(kCategory, "RegisterRawInputDevices failed; using cursor deltas");
        }

        client_device_context_ = GetDC(hwnd_);
        valid_ = true;
        UpdateClientRect();
        LogInfo(kCategory, "window created: \"{}\" {}x{}", desc.title, width_, height_);
    }

    ~Win32Window() override {
        if (client_device_context_ != nullptr && hwnd_ != nullptr) {
            ReleaseDC(hwnd_, client_device_context_);
        }
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
    }

    bool IsValid() const override { return valid_; }
    bool IsHeadless() const override { return false; }

    u32 width() const override { return width_; }
    u32 height() const override { return height_; }

    void* native_handle() const override { return hwnd_; }

    void SetTitle(std::string_view title) override {
        if (hwnd_ == nullptr) {
            return;
        }
        const std::wstring wide(title.begin(), title.end());
        SetWindowTextW(hwnd_, wide.c_str());
    }

    void SetCursorVisible(bool visible) override {
        cursor_visible_ = visible;
        ShowCursor(visible ? TRUE : FALSE);
    }

    void SetRelativeMouseMode(bool enabled) override {
        if (enabled == relative_mouse_) {
            return;
        }
        relative_mouse_ = enabled;
        if (enabled) {
            ShowCursor(FALSE);
            SetCapture(hwnd_);
            RECT rect{};
            GetClientRect(hwnd_, &rect);
            POINT top_left{rect.left, rect.top};
            POINT bottom_right{rect.right, rect.bottom};
            ClientToScreen(hwnd_, &top_left);
            ClientToScreen(hwnd_, &bottom_right);
            RECT clip{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
            ClipCursor(&clip);
        } else {
            ClipCursor(nullptr);
            ReleaseCapture();
            ShowCursor(TRUE);
        }
    }

    void PollEvents() override {
        input_.BeginFrame();

        if (relative_mouse_) {
            POINT center{};
            RECT client{};
            if (GetClientRect(hwnd_, &client)) {
                center.x = (client.right - client.left) / 2;
                center.y = (client.bottom - client.top) / 2;
                ClientToScreen(hwnd_, &center);
                SetCursorPos(center.x, center.y);
            }
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                close_requested_ = true;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        UpdateClientRect();
        if (input_.quit_requested()) {
            close_requested_ = true;
        }
    }

    input::InputState& input() override { return input_; }

    bool ShouldClose() const override { return close_requested_; }

    void RequestClose() override {
        close_requested_ = true;
        if (hwnd_ != nullptr) {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
    }

    void BlitFramebuffer(const u8* rgba_pixels, u32 width, u32 height) override {
        if (client_device_context_ == nullptr || rgba_pixels == nullptr || width == 0 || height == 0) {
            return;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height);  // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        // The software backend produces RGBA; GDI expects BGRA. Convert into a
        // reusable staging buffer so the blit costs one pass, not one per pixel
        // allocation.
        staging_.resize(static_cast<size_t>(width) * height);
        for (size_t i = 0; i < staging_.size(); ++i) {
            const u8* source = rgba_pixels + i * 4;
            u32& target = staging_[i];
            target = static_cast<u32>(source[0]) | (static_cast<u32>(source[1]) << 8) |
                     (static_cast<u32>(source[2]) << 16) | (static_cast<u32>(source[3]) << 24);
        }
        const int rows = StretchDIBits(client_device_context_, 0, 0, static_cast<int>(width_),
                                       static_cast<int>(height_), 0, 0, static_cast<int>(width),
                                       static_cast<int>(height), staging_.data(), &info, DIB_RGB_COLORS,
                                       SRCCOPY);
        if (rows == GDI_ERROR) {
            LogWarn(kCategory, "StretchDIBits failed");
        }
    }

    void SetAssetRoot(std::string_view root) override { asset_root_.assign(root.begin(), root.end()); }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        Win32Window* window = nullptr;
        if (message == WM_NCCREATE) {
            auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
            window = static_cast<Win32Window*>(create_struct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            if (window != nullptr) {
                window->hwnd_ = hwnd;
            }
        } else {
            window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (window == nullptr) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
        return window->HandleMessage(message, w_param, l_param);
    }

    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
        switch (message) {
            case WM_CLOSE:
            case WM_DESTROY:
                close_requested_ = true;
                return 0;

            case WM_SIZE:
                if (w_param != SIZE_MINIMIZED) {
                    width_ = static_cast<u32>(LOWORD(l_param));
                    height_ = static_cast<u32>(HIWORD(l_param));
                }
                return 0;

            case WM_SETFOCUS:
                focused_ = true;
                return 0;

            case WM_KILLFOCUS:
                focused_ = false;
                input_.ClearAll();
                if (relative_mouse_) {
                    SetRelativeMouseMode(false);
                }
                return 0;

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                const input::Key key = KeyFromVirtualKey(w_param);
                if (key != input::Key::Unknown) {
                    input_.SetKey(key, true);
                }
                if (w_param == VK_ESCAPE && !relative_mouse_) {
                    close_requested_ = true;
                }
                return 0;
            }

            case WM_KEYUP:
            case WM_SYSKEYUP: {
                const input::Key key = KeyFromVirtualKey(w_param);
                if (key != input::Key::Unknown) {
                    input_.SetKey(key, false);
                }
                return 0;
            }

            case WM_LBUTTONDOWN: input_.SetMouseButton(input::MouseButton::Left, true); return 0;
            case WM_LBUTTONUP: input_.SetMouseButton(input::MouseButton::Left, false); return 0;
            case WM_RBUTTONDOWN: input_.SetMouseButton(input::MouseButton::Right, true); return 0;
            case WM_RBUTTONUP: input_.SetMouseButton(input::MouseButton::Right, false); return 0;
            case WM_MBUTTONDOWN: input_.SetMouseButton(input::MouseButton::Middle, true); return 0;
            case WM_MBUTTONUP: input_.SetMouseButton(input::MouseButton::Middle, false); return 0;

            case WM_MOUSEWHEEL:
                input_.AddWheelDelta(static_cast<f32>(GET_WHEEL_DELTA_WPARAM(w_param)) / WHEEL_DELTA);
                return 0;

            case WM_MOUSEMOVE:
                if (!relative_mouse_) {
                    input_.SetMousePosition(static_cast<f32>(GET_X_LPARAM(l_param)),
                                            static_cast<f32>(GET_Y_LPARAM(l_param)));
                }
                return 0;

            case WM_INPUT: {
                if (!relative_mouse_) {
                    return 0;
                }
                UINT size = 0;
                GetRawInputData(reinterpret_cast<HRAWINPUT>(l_param), RID_INPUT, nullptr, &size,
                                sizeof(RAWINPUTHEADER));
                if (size == 0 || size > sizeof(raw_input_buffer_)) {
                    return 0;
                }
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(l_param), RID_INPUT, raw_input_buffer_,
                                    &size, sizeof(RAWINPUTHEADER)) != size) {
                    return 0;
                }
                const auto* raw = reinterpret_cast<const RAWINPUT*>(raw_input_buffer_);
                if (raw->header.dwType == RIM_TYPEMOUSE &&
                    (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                    input_.AddMouseDelta(static_cast<f32>(raw->data.mouse.lLastX),
                                         static_cast<f32>(raw->data.mouse.lLastY));
                }
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;  // we paint every frame

            default:
                break;
        }
        return DefWindowProcW(hwnd_, message, w_param, l_param);
    }

    void UpdateClientRect() {
        if (hwnd_ == nullptr) {
            return;
        }
        RECT client{};
        if (GetClientRect(hwnd_, &client)) {
            const u32 width = static_cast<u32>(client.right - client.left);
            const u32 height = static_cast<u32>(client.bottom - client.top);
            if (width > 0 && height > 0) {
                width_ = width;
                height_ = height;
            }
        }
    }

    HWND hwnd_ = nullptr;
    HDC client_device_context_ = nullptr;
    u32 width_ = 1280;
    u32 height_ = 720;
    bool valid_ = false;
    bool close_requested_ = false;
    bool relative_mouse_ = false;
    bool cursor_visible_ = true;
    bool focused_ = true;
    std::string asset_root_;
    std::vector<u32> staging_;
    unsigned char raw_input_buffer_[sizeof(RAWINPUT) + 64]{};
    input::InputState input_;
};

}  // namespace

std::unique_ptr<IWindow> CreateWin32Window(const WindowDesc& desc) {
    return std::make_unique<Win32Window>(desc);
}

}  // namespace valgo::platform

#endif  // VALGO_PLATFORM_WINDOWS
