// =============================================================================
// Physics: raycasting, axis sweeps and the character motor.
// =============================================================================
#include "../framework/test_framework.h"

#include "valgo/physics/character_motor.h"
#include "valgo/physics/physics_world.h"

using namespace valgo;
using namespace valgo::physics;

namespace {

PhysicsWorld MakeFloorWorld(f32 floor_top = 0.0f) {
    PhysicsWorld world;
    world.AddStaticBox(Aabb{Vec3{-50.0f, floor_top - 1.0f, -50.0f}, Vec3{50.0f, floor_top, 50.0f}},
                       kInvalidIndex, kLayerWorld);
    return world;
}

CharacterMotor MakeMotor(const Vec3& feet) {
    CharacterMotorConfig config;
    config.collider_min = Vec3{-0.32f, 0.0f, -0.32f};
    config.collider_max = Vec3{0.32f, 1.82f, 0.32f};
    config.gravity = 20.0f;
    config.jump_speed = 5.9f;
    config.step_height = 0.38f;
    CharacterMotor motor(config);
    motor.Teleport(feet);
    return motor;
}

}  // namespace

VALGO_TEST("physics", "raycast_hits_box_with_surface_normal") {
    PhysicsWorld world;
    world.AddStaticBox(Aabb::FromCenterExtents({0.0f, 0.0f, -5.0f}, {1.0f, 1.0f, 1.0f}), 42u, kLayerProp);

    Ray ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    const RayHit hit = world.Raycast(ray, 100.0f);
    VALGO_REQUIRE(hit.hit);
    VALGO_CHECK_EQ(hit.user_data, 42u);
    VALGO_CHECK_NEAR(hit.distance, 4.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(hit.normal.z, 1.0f, 1.0e-4f);
    VALGO_CHECK_NEAR(hit.point.z, -4.0f, 1.0e-4f);
}

VALGO_TEST("physics", "raycast_respects_layer_mask_and_miss") {
    PhysicsWorld world;
    world.AddStaticBox(Aabb::FromCenterExtents({0.0f, 0.0f, -5.0f}, {1.0f, 1.0f, 1.0f}), 1u, kLayerProp);
    Ray ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};

    VALGO_CHECK(!world.Raycast(ray, 100.0f, kLayerWorld).hit);
    VALGO_CHECK(world.Raycast(ray, 100.0f, kLayerProp).hit);

    Ray sideways{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    VALGO_CHECK(!world.Raycast(sideways, 100.0f).hit);
}

VALGO_TEST("physics", "raycast_returns_closest_hit") {
    PhysicsWorld world;
    world.AddStaticBox(Aabb::FromCenterExtents({0.0f, 0.0f, -10.0f}, {1.0f, 1.0f, 1.0f}), 1u);
    world.AddStaticBox(Aabb::FromCenterExtents({0.0f, 0.0f, -4.0f}, {1.0f, 1.0f, 1.0f}), 2u);

    Ray ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    const RayHit hit = world.Raycast(ray, 100.0f);
    VALGO_REQUIRE(hit.hit);
    VALGO_CHECK_EQ(hit.user_data, 2u);
    VALGO_CHECK_NEAR(hit.distance, 3.0f, 1.0e-4f);
}

VALGO_TEST("physics", "raycast_obeys_max_distance") {
    PhysicsWorld world;
    world.AddStaticBox(Aabb::FromCenterExtents({0.0f, 0.0f, -10.0f}, {1.0f, 1.0f, 1.0f}), 1u);
    Ray ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    VALGO_CHECK(!world.Raycast(ray, 5.0f).hit);
    VALGO_CHECK(world.Raycast(ray, 20.0f).hit);
}

VALGO_TEST("physics", "motor_lands_on_floor_and_stays_grounded") {
    PhysicsWorld world = MakeFloorWorld();
    CharacterMotor motor = MakeMotor({0.0f, 3.0f, 0.0f});

    for (int i = 0; i < 240; ++i) {
        const CharacterMoveResult result = motor.Move(world, Vec3{}, false, 1.0f / 60.0f);
        (void)result;
    }
    VALGO_CHECK(motor.grounded());
    VALGO_CHECK_NEAR(motor.position().y, 0.0f, 0.01f);
}

VALGO_TEST("physics", "motor_walks_at_requested_speed") {
    PhysicsWorld world = MakeFloorWorld();
    CharacterMotor motor = MakeMotor({0.0f, 0.0f, 0.0f});

    // Settle on the floor first.
    for (int i = 0; i < 10; ++i) {
        motor.Move(world, Vec3{}, false, 1.0f / 60.0f);
    }

    const f32 step = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) {
        motor.Move(world, Vec3{5.0f, 0.0f, 0.0f}, false, step);
    }
    VALGO_CHECK(motor.grounded());
    VALGO_CHECK_NEAR(motor.position().y, 0.0f, 0.02f);
    VALGO_CHECK_NEAR(motor.position().x, 5.0f, 0.15f);
}

VALGO_TEST("physics", "motor_never_sinks_through_the_floor") {
    PhysicsWorld world = MakeFloorWorld();
    CharacterMotor motor = MakeMotor({0.0f, 0.0f, 0.0f});

    f32 minimum_y = 1.0e9f;
    for (int i = 0; i < 600; ++i) {
        // Alternate horizontal movement to exercise the ground probe every step.
        motor.Move(world, Vec3{2.0f, 0.0f, 1.0f}, false, 1.0f / 60.0f);
        minimum_y = std::min(minimum_y, motor.position().y);
    }
    VALGO_CHECK_MSG(minimum_y > -0.02f, "character sank below the floor");
}

