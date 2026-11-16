#include "valgo_fps/fps_game.h"

#include <algorithm>
#include <cmath>

#include "valgo/core/log.h"
#include "valgo/render/overlay2d.h"

#include "test_map.h"

namespace valgo_fps {
namespace {

using valgo::Aabb;
using valgo::Mat4;
using valgo::Quat;
using valgo::RayHit;
using valgo::Transform;
using valgo::Vec2;
using valgo::Vec3;
using valgo::Vec4;
using valgo::game::GameFrame;
using valgo::game::GameServices;
using valgo::input::Key;
using valgo::input::MouseButton;
using valgo::physics::kLayerTarget;
using valgo::scene::Entity;
using valgo::scene::MeshRendererComponent;
using valgo::scene::TransformComponent;

constexpr const char* kCategory = "fps";

constexpr u32 kTargetFlag = 0x80000000u;
constexpr u32 kHeadFlag = 0x40000000u;
constexpr u32 kTargetIndexMask = 0x0000FFFFu;

constexpr f32 kTargetMaxHealth = 100.0f;

// Palette
constexpr Vec3 kTargetBodyColor{0.82f, 0.31f, 0.12f};
constexpr Vec3 kTargetHeadColor{0.92f, 0.52f, 0.18f};
constexpr Vec3 kTargetBaseColor{0.20f, 0.20f, 0.21f};
constexpr Vec3 kGunMetal{0.16f, 0.17f, 0.185f};
constexpr Vec3 kGunMetalLight{0.23f, 0.24f, 0.25f};
constexpr Vec3 kPolymer{0.14f, 0.14f, 0.13f};

Entity CreateBoxEntity(valgo::scene::Scene& scene, const valgo::render::MeshGpuData* mesh, const Vec3& position,
                       const Vec3& scale, const valgo::render::Material& material, const Quat& rotation = {}) {
    Entity entity = scene.CreateEntity("prop");
    TransformComponent* transform = scene.GetComponent<TransformComponent>(entity);
    transform->local.position = position;
    transform->local.scale = scale;
    transform->local.rotation = rotation;
    MeshRendererComponent renderer;
    renderer.mesh = mesh;
    renderer.material = material;
    scene.AddComponent<MeshRendererComponent>(entity, renderer);
    return entity;
}

}  // namespace

FpsGame::FpsGame(FpsGameOptions options) : options_(options) {}

void FpsGame::OnStartup(GameServices& services) {
    VALGO_ASSERT(services.renderer != nullptr, "renderer service missing");
    VALGO_ASSERT(services.physics != nullptr, "physics service missing");

    // --- Engine rendering configuration ----------------------------------
    valgo::render::RenderSettings& settings = services.renderer->settings();
    // Colours are authored in sRGB. Lighting is done in linear space, so the
    // light intensity is a linear multiplier (~0.35 keeps daylight at the level
    // a display-space author would expect from an 0.35 sRGB tint).
    settings.ambient_sky = Vec3{0.30f, 0.35f, 0.45f};
    settings.ambient_ground = Vec3{0.13f, 0.12f, 0.11f};
    settings.fog_color = Vec3{0.40f, 0.46f, 0.55f};
    settings.sky_zenith = Vec3{0.20f, 0.34f, 0.58f};
    settings.sky_horizon = Vec3{0.58f, 0.67f, 0.78f};
    settings.fog_start = 70.0f;
    settings.fog_end = 240.0f;
    settings.enable_fog = true;
    settings.default_light_intensity = 0.55f;

    // --- Player + weapon --------------------------------------------------
    player_.Initialize(PlayerTuning{});

    WeaponConfig weapon_config;
    weapon_.Configure(weapon_config);
    total_shots_fired_ = 0;

    BuildEnvironment(services);
    BuildViewModel(services);

    valgo::LogInfo(kCategory, "range ready: {} training targets, {} engine colliders", targets_.size(),
                   services.physics->collider_count());

    if (services.window != nullptr && !services.window->IsHeadless()) {
        services.window->SetCursorVisible(false);
        services.window->SetRelativeMouseMode(true);
        services.window->SetTitle("Project Valgo | Range Alpha");
    }
}

void FpsGame::BuildEnvironment(GameServices& services) {
    valgo::scene::Scene& scene = *services.scene;
    valgo::physics::PhysicsWorld& physics = *services.physics;
    valgo::assets::AssetManager& assets = *services.assets;

    // Use the imported crate asset when it is available; the primitive remains a
    // guaranteed fallback so the game always runs.
    const valgo::render::MeshGpuData* crate_mesh =
        assets.LoadObjMesh("crate", "games/fps/assets/meshes/crate.obj");
    if (crate_mesh != nullptr) {
        valgo::LogInfo(kCategory, "imported crate asset ({} triangles)", crate_mesh->index_count / 3);
    } else {
        valgo::LogWarn(kCategory, "crate asset missing; using procedural geometry");
        crate_mesh = assets.GetPrimitive("box");
    }

    const MapBuildResult map = BuildTrainingMap(scene, physics, assets, crate_mesh);

    // Directional light entity (engine scene component, no game logic).
    Entity light_entity = scene.CreateEntity("sun");
    valgo::scene::DirectionalLightComponent light;
    light.direction = map.light_direction;
    light.color = Vec3{1.0f, 0.94f, 0.85f};
    light.intensity = 0.55f;
    scene.AddComponent<valgo::scene::DirectionalLightComponent>(light_entity, light);

    // Spawn the player in the shooting lane.
    player_.Spawn(map.player_spawn, map.player_yaw);

    // Targets are game entities built from engine primitives.
    for (const Vec3& position : map.target_positions) {
        SpawnTarget(position, services);
    }
}

void FpsGame::SpawnTarget(const Vec3& feet_position, GameServices& services) {
    valgo::scene::Scene& scene = *services.scene;
    valgo::assets::AssetManager& assets = *services.assets;
    valgo::physics::PhysicsWorld& physics = *services.physics;

    const valgo::render::MeshGpuData* box = assets.GetPrimitive("box");
    if (box == nullptr) {
        return;
    }

    Target target;
    target.feet_position = feet_position;
    target.health = kTargetMaxHealth;
    target.alive = true;
    target.respawn_timer = 0.0f;

    const u32 index = static_cast<u32>(targets_.size());
    if (index > kTargetIndexMask) {
        valgo::LogError(kCategory, "too many targets for the user_data encoding");
        return;
    }

    // Base, torso and head are separate entities sharing the same collider owner.
    target.base = CreateBoxEntity(scene, box, feet_position + Vec3{0.0f, 0.05f, 0.0f},
                                  Vec3{0.55f, 0.10f, 0.55f},
                                  valgo::render::Material::MakeSolid(kTargetBaseColor));
    // Torso spans 0.10 .. 1.60 m, head 1.60 .. 1.90 m: a silhouette at human scale.
    target.body = CreateBoxEntity(scene, box, feet_position + Vec3{0.0f, 0.85f, 0.0f},
                                  Vec3{0.62f, 1.50f, 0.28f},
                                  valgo::render::Material::MakeSolid(kTargetBodyColor));
    target.head = CreateBoxEntity(scene, box, feet_position + Vec3{0.0f, 1.75f, 0.0f},
                                  Vec3{0.28f, 0.30f, 0.28f},
                                  valgo::render::Material::MakeSolid(kTargetHeadColor));

    // Colliders: torso and head are separate physics volumes so headshots are a
    // physics-level fact, not a screen-space guess.
    const Aabb body_bounds = Aabb::FromCenterExtents(feet_position + Vec3{0.0f, 0.85f, 0.0f},
                                                    Vec3{0.31f, 0.75f, 0.14f});
    const Aabb head_bounds = Aabb::FromCenterExtents(feet_position + Vec3{0.0f, 1.75f, 0.0f},
                                                    Vec3{0.14f, 0.15f, 0.14f});
    target.body_collider = physics.AddStaticBox(body_bounds, kTargetFlag | index, kLayerTarget);
    target.head_collider = physics.AddStaticBox(head_bounds, kTargetFlag | kHeadFlag | index, kLayerTarget);

    targets_.push_back(target);
}

void FpsGame::BuildViewModel(GameServices& services) {
    valgo::scene::Scene& scene = *services.scene;
    valgo::assets::AssetManager& assets = *services.assets;
    const valgo::render::MeshGpuData* box = assets.GetPrimitive("box");
    const valgo::render::MeshGpuData* quad = assets.GetPrimitive("quad");
    if (box == nullptr || quad == nullptr) {
        return;
    }

    view_model_body_ = CreateBoxEntity(scene, box, Vec3{}, Vec3{0.050f, 0.062f, 0.24f},
                                       valgo::render::Material::MakeSolid(kGunMetal));
    view_model_handguard_ = CreateBoxEntity(scene, box, Vec3{}, Vec3{0.032f, 0.034f, 0.22f},
                                            valgo::render::Material::MakeSolid(kGunMetalLight));
    view_model_magazine_ = CreateBoxEntity(scene, box, Vec3{}, Vec3{0.034f, 0.105f, 0.050f},
                                           valgo::render::Material::MakeSolid(kPolymer));
    view_model_sight_ = CreateBoxEntity(scene, box, Vec3{}, Vec3{0.016f, 0.016f, 0.055f},
                                        valgo::render::Material::MakeSolid(kGunMetalLight));

    muzzle_flash_ = CreateBoxEntity(scene, quad, Vec3{}, Vec3{0.09f, 0.09f, 0.09f},
                                    valgo::render::Material::MakeUnlit(Vec3{1.0f, 0.72f, 0.25f}, 2.5f));
}

void FpsGame::OnShutdown(GameServices& services) {
    if (services.window != nullptr && !services.window->IsHeadless()) {
        services.window->SetRelativeMouseMode(false);
        services.window->SetCursorVisible(true);
    }
    valgo::LogInfo(kCategory, "session ended: {} shots fired, {} hits, {} targets down, {}s simulated",
                   weapon_.ShotsFired(), shots_hit_, score_, elapsed_seconds_);
}

PlayerInput FpsGame::SampleDeviceInput(const GameServices& services) const {
    PlayerInput input;
    const valgo::input::InputState* state = services.input;
    if (state == nullptr) {
        return input;
    }

    if (state->IsKeyDown(Key::W)) input.move.y += 1.0f;
    if (state->IsKeyDown(Key::S)) input.move.y -= 1.0f;
    if (state->IsKeyDown(Key::D)) input.move.x += 1.0f;
    if (state->IsKeyDown(Key::A)) input.move.x -= 1.0f;

    input.look = Vec2{state->mouse_delta_x(), state->mouse_delta_y()};
    input.jump = state->WasKeyPressed(Key::Space);
    input.sprint = state->IsKeyDown(Key::LeftShift);
    input.crouch = state->IsKeyDown(Key::LeftControl) || state->IsKeyDown(Key::C);
    input.lean_left = state->IsKeyDown(Key::Q);
    input.lean_right = state->IsKeyDown(Key::E);
    input.fire = state->IsMouseButtonDown(MouseButton::Left);
    input.reload = state->WasKeyPressed(Key::R);
    return input;
}

Vec2 FpsGame::ComputeAimDelta(const Vec3& aim_point) const {
    const Vec3 eye = player_.EyePosition();
    const Vec3 to_target = aim_point - eye;
    if (valgo::LengthSquared(to_target) < 0.25f) {
        return Vec2{};
    }
    const valgo::SphericalAngles desired = valgo::AnglesFromDirection(to_target);
    const f32 yaw_error = valgo::WrapAngle(desired.yaw - player_.yaw());
    const f32 pitch_error = desired.pitch - player_.pitch();
    // Convert an angular error into the mouse delta the controller expects.
    const f32 radians_per_pixel = player_.tuning().mouse_sensitivity * kDegToRad;
    Vec2 look;
    look.x = Clamp(-yaw_error / radians_per_pixel * 0.35f, -24.0f, 24.0f);
    look.y = Clamp(-pitch_error / radians_per_pixel * 0.35f, -24.0f, 24.0f);
    return look;
}

bool FpsGame::FindNearestTarget(Vec3& out_aim_point) const {
    const Vec3 eye = player_.EyePosition();
    const Vec3 forward = player_.AimDirection();
    f32 best_distance = 1.0e9f;
    bool found = false;
    for (const Target& target : targets_) {
        if (!target.alive) {
            continue;
        }
        const Vec3 aim_point = target.feet_position + Vec3{0.0f, 1.25f, 0.0f};
        const Vec3 to_target = aim_point - eye;
        const f32 distance = Length(to_target);
        if (distance < 0.5f || distance > 60.0f) {
            continue;
        }
        if (Dot(Normalize(to_target), forward) < 0.55f) {
            continue;  // outside the demo's field of view
        }
        if (distance < best_distance) {
            best_distance = distance;
            out_aim_point = aim_point;
            found = true;
        }
    }
    return found;
}

PlayerInput FpsGame::SampleScriptedInput(f32 delta_seconds) const {
    PlayerInput input;
    const f32 t = static_cast<f32>(std::fmod(elapsed_seconds_, 16.0));
    auto in_range = [t](f32 start, f32 end) { return t >= start && t < end; };

    // Rate-based look input, expressed as pixels for this step.
    const f32 look_rate = 110.0f;  // pixels per second (~11.5 deg/s)

    // --- Phase 1: scan the range -----------------------------------------
    if (in_range(0.0f, 1.2f)) {
        input.look.x = look_rate * delta_seconds;
    } else if (in_range(1.2f, 2.2f)) {
        input.look.x = -look_rate * delta_seconds;
    }

    // --- Phase 2: advance and clear the low wall -------------------------
    if (in_range(2.2f, 5.0f)) {
        input.move.y = 1.0f;
    }
    if (in_range(3.5f, 3.56f)) {
        input.jump = true;
    }
    if (in_range(4.0f, 5.0f)) {
        input.sprint = true;
    }

    // --- Phase 3: fire from cover ---------------------------------------
    if (in_range(5.0f, 6.8f)) {
        input.crouch = true;
        input.fire = t > 5.2f;
        input.move.x = in_range(6.0f, 6.5f) ? 0.6f : 0.0f;
    }

    // --- Phase 4: standing burst ----------------------------------------
    if (in_range(6.8f, 8.8f)) {
        input.fire = true;
    }

    // --- Phase 5: reload -------------------------------------------------
    if (in_range(8.8f, 8.9f)) {
        input.reload = true;
    }
    if (in_range(9.2f, 10.4f)) {
        input.look.x = look_rate * delta_seconds * 0.6f;
        input.move.x = 0.5f;
    }

    // --- Phase 6: leaning shots -----------------------------------------
    if (in_range(10.4f, 12.2f)) {
        input.lean_right = true;
        input.fire = true;
    }

    // --- Phase 7: reposition ---------------------------------------------
    if (in_range(12.2f, 13.6f)) {
        input.move.y = -0.8f;
        input.look.x = look_rate * delta_seconds * 0.8f;
    }
    if (in_range(13.6f, 15.0f)) {
        input.move.x = -0.8f;
        input.move.y = 0.4f;
    }

    // --- Demo aim assist -------------------------------------------------
    // Runs only while the demo intends to shoot. This keeps the capture
    // deterministic while still exercising real raycast hit detection.
    const bool aiming = (in_range(5.2f, 8.8f) || in_range(10.6f, 12.2f));
    if (aiming) {
        Vec3 aim_point{};
        if (FindNearestTarget(aim_point)) {
            const Vec2 aim_delta = ComputeAimDelta(aim_point);
            input.look = Vec2{input.look.x + aim_delta.x, input.look.y + aim_delta.y};
        }
    }

    return input;
}

PlayerInput FpsGame::SampleInput(const GameServices& services, f32 delta_seconds) const {
    if (has_external_input_) {
        return external_input_;
    }
    if (options_.scripted_demo) {
        return SampleScriptedInput(delta_seconds);
    }
    return SampleDeviceInput(services);
}

void FpsGame::SetExternalInput(const PlayerInput& input) {
    external_input_ = input;
    has_external_input_ = true;
}

void FpsGame::ClearExternalInput() { has_external_input_ = false; }

void FpsGame::TeleportPlayer(const Vec3& feet_position, f32 yaw_radians) {
    player_.Spawn(feet_position, yaw_radians);
}

void FpsGame::OnUpdate(const GameFrame& frame, GameServices& services) {
    if (services.scene == nullptr || services.physics == nullptr) {
        return;
    }

    const f32 fixed_step = frame.fixed_step_seconds > 0.0f ? frame.fixed_step_seconds : 1.0f / 60.0f;
    const u32 steps = std::max(frame.fixed_steps, 1u);
    for (u32 step = 0; step < steps; ++step) {
        StepSimulation(fixed_step, services);
    }

    if (services.camera != nullptr) {
        player_.ApplyCamera(*services.camera);
    }
    UpdateViewModel(services);
    UpdateDebugOverlay(services);

    if (valgo::Logger::Get().level() <= valgo::LogLevel::Debug && services.camera != nullptr &&
        (frame.frame_index % 15u) == 0u) {
        const Vec3 eye = services.camera->position();
        valgo::LogDebug(kCategory, "cam frame={} eye=({},{},{}) pitch={} yaw={} roll={}", frame.frame_index,
                        eye.x, eye.y, eye.z, valgo::Degrees(services.camera->pitch()),
                        valgo::Degrees(services.camera->yaw()), valgo::Degrees(services.camera->roll()));
    }
    if (options_.show_hud) {
        UpdateHud(services);
    }
}

void FpsGame::StepSimulation(f32 delta_seconds, GameServices& services) {
    const PlayerInput input = SampleInput(services, delta_seconds);

    aim_was_down_ = input.fire;

    player_.Update(input, delta_seconds, *services.physics, player_.IsAlive());

    WeaponInput weapon_input;
    weapon_input.fire = input.fire;
    weapon_input.reload = input.reload;
    if (weapon_.Update(weapon_input, delta_seconds)) {
        FireWeapon(services);
    }

    UpdateTargets(delta_seconds, services);

    weapon_kick_ = std::max(weapon_kick_ - delta_seconds * 7.0f, 0.0f);
    hit_marker_ = std::max(hit_marker_ - delta_seconds, 0.0f);
    damage_flash_ = std::max(damage_flash_ - delta_seconds * 2.0f, 0.0f);

    // View model sway follows the look delta.
    const f32 sway_target_x = valgo::Clamp(-input.look.x * 0.00035f, -0.022f, 0.022f);
    const f32 sway_target_y = valgo::Clamp(-input.look.y * 0.00035f, -0.018f, 0.018f);
    sway_x_ = valgo::Lerp(sway_x_, sway_target_x, valgo::Saturate(delta_seconds * 9.0f));
    sway_y_ = valgo::Lerp(sway_y_, sway_target_y, valgo::Saturate(delta_seconds * 9.0f));

    for (auto it = impacts_.begin(); it != impacts_.end();) {
        it->life -= delta_seconds;
        if (it->life <= 0.0f) {
            it = impacts_.erase(it);
        } else {
            ++it;
        }
    }

    // One diagnostic line every two simulated seconds: invaluable when tuning
    // movement and collision, and cheap enough to keep in debug builds.
    const auto previous_tick = static_cast<i32>((elapsed_seconds_) * 2.0);
    const auto current_tick = static_cast<i32>((elapsed_seconds_ + delta_seconds) * 2.0);
    if (current_tick != previous_tick) {
        const Vec3 position = player_.FeetPosition();
        valgo::LogDebug(kCategory, "t={} pos=({},{},{}) vel=({},{},{}) grounded={} crouch={}",
                        elapsed_seconds_ + delta_seconds, position.x, position.y, position.z,
                        player_.Motor().velocity().x, player_.Motor().velocity().y,
                        player_.Motor().velocity().z, player_.Grounded(), player_.IsCrouching());
    }

    elapsed_seconds_ += delta_seconds;
}

void FpsGame::FireWeapon(GameServices& services) {
    if (services.physics == nullptr) {
        return;
    }

    Ray ray = player_.AimRay();
    const bool moving = player_.HorizontalSpeed() > 1.2f;
    const f32 spread_degrees = weapon_.SpreadDegrees(moving);

    if (spread_degrees > 0.0f) {
        const Vec3 right = valgo::Normalize(valgo::Cross(ray.direction, Vec3{0.0f, 1.0f, 0.0f}));
        const Vec3 up = valgo::Cross(right, ray.direction);
        const f32 angle = rng_.NextFloat(0.0f, valgo::kTwoPi);
        const f32 radius = std::sqrt(rng_.NextFloat()) * std::tan(spread_degrees * valgo::kDegToRad);
        ray.direction = valgo::Normalize(ray.direction + right * (std::cos(angle) * radius) +
                                         up * (std::sin(angle) * radius));
    }

    const RayHit hit = services.physics->Raycast(ray, weapon_.config().range_metres,
                                                 valgo::physics::kAllLayers);
    ++total_shots_fired_;

    // Recoil: a vertical kick plus a random horizontal component.
    player_.AddViewKick(weapon_.config().recoil_pitch_degrees * rng_.NextFloat(0.85f, 1.2f),
                        weapon_.config().recoil_yaw_degrees * rng_.NextFloat(-1.0f, 1.0f));
    weapon_kick_ = 1.0f;
    shot_pitch_kick_ += 0.02f;

    if (!hit.hit) {
        return;
    }

    if ((hit.user_data & kTargetFlag) != 0u) {
        ApplyHit(hit.user_data, hit, services);
        return;
    }

    Impact impact;
    impact.position = hit.point;
    impact.normal = hit.normal;
    impact.life = 2.5f;
    impacts_.push_back(impact);
}

void FpsGame::ApplyHit(u32 user_data, const RayHit& hit, GameServices& services) {
    VALGO_UNUSED(hit);
    const u32 index = user_data & kTargetIndexMask;
    if (index >= targets_.size()) {
        return;
    }
    Target& target = targets_[index];
    if (!target.alive) {
        return;
    }

    const bool headshot = (user_data & kHeadFlag) != 0u;
    const f32 damage = headshot ? weapon_.config().headshot_damage : weapon_.config().damage;
    target.health -= damage;
    target.hit_flash = 0.12f;
    hit_marker_ = 0.18f;
    ++shots_hit_;

    if (target.health <= 0.0f) {
        target.alive = false;
        target.health = 0.0f;
        target.respawn_timer = options_.target_respawn_seconds;
        ++score_;

        // Remove the physics volumes so dead dummies cannot be shot again.
        if (services.physics != nullptr) {
            services.physics->RemoveCollider(target.body_collider);
            services.physics->RemoveCollider(target.head_collider);
        }
        target.body_collider = 0;
        target.head_collider = 0;

        if (valgo::scene::MeshRendererComponent* body =
                services.scene->GetComponent<MeshRendererComponent>(target.body)) {
            body->material.base_color = Vec3{0.16f, 0.15f, 0.14f};
            body->material.emissive = 0.0f;
        }
        if (valgo::scene::MeshRendererComponent* head =
                services.scene->GetComponent<MeshRendererComponent>(target.head)) {
            head->material.base_color = Vec3{0.18f, 0.16f, 0.15f};
            head->material.emissive = 0.0f;
        }
        if (valgo::scene::MeshRendererComponent* body =
                services.scene->GetComponent<MeshRendererComponent>(target.body)) {
            body->visible = false;
        }
        if (valgo::scene::MeshRendererComponent* head =
                services.scene->GetComponent<MeshRendererComponent>(target.head)) {
            head->visible = false;
        }
        valgo::LogInfo(kCategory, "target {} down ({})", index, headshot ? "headshot" : "body");
    }
}

void FpsGame::ReviveTarget(Target& target, GameServices& services) {
    if (services.physics == nullptr || services.scene == nullptr) {
        return;
    }
    target.alive = true;
    target.health = kTargetMaxHealth;
    target.hit_flash = 0.0f;

    const u32 index = static_cast<u32>(&target - targets_.data());
    const Aabb body_bounds = Aabb::FromCenterExtents(target.feet_position + Vec3{0.0f, 0.85f, 0.0f},
                                                    Vec3{0.31f, 0.75f, 0.14f});
    const Aabb head_bounds = Aabb::FromCenterExtents(target.feet_position + Vec3{0.0f, 1.75f, 0.0f},
                                                    Vec3{0.14f, 0.15f, 0.14f});
    target.body_collider = services.physics->AddStaticBox(body_bounds, kTargetFlag | index, kLayerTarget);
    target.head_collider =
        services.physics->AddStaticBox(head_bounds, kTargetFlag | kHeadFlag | index, kLayerTarget);

    if (valgo::scene::MeshRendererComponent* body =
            services.scene->GetComponent<MeshRendererComponent>(target.body)) {
        body->visible = true;
        body->material.base_color = kTargetBodyColor;
        body->material.emissive = 0.0f;
    }
    if (valgo::scene::MeshRendererComponent* head =
            services.scene->GetComponent<MeshRendererComponent>(target.head)) {
        head->visible = true;
        head->material.base_color = kTargetHeadColor;
        head->material.emissive = 0.0f;
    }
}

void FpsGame::UpdateTargets(f32 delta_seconds, GameServices& services) {
    for (Target& target : targets_) {
        if (!target.alive) {
            if (options_.target_respawn) {
                target.respawn_timer -= delta_seconds;
                if (target.respawn_timer <= 0.0f) {
                    ReviveTarget(target, services);
                }
            }
            continue;
        }

        if (target.hit_flash > 0.0f) {
            target.hit_flash = std::max(target.hit_flash - delta_seconds, 0.0f);
        }
        const f32 flash = target.hit_flash > 0.0f ? (target.hit_flash / 0.12f) : 0.0f;
        if (valgo::scene::MeshRendererComponent* body =
                services.scene->GetComponent<MeshRendererComponent>(target.body)) {
            body->material.emissive = flash * 2.4f;
        }
        if (valgo::scene::MeshRendererComponent* head =
                services.scene->GetComponent<MeshRendererComponent>(target.head)) {
            head->material.emissive = flash * 2.4f;
        }
    }
}

void FpsGame::UpdateViewModel(GameServices& services) {
    if (services.camera == nullptr || services.scene == nullptr) {
        return;
    }
    const valgo::scene::Scene& scene = *services.scene;
    const valgo::assets::AssetManager& assets = *services.assets;
    VALGO_UNUSED(scene);
    VALGO_UNUSED(assets);

    const Transform camera = services.camera->GetTransform();
    const Vec3 right = services.camera->Right();
    const Vec3 up = services.camera->Up();
    const Vec3 forward = services.camera->Forward();

    const f32 speed = player_.HorizontalSpeed();
    const f32 bob = std::sin(static_cast<f32>(elapsed_seconds_) * 9.2f) * 0.0075f *
                    valgo::Saturate(speed / player_.tuning().walk_speed);
    const f32 bob_side = std::cos(static_cast<f32>(elapsed_seconds_) * 4.6f) * 0.010f *
                         valgo::Saturate(speed / player_.tuning().walk_speed);

    const f32 kick = weapon_kick_ * -0.045f;
    const f32 reload_dip = weapon_.IsReloading() ? std::sin(weapon_.ReloadProgress() * valgo::kPi) * 0.09f : 0.0f;

    const Vec3 origin = camera.position + forward * (0.72f + kick) + right * (0.185f + sway_x_ + bob_side) +
                        up * (-0.185f + sway_y_ + bob - reload_dip);

    auto place = [&](Entity entity, const Vec3& local_offset, const Vec3& scale, const Quat& local_rotation) {
        if (!entity.IsValid()) {
            return;
        }
        TransformComponent* transform = services.scene->GetComponent<TransformComponent>(entity);
        MeshRendererComponent* renderer = services.scene->GetComponent<MeshRendererComponent>(entity);
        if (transform == nullptr) {
            return;
        }
        transform->local.position = origin + camera.rotation * local_offset;
        transform->local.rotation = (camera.rotation * local_rotation).Normalized();
        transform->local.scale = scale;
        if (renderer != nullptr) {
            renderer->visible = true;
        }
    };

    // Assault-rifle silhouette: receiver, handguard/barrel, magazine and sight.
    place(view_model_body_, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.050f, 0.062f, 0.24f},
          Quat::FromEuler(0.0f, -0.035f, 0.0f));
    place(view_model_handguard_, Vec3{0.0f, 0.006f, -0.24f}, Vec3{0.032f, 0.034f, 0.22f},
          Quat::FromEuler(0.0f, -0.035f, 0.0f));
    place(view_model_magazine_, Vec3{0.0f, -0.075f, 0.035f}, Vec3{0.034f, 0.105f, 0.050f},
          Quat::FromEuler(0.28f, -0.035f, 0.0f));
    place(view_model_sight_, Vec3{0.0f, 0.048f, -0.05f}, Vec3{0.016f, 0.016f, 0.055f},
          Quat::FromEuler(0.0f, -0.035f, 0.0f));

