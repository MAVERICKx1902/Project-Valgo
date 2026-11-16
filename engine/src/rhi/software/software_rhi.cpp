#include "software_rhi.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "valgo/core/log.h"

namespace valgo::rhi::software {
namespace {

constexpr const char* kCategory = "rhi.software";

inline Vec3 ToVec3(const Vec4& v) { return {v.x, v.y, v.z}; }

inline u8 ToByte(f32 value) {
    const f32 scaled = Saturate(value) * 255.0f + 0.5f;
    return static_cast<u8>(scaled);
}

/// Converts a linear shading result into the display value stored in the
/// framebuffer. sRGB targets are encoded exactly like the D3D12 backend does in
/// hardware; linear targets (HDR experiments, data passes) are stored as-is.
inline u8 ToDisplayByte(f32 linear, bool srgb) {
    return ToByte(srgb ? LinearToSrgb(linear) : linear);
}

inline f32 FromDisplayByte(u8 value, bool srgb) {
    const f32 normalised = static_cast<f32>(value) / 255.0f;
    return srgb ? SrgbToLinear(normalised) : normalised;
}

inline bool IsSrgbTarget(Format format) {
    return format == Format::RGBA8_SRGB || format == Format::BGRA8_SRGB;
}

/// Stores a linear colour. Alpha blending happens in linear space, so stored
/// sRGB bytes are decoded first — same result as hardware blending on an sRGB
/// render target with the sRGB blend bits set.
inline void StorePixel(std::vector<u8>& color, u32 width, u32 x, u32 y, const Vec3& rgb, f32 alpha,
                       bool blend, bool srgb) {
    const usize offset = (static_cast<usize>(y) * width + x) * 4u;
    if (!blend || alpha >= 1.0f) {
        color[offset + 0] = ToDisplayByte(rgb.x, srgb);
        color[offset + 1] = ToDisplayByte(rgb.y, srgb);
        color[offset + 2] = ToDisplayByte(rgb.z, srgb);
        color[offset + 3] = ToByte(alpha);
        return;
    }
    if (alpha <= 0.0f) {
        return;
    }
    const f32 inv_alpha = 1.0f - alpha;
    const f32 existing_r = FromDisplayByte(color[offset + 0], srgb);
    const f32 existing_g = FromDisplayByte(color[offset + 1], srgb);
    const f32 existing_b = FromDisplayByte(color[offset + 2], srgb);
    const f32 existing_a = static_cast<f32>(color[offset + 3]) / 255.0f;
    color[offset + 0] = ToDisplayByte(rgb.x * alpha + existing_r * inv_alpha, srgb);
    color[offset + 1] = ToDisplayByte(rgb.y * alpha + existing_g * inv_alpha, srgb);
    color[offset + 2] = ToDisplayByte(rgb.z * alpha + existing_b * inv_alpha, srgb);
    color[offset + 3] = ToByte(alpha + existing_a * inv_alpha);
}

/// Packed vertex colours (debug lines, 2D overlay) are authored in sRGB.
inline Vec3 DisplayColorToLinear(u32 packed) {
    const Vec4 display = UnpackColor(packed);
    return SrgbToLinear(Vec3{display.x, display.y, display.z});
}

inline f32 EdgeFunction(const Vec2& a, const Vec2& b, f32 px, f32 py) {
    return (px - a.x) * (b.y - a.y) - (py - a.y) * (b.x - a.x);
}

struct ProjectedVertex {
    Vec2 screen;
    Vec2 uv;
    f32 ndc_z = 1.0f;
    f32 inv_w = 1.0f;
    f32 ndc_x = 0.0f;
    f32 ndc_y = 0.0f;
};

}  // namespace

// =============================================================================
// Framebuffer
// =============================================================================
void Framebuffer::Resize(u32 new_width, u32 new_height) {
    width = new_width;
    height = new_height;
    color.assign(static_cast<usize>(width) * height * 4u, 0);
    depth.assign(static_cast<usize>(width) * height, 1.0f);
}

void Framebuffer::Clear(const Vec4& color_value, f32 depth_value) {
    const u8 r = ToDisplayByte(color_value.x, srgb);
    const u8 g = ToDisplayByte(color_value.y, srgb);
    const u8 b = ToDisplayByte(color_value.z, srgb);
    const u8 a = ToByte(color_value.w);
    for (usize i = 0; i + 3 < color.size(); i += 4) {
        color[i + 0] = r;
        color[i + 1] = g;
        color[i + 2] = b;
        color[i + 3] = a;
    }
    std::fill(depth.begin(), depth.end(), depth_value);
}

// =============================================================================
// Buffer
// =============================================================================
SoftwareBuffer::SoftwareBuffer(BufferUsage usage, u64 size, u32 stride)
    : usage_(usage), size_(size), stride_(stride), data_(static_cast<usize>(size), 0) {}

bool SoftwareBuffer::Write(const void* data, u64 size, u64 offset) {
    if (data == nullptr || offset + size > size_) {
        return false;
    }
    std::memcpy(data_.data() + static_cast<usize>(offset), data, static_cast<usize>(size));
    return true;
}

// =============================================================================
// Texture
// =============================================================================
SoftwareTexture::SoftwareTexture(const TextureDesc& desc)
    : width_(desc.width), height_(desc.height), format_(desc.format) {
    const u32 bpp = BytesPerPixel(desc.format);
    const usize row = static_cast<usize>(desc.width) * bpp;
    const usize total = row * desc.height;
    pixels_.assign(total, 255);

    if (desc.initial_data != nullptr && IsDepthFormat(desc.format) == false) {
        const auto* source = static_cast<const u8*>(desc.initial_data);
        const u32 source_pitch = desc.row_pitch != 0 ? desc.row_pitch : static_cast<u32>(row);
        for (u32 y = 0; y < desc.height; ++y) {
            std::memcpy(pixels_.data() + y * row, source + static_cast<usize>(y) * source_pitch, row);
        }
    }
}

Vec4 SoftwareTexture::SampleNearest(f32 u, f32 v) const {
    if (width_ == 0 || height_ == 0 || pixels_.empty()) {
        return Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    }
    const f32 wrapped_u = u - std::floor(u);
    const f32 wrapped_v = v - std::floor(v);
    const u32 x = std::min(static_cast<u32>(wrapped_u * static_cast<f32>(width_)), width_ - 1u);
    const u32 y = std::min(static_cast<u32>(wrapped_v * static_cast<f32>(height_)), height_ - 1u);
    const usize offset = (static_cast<usize>(y) * width_ + x) * BytesPerPixel(format_);

    Vec4 sample{1.0f, 1.0f, 1.0f, 1.0f};
    if (format_ == Format::Depth32_Float) {
        f32 depth = 1.0f;
        std::memcpy(&depth, pixels_.data() + offset, sizeof(f32));
        sample = Vec4{depth, depth, depth, 1.0f};
        return sample;
    }
    const f32 scale = 1.0f / 255.0f;
    sample.x = static_cast<f32>(pixels_[offset + 0]) * scale;
    sample.y = static_cast<f32>(pixels_[offset + 1]) * scale;
    sample.z = static_cast<f32>(pixels_[offset + 2]) * scale;
    sample.w = static_cast<f32>(pixels_[offset + 3]) * scale;
    return sample;
}

// =============================================================================
// SwapChain
// =============================================================================
SoftwareSwapChain::SoftwareSwapChain(SoftwareDevice* device, const NativeWindow& window,
                                     const SwapChainDesc& desc)
    : device_(device), format_(desc.format) {
    const u32 width = window.width != 0 ? window.width : desc.width;
    const u32 height = window.height != 0 ? window.height : desc.height;
    framebuffer_.srgb = IsSrgbTarget(desc.format);
    framebuffer_.Resize(std::max(width, 1u), std::max(height, 1u));
    command_list_ = std::make_unique<SoftwareCommandList>(device_, this);
    VALGO_UNUSED(window.headless);
}

bool SoftwareSwapChain::BeginFrame() {
    command_list_->ResetStats();
    return true;
}

void SoftwareSwapChain::Present() {
    // The software backend presents into its own framebuffer. A windowed build
    // forwards the framebuffer to the platform blitter (see platform/window);
    // headless builds simply expose it through ReadPixels().
}

bool SoftwareSwapChain::Resize(u32 width, u32 height) {
    if (width == 0 || height == 0) {
        return false;
    }
    framebuffer_.Resize(width, height);
    return true;
}

bool SoftwareSwapChain::ReadPixels(std::vector<u8>& out_rgba) {
    out_rgba = framebuffer_.color;
    return !out_rgba.empty();
}

// =============================================================================
// CommandList
// =============================================================================
SoftwareCommandList::SoftwareCommandList(SoftwareDevice* device, SoftwareSwapChain* swap_chain)
    : device_(device), swap_chain_(swap_chain) {}

void SoftwareCommandList::ResetStats() {
    draw_calls_ = 0;
    triangles_submitted_ = 0;
    triangles_rasterized_ = 0;
    pixels_shaded_ = 0;
}

void SoftwareCommandList::BeginFrame(const FrameConstants& constants, const ClearValue& clear) {
    frame_ = constants;
    target_ = &swap_chain_->framebuffer();
    target_->Clear(clear.color, clear.depth);

    // Derive the near plane from the projection matrix: m[14] / m[10] is the
    // near distance for the engine's perspective projection.
    const f32 m10 = constants.projection.m[10];
    const f32 m14 = constants.projection.m[14];
    near_plane_ = (std::fabs(m10) > 1.0e-8f) ? (m14 / m10) : 0.05f;
    if (near_plane_ <= 1.0e-4f) {
        near_plane_ = 0.05f;
    }
}

void SoftwareCommandList::SetPipeline(const PipelineDesc& pipeline) { pipeline_ = pipeline; }

void SoftwareCommandList::EndFrame() {
    target_ = nullptr;
}

void SoftwareCommandList::DrawGeometry(const GeometryDraw& draw) {
    if (target_ == nullptr) {
        return;
    }

    draw_constants_ = draw.constants;

    // The sky program is a full-screen pass: it owns no vertex data, so the
    // buffer validation below must not apply to it.
    if (pipeline_.program == ShaderProgram::Sky) {
        FillSky();
        return;
    }

    if (draw.vertex_buffer == nullptr || draw.index_buffer == nullptr) {
        return;
    }

    ++draw_calls_;

    switch (pipeline_.program) {
        case ShaderProgram::Sky:
            break;
        case ShaderProgram::DebugLine:
            // Line work is submitted through DrawLines().
            break;
        case ShaderProgram::Screen2D:
            // Screen-space work is submitted through DrawScreenTriangles().
            break;
        case ShaderProgram::LitMesh:
        case ShaderProgram::UnlitMesh:
        default:
            DrawTriangleList(draw);
            break;
    }
}

void SoftwareCommandList::DrawTriangleList(const GeometryDraw& draw) {
    const auto* vertex_buffer = static_cast<const SoftwareBuffer*>(draw.vertex_buffer);
    const auto* index_buffer = static_cast<const SoftwareBuffer*>(draw.index_buffer);
    if (vertex_buffer == nullptr || index_buffer == nullptr || target_ == nullptr) {
        return;
    }

    const u64 vertex_count = vertex_buffer->size() / sizeof(Vertex);
    const auto* index_bytes = reinterpret_cast<const u8*>(index_buffer->data());
    const auto* vertex_bytes = reinterpret_cast<const u8*>(vertex_buffer->data());

    const bool unlit = pipeline_.program == ShaderProgram::UnlitMesh;
    const Mat4 world_view = frame_.view * draw.constants.world;

    // Textures are optional; the software backend only binds textures it owns.
    current_texture_ = dynamic_cast<const SoftwareTexture*>(draw.base_color_texture);

    for (u32 i = 0; i + 2 < draw.index_count; i += 3) {
        u32 indices[3] = {0, 0, 0};
        bool valid = true;
        for (u32 k = 0; k < 3; ++k) {
            const u64 index_offset = static_cast<u64>(draw.first_index + i + k) * sizeof(u32);
            if (index_offset + sizeof(u32) > index_buffer->size()) {
                valid = false;
                break;
            }
            std::memcpy(&indices[k], index_bytes + index_offset, sizeof(u32));
            if (indices[k] >= vertex_count) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            continue;
        }

        ClipVertex corners[3];
        for (u32 k = 0; k < 3; ++k) {
            Vertex vertex{};
            std::memcpy(&vertex, vertex_bytes + static_cast<u64>(indices[k]) * sizeof(Vertex), sizeof(Vertex));
            corners[k].view_position = world_view.TransformPoint(vertex.position);
            corners[k].normal = Normalize(draw.constants.world.TransformVector(vertex.normal));
            corners[k].uv = vertex.uv;
        }

        ++triangles_submitted_;
        ShadeTriangle(corners[0], corners[1], corners[2], draw.constants, unlit);
    }
}

void SoftwareCommandList::ShadeTriangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c,
                                        const DrawConstants& constants, bool unlit_program) {
    // ---------------------------------------------------------------------
    // 1. Clip against the near plane in view space (RH: visible is z <= -near)
    // ---------------------------------------------------------------------
    const f32 near_z = -near_plane_;
    const ClipVertex* input[3] = {&a, &b, &c};
    ClipVertex polygon[4];
    u32 polygon_count = 0;

    for (u32 i = 0; i < 3; ++i) {
        const ClipVertex& current = *input[i];
        const ClipVertex& next = *input[(i + 1) % 3];
        const bool current_inside = current.view_position.z <= near_z;
        const bool next_inside = next.view_position.z <= near_z;

        if (current_inside) {
            polygon[polygon_count++] = current;
        }
        if (current_inside != next_inside) {
            const f32 denom = next.view_position.z - current.view_position.z;
            if (std::fabs(denom) > 1.0e-8f) {
                const f32 t = (near_z - current.view_position.z) / denom;
                ClipVertex clipped;
                clipped.view_position = Lerp(current.view_position, next.view_position, t);
                clipped.normal = Normalize(Lerp(current.normal, next.normal, t));
                clipped.uv = Lerp(current.uv, next.uv, t);
                polygon[polygon_count++] = clipped;
            }
        }
    }

    if (polygon_count < 3) {
        return;
    }

    // ---------------------------------------------------------------------
    // 2. Project and set up screen-space data
    // ---------------------------------------------------------------------
    ProjectedVertex projected[4];
    for (u32 i = 0; i < polygon_count; ++i) {
        const Vec4 clip = frame_.projection * Vec4{polygon[i].view_position, 1.0f};
        if (clip.w <= 1.0e-6f) {
            return;
        }
        const f32 inv_w = 1.0f / clip.w;
        const f32 ndc_x = clip.x * inv_w;
        const f32 ndc_y = clip.y * inv_w;

        ProjectedVertex& out = projected[i];
        out.ndc_x = ndc_x;
        out.ndc_y = ndc_y;
        out.uv = polygon[i].uv;
        out.ndc_z = clip.z * inv_w;
        out.inv_w = inv_w;
        out.screen.x = (ndc_x * 0.5f + 0.5f) * static_cast<f32>(target_->width);
        out.screen.y = (0.5f - ndc_y * 0.5f) * static_cast<f32>(target_->height);
    }

    // ---------------------------------------------------------------------
    // 3. Fan triangulation of the clipped polygon
    // ---------------------------------------------------------------------
    for (u32 fan = 1; fan + 1 < polygon_count; ++fan) {
        const u32 i0 = 0;
        const u32 i1 = fan;
        const u32 i2 = fan + 1;
        const ProjectedVertex& p0 = projected[i0];
        const ProjectedVertex& p1 = projected[i1];
        const ProjectedVertex& p2 = projected[i2];

        // Counter-clockwise winding in NDC space is front-facing.
        const f32 area_ndc = (p1.ndc_x - p0.ndc_x) * (p2.ndc_y - p0.ndc_y) -
                             (p2.ndc_x - p0.ndc_x) * (p1.ndc_y - p0.ndc_y);
        if (std::fabs(area_ndc) < 1.0e-12f) {
            continue;
        }
        if (pipeline_.cull == CullMode::Back && area_ndc < 0.0f) {
            continue;
        }
        if (pipeline_.cull == CullMode::Front && area_ndc > 0.0f) {
            continue;
        }

        // Screen-space area is negated because screen Y points down.
        const f32 area = -((p1.screen.x - p0.screen.x) * (p2.screen.y - p0.screen.y) -
                           (p2.screen.x - p0.screen.x) * (p1.screen.y - p0.screen.y));
        if (std::fabs(area) < 1.0e-9f) {
            continue;
        }
        ++triangles_rasterized_;

        const ClipVertex& v0 = polygon[i0];
        const ClipVertex& v1 = polygon[i1];
        const ClipVertex& v2 = polygon[i2];

        // Bounding box clipped to the framebuffer.
        i32 min_x = static_cast<i32>(std::floor(std::min({p0.screen.x, p1.screen.x, p2.screen.x})));
        i32 max_x = static_cast<i32>(std::ceil(std::max({p0.screen.x, p1.screen.x, p2.screen.x})));
        i32 min_y = static_cast<i32>(std::floor(std::min({p0.screen.y, p1.screen.y, p2.screen.y})));
        i32 max_y = static_cast<i32>(std::ceil(std::max({p0.screen.y, p1.screen.y, p2.screen.y})));
        min_x = std::max(min_x, 0);
        min_y = std::max(min_y, 0);
        max_x = std::min(max_x, static_cast<i32>(target_->width) - 1);
        max_y = std::min(max_y, static_cast<i32>(target_->height) - 1);
        if (min_x > max_x || min_y > max_y) {
            continue;
        }

        const f32 inv_area = 1.0f / area;
        const Vec3 light_direction = Normalize(ToVec3(frame_.light_direction));
        const f32 light_intensity = frame_.light_direction.w;
        const Vec3 light_color = ToVec3(frame_.light_color);
        const Vec3 ambient_sky = ToVec3(frame_.ambient_sky);
        const Vec3 ambient_ground = ToVec3(frame_.ambient_ground);
        const Vec3 fog_color = ToVec3(frame_.fog_color);
        const f32 fog_enabled = frame_.fog_parameters.w;
        const f32 fog_start = frame_.fog_parameters.x;
        const f32 fog_end = frame_.fog_parameters.y;

        const f32 alpha = constants.color.w;
        const f32 unlit_amount = Saturate(constants.params.x);
        const f32 emissive = constants.params.z;
        const bool is_unlit = unlit_program || unlit_amount >= 0.999f;
        const bool blend = pipeline_.blend == BlendMode::Alpha;

        for (i32 y = min_y; y <= max_y; ++y) {
            const f32 py = static_cast<f32>(y) + 0.5f;
            for (i32 x = min_x; x <= max_x; ++x) {
                const f32 px = static_cast<f32>(x) + 0.5f;

                const f32 w0 = EdgeFunction(p1.screen, p2.screen, px, py) * inv_area;
                const f32 w1 = EdgeFunction(p2.screen, p0.screen, px, py) * inv_area;
                const f32 w2 = EdgeFunction(p0.screen, p1.screen, px, py) * inv_area;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }

                const f32 depth = w0 * p0.ndc_z + w1 * p1.ndc_z + w2 * p2.ndc_z;
                if (depth < 0.0f || depth > 1.0f) {
                    continue;
                }

                const usize pixel_index = static_cast<usize>(y) * target_->width + static_cast<usize>(x);
                const f32 stored = target_->depth[pixel_index];
                bool depth_pass = true;
                if (pipeline_.depth_test) {
                    switch (pipeline_.depth_compare) {
                        case CompareOp::Less: depth_pass = depth < stored; break;
                        case CompareOp::LessEqual: depth_pass = depth <= stored; break;
                        case CompareOp::Greater: depth_pass = depth > stored; break;
                        case CompareOp::Always: depth_pass = true; break;
                    }
                }
                if (!depth_pass) {
                    continue;
                }

                // Perspective-correct interpolation of 1/w.
                const f32 inv_w = w0 * p0.inv_w + w1 * p1.inv_w + w2 * p2.inv_w;
                const f32 perspective_scale = 1.0f / std::max(inv_w, 1.0e-12f);
                const f32 b0 = w0 * p0.inv_w * perspective_scale;
                const f32 b1 = w1 * p1.inv_w * perspective_scale;
                const f32 b2 = w2 * p2.inv_w * perspective_scale;

                Vec3 albedo = ToVec3(constants.color);
                const f32 texture_weight = Saturate(constants.params.y);
                if (current_texture_ != nullptr && texture_weight > 0.001f) {
                    const Vec2 uv = p0.uv * b0 + p1.uv * b1 + p2.uv * b2;
                    const Vec4 texel = current_texture_->SampleNearest(uv.x, uv.y);
                    const Vec3 texel_linear = SrgbToLinear(Vec3{texel.x, texel.y, texel.z});
                    albedo = Lerp(albedo, Vec3{albedo.x * texel_linear.x, albedo.y * texel_linear.y,
                                               albedo.z * texel_linear.z},
                                  texture_weight);
                }

                Vec3 rgb;
                if (is_unlit) {
                    rgb = albedo;
                } else {
                    Vec3 normal = Normalize(v0.normal * b0 + v1.normal * b1 + v2.normal * b2);
                    const Vec3 view_position = v0.view_position * b0 + v1.view_position * b1 +
                                               v2.view_position * b2;
                    const Vec3 to_camera = Normalize(-view_position);
                    if (Dot(normal, to_camera) < 0.0f) {
                        normal = -normal;  // two-sided lighting
                    }

                    const f32 ndl = std::max(Dot(normal, light_direction), 0.0f) * light_intensity;
                    const Vec3 hemisphere = Lerp(ambient_ground, ambient_sky, normal.y * 0.5f + 0.5f);
                    // Camera-facing fill keeps surfaces readable in a single-light
                    // forward renderer (matches the D3D12 shader exactly).
                    const f32 fill = std::max(Dot(normal, to_camera), 0.0f) * 0.20f;
                    rgb = albedo * (hemisphere + ambient_sky * fill + light_color * ndl);

                    if (unlit_amount > 0.0f) {
                        rgb = Lerp(rgb, albedo, unlit_amount);
                    }
                    if (emissive > 0.0f) {
                        rgb = rgb + albedo * emissive;
                    }

                    if (fog_enabled > 0.5f) {
                        const f32 distance = Length(view_position);
                        const f32 fog_t = Saturate((distance - fog_start) / std::max(fog_end - fog_start, 1.0e-3f));
                        rgb = Lerp(rgb, fog_color, fog_t);
                    }
                }

                StorePixel(target_->color, target_->width, static_cast<u32>(x), static_cast<u32>(y),
                           rgb, alpha, blend, target_->srgb);
                ++pixels_shaded_;

                if (pipeline_.depth_write) {
                    target_->depth[pixel_index] = depth;
                }
            }
        }
    }
}

