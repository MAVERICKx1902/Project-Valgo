// =============================================================================
// Project Valgo — headless engine harness for tests.
//
// Creates a real engine stack (software RHI device, swap chain, renderer, scene,
// physics world, asset manager, camera, input) with no window. Integration tests
// drive games through the same IGameModule interface the real application uses,
// so what is tested is what ships.
// =============================================================================
#pragma once

#include <functional>
#include <memory>

#include "valgo/assets/asset_manager.h"
#include "valgo/core/image.h"
#include "valgo/game/game_module.h"
#include "valgo/input/input.h"
#include "valgo/physics/physics_world.h"
#include "valgo/platform/window.h"
#include "valgo/render/camera.h"
#include "valgo/render/renderer.h"
#include "valgo/rhi/rhi.h"
#include "valgo/scene/scene.h"

namespace valgo_test {

using valgo::f32;
using valgo::f64;
using valgo::u32;
using valgo::u8;
using valgo::u64;

/// A game module that does nothing: used to drive engine-only frames.
class NullGame final : public valgo::game::IGameModule {
public:
    std::string_view name() const override { return "null"; }
    void OnStartup(valgo::game::GameServices&) override {}
    void OnUpdate(const valgo::game::GameFrame&, valgo::game::GameServices&) override { ++frames; }
    void OnShutdown(valgo::game::GameServices&) override {}
    u32 frames = 0;
};

/// A game module that forwards its update to a callback: lets a test queue
/// debug geometry, overlay draws or gameplay input at exactly the point in the
/// frame where real game code would do it.
class LambdaGame final : public valgo::game::IGameModule {
public:
    using Callback = std::function<void(valgo::game::GameServices&)>;

    explicit LambdaGame(Callback callback) : callback_(std::move(callback)) {}

    std::string_view name() const override { return "lambda"; }
    void OnStartup(valgo::game::GameServices&) override {}
    void OnUpdate(const valgo::game::GameFrame&, valgo::game::GameServices& services) override {
        if (callback_) {
            callback_(services);
        }
    }
    void OnShutdown(valgo::game::GameServices&) override {}

private:
    Callback callback_;
};

class EngineHarness {
public:
    EngineHarness(u32 width = 320, u32 height = 180);
    ~EngineHarness();

    EngineHarness(const EngineHarness&) = delete;
    EngineHarness& operator=(const EngineHarness&) = delete;

    bool valid() const { return device_ != nullptr && swap_chain_ != nullptr; }
    const valgo::rhi::DeviceInfo& device_info() const { return device_info_; }

    valgo::game::GameServices& services() { return services_; }
    valgo::scene::Scene& scene() { return scene_; }
    valgo::physics::PhysicsWorld& physics() { return physics_; }
    valgo::render::Renderer& renderer() { return *renderer_holder_; }
    valgo::assets::AssetManager& assets() { return *assets_holder_; }
    valgo::render::Camera& camera() { return camera_; }
    valgo::input::InputState& input() { return input_; }

    /// Runs one complete frame: input snapshot, game update, world transform
    /// update, render and present.
    void StepFrame(valgo::game::IGameModule& game, u32 fixed_steps = 1, f32 fixed_step = 1.0f / 60.0f);

    /// Copies the last presented frame (software backend only).
    bool CaptureImage(valgo::Image& out_image) const;

    u64 frame_index() const { return frame_index_; }
    f64 simulated_seconds() const { return simulated_seconds_; }

private:
    std::unique_ptr<valgo::rhi::Device> device_;
    std::unique_ptr<valgo::rhi::SwapChain> swap_chain_;
    valgo::rhi::DeviceInfo device_info_{};
    std::unique_ptr<valgo::render::Renderer> renderer_holder_;
    valgo::scene::Scene scene_;
    valgo::physics::PhysicsWorld physics_;
    std::unique_ptr<valgo::assets::AssetManager> assets_holder_;
    valgo::render::Camera camera_;
    valgo::input::InputState input_;
    valgo::game::GameServices services_{};
    u32 width_ = 320;
    u32 height_ = 180;
    u64 frame_index_ = 0;
    f64 simulated_seconds_ = 0.0;
};

}  // namespace valgo_test
