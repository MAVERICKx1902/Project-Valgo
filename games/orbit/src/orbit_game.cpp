#include "valgo_orbit/orbit_game.h"

#include <algorithm>
#include <cmath>

#include "valgo/core/log.h"

namespace valgo_orbit {
namespace {

using namespace valgo;
using valgo::game::GameFrame;
using valgo::game::GameServices;
using valgo::scene::Entity;
using valgo::scene::MeshRendererComponent;
using valgo::scene::TransformComponent;

constexpr const char* kCategory = "orbit";
constexpr f32 kArenaRadius = 20.0f;
constexpr f32 kOrbPickupRadius = 1.5f;

constexpr Vec3 kFloorColor{0.10f, 0.12f, 0.17f};
constexpr Vec3 kRimColor{0.16f, 0.22f, 0.30f};
constexpr Vec3 kShipColor{0.30f, 0.75f, 0.92f};
constexpr Vec3 kOrbColor{0.98f, 0.78f, 0.22f};
constexpr Vec3 kHazardColor{0.86f, 0.24f, 0.30f};

Entity CreateMeshEntity(valgo::scene::Scene& scene, const valgo::render::MeshGpuData* mesh,
                        const Vec3& position, const Vec3& scale, const valgo::render::Material& material) {
    Entity entity = scene.CreateEntity("orbit.entity");
    TransformComponent* transform = scene.GetComponent<TransformComponent>(entity);
    transform->local.position = position;
    transform->local.scale = scale;
    MeshRendererComponent renderer;
    renderer.mesh = mesh;
    renderer.material = material;
    scene.AddComponent<MeshRendererComponent>(entity, renderer);
    return entity;
}

}  // namespace

OrbitGame::OrbitGame(OrbitGameOptions options) : options_(options) {}

void OrbitGame::OnStartup(GameServices& services) {
    VALGO_ASSERT(services.renderer != nullptr, "renderer service missing");

    valgo::render::RenderSettings& settings = services.renderer->settings();
    settings.ambient_sky = Vec3{0.30f, 0.38f, 0.52f};
    settings.ambient_ground = Vec3{0.10f, 0.12f, 0.18f};
    settings.fog_color = Vec3{0.12f, 0.15f, 0.22f};
    settings.fog_start = 30.0f;
    settings.fog_end = 90.0f;
    settings.default_light_direction = Vec3{-0.5f, -1.0f, -0.6f};
    settings.default_light_intensity = 0.45f;

    BuildArena(services);

    ship_position_ = Vec3{0.0f, 0.45f, 6.0f};
    ship_entity_ = CreateMeshEntity(*services.scene, services.assets->GetPrimitive("box"), ship_position_,
                                    Vec3{1.4f, 0.35f, 1.8f},
                                    valgo::render::Material::MakeSolid(kShipColor));
    cursor_entity_ = CreateMeshEntity(*services.scene, services.assets->GetPrimitive("quad"),
                                      Vec3{0.0f, 1.2f, 0.0f}, Vec3{1.6f, 1.6f, 1.6f},
                                      valgo::render::Material::MakeUnlit({0.6f, 0.95f, 1.0f}, 1.6f));

    orbs_.resize(10);
    for (u32 i = 0; i < orbs_.size(); ++i) {
        SpawnOrb(i, services);
    }

    valgo::LogInfo(kCategory, "orbit arena ready: {} orbs, {} hazards", orbs_.size(), hazards_.size());
}

void OrbitGame::BuildArena(GameServices& services) {
    valgo::scene::Scene& scene = *services.scene;
    const valgo::render::MeshGpuData* cylinder = services.assets->GetPrimitive("cylinder");

    // Arena floor: a wide, flat cylinder.
    CreateMeshEntity(scene, cylinder, Vec3{0.0f, -0.2f, 0.0f}, Vec3{kArenaRadius * 2.2f, 0.4f, kArenaRadius * 2.2f},
                     valgo::render::Material::MakeSolid(kFloorColor));

    // Rim pillars double as the physical boundary and as visual landmarks.
    const valgo::render::MeshGpuData* box = services.assets->GetPrimitive("box");
    for (u32 i = 0; i < 16; ++i) {
        const f32 angle = valgo::kTwoPi * static_cast<f32>(i) / 16.0f;
        const Vec3 position{std::cos(angle) * (kArenaRadius + 0.8f), 0.8f, std::sin(angle) * (kArenaRadius + 0.8f)};
        CreateMeshEntity(scene, box, position, Vec3{1.0f, 1.6f, 1.0f},
                         valgo::render::Material::MakeSolid(kRimColor));
        services.physics->AddStaticBox(Aabb::FromCenterExtents(position, Vec3{0.6f, 0.8f, 0.6f}), i,
                                       valgo::physics::kLayerWorld);
    }

    // Four orbiting hazards.
    for (u32 i = 0; i < 4; ++i) {
        Hazard hazard;
        hazard.angle = valgo::kTwoPi * static_cast<f32>(i) / 4.0f;
        hazard.radius = 6.5f + static_cast<f32>(i) * 1.6f;
        hazard.speed = 0.55f + static_cast<f32>(i) * 0.12f;
        hazard.height = 1.1f;
        hazard.entity = CreateMeshEntity(scene, box, Vec3{0.0f, hazard.height, 0.0f}, Vec3{1.1f, 0.5f, 3.6f},
                                         valgo::render::Material::MakeSolid(kHazardColor));
        hazards_.push_back(hazard);
    }

    // A central beacon for orientation.
    CreateMeshEntity(scene, services.assets->GetPrimitive("cylinder"), Vec3{0.0f, 1.5f, 0.0f},
                     Vec3{2.2f, 3.0f, 2.2f}, valgo::render::Material::MakeUnlit({0.25f, 0.6f, 0.9f}, 0.8f));

    valgo::scene::Entity light_entity = scene.CreateEntity("orbit.sun");
    valgo::scene::DirectionalLightComponent light;
    light.direction = Vec3{-0.5f, -1.0f, -0.6f};
    light.color = Vec3{0.85f, 0.9f, 1.0f};
    light.intensity = 0.45f;
    scene.AddComponent<valgo::scene::DirectionalLightComponent>(light_entity, light);
}

void OrbitGame::SpawnOrb(u32 index, GameServices& services) {
    if (index >= orbs_.size()) {
        return;
    }
    Orb& orb = orbs_[index];
    const f32 angle = rng_.NextFloat(0.0f, valgo::kTwoPi);
    const f32 radius = rng_.NextFloat(4.0f, kArenaRadius - 2.0f);
    orb.base_position = Vec3{std::cos(angle) * radius, 1.0f, std::sin(angle) * radius};
    orb.phase = rng_.NextFloat(0.0f, valgo::kTwoPi);
    orb.active = true;
    orb.respawn_timer = 0.0f;

    if (!orb.entity.IsValid()) {
        orb.entity = CreateMeshEntity(*services.scene, services.assets->GetPrimitive("sphere"),
                                      orb.base_position, Vec3{0.7f, 0.7f, 0.7f},
                                      valgo::render::Material::MakeUnlit(kOrbColor, 1.4f));
        orb.collider_id = services.physics->AddStaticBox(
            Aabb::FromCenterExtents(orb.base_position, Vec3{0.5f, 0.8f, 0.5f}), index,
            valgo::physics::kLayerProp);
    } else {
        if (valgo::scene::MeshRendererComponent* renderer =
                services.scene->GetComponent<MeshRendererComponent>(orb.entity)) {
            renderer->visible = true;
        }
        if (valgo::scene::TransformComponent* transform =
                services.scene->GetComponent<TransformComponent>(orb.entity)) {
            transform->local.position = orb.base_position;
        }
        orb.collider_id = services.physics->AddStaticBox(
            Aabb::FromCenterExtents(orb.base_position, Vec3{0.5f, 0.8f, 0.5f}), index,
            valgo::physics::kLayerProp);
    }
}

Vec2 OrbitGame::SampleInput(const GameServices& services) const {
    Vec2 input;
    if (options_.scripted_demo) {
        // Deterministic demo driver: head for the closest active orb. This is
        // the game's own "input source", exactly like the FPS demo script, and
        // it keeps captured runs reproducible.
        f32 best_distance_squared = 0.0f;
        Vec3 target{};
        bool found = false;
        for (const Orb& orb : orbs_) {
            if (!orb.active) {
                continue;
            }
            const Vec3 delta{orb.base_position.x - ship_position_.x, 0.0f,
                             orb.base_position.z - ship_position_.z};
            const f32 distance_squared = LengthSquared(delta);
            if (!found || distance_squared < best_distance_squared) {
                best_distance_squared = distance_squared;
                target = delta;
                found = true;
            }
        }
        if (found && best_distance_squared > 0.01f) {
            const Vec3 direction = Normalize(target);
            // Blend in a repulsion from nearby sweepers, the way a player would.
            Vec3 steer = direction;
            for (const Hazard& hazard : hazards_) {
                const f32 angle = hazard.angle;
                const Vec3 hazard_position{std::cos(angle) * hazard.radius, 0.0f,
                                           std::sin(angle) * hazard.radius};
                const Vec3 away{ship_position_.x - hazard_position.x, 0.0f,
                                ship_position_.z - hazard_position.z};
                const f32 distance_squared = LengthSquared(away);
                if (distance_squared < 16.0f && distance_squared > 1.0e-4f) {
                    steer = steer + Normalize(away) * (1.6f * (1.0f - std::sqrt(distance_squared) / 4.0f));
                }
            }
            steer = Normalize(steer);
            input.x = steer.x;
            input.y = -steer.z;
        }
        return input;
    }
    if (services.input != nullptr) {
        using valgo::input::Key;
        if (services.input->IsKeyDown(Key::W)) input.y += 1.0f;
        if (services.input->IsKeyDown(Key::S)) input.y -= 1.0f;
        if (services.input->IsKeyDown(Key::D)) input.x += 1.0f;
        if (services.input->IsKeyDown(Key::A)) input.x -= 1.0f;
    }
    return input;
}

void OrbitGame::OnUpdate(const GameFrame& frame, GameServices& services) {
    const f32 fixed_step = frame.fixed_step_seconds > 0.0f ? frame.fixed_step_seconds : 1.0f / 60.0f;
    for (u32 step = 0; step < std::max(frame.fixed_steps, 1u); ++step) {
        UpdateShip(fixed_step, services);
        UpdateOrbs(fixed_step, services);
        UpdateHazards(fixed_step, services);
        elapsed_ += fixed_step;
        if (!game_over_) {
            time_remaining_ = std::max(time_remaining_ - fixed_step, 0.0f);
            if (time_remaining_ <= 0.0f) {
                game_over_ = true;
                valgo::LogInfo(kCategory, "run finished with {} points", score_);
            }
        }
    }

    // --- Camera: a fixed chase view of the arena -------------------------
    if (services.camera != nullptr) {
        const Vec3 focus = ship_position_;
        services.camera->SetPosition(focus + Vec3{0.0f, 17.0f, 15.0f});
        services.camera->LookAt(focus + Vec3{0.0f, 0.0f, 1.0f});
        services.camera->SetPerspectiveFov(52.0f);
    }

    // --- Ship transform + engine debug draw ------------------------------
    if (valgo::scene::TransformComponent* transform =
            services.scene->GetComponent<TransformComponent>(ship_entity_)) {
        transform->local.position = ship_position_;
        transform->local.rotation = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, ship_heading_);
    }
    if (valgo::scene::TransformComponent* transform =
            services.scene->GetComponent<TransformComponent>(cursor_entity_)) {
        transform->local.position = ship_position_ + Vec3{0.0f, 1.6f, 0.0f};
        transform->local.rotation = Quat::FromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, -valgo::kHalfPi);
    }

    services.renderer->DrawWireSphere(ship_position_ + Vec3{0.0f, 0.4f, 0.0f}, 1.4f,
                                      {0.4f, 0.9f, 1.0f, 0.8f}, 16);

    DrawHud(services);
}

