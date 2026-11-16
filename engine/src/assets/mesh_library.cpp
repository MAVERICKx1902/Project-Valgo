#include "valgo/assets/mesh_library.h"

#include <cmath>

namespace valgo::assets {
namespace {

void AddQuad(MeshData& data, const Vec3& center, const Vec3& u, const Vec3& v, f32 half_u, f32 half_v,
             const Vec3& normal, f32 uv_scale_u = 1.0f, f32 uv_scale_v = 1.0f) {
    const u32 base = static_cast<u32>(data.vertices.size());
    const Vec3 corners[4] = {center - u * half_u - v * half_v, center + u * half_u - v * half_v,
                             center + u * half_u + v * half_v, center - u * half_u + v * half_v};
    const Vec2 uvs[4] = {{0.0f, 1.0f}, {uv_scale_u, 1.0f}, {uv_scale_u, 1.0f - uv_scale_v}, {0.0f, 1.0f - uv_scale_v}};
    for (int i = 0; i < 4; ++i) {
        rhi::Vertex vertex{};
        vertex.position = corners[i];
        vertex.normal = normal;
        vertex.uv = uvs[i];
        data.vertices.push_back(vertex);
    }
    const u32 indices[6] = {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3};
    data.indices.insert(data.indices.end(), indices, indices + 6);
}

}  // namespace

void MeshData::RecomputeBounds() {
    if (vertices.empty()) {
        bounds = Aabb::FromCenterExtents(Vec3{}, Vec3{});
        return;
    }
    bounds.min = vertices[0].position;
    bounds.max = vertices[0].position;
    for (const rhi::Vertex& vertex : vertices) {
        bounds.Encapsulate(vertex.position);
    }
}

void MeshData::RecomputeSmoothNormals() {
    for (rhi::Vertex& vertex : vertices) {
        vertex.normal = Vec3{0.0f, 0.0f, 0.0f};
    }
    for (usize i = 0; i + 2 < indices.size(); i += 3) {
        const u32 i0 = indices[i];
        const u32 i1 = indices[i + 1];
        const u32 i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }
        const Vec3 p0 = vertices[i0].position;
        const Vec3 p1 = vertices[i1].position;
        const Vec3 p2 = vertices[i2].position;
        const Vec3 face_normal = Cross(p1 - p0, p2 - p0);
        vertices[i0].normal += face_normal;
        vertices[i1].normal += face_normal;
        vertices[i2].normal += face_normal;
    }
    for (rhi::Vertex& vertex : vertices) {
        vertex.normal = Normalize(vertex.normal);
    }
}

MeshData MakeBox(const Vec3& half_extents) {
    MeshData data;
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    const Vec3 z{0.0f, 0.0f, 1.0f};

    AddQuad(data, {0.0f, 0.0f, half_extents.z}, x, y, half_extents.x, half_extents.y, z);
    AddQuad(data, {0.0f, 0.0f, -half_extents.z}, -x, y, half_extents.x, half_extents.y, -z);
    AddQuad(data, {half_extents.x, 0.0f, 0.0f}, -z, y, half_extents.z, half_extents.y, x);
    AddQuad(data, {-half_extents.x, 0.0f, 0.0f}, z, y, half_extents.z, half_extents.y, -x);
    AddQuad(data, {0.0f, half_extents.y, 0.0f}, x, -z, half_extents.x, half_extents.z, y);
    AddQuad(data, {0.0f, -half_extents.y, 0.0f}, x, z, half_extents.x, half_extents.z, -y);

    data.RecomputeBounds();
    return data;
}

