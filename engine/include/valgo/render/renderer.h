// =============================================================================
// Project Valgo — forward renderer.
//
// Consumes a scene + camera and submits work through the RHI. It owns no game
// logic: culling, light selection, material interpretation, debug lines and the
// 2D overlay are all generic services that any genre can use.
//
// Frame structure
//   1. sky program (background gradient + sun glow)
//   2. opaque meshes (frustum culled, batched by mesh)
//   3. alpha-blended meshes (back to front)
//   4. debug lines (depth tested)
//   5. screen-space overlay (HUD)
// =============================================================================
#pragma once

#include <memory>
#include <vector>

#include "valgo/render/camera.h"
#include "valgo/render/mesh.h"
#include "valgo/render/overlay2d.h"
#include "valgo/rhi/rhi.h"
#include "valgo/scene/scene.h"

namespace valgo::render {

struct RenderSettings {
    Vec3 ambient_sky{0.30f, 0.36f, 0.48f};
    Vec3 ambient_ground{0.10f, 0.10f, 0.09f};
    Vec3 fog_color{0.55f, 0.62f, 0.72f};
    /// Sky gradient, authored in sRGB. Independent from the ambient light colours
    /// so the sky can stay atmospheric while the ambient term stays neutral.
    Vec3 sky_zenith{0.34f, 0.48f, 0.70f};
    Vec3 sky_horizon{0.76f, 0.82f, 0.88f};
    Vec3 clear_color{0.55f, 0.62f, 0.72f};
    Vec3 default_light_direction{-0.45f, -1.0f, -0.35f};
    Vec3 default_light_color{1.0f, 0.96f, 0.90f};
    /// Linear multiplier applied to the light colour. Colours are authored in
    /// sRGB and converted by the renderer, so this value is not a display tint.
    f32 default_light_intensity = 0.45f;

    f32 fog_start = 45.0f;
    f32 fog_end = 260.0f;
    f32 fog_height_falloff = 0.0f;
    bool enable_fog = true;
    bool enable_frustum_culling = true;
    bool draw_sky = true;
};

struct RenderStats {
    u32 draw_calls = 0;
    u32 meshes_drawn = 0;
    u32 meshes_culled = 0;
    u32 triangles = 0;
    u32 debug_lines = 0;
    u32 overlay_triangles = 0;
};

class Renderer {
public:
    explicit Renderer(rhi::Device& device);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    RenderSettings& settings() { return settings_; }
    const RenderSettings& settings() const { return settings_; }

    Overlay2D& overlay() { return overlay_; }

    /// Clears per-frame debug geometry and the overlay. Called by the
    /// application before the game's update.
    void BeginFrame();

    /// Queues a world-space debug line (cleared every frame).
    void DrawLine(const Vec3& from, const Vec3& to, const Vec4& color);
    void DrawAabb(const Aabb& bounds, const Vec4& color);
    void DrawWireSphere(const Vec3& center, f32 radius, const Vec4& color, u32 segments = 12);

    /// Renders the scene. `cmd` must already be recording a frame.
    void Render(rhi::CommandList& cmd, const scene::Scene& scene, const Camera& camera, u32 viewport_width,
                u32 viewport_height);

    const RenderStats& stats() const { return stats_; }

private:
    struct DrawItem {
        const MeshGpuData* mesh = nullptr;
        Material material;
        Mat4 world;
        f32 distance_squared = 0.0f;
        bool unlit = false;
    };

    void EnsureLineBuffer(u64 required_bytes);
    void EnsureOverlayBuffer(u64 required_bytes);

    rhi::Device& device_;
    RenderSettings settings_;
    Overlay2D overlay_;
    RenderStats stats_;

    std::vector<rhi::LineVertex> lines_;
    std::unique_ptr<rhi::Buffer> line_buffer_;
    u64 line_buffer_capacity_ = 0;

    std::unique_ptr<rhi::Buffer> overlay_buffer_;
    u64 overlay_buffer_capacity_ = 0;
};

/// Computes the world-space bounds of a local AABB under a transform.
Aabb TransformBounds(const Aabb& local_bounds, const Mat4& transform);

}  // namespace valgo::render