VALGO_TEST("physics", "motor_jump_apex_and_landing") {
    PhysicsWorld world = MakeFloorWorld();
    CharacterMotor motor = MakeMotor({0.0f, 0.0f, 0.0f});
    for (int i = 0; i < 10; ++i) {
        motor.Move(world, Vec3{}, false, 1.0f / 60.0f);
    }
    VALGO_REQUIRE(motor.grounded());

    f32 apex = 0.0f;
    bool left_ground = false;
    for (int i = 0; i < 120; ++i) {
        const CharacterMoveResult result = motor.Move(world, Vec3{}, i == 0, 1.0f / 60.0f);
        if (result.jumped) {
            left_ground = true;
        }
        apex = std::max(apex, motor.position().y);
    }
    VALGO_CHECK(left_ground);
    VALGO_CHECK(apex > 0.6f);
    VALGO_CHECK(apex < 1.4f);
    VALGO_CHECK(motor.grounded());
    VALGO_CHECK_NEAR(motor.position().y, 0.0f, 0.02f);
}

VALGO_TEST("physics", "motor_reports_a_single_landing_event") {
    PhysicsWorld world = MakeFloorWorld();
    CharacterMotor motor = MakeMotor({0.0f, 2.0f, 0.0f});
    int landings = 0;
    for (int i = 0; i < 240; ++i) {
        const CharacterMoveResult result = motor.Move(world, Vec3{}, false, 1.0f / 60.0f);
        if (result.landed) {
            ++landings;
        }
    }
    VALGO_CHECK(motor.grounded());
    VALGO_CHECK_NEAR(landings, 1, 0);
}

VALGO_TEST("physics", "motor_is_blocked_by_a_wall") {
    PhysicsWorld world = MakeFloorWorld();
    world.AddStaticBox(Aabb::FromCenterExtents({2.0f, 1.5f, 0.0f}, {0.2f, 1.5f, 4.0f}), 7u, kLayerWorld);
    CharacterMotor motor = MakeMotor({0.0f, 0.0f, 0.0f});
    for (int i = 0; i < 10; ++i) {
        motor.Move(world, Vec3{}, false, 1.0f / 60.0f);
    }

    bool hit_wall = false;
    for (int i = 0; i < 120; ++i) {
        const CharacterMoveResult result = motor.Move(world, Vec3{5.0f, 0.0f, 0.0f}, false, 1.0f / 60.0f);
        hit_wall = hit_wall || result.hit_wall;
    }
    VALGO_CHECK(hit_wall);
    VALGO_CHECK_MSG(motor.position().x < 1.85f, "character passed through the wall");
    VALGO_CHECK(motor.position().x > 1.2f);
}

VALGO_TEST("physics", "motor_steps_up_a_low_ledge") {
    PhysicsWorld world = MakeFloorWorld();
    // A 0.28 m ledge the character should climb thanks to step assistance.
    world.AddStaticBox(Aabb{Vec3{1.5f, 0.0f, -3.0f}, Vec3{4.0f, 0.28f, 3.0f}}, 9u, kLayerWorld);
    CharacterMotor motor = MakeMotor({0.0f, 0.0f, 0.0f});
    for (int i = 0; i < 10; ++i) {
        motor.Move(world, Vec3{}, false, 1.0f / 60.0f);
    }

    bool stepped = false;
    f32 highest = 0.0f;
    for (int i = 0; i < 90; ++i) {
        const CharacterMoveResult result = motor.Move(world, Vec3{4.0f, 0.0f, 0.0f}, false, 1.0f / 60.0f);
        stepped = stepped || result.stepped_up;
        highest = std::max(highest, motor.position().y);
    }
    VALGO_CHECK(stepped);
    VALGO_CHECK_MSG(motor.position().x > 1.6f, "character did not climb onto the ledge");
    VALGO_CHECK_MSG(highest > 0.2f, "character stayed at floor height");
}

VALGO_TEST("physics", "motor_does_not_climb_a_tall_wall") {
    PhysicsWorld world = MakeFloorWorld();
    world.AddStaticBox(Aabb{Vec3{1.5f, 0.0f, -3.0f}, Vec3{4.0f, 1.6f, 3.0f}}, 9u, kLayerWorld);
    CharacterMotor motor = MakeMotor({0.0f, 0.0f, 0.0f});
    for (int i = 0; i < 10; ++i) {
        motor.Move(world, Vec3{}, false, 1.0f / 60.0f);
    }
    for (int i = 0; i < 120; ++i) {
        motor.Move(world, Vec3{4.0f, 0.0f, 0.0f}, false, 1.0f / 60.0f);
    }
    VALGO_CHECK(motor.position().x < 1.5f);
    VALGO_CHECK_NEAR(motor.position().y, 0.0f, 0.05f);
}

VALGO_TEST("physics", "overlap_queries") {
    PhysicsWorld world;
    world.AddStaticBox(Aabb::FromCenterExtents({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}), 11u, kLayerProp);
    u32 user_data = kInvalidIndex;
    VALGO_CHECK(world.OverlapAabb(Aabb::FromCenterExtents({0.0f, 0.0f, 0.0f}, {0.1f, 0.1f, 0.1f}), user_data));
    VALGO_CHECK_EQ(user_data, 11u);
    VALGO_CHECK(!world.OverlapAabb(Aabb::FromCenterExtents({5.0f, 0.0f, 0.0f}, {0.1f, 0.1f, 0.1f}), user_data));
    VALGO_CHECK_EQ(world.OverlapAabbAll(Aabb::FromCenterExtents({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f})).size(),
                   1u);
}
