// =============================================================================
// Project Valgo — training map builder (GAME code).
//
// Builds the first playable environment entirely from engine services: meshes
// from the asset manager, transforms in the scene, colliders in the physics
// world. The map is data (a function today, an editor-authored scene in
// milestone 7).
// =============================================================================
#pragma once

#include <vector>

#include "valgo_fps/game_types.h"

#include "valgo/assets/asset_manager.h"
#include "valgo/physics/physics_world.h"
#include "valgo/render/renderer.h"
#include "valgo/scene/scene.h"

namespace valgo_fps {

struct MapBuildResult {
    Vec3 player_spawn{0.0f, 0.0f, 8.0f};
    f32 player_yaw = 0.0f;
    std::vector<Vec3> target_positions;  ///< Feet positions for training dummies.
    Vec3 light_direction{-0.42f, -1.0f, -0.3f};
};

/// Builds "Range Alpha": a training yard with cover, a crouch wall, step-up
/// platforms and a shooting range.
MapBuildResult BuildTrainingMap(valgo::scene::Scene& scene, valgo::physics::PhysicsWorld& physics,
                                valgo::assets::AssetManager& assets,
                                const valgo::render::MeshGpuData* crate_mesh = nullptr);

}  // namespace valgo_fps
