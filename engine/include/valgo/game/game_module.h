// =============================================================================
// Project Valgo — the engine ↔ game boundary.
//
// A game is a module that receives references to engine services and returns
// nothing. All gameplay lives behind this interface; the engine never knows what
// genre it is hosting. This is what makes the second validation game possible
// without touching engine code.
// =============================================================================
#pragma once

#include <string_view>

#include "valgo/assets/asset_manager.h"
#include "valgo/input/input.h"
#include "valgo/physics/physics_world.h"
#include "valgo/platform/window.h"
#include "valgo/render/camera.h"
#include "valgo/render/renderer.h"
#include "valgo/rhi/rhi.h"
#include "valgo/scene/scene.h"

namespace valgo::game {

/// Engine services handed to the game every frame. The game may use them but
/// must not destroy or re-create them.
struct GameServices {
    platform::IWindow* window = nullptr;
    rhi::Device* device = nullptr;
    rhi::SwapChain* swap_chain = nullptr;
    render::Renderer* renderer = nullptr;
    scene::Scene* scene = nullptr;
    physics::PhysicsWorld* physics = nullptr;
    assets::AssetManager* assets = nullptr;
    input::InputState* input = nullptr;
    render::Camera* camera = nullptr;

    u32 viewport_width = 1280;
    u32 viewport_height = 720;
};

/// Timing for one rendered frame.
struct GameFrame {
    u64 frame_index = 0;
    f64 total_seconds = 0.0;
    f32 delta_seconds = 0.0f;
    u32 fixed_steps = 1;          ///< Number of fixed simulation steps this frame.
    f32 fixed_step_seconds = 1.0f / 60.0f;
};

class IGameModule {
public:
    virtual ~IGameModule() = default;

    /// Human readable game name, used in logs and window titles.
    virtual std::string_view name() const = 0;

    /// Called once after all engine services exist. Build the initial scene,
    /// assets and gameplay state here.
    virtual void OnStartup(GameServices& services) = 0;

    /// Called once per rendered frame, before rendering. Advance simulation by
    /// `frame.fixed_steps` fixed steps of `frame.fixed_step_seconds` each.
    virtual void OnUpdate(const GameFrame& frame, GameServices& services) = 0;

    /// Called once per frame after rendering (profiling hooks, capture labels).
    virtual void OnFrameEnd(const GameFrame& frame, GameServices& services) {
        (void)frame;
        (void)services;
    }

    /// Release game resources. Engine services are still valid here.
    virtual void OnShutdown(GameServices& services) = 0;
};

}  // namespace valgo::game
