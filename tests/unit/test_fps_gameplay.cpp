// =============================================================================
// FPS gameplay: player controller, weapon model and the game module driven
// through the engine's public interfaces exactly as the application does.
// =============================================================================
#include "../framework/test_framework.h"

#include "framework/engine_harness.h"

#include "valgo_fps/fps_game.h"
#include "valgo_fps/player_controller.h"
#include "test_map.h"
#include "valgo_fps/weapon.h"

using namespace valgo;
using namespace valgo_fps;
using valgo_test::EngineHarness;

namespace {

/// A floor to stand on, plus optional walls, shared by controller tests.
physics::PhysicsWorld MakeWorld(bool with_wall = false) {
    physics::PhysicsWorld world;
    world.AddStaticBox(Aabb{Vec3{-40.0f, -1.0f, -40.0f}, Vec3{40.0f, 0.0f, 40.0f}}, kInvalidIndex,
                       physics::kLayerWorld);
    if (with_wall) {
        world.AddStaticBox(Aabb::FromCenterExtents({0.0f, 1.5f, -3.0f}, {6.0f, 1.5f, 0.25f}), 1u,
                           physics::kLayerWorld);
    }
    return world;
}

PlayerController MakePlayer(const physics::PhysicsWorld& world, const Vec3& feet = {0.0f, 0.0f, 0.0f},
                            f32 yaw = 0.0f) {
    PlayerController player;
    player.Initialize(PlayerTuning{});
    player.Spawn(feet, yaw);
    // Settle on the ground.
    for (int i = 0; i < 8; ++i) {
        player.Update(PlayerInput{}, 1.0f / 60.0f, world);
    }
    return player;
}

/// Runs `seconds` of simulation with a fixed input.
void Simulate(PlayerController& player, const physics::PhysicsWorld& world, const PlayerInput& input, f32 seconds) {
    const f32 step = 1.0f / 60.0f;
    const u32 steps = static_cast<u32>(seconds / step);
    for (u32 i = 0; i < steps; ++i) {
        player.Update(input, step, world);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Player controller
// ---------------------------------------------------------------------------
VALGO_TEST("gameplay", "player_walks_forward_and_stops") {
    const physics::PhysicsWorld world = MakeWorld();
    PlayerController player = MakePlayer(world);
    const f32 start_z = player.FeetPosition().z;

    PlayerInput forward;
    forward.move.y = 1.0f;
    Simulate(player, world, forward, 1.0f);
    const f32 walked = start_z - player.FeetPosition().z;
    VALGO_CHECK_MSG(walked > 3.0f, "player did not walk forward");
    VALGO_CHECK_MSG(walked < 7.0f, "player moved faster than the walk speed");
    VALGO_CHECK_NEAR(player.FeetPosition().y, 0.0f, 0.02f);
    VALGO_CHECK(player.Grounded());

    // Releasing the stick must stop the player within a short slide (friction),
    // not teleport him back and not let him keep gliding.
    const f32 resting_z = player.FeetPosition().z;
    Simulate(player, world, PlayerInput{}, 0.5f);
    VALGO_CHECK_MSG(player.FeetPosition().z > resting_z - 0.5f, "the player slid far after release");
    VALGO_CHECK_NEAR(player.HorizontalSpeed(), 0.0f, 0.05f);
}

VALGO_TEST("gameplay", "sprint_is_faster_than_walk_and_crouch_is_slower") {
    const physics::PhysicsWorld world = MakeWorld();

    PlayerController walker = MakePlayer(world);
    PlayerInput walk;
    walk.move.y = 1.0f;
    Simulate(walker, world, walk, 0.75f);
    const f32 walk_distance = -walker.FeetPosition().z;

    PlayerController sprinter = MakePlayer(world);
    PlayerInput sprint = walk;
    sprint.sprint = true;
    Simulate(sprinter, world, sprint, 0.75f);
    const f32 sprint_distance = -sprinter.FeetPosition().z;

    PlayerController croucher = MakePlayer(world);
    PlayerInput crouch = walk;
    crouch.crouch = true;
    Simulate(croucher, world, crouch, 0.75f);
    const f32 crouch_distance = -croucher.FeetPosition().z;

    VALGO_CHECK_MSG(sprint_distance > walk_distance * 1.15f, "sprint did not increase speed");
    VALGO_CHECK_MSG(crouch_distance < walk_distance * 0.8f, "crouch did not reduce speed");
    VALGO_CHECK(croucher.IsCrouching());
}

VALGO_TEST("gameplay", "crouch_lowers_the_eye_and_returns_to_standing") {
    const physics::PhysicsWorld world = MakeWorld();
    PlayerController player = MakePlayer(world);
    const f32 standing_eye = player.EyePosition().y;
    VALGO_CHECK_NEAR(standing_eye, 1.68f, 0.05f);

    PlayerInput crouch;
    crouch.crouch = true;
    Simulate(player, world, crouch, 0.5f);
    const f32 crouched_eye = player.EyePosition().y;
    VALGO_CHECK_MSG(crouched_eye < standing_eye - 0.4f, "crouch did not lower the camera");
    VALGO_CHECK_NEAR(crouched_eye, 1.02f, 0.08f);

    Simulate(player, world, PlayerInput{}, 0.6f);
    VALGO_CHECK_NEAR(player.EyePosition().y, standing_eye, 0.05f);
    VALGO_CHECK(!player.IsCrouching());
}

VALGO_TEST("gameplay", "eye_never_goes_below_the_feet") {
    const physics::PhysicsWorld world = MakeWorld();
    PlayerController player = MakePlayer(world);

    // Stress the stance and movement transitions that used to sink the camera.
    for (int i = 0; i < 600; ++i) {
        PlayerInput input;
        input.move.y = (i / 60) % 2 == 0 ? 1.0f : -1.0f;
        input.crouch = (i / 45) % 2 == 0;
        input.jump = (i % 120) == 0;
        player.Update(input, 1.0f / 60.0f, world);
        VALGO_CHECK_MSG(player.EyePosition().y > player.FeetPosition().y + 0.5f,
                        "camera eye fell through the player");
    }
}

VALGO_TEST("gameplay", "jump_leaves_the_ground_and_lands") {
    const physics::PhysicsWorld world = MakeWorld();
    PlayerController player = MakePlayer(world);
    VALGO_REQUIRE(player.Grounded());

    PlayerInput jump;
    jump.jump = true;
    player.Update(jump, 1.0f / 60.0f, world);
    VALGO_CHECK(!player.Grounded());

    f32 apex = 0.0f;
    for (int i = 0; i < 120; ++i) {
        player.Update(PlayerInput{}, 1.0f / 60.0f, world);
        apex = std::max(apex, player.FeetPosition().y);
    }
    VALGO_CHECK_MSG(apex > 0.6f, "jump was too shallow");
    VALGO_CHECK_MSG(apex < 1.4f, "jump was too high");
    VALGO_CHECK(player.Grounded());
    // The eye must follow the body, not lag behind or clip through it.
    VALGO_CHECK_NEAR(player.EyePosition().y, player.FeetPosition().y + 1.68f, 0.08f);
}

VALGO_TEST("gameplay", "lean_and_look_change_the_camera_pose") {
    const physics::PhysicsWorld world = MakeWorld();
    PlayerController player = MakePlayer(world);

    PlayerInput look;
    look.look.x = 100.0f;  // mouse pixels to the right
    player.Update(look, 1.0f / 60.0f, world);
    VALGO_CHECK_MSG(player.yaw() < -0.05f, "mouse right did not turn the view right");

    // Mouse down is positive look.y, so negative values look up.
    PlayerInput pitch;
    pitch.look.y = -400.0f;
    player.Update(pitch, 1.0f / 60.0f, world);
    VALGO_CHECK_MSG(player.pitch() > 0.0f, "mouse up did not pitch the view up");
    VALGO_CHECK(player.pitch() <= Radians(88.0f) + 1.0e-3f);

    PlayerInput down;
    down.look.y = 8000.0f;
    player.Update(down, 1.0f / 60.0f, world);
    VALGO_CHECK_NEAR(player.pitch(), -Radians(88.0f), 1.0e-3f);  // clamped

    render::Camera camera;
    camera.SetPerspective(70.0f, 16.0f / 9.0f, 0.05f, 200.0f);
    player.ApplyCamera(camera);
    const f32 neutral_roll = camera.roll();

    PlayerInput lean;
    lean.lean_right = true;
    Simulate(player, world, lean, 0.4f);
    player.ApplyCamera(camera);
    VALGO_CHECK(player.IsLeaning());
    VALGO_CHECK_MSG(std::fabs(camera.roll() - neutral_roll) > 0.02f, "lean did not roll the camera");

    // Aim direction follows pitch/yaw and is normalised.
    const Vec3 aim = player.AimDirection();
    VALGO_CHECK_NEAR(Length(aim), 1.0f, 1.0e-3f);
    VALGO_CHECK_MSG(aim.y < 0.0f, "aim direction ignored the clamped look-down");
    VALGO_CHECK_NEAR(Length(aim), 1.0f, 1.0e-3f);
}

VALGO_TEST("gameplay", "damage_and_death") {
    const physics::PhysicsWorld world = MakeWorld();
    PlayerController player = MakePlayer(world);
    VALGO_CHECK(player.IsAlive());
    VALGO_CHECK_NEAR(player.Health(), 100.0f, 1.0e-4f);

    player.ApplyDamage(40.0f);
    VALGO_CHECK_NEAR(player.Health(), 60.0f, 1.0e-4f);
    player.ApplyDamage(80.0f);
    VALGO_CHECK(!player.IsAlive());
    VALGO_CHECK_NEAR(player.Health(), 0.0f, 1.0e-4f);

    player.ResetHealth();
    VALGO_CHECK(player.IsAlive());
}

VALGO_TEST("gameplay", "player_is_blocked_by_walls") {
    const physics::PhysicsWorld world = MakeWorld(true);
    PlayerController player = MakePlayer(world);
    PlayerInput forward;
    forward.move.y = 1.0f;
    Simulate(player, world, forward, 1.5f);
    VALGO_CHECK_MSG(player.FeetPosition().z > -3.0f, "player walked through a wall");
}

// ---------------------------------------------------------------------------
// Weapon model
// ---------------------------------------------------------------------------
VALGO_TEST("gameplay", "weapon_fire_cadence_and_ammunition") {
    Weapon weapon;
    WeaponConfig config;
    config.fire_interval = 0.1f;
    config.magazine_size = 5;
    weapon.Configure(config);
    weapon.Reset();
    VALGO_CHECK_EQ(weapon.AmmoInMagazine(), 5u);

    WeaponInput fire;
    fire.fire = true;

    VALGO_CHECK(weapon.Update(fire, 0.016f));  // first shot fires immediately
    VALGO_CHECK_EQ(weapon.AmmoInMagazine(), 4u);
    VALGO_CHECK_EQ(weapon.ShotsFired(), 1u);

    VALGO_CHECK(!weapon.Update(fire, 0.016f));  // cadence gate
    VALGO_CHECK_EQ(weapon.AmmoInMagazine(), 4u);

    // Hold the trigger for two seconds: the magazine must run dry.
    int fired = 1;
    for (int i = 0; i < 130; ++i) {
        if (weapon.Update(fire, 0.016f)) {
            ++fired;
        }
    }
    VALGO_CHECK_EQ(weapon.AmmoInMagazine(), 0u);
    VALGO_CHECK_EQ(fired, 5);
    VALGO_CHECK(!weapon.CanFire());
}

VALGO_TEST("gameplay", "weapon_reload_restores_the_magazine") {
    Weapon weapon;
    WeaponConfig config;
    config.reload_seconds = 1.0f;
    config.magazine_size = 10;
    weapon.Configure(config);
    weapon.Reset();

    WeaponInput fire;
    fire.fire = true;
    for (int i = 0; i < 30; ++i) {
        weapon.Update(fire, 0.05f);
    }
    VALGO_CHECK(weapon.AmmoInMagazine() < 10u);

    WeaponInput reload;
    reload.reload = true;
    weapon.Update(reload, 0.016f);  // Update() reports shots, not reload starts
    VALGO_CHECK(weapon.IsReloading());
    VALGO_CHECK(!weapon.CanFire());

    // Reload progress must be monotonic and complete.
    f32 previous = weapon.ReloadProgress();
    for (int i = 0; i < 80; ++i) {
        weapon.Update(WeaponInput{}, 0.016f);
        VALGO_CHECK(weapon.ReloadProgress() >= previous - 1.0e-4f);
        previous = weapon.ReloadProgress();
    }
    VALGO_CHECK(!weapon.IsReloading());
    VALGO_CHECK_EQ(weapon.AmmoInMagazine(), 10u);
}

VALGO_TEST("gameplay", "weapon_empty_magazine_triggers_an_automatic_reload") {
    Weapon weapon;
    WeaponConfig config;
    config.magazine_size = 2;
    config.reload_seconds = 0.4f;
    weapon.Configure(config);
    weapon.Reset();

    WeaponInput fire;
    fire.fire = true;
    for (int i = 0; i < 40; ++i) {
        weapon.Update(fire, 0.05f);
    }
    VALGO_CHECK_MSG(weapon.IsReloading() || weapon.AmmoInMagazine() > 0u,
                    "empty weapon neither reloaded nor refilled");
    for (int i = 0; i < 60; ++i) {
        weapon.Update(WeaponInput{}, 0.05f);
    }
    VALGO_CHECK_EQ(weapon.AmmoInMagazine(), 2u);
}

VALGO_TEST("gameplay", "weapon_spread_grows_while_moving") {
    Weapon weapon;
    weapon.Configure(WeaponConfig{});
    VALGO_CHECK(weapon.SpreadDegrees(true) > weapon.SpreadDegrees(false));
    VALGO_CHECK(weapon.SpreadDegrees(false) > 0.0f);
}

// ---------------------------------------------------------------------------
// The game module through the engine harness
// ---------------------------------------------------------------------------
VALGO_TEST("gameplay", "fps_game_starts_and_builds_its_environment") {
    EngineHarness harness(320, 180);
    VALGO_REQUIRE(harness.valid());

    FpsGame game;
    game.OnStartup(harness.services());
    VALGO_CHECK_EQ(game.target_count(), static_cast<usize>(6));
    VALGO_CHECK_EQ(game.alive_target_count(), static_cast<usize>(6));
    VALGO_CHECK(game.player().IsAlive());
    VALGO_CHECK(harness.scene().entity_count() > 10u);
    VALGO_CHECK(harness.physics().collider_count() > 10u);

    for (int i = 0; i < 30; ++i) {
        harness.StepFrame(game);
    }
    VALGO_CHECK_EQ(harness.frame_index(), static_cast<u64>(30));
    VALGO_CHECK(harness.renderer().stats().meshes_drawn > 0u);
    game.OnShutdown(harness.services());
}

VALGO_TEST("gameplay", "shooting_a_target_damages_and_destroys_it") {
    EngineHarness harness(320, 180);
    VALGO_REQUIRE(harness.valid());

    FpsGame game;
    game.OnStartup(harness.services());
    // Face the centre target from close range so the hitscan cannot miss.
    game.TeleportPlayer(Vec3{0.0f, 0.0f, -9.0f}, 0.0f);

    PlayerInput input;
    input.fire = true;
    game.SetExternalInput(input);

    for (int i = 0; i < 240 && game.alive_target_count() == 6; ++i) {
        harness.StepFrame(game);
    }
    VALGO_CHECK_MSG(game.shots() > 0u, "the weapon never fired");
    VALGO_CHECK_MSG(game.hits() > 0u, "hitscan never registered a hit");
    VALGO_CHECK_MSG(game.alive_target_count() < 6, "the target survived sustained fire");
    VALGO_CHECK(game.score() > 0u);
}

VALGO_TEST("gameplay", "missing_a_shot_fires_but_does_not_score") {
    EngineHarness harness(320, 180);
    VALGO_REQUIRE(harness.valid());

    FpsGame game;
    game.OnStartup(harness.services());
    // Face the empty wall away from every dummy.
    game.TeleportPlayer(Vec3{12.0f, 0.0f, 0.0f}, -kHalfPi);

    PlayerInput input;
    input.fire = true;
    game.SetExternalInput(input);
    for (int i = 0; i < 90; ++i) {
        harness.StepFrame(game);
    }
    VALGO_CHECK(game.shots() > 0u);
    VALGO_CHECK_EQ(game.hits(), 0u);
    VALGO_CHECK_EQ(game.score(), 0u);
    VALGO_CHECK_EQ(game.alive_target_count(), static_cast<usize>(6));
}

VALGO_TEST("gameplay", "movement_input_moves_the_player_through_the_engine_motor") {
    EngineHarness harness(320, 180);
    VALGO_REQUIRE(harness.valid());

    FpsGame game;
    game.OnStartup(harness.services());
    const Vec3 start = game.player().FeetPosition();

    PlayerInput input;
    input.move.y = 1.0f;
    game.SetExternalInput(input);
    for (int i = 0; i < 90; ++i) {
        harness.StepFrame(game, 1, 1.0f / 60.0f);
    }
    const Vec3 moved = game.player().FeetPosition();
    VALGO_CHECK_MSG(-moved.z > -start.z + 1.0f, "the player did not advance");
    VALGO_CHECK_NEAR(moved.y, 0.0f, 0.05f);
}

VALGO_TEST("gameplay", "scripted_demo_fires_and_hits_deterministically") {
    EngineHarness harness(320, 180);
    VALGO_REQUIRE(harness.valid());

    FpsGameOptions options;
    options.scripted_demo = true;
    options.target_respawn = false;
    FpsGame game(options);
    game.OnStartup(harness.services());

    // Eleven seconds covers the scan/advance/fire phases of the demo script.
    for (int i = 0; i < 660; ++i) {
        harness.StepFrame(game, 1, 1.0f / 60.0f);
    }
    VALGO_CHECK_MSG(game.shots() > 0u, "the scripted demo never fired");
    VALGO_CHECK_MSG(game.hits() > 0u, "the scripted demo never hit a target");
    VALGO_CHECK_MSG(game.alive_target_count() < 6u, "no target was destroyed");
}

VALGO_TEST("gameplay", "training_map_is_playable_and_consistent") {
    EngineHarness harness(320, 180);
    VALGO_REQUIRE(harness.valid());

    const MapBuildResult map =
        BuildTrainingMap(harness.scene(), harness.physics(), harness.assets(), harness.assets().GetPrimitive("box"));
    VALGO_CHECK_EQ(map.target_positions.size(), static_cast<usize>(6));
    VALGO_CHECK(harness.physics().collider_count() >= 20u);
    VALGO_CHECK_NEAR(map.player_spawn.y, 0.0f, 1.0e-4f);

    // The spawn point must be inside the arena and free of geometry.
    u32 hit_user_data = kInvalidIndex;
    VALGO_CHECK_MSG(!harness.physics().OverlapAabb(
                        Aabb::FromCenterExtents(map.player_spawn + Vec3{0.0f, 0.9f, 0.0f},
                                                Vec3{0.3f, 0.85f, 0.3f}),
                        hit_user_data, physics::kLayerWorld),
                    "the player spawns inside the map geometry");

    // Every dummy stands on solid ground (a downward ray must hit the floor).
    for (const Vec3& target : map.target_positions) {
        const RayHit hit = harness.physics().Raycast(Ray{target + Vec3{0.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
                                                     6.0f, physics::kLayerWorld);
        VALGO_CHECK_MSG(hit.hit, "a target is floating without ground beneath it");
    }
}
