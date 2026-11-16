#include "valgo/input/input.h"

namespace valgo::input {
namespace {

usize Index(Key key) { return static_cast<usize>(key); }
usize Index(MouseButton button) { return static_cast<usize>(button); }

}  // namespace

std::string_view KeyName(Key key) {
    switch (key) {
        case Key::A: return "A";
        case Key::B: return "B";
        case Key::C: return "C";
        case Key::D: return "D";
        case Key::E: return "E";
        case Key::F: return "F";
        case Key::G: return "G";
        case Key::H: return "H";
        case Key::I: return "I";
        case Key::J: return "J";
        case Key::K: return "K";
        case Key::L: return "L";
        case Key::M: return "M";
        case Key::N: return "N";
        case Key::O: return "O";
        case Key::P: return "P";
        case Key::Q: return "Q";
        case Key::R: return "R";
        case Key::S: return "S";
        case Key::T: return "T";
        case Key::U: return "U";
        case Key::V: return "V";
        case Key::W: return "W";
        case Key::X: return "X";
        case Key::Y: return "Y";
        case Key::Z: return "Z";
        case Key::Space: return "Space";
        case Key::Escape: return "Escape";
        case Key::Enter: return "Enter";
        case Key::Tab: return "Tab";
        case Key::LeftShift: return "LeftShift";
        case Key::LeftControl: return "LeftControl";
        case Key::Left: return "Left";
        case Key::Right: return "Right";
        case Key::Up: return "Up";
        case Key::Down: return "Down";
        default: break;
    }
    return "Unknown";
}

void InputState::BeginFrame() {
    pressed_.fill(0);
    released_.fill(0);
    mouse_pressed_.fill(0);
    mouse_released_.fill(0);
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
    wheel_delta_ = 0.0f;
}

void InputState::SetKey(Key key, bool down) {
    const usize index = Index(key);
    if (key == Key::Unknown || index >= kKeyCount) {
        return;
    }
    const bool was_down = down_[index] != 0;
    if (down && !was_down) {
        pressed_[index] = 1;
    } else if (!down && was_down) {
        released_[index] = 1;
    }
    down_[index] = down ? 1 : 0;
}

void InputState::SetMouseButton(MouseButton button, bool down) {
    const usize index = Index(button);
    if (index >= kMouseButtonCount) {
        return;
    }
    const bool was_down = mouse_down_[index] != 0;
    if (down && !was_down) {
        mouse_pressed_[index] = 1;
    } else if (!down && was_down) {
        mouse_released_[index] = 1;
    }
    mouse_down_[index] = down ? 1 : 0;
}

void InputState::AddMouseDelta(f32 delta_x, f32 delta_y) {
    mouse_delta_x_ += delta_x;
    mouse_delta_y_ += delta_y;
}

void InputState::SetMousePosition(f32 x, f32 y) {
    mouse_x_ = x;
    mouse_y_ = y;
}

void InputState::AddWheelDelta(f32 delta) { wheel_delta_ += delta; }

bool InputState::IsKeyDown(Key key) const {
    const usize index = Index(key);
    return index < kKeyCount && down_[index] != 0;
}

bool InputState::WasKeyPressed(Key key) const {
    const usize index = Index(key);
    return index < kKeyCount && pressed_[index] != 0;
}

bool InputState::WasKeyReleased(Key key) const {
    const usize index = Index(key);
    return index < kKeyCount && released_[index] != 0;
}

bool InputState::IsMouseButtonDown(MouseButton button) const {
    const usize index = Index(button);
    return index < kMouseButtonCount && mouse_down_[index] != 0;
}

bool InputState::WasMouseButtonPressed(MouseButton button) const {
    const usize index = Index(button);
    return index < kMouseButtonCount && mouse_pressed_[index] != 0;
}

bool InputState::WasMouseButtonReleased(MouseButton button) const {
    const usize index = Index(button);
    return index < kMouseButtonCount && mouse_released_[index] != 0;
}

void InputState::ClearAll() {
    down_.fill(0);
    pressed_.fill(0);
    released_.fill(0);
    mouse_down_.fill(0);
    mouse_pressed_.fill(0);
    mouse_released_.fill(0);
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
    wheel_delta_ = 0.0f;
    quit_requested_ = false;
}

}  // namespace valgo::input
