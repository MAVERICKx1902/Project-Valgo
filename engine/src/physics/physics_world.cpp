#include "valgo/physics/physics_world.h"

#include <algorithm>
#include <cmath>

#include "valgo/core/log.h"

namespace valgo::physics {
namespace {

constexpr const char* kCategory = "physics";
// Tolerance used when testing perpendicular overlap during an axis sweep.
// Surfaces the box is resting on (or touching) must not block horizontal
// movement, otherwise a character standing on the floor can never move.
constexpr f32 kContactSkin = 1.0e-3f;

}  // namespace

u32 PhysicsWorld::AddStaticBox(const Aabb& bounds, u32 user_data, u32 layer) {
    Collider collider;
    collider.bounds = bounds;
    collider.user_data = user_data;
    collider.layer = layer;
    collider.id = next_id_++;
    colliders_.push_back(collider);
    return collider.id;
}

void PhysicsWorld::Clear() {
    colliders_.clear();
    next_id_ = 1;
}

bool PhysicsWorld::RemoveCollider(u32 id) {
    for (auto it = colliders_.begin(); it != colliders_.end(); ++it) {
        if (it->id == id) {
            colliders_.erase(it);
            return true;
        }
    }
    return false;
}

RayHit PhysicsWorld::Raycast(const Ray& ray, f32 max_distance, u32 layer_mask) const {
    RayHit closest;
    closest.distance = max_distance;
    const Vec3 direction = Normalize(ray.direction);

    for (const Collider& collider : colliders_) {
        if ((collider.layer & layer_mask) == 0u) {
            continue;
        }
        f32 distance = 0.0f;
        Vec3 normal{};
        if (collider.bounds.IntersectRay(ray.origin, direction, closest.distance, distance, normal)) {
            if (distance < closest.distance) {
                closest.hit = true;
                closest.distance = distance;
                closest.point = ray.origin + direction * distance;
                closest.normal = normal;
                closest.user_data = collider.user_data;
            }
        }
    }
    return closest;
}

bool PhysicsWorld::OverlapAabb(const Aabb& query, u32& out_user_data, u32 layer_mask) const {
    for (const Collider& collider : colliders_) {
        if ((collider.layer & layer_mask) == 0u) {
            continue;
        }
        if (collider.bounds.Overlaps(query)) {
            out_user_data = collider.user_data;
            return true;
        }
    }
    return false;
}

std::vector<u32> PhysicsWorld::OverlapAabbAll(const Aabb& query, u32 layer_mask) const {
    std::vector<u32> result;
    for (const Collider& collider : colliders_) {
        if ((collider.layer & layer_mask) == 0u) {
            continue;
        }
        if (collider.bounds.Overlaps(query)) {
            result.push_back(collider.user_data);
        }
    }
    return result;
}

AxisSweepResult PhysicsWorld::SweepAxis(const Aabb& box, int axis, f32 distance, u32 layer_mask) const {
    AxisSweepResult result;
    result.distance = distance;
    if (axis < 0 || axis > 2 || std::fabs(distance) < 1.0e-8f) {
        return result;
    }

    for (const Collider& collider : colliders_) {
        if ((collider.layer & layer_mask) == 0u) {
            continue;
        }
        const Aabb& other = collider.bounds;

        // Only boxes that already overlap on the two perpendicular axes can
        // block this movement.
        bool overlaps_other_axes = true;
        for (int other_axis = 0; other_axis < 3; ++other_axis) {
            if (other_axis == axis) {
                continue;
            }
            if (box.max[other_axis] - kContactSkin <= other.min[other_axis] ||
                box.min[other_axis] + kContactSkin >= other.max[other_axis]) {
                overlaps_other_axes = false;
                break;
            }
        }
        if (!overlaps_other_axes) {
            continue;
        }

        if (distance > 0.0f) {
            const f32 gap = other.min[axis] - box.max[axis];
            if (gap < -kContactSkin) {
                continue;  // collider is behind the moving box
            }
            const f32 candidate = std::max(gap, 0.0f);
            if (candidate < result.distance) {
                result.distance = candidate;
                result.blocked = true;
                result.normal = Vec3{};
                result.normal[axis] = -1.0f;
                result.user_data = collider.user_data;
            }
        } else {
            const f32 gap = other.max[axis] - box.min[axis];
            if (gap > kContactSkin) {
                continue;  // collider is ahead of the moving box
            }
            const f32 candidate = std::min(gap, 0.0f);
            if (candidate > result.distance) {
                result.distance = candidate;
                result.blocked = true;
                result.normal = Vec3{};
                result.normal[axis] = 1.0f;
                result.user_data = collider.user_data;
            }
        }
    }

    if (result.blocked) {
        LogTrace(kCategory, "sweep axis {} blocked at {}", axis, result.distance);
    }
    return result;
}

}  // namespace valgo::physics
