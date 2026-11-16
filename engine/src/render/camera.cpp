#include "valgo/render/camera.h"

namespace valgo::render {

void Camera::SetPerspective(f32 vertical_fov_degrees, f32 aspect_ratio, f32 near_plane, f32 far_plane) {
    SetPerspectiveFov(vertical_fov_degrees);
    SetAspectRatio(aspect_ratio);
    SetClipPlanes(near_plane, far_plane);
}

void Camera::SetPerspectiveFov(f32 vertical_fov_degrees) {
    vertical_fov_degrees_ = Clamp(vertical_fov_degrees, 1.0f, 179.0f);
}

void Camera::SetAspectRatio(f32 aspect_ratio) {
    if (aspect_ratio > kEpsilon) {
        aspect_ratio_ = aspect_ratio;
    }
}

void Camera::SetClipPlanes(f32 near_plane, f32 far_plane) {
    near_plane_ = std::max(near_plane, 1.0e-4f);
    far_plane_ = std::max(far_plane, near_plane_ + 1.0e-3f);
}

void Camera::SetPosition(const Vec3& position) { position_ = position; }

void Camera::LookAt(const Vec3& target, const Vec3& up) {
    VALGO_UNUSED(up);
    const Vec3 direction = Normalize(target - position_);
    const SphericalAngles angles = AnglesFromDirection(direction);
    pitch_ = angles.pitch;
    yaw_ = angles.yaw;
    roll_ = 0.0f;
}

void Camera::SetAngles(f32 pitch_radians, f32 yaw_radians, f32 roll_radians) {
    pitch_ = Clamp(pitch_radians, -kHalfPi + 0.001f, kHalfPi - 0.001f);
    yaw_ = WrapAngle(yaw_radians);
    roll_ = WrapAngle(roll_radians);
}

Vec3 Camera::Forward() const { return ForwardFromEuler(pitch_, yaw_); }

Vec3 Camera::Right() const {
    const Vec3 forward = Forward();
    return Normalize(Cross(forward, Vec3{0.0f, 1.0f, 0.0f}));
}

Vec3 Camera::Up() const { return Normalize(Cross(Right(), Forward())); }

Transform Camera::GetTransform() const {
    Transform transform;
    transform.position = position_;
    transform.rotation = Quat::FromEuler(pitch_, yaw_, roll_);
    return transform;
}

Mat4 Camera::ViewMatrix() const {
    const Mat4 rotation = Mat4::Rotation(Quat::FromEuler(pitch_, yaw_, roll_));
    const Mat4 camera_to_world = Mat4::Translation(position_) * rotation;
    return camera_to_world.InverseAffine();
}

Mat4 Camera::ProjectionMatrix() const {
    return Mat4::Perspective(Radians(vertical_fov_degrees_), aspect_ratio_, near_plane_, far_plane_);
}

Mat4 Camera::ViewProjectionMatrix() const { return ProjectionMatrix() * ViewMatrix(); }

Frustum Camera::BuildFrustum() const { return Frustum::FromViewProjection(ViewProjectionMatrix()); }

Ray Camera::ScreenRay(f32 pixel_x, f32 pixel_y, f32 viewport_width, f32 viewport_height) const {
    const f32 ndc_x = (pixel_x / std::max(viewport_width, 1.0f)) * 2.0f - 1.0f;
    const f32 ndc_y = 1.0f - (pixel_y / std::max(viewport_height, 1.0f)) * 2.0f;

    const Mat4 inverse_view_projection = ViewProjectionMatrix().Inverse();
    const Vec4 near_point = inverse_view_projection * Vec4{ndc_x, ndc_y, 0.0f, 1.0f};
    const Vec4 far_point = inverse_view_projection * Vec4{ndc_x, ndc_y, 1.0f, 1.0f};
    if (std::fabs(near_point.w) < kEpsilon || std::fabs(far_point.w) < kEpsilon) {
        return Ray{position_, Forward()};
    }

    const Vec3 near_world{near_point.x / near_point.w, near_point.y / near_point.w, near_point.z / near_point.w};
    const Vec3 far_world{far_point.x / far_point.w, far_point.y / far_point.w, far_point.z / far_point.w};

    Ray ray;
    ray.origin = near_world;
    ray.direction = Normalize(far_world - near_world);
    return ray;
}

bool Camera::WorldToScreen(const Vec3& world_position, f32 viewport_width, f32 viewport_height,
                           Vec2& out_pixel, f32& out_depth) const {
    const Vec4 clip = ViewProjectionMatrix() * Vec4{world_position, 1.0f};
    if (clip.w <= kEpsilon) {
        return false;
    }
    const f32 ndc_x = clip.x / clip.w;
    const f32 ndc_y = clip.y / clip.w;
    out_depth = clip.z / clip.w;
    out_pixel.x = (ndc_x * 0.5f + 0.5f) * viewport_width;
    out_pixel.y = (0.5f - ndc_y * 0.5f) * viewport_height;
    return out_depth >= 0.0f && out_depth <= 1.0f;
}

Frustum Frustum::FromViewProjection(const Mat4& view_projection) {
    // Gribb/Hartmann plane extraction for column-major matrices and a [0,1]
    // depth range: row4 +/- rowN for the lateral planes, row3 for near, row4-row3
    // for far.
    Frustum frustum;

    const Vec4 row0 = view_projection.Row(0);
    const Vec4 row1 = view_projection.Row(1);
    const Vec4 row2 = view_projection.Row(2);
    const Vec4 row3 = view_projection.Row(3);

    auto make_plane = [](const Vec4& coefficients) {
        Plane plane;
        plane.normal = Normalize(coefficients.xyz());
        plane.distance = coefficients.w / std::max(Length(coefficients.xyz()), kEpsilon);
        return plane;
    };

    frustum.planes[0] = make_plane(Vec4{row3.x + row0.x, row3.y + row0.y, row3.z + row0.z, row3.w + row0.w});
    frustum.planes[1] = make_plane(Vec4{row3.x - row0.x, row3.y - row0.y, row3.z - row0.z, row3.w - row0.w});
    frustum.planes[2] = make_plane(Vec4{row3.x + row1.x, row3.y + row1.y, row3.z + row1.z, row3.w + row1.w});
    frustum.planes[3] = make_plane(Vec4{row3.x - row1.x, row3.y - row1.y, row3.z - row1.z, row3.w - row1.w});
    frustum.planes[4] = make_plane(row2);
    frustum.planes[5] = make_plane(Vec4{row3.x - row2.x, row3.y - row2.y, row3.z - row2.z, row3.w - row2.w});
    return frustum;
}

bool Frustum::Intersects(const Aabb& bounds) const {
    for (const Plane& plane : planes) {
        // Pick the AABB corner furthest along the plane normal; if even that is
        // outside, the whole box is outside.
        const Vec3 positive_vertex{plane.normal.x >= 0.0f ? bounds.max.x : bounds.min.x,
                                   plane.normal.y >= 0.0f ? bounds.max.y : bounds.min.y,
                                   plane.normal.z >= 0.0f ? bounds.max.z : bounds.min.z};
        if (!plane.IsPointInside(positive_vertex)) {
            return false;
        }
    }
    return true;
}

bool Frustum::Contains(const Vec3& point) const {
    for (const Plane& plane : planes) {
        if (!plane.IsPointInside(point)) {
            return false;
        }
    }
    return true;
}

}  // namespace valgo::render