void SoftwareCommandList::FillSky() {
    if (target_ == nullptr) {
        return;
    }
    const Mat4 inverse_view_projection = frame_.view_projection.Inverse();
    const Vec3 horizon = ToVec3(frame_.sky_horizon);
    const Vec3 zenith = ToVec3(frame_.sky_zenith);
    const Vec3 ground = Vec3{horizon.x * 0.18f, horizon.y * 0.19f, horizon.z * 0.21f};

    for (u32 y = 0; y < target_->height; ++y) {
        const f32 ndc_y = 1.0f - (static_cast<f32>(y) + 0.5f) / static_cast<f32>(target_->height) * 2.0f;
        const Vec4 world = inverse_view_projection * Vec4{0.0f, ndc_y, 1.0f, 1.0f};
        if (std::fabs(world.w) < 1.0e-6f) {
            continue;
        }
        const Vec3 direction = Normalize(Vec3{world.x / world.w, world.y / world.w, world.z / world.w} -
                                         ToVec3(frame_.camera_position));
        const f32 up = direction.y;

        Vec3 rgb;
        if (up >= 0.0f) {
            const f32 t = std::pow(Saturate(up), 0.65f);
            rgb = Lerp(horizon, zenith, t);
        } else {
            const f32 t = std::pow(Saturate(-up), 0.5f);
            rgb = Lerp(horizon, ground, t);
        }

        // Subtle sun glow towards the light direction makes the world readable.
        const f32 sun = Saturate(Dot(direction, Normalize(ToVec3(frame_.light_direction))));
        rgb = rgb + ToVec3(frame_.light_color) * (std::pow(sun, 64.0f) * 0.55f + std::pow(sun, 8.0f) * 0.08f);

        const usize row_offset = static_cast<usize>(y) * target_->width * 4u;
        for (u32 x = 0; x < target_->width; ++x) {
            const usize offset = row_offset + static_cast<usize>(x) * 4u;
            target_->color[offset + 0] = ToDisplayByte(rgb.x, target_->srgb);
            target_->color[offset + 1] = ToDisplayByte(rgb.y, target_->srgb);
            target_->color[offset + 2] = ToDisplayByte(rgb.z, target_->srgb);
            target_->color[offset + 3] = 255;
        }
    }
    ++draw_calls_;
}

