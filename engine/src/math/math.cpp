#include "valgo/math/math.h"

namespace valgo {

bool Aabb::IntersectRay(const Vec3& origin, const Vec3& direction, f32 max_distance, f32& out_distance,
                        Vec3& out_normal) const {
    f32 t_min = 0.0f;
    f32 t_max = max_distance;
    Vec3 hit_normal{0.0f, 0.0f, 0.0f};

    for (int axis = 0; axis < 3; ++axis) {
        const f32 origin_axis = origin[axis];
        const f32 direction_axis = direction[axis];
        const f32 min_axis = min[axis];
        const f32 max_axis = max[axis];

        if (std::fabs(direction_axis) < 1.0e-8f) {
            // Ray is parallel to this slab: it must already be inside it.
            if (origin_axis < min_axis || origin_axis > max_axis) {
                return false;
            }
            continue;
        }

        const f32 inv = 1.0f / direction_axis;
        f32 t1 = (min_axis - origin_axis) * inv;
        f32 t2 = (max_axis - origin_axis) * inv;
        f32 normal_sign = -1.0f;
        if (t1 > t2) {
            const f32 swap = t1;
            t1 = t2;
            t2 = swap;
            normal_sign = 1.0f;
        }

        if (t1 > t_min) {
            t_min = t1;
            hit_normal = Vec3{0.0f, 0.0f, 0.0f};
            hit_normal[axis] = normal_sign;
        }
        if (t2 < t_max) {
            t_max = t2;
        }
        if (t_min > t_max) {
            return false;
        }
    }

    // Starting inside the box: report the exit point with the normal facing back
    // towards the ray origin so callers can push objects apart.
    if (t_min <= 0.0f) {
        return false;
    }
    out_distance = t_min;
    out_normal = hit_normal;
    return true;
}

}  // namespace valgo
