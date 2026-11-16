// =============================================================================
// Headless window: no OS window, deterministic input, used by tests, CI and
// offline screenshot capture. The renderer still produces real frames because
// the software RHI backend rasterises into memory.
// =============================================================================
#include <string>

#include "valgo/platform/window.h"

namespace valgo::platform {
namespace {

class HeadlessWindow final : public IWindow {
public:
    explicit HeadlessWindow(const WindowDesc& desc) : width_(desc.width), height_(desc.height) {}

    bool IsValid() const override { return true; }
    bool IsHeadless() const override { return true; }

    u32 width() const override { return width_; }
    u32 height() const override { return height_; }

    void* native_handle() const override { return nullptr; }

    void SetTitle(std::string_view) override {}
    void SetCursorVisible(bool) override {}
    void SetRelativeMouseMode(bool enabled) override { relative_mouse_ = enabled; }

    void PollEvents() override {
        // Headless runs are frame-driven: input is injected by the game or by
        // the scripted input driver, so there are no OS events to consume.
        input_.BeginFrame();
    }

    input::InputState& input() override { return input_; }

    bool ShouldClose() const override { return close_requested_ || input_.quit_requested(); }
    void RequestClose() override { close_requested_ = true; }

    void BlitFramebuffer(const u8*, u32, u32) override {}

    void SetAssetRoot(std::string_view) override {}

private:
    u32 width_ = 1280;
    u32 height_ = 720;
    bool close_requested_ = false;
    bool relative_mouse_ = false;
    input::InputState input_;
};

}  // namespace

std::unique_ptr<IWindow> CreateHeadlessWindow(const WindowDesc& desc) {
    return std::make_unique<HeadlessWindow>(desc);
}

}  // namespace valgo::platform