void SoftwareCommandList::DrawLines(std::span<const LineVertex> vertices) {
    if (target_ == nullptr || vertices.size() < 2) {
        return;
    }
    ++draw_calls_;

    const Mat4 view_projection = frame_.view_projection;
    const f32 near_z = -near_plane_;
    const f32 width = static_cast<f32>(target_->width);
    const f32 height = static_cast<f32>(target_->height);

    auto project = [&](const Vec3& world, Vec2& out_screen, f32& out_depth, bool& out_valid) {
        const Vec4 view_position = frame_.view * Vec4{world, 1.0f};
        if (view_position.z > near_z) {
            out_valid = false;
            return;
        }
        const Vec4 clip = view_projection * Vec4{world, 1.0f};
        if (clip.w <= 1.0e-6f) {
            out_valid = false;
            return;
        }
        const f32 inv_w = 1.0f / clip.w;
        out_screen.x = (clip.x * inv_w * 0.5f + 0.5f) * width;
        out_screen.y = (0.5f - clip.y * inv_w * 0.5f) * height;
        out_depth = clip.z * inv_w;
        out_valid = true;
    };

    for (usize i = 0; i + 1 < vertices.size(); i += 2) {
        const LineVertex& start = vertices[i];
        const LineVertex& end = vertices[i + 1];

        Vec2 start_screen;
        Vec2 end_screen;
        f32 start_depth = 0.0f;
        f32 end_depth = 0.0f;
        bool start_valid = false;
        bool end_valid = false;
        project(start.position, start_screen, start_depth, start_valid);
        project(end.position, end_screen, end_depth, end_valid);

        if (!start_valid && !end_valid) {
            // Whole segment behind the near plane: nothing to draw.
            continue;
        }
        if (!start_valid || !end_valid) {
            // Clip the segment against the near plane in view space.
            const Vec4 view_start = frame_.view * Vec4{start.position, 1.0f};
            const Vec4 view_end = frame_.view * Vec4{end.position, 1.0f};
            const f32 denom = view_end.z - view_start.z;
            if (std::fabs(denom) < 1.0e-8f) {
                continue;
            }
            const f32 t = (near_z - view_start.z) / denom;
            const Vec3 clipped_world = Lerp(start.position, end.position, t);
            const LineVertex clipped{clipped_world, start_valid ? end.color : start.color};
            if (!start_valid) {
                project(clipped.position, start_screen, start_depth, start_valid);
            } else {
                project(clipped.position, end_screen, end_depth, end_valid);
            }
        }
        if (!start_valid || !end_valid) {
            continue;
        }

        const Vec4 start_color = UnpackColor(start.color);
        const Vec4 end_color = UnpackColor(end.color);

        const f32 delta_x = end_screen.x - start_screen.x;
        const f32 delta_y = end_screen.y - start_screen.y;
        const f32 steps = std::max(std::fabs(delta_x), std::fabs(delta_y));
        if (steps < 1.0f) {
            continue;
        }
        const u32 pixel_steps = static_cast<u32>(std::min(steps, 8192.0f));

        for (u32 step = 0; step <= pixel_steps; ++step) {
            const f32 t = static_cast<f32>(step) / static_cast<f32>(pixel_steps);
            const i32 x = static_cast<i32>(start_screen.x + delta_x * t);
            const i32 y = static_cast<i32>(start_screen.y + delta_y * t);
            if (x < 0 || y < 0 || x >= static_cast<i32>(target_->width) || y >= static_cast<i32>(target_->height)) {
                continue;
            }
            const usize pixel_index = static_cast<usize>(y) * target_->width + static_cast<usize>(x);
            const f32 depth = Lerp(start_depth, end_depth, t);
            if (depth < 0.0f || depth > 1.0f || depth > target_->depth[pixel_index]) {
                continue;
            }
            const Vec4 color = Lerp(start_color, end_color, t);
            StorePixel(target_->color, target_->width, static_cast<u32>(x), static_cast<u32>(y),
                       SrgbToLinear(Vec3{color.x, color.y, color.z}), color.w, true, target_->srgb);
        }
    }
}

