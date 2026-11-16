// =============================================================================
// Project Valgo — hitscan weapon model (GAME code).
//
// Pure gameplay: ammunition, fire cadence, spread, reload timing and recoil
// parameters. Actual ray queries and damage application are performed by the FPS
// game through the engine's physics and scene interfaces.
// =============================================================================
#pragma once

#include <string_view>

#include "valgo_fps/game_types.h"
#include "valgo/math/math.h"

namespace valgo_fps {

struct WeaponConfig {
    std::string_view display_name = "VK-7 RIFLE";
    u32 magazine_size = 30;
    f32 damage = 26.0f;
    f32 headshot_damage = 90.0f;
    f32 fire_interval = 0.1f;  ///< 600 rounds per minute.
    f32 reload_seconds = 2.1f;
    f32 hip_spread_degrees = 0.55f;
    f32 moving_spread_degrees = 0.9f;
    f32 recoil_pitch_degrees = 0.5f;
    f32 recoil_yaw_degrees = 0.22f;
    f32 range_metres = 150.0f;
};

struct WeaponInput {
    bool fire = false;
    bool reload = false;
};

class Weapon {
public:
    void Configure(const WeaponConfig& config);
    const WeaponConfig& config() const { return config_; }

    void Reset();

    /// Advances timers and consumes one shot when possible.
    /// Returns true when a shot was fired this step.
    bool Update(const WeaponInput& input, f32 delta_seconds);

    bool CanFire() const { return !reloading_ && ammo_ > 0 && fire_cooldown_ <= 0.0f; }
    bool IsReloading() const { return reloading_; }
    f32 ReloadProgress() const;
    u32 AmmoInMagazine() const { return ammo_; }
    u32 MagazineSize() const { return config_.magazine_size; }
    f32 MuzzleFlashTime() const { return muzzle_flash_; }
    u32 ShotsFired() const { return shots_fired_; }
    f32 SpreadDegrees(bool moving) const;

private:
    void BeginReload();

    WeaponConfig config_{};
    u32 ammo_ = 30;
    f32 fire_cooldown_ = 0.0f;
    f32 reload_timer_ = 0.0f;
    bool reloading_ = false;
    f32 muzzle_flash_ = 0.0f;
    u32 shots_fired_ = 0;
};

}  // namespace valgo_fps