f32 OrbitGame::NearestActiveOrbDistance() const {
    f32 best = 1.0e9f;
    for (const Orb& orb : orbs_) {
        if (!orb.active) {
            continue;
        }
        const f32 distance = Length(Vec3{orb.base_position.x - ship_position_.x, 0.0f,
                                         orb.base_position.z - ship_position_.z});
        best = std::min(best, distance);
    }
    return best;
}

void OrbitGame::UpdateShip(f32 delta_seconds, GameServices& services) {
    if (game_over_) {
        return;
    }
    hazard_cooldown_ = std::max(hazard_cooldown_ - delta_seconds, 0.0f);
    const Vec2 input = SampleInput(services);
    const Vec3 wish{input.x, 0.0f, -input.y};
    constexpr f32 kCruiseSpeed = 12.0f;
    // Slow down on approach: at 12 m/s a 1 m pickup window is a single frame.
    const f32 target_distance = NearestActiveOrbDistance();
    const f32 kSpeed = Lerp(3.0f, kCruiseSpeed, Saturate(target_distance / 6.0f));
    ship_velocity_ = valgo::MoveTowards(ship_velocity_, wish * kSpeed, 40.0f * delta_seconds);

    Vec3 next = ship_position_ + ship_velocity_ * delta_seconds;
    if (valgo::LengthSquared(Vec3{next.x, 0.0f, next.z}) > 0.0f) {
        const f32 radius = valgo::Length(Vec3{next.x, 0.0f, next.z});
        if (radius > kArenaRadius - 1.5f) {
            // Use the engine's physics world to resolve against the rim instead of
            // hard-coding a second collision model in game code.
            u32 hit_user_data = kInvalidIndex;
            if (services.physics->OverlapAabb(Aabb::FromCenterExtents(next, Vec3{0.9f, 0.4f, 0.9f}),
                                              hit_user_data, valgo::physics::kLayerWorld)) {
                next = ship_position_;
                ship_velocity_ = ship_velocity_ * -0.25f;
            }
        }
    }
    ship_position_ = next;
    ship_position_.y = 0.45f;
    if (valgo::LengthSquared(Vec3{ship_velocity_.x, 0.0f, ship_velocity_.z}) > 0.05f) {
        ship_heading_ = std::atan2(-ship_velocity_.x, -ship_velocity_.z);
    }
}

