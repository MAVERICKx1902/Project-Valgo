#include "valgo/render/renderer.h"

#include <algorithm>
#include <cmath>

#include "valgo/core/log.h"

namespace valgo::render {
namespace {

constexpr const char* kCategory = "render";

rhi::PipelineDesc MakeOpaquePipeline() {
    rhi::PipelineDesc pipeline;
    pipeline.program = rhi::ShaderProgram::LitMesh;
    pipeline.topology = rhi::PrimitiveTopology::TriangleList;
    pipeline.cull = rhi::CullMode::Back;
    pipeline.blend = rhi::BlendMode::Opaque;
    pipeline.depth_compare = rhi::CompareOp::LessEqual;
    pipeline.depth_test = true;
    pipeline.depth_write = true;
    return pipeline;
}

rhi::PipelineDesc MakeUnlitPipeline() {
    rhi::PipelineDesc pipeline = MakeOpaquePipeline();
    pipeline.program = rhi::ShaderProgram::UnlitMesh;
    return pipeline;
}

rhi::PipelineDesc MakeTransparentPipeline() {
    rhi::PipelineDesc pipeline = MakeOpaquePipeline();
    pipeline.blend = rhi::BlendMode::Alpha;
    pipeline.depth_write = false;
    return pipeline;
}

rhi::PipelineDesc MakeLinePipeline() {
    rhi::PipelineDesc pipeline;
    pipeline.program = rhi::ShaderProgram::DebugLine;
    pipeline.topology = rhi::PrimitiveTopology::LineList;
    pipeline.cull = rhi::CullMode::None;
    pipeline.blend = rhi::BlendMode::Alpha;
    pipeline.depth_compare = rhi::CompareOp::LessEqual;
    pipeline.depth_test = true;
    pipeline.depth_write = false;
    return pipeline;
}

rhi::PipelineDesc MakeSkyPipeline() {
    rhi::PipelineDesc pipeline;
    pipeline.program = rhi::ShaderProgram::Sky;
    pipeline.topology = rhi::PrimitiveTopology::TriangleList;
    pipeline.cull = rhi::CullMode::None;
    pipeline.blend = rhi::BlendMode::Opaque;
    pipeline.depth_compare = rhi::CompareOp::Always;
    pipeline.depth_test = false;
    pipeline.depth_write = false;
    return pipeline;
}

rhi::PipelineDesc MakeScreenPipeline() {
    rhi::PipelineDesc pipeline;
    pipeline.program = rhi::ShaderProgram::Screen2D;
    pipeline.topology = rhi::PrimitiveTopology::TriangleList;
    pipeline.cull = rhi::CullMode::None;
    pipeline.blend = rhi::BlendMode::Alpha;
    pipeline.depth_compare = rhi::CompareOp::Always;
    pipeline.depth_test = false;
    pipeline.depth_write = false;
    return pipeline;
}

}  // namespace

Aabb TransformBounds(const Aabb& local_bounds, const Mat4& transform) {
    const Vec3 corners[8] = {
        {local_bounds.min.x, local_bounds.min.y, local_bounds.min.z},
        {local_bounds.max.x, local_bounds.min.y, local_bounds.min.z},
        {local_bounds.min.x, local_bounds.max.y, local_bounds.min.z},
        {local_bounds.max.x, local_bounds.max.y, local_bounds.min.z},
        {local_bounds.min.x, local_bounds.min.y, local_bounds.max.z},
        {local_bounds.max.x, local_bounds.min.y, local_bounds.max.z},
        {local_bounds.min.x, local_bounds.max.y, local_bounds.max.z},
        {local_bounds.max.x, local_bounds.max.y, local_bounds.max.z},
    };

    Aabb result;
    bool first = true;
    for (const Vec3& corner : corners) {
        const Vec3 world = transform.TransformPoint(corner);
        if (first) {
            result.min = world;
            result.max = world;
            first = false;
        } else {
            result.Encapsulate(world);
        }
    }
    return result;
}

Renderer::Renderer(rhi::Device& device) : device_(device) {}

Renderer::~Renderer() = default;

void Renderer::BeginFrame() {
    lines_.clear();
    overlay_.Clear();
    // stats_ intentionally keeps the previous frame's numbers: the game draws its
    // HUD before rendering, and showing the last measured frame is both correct
    // and more useful than zeros.
}

void Renderer::DrawLine(const Vec3& from, const Vec3& to, const Vec4& color) {
    const u32 packed = rhi::PackColor(color);
    lines_.push_back({from, packed});
    lines_.push_back({to, packed});
}

void Renderer::DrawAabb(const Aabb& bounds, const Vec4& color) {
    const Vec3 min = bounds.min;
    const Vec3 max = bounds.max;
    const Vec3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z}, {min.x, max.y, min.z}, {max.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z}, {min.x, max.y, max.z}, {max.x, max.y, max.z},
    };
    static constexpr u32 kEdges[24] = {0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4,
                                       0, 4, 1, 5, 2, 6, 3, 7};
    for (u32 i = 0; i < 24; i += 2) {
        DrawLine(corners[kEdges[i]], corners[kEdges[i + 1]], color);
    }
}

