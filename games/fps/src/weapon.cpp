#include "valgo_fps/weapon.h"

#include <algorithm>

namespace valgo_fps {

void Weapon::Configure(const WeaponConfig& config) {
    config_ = config;
    ammo_ = config_.magazine_size;
    fire_cooldown_ = 0.0f;
    reload_timer_ = 0.0f;
    reloading_ = false;
    muzzle_flash_ = 0.0f;
    shots_fired_ = 0;
}

void Weapon::Reset() { Configure(config_); }

f32 Weapon::ReloadProgress() const {
    if (!reloading_) {
        return 1.0f;
    }
    const f32 total = std::max(config_.reload_seconds, 0.01f);
    return Saturate(1.0f - reload_timer_ / total);
}

void Weapon::BeginReload() {
    if (reloading_ || ammo_ >= config_.magazine_size) {
        return;
    }
    reloading_ = true;
    reload_timer_ = config_.reload_seconds;
}

f32 Weapon::SpreadDegrees(bool moving) const {
    return moving ? config_.moving_spread_degrees : config_.hip_spread_degrees;
}

bool Weapon::Update(const WeaponInput& input, f32 delta_seconds) {
    fire_cooldown_ = std::max(fire_cooldown_ - delta_seconds, 0.0f);
    muzzle_flash_ = std::max(muzzle_flash_ - delta_seconds, 0.0f);

    if (reloading_) {
        reload_timer_ -= delta_seconds;
        if (reload_timer_ <= 0.0f) {
            reloading_ = false;
            ammo_ = config_.magazine_size;
        }
        return false;
    }

    if (input.reload) {
        BeginReload();
        return false;
    }

    if (input.fire && ammo_ == 0) {
        BeginReload();  // automatic reload on an empty magazine
        return false;
    }

    if (!input.fire || fire_cooldown_ > 0.0f || ammo_ == 0) {
        return false;
    }

    --ammo_;
    fire_cooldown_ = config_.fire_interval;
    muzzle_flash_ = 0.055f;
    ++shots_fired_;
    return true;
}

}  // namespace valgo_fps
