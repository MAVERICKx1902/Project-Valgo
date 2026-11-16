// =============================================================================
// Project Valgo — the tactical FPS game module.
//
// Everything here is game code. It consumes engine services (renderer, scene,
// physics, assets, input, camera) through their public interfaces only. It is
// also the engine's proof of concept: if a feature cannot be built here without
// touching engine internals, the engine is missing a capability.
// =============================================================================
#pragma once

#include "valgo_fps/game_types.h"
#include <string>
#include <string_view>
#include <vector>

#include "valgo/core/random.h"
#include "valgo/game/game_module.h"

#include "valgo_fps/player_controller.h"
#include "valgo_fps/weapon.h"

namespace valgo_fps {

struct FpsGameOptions {
    bool scripted_demo = false;  ///< Deterministic demo input (CI capture).
    bool show_hud = true;
    bool show_debug_overlay = false;
    bool target_respawn = true;
    f32 target_respawn_seconds = 7.0f;
};

class FpsGame final : public valgo::game::IGameModule {
public:
    explicit FpsGame(FpsGameOptions options = {});

    std::string_view name() const override { return "Project Valgo | Tactical FPS"; }

    void OnStartup(valgo::game::GameServices& services) override;
    void OnUpdate(const valgo::game::GameFrame& frame, valgo::game::GameServices& services) override;
    void OnShutdown(valgo::game::GameServices& services) override;

    // --- Automation / test hooks -----------------------------------------
    /// External input source (tests). Overrides both scripted and device input.
    void SetExternalInput(const PlayerInput& input);
    void ClearExternalInput();
    bool HasExternalInput() const { return has_external_input_; }

    const PlayerController& player() const { return player_; }
    const Weapon& weapon() const { return weapon_; }
    usize target_count() const { return targets_.size(); }
    usize alive_target_count() const;
    u32 score() const { return score_; }
    u32 hits() const { return shots_hit_; }
    u32 shots() const { return weapon_.ShotsFired(); }
    f64 elapsed_seconds() const { return elapsed_seconds_; }
    void TeleportPlayer(const Vec3& feet_position, f32 yaw_radians);

private:
    struct Target {
        valgo::scene::Entity body{};
        valgo::scene::Entity head{};
        valgo::scene::Entity base{};
        Vec3 feet_position{};
        f32 health = 100.0f;
        bool alive = true;
        f32 hit_flash = 0.0f;
        f32 respawn_timer = 0.0f;
        u32 body_collider = 0;
        u32 head_collider = 0;
    };

    struct Impact {
        Vec3 position{};
        Vec3 normal{0.0f, 1.0f, 0.0f};
        f32 life = 0.0f;
    };

    void BuildEnvironment(valgo::game::GameServices& services);
    void SpawnTarget(const Vec3& feet_position, valgo::game::GameServices& services);
    void BuildViewModel(valgo::game::GameServices& services);

    PlayerInput SampleDeviceInput(const valgo::game::GameServices& services) const;
    PlayerInput SampleScriptedInput(f32 delta_seconds) const;

    /// Look delta (pixels) that steers the view towards `aim_point`.
    Vec2 ComputeAimDelta(const Vec3& aim_point) const;
    /// Nearest living target in front of the player (invalid when none).
    bool FindNearestTarget(Vec3& out_aim_point) const;
    PlayerInput SampleInput(const valgo::game::GameServices& services, f32 delta_seconds) const;

    void StepSimulation(f32 delta_seconds, valgo::game::GameServices& services);
    void FireWeapon(valgo::game::GameServices& services);
    void ApplyHit(u32 user_data, const RayHit& hit, valgo::game::GameServices& services);
    void UpdateTargets(f32 delta_seconds, valgo::game::GameServices& services);
    void ReviveTarget(Target& target, valgo::game::GameServices& services);
    void UpdateViewModel(valgo::game::GameServices& services);
    void UpdateHud(const valgo::game::GameServices& services);
    void UpdateDebugOverlay(const valgo::game::GameServices& services);

    FpsGameOptions options_;
    PlayerController player_{};
    Weapon weapon_{};
    valgo::Random rng_{0x5A1ED2701ull};

    std::vector<Target> targets_;
    std::vector<Impact> impacts_;

    valgo::scene::Entity view_model_body_{};
    valgo::scene::Entity view_model_handguard_{};
    valgo::scene::Entity view_model_magazine_{};
    valgo::scene::Entity view_model_sight_{};
    valgo::scene::Entity muzzle_flash_{};

    PlayerInput external_input_{};
    bool has_external_input_ = false;

    f64 elapsed_seconds_ = 0.0;
    f32 weapon_kick_ = 0.0f;
    f32 hit_marker_ = 0.0f;
    f32 damage_flash_ = 0.0f;
    f32 sway_x_ = 0.0f;
    f32 sway_y_ = 0.0f;
    f32 shot_pitch_kick_ = 0.0f;
    u32 score_ = 0;
    u32 shots_hit_ = 0;
    u32 total_shots_fired_ = 0;
    bool aim_was_down_ = false;
};

}  // namespace valgo_fps