void SoftwareCommandList::DrawScreenTriangles(std::span<const ScreenVertex> vertices) {
    if (target_ == nullptr || vertices.size() < 3) {
        return;
    }
    ++draw_calls_;
    for (usize i = 0; i + 2 < vertices.size(); i += 3) {
        RasterizeScreenTriangle(vertices[i], vertices[i + 1], vertices[i + 2]);
    }
}

void SoftwareCommandList::RasterizeScreenTriangle(const ScreenVertex& a, const ScreenVertex& b,
                                                  const ScreenVertex& c) {
    const f32 area = -((b.position.x - a.position.x) * (c.position.y - a.position.y) -
                       (c.position.x - a.position.x) * (b.position.y - a.position.y));
    if (std::fabs(area) < 1.0e-6f) {
        return;
    }
    const f32 inv_area = 1.0f / area;

    i32 min_x = static_cast<i32>(std::floor(std::min({a.position.x, b.position.x, c.position.x})));
    i32 max_x = static_cast<i32>(std::ceil(std::max({a.position.x, b.position.x, c.position.x})));
    i32 min_y = static_cast<i32>(std::floor(std::min({a.position.y, b.position.y, c.position.y})));
    i32 max_y = static_cast<i32>(std::ceil(std::max({a.position.y, b.position.y, c.position.y})));
    min_x = std::max(min_x, 0);
    min_y = std::max(min_y, 0);
    max_x = std::min(max_x, static_cast<i32>(target_->width) - 1);
    max_y = std::min(max_y, static_cast<i32>(target_->height) - 1);
    if (min_x > max_x || min_y > max_y) {
        return;
    }

    const Vec4 color_a = UnpackColor(a.color);
    const Vec4 color_b = UnpackColor(b.color);
    const Vec4 color_c = UnpackColor(c.color);
    const bool blend = pipeline_.blend == BlendMode::Alpha;

    for (i32 y = min_y; y <= max_y; ++y) {
        const f32 py = static_cast<f32>(y) + 0.5f;
        for (i32 x = min_x; x <= max_x; ++x) {
            const f32 px = static_cast<f32>(x) + 0.5f;
            const f32 w0 = EdgeFunction(b.position, c.position, px, py) * inv_area;
            const f32 w1 = EdgeFunction(c.position, a.position, px, py) * inv_area;
            const f32 w2 = EdgeFunction(a.position, b.position, px, py) * inv_area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                continue;
            }
            const Vec4 color = color_a * w0 + color_b * w1 + color_c * w2;
            const usize pixel_index = static_cast<usize>(y) * target_->width + static_cast<usize>(x);
            const f32 existing_alpha = static_cast<f32>(target_->color[pixel_index * 4 + 3]) / 255.0f;
            if (blend && existing_alpha > 0.0f && color.w < 0.004f) {
                continue;
            }
            StorePixel(target_->color, target_->width, static_cast<u32>(x), static_cast<u32>(y),
                       SrgbToLinear(Vec3{color.x, color.y, color.z}), color.w, blend, target_->srgb);
        }
    }
}

