#include "valgo/physics/character_motor.h"

#include <algorithm>
#include <cmath>

namespace valgo::physics {
namespace {

f32 SignOf(f32 value) { return value > 0.0f ? 1.0f : (value < 0.0f ? -1.0f : 0.0f); }

}  // namespace

void CharacterMotor::Teleport(const Vec3& feet_position) {
    position_ = feet_position;
    velocity_ = Vec3{};
    grounded_ = false;
}

f32 CharacterMotor::MoveAxisClamped(const PhysicsWorld& world, int axis, f32 distance, bool& blocked,
                                    Vec3& normal) {
    blocked = false;
    normal = Vec3{};
    if (std::fabs(distance) <= 1.0e-9f) {
        return 0.0f;
    }

    const AxisSweepResult sweep = world.SweepAxis(bounds(), axis, distance, config_.layer_mask);
    f32 allowed = sweep.distance;
    if (sweep.blocked) {
        blocked = true;
        normal = sweep.normal;
        // Stop short by the skin width so the next sweep still sees separation.
        allowed -= SignOf(distance) * config_.skin_width;
        if (SignOf(allowed) != SignOf(distance)) {
            allowed = 0.0f;
        }
    }

    position_[axis] += allowed;
    return allowed;
}

CharacterMoveResult CharacterMotor::Move(const PhysicsWorld& world, const Vec3& horizontal_velocity,
                                         bool jump, f32 delta_seconds) {
    CharacterMoveResult result;
    if (delta_seconds <= 0.0f) {
        return result;
    }

    // Transitions matter, not states: "landed" must fire once when the character
    // arrives on a surface, never on every step spent standing on it.
    const bool was_grounded = grounded_;

    // --- Vertical velocity ------------------------------------------------
    if (jump && grounded_) {
        velocity_.y = config_.jump_speed;
        grounded_ = false;
        result.jumped = true;
    }
    velocity_.y -= config_.gravity * delta_seconds;
    velocity_.y = std::max(velocity_.y, -config_.terminal_velocity);

    // --- Horizontal movement with step-up assistance ----------------------
    const Vec3 horizontal_delta{horizontal_velocity.x * delta_seconds, 0.0f,
                                horizontal_velocity.z * delta_seconds};
    const Vec3 start_position = position_;

    for (int axis : {0, 2}) {
        const f32 requested = horizontal_delta[axis];
        if (std::fabs(requested) <= 1.0e-8f) {
            continue;
        }

        bool blocked = false;
        Vec3 normal{};
        const f32 moved = MoveAxisClamped(world, axis, requested, blocked, normal);
        result.horizontal_distance += std::fabs(moved);

        if (!blocked || std::fabs(moved) >= std::fabs(requested) - 1.0e-4f) {
            continue;
        }

        result.hit_wall = true;
        result.wall_normal = normal;

        // Step-up: lift, retry the horizontal move, then settle back down.
        if (!grounded_ || config_.step_height <= 0.0f) {
            continue;
        }

        const Vec3 before_step = position_;
        bool lift_blocked = false;
        Vec3 lift_normal{};
        const f32 lifted = MoveAxisClamped(world, 1, config_.step_height, lift_blocked, lift_normal);
        if (lifted < config_.step_height * 0.25f) {
            position_ = before_step;  // no headroom
            continue;
        }

        bool retry_blocked = false;
        Vec3 retry_normal{};
        const f32 retried = MoveAxisClamped(world, axis, requested, retry_blocked, retry_normal);

        if (std::fabs(retried) > std::fabs(moved) + 0.01f) {
            // Settle back down onto the step.
            bool settle_blocked = false;
            Vec3 settle_normal{};
            MoveAxisClamped(world, 1, -(lifted + config_.ground_probe_distance), settle_blocked,
                            settle_normal);
            if (settle_blocked) {
                result.stepped_up = true;
                result.hit_wall = false;
                result.wall_normal = Vec3{};
                result.horizontal_distance += std::fabs(retried) - std::fabs(moved);
                continue;
            }
        }

        position_ = before_step;  // step rejected
    }

    // --- Vertical movement -------------------------------------------------
    bool vertical_blocked = false;
    Vec3 vertical_normal{};
    const f32 vertical_requested = velocity_.y * delta_seconds;
    const f32 vertical_moved = MoveAxisClamped(world, 1, vertical_requested, vertical_blocked,
                                               vertical_normal);
    if (vertical_blocked) {
        if (vertical_requested < 0.0f && vertical_normal.y > 0.5f) {
            result.landed = !was_grounded;
            grounded_ = true;
        } else if (vertical_requested > 0.0f && vertical_normal.y < -0.5f) {
            result.hit_ceiling = true;
            grounded_ = false;
        }
        velocity_.y = 0.0f;
    } else {
        // Not blocked: if we were grounded and this step moved us down, we are
        // now falling.
        if (vertical_moved < -1.0e-6f && grounded_) {
            grounded_ = false;
        }
        if (vertical_requested > 0.0f) {
            grounded_ = false;
        }
    }

    // --- Ground probe ------------------------------------------------------
    // Catches the case where the character ends the step exactly on a surface
    // (no downward velocity left to trigger a collision) or walks off a ledge.
    if (!grounded_ && velocity_.y <= 0.0f) {
        bool probe_blocked = false;
        Vec3 probe_normal{};
        const f32 probe = MoveAxisClamped(world, 1, -config_.ground_probe_distance, probe_blocked,
                                          probe_normal);
        if (probe_blocked) {
            result.landed = result.landed || !was_grounded;
            grounded_ = true;
            velocity_.y = 0.0f;
        } else if (probe < 0.0f) {
            // Nothing below: restore, we are airborne.
            position_.y -= probe;
            grounded_ = false;
        }
    }

    VALGO_UNUSED(start_position);
    return result;
}

}  // namespace valgo::physics
