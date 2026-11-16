// =============================================================================
// Project Valgo — engine camera.
//
// A camera is a pure data object: transform + lens + viewport. It is consumed by
// the renderer (view/projection) and by gameplay (screen rays for hit
// detection). It deliberately contains no game logic.
// =============================================================================
#pragma once

#include "valgo/math/math.h"

namespace valgo::render {

struct Plane {
    Vec3 normal{0.0f, 1.0f, 0.0f};
    f32 distance = 0.0f;  ///< Points with dot(normal, p) + distance >= 0 are inside.

    bool IsPointInside(const Vec3& point) const { return Dot(normal, point) + distance >= 0.0f; }
};

/// View frustum extracted from a view-projection matrix, used for culling.
struct Frustum {
    Plane planes[6];  ///< left, right, bottom, top, near, far

    static Frustum FromViewProjection(const Mat4& view_projection);
    bool Intersects(const Aabb& bounds) const;
    bool Contains(const Vec3& point) const;
};

class Camera {
public:
    Camera() = default;

    // --- Lens ---------------------------------------------------------------
    void SetPerspective(f32 vertical_fov_degrees, f32 aspect_ratio, f32 near_plane, f32 far_plane);
    void SetPerspectiveFov(f32 vertical_fov_degrees);
    void SetAspectRatio(f32 aspect_ratio);
    void SetClipPlanes(f32 near_plane, f32 far_plane);

    f32 vertical_fov_degrees() const { return vertical_fov_degrees_; }
    f32 aspect_ratio() const { return aspect_ratio_; }
    f32 near_plane() const { return near_plane_; }
    f32 far_plane() const { return far_plane_; }

    // --- Pose ---------------------------------------------------------------
    void SetPosition(const Vec3& position);
    void LookAt(const Vec3& target, const Vec3& up = {0.0f, 1.0f, 0.0f});
    void SetAngles(f32 pitch_radians, f32 yaw_radians, f32 roll_radians = 0.0f);

    const Vec3& position() const { return position_; }
    f32 pitch() const { return pitch_; }
    f32 yaw() const { return yaw_; }
    f32 roll() const { return roll_; }

    Vec3 Forward() const;
    Vec3 Right() const;
    Vec3 Up() const;

    /// World-space transform of the camera (position + orientation).
    Transform GetTransform() const;

    // --- Matrices -----------------------------------------------------------
    Mat4 ViewMatrix() const;
    Mat4 ProjectionMatrix() const;
    Mat4 ViewProjectionMatrix() const;
    Frustum BuildFrustum() const;

    // --- Screen space helpers ----------------------------------------------
    /// Converts pixel coordinates (origin top-left) into a world-space ray.
    Ray ScreenRay(f32 pixel_x, f32 pixel_y, f32 viewport_width, f32 viewport_height) const;

    /// Projects a world position into pixel coordinates. Returns false when the
    /// point is behind the camera. `out_depth` is in [0, 1].
    bool WorldToScreen(const Vec3& world_position, f32 viewport_width, f32 viewport_height,
                       Vec2& out_pixel, f32& out_depth) const;

private:
    Vec3 position_{0.0f, 1.7f, 0.0f};
    f32 pitch_ = 0.0f;
    f32 yaw_ = 0.0f;
    f32 roll_ = 0.0f;

    f32 vertical_fov_degrees_ = 70.0f;
    f32 aspect_ratio_ = 16.0f / 9.0f;
    f32 near_plane_ = 0.05f;
    f32 far_plane_ = 500.0f;
};

}  // namespace valgo::render
