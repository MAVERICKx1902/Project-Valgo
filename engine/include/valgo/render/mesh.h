// =============================================================================
// Project Valgo — GPU mesh and material descriptions.
//
// MeshGpuData is created and owned by assets::AssetManager; scenes reference it
// by pointer (stable for the lifetime of the manager). Materials are plain
// values so game code can tint individual instances without new uploads.
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include "valgo/math/math.h"
#include "valgo/rhi/rhi.h"

namespace valgo::render {

struct MeshGpuData {
    std::unique_ptr<rhi::Buffer> vertex_buffer;
    std::unique_ptr<rhi::Buffer> index_buffer;
    u32 index_count = 0;
    u32 vertex_count = 0;
    Aabb bounds;  ///< Local-space bounds used for frustum culling.
    std::string name;

    bool IsValid() const { return vertex_buffer != nullptr && index_buffer != nullptr && index_count > 0; }
};

/// Surface description consumed by the LitMesh / UnlitMesh shader programs.
struct Material {
    Vec3 base_color{0.75f, 0.75f, 0.75f};
    f32 opacity = 1.0f;
    f32 emissive = 0.0f;
    f32 unlit = 0.0f;  ///< 1 = ignore lighting (muzzle flashes, HUD props).
    const rhi::Texture* base_color_texture = nullptr;

    static Material MakeSolid(const Vec3& color) {
        Material material;
        material.base_color = color;
        return material;
    }
    static Material MakeUnlit(const Vec3& color, f32 emissive = 0.0f) {
        Material material;
        material.base_color = color;
        material.unlit = 1.0f;
        material.emissive = emissive;
        return material;
    }
};

}  // namespace valgo::render
