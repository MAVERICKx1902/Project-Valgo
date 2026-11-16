// =============================================================================
// Project Valgo — FPS game type aliases.
//
// Engine types live in namespace valgo; the game lives in valgo_fps. Importing
// the handful of engine names the game uses keeps game code readable without
// pushing game-specific aliases into the engine.
// =============================================================================
#pragma once

#include "valgo/assets/asset_manager.h"
#include "valgo/core/log.h"
#include "valgo/core/types.h"
#include "valgo/game/game_module.h"
#include "valgo/input/input.h"
#include "valgo/math/math.h"
#include "valgo/physics/character_motor.h"
#include "valgo/physics/physics_world.h"
#include "valgo/render/camera.h"
#include "valgo/render/mesh.h"
#include "valgo/render/overlay2d.h"
#include "valgo/render/renderer.h"
#include "valgo/scene/scene.h"

namespace valgo_fps {

using valgo::f32;
using valgo::f64;
using valgo::i32;
using valgo::u32;
using valgo::u64;
using valgo::usize;
using valgo::Aabb;
using valgo::Mat4;
using valgo::Quat;
using valgo::Ray;
using valgo::Transform;
using valgo::Vec2;
using valgo::Vec3;
using valgo::Vec4;

using valgo::AnglesFromDirection;
using valgo::AnyPerpendicular;
using valgo::Clamp;
using valgo::Cross;
using valgo::Dot;
using valgo::ForwardFromEuler;
using valgo::Length;
using valgo::LengthSquared;
using valgo::Lerp;
using valgo::MoveTowards;
using valgo::MovementBasis;
using valgo::MovementBasisFromYaw;
using valgo::Normalize;
using valgo::RayHit;
using valgo::Saturate;
using valgo::SphericalAngles;
using valgo::WrapAngle;
using valgo::kDegToRad;
using valgo::kEpsilon;
using valgo::kHalfPi;
using valgo::kInvalidIndex;
using valgo::kPi;
using valgo::kRadToDeg;
using valgo::kTwoPi;

namespace physics = valgo::physics;
namespace render = valgo::render;
namespace scene = valgo::scene;
namespace input = valgo::input;
namespace assets = valgo::assets;
namespace game = valgo::game;
using valgo::render::Camera;

}  // namespace valgo_fps