    if (muzzle_flash_.IsValid()) {
        TransformComponent* transform = services.scene->GetComponent<TransformComponent>(muzzle_flash_);
        MeshRendererComponent* renderer = services.scene->GetComponent<MeshRendererComponent>(muzzle_flash_);
        if (transform != nullptr && renderer != nullptr) {
            const bool visible = weapon_.MuzzleFlashTime() > 0.0f;
            renderer->visible = visible;
            if (visible) {
                const f32 flicker = 0.09f + rng_.NextFloat(0.0f, 0.05f);
                transform->local.position = origin + camera.rotation * Vec3{0.0f, 0.012f, -0.50f};
                transform->local.rotation = camera.rotation;
                transform->local.scale = Vec3{flicker, flicker, flicker};
            }
        }
    }

    VALGO_UNUSED(shot_pitch_kick_);
}

void FpsGame::UpdateDebugOverlay(const GameServices& services) {
    valgo::render::Renderer& renderer = *services.renderer;

    // Impact markers always visible: enemy feedback reads better with them.
    for (const Impact& impact : impacts_) {
        const Vec3 tangent = valgo::AnyPerpendicular(impact.normal) * 0.07f;
        const Vec3 bitangent = valgo::Cross(impact.normal, tangent);
        const Vec4 color{1.0f, 0.78f, 0.25f, 1.0f};
        renderer.DrawLine(impact.position - tangent - bitangent, impact.position + tangent + bitangent, color);
        renderer.DrawLine(impact.position - tangent + bitangent, impact.position + tangent - bitangent, color);
    }

    if (!options_.show_debug_overlay) {
        return;
    }

    renderer.DrawAabb(player_.Motor().bounds(), {0.2f, 0.9f, 1.0f, 1.0f});
    for (const Target& target : targets_) {
        if (!target.alive) {
            continue;
        }
        renderer.DrawAabb(Aabb::FromCenterExtents(target.feet_position + Vec3{0.0f, 0.95f, 0.0f},
                                                 Vec3{0.32f, 0.95f, 0.2f}),
                          {1.0f, 0.4f, 0.2f, 1.0f});
    }
    const Ray ray = player_.AimRay();
    renderer.DrawLine(ray.origin, ray.origin + ray.direction * 40.0f, {1.0f, 1.0f, 0.2f, 1.0f});
}