// =============================================================================
// Device
// =============================================================================
SoftwareDevice::SoftwareDevice(const DeviceDesc& desc) {
    info_.api = GraphicsApi::Software;
    info_.adapter_name = "Valgo software rasteriser";
    info_.api_version = "CPU reference (single threaded)";
    info_.software = true;
    VALGO_UNUSED(desc);
    LogInfo(kCategory, "software RHI device created");
}

std::unique_ptr<Buffer> SoftwareDevice::CreateBuffer(const BufferDesc& desc) {
    auto buffer = std::make_unique<SoftwareBuffer>(desc.usage, desc.size, desc.stride);
    if (desc.initial_data != nullptr) {
        buffer->Write(desc.initial_data, desc.size, 0);
    }
    return buffer;
}

std::unique_ptr<Texture> SoftwareDevice::CreateTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || desc.format == Format::Unknown) {
        LogError(kCategory, "CreateTexture rejected an invalid descriptor");
        return nullptr;
    }
    return std::make_unique<SoftwareTexture>(desc);
}

std::unique_ptr<SwapChain> SoftwareDevice::CreateSwapChain(const NativeWindow& window,
                                                           const SwapChainDesc& desc) {
    return std::make_unique<SoftwareSwapChain>(this, window, desc);
}

}  // namespace valgo::rhi::software