void OrbitGame::UpdateOrbs(f32 delta_seconds, GameServices& services) {
    for (Orb& orb : orbs_) {
        if (!orb.active) {
            orb.respawn_timer -= delta_seconds;
            if (orb.respawn_timer <= 0.0f) {
                SpawnOrb(static_cast<u32>(&orb - orbs_.data()), services);
            }
            continue;
        }

        orb.phase += delta_seconds * 2.2f;
        const Vec3 position = orb.base_position + Vec3{0.0f, std::sin(orb.phase) * 0.25f, 0.0f};
        if (valgo::scene::TransformComponent* transform =
                services.scene->GetComponent<TransformComponent>(orb.entity)) {
            transform->local.position = position;
        }

        if (valgo::LengthSquared(Vec3{position.x - ship_position_.x, 0.0f, position.z - ship_position_.z}) <
            kOrbPickupRadius * kOrbPickupRadius) {
            orb.active = false;
            orb.respawn_timer = 1.4f;
            ++score_;
            ++collected_;
            if (orb.collider_id != 0) {
                services.physics->RemoveCollider(orb.collider_id);
                orb.collider_id = 0;
            }
            if (valgo::scene::MeshRendererComponent* renderer =
                    services.scene->GetComponent<MeshRendererComponent>(orb.entity)) {
                renderer->visible = false;
            }
        }
    }
}