void FpsGame::UpdateHud(const GameServices& services) {
    valgo::render::Overlay2D& hud = services.renderer->overlay();
    const f32 width = static_cast<f32>(services.viewport_width);
    const f32 height = static_cast<f32>(services.viewport_height);
    const Vec4 white{0.94f, 0.95f, 0.97f, 1.0f};
    const Vec4 dim{0.72f, 0.75f, 0.78f, 1.0f};
    const Vec4 accent{0.96f, 0.72f, 0.24f, 1.0f};
    const Vec4 danger{0.92f, 0.28f, 0.22f, 1.0f};

    // --- Crosshair --------------------------------------------------------
    const f32 center_x = width * 0.5f;
    const f32 center_y = height * 0.5f;
    const f32 moving = valgo::Saturate(player_.HorizontalSpeed() / player_.tuning().sprint_speed);
    const f32 gap = 7.0f + moving * 8.0f + weapon_.SpreadDegrees(moving > 0.2f) * 9.0f;
    const f32 arm = 6.0f;
    hud.DrawRect({center_x - 1.0f, center_y - 1.0f}, {2.0f, 2.0f}, white);
    hud.DrawLine({center_x - gap - arm, center_y}, {center_x - gap, center_y}, 1.6f, white);
    hud.DrawLine({center_x + gap, center_y}, {center_x + gap + arm, center_y}, 1.6f, white);
    hud.DrawLine({center_x, center_y - gap - arm}, {center_x, center_y - gap}, 1.6f, white);
    hud.DrawLine({center_x, center_y + gap}, {center_x, center_y + gap + arm}, 1.6f, white);

    if (hit_marker_ > 0.0f) {
        const f32 alpha = valgo::Saturate(hit_marker_ / 0.18f);
        const Vec4 marker{1.0f, 0.92f, 0.4f, alpha};
        hud.DrawLine({center_x - 11.0f, center_y - 11.0f}, {center_x - 5.0f, center_y - 5.0f}, 2.0f, marker);
        hud.DrawLine({center_x + 11.0f, center_y - 11.0f}, {center_x + 5.0f, center_y - 5.0f}, 2.0f, marker);
        hud.DrawLine({center_x - 11.0f, center_y + 11.0f}, {center_x - 5.0f, center_y + 5.0f}, 2.0f, marker);
        hud.DrawLine({center_x + 11.0f, center_y + 11.0f}, {center_x + 5.0f, center_y + 5.0f}, 2.0f, marker);
    }

    // --- Health -----------------------------------------------------------
    const f32 health_ratio = valgo::Saturate(player_.Health() / player_.tuning().max_health);
    const Vec2 bar_position{28.0f, height - 62.0f};
    const Vec2 bar_size{240.0f, 14.0f};
    hud.DrawRect({bar_position.x - 2.0f, bar_position.y - 2.0f}, {bar_size.x + 4.0f, bar_size.y + 4.0f},
                 {0.05f, 0.06f, 0.07f, 0.82f});
    hud.DrawRect(bar_position, {bar_size.x * health_ratio, bar_size.y},
                 health_ratio > 0.35f ? Vec4{0.35f, 0.82f, 0.45f, 0.95f} : danger);
    hud.DrawText({bar_position.x, bar_position.y - 18.0f}, "HEALTH", dim, 1.5f);
    hud.DrawText({bar_position.x + bar_size.x + 12.0f, bar_position.y - 3.0f},
                 valgo::ToString(static_cast<int>(player_.Health())), white, 2.0f);

    // Stance + movement indicator.
    const char* stance = "STAND";
    if (player_.IsCrouching()) {
        stance = "CROUCH";
    } else if (player_.IsSprinting()) {
        stance = "SPRINT";
    } else if (!player_.Grounded()) {
        stance = "AIRBORNE";
    }
    hud.DrawText({bar_position.x, bar_position.y + 24.0f}, stance, player_.IsSprinting() ? accent : dim, 1.5f);
    if (player_.IsLeaning()) {
        hud.DrawText({bar_position.x + 110.0f, bar_position.y + 24.0f},
                     player_.yaw() > 0.0f ? "LEAN L" : "LEAN R", dim, 1.5f);
    }

    // --- Weapon + ammo ----------------------------------------------------
    const Vec2 ammo_origin{width - 250.0f, height - 74.0f};
    hud.DrawText(ammo_origin, weapon_.config().display_name, dim, 1.5f);

    const std::string ammo_text = valgo::Format("{}", weapon_.AmmoInMagazine());
    hud.DrawText({ammo_origin.x, ammo_origin.y + 16.0f}, ammo_text,
                 weapon_.AmmoInMagazine() == 0 ? danger : white, 3.0f);
    const std::string reserve_text = valgo::Format("/ {}", weapon_.MagazineSize());
    hud.DrawText({ammo_origin.x + 62.0f, ammo_origin.y + 34.0f}, reserve_text, dim, 2.0f);

    if (weapon_.IsReloading()) {
        const f32 progress = weapon_.ReloadProgress();
        hud.DrawRect({ammo_origin.x, ammo_origin.y + 76.0f}, {150.0f, 6.0f}, {0.08f, 0.09f, 0.1f, 0.9f});
        hud.DrawRect({ammo_origin.x, ammo_origin.y + 76.0f}, {150.0f * progress, 6.0f}, accent);
        hud.DrawText({ammo_origin.x, ammo_origin.y + 88.0f}, "RELOADING", accent, 1.5f);
    }

    // --- Score ------------------------------------------------------------
    const std::string score_text =
        valgo::Format("TARGETS {}/{}", alive_target_count(), targets_.size());
    hud.DrawText({width - 190.0f, 26.0f}, score_text, white, 1.8f);
    const std::string hits_text = valgo::Format("HITS {}  SHOTS {}", shots_hit_, weapon_.ShotsFired());
    hud.DrawText({width - 190.0f, 48.0f}, hits_text, dim, 1.5f);

    // --- Frame stats ------------------------------------------------------
    const std::string stats = valgo::Format("MESH {} | DRAW {} | TRI {} | LINE {}",
                                            services.renderer->stats().meshes_drawn,
                                            services.renderer->stats().draw_calls,
                                            services.renderer->stats().triangles,
                                            services.renderer->stats().debug_lines);
    hud.DrawText({18.0f, 18.0f}, stats, dim, 1.3f);

    if (options_.scripted_demo) {
        hud.DrawText({18.0f, 36.0f}, "DEMO PLAYBACK", accent, 1.4f);
    }

    // --- Contextual hints -------------------------------------------------
    if (elapsed_seconds_ < 12.0) {
        const f32 alpha = valgo::Saturate(static_cast<f32>((12.0 - elapsed_seconds_) / 3.0));
        hud.DrawTextCentered(center_x, height - 30.0f,
                             "WASD MOVE  SHIFT SPRINT  CTRL CROUCH  Q/E LEAN  SPACE JUMP  LMB FIRE  R RELOAD",
                             {0.85f, 0.87f, 0.9f, alpha * 0.85f}, 1.2f);
    }

    if (damage_flash_ > 0.0f) {
        hud.DrawRectOutline({0.0f, 0.0f}, {width, height}, 12.0f,
                            {0.9f, 0.1f, 0.1f, valgo::Saturate(damage_flash_)});
    }
}

usize FpsGame::alive_target_count() const {
    usize count = 0;
    for (const Target& target : targets_) {
        if (target.alive) {
            ++count;
        }
    }
    return count;
}

}  // namespace valgo_fps
