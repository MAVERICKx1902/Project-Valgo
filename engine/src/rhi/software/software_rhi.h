// =============================================================================
// Project Valgo — software RHI backend.
//
// A real CPU rasteriser implementing the engine shader programs. It is not a
// mock: the D3D12 and software backends are expected to produce comparable
// frames for the same scene (see tests/integration/test_backend_parity.cpp).
//
// Performance target: correctness and portability first. The rasteriser is
// single-threaded and uses no SIMD intrinsics; that keeps it easy to verify and
// makes it a reliable reference implementation.
// =============================================================================
#pragma once

#include <memory>
#include <vector>

#include "valgo/rhi/rhi.h"

namespace valgo::rhi::software {

/// RGBA8 colour + 32-bit float depth buffer.
struct Framebuffer {
    u32 width = 0;
    u32 height = 0;
    bool srgb = false;
    std::vector<u8> color;   ///< width * height * 4 (RGBA8, display encoded)
    std::vector<f32> depth;  ///< width * height

    void Resize(u32 new_width, u32 new_height);
    void Clear(const Vec4& color_value, f32 depth_value);
};

class SoftwareDevice;

class SoftwareBuffer final : public Buffer {
public:
    SoftwareBuffer(BufferUsage usage, u64 size, u32 stride);

    BufferUsage usage() const override { return usage_; }
    u64 size() const override { return size_; }
    u32 stride() const override { return stride_; }

    bool Write(const void* data, u64 size, u64 offset) override;

    const u8* data() const { return data_.data(); }

private:
    BufferUsage usage_;
    u64 size_ = 0;
    u32 stride_ = 0;
    std::vector<u8> data_;
};

class SoftwareTexture final : public Texture {
public:
    /// Nearest-neighbour sample in normalised coordinates. Bytes are stored as
    /// authored (sRGB); callers decode to linear before lighting.
    Vec4 SampleNearest(f32 u, f32 v) const;

    SoftwareTexture(const TextureDesc& desc);

    u32 width() const override { return width_; }
    u32 height() const override { return height_; }
    Format format() const override { return format_; }

    const std::vector<u8>& pixels() const { return pixels_; }

private:
    u32 width_ = 0;
    u32 height_ = 0;
    Format format_ = Format::RGBA8_UNorm;
    std::vector<u8> pixels_;
};

class SoftwareSwapChain;

class SoftwareCommandList final : public CommandList {
public:
    SoftwareCommandList(SoftwareDevice* device, SoftwareSwapChain* swap_chain);

    void BeginFrame(const FrameConstants& constants, const ClearValue& clear) override;
    void SetPipeline(const PipelineDesc& pipeline) override;
    void DrawGeometry(const GeometryDraw& draw) override;
    void DrawLines(std::span<const LineVertex> vertices) override;
    void DrawScreenTriangles(std::span<const ScreenVertex> vertices) override;
    void EndFrame() override;

    void ResetStats();

    /// Statistics for tests and the debug overlay.
    u32 draw_calls() const { return draw_calls_; }
    u32 triangles_submitted() const { return triangles_submitted_; }
    u32 triangles_rasterized() const { return triangles_rasterized_; }
    u32 pixels_shaded() const { return pixels_shaded_; }

private:
    struct ClipVertex {
        Vec3 view_position;
        Vec3 normal;
        Vec2 uv;
    };

    void DrawTriangleList(const GeometryDraw& draw);
    void ShadeTriangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c,
                       const DrawConstants& constants, bool unlit);
    void RasterizeScreenTriangle(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c);
    void FillSky();

    SoftwareDevice* device_ = nullptr;
    SoftwareSwapChain* swap_chain_ = nullptr;
    Framebuffer* target_ = nullptr;

    FrameConstants frame_{};
    PipelineDesc pipeline_{};
    DrawConstants draw_constants_{};
    const SoftwareTexture* current_texture_ = nullptr;
    f32 near_plane_ = 0.05f;

    u32 draw_calls_ = 0;
    u32 triangles_submitted_ = 0;
    u32 triangles_rasterized_ = 0;
    u32 pixels_shaded_ = 0;
};

class SoftwareSwapChain final : public SwapChain {
public:
    SoftwareSwapChain(SoftwareDevice* device, const NativeWindow& window, const SwapChainDesc& desc);

    u32 width() const override { return framebuffer_.width; }
    u32 height() const override { return framebuffer_.height; }
    Format format() const override { return format_; }

    bool BeginFrame() override;
    void Present() override;
    bool Resize(u32 width, u32 height) override;
    CommandList* command_list() override { return command_list_.get(); }

    bool ReadPixels(std::vector<u8>& out_rgba) override;

    Framebuffer& framebuffer() { return framebuffer_; }
    const Framebuffer& framebuffer() const { return framebuffer_; }

private:
    SoftwareDevice* device_ = nullptr;
    Framebuffer framebuffer_;
    Format format_ = Format::RGBA8_UNorm;
    std::unique_ptr<SoftwareCommandList> command_list_;
};

class SoftwareDevice final : public Device {
public:
    explicit SoftwareDevice(const DeviceDesc& desc);

    const DeviceInfo& info() const override { return info_; }

    std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
    std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) override;
    std::unique_ptr<SwapChain> CreateSwapChain(const NativeWindow& window,
                                               const SwapChainDesc& desc) override;

    void WaitIdle() override {}

private:
    DeviceInfo info_;
};

}  // namespace valgo::rhi::software
