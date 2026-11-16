// =============================================================================
// Project Valgo — character motor.
//
// Generic kinematic character movement: gravity, axis-separated collision
// resolution, wall sliding and step-up assistance. It knows nothing about FPS
// gameplay; the game supplies velocities and interprets the result (sprinting,
// crouching, leaning, landing effects, ...).
// =============================================================================
#pragma once

#include "valgo/math/math.h"
#include "valgo/physics/physics_world.h"

namespace valgo::physics {

struct CharacterMotorConfig {
    Vec3 collider_min{-0.3f, 0.0f, -0.3f};  ///< Relative to the character's feet.
    Vec3 collider_max{0.3f, 1.8f, 0.3f};
    f32 gravity = 20.0f;
    f32 terminal_velocity = 55.0f;
    f32 jump_speed = 6.2f;
    f32 step_height = 0.35f;
    f32 skin_width = 0.002f;
    f32 ground_probe_distance = 0.06f;
    u32 layer_mask = kAllLayers;

    /// Collider box for a character height (used when crouching changes height).
    Aabb BoundsAt(const Vec3& feet_position) const {
        return Aabb{feet_position + collider_min, feet_position + collider_max};
    }
};

struct CharacterMoveResult {
    bool hit_wall = false;
    bool hit_ceiling = false;
    bool landed = false;
    bool jumped = false;
    bool stepped_up = false;
    f32 horizontal_distance = 0.0f;
    Vec3 wall_normal{};
};

class CharacterMotor {
public:
    CharacterMotor() = default;
    explicit CharacterMotor(const CharacterMotorConfig& config) : config_(config) {}

    void Configure(const CharacterMotorConfig& config) { config_ = config; }
    CharacterMotorConfig& MutableConfig() { return config_; }
    const CharacterMotorConfig& config() const { return config_; }

    void Teleport(const Vec3& feet_position);
    const Vec3& position() const { return position_; }
    Aabb bounds() const { return config_.BoundsAt(position_); }

    /// Advances the character by one fixed simulation step.
    /// `horizontal_velocity` is in metres/second and is clamped by the caller.
    CharacterMoveResult Move(const PhysicsWorld& world, const Vec3& horizontal_velocity, bool jump,
                             f32 delta_seconds);

    bool grounded() const { return grounded_; }
    const Vec3& velocity() const { return velocity_; }
    void SetVelocity(const Vec3& velocity) { velocity_ = velocity; }
    void SetGrounded(bool grounded) { grounded_ = grounded; }

private:
    f32 MoveAxisClamped(const PhysicsWorld& world, int axis, f32 distance, bool& blocked, Vec3& normal);

    CharacterMotorConfig config_{};
    Vec3 position_{0.0f, 0.0f, 0.0f};
    Vec3 velocity_{0.0f, 0.0f, 0.0f};
    bool grounded_ = false;
};

}  // namespace valgo::physics
