// =============================================================================
// Project Valgo — engine input.
//
// InputState is a plain data snapshot of one frame. It is fed either by the
// platform window (real devices) or by a scripted source (automated tests,
// deterministic demo capture), so gameplay code never talks to an OS API.
// =============================================================================
#pragma once

#include <array>
#include <string_view>

#include "valgo/core/types.h"
#include "valgo/math/math.h"

namespace valgo::input {

enum class Key : u16 {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space,
    Escape,
    Enter,
    Tab,
    Backspace,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
    Left,
    Right,
    Up,
    Down,
    PageUp,
    PageDown,
    Home,
    End,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count,
};

inline constexpr usize kKeyCount = static_cast<usize>(Key::Count);

std::string_view KeyName(Key key);

enum class MouseButton : u8 { Left = 0, Right = 1, Middle = 2, Count = 3 };

inline constexpr usize kMouseButtonCount = static_cast<usize>(MouseButton::Count);

/// Per-frame input snapshot. Transitions (pressed/released) last exactly one
/// frame and are cleared by BeginFrame().
class InputState {
public:
    void BeginFrame();

    void SetKey(Key key, bool down);
    void SetMouseButton(MouseButton button, bool down);
    void AddMouseDelta(f32 delta_x, f32 delta_y);
    void SetMousePosition(f32 x, f32 y);
    void AddWheelDelta(f32 delta);

    bool IsKeyDown(Key key) const;
    bool WasKeyPressed(Key key) const;
    bool WasKeyReleased(Key key) const;

    bool IsMouseButtonDown(MouseButton button) const;
    bool WasMouseButtonPressed(MouseButton button) const;
    bool WasMouseButtonReleased(MouseButton button) const;

    f32 mouse_x() const { return mouse_x_; }
    f32 mouse_y() const { return mouse_y_; }
    f32 mouse_delta_x() const { return mouse_delta_x_; }
    f32 mouse_delta_y() const { return mouse_delta_y_; }
    f32 wheel_delta() const { return wheel_delta_; }

    void RequestQuit() { quit_requested_ = true; }
    bool quit_requested() const { return quit_requested_; }
    void ClearQuitRequest() { quit_requested_ = false; }
    void ClearAll();

private:
    std::array<u8, kKeyCount> down_{};
    std::array<u8, kKeyCount> pressed_{};
    std::array<u8, kKeyCount> released_{};
    std::array<u8, kMouseButtonCount> mouse_down_{};
    std::array<u8, kMouseButtonCount> mouse_pressed_{};
    std::array<u8, kMouseButtonCount> mouse_released_{};
    f32 mouse_x_ = 0.0f;
    f32 mouse_y_ = 0.0f;
    f32 mouse_delta_x_ = 0.0f;
    f32 mouse_delta_y_ = 0.0f;
    f32 wheel_delta_ = 0.0f;
    bool quit_requested_ = false;
};

/// Maps a platform-specific virtual key code to the engine key enum.
Key KeyFromNativeCode(u32 native_code);

/// Maps a platform-specific mouse button index to the engine enum.
MouseButton MouseButtonFromNativeCode(u32 native_code);

}  // namespace valgo::input
