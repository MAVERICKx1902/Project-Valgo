// =============================================================================
// Project Valgo — tactical FPS player controller (GAME code).
//
// This is deliberately game-side code: it defines what "tactical FPS movement"
// means (sprint, crouch, lean, view bob, recoil) and drives engine services
// (character motor, camera, input) through their public interfaces. The engine
// knows nothing about leaning or recoil.
// =============================================================================
#pragma once

#include "valgo_fps/game_types.h"
#include "valgo/math/math.h"
#include "valgo/physics/character_motor.h"
#include "valgo/physics/physics_world.h"
#include "valgo/render/camera.h"

namespace valgo_fps {

/// Device-independent movement/look intent, produced either by real input or by
/// the scripted demo driver. Keeping this separate from InputState is what makes
/// deterministic integration tests possible.
struct PlayerInput {
    Vec2 move;          ///< x = right, y = forward, each in [-1, 1].
    Vec2 look;          ///< Mouse delta in pixels.
    bool jump = false;
    bool sprint = false;
    bool crouch = false;
    bool lean_left = false;
    bool lean_right = false;
    bool fire = false;
    bool reload = false;
};

struct PlayerTuning {
    f32 walk_speed = 5.4f;
    f32 sprint_speed = 8.2f;
    f32 crouch_speed = 2.5f;
    f32 ground_acceleration = 70.0f;
    f32 air_acceleration = 16.0f;
    f32 ground_friction = 52.0f;
    f32 jump_speed = 5.9f;
    f32 gravity = 20.0f;

    f32 mouse_sensitivity = 0.105f;  ///< Degrees of rotation per pixel.
    f32 pitch_limit_degrees = 88.0f;

    f32 stand_eye_height = 1.68f;
    f32 crouch_eye_height = 1.02f;
    f32 stand_collider_height = 1.82f;
    f32 crouch_collider_height = 1.15f;
    f32 collider_radius = 0.32f;
    f32 crouch_transition_seconds = 0.14f;

    f32 lean_angle_degrees = 15.0f;
    f32 lean_lateral_offset = 0.22f;
    f32 lean_speed = 7.0f;

    f32 bob_frequency = 8.5f;
    f32 bob_amplitude = 0.028f;
    f32 bob_roll_degrees = 0.75f;
    f32 landing_dip = 0.14f;
    f32 landing_recovery = 6.5f;

    f32 max_health = 100.0f;
};

class PlayerController {
public:
    void Initialize(const PlayerTuning& tuning);
    void Spawn(const Vec3& feet_position, f32 yaw_radians);

    /// Advances one fixed simulation step.
    void Update(const PlayerInput& input, f32 delta_seconds, const physics::PhysicsWorld& world,
                bool input_enabled = true);

    /// Applies the current pose (eye height, bob, lean, recoil) to the camera.
    void ApplyCamera(render::Camera& camera) const;

    Vec3 EyePosition() const;
    Vec3 AimDirection() const;
    Ray AimRay() const;

    f32 yaw() const { return yaw_; }
    f32 pitch() const { return pitch_; }
    f32 Health() const { return health_; }
    bool IsAlive() const { return health_ > 0.0f; }
    bool Grounded() const { return motor_.grounded(); }
    bool IsCrouching() const { return crouch_blend_ > 0.5f; }
    bool IsSprinting() const { return sprinting_; }
    bool IsLeaning() const { return std::fabs(lean_) > 0.15f; }
    f32 HorizontalSpeed() const;

    void AddViewKick(f32 pitch_degrees, f32 yaw_degrees);
    void ApplyDamage(f32 amount);
    void ResetHealth();

    const Vec3& FeetPosition() const { return motor_.position(); }
    physics::CharacterMotor& Motor() { return motor_; }
    const physics::CharacterMotor& Motor() const { return motor_; }
    const PlayerTuning& tuning() const { return tuning_; }

private:
    void UpdateLook(const PlayerInput& input);
    void UpdateStance(const PlayerInput& input, f32 delta_seconds, const physics::PhysicsWorld& world);
    void UpdateMovement(const PlayerInput& input, f32 delta_seconds, const physics::PhysicsWorld& world);
    void UpdateLean(const PlayerInput& input, f32 delta_seconds);

    PlayerTuning tuning_{};
    physics::CharacterMotor motor_{};
    Vec3 horizontal_velocity_{};
    Vec3 spawn_position_{0.0f, 0.0f, 6.0f};

    f32 yaw_ = 0.0f;
    f32 pitch_ = 0.0f;
    f32 recoil_pitch_ = 0.0f;
    f32 recoil_yaw_ = 0.0f;

    f32 crouch_blend_ = 0.0f;
    f32 eye_height_ = 1.68f;
    f32 lean_ = 0.0f;  ///< -1 = left, +1 = right.
    f32 strafe_tilt_ = 0.0f;
    f32 bob_phase_ = 0.0f;
    f32 bob_offset_ = 0.0f;
    f32 bob_roll_ = 0.0f;
    f32 landing_offset_ = 0.0f;
    f32 sprinting_ = false;
    f32 health_ = 100.0f;
};

}  // namespace valgo_fps