void Renderer::DrawWireSphere(const Vec3& center, f32 radius, const Vec4& color, u32 segments) {
    const u32 steps = std::max(segments, 4u);
    for (u32 axis = 0; axis < 3; ++axis) {
        for (u32 i = 0; i < steps; ++i) {
            const f32 angle0 = kTwoPi * static_cast<f32>(i) / static_cast<f32>(steps);
            const f32 angle1 = kTwoPi * static_cast<f32>(i + 1) / static_cast<f32>(steps);
            Vec3 a{};
            Vec3 b{};
            const int first = static_cast<int>((axis + 1) % 3);
            const int second = static_cast<int>((axis + 2) % 3);
            a[first] = std::cos(angle0) * radius;
            a[second] = std::sin(angle0) * radius;
            b[first] = std::cos(angle1) * radius;
            b[second] = std::sin(angle1) * radius;
            DrawLine(center + a, center + b, color);
        }
    }
}

void Renderer::EnsureLineBuffer(u64 required_bytes) {
    if (line_buffer_ != nullptr && required_bytes <= line_buffer_capacity_) {
        return;
    }
    const u64 capacity = std::max<u64>(required_bytes * 2, 4096);
    rhi::BufferDesc desc;
    desc.usage = rhi::BufferUsage::Vertex;
    desc.size = capacity;
    desc.stride = sizeof(rhi::LineVertex);
    desc.debug_name = "renderer.lines";
    line_buffer_ = device_.CreateBuffer(desc);
    line_buffer_capacity_ = capacity;
}

void Renderer::EnsureOverlayBuffer(u64 required_bytes) {
    if (overlay_buffer_ != nullptr && required_bytes <= overlay_buffer_capacity_) {
        return;
    }
    const u64 capacity = std::max<u64>(required_bytes * 2, 16384);
    rhi::BufferDesc desc;
    desc.usage = rhi::BufferUsage::Vertex;
    desc.size = capacity;
    desc.stride = sizeof(rhi::ScreenVertex);
    desc.debug_name = "renderer.overlay";
    overlay_buffer_ = device_.CreateBuffer(desc);
    overlay_buffer_capacity_ = capacity;
}