void OrbitGame::UpdateHazards(f32 delta_seconds, GameServices& services) {
    for (Hazard& hazard : hazards_) {
        hazard.angle += hazard.speed * delta_seconds;
        const Vec3 position{std::cos(hazard.angle) * hazard.radius, hazard.height,
                            std::sin(hazard.angle) * hazard.radius};
        if (valgo::scene::TransformComponent* transform =
                services.scene->GetComponent<TransformComponent>(hazard.entity)) {
            transform->local.position = position;
            transform->local.rotation = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, -hazard.angle);
        }

        // Gameplay collision rule (not engine collateral): proximity knock-back
        // with a per-ship cooldown so a sweeper cannot drain the clock in a
        // handful of frames.
        const Vec3 delta{position.x - ship_position_.x, 0.0f, position.z - ship_position_.z};
        if (hazard_cooldown_ <= 0.0f && valgo::LengthSquared(delta) < 2.2f * 2.2f) {
            ship_velocity_ = valgo::Normalize(delta) * 9.0f;
            time_remaining_ = std::max(time_remaining_ - 0.5f, 0.0f);
            hazard_cooldown_ = 0.6f;
            ++hits_taken_;
        }
    }
}

void OrbitGame::DrawHud(const GameServices& services) {
    valgo::render::Overlay2D& hud = services.renderer->overlay();
    const f32 width = static_cast<f32>(services.viewport_width);
    const f32 height = static_cast<f32>(services.viewport_height);

    hud.DrawText({24.0f, 22.0f}, "ORBIT RUNNER", {0.75f, 0.92f, 1.0f, 1.0f}, 2.2f);
    hud.DrawText({24.0f, 48.0f}, "COLLECT THE ORBS. AVOID THE SWEEPERS.",
                 {0.55f, 0.68f, 0.8f, 1.0f}, 1.3f);

    const std::string score_text = valgo::Format("SCORE {}", score_);
    hud.DrawText({width - 210.0f, 24.0f}, score_text, {1.0f, 0.85f, 0.35f, 1.0f}, 2.2f);
    const std::string time_text = valgo::Format("TIME {}", static_cast<int>(time_remaining_ + 0.5f));
    hud.DrawText({width - 210.0f, 52.0f}, time_text, {0.8f, 0.86f, 0.92f, 1.0f}, 1.8f);
    const std::string orbs_text = valgo::Format("ORBS {}", collected_);
    hud.DrawText({width - 210.0f, 76.0f}, orbs_text, {0.8f, 0.86f, 0.92f, 1.0f}, 1.5f);

    hud.DrawText({24.0f, height - 34.0f}, "WASD  MOVE", {0.6f, 0.7f, 0.8f, 0.9f}, 1.3f);

    if (game_over_) {
        hud.DrawTextCentered(width * 0.5f, height * 0.5f - 14.0f, "RUN COMPLETE",
                             {1.0f, 0.9f, 0.5f, 1.0f}, 3.0f);
        const std::string final_score = valgo::Format("FINAL SCORE {}", score_);
        hud.DrawTextCentered(width * 0.5f, height * 0.5f + 20.0f, final_score,
                             {0.9f, 0.94f, 1.0f, 1.0f}, 2.0f);
    }
}

void OrbitGame::OnShutdown(GameServices& services) {
    VALGO_UNUSED(services);
    valgo::LogInfo(kCategory, "orbit session ended: score {} ({} orbs collected, {} hits taken)", score_,
                   collected_, hits_taken_);
}

}  // namespace valgo_orbit
