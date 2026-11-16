// =============================================================================
// Project Valgo — physics abstraction and collision queries.
//
// Current implementation: static axis-aligned boxes with a linear broadphase.
// That is enough to validate movement, collision, raycasting and hit detection
// through the *engine interface*; swapping in a real solver (or Jolt/Bullet
// behind this interface) does not change any gameplay code.
//
// Deliberately game-agnostic: no characters, weapons or damage live here.
// =============================================================================
#pragma once

#include <vector>

#include "valgo/math/math.h"

namespace valgo::physics {

inline constexpr u32 kAllLayers = 0xFFFFFFFFu;
inline constexpr u32 kLayerDefault = 1u << 0u;
inline constexpr u32 kLayerWorld = 1u << 1u;
inline constexpr u32 kLayerProp = 1u << 2u;
inline constexpr u32 kLayerTarget = 1u << 3u;

struct Collider {
    Aabb bounds;
    u32 user_data = kInvalidIndex;  ///< Opaque owner id (game decides the meaning).
    u32 layer = kLayerWorld;
    u32 id = 0;
};

/// Result of an axis-sweep query used by character movement.
struct AxisSweepResult {
    f32 distance = 0.0f;  ///< Allowed signed distance along the axis.
    bool blocked = false;
    Vec3 normal{};
    u32 user_data = kInvalidIndex;
};

class PhysicsWorld {
public:
    /// Adds a static box collider and returns its id.
    u32 AddStaticBox(const Aabb& bounds, u32 user_data = kInvalidIndex, u32 layer = kLayerWorld);

    /// Removes every collider (used when loading a new level).
    void Clear();

    usize collider_count() const { return colliders_.size(); }
    const std::vector<Collider>& colliders() const { return colliders_; }

    /// Closest hit along the ray. `layer_mask` filters colliders by bit mask.
    RayHit Raycast(const Ray& ray, f32 max_distance = 1000.0f, u32 layer_mask = kAllLayers) const;

    /// True when any collider overlaps the query box.
    bool OverlapAabb(const Aabb& query, u32& out_user_data, u32 layer_mask = kAllLayers) const;

    /// All collider user ids overlapping the query box.
    std::vector<u32> OverlapAabbAll(const Aabb& query, u32 layer_mask = kAllLayers) const;

    /// Sweeps `box` along one axis (0 = X, 1 = Y, 2 = Z) and returns the
    /// distance it can travel before touching a collider.
    AxisSweepResult SweepAxis(const Aabb& box, int axis, f32 distance, u32 layer_mask = kAllLayers) const;

    /// Removes a collider by id (returns false when not found).
    bool RemoveCollider(u32 id);

private:
    std::vector<Collider> colliders_;
    u32 next_id_ = 1;
};

}  // namespace valgo::physics
