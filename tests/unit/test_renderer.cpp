// =============================================================================
// Renderer: draw submission, culling, debug geometry, overlay and statistics.
// =============================================================================
#include "../framework/test_framework.h"

#include "framework/engine_harness.h"

#include "valgo/assets/asset_manager.h"
#include "valgo/render/renderer.h"
#include "valgo/scene/scene.h"

using namespace valgo;
using valgo_test::LambdaGame;
using valgo_test::NullGame;
using valgo::scene::Entity;
using valgo::scene::MeshRendererComponent;
using valgo::scene::TransformComponent;

namespace {

Entity AddMesh(valgo::scene::Scene& scene, const render::MeshGpuData* mesh, const Vec3& position,
               const Vec3& scale = Vec3{1.0f, 1.0f, 1.0f}, const Vec3& color = Vec3{0.8f, 0.8f, 0.8f}) {
    const Entity entity = scene.CreateEntity("mesh");
    scene.GetComponent<TransformComponent>(entity)->local.position = position;
    scene.GetComponent<TransformComponent>(entity)->local.scale = scale;
    MeshRendererComponent renderer;
    renderer.mesh = mesh;
    renderer.material = render::Material::MakeSolid(color);
    scene.AddComponent<MeshRendererComponent>(entity, renderer);
    return entity;
}

}  // namespace

