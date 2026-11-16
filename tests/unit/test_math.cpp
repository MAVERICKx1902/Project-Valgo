// =============================================================================
// Math: vectors, quaternions, matrices, AABBs and camera projection.
// These are the conventions everything else depends on, so they are pinned here.
// =============================================================================
#include "../framework/test_framework.h"

#include "valgo/math/math.h"
#include "valgo/render/camera.h"

using namespace valgo;

namespace {
constexpr f32 kTolerance = 1.0e-4f;
}

// ---------------------------------------------------------------------------
// Scalars and vectors
// ---------------------------------------------------------------------------
VALGO_TEST("math", "scalar_helpers") {
    VALGO_CHECK_NEAR(Clamp(5.0f, 0.0f, 3.0f), 3.0f, kTolerance);
    VALGO_CHECK_NEAR(Clamp(-2.0f, -1.0f, 1.0f), -1.0f, kTolerance);
    VALGO_CHECK_NEAR(Saturate(1.5f), 1.0f, kTolerance);
    VALGO_CHECK_NEAR(Lerp(2.0f, 4.0f, 0.25f), 2.5f, kTolerance);
    VALGO_CHECK_NEAR(InverseLerp(0.0f, 10.0f, 4.0f), 0.4f, kTolerance);
    VALGO_CHECK_NEAR(MoveTowards(0.0f, 10.0f, 3.0f), 3.0f, kTolerance);
    VALGO_CHECK_NEAR(MoveTowards(9.0f, 10.0f, 3.0f), 10.0f, kTolerance);
    VALGO_CHECK_NEAR(Sign(-7.0f), -1.0f, kTolerance);
    VALGO_CHECK_NEAR(WrapAngle(kTwoPi + 0.5f), 0.5f, 1.0e-3f);
    VALGO_CHECK_NEAR(WrapAngle(-kTwoPi - 0.5f), -0.5f, 1.0e-3f);
}

