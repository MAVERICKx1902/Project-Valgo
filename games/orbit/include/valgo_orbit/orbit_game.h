// =============================================================================
// Project Valgo — "Orbit Runner", the second validation game.
//
// A top-down arcade collector. Different genre, different camera, different
// gameplay rules, and *zero* FPS-specific code: it consumes exactly the same
// engine services through exactly the same IGameModule interface. If this game
// needed engine changes to exist, the engine would still be an FPS framework.
// =============================================================================
#pragma once

#include <string_view>
#include <vector>

#include "valgo/core/random.h"
#include "valgo/game/game_module.h"

namespace valgo_orbit {

using valgo::f32;
using valgo::u32;
using valgo::usize;
using valgo::Vec2;
using valgo::Vec3;


struct OrbitGameOptions {
    bool scripted_demo = false;  ///< Deterministic input for automated capture.
};

class OrbitGame final : public valgo::game::IGameModule {
public:
    explicit OrbitGame(OrbitGameOptions options = {});

    std::string_view name() const override { return "Valgo | Orbit Runner"; }

    void OnStartup(valgo::game::GameServices& services) override;
    void OnUpdate(const valgo::game::GameFrame& frame, valgo::game::GameServices& services) override;
    void OnShutdown(valgo::game::GameServices& services) override;

    // Test hooks
    u32 score() const { return score_; }
    u32 hits_taken() const { return hits_taken_; }
    f32 time_remaining() const { return time_remaining_; }
    usize orb_count() const { return orbs_.size(); }
    Vec3 ship_position() const { return ship_position_; }

private:
    struct Orb {
        valgo::scene::Entity entity{};
        Vec3 base_position{};
        f32 phase = 0.0f;
        bool active = true;
        f32 respawn_timer = 0.0f;
        u32 collider_id = 0;
        bool operator==(const Orb&) const = default;
    };

    struct Hazard {
        valgo::scene::Entity entity{};
        f32 angle = 0.0f;
        f32 radius = 0.0f;
        f32 speed = 0.0f;
        f32 height = 0.0f;
    };

    void BuildArena(valgo::game::GameServices& services);
    void SpawnOrb(u32 index, valgo::game::GameServices& services);
    void UpdateShip(f32 delta_seconds, valgo::game::GameServices& services);
    void UpdateOrbs(f32 delta_seconds, valgo::game::GameServices& services);
    void UpdateHazards(f32 delta_seconds, valgo::game::GameServices& services);
    Vec2 SampleInput(const valgo::game::GameServices& services) const;
    f32 NearestActiveOrbDistance() const;
    void DrawHud(const valgo::game::GameServices& services);

    OrbitGameOptions options_;
    valgo::Random rng_{0x0B17ull};
    std::vector<Orb> orbs_;
    std::vector<Hazard> hazards_;
    valgo::scene::Entity ship_entity_{};
    valgo::scene::Entity cursor_entity_{};

    Vec3 ship_position_{0.0f, 0.0f, 0.0f};
    Vec3 ship_velocity_{0.0f, 0.0f, 0.0f};
    f32 ship_heading_ = 0.0f;
    f32 time_remaining_ = 60.0f;
    f32 elapsed_ = 0.0f;
    u32 score_ = 0;
    u32 collected_ = 0;
    u32 hits_taken_ = 0;
    f32 hazard_cooldown_ = 0.0f;
    bool game_over_ = false;
};

}  // namespace valgo_orbit
