#include "valgo_fps/player_controller.h"

#include <algorithm>
#include <cmath>

namespace valgo_fps {

void PlayerController::Initialize(const PlayerTuning& tuning) {
    tuning_ = tuning;
    health_ = tuning_.max_health;

    physics::CharacterMotorConfig motor_config;
    motor_config.collider_min = Vec3{-tuning_.collider_radius, 0.0f, -tuning_.collider_radius};
    motor_config.collider_max = Vec3{tuning_.collider_radius, tuning_.stand_collider_height,
                                     tuning_.collider_radius};
    motor_config.gravity = tuning_.gravity;
    motor_config.jump_speed = tuning_.jump_speed;
    motor_config.step_height = 0.38f;
    motor_config.skin_width = 0.002f;
    motor_config.ground_probe_distance = 0.08f;
    motor_config.layer_mask = physics::kAllLayers;
    motor_.Configure(motor_config);

    eye_height_ = tuning_.stand_eye_height;
}

void PlayerController::Spawn(const Vec3& feet_position, f32 yaw_radians) {
    spawn_position_ = feet_position;
    motor_.Teleport(feet_position);
    motor_.SetGrounded(false);
    horizontal_velocity_ = Vec3{};
    yaw_ = yaw_radians;
    pitch_ = 0.0f;
    recoil_pitch_ = 0.0f;
    recoil_yaw_ = 0.0f;
    crouch_blend_ = 0.0f;
    lean_ = 0.0f;
    strafe_tilt_ = 0.0f;
    bob_phase_ = 0.0f;
    bob_offset_ = 0.0f;
    bob_roll_ = 0.0f;
    landing_offset_ = 0.0f;
    health_ = tuning_.max_health;
    eye_height_ = tuning_.stand_eye_height;
}

void PlayerController::Update(const PlayerInput& input, f32 delta_seconds,
                              const physics::PhysicsWorld& world, bool input_enabled) {
    if (delta_seconds <= 0.0f) {
        return;
    }

    PlayerInput effective = input;
    if (!input_enabled) {
        effective = PlayerInput{};
    }

    UpdateLook(effective);
    UpdateStance(effective, delta_seconds, world);
    UpdateLean(effective, delta_seconds);
    UpdateMovement(effective, delta_seconds, world);
}

void PlayerController::UpdateLook(const PlayerInput& input) {
    const f32 sensitivity = tuning_.mouse_sensitivity;
    yaw_ = WrapAngle(yaw_ - input.look.x * sensitivity * kDegToRad);
    pitch_ = Clamp(pitch_ - input.look.y * sensitivity * kDegToRad,
                   -tuning_.pitch_limit_degrees * kDegToRad,
                   tuning_.pitch_limit_degrees * kDegToRad);
}

void PlayerController::UpdateStance(const PlayerInput& input, f32 delta_seconds,
                                    const physics::PhysicsWorld& world) {
    bool wants_crouch = input.crouch;

    // Refuse to stand up under geometry: test the standing collider for overlap.
    if (!wants_crouch && crouch_blend_ > 0.01f) {
        physics::CharacterMotorConfig stand_config = motor_.config();
        stand_config.collider_max.y = tuning_.stand_collider_height;
        const Aabb stand_bounds = stand_config.BoundsAt(motor_.position());
        u32 ignored = kInvalidIndex;
        if (world.OverlapAabb(stand_bounds.Expanded(-0.01f), ignored)) {
            wants_crouch = true;
        }
    }

    const f32 target = wants_crouch ? 1.0f : 0.0f;
    const f32 rate = 1.0f / std::max(tuning_.crouch_transition_seconds, 0.01f);
    crouch_blend_ = MoveTowards(crouch_blend_, target, rate * delta_seconds);
    crouch_blend_ = Saturate(crouch_blend_);

    const f32 collider_height =
        Lerp(tuning_.stand_collider_height, tuning_.crouch_collider_height, crouch_blend_);
    motor_.MutableConfig().collider_max.y = collider_height;

    const f32 eye_target = Lerp(tuning_.stand_eye_height, tuning_.crouch_eye_height, crouch_blend_);
    eye_height_ = Lerp(eye_height_, eye_target, Saturate(delta_seconds * 18.0f));
}

void PlayerController::UpdateLean(const PlayerInput& input, f32 delta_seconds) {
    f32 lean_target = 0.0f;
    if (input.lean_left && !input.lean_right) {
        lean_target = -1.0f;
    } else if (input.lean_right && !input.lean_left) {
        lean_target = 1.0f;
    }
    lean_ = MoveTowards(lean_, lean_target, tuning_.lean_speed * delta_seconds);
    lean_ = Clamp(lean_, -1.0f, 1.0f);

    // Slight camera roll when strafing keeps movement readable.
    strafe_tilt_ = Lerp(strafe_tilt_, -input.move.x * 2.2f, Saturate(delta_seconds * 10.0f));
}

void PlayerController::UpdateMovement(const PlayerInput& input, f32 delta_seconds,
                                      const physics::PhysicsWorld& world) {
    const MovementBasis basis = MovementBasisFromYaw(yaw_);
    Vec3 wish_direction = basis.forward * input.move.y + basis.right * input.move.x;
    if (LengthSquared(wish_direction) > 1.0f) {
        wish_direction = Normalize(wish_direction);
    }

    const bool can_sprint = input.sprint && input.move.y > 0.1f && crouch_blend_ < 0.2f;
    sprinting_ = can_sprint;

    f32 target_speed = tuning_.walk_speed;
    if (crouch_blend_ > 0.5f) {
        target_speed = tuning_.crouch_speed;
    } else if (can_sprint) {
        target_speed = tuning_.sprint_speed;
    }

    const Vec3 desired_velocity = wish_direction * target_speed;
    const bool grounded = motor_.grounded();
    const f32 horizontal_speed = LengthSquared(horizontal_velocity_) > 0.0f
                                     ? Length(Vec3{horizontal_velocity_.x, 0.0f, horizontal_velocity_.z})
                                     : 0.0f;

    if (grounded) {
        if (horizontal_speed > 0.05f) {
            const f32 drop = tuning_.ground_friction * delta_seconds;
            const f32 new_speed = std::max(horizontal_speed - drop, 0.0f);
            horizontal_velocity_ = horizontal_velocity_ * (new_speed / horizontal_speed);
        }
        const f32 acceleration = tuning_.ground_acceleration;
        Vec3 delta = desired_velocity - horizontal_velocity_;
        const f32 delta_length = Length(delta);
        if (delta_length > 0.0f) {
            const f32 step = std::min(delta_length, acceleration * delta_seconds);
            horizontal_velocity_ += delta * (step / delta_length);
        }
    } else {
        // Limited air control; preserves momentum from a jump.
        const Vec3 delta = desired_velocity - horizontal_velocity_;
        const f32 delta_length = Length(delta);
        if (delta_length > 0.0f) {
            const f32 step = std::min(delta_length, tuning_.air_acceleration * delta_seconds);
            horizontal_velocity_ += delta * (step / delta_length);
        }
    }

    const bool jump = input.jump && grounded;
    const physics::CharacterMoveResult move =
        motor_.Move(world, horizontal_velocity_, jump, delta_seconds);

    if (move.hit_wall) {
        // Kill the component of velocity going into the wall to stop "sticky"
        // accumulation against surfaces.
        const Vec3 normal = Normalize(move.wall_normal);
        const f32 into_wall = Dot(horizontal_velocity_, normal);
        if (into_wall < 0.0f) {
            horizontal_velocity_ -= normal * into_wall;
        }
    }

    // --- View feedback ----------------------------------------------------
    const f32 speed = HorizontalSpeed();
    if (grounded && speed > 0.4f) {
        bob_phase_ += delta_seconds * tuning_.bob_frequency * (speed / tuning_.walk_speed);
    } else {
        bob_phase_ = Lerp(bob_phase_, std::round(bob_phase_ / kPi) * kPi, Saturate(delta_seconds * 8.0f));
    }

    const f32 bob_target = std::sin(bob_phase_ * 2.0f) * tuning_.bob_amplitude *
                           Saturate(speed / tuning_.walk_speed);
    bob_offset_ = Lerp(bob_offset_, bob_target, Saturate(delta_seconds * 20.0f));
    const f32 roll_target = std::sin(bob_phase_) * tuning_.bob_roll_degrees * kDegToRad *
                            Saturate(speed / tuning_.walk_speed);
    bob_roll_ = Lerp(bob_roll_, roll_target, Saturate(delta_seconds * 20.0f));

    if (move.landed) {
        landing_offset_ -= tuning_.landing_dip * Saturate(1.0f - speed / tuning_.sprint_speed * 0.5f);
    }
    landing_offset_ = MoveTowards(landing_offset_, 0.0f, tuning_.landing_recovery * delta_seconds);

    // --- Recoil recovery --------------------------------------------------
    recoil_pitch_ = MoveTowards(recoil_pitch_, 0.0f, delta_seconds * 14.0f);
    recoil_yaw_ = MoveTowards(recoil_yaw_, 0.0f, delta_seconds * 14.0f);
    if (!grounded) {
        horizontal_velocity_ = ClampLength(horizontal_velocity_, tuning_.sprint_speed * 1.4f);
    }
}

f32 PlayerController::HorizontalSpeed() const {
    return Length(Vec3{horizontal_velocity_.x, 0.0f, horizontal_velocity_.z});
}

void PlayerController::AddViewKick(f32 pitch_degrees, f32 yaw_degrees) {
    recoil_pitch_ += pitch_degrees * kDegToRad;
    recoil_yaw_ += yaw_degrees * kDegToRad;
}

void PlayerController::ApplyDamage(f32 amount) {
    health_ = std::max(health_ - amount, 0.0f);
}

void PlayerController::ResetHealth() { health_ = tuning_.max_health; }

Vec3 PlayerController::EyePosition() const {
    const Vec3 feet = motor_.position();
    const f32 lateral = lean_ * tuning_.lean_lateral_offset;
    const Vec3 right = Normalize(Cross(ForwardFromEuler(pitch_, yaw_), Vec3{0.0f, 1.0f, 0.0f}));
    return Vec3{feet.x + right.x * lateral, feet.y + eye_height_ + bob_offset_ + landing_offset_,
                feet.z + right.z * lateral};
}

Vec3 PlayerController::AimDirection() const {
    return ForwardFromEuler(pitch_ + recoil_pitch_, yaw_ + recoil_yaw_);
}

Ray PlayerController::AimRay() const {
    Ray ray;
    ray.origin = EyePosition();
    ray.direction = AimDirection();
    return ray;
}

void PlayerController::ApplyCamera(render::Camera& camera) const {
    camera.SetPosition(EyePosition());
    camera.SetAngles(pitch_ + recoil_pitch_, yaw_ + recoil_yaw_,
                     lean_ * tuning_.lean_angle_degrees * kDegToRad + bob_roll_ + strafe_tilt_ * kDegToRad);
}

}  // namespace valgo_fps
