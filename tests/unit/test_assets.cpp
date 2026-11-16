// =============================================================================
// Assets: procedural primitives, OBJ import and the asset manager cache.
// =============================================================================
#include "../framework/test_framework.h"

#include <string>

#include "valgo/assets/asset_manager.h"
#include "valgo/assets/mesh_library.h"
#include "valgo/assets/obj_loader.h"
#include "valgo/rhi/rhi.h"

using namespace valgo;
using namespace valgo::assets;

namespace {

rhi::Vertex MakeVertex(const Vec3& position, const Vec3& normal = {0.0f, 1.0f, 0.0f}) {
    return rhi::Vertex{position, normal, Vec2{0.0f, 0.0f}};
}

bool AllNormalsUnitLength(const MeshData& mesh) {
    for (const rhi::Vertex& vertex : mesh.vertices) {
        if (std::fabs(Length(vertex.normal) - 1.0f) > 1.0e-3f) {
            return false;
        }
    }
    return true;
}

bool IndicesInRange(const MeshData& mesh) {
    for (const u32 index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            return false;
        }
    }
    return true;
}

bool NormalsPointOutwards(const MeshData& mesh) {
    // For closed convex primitives centred on the origin the vertex normal must
    // agree with the direction from the centre.
    for (const rhi::Vertex& vertex : mesh.vertices) {
        if (Dot(vertex.normal, Normalize(vertex.position)) < 0.5f) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
VALGO_TEST("assets", "box_primitive_is_well_formed") {
    const MeshData box = MakeBox(Vec3{0.5f, 1.0f, 2.0f});
    VALGO_REQUIRE(!box.empty());
    VALGO_CHECK_EQ(box.triangle_count(), static_cast<usize>(12));  // 6 faces * 2
    VALGO_CHECK_EQ(box.vertices.size(), static_cast<usize>(24));   // 4 per face, hard edges
    VALGO_CHECK(IndicesInRange(box));
    VALGO_CHECK(AllNormalsUnitLength(box));
    VALGO_CHECK(NormalsPointOutwards(MakeBox(Vec3{1.0f, 1.0f, 1.0f})));
    VALGO_CHECK_NEAR(box.bounds.min.x, -0.5f, 1.0e-4f);
    VALGO_CHECK_NEAR(box.bounds.max.y, 1.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(box.bounds.max.z, 2.0f, 1.0e-4f);
}

VALGO_TEST("assets", "plane_and_quad_primitives") {
    const MeshData plane = MakePlane(5.0f, 3.0f);
    VALGO_REQUIRE(!plane.empty());
    VALGO_CHECK_EQ(plane.triangle_count(), static_cast<usize>(2));
    VALGO_CHECK(AllNormalsUnitLength(plane));
    VALGO_CHECK_NEAR(plane.bounds.max.y, 0.0f, 1.0e-5f);

    const MeshData subdivided = MakeSubdividedPlane(4.0f, 4);
    VALGO_REQUIRE(!subdivided.empty());
    VALGO_CHECK(subdivided.triangle_count() > plane.triangle_count());
    VALGO_CHECK(IndicesInRange(subdivided));
    VALGO_CHECK(AllNormalsUnitLength(subdivided));
    VALGO_CHECK_NEAR(subdivided.bounds.max.x, 4.0f, 1.0e-4f);

    const MeshData quad = MakeQuad(1.0f, 0.5f);
    VALGO_REQUIRE(!quad.empty());
    VALGO_CHECK_EQ(quad.triangle_count(), static_cast<usize>(2));
    VALGO_CHECK_NEAR(quad.bounds.max.y, 0.5f, 1.0e-4f);
}

VALGO_TEST("assets", "cylinder_and_sphere_primitives") {
    const MeshData cylinder = MakeCylinder(0.5f, 2.0f, 16);
    VALGO_REQUIRE(!cylinder.empty());
    VALGO_CHECK_NEAR(cylinder.bounds.max.y, 1.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(cylinder.bounds.max.x, 0.5f, 1.0e-3f);
    VALGO_CHECK(IndicesInRange(cylinder));
    VALGO_CHECK(AllNormalsUnitLength(cylinder));

    const MeshData sphere = MakeSphere(1.5f, 8, 12);
    VALGO_REQUIRE(!sphere.empty());
    VALGO_CHECK_NEAR(sphere.bounds.max.x, 1.5f, 1.0e-3f);
    VALGO_CHECK_NEAR(sphere.bounds.min.y, -1.5f, 1.0e-3f);
    VALGO_CHECK(IndicesInRange(sphere));
    VALGO_CHECK(AllNormalsUnitLength(sphere));
    VALGO_CHECK(NormalsPointOutwards(sphere));
}

VALGO_TEST("assets", "primitive_bounds_and_normals_are_recomputed") {
    MeshData mesh;
    mesh.vertices = {MakeVertex({0.0f, 0.0f, 0.0f}), MakeVertex({1.0f, 0.0f, 0.0f}),
                     MakeVertex({0.0f, 2.0f, 0.0f})};
    mesh.indices = {0, 1, 2};
    mesh.RecomputeBounds();
    VALGO_CHECK_NEAR(mesh.bounds.max.y, 2.0f, 1.0e-5f);
    mesh.RecomputeSmoothNormals();
    for (const rhi::Vertex& vertex : mesh.vertices) {
        VALGO_CHECK_NEAR(Length(vertex.normal), 1.0f, 1.0e-3f);
    }
}

// ---------------------------------------------------------------------------
// OBJ import
// ---------------------------------------------------------------------------
VALGO_TEST("assets", "obj_parser_reads_triangles_and_quads") {
    const std::string text =
        "# comment\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 0 -1\n"
        "v 0 0 -1\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vn 0 1 0\n"
        "f 1/1/1 2/2/1 3/2/1 4/1/1\n"
        "f 1//1 3//1 2//1\n";

    const std::optional<ObjLoadResult> result = ParseObj(text, "inline");
    VALGO_REQUIRE(result.has_value());
    // Vertices are de-duplicated per (position, uv, normal) triple, so the
    // second face (which omits UVs) adds three new vertices.
    VALGO_CHECK_EQ(result->mesh.vertices.size(), static_cast<usize>(7));
    VALGO_CHECK_EQ(result->mesh.triangle_count(), static_cast<usize>(3));  // quad -> 2, tri -> 1
    VALGO_CHECK(result->had_normals);
    VALGO_CHECK(result->had_uvs);
    VALGO_CHECK(IndicesInRange(result->mesh));
    VALGO_CHECK_NEAR(result->mesh.bounds.min.z, -1.0f, 1.0e-5f);
}

VALGO_TEST("assets", "obj_parser_rejects_malformed_input") {
    VALGO_CHECK(!ParseObj("v 0 0\n", "bad").has_value());
    VALGO_CHECK(!ParseObj("f 1 2 3\n", "no_vertices").has_value());
    VALGO_CHECK(!ParseObj("", "empty").has_value());
}

VALGO_TEST("assets", "obj_parser_generates_normals_when_absent") {
    const std::string text =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";
    const std::optional<ObjLoadResult> result = ParseObj(text, "flat");
    VALGO_REQUIRE(result.has_value());
    VALGO_CHECK(!result->had_normals);
    for (const rhi::Vertex& vertex : result->mesh.vertices) {
        VALGO_CHECK_NEAR(Length(vertex.normal), 1.0f, 1.0e-3f);
        VALGO_CHECK_NEAR(vertex.normal.z, 1.0f, 1.0e-3f);
    }
}

VALGO_TEST("assets", "repository_obj_assets_load") {
    const std::string root = VALGO_PROJECT_ROOT;
    for (const std::string name : {"crate", "target_dummy"}) {
        const std::string path = root + "/games/fps/assets/meshes/" + name + ".obj";
        const std::optional<ObjLoadResult> mesh = LoadObjFile(path);
        VALGO_REQUIRE(mesh.has_value());
        VALGO_CHECK_MSG(mesh->mesh.triangle_count() > 0, ("empty mesh: " + name).c_str());
        VALGO_CHECK(IndicesInRange(mesh->mesh));
        VALGO_CHECK_MSG(mesh->mesh.bounds.Size().y > 0.0f, ("degenerate bounds: " + name).c_str());
    }
}

// ---------------------------------------------------------------------------
// Asset manager
// ---------------------------------------------------------------------------
VALGO_TEST("assets", "manager_uploads_caches_and_resolves") {
    rhi::DeviceDesc desc;
    desc.api = rhi::GraphicsApi::Software;
    const std::unique_ptr<rhi::Device> device = rhi::Device::Create(desc);
    VALGO_REQUIRE(device != nullptr);

    AssetManager manager(*device);
    manager.SetAssetRoot(VALGO_PROJECT_ROOT);
    VALGO_CHECK_EQ(manager.mesh_count(), static_cast<usize>(0));

    const render::MeshGpuData* box = manager.GetPrimitive("box");
    VALGO_REQUIRE(box != nullptr);
    VALGO_CHECK(box->IsValid());
    VALGO_CHECK(box->index_count > 0);
    VALGO_CHECK_EQ(manager.GetPrimitive("box"), box);  // cached, not duplicated
    VALGO_CHECK(manager.GetPrimitive("unknown_primitive") == nullptr);

    VALGO_CHECK(manager.GetPrimitive("sphere") != nullptr);
    VALGO_CHECK(manager.GetPrimitive("cylinder") != nullptr);
    VALGO_CHECK(manager.GetPrimitive("plane") != nullptr);

    const render::MeshGpuData* crate = manager.LoadObjMesh("crate", "games/fps/assets/meshes/crate.obj");
    VALGO_REQUIRE(crate != nullptr);
    VALGO_CHECK(manager.HasMesh("crate"));
    VALGO_CHECK_EQ(manager.FindMesh("crate"), crate);
    VALGO_CHECK(!manager.HasMesh("missing"));

    const rhi::Texture* white = manager.GetWhiteTexture();
    VALGO_REQUIRE(white != nullptr);
    VALGO_CHECK_EQ(white->width(), 1u);
    const rhi::Texture* red = manager.GetSolidTexture("red", Vec4{1.0f, 0.0f, 0.0f, 1.0f});
    VALGO_REQUIRE(red != nullptr);
    VALGO_CHECK_EQ(red, manager.GetSolidTexture("red", Vec4{0.0f, 1.0f, 0.0f, 1.0f}));  // cached by name
    VALGO_CHECK(manager.texture_count() >= 2);

    VALGO_CHECK(manager.ResolvePath("meshes/crate.obj").find("meshes/crate.obj") != std::string::npos);
}
