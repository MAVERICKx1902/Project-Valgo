// =============================================================================
// Project Valgo — engine math.
//
// Conventions (fixed engine-wide, documented in docs/ARCHITECTURE.md):
//   * Right-handed world space, +Y up.
//   * Yaw 0 looks towards -Z. Positive yaw turns left (counter-clockwise seen
//     from above). Pitch is positive when looking up.
//   * Column-major matrices with column vectors: v' = M * v. Memory layout is
//     m[column * 4 + row], which uploads directly to a column-major HLSL
//     constant buffer and is used with mul(matrix, vector) in shaders.
//   * Depth range is [0, 1] (Direct3D convention) so the same projection matrix
//     feeds both the D3D12 backend and the software rasteriser.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "valgo/core/types.h"

namespace valgo {

inline constexpr f32 kPi = 3.14159265358979323846f;
inline constexpr f32 kTwoPi = 6.28318530717958647692f;
inline constexpr f32 kHalfPi = 1.57079632679489661923f;
inline constexpr f32 kEpsilon = 1.0e-6f;
inline constexpr f32 kDegToRad = kPi / 180.0f;
inline constexpr f32 kRadToDeg = 180.0f / kPi;

constexpr f32 Radians(f32 degrees) { return degrees * kDegToRad; }
constexpr f32 Degrees(f32 radians) { return radians * kRadToDeg; }

template <typename T>
constexpr T Clamp(T value, T low, T high) {
    return value < low ? low : (value > high ? high : value);
}
constexpr f32 Saturate(f32 value) { return Clamp(value, 0.0f, 1.0f); }
constexpr f32 Lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
constexpr f32 InverseLerp(f32 a, f32 b, f32 value) {
    return (b - a) != 0.0f ? (value - a) / (b - a) : 0.0f;
}
constexpr f32 SmoothStep(f32 edge0, f32 edge1, f32 value) {
    const f32 t = Saturate(InverseLerp(edge0, edge1, value));
    return t * t * (3.0f - 2.0f * t);
}
constexpr f32 Sign(f32 value) { return value < 0.0f ? -1.0f : (value > 0.0f ? 1.0f : 0.0f); }

inline f32 MoveTowards(f32 current, f32 target, f32 max_delta) {
    const f32 delta = target - current;
    if (std::fabs(delta) <= max_delta) {
        return target;
    }
    return current + Sign(delta) * max_delta;
}

// -----------------------------------------------------------------------------
// Colour space
//
// Colours authored by tools, artists and game code are sRGB (the space in which
// they were picked). Lighting is done in linear space, and the swap chain's
// sRGB format encodes the final image. These two functions are the single
// definition of that transfer function for every backend.
// -----------------------------------------------------------------------------
inline f32 SrgbToLinear(f32 channel) {
    const f32 c = Clamp(channel, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline f32 LinearToSrgb(f32 channel) {
    const f32 c = Clamp(channel, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

/// Wraps an angle in radians into (-pi, pi].
inline f32 WrapAngle(f32 radians) {
    const f32 wrapped = std::fmod(radians + kPi, kTwoPi);
    return (wrapped < 0.0f ? wrapped + kTwoPi : wrapped) - kPi;
}

inline f32 MoveTowardsAngle(f32 current, f32 target, f32 max_delta) {
    return current + MoveTowards(0.0f, WrapAngle(target - current), max_delta);
}

constexpr bool ApproximatelyEqual(f32 a, f32 b, f32 epsilon = 1.0e-5f) {
    return std::fabs(a - b) <= epsilon;
}

// =============================================================================
// Vec2
// =============================================================================
struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(f32 in_x, f32 in_y) : x(in_x), y(in_y) {}

    constexpr Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    constexpr Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    constexpr Vec2 operator*(f32 scalar) const { return {x * scalar, y * scalar}; }
    constexpr Vec2 operator/(f32 scalar) const { return {x / scalar, y / scalar}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    Vec2& operator*=(f32 scalar) { x *= scalar; y *= scalar; return *this; }
    constexpr bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
};

inline constexpr f32 Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline f32 Length(const Vec2& v) { return std::sqrt(Dot(v, v)); }
inline Vec2 Normalize(const Vec2& v) {
    const f32 length = Length(v);
    return length > kEpsilon ? v / length : Vec2{};
}
inline constexpr Vec2 Lerp(const Vec2& a, const Vec2& b, f32 t) { return a + (b - a) * t; }

// =============================================================================
// Vec3
// =============================================================================
struct Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(f32 in_x, f32 in_y, f32 in_z) : x(in_x), y(in_y), z(in_z) {}
    explicit constexpr Vec3(f32 scalar) : x(scalar), y(scalar), z(scalar) {}

    constexpr Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    constexpr Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    constexpr Vec3 operator*(const Vec3& other) const { return {x * other.x, y * other.y, z * other.z}; }
    constexpr Vec3 operator/(const Vec3& other) const { return {x / other.x, y / other.y, z / other.z}; }
    constexpr Vec3 operator*(f32 scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    constexpr Vec3 operator/(f32 scalar) const { return {x / scalar, y / scalar, z / scalar}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vec3& operator*=(f32 scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
    Vec3& operator/=(f32 scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }
    constexpr bool operator==(const Vec3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    constexpr bool operator!=(const Vec3& other) const { return !(*this == other); }

    constexpr f32 operator[](int index) const { return index == 0 ? x : (index == 1 ? y : z); }
    constexpr f32& operator[](int index) { return index == 0 ? x : (index == 1 ? y : z); }
};

inline constexpr Vec3 operator*(f32 scalar, const Vec3& v) { return v * scalar; }
inline constexpr f32 Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline constexpr Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline constexpr f32 LengthSquared(const Vec3& v) { return Dot(v, v); }
inline f32 Length(const Vec3& v) { return std::sqrt(LengthSquared(v)); }
inline Vec3 Normalize(const Vec3& v) {
    const f32 length = Length(v);
    return length > kEpsilon ? v / length : Vec3{};
}
inline constexpr Vec3 Lerp(const Vec3& a, const Vec3& b, f32 t) { return a + (b - a) * t; }
inline constexpr Vec3 Min(const Vec3& a, const Vec3& b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline constexpr Vec3 Max(const Vec3& a, const Vec3& b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
inline constexpr Vec3 Abs(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }
inline constexpr f32 MaxComponent(const Vec3& v) { return std::max(v.x, std::max(v.y, v.z)); }
inline constexpr f32 MinComponent(const Vec3& v) { return std::min(v.x, std::min(v.y, v.z)); }

inline Vec3 SrgbToLinear(const Vec3& srgb) {
    return {SrgbToLinear(srgb.x), SrgbToLinear(srgb.y), SrgbToLinear(srgb.z)};
}

inline Vec3 LinearToSrgb(const Vec3& linear) {
    return {LinearToSrgb(linear.x), LinearToSrgb(linear.y), LinearToSrgb(linear.z)};
}

/// Luminance of a linear RGB colour (Rec. 709 weights).
inline f32 Luminance(const Vec3& linear) {
    return Dot(linear, Vec3{0.2126f, 0.7152f, 0.0722f});
}

inline Vec3 ClampLength(const Vec3& v, f32 max_length) {
    const f32 length = Length(v);
    if (length <= max_length || length <= kEpsilon) {
        return v;
    }
    return v * (max_length / length);
}

inline Vec3 MoveTowards(const Vec3& current, const Vec3& target, f32 max_delta) {
    const Vec3 delta = target - current;
    const f32 distance = Length(delta);
    if (distance <= max_delta || distance <= kEpsilon) {
        return target;
    }
    return current + delta * (max_delta / distance);
}

inline constexpr Vec3 Reflect(const Vec3& direction, const Vec3& normal) {
    return direction - normal * (2.0f * Dot(direction, normal));
}

/// Any unit vector perpendicular to `axis` (used to build orthonormal bases).
inline Vec3 AnyPerpendicular(const Vec3& axis) {
    const Vec3 helper = std::fabs(axis.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    return Normalize(Cross(axis, helper));
}

// =============================================================================
// Vec4
// =============================================================================
struct Vec4 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(f32 in_x, f32 in_y, f32 in_z, f32 in_w) : x(in_x), y(in_y), z(in_z), w(in_w) {}
    constexpr Vec4(const Vec3& xyz, f32 in_w) : x(xyz.x), y(xyz.y), z(xyz.z), w(in_w) {}
    explicit constexpr Vec4(f32 scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}

    constexpr Vec3 xyz() const { return {x, y, z}; }

    constexpr Vec4 operator+(const Vec4& other) const {
        return {x + other.x, y + other.y, z + other.z, w + other.w};
    }
    constexpr Vec4 operator-(const Vec4& other) const {
        return {x - other.x, y - other.y, z - other.z, w - other.w};
    }
    constexpr Vec4 operator*(f32 scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
    constexpr bool operator==(const Vec4& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
};

inline constexpr f32 Dot(const Vec4& a, const Vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
inline constexpr Vec4 Lerp(const Vec4& a, const Vec4& b, f32 t) { return a + (b - a) * t; }

// =============================================================================
// Quaternion (unit, right-handed, rotates column vectors: v' = q * v * q^-1)
// =============================================================================
struct Quat {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(f32 in_x, f32 in_y, f32 in_z, f32 in_w) : x(in_x), y(in_y), z(in_z), w(in_w) {}

    static constexpr Quat Identity() { return {}; }

    static Quat FromAxisAngle(const Vec3& axis, f32 radians) {
        const Vec3 unit = Normalize(axis);
        const f32 half = radians * 0.5f;
        const f32 s = std::sin(half);
        return {unit.x * s, unit.y * s, unit.z * s, std::cos(half)};
    }

    /// Yaw (about +Y) is applied first, then pitch (about +X), then roll (+Z).
    static Quat FromEuler(f32 pitch_radians, f32 yaw_radians, f32 roll_radians) {
        const Quat q_yaw = FromAxisAngle({0.0f, 1.0f, 0.0f}, yaw_radians);
        const Quat q_pitch = FromAxisAngle({1.0f, 0.0f, 0.0f}, pitch_radians);
        const Quat q_roll = FromAxisAngle({0.0f, 0.0f, 1.0f}, roll_radians);
        return q_yaw * q_pitch * q_roll;
    }

    static Quat FromTo(const Vec3& from, const Vec3& to) {
        const Vec3 a = Normalize(from);
        const Vec3 b = Normalize(to);
        const f32 d = Dot(a, b);
        if (d >= 1.0f - 1.0e-6f) {
            return Identity();
        }
        if (d <= -1.0f + 1.0e-6f) {
            return FromAxisAngle(AnyPerpendicular(a), kPi);
        }
        const Vec3 axis = Cross(a, b);
        const f32 angle = std::acos(Clamp(d, -1.0f, 1.0f));
        return FromAxisAngle(axis, angle);
    }

    constexpr Quat operator*(const Quat& other) const {
        return {w * other.x + x * other.w + y * other.z - z * other.y,
                w * other.y - x * other.z + y * other.w + z * other.x,
                w * other.z + x * other.y - y * other.x + z * other.w,
                w * other.w - x * other.x - y * other.y - z * other.z};
    }

    constexpr Vec3 operator*(const Vec3& v) const {
        const Vec3 q_vec{x, y, z};
        const Vec3 t = Cross(q_vec, v) * 2.0f;
        return v + t * w + Cross(q_vec, t);
    }

    constexpr Quat Conjugate() const { return {-x, -y, -z, w}; }

    Quat Normalized() const {
        const f32 length = std::sqrt(x * x + y * y + z * z + w * w);
        if (length <= kEpsilon) {
            return Identity();
        }
        return {x / length, y / length, z / length, w / length};
    }

    constexpr Vec3 RotateVector(const Vec3& v) const { return (*this) * v; }

    static Quat Slerp(const Quat& a, const Quat& b, f32 t);
};

/// Converts an Euler triple (pitch, yaw, roll in radians) into a forward vector.
inline Vec3 ForwardFromEuler(f32 pitch_radians, f32 yaw_radians) {
    const f32 cp = std::cos(pitch_radians);
    return {-std::sin(yaw_radians) * cp, std::sin(pitch_radians), -std::cos(yaw_radians) * cp};
}

inline Quat Slerp(const Quat& a, const Quat& b, f32 t) { return Quat::Slerp(a, b, t); }

inline Quat Quat::Slerp(const Quat& a, const Quat& b, f32 t) {
    f32 cos_omega = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat end = b;
    if (cos_omega < 0.0f) {
        cos_omega = -cos_omega;
        end = {-b.x, -b.y, -b.z, -b.w};
    }
    if (cos_omega > 0.9995f) {
        return Quat{a.x + (end.x - a.x) * t, a.y + (end.y - a.y) * t, a.z + (end.z - a.z) * t,
                    a.w + (end.w - a.w) * t}
            .Normalized();
    }
    const f32 omega = std::acos(Clamp(cos_omega, -1.0f, 1.0f));
    const f32 sin_omega = std::sin(omega);
    const f32 w0 = std::sin((1.0f - t) * omega) / sin_omega;
    const f32 w1 = std::sin(t * omega) / sin_omega;
    return Quat{a.x * w0 + end.x * w1, a.y * w0 + end.y * w1, a.z * w0 + end.z * w1,
                a.w * w0 + end.w * w1}
        .Normalized();
}

// =============================================================================
// Mat4 — column-major, m[column * 4 + row]
// =============================================================================
struct Mat4 {
    f32 m[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 1.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 1.0f};

    constexpr Mat4() = default;

    static constexpr Mat4 Identity() { return Mat4{}; }
    static constexpr Mat4 Zero() {
        Mat4 out;
        for (int i = 0; i < 16; ++i) {
            out.m[i] = 0.0f;
        }
        return out;
    }

    constexpr f32& At(int column, int row) { return m[column * 4 + row]; }
    constexpr f32 At(int column, int row) const { return m[column * 4 + row]; }

    Vec4 Column(int column) const {
        return {m[column * 4 + 0], m[column * 4 + 1], m[column * 4 + 2], m[column * 4 + 3]};
    }
    Vec4 Row(int row) const {
        return {m[0 * 4 + row], m[1 * 4 + row], m[2 * 4 + row], m[3 * 4 + row]};
    }
    Vec3 Translation() const { return {m[12], m[13], m[14]}; }

    static constexpr Mat4 Translation(const Vec3& t) {
        Mat4 out;
        out.m[12] = t.x;
        out.m[13] = t.y;
        out.m[14] = t.z;
        return out;
    }

    static constexpr Mat4 Scale(const Vec3& s) {
        Mat4 out;
        out.m[0] = s.x;
        out.m[5] = s.y;
        out.m[10] = s.z;
        return out;
    }

    static constexpr Mat4 Scale(f32 uniform_scale) { return Scale({uniform_scale, uniform_scale, uniform_scale}); }

    static Mat4 RotationX(f32 radians) {
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        Mat4 out;
        out.m[5] = c;
        out.m[6] = s;
        out.m[9] = -s;
        out.m[10] = c;
        return out;
    }

    static Mat4 RotationY(f32 radians) {
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        Mat4 out;
        out.m[0] = c;
        out.m[2] = -s;
        out.m[8] = s;
        out.m[10] = c;
        return out;
    }

    static Mat4 RotationZ(f32 radians) {
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        Mat4 out;
        out.m[0] = c;
        out.m[1] = s;
        out.m[4] = -s;
        out.m[5] = c;
        return out;
    }

    static Mat4 Rotation(const Quat& q) {
        const f32 xx = q.x * q.x;
        const f32 yy = q.y * q.y;
        const f32 zz = q.z * q.z;
        const f32 xy = q.x * q.y;
        const f32 xz = q.x * q.z;
        const f32 yz = q.y * q.z;
        const f32 wx = q.w * q.x;
        const f32 wy = q.w * q.y;
        const f32 wz = q.w * q.z;

        Mat4 out;
        out.m[0] = 1.0f - 2.0f * (yy + zz);
        out.m[1] = 2.0f * (xy + wz);
        out.m[2] = 2.0f * (xz - wy);
        out.m[3] = 0.0f;

        out.m[4] = 2.0f * (xy - wz);
        out.m[5] = 1.0f - 2.0f * (xx + zz);
        out.m[6] = 2.0f * (yz + wx);
        out.m[7] = 0.0f;

        out.m[8] = 2.0f * (xz + wy);
        out.m[9] = 2.0f * (yz - wx);
        out.m[10] = 1.0f - 2.0f * (xx + yy);
        out.m[11] = 0.0f;

        out.m[12] = 0.0f;
        out.m[13] = 0.0f;
        out.m[14] = 0.0f;
        out.m[15] = 1.0f;
        return out;
    }

    static Mat4 TRS(const Vec3& translation, const Quat& rotation, const Vec3& scale) {
        Mat4 out = Rotation(rotation);
        for (int row = 0; row < 3; ++row) {
            out.m[0 * 4 + row] *= scale.x;
            out.m[1 * 4 + row] *= scale.y;
            out.m[2 * 4 + row] *= scale.z;
        }
        out.m[12] = translation.x;
        out.m[13] = translation.y;
        out.m[14] = translation.z;
        return out;
    }

    /// Right-handed perspective projection with depth in [0, 1].
    static Mat4 Perspective(f32 vertical_fov_radians, f32 aspect_ratio, f32 near_plane, f32 far_plane) {
        const f32 f = 1.0f / std::tan(vertical_fov_radians * 0.5f);
        Mat4 out = Zero();
        out.m[0] = f / aspect_ratio;
        out.m[5] = f;
        out.m[10] = far_plane / (near_plane - far_plane);
        out.m[11] = -1.0f;
        out.m[14] = (near_plane * far_plane) / (near_plane - far_plane);
        return out;
    }

    /// Right-handed orthographic projection with depth in [0, 1].
    static Mat4 Orthographic(f32 width, f32 height, f32 near_plane, f32 far_plane) {
        Mat4 out;
        out.m[0] = 2.0f / width;
        out.m[5] = 2.0f / height;
        out.m[10] = 1.0f / (near_plane - far_plane);
        out.m[12] = 0.0f;
        out.m[13] = 0.0f;
        out.m[14] = near_plane / (near_plane - far_plane);
        out.m[15] = 1.0f;
        return out;
    }

    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        const Vec3 forward = Normalize(target - eye);          // camera looks down -Z
        const Vec3 right = Normalize(Cross(forward, up));
        const Vec3 true_up = Cross(right, forward);

        Mat4 out;
        out.m[0] = right.x;
        out.m[4] = right.y;
        out.m[8] = right.z;
        out.m[1] = true_up.x;
        out.m[5] = true_up.y;
        out.m[9] = true_up.z;
        out.m[2] = -forward.x;
        out.m[6] = -forward.y;
        out.m[10] = -forward.z;
        out.m[12] = -Dot(right, eye);
        out.m[13] = -Dot(true_up, eye);
        out.m[14] = Dot(forward, eye);
        out.m[15] = 1.0f;
        return out;
    }

    Mat4 operator*(const Mat4& rhs) const {
        Mat4 out;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                f32 sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += At(k, row) * rhs.At(column, k);
                }
                out.At(column, row) = sum;
            }
        }
        return out;
    }

    Vec4 operator*(const Vec4& v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
                m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
    }

    Vec3 TransformPoint(const Vec3& p) const {
        const Vec4 result = (*this) * Vec4{p, 1.0f};
        if (std::fabs(result.w) > kEpsilon && std::fabs(result.w - 1.0f) > kEpsilon) {
            return {result.x / result.w, result.y / result.w, result.z / result.w};
        }
        return {result.x, result.y, result.z};
    }

    Vec3 TransformVector(const Vec3& v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z,
                m[1] * v.x + m[5] * v.y + m[9] * v.z,
                m[2] * v.x + m[6] * v.y + m[10] * v.z};
    }

    Mat4 Transposed() const {
        Mat4 out;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                out.At(column, row) = At(row, column);
            }
        }
        return out;
    }

    /// General 4x4 inverse (used rarely; prefer InverseAffine for transforms).
    Mat4 Inverse() const {
        const f32* a = m;
        f32 inv[16];

        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
                 a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
                 a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
                 a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
                  a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
                 a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
                 a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
                 a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
                  a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] +
                 a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
                 a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
                  a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
                  a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
                 a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] +
                 a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] -
                  a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] +
                  a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

        f32 det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (std::fabs(det) < 1.0e-12f) {
            return Identity();
        }
        det = 1.0f / det;

        Mat4 out;
        for (int i = 0; i < 16; ++i) {
            out.m[i] = inv[i] * det;
        }
        return out;
    }

    /// Inverse of a rigid transform with uniform (or no) scaling.
    Mat4 InverseAffine() const {
        const Vec3 translation = Translation();
        const Vec3 x_axis{m[0], m[1], m[2]};
        const Vec3 y_axis{m[4], m[5], m[6]};
        const Vec3 z_axis{m[8], m[9], m[10]};

        const f32 sx = LengthSquared(x_axis);
        const f32 sy = LengthSquared(y_axis);
        const f32 sz = LengthSquared(z_axis);

        Mat4 out;
        out.m[0] = x_axis.x / (sx > kEpsilon ? sx : 1.0f);
        out.m[1] = y_axis.x / (sy > kEpsilon ? sy : 1.0f);
        out.m[2] = z_axis.x / (sz > kEpsilon ? sz : 1.0f);
        out.m[4] = x_axis.y / (sx > kEpsilon ? sx : 1.0f);
        out.m[5] = y_axis.y / (sy > kEpsilon ? sy : 1.0f);
        out.m[6] = z_axis.y / (sz > kEpsilon ? sz : 1.0f);
        out.m[8] = x_axis.z / (sx > kEpsilon ? sx : 1.0f);
        out.m[9] = y_axis.z / (sy > kEpsilon ? sy : 1.0f);
        out.m[10] = z_axis.z / (sz > kEpsilon ? sz : 1.0f);
        const Vec3 inverse_translation = -(out.TransformVector(translation));
        out.m[12] = inverse_translation.x;
        out.m[13] = inverse_translation.y;
        out.m[14] = inverse_translation.z;
        out.m[15] = 1.0f;
        return out;
    }
};

// =============================================================================
// Transform — position / rotation / scale, the engine's standard local pose.
// =============================================================================
struct Transform {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    constexpr Transform() = default;
    explicit Transform(const Vec3& translation) : position(translation) {}

    Mat4 ToMatrix() const { return Mat4::TRS(position, rotation, scale); }

    Vec3 TransformPoint(const Vec3& local_point) const { return ToMatrix().TransformPoint(local_point); }
    Vec3 TransformDirection(const Vec3& local_direction) const { return rotation * local_direction; }

    /// Forward (+Z in local space is *not* used: engine objects face -Z).
    Vec3 Forward() const { return rotation * Vec3{0.0f, 0.0f, -1.0f}; }
    Vec3 Right() const { return rotation * Vec3{1.0f, 0.0f, 0.0f}; }
    Vec3 Up() const { return rotation * Vec3{0.0f, 1.0f, 0.0f}; }

    static Transform Lerp(const Transform& a, const Transform& b, f32 t) {
        Transform out;
        out.position = valgo::Lerp(a.position, b.position, t);
        out.rotation = Quat::Slerp(a.rotation, b.rotation, t);
        out.scale = valgo::Lerp(a.scale, b.scale, t);
        return out;
    }
};

// =============================================================================
// Axis-aligned bounding box
// =============================================================================
struct Aabb {
    Vec3 min{0.0f, 0.0f, 0.0f};
    Vec3 max{0.0f, 0.0f, 0.0f};

    constexpr Aabb() = default;
    constexpr Aabb(const Vec3& in_min, const Vec3& in_max) : min(in_min), max(in_max) {}

    static constexpr Aabb FromCenterExtents(const Vec3& center, const Vec3& extents) {
        return {center - extents, center + extents};
    }

    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Extents() const { return (max - min) * 0.5f; }
    Vec3 Size() const { return max - min; }

    bool Contains(const Vec3& point) const {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    bool Overlaps(const Aabb& other) const {
        return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y &&
               max.y >= other.min.y && min.z <= other.max.z && max.z >= other.min.z;
    }

    Aabb Expanded(f32 margin) const { return Expanded(Vec3{margin, margin, margin}); }
    Aabb Expanded(const Vec3& margin) const { return {min - margin, max + margin}; }

    void Encapsulate(const Vec3& point) {
        min = Min(min, point);
        max = Max(max, point);
    }

    /// Ray/AABB intersection using the slab method.
    /// `direction` must be normalised for `t` values to be distances.
    bool IntersectRay(const Vec3& origin, const Vec3& direction, f32 max_distance, f32& out_distance,
                      Vec3& out_normal) const;
};

// =============================================================================
// Ray / hit results
// =============================================================================
struct Ray {
    Vec3 origin;
    Vec3 direction;  ///< Expected to be normalised.
};

struct RayHit {
    bool hit = false;
    f32 distance = 0.0f;
    Vec3 point{};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    u32 user_data = kInvalidIndex;  ///< Opaque id supplied by the collider owner.
};

/// Ray/sphere intersection. Returns the distance along the ray or -1.
inline f32 IntersectRaySphere(const Ray& ray, const Vec3& center, f32 radius) {
    const Vec3 to_center = center - ray.origin;
    const f32 projection = Dot(to_center, ray.direction);
    const f32 distance_squared = LengthSquared(to_center) - projection * projection;
    const f32 radius_squared = radius * radius;
    if (distance_squared > radius_squared) {
        return -1.0f;
    }
    const f32 offset = std::sqrt(radius_squared - distance_squared);
    const f32 near = projection - offset;
    if (near >= 0.0f) {
        return near;
    }
    const f32 far = projection + offset;
    return far >= 0.0f ? far : -1.0f;
}

/// Ray/plane intersection where the plane is defined by a normal and offset.
inline f32 IntersectRayPlane(const Ray& ray, const Vec3& plane_normal, f32 plane_offset) {
    const f32 denominator = Dot(ray.direction, plane_normal);
    if (std::fabs(denominator) < kEpsilon) {
        return -1.0f;
    }
    const f32 t = (plane_offset - Dot(ray.origin, plane_normal)) / denominator;
    return t >= 0.0f ? t : -1.0f;
}

/// Distance from a point to a segment (used for capsule-vs-box style queries).
inline f32 DistancePointToSegment(const Vec3& point, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const f32 length_squared = LengthSquared(ab);
    if (length_squared <= kEpsilon) {
        return Length(point - a);
    }
    const f32 t = Saturate(Dot(point - a, ab) / length_squared);
    return Length(point - (a + ab * t));
}

// =============================================================================
// Spherical / gameplay helpers shared by engine and games
// =============================================================================
struct SphericalAngles {
    f32 pitch = 0.0f;  ///< Radians, positive looks up.
    f32 yaw = 0.0f;    ///< Radians, 0 looks towards -Z.
};

inline Vec3 DirectionFromAngles(const SphericalAngles& angles) {
    return ForwardFromEuler(angles.pitch, angles.yaw);
}

inline SphericalAngles AnglesFromDirection(const Vec3& direction) {
    const Vec3 unit = Normalize(direction);
    SphericalAngles angles;
    angles.pitch = std::asin(Clamp(unit.y, -1.0f, 1.0f));
    angles.yaw = std::atan2(-unit.x, -unit.z);
    return angles;
}

/// Camera-relative movement basis on the horizontal plane.
struct MovementBasis {
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 right{1.0f, 0.0f, 0.0f};
};

inline MovementBasis MovementBasisFromYaw(f32 yaw) {
    MovementBasis basis;
    basis.forward = Normalize(Vec3{-std::sin(yaw), 0.0f, -std::cos(yaw)});
    basis.right = Normalize(Cross(basis.forward, Vec3{0.0f, 1.0f, 0.0f}));
    return basis;
}

}  // namespace valgo