VALGO_TEST("math", "vector_arithmetic") {
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 5.0f, 6.0f};
    const Vec3 sum = a + b;
    VALGO_CHECK_NEAR(sum.x, 5.0f, kTolerance);
    VALGO_CHECK_NEAR(sum.y, 7.0f, kTolerance);
    VALGO_CHECK_NEAR(sum.z, 9.0f, kTolerance);
    VALGO_CHECK_NEAR(Dot(a, b), 32.0f, kTolerance);

    const Vec3 cross = Cross(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    VALGO_CHECK_NEAR(cross.z, 1.0f, kTolerance);  // right-handed

    VALGO_CHECK_NEAR(Length(Vec3{3.0f, 4.0f, 0.0f}), 5.0f, kTolerance);
    VALGO_CHECK_NEAR(Length(Normalize(Vec3{0.0f, 0.0f, 7.0f})), 1.0f, kTolerance);
    VALGO_CHECK_NEAR(Length(ClampLength(Vec3{10.0f, 0.0f, 0.0f}, 2.0f)), 2.0f, kTolerance);
    VALGO_CHECK_NEAR(MaxComponent(Vec3{-1.0f, 4.0f, 2.0f}), 4.0f, kTolerance);
    VALGO_CHECK_NEAR(MinComponent(Vec3{-1.0f, 4.0f, 2.0f}), -1.0f, kTolerance);

    // Degenerate input must stay finite.
    const Vec3 zero_normal = Normalize(Vec3{});
    VALGO_CHECK(std::isfinite(zero_normal.x) && std::isfinite(zero_normal.y) && std::isfinite(zero_normal.z));

    const Vec3 reflected = Reflect(Vec3{1.0f, -1.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    VALGO_CHECK_NEAR(reflected.x, 1.0f, kTolerance);
    VALGO_CHECK_NEAR(reflected.y, 1.0f, kTolerance);
}

VALGO_TEST("math", "angles_and_directions_round_trip") {
    // Convention: yaw 0 looks towards -Z, positive pitch looks up.
    const Vec3 forward = ForwardFromEuler(0.0f, 0.0f);
    VALGO_CHECK_NEAR(forward.x, 0.0f, kTolerance);
    VALGO_CHECK_NEAR(forward.y, 0.0f, kTolerance);
    VALGO_CHECK_NEAR(forward.z, -1.0f, kTolerance);

    const Vec3 up = ForwardFromEuler(kHalfPi * 0.5f, 0.0f);
    VALGO_CHECK(up.y > 0.6f);

    const SphericalAngles angles = AnglesFromDirection(Normalize(Vec3{1.0f, 1.0f, -1.0f}));
    const Vec3 rebuilt = DirectionFromAngles(angles);
    VALGO_CHECK_NEAR(rebuilt.x, 1.0f / std::sqrt(3.0f), 1.0e-3f);
    VALGO_CHECK_NEAR(rebuilt.y, 1.0f / std::sqrt(3.0f), 1.0e-3f);
    VALGO_CHECK_NEAR(rebuilt.z, -1.0f / std::sqrt(3.0f), 1.0e-3f);

    const MovementBasis basis = MovementBasisFromYaw(0.0f);
    VALGO_CHECK_NEAR(basis.forward.z, -1.0f, kTolerance);
    VALGO_CHECK_NEAR(basis.right.x, 1.0f, kTolerance);
}

// ---------------------------------------------------------------------------
// Quaternions and transforms
// ---------------------------------------------------------------------------
VALGO_TEST("math", "quaternion_rotation") {
    const Quat identity = Quat::Identity();
    const Vec3 rotated = identity * Vec3{1.0f, 2.0f, 3.0f};
    VALGO_CHECK_NEAR(rotated.x, 1.0f, kTolerance);
    VALGO_CHECK_NEAR(rotated.y, 2.0f, kTolerance);
    VALGO_CHECK_NEAR(rotated.z, 3.0f, kTolerance);

    // 90 degrees about +Y maps -Z (forward) onto -X.
    const Quat yaw = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, kHalfPi);
    const Vec3 forward = yaw * Vec3{0.0f, 0.0f, -1.0f};
    VALGO_CHECK_NEAR(forward.x, -1.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(forward.y, 0.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(forward.z, 0.0f, 1.0e-3f);

    const Quat inverse = yaw.Conjugate();
    const Vec3 back = inverse * forward;
    VALGO_CHECK_NEAR(back.z, -1.0f, 1.0e-3f);

    const Quat composed = yaw * yaw;
    const Vec3 twice = composed * Vec3{0.0f, 0.0f, -1.0f};
    VALGO_CHECK_NEAR(twice.z, 1.0f, 1.0e-3f);

    const Quat slerped = Quat::Slerp(Quat::Identity(), yaw, 0.5f);
    const Vec3 halfway = slerped * Vec3{0.0f, 0.0f, -1.0f};
    VALGO_CHECK_NEAR(halfway.x, -std::sqrt(0.5f), 1.0e-3f);
    VALGO_CHECK_NEAR(halfway.z, -std::sqrt(0.5f), 1.0e-3f);

    const Quat normalized = Quat{1.0f, 2.0f, 3.0f, 4.0f}.Normalized();
    VALGO_CHECK_NEAR(std::sqrt(normalized.x * normalized.x + normalized.y * normalized.y +
                               normalized.z * normalized.z + normalized.w * normalized.w),
                     1.0f, 1.0e-3f);
}

VALGO_TEST("math", "quaternion_axis_angle_matches_direction") {
    const Vec3 direction = Normalize(Vec3{-0.4f, 0.2f, -0.9f});
    const Quat look = Quat::FromTo(Vec3{0.0f, 0.0f, -1.0f}, direction);
    const Vec3 result = look * Vec3{0.0f, 0.0f, -1.0f};
    VALGO_CHECK_NEAR(result.x, direction.x, 1.0e-3f);
    VALGO_CHECK_NEAR(result.y, direction.y, 1.0e-3f);
    VALGO_CHECK_NEAR(result.z, direction.z, 1.0e-3f);
}

VALGO_TEST("math", "transform_point_direction_hierarchy") {
    Transform parent;
    parent.position = Vec3{10.0f, 0.0f, 0.0f};
    parent.rotation = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, kHalfPi);
    parent.scale = Vec3{2.0f, 2.0f, 2.0f};

    const Vec3 world = parent.TransformPoint(Vec3{0.0f, 0.0f, -1.0f});
    VALGO_CHECK_NEAR(world.x, 8.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(world.y, 0.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(world.z, 0.0f, 1.0e-3f);

    const Transform identity;
    const Vec3 untouched = identity.TransformPoint(Vec3{1.0f, 2.0f, 3.0f});
    VALGO_CHECK_NEAR(untouched.y, 2.0f, kTolerance);

    const Vec3 forward = parent.Forward();
    VALGO_CHECK_NEAR(forward.x, -1.0f, 1.0e-3f);
}

VALGO_TEST("math", "matrices_compose_in_expected_order") {
    const Mat4 translate = Mat4::Translation(Vec3{0.0f, 0.0f, -5.0f});
    const Mat4 scale = Mat4::Scale(Vec3{2.0f, 2.0f, 2.0f});
    const Mat4 combined = translate * scale;
    const Vec3 point = combined.TransformPoint(Vec3{1.0f, 0.0f, 0.0f});
    VALGO_CHECK_NEAR(point.x, 2.0f, kTolerance);
    VALGO_CHECK_NEAR(point.z, -5.0f, kTolerance);

    const Mat4 inverse = combined.Inverse();
    const Vec3 restored = inverse.TransformPoint(point);
    VALGO_CHECK_NEAR(restored.x, 1.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(restored.z, 0.0f, 1.0e-3f);

    const Mat4 identity = combined * inverse;
    VALGO_CHECK_NEAR(identity.At(0, 0), 1.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(identity.At(1, 1), 1.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(identity.At(2, 2), 1.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(identity.At(3, 0), 0.0f, 1.0e-3f);

    // Column-major storage: translation lives in the last column.
    VALGO_CHECK_NEAR(translate.Translation().z, -5.0f, kTolerance);
    VALGO_CHECK_NEAR(translate.At(3, 2), -5.0f, kTolerance);
}

VALGO_TEST("math", "perspective_and_lookat_project_the_expected_points") {
    const Mat4 projection = Mat4::Perspective(Radians(70.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Vec4 near_point = projection * Vec4{0.0f, 0.0f, -0.1f, 1.0f};
    VALGO_CHECK_NEAR(near_point.z / near_point.w, 0.0f, 1.0e-3f);  // depth 0 at near plane
    const Vec4 far_point = projection * Vec4{0.0f, 0.0f, -100.0f, 1.0f};
    VALGO_CHECK_NEAR(far_point.z / far_point.w, 1.0f, 1.0e-3f);  // depth 1 at far plane

    const Mat4 view = Mat4::LookAt(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -10.0f},
                                   Vec3{0.0f, 1.0f, 0.0f});
    const Vec3 ahead = view.TransformPoint(Vec3{0.0f, 0.0f, -3.0f});
    VALGO_CHECK_NEAR(ahead.z, -3.0f, 1.0e-3f);  // right-handed: forward is -Z
}

// ---------------------------------------------------------------------------
// AABBs and rays
// ---------------------------------------------------------------------------
VALGO_TEST("math", "aabb_queries") {
    const Aabb box = Aabb::FromCenterExtents(Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 2.0f, 3.0f});
    VALGO_CHECK_NEAR(box.min.x, -1.0f, kTolerance);
    VALGO_CHECK_NEAR(box.max.y, 3.0f, kTolerance);
    VALGO_CHECK(box.Contains(Vec3{0.0f, 1.0f, 0.0f}));
    VALGO_CHECK(!box.Contains(Vec3{2.0f, 1.0f, 0.0f}));
    VALGO_CHECK(box.Overlaps(Aabb::FromCenterExtents(Vec3{0.5f, 1.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f})));
    VALGO_CHECK(!box.Overlaps(Aabb::FromCenterExtents(Vec3{5.0f, 1.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f})));

    f32 distance = 0.0f;
    Vec3 normal{};
    VALGO_REQUIRE(box.IntersectRay(Vec3{0.0f, 1.0f, 10.0f}, Vec3{0.0f, 0.0f, -1.0f}, 100.0f, distance, normal));
    VALGO_CHECK_NEAR(distance, 7.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(normal.z, 1.0f, 1.0e-3f);
    VALGO_CHECK(!box.IntersectRay(Vec3{0.0f, 1.0f, 10.0f}, Vec3{0.0f, 1.0f, 0.0f}, 100.0f, distance, normal));

    Aabb accumulated{Aabb::FromCenterExtents(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f})};
    accumulated.Encapsulate(Vec3{-1.0f, 2.0f, -3.0f});
    VALGO_CHECK_NEAR(accumulated.min.x, -1.0f, kTolerance);
    VALGO_CHECK_NEAR(accumulated.min.z, -3.0f, kTolerance);
    VALGO_CHECK_NEAR(accumulated.max.y, 2.0f, kTolerance);
    VALGO_CHECK_NEAR(accumulated.max.x, 0.0f, kTolerance);
}

VALGO_TEST("math", "ray_primitive_intersections") {
    const Ray ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    VALGO_CHECK_NEAR(IntersectRaySphere(ray, Vec3{0.0f, 0.0f, -5.0f}, 1.0f), 4.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(IntersectRaySphere(ray, Vec3{0.0f, 3.0f, -5.0f}, 1.0f), -1.0f, kTolerance);
    // Sphere behind the ray origin must miss.
    VALGO_CHECK_NEAR(IntersectRaySphere(ray, Vec3{0.0f, 0.0f, 5.0f}, 1.0f), -1.0f, kTolerance);

    VALGO_CHECK_NEAR(IntersectRayPlane(ray, Vec3{0.0f, 0.0f, 1.0f}, -4.0f), 4.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(IntersectRayPlane(ray, Vec3{0.0f, 1.0f, 0.0f}, 0.0f), -1.0f, kTolerance);

    VALGO_CHECK_NEAR(DistancePointToSegment(Vec3{1.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                                           Vec3{2.0f, 0.0f, 0.0f}),
                     1.0f, kTolerance);
    // A point on the segment has zero distance; past the end it clamps.
    VALGO_CHECK_NEAR(DistancePointToSegment(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                                           Vec3{2.0f, 0.0f, 0.0f}),
                     0.0f, kTolerance);
    VALGO_CHECK_NEAR(DistancePointToSegment(Vec3{5.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                                           Vec3{2.0f, 0.0f, 0.0f}),
                     3.0f, kTolerance);
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
VALGO_TEST("math", "camera_screen_ray_round_trip") {
    render::Camera camera;
    camera.SetPerspective(70.0f, 16.0f / 9.0f, 0.05f, 400.0f);
    camera.SetPosition(Vec3{0.0f, 1.7f, 0.0f});
    camera.SetAngles(0.0f, 0.0f);

    const Ray center = camera.ScreenRay(800.0f, 450.0f, 1600.0f, 900.0f);
    VALGO_CHECK_NEAR(center.direction.z, -1.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(center.direction.x, 0.0f, 1.0e-3f);

    // A world point in front of the camera projects and unprojects consistently.
    const Vec3 world{1.5f, 1.9f, -6.0f};
    Vec2 pixel{};
    f32 depth = 0.0f;
    VALGO_REQUIRE(camera.WorldToScreen(world, 1600.0f, 900.0f, pixel, depth));
    VALGO_CHECK(depth > 0.0f && depth < 1.0f);
    const Ray ray = camera.ScreenRay(pixel.x, pixel.y, 1600.0f, 900.0f);
    const Vec3 to_world = Normalize(world - camera.position());
    VALGO_CHECK_NEAR(ray.direction.x, to_world.x, 1.0e-3f);
    VALGO_CHECK_NEAR(ray.direction.y, to_world.y, 1.0e-3f);

    // Points behind the camera must not project.
    Vec2 behind{};
    f32 behind_depth = 0.0f;
    VALGO_CHECK(!camera.WorldToScreen(Vec3{0.0f, 1.7f, 5.0f}, 1600.0f, 900.0f, behind, behind_depth));
}

VALGO_TEST("math", "camera_pose_and_frustum") {
    render::Camera camera;
    camera.SetPerspective(60.0f, 1.0f, 0.1f, 100.0f);
    camera.SetPosition(Vec3{0.0f, 0.0f, 5.0f});
    camera.LookAt(Vec3{0.0f, 0.0f, 0.0f});
    VALGO_CHECK_NEAR(camera.Forward().z, -1.0f, 1.0e-3f);
    VALGO_CHECK_NEAR(camera.Forward().x, 0.0f, 1.0e-3f);

    const render::Frustum frustum = camera.BuildFrustum();
    VALGO_CHECK(frustum.Contains(Vec3{0.0f, 0.0f, 0.0f}));
    VALGO_CHECK(frustum.Intersects(Aabb::FromCenterExtents(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.5f, 0.5f, 0.5f})));
    VALGO_CHECK(!frustum.Contains(Vec3{0.0f, 0.0f, 50.0f}));      // behind the camera
    VALGO_CHECK(!frustum.Intersects(Aabb::FromCenterExtents(Vec3{300.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f})));

    camera.SetAngles(0.5f, 0.0f);
    VALGO_CHECK_NEAR(camera.pitch(), 0.5f, 1.0e-4f);
    VALGO_CHECK(camera.Forward().y > 0.4f);  // positive pitch looks up
}
