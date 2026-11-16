// =============================================================================
// Project Valgo — CPU mesh data and procedural primitive generation.
//
// Procedural primitives are not a game-specific shortcut: they are the engine's
// fallback content and the test asset set. Real assets arrive through the OBJ
// importer and (next milestone) the full asset pipeline.
// =============================================================================
#pragma once

#include <vector>

#include "valgo/math/math.h"
#include "valgo/rhi/rhi.h"

namespace valgo::assets {

/// CPU-side mesh: positions, normals, UVs and 32-bit indices.
struct MeshData {
    std::vector<rhi::Vertex> vertices;
    std::vector<u32> indices;
    Aabb bounds{Aabb::FromCenterExtents({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f})};

    void RecomputeBounds();
    void RecomputeSmoothNormals();
    usize triangle_count() const { return indices.size() / 3; }
    bool empty() const { return vertices.empty() || indices.empty(); }
};

/// Axis-aligned box centred on the origin. Winding is counter-clockwise when
/// seen from outside (the engine's front-face convention).
MeshData MakeBox(const Vec3& half_extents);

/// Horizontal plane (normal +Y) centred on the origin.
MeshData MakePlane(f32 half_width, f32 half_depth);

/// Subdivided horizontal plane; useful for large floors with vertex detail.
MeshData MakeSubdividedPlane(f32 half_extent, u32 subdivisions);

/// Vertical cylinder centred on the origin.
MeshData MakeCylinder(f32 radius, f32 height, u32 segments = 24);

/// UV sphere centred on the origin.
MeshData MakeSphere(f32 radius, u32 rings = 16, u32 segments = 32);

/// Thin quad facing -Z (used for HUD-style props and muzzle flashes).
MeshData MakeQuad(f32 half_width, f32 half_height);

}  // namespace valgo::assets
