// =============================================================================
// Integration: engine reusability.
//
// Two games from different genres run through the same engine, in the same
// process, using the same public interfaces. If this test ever needs FPS code to
// pass, the engine has become game-specific and the test fails.
// =============================================================================
#include "../framework/test_framework.h"

#include "framework/engine_harness.h"

#include "valgo_fps/fps_game.h"
#include "valgo_orbit/orbit_game.h"

using namespace valgo;
using valgo_test::EngineHarness;

VALGO_TEST("integration", "two_genres_share_one_engine") {
    EngineHarness fps_harness(240, 135);
    EngineHarness orbit_harness(240, 135);
    VALGO_REQUIRE(fps_harness.valid());
    VALGO_REQUIRE(orbit_harness.valid());

    valgo_fps::FpsGame fps;
    valgo_orbit::OrbitGame orbit;

    fps.OnStartup(fps_harness.services());
    orbit.OnStartup(orbit_harness.services());

    VALGO_CHECK_MSG(fps_harness.scene().entity_count() > 0u, "the FPS built no scene");
    VALGO_CHECK_MSG(orbit_harness.scene().entity_count() > 0u, "Orbit Runner built no scene");
    VALGO_CHECK_NEAR(orbit.orb_count(), 10.0, 0.0);
    VALGO_CHECK_EQ(orbit.score(), 0u);

    for (int i = 0; i < 60; ++i) {
        fps_harness.StepFrame(fps);
        orbit_harness.StepFrame(orbit);
    }

    // Both games rendered something, and each game's camera was driven by its
    // own logic (chase camera for orbit, first person for the FPS).
    VALGO_CHECK(fps_harness.renderer().stats().meshes_drawn > 0u);
    VALGO_CHECK(orbit_harness.renderer().stats().meshes_drawn > 0u);
    VALGO_CHECK(orbit_harness.camera().position().y > 5.0f);
    VALGO_CHECK(fps_harness.camera().position().y < 3.0f);

    // The scenes are fully independent.
    VALGO_CHECK(fps_harness.scene().entity_count() != orbit_harness.scene().entity_count());

    fps.OnShutdown(fps_harness.services());
    orbit.OnShutdown(orbit_harness.services());
}

VALGO_TEST("integration", "orbit_runner_gameplay_runs_on_engine_services") {
    EngineHarness harness(240, 135);
    VALGO_REQUIRE(harness.valid());

    valgo_orbit::OrbitGameOptions options;
    options.scripted_demo = true;
    valgo_orbit::OrbitGame game(options);
    game.OnStartup(harness.services());

    const f32 start_time = game.time_remaining();
    const Vec3 start_position = game.ship_position();

    for (int i = 0; i < 600; ++i) {  // ten seconds
        harness.StepFrame(game);
    }

    VALGO_CHECK_MSG(game.time_remaining() < start_time, "the run timer never advanced");
    VALGO_CHECK_MSG(Length(game.ship_position() - start_position) > 1.0f, "the ship never moved");
    VALGO_CHECK_MSG(game.score() > 0u, "the scripted run collected no orbs");
    VALGO_CHECK(harness.renderer().stats().meshes_drawn > 0u);
    VALGO_CHECK(harness.renderer().stats().overlay_triangles > 0u);
    VALGO_CHECK(harness.renderer().stats().debug_lines > 0u);
}

VALGO_TEST("integration", "engine_features_are_not_fps_specific") {
    // The engine's public surface is exercised with no FPS concepts: physics
    // queries, scene hierarchy, rendering and the camera all work standalone.
    EngineHarness harness(160, 90);
    VALGO_REQUIRE(harness.valid());

    physics::PhysicsWorld& physics = harness.physics();
    const u32 collider = physics.AddStaticBox(Aabb::FromCenterExtents({0.0f, 1.0f, -5.0f}, {1.0f, 1.0f, 1.0f}),
                                              7u, physics::kLayerProp);
    VALGO_CHECK(collider != 0u);

    const RayHit hit = physics.Raycast(Ray{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, 50.0f,
                                       physics::kLayerProp);
    VALGO_CHECK(hit.hit);
    VALGO_CHECK(hit.distance < 5.0f);

    VALGO_REQUIRE(physics.RemoveCollider(collider));
    VALGO_CHECK(!physics.Raycast(Ray{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, 50.0f,
                                 physics::kLayerProp)
                     .hit);

    // Camera helpers used for gameplay aiming are engine code, not game code.
    render::Camera camera;
    camera.SetPerspective(70.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    camera.SetPosition(Vec3{0.0f, 1.0f, 0.0f});
    camera.SetAngles(0.0f, 0.0f);
    const Ray center = camera.ScreenRay(80.0f, 45.0f, 160.0f, 90.0f);
    VALGO_CHECK_NEAR(center.direction.z, -1.0f, 1.0e-3f);
}