void Renderer::Render(rhi::CommandList& cmd, const scene::Scene& scene, const Camera& camera,
                      u32 viewport_width, u32 viewport_height) {
    stats_ = RenderStats{};
    const f32 aspect = viewport_height > 0 ? static_cast<f32>(viewport_width) / static_cast<f32>(viewport_height)
                                          : 1.0f;

    // --- Frame constants --------------------------------------------------
    rhi::FrameConstants frame{};
    frame.view_projection = camera.ViewProjectionMatrix();
    frame.view = camera.ViewMatrix();
    frame.projection = camera.ProjectionMatrix();
    frame.inverse_view_projection = frame.view_projection.Inverse();
    frame.camera_position = Vec4{camera.position(), 1.0f};
    frame.viewport = Vec4{static_cast<f32>(viewport_width), static_cast<f32>(viewport_height),
                          1.0f / static_cast<f32>(std::max(viewport_width, 1u)),
                          1.0f / static_cast<f32>(std::max(viewport_height, 1u))};
    // Settings colours are authored in sRGB; lighting happens in linear space.
    frame.clear_color = Vec4{SrgbToLinear(settings_.clear_color), 1.0f};
    frame.ambient_sky = Vec4{SrgbToLinear(settings_.ambient_sky), 1.0f};
    frame.ambient_ground = Vec4{SrgbToLinear(settings_.ambient_ground), 1.0f};
    frame.fog_color = Vec4{SrgbToLinear(settings_.fog_color), 1.0f};
    frame.sky_zenith = Vec4{SrgbToLinear(settings_.sky_zenith), 1.0f};
    frame.sky_horizon = Vec4{SrgbToLinear(settings_.sky_horizon), 1.0f};
    frame.fog_parameters = Vec4{settings_.fog_start, settings_.fog_end, settings_.fog_height_falloff,
                                settings_.enable_fog ? 1.0f : 0.0f};

    // --- Light selection --------------------------------------------------
    Vec3 light_direction = Normalize(-settings_.default_light_direction);
    Vec3 light_color = SrgbToLinear(settings_.default_light_color);
    f32 light_intensity = settings_.default_light_intensity;
    scene.ForEach<scene::DirectionalLightComponent>(
        [&](scene::Entity, const scene::DirectionalLightComponent& light) {
            light_direction = Normalize(-light.direction);
            light_color = SrgbToLinear(light.color);
            light_intensity = light.intensity;
        });
    frame.light_direction = Vec4{light_direction, light_intensity};
    frame.light_color = Vec4{light_color, 1.0f};

    VALGO_UNUSED(aspect);

    rhi::ClearValue clear;
    clear.color = frame.clear_color;
    cmd.BeginFrame(frame, clear);

    // --- 1. Sky -----------------------------------------------------------
    if (settings_.draw_sky) {
        cmd.SetPipeline(MakeSkyPipeline());
        rhi::GeometryDraw sky_draw;
        sky_draw.constants.world = Mat4::Identity();
        sky_draw.constants.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        cmd.DrawGeometry(sky_draw);
        ++stats_.draw_calls;
    }

    // --- Gather mesh draws ------------------------------------------------
    const Frustum frustum = camera.BuildFrustum();
    const Mat4 view = frame.view;

    std::vector<DrawItem> opaque_items;
    std::vector<DrawItem> transparent_items;

    scene.ForEach<scene::MeshRendererComponent>(
        [&](scene::Entity entity, const scene::MeshRendererComponent& component) {
            if (!component.visible || component.mesh == nullptr || !component.mesh->IsValid()) {
                return;
            }
            const scene::TransformComponent* transform = scene.GetComponent<scene::TransformComponent>(entity);
            const Mat4 world = transform != nullptr ? transform->world.ToMatrix() : Mat4::Identity();
            const Aabb world_bounds = TransformBounds(component.mesh->bounds, world);

            if (settings_.enable_frustum_culling && !frustum.Intersects(world_bounds)) {
                ++stats_.meshes_culled;
                return;
            }

            DrawItem item;
            item.mesh = component.mesh;
            item.material = component.material;
            item.world = world;
            item.unlit = component.material.unlit >= 0.5f;
            const Vec3 center = world_bounds.Center();
            item.distance_squared = LengthSquared(center - camera.position());

            if (component.material.opacity >= 0.999f) {
                opaque_items.push_back(item);
            } else {
                transparent_items.push_back(item);
            }
        });

    // Batch opaque draws by mesh to reduce pipeline/state churn.
    std::sort(opaque_items.begin(), opaque_items.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.unlit != b.unlit) {
            return !a.unlit;
        }
        return a.mesh < b.mesh;
    });
    std::sort(transparent_items.begin(), transparent_items.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.distance_squared > b.distance_squared;
    });

    auto submit = [&](const DrawItem& item, const rhi::PipelineDesc& pipeline) {
        cmd.SetPipeline(pipeline);
        rhi::GeometryDraw draw;
        draw.vertex_buffer = item.mesh->vertex_buffer.get();
        draw.index_buffer = item.mesh->index_buffer.get();
        draw.index_count = item.mesh->index_count;
        draw.base_color_texture = item.material.base_color_texture;
        draw.constants.world = item.world;
        draw.constants.color = Vec4{SrgbToLinear(item.material.base_color), item.material.opacity};
        draw.constants.params = Vec4{item.material.unlit, item.material.base_color_texture != nullptr ? 1.0f : 0.0f,
                                     item.material.emissive, 1.0f};
        cmd.DrawGeometry(draw);
        ++stats_.draw_calls;
        ++stats_.meshes_drawn;
        stats_.triangles += item.mesh->index_count / 3;
    };

    // --- 2. Opaque --------------------------------------------------------
    const rhi::PipelineDesc opaque_pipeline = MakeOpaquePipeline();
    const rhi::PipelineDesc unlit_pipeline = MakeUnlitPipeline();
    for (const DrawItem& item : opaque_items) {
        submit(item, item.unlit ? unlit_pipeline : opaque_pipeline);
    }

    // --- 3. Transparent ---------------------------------------------------
    if (!transparent_items.empty()) {
        const rhi::PipelineDesc transparent_pipeline = MakeTransparentPipeline();
        for (const DrawItem& item : transparent_items) {
            submit(item, transparent_pipeline);
        }
    }

    // --- 4. Debug lines ---------------------------------------------------
    if (!lines_.empty()) {
        const u64 bytes = static_cast<u64>(lines_.size()) * sizeof(rhi::LineVertex);
        EnsureLineBuffer(bytes);
        if (line_buffer_ != nullptr && line_buffer_->Write(lines_.data(), bytes, 0)) {
            cmd.SetPipeline(MakeLinePipeline());
            cmd.DrawLines(std::span<const rhi::LineVertex>(lines_.data(), lines_.size()));
            ++stats_.draw_calls;
        }
        stats_.debug_lines = static_cast<u32>(lines_.size() / 2);
    }

    // --- 5. Overlay -------------------------------------------------------
    const std::vector<rhi::ScreenVertex>& overlay_triangles = overlay_.triangles();
    if (!overlay_triangles.empty()) {
        const u64 bytes = static_cast<u64>(overlay_triangles.size()) * sizeof(rhi::ScreenVertex);
        EnsureOverlayBuffer(bytes);
        if (overlay_buffer_ != nullptr && overlay_buffer_->Write(overlay_triangles.data(), bytes, 0)) {
            cmd.SetPipeline(MakeScreenPipeline());
            cmd.DrawScreenTriangles(
                std::span<const rhi::ScreenVertex>(overlay_triangles.data(), overlay_triangles.size()));
            ++stats_.draw_calls;
        }
        stats_.overlay_triangles = static_cast<u32>(overlay_triangles.size() / 3);
    }

    cmd.EndFrame();

    // Silence unused warnings in builds where the view matrix is only used by
    // the backends through FrameConstants.
    VALGO_UNUSED(view);
}

}  // namespace valgo::render