VALGO_TEST("renderer", "transform_bounds_covers_rotated_box") {
    const Aabb local = Aabb::FromCenterExtents(Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.5f, 0.25f});
    const Mat4 translated = Mat4::Translation(Vec3{5.0f, 2.0f, -3.0f});
    const Aabb moved = render::TransformBounds(local, translated);
    VALGO_CHECK_NEAR(moved.Center().x, 5.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(moved.Center().y, 2.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(moved.max.z, -2.75f, 1.0e-4f);

    // A 90 degree rotation about Y swaps the X and Z extents.
    const Mat4 rotated = translated * Mat4::RotationY(kHalfPi);
    const Aabb spun = render::TransformBounds(local, rotated);
    VALGO_CHECK_NEAR(spun.Extents().x, 0.25f, 1.0e-3f);
    VALGO_CHECK_NEAR(spun.Extents().z, 1.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(spun.Extents().y, 0.5f, 1.0e-3f);

    const Aabb scaled = render::TransformBounds(local, Mat4::Scale(Vec3{2.0f, 2.0f, 2.0f}));
    VALGO_CHECK_NEAR(scaled.max.x, 2.0f, 1.0e-4f);
}

VALGO_TEST("renderer", "renders_a_scene_and_reports_statistics") {
    valgo_test::EngineHarness harness(160, 90);
    VALGO_REQUIRE(harness.valid());

    const render::MeshGpuData* box = harness.assets().GetPrimitive("box");
    VALGO_REQUIRE(box != nullptr);
    for (int i = 0; i < 4; ++i) {
        AddMesh(harness.scene(), box, Vec3{static_cast<f32>(i) - 1.5f, 0.5f, -4.0f});
    }

    harness.camera().SetPosition(Vec3{0.0f, 1.6f, 0.0f});
    harness.camera().SetAngles(0.0f, 0.0f);
    NullGame game;
    harness.StepFrame(game);
    const render::RenderStats& stats = harness.renderer().stats();
    VALGO_CHECK(stats.meshes_drawn >= 4u);
    VALGO_CHECK(stats.triangles >= 4u * 12u);
    VALGO_CHECK(stats.draw_calls >= 2u);  // sky + meshes
    VALGO_CHECK_EQ(stats.meshes_culled, 0u);
}

VALGO_TEST("renderer", "frustum_culling_skips_geometry_behind_the_camera") {
    valgo_test::EngineHarness harness(128, 72);
    VALGO_REQUIRE(harness.valid());

    const render::MeshGpuData* box = harness.assets().GetPrimitive("box");
    AddMesh(harness.scene(), box, Vec3{0.0f, 0.5f, -5.0f});   // in front
    AddMesh(harness.scene(), box, Vec3{0.0f, 0.5f, 40.0f});   // behind
    AddMesh(harness.scene(), box, Vec3{300.0f, 0.5f, -5.0f});  // far to the side

    harness.camera().SetPosition(Vec3{0.0f, 1.6f, 0.0f});
    harness.camera().SetAngles(0.0f, 0.0f);

    NullGame game;
    harness.StepFrame(game);
    const render::RenderStats culled = harness.renderer().stats();
    VALGO_CHECK_EQ(culled.meshes_drawn, 1u);
    VALGO_CHECK_EQ(culled.meshes_culled, 2u);

    harness.renderer().settings().enable_frustum_culling = false;
    harness.StepFrame(game);
    const render::RenderStats uncalled = harness.renderer().stats();
    VALGO_CHECK_EQ(uncalled.meshes_drawn, 3u);
    VALGO_CHECK_EQ(uncalled.meshes_culled, 0u);
}

VALGO_TEST("renderer", "debug_geometry_is_queued_and_cleared") {
    valgo_test::EngineHarness harness(128, 72);
    VALGO_REQUIRE(harness.valid());
    harness.camera().SetPosition(Vec3{0.0f, 1.6f, 0.0f});
    harness.camera().SetAngles(0.0f, 0.0f);

    // Debug geometry is queued from the game's update, exactly like real code.
    bool draw_debug = true;
    LambdaGame game([&](game::GameServices& services) {
        if (!draw_debug) {
            return;
        }
        services.renderer->DrawLine(Vec3{-2.0f, 0.0f, -4.0f}, Vec3{2.0f, 0.0f, -4.0f},
                                    Vec4{1.0f, 0.0f, 0.0f, 1.0f});
        services.renderer->DrawAabb(Aabb::FromCenterExtents(Vec3{0.0f, 1.0f, -6.0f}, Vec3{0.5f, 0.5f, 0.5f}),
                                    Vec4{0.0f, 1.0f, 0.0f, 1.0f});
        services.renderer->DrawWireSphere(Vec3{0.0f, 1.0f, -8.0f}, 1.0f, Vec4{0.0f, 0.0f, 1.0f, 1.0f}, 8);
    });

    harness.StepFrame(game);
    VALGO_CHECK(harness.renderer().stats().debug_lines > 0u);

    // Debug geometry lives for a single frame only.
    draw_debug = false;
    harness.StepFrame(game);
    VALGO_CHECK_EQ(harness.renderer().stats().debug_lines, 0u);
}

VALGO_TEST("renderer", "overlay_draws_text_and_rectangles") {
    valgo_test::EngineHarness harness(200, 100);
    VALGO_REQUIRE(harness.valid());

    render::Overlay2D& overlay = harness.renderer().overlay();
    overlay.Clear();
    VALGO_CHECK(overlay.empty());

    overlay.DrawRect(Vec2{10.0f, 10.0f}, Vec2{50.0f, 20.0f}, Vec4{1.0f, 0.0f, 0.0f, 1.0f});
    VALGO_CHECK_EQ(overlay.triangle_count(), static_cast<usize>(2));

    overlay.DrawText(Vec2{10.0f, 40.0f}, "VALGO", Vec4{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    VALGO_CHECK(overlay.triangle_count() > 2u);

    const Vec2 measured = overlay.MeasureText("VALGO", 2.0f);
    VALGO_CHECK_NEAR(measured.x, 5.0f * render::kFontAdvance * 2.0f, 1.0f);
    VALGO_CHECK_NEAR(measured.y, render::kFontHeight * 2.0f, 1.0f);

    LambdaGame game([](game::GameServices& services) {
        services.renderer->overlay().DrawText({10.0f, 10.0f}, "HUD", {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
    });
    harness.StepFrame(game);
    VALGO_CHECK(harness.renderer().stats().overlay_triangles > 0u);

    // The application calls BeginFrame() every frame, which clears stale overlays.
    harness.renderer().BeginFrame();
    VALGO_CHECK_EQ(harness.renderer().overlay().triangle_count(), static_cast<usize>(0));
}


VALGO_TEST("renderer", "scene_lights_override_renderer_defaults") {
    valgo_test::EngineHarness harness(96, 54);
    VALGO_REQUIRE(harness.valid());

    const Entity light_entity = harness.scene().CreateEntity("light");
    scene::DirectionalLightComponent light;
    light.direction = Vec3{0.2f, -1.0f, -1.0f};  // shines towards the camera-facing side
    light.color = Vec3{1.0f, 0.0f, 0.0f};
    light.intensity = 2.0f;
    harness.scene().AddComponent<scene::DirectionalLightComponent>(light_entity, light);

    AddMesh(harness.scene(), harness.assets().GetPrimitive("box"), Vec3{0.0f, 0.0f, -3.0f},
            Vec3{2.0f, 2.0f, 2.0f});
    harness.camera().SetPosition(Vec3{0.0f, 1.0f, 0.0f});
    harness.camera().SetAngles(0.0f, 0.0f);
    // Isolate the mesh: no sky, black clear, no fog.
    harness.renderer().settings().draw_sky = false;
    harness.renderer().settings().enable_fog = false;
    harness.renderer().settings().clear_color = Vec3{0.0f, 0.0f, 0.0f};

    NullGame game;
    harness.StepFrame(game);

    Image frame;
    VALGO_REQUIRE(harness.CaptureImage(frame));
    // A red light on a grey box means red dominates the brightest pixels.
    u64 r = 0;
    u64 g = 0;
    u64 b = 0;
    for (usize i = 0; i + 3 < frame.pixels.size(); i += 4) {
        r += frame.pixels[i + 0];
        g += frame.pixels[i + 1];
        b += frame.pixels[i + 2];
    }
    VALGO_CHECK_MSG(r > 0u, "nothing was rendered");
    VALGO_CHECK_MSG(r * 2 > g * 3 && r * 2 > b * 3, "scene directional light colour was not applied");
}
