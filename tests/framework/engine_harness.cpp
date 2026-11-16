#include "engine_harness.h"

#include <algorithm>

namespace valgo_test {

EngineHarness::EngineHarness(u32 width, u32 height) {
    width_ = std::max(width, 1u);
    height_ = std::max(height, 1u);

    valgo::rhi::DeviceDesc device_desc;
    device_desc.api = valgo::rhi::GraphicsApi::Software;
    device_desc.application_name = "Valgo tests";
    device_desc.enable_validation = true;
    device_ = valgo::rhi::Device::Create(device_desc);
    if (device_ == nullptr) {
        return;
    }
    device_info_ = device_->info();

    valgo::rhi::NativeWindow native_window;
    native_window.window_handle = nullptr;
    native_window.width = width_;
    native_window.height = height_;
    native_window.headless = true;

    valgo::rhi::SwapChainDesc swap_desc;
    swap_desc.width = width_;
    swap_desc.height = height_;
    swap_desc.format = valgo::rhi::Format::RGBA8_SRGB;
    swap_desc.buffer_count = 2;
    swap_chain_ = device_->CreateSwapChain(native_window, swap_desc);
    if (swap_chain_ == nullptr) {
        return;
    }

    renderer_holder_ = std::make_unique<valgo::render::Renderer>(*device_);

    assets_holder_ = std::make_unique<valgo::assets::AssetManager>(*device_);
    assets_holder_->SetAssetRoot(VALGO_PROJECT_ROOT);

    camera_.SetPerspective(70.0f, static_cast<f32>(width_) / static_cast<f32>(height_), 0.05f, 400.0f);
    camera_.SetPosition({0.0f, 1.7f, 0.0f});

    services_.window = nullptr;
    services_.device = device_.get();
    services_.swap_chain = swap_chain_.get();
    services_.renderer = renderer_holder_.get();
    services_.scene = &scene_;
    services_.physics = &physics_;
    services_.assets = assets_holder_.get();
    services_.input = &input_;
    services_.camera = &camera_;
    services_.viewport_width = swap_chain_->width();
    services_.viewport_height = swap_chain_->height();
}

EngineHarness::~EngineHarness() {
    if (device_ != nullptr) {
        device_->WaitIdle();
    }
}

void EngineHarness::StepFrame(valgo::game::IGameModule& game, u32 fixed_steps, f32 fixed_step) {
    if (!valid()) {
        return;
    }
    input_.BeginFrame();

    valgo::game::GameFrame frame;
    frame.frame_index = frame_index_;
    frame.total_seconds = simulated_seconds_;
    frame.fixed_steps = std::max(fixed_steps, 1u);
    frame.fixed_step_seconds = fixed_step;
    frame.delta_seconds = static_cast<f32>(static_cast<f64>(frame.fixed_steps) * fixed_step);

    services_.viewport_width = swap_chain_->width();
    services_.viewport_height = swap_chain_->height();
    services_.input = &input_;

    renderer_holder_->BeginFrame();
    game.OnUpdate(frame, services_);
    scene_.UpdateWorldTransforms();

    camera_.SetAspectRatio(static_cast<f32>(swap_chain_->width()) / static_cast<f32>(swap_chain_->height()));

    if (swap_chain_->BeginFrame()) {
        renderer_holder_->Render(*swap_chain_->command_list(), scene_, camera_,
                                 swap_chain_->width(), swap_chain_->height());
        swap_chain_->Present();
    }

    ++frame_index_;
    simulated_seconds_ += static_cast<f64>(fixed_steps) * static_cast<f64>(fixed_step);
}

bool EngineHarness::CaptureImage(valgo::Image& out_image) const {
    if (swap_chain_ == nullptr) {
        return false;
    }
    std::vector<u8> pixels;
    if (!swap_chain_->ReadPixels(pixels)) {
        return false;
    }
    out_image.width = swap_chain_->width();
    out_image.height = swap_chain_->height();
    out_image.pixels = std::move(pixels);
    return !out_image.empty();
}

}  // namespace valgo_test