MeshData MakePlane(f32 half_width, f32 half_depth) {
    MeshData data;
    AddQuad(data, {}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, half_width, half_depth,
            {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f);
    data.RecomputeBounds();
    return data;
}

MeshData MakeSubdividedPlane(f32 half_extent, u32 subdivisions) {
    MeshData data;
    const u32 cells = subdivisions < 1 ? 1 : subdivisions;
    for (u32 z = 0; z <= cells; ++z) {
        for (u32 x = 0; x <= cells; ++x) {
            const f32 fx = static_cast<f32>(x) / static_cast<f32>(cells);
            const f32 fz = static_cast<f32>(z) / static_cast<f32>(cells);
            rhi::Vertex vertex{};
            vertex.position = Vec3{-half_extent + fx * 2.0f * half_extent, 0.0f,
                                   -half_extent + fz * 2.0f * half_extent};
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.uv = {fx, fz};
            data.vertices.push_back(vertex);
        }
    }
    for (u32 z = 0; z < cells; ++z) {
        for (u32 x = 0; x < cells; ++x) {
            const u32 row = cells + 1;
            const u32 i0 = z * row + x;
            const u32 i1 = i0 + 1;
            const u32 i2 = i0 + row;
            const u32 i3 = i2 + 1;
            // Counter-clockwise when viewed from above (+Y).
            data.indices.insert(data.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    data.RecomputeBounds();
    return data;
}

MeshData MakeCylinder(f32 radius, f32 height, u32 segments) {
    MeshData data;
    const u32 sides = segments < 3 ? 3 : segments;
    const f32 half_height = height * 0.5f;
    const f32 step = kTwoPi / static_cast<f32>(sides);

    for (u32 i = 0; i < sides; ++i) {
        const f32 angle0 = step * static_cast<f32>(i);
        const f32 angle1 = step * static_cast<f32>(i + 1);
        const Vec3 n0{std::cos(angle0), 0.0f, std::sin(angle0)};
        const Vec3 n1{std::cos(angle1), 0.0f, std::sin(angle1)};
        const Vec3 p0{n0.x * radius, -half_height, n0.z * radius};
        const Vec3 p1{n1.x * radius, -half_height, n1.z * radius};
        const Vec3 p2{n1.x * radius, half_height, n1.z * radius};
        const Vec3 p3{n0.x * radius, half_height, n0.z * radius};

        const u32 base = static_cast<u32>(data.vertices.size());
        const Vec3 positions[4] = {p0, p1, p2, p3};
        const Vec3 normals[4] = {n0, n1, n1, n0};
        const Vec2 uvs[4] = {{static_cast<f32>(i) / static_cast<f32>(sides), 1.0f},
                             {static_cast<f32>(i + 1) / static_cast<f32>(sides), 1.0f},
                             {static_cast<f32>(i + 1) / static_cast<f32>(sides), 0.0f},
                             {static_cast<f32>(i) / static_cast<f32>(sides), 0.0f}};
        for (int k = 0; k < 4; ++k) {
            rhi::Vertex vertex{};
            vertex.position = positions[k];
            vertex.normal = normals[k];
            vertex.uv = uvs[k];
            data.vertices.push_back(vertex);
        }
        data.indices.insert(data.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }

    // Caps (triangle fans).
    for (int cap = 0; cap < 2; ++cap) {
        const f32 y = cap == 0 ? half_height : -half_height;
        const Vec3 normal{0.0f, cap == 0 ? 1.0f : -1.0f, 0.0f};
        const u32 center_index = static_cast<u32>(data.vertices.size());
        rhi::Vertex center{};
        center.position = {0.0f, y, 0.0f};
        center.normal = normal;
        center.uv = {0.5f, 0.5f};
        data.vertices.push_back(center);

        for (u32 i = 0; i < sides; ++i) {
            const f32 angle = step * static_cast<f32>(i);
            rhi::Vertex vertex{};
            vertex.position = {std::cos(angle) * radius, y, std::sin(angle) * radius};
            vertex.normal = normal;
            vertex.uv = {std::cos(angle) * 0.5f + 0.5f, std::sin(angle) * 0.5f + 0.5f};
            data.vertices.push_back(vertex);
        }
        for (u32 i = 0; i < sides; ++i) {
            const u32 current = center_index + 1 + i;
            const u32 next = center_index + 1 + ((i + 1) % sides);
            if (cap == 0) {
                data.indices.insert(data.indices.end(), {center_index, next, current});
            } else {
                data.indices.insert(data.indices.end(), {center_index, current, next});
            }
        }
    }

    data.RecomputeBounds();
    return data;
}

MeshData MakeSphere(f32 radius, u32 rings, u32 segments) {
    MeshData data;
    const u32 ring_count = rings < 2 ? 2 : rings;
    const u32 segment_count = segments < 3 ? 3 : segments;

    for (u32 ring = 0; ring <= ring_count; ++ring) {
        const f32 v = static_cast<f32>(ring) / static_cast<f32>(ring_count);
        const f32 phi = v * kPi;  // 0 at +Y pole
        const f32 y = std::cos(phi);
        const f32 ring_radius = std::sin(phi);
        for (u32 segment = 0; segment <= segment_count; ++segment) {
            const f32 u = static_cast<f32>(segment) / static_cast<f32>(segment_count);
            const f32 theta = u * kTwoPi;
            const Vec3 normal{ring_radius * std::cos(theta), y, ring_radius * std::sin(theta)};
            rhi::Vertex vertex{};
            vertex.position = normal * radius;
            vertex.normal = normal;
            vertex.uv = {u, v};
            data.vertices.push_back(vertex);
        }
    }

    const u32 stride = segment_count + 1;
    for (u32 ring = 0; ring < ring_count; ++ring) {
        for (u32 segment = 0; segment < segment_count; ++segment) {
            const u32 a = ring * stride + segment;
            const u32 b = a + stride;
            const u32 c = a + 1;
            const u32 d = b + 1;
            // Counter-clockwise seen from outside.
            data.indices.insert(data.indices.end(), {a, b, c, c, b, d});
        }
    }

    data.RecomputeBounds();
    return data;
}

MeshData MakeQuad(f32 half_width, f32 half_height) {
    MeshData data;
    AddQuad(data, {}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, half_width, half_height,
            {0.0f, 0.0f, 1.0f});
    data.RecomputeBounds();
    return data;
}

}  // namespace valgo::assets
