// mathf/transform.hpp — transform, view and projection matrices.
//
// Everything here follows the conventions the rest of the library already
// committed to: row-major storage, row vectors, composition left to right in
// application order, translation in row 3.
//
// HANDEDNESS IS ALWAYS IN THE NAME. There is no LookAt, only LookAtLH and
// LookAtRH. A default would be a coin flip that silently mirrors a scene, and
// the compiler cannot catch it -- everything still builds, renders, and looks
// almost right until something reads a normal or a winding order. DirectXMath
// makes the same choice, and the numbers below are matched against it.
//
// The depth convention is Direct3D's: clip z runs from 0 at the near plane to 1
// at the far plane, not OpenGL's -1 to 1. That is what the LH and RH pairs here
// produce, in both the perspective and the orthographic forms.
#ifndef MATHF_TRANSFORM_HPP
#define MATHF_TRANSFORM_HPP

#include <mathf/matrix.hpp>
#include <mathf/quaternion.hpp>
#include <mathf/scalar.hpp>
#include <mathf/vector.hpp>

namespace mathf {

// ------------------------------------------------------------ basic transforms
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
ScalingMatrix(const Vector3& s) noexcept {
    return Matrix4x4{s.x, 0, 0, 0,
                     0, s.y, 0, 0,
                     0, 0, s.z, 0,
                     0, 0, 0,   1};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
ScalingMatrix(float s) noexcept {
    return ScalingMatrix(Vector3{s, s, s});
}

// Row 3, per the convention -- not column 3.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
TranslationMatrix(const Vector3& t) noexcept {
    return Matrix4x4{1, 0, 0, 0,
                     0, 1, 0, 0,
                     0, 0, 1, 0,
                     t.x, t.y, t.z, 1};
}

// Right-handed about each axis, matching QuaternionFromAxisAngle: RotationZ
// sends +X toward +Y. In row-vector form that puts the positive sine above the
// diagonal, which is the transpose of the arrangement most textbooks print.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
RotationX(float radians) noexcept {
    float s = 0.0f, c = 0.0f;
    SinCos(radians, s, c);
    return Matrix4x4{1, 0, 0, 0,
                     0, c, s, 0,
                     0, -s, c, 0,
                     0, 0, 0, 1};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
RotationY(float radians) noexcept {
    float s = 0.0f, c = 0.0f;
    SinCos(radians, s, c);
    return Matrix4x4{c, 0, -s, 0,
                     0, 1, 0, 0,
                     s, 0, c, 0,
                     0, 0, 0, 1};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
RotationZ(float radians) noexcept {
    float s = 0.0f, c = 0.0f;
    SinCos(radians, s, c);
    return Matrix4x4{c, s, 0, 0,
                     -s, c, 0, 0,
                     0, 0, 1, 0,
                     0, 0, 0, 1};
}

// ------------------------------------------------------------------ TRS
// Scale, then rotate, then translate -- the order that reads left to right and
// the only one that behaves the way a scene graph expects, since it leaves the
// translation unscaled and unrotated.
//
// Built directly rather than as `ScalingMatrix(s) * RotationMatrix(r) *
// TranslationMatrix(t)`: those two products are almost all multiplications by
// zero and one, and writing the result out skips both of them.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
Compose(const Vector3& scale, const Quaternion& rotation,
        const Vector3& translation) noexcept {
    Matrix4x4 m = RotationMatrix(rotation);

    m.m[0][0] *= scale.x; m.m[0][1] *= scale.x; m.m[0][2] *= scale.x;
    m.m[1][0] *= scale.y; m.m[1][1] *= scale.y; m.m[1][2] *= scale.y;
    m.m[2][0] *= scale.z; m.m[2][1] *= scale.z; m.m[2][2] *= scale.z;

    m.m[3][0] = translation.x;
    m.m[3][1] = translation.y;
    m.m[3][2] = translation.z;
    return m;
}

// Splits an affine transform back into scale, rotation and translation.
//
// Returns false, and leaves the outputs untouched, when the matrix has no such
// decomposition: a zero or near-zero scale on any axis destroys the direction of
// that basis vector, and no rotation can be recovered from it.
//
// A negative determinant means the transform includes a reflection, which no
// combination of scale and rotation can express -- a rotation matrix always has
// determinant +1. The convention here is to fold the reflection into a negative
// X scale, which is what DirectXMath and most engines do. It is a choice, not a
// recovery: the original might have had the mirror on Y or Z, and nothing in the
// matrix records which.
//
// Skew is not representable either, and unlike reflection it is not detected --
// a sheared matrix decomposes into something that is not equal to it. Compose
// and Decompose round-trip exactly for matrices Compose could have produced.
MATHF_NODISCARD MATHF_INLINE constexpr bool
Decompose(const Matrix4x4& m, Vector3& scaleOut, Quaternion& rotationOut,
          Vector3& translationOut) noexcept {
    const Vector3 r0{m.m[0][0], m.m[0][1], m.m[0][2]};
    const Vector3 r1{m.m[1][0], m.m[1][1], m.m[1][2]};
    const Vector3 r2{m.m[2][0], m.m[2][1], m.m[2][2]};

    float sx = Length(r0);
    const float sy = Length(r1);
    const float sz = Length(r2);

    if (!detail::IsFiniteNonZero(sx) || !detail::IsFiniteNonZero(sy) ||
        !detail::IsFiniteNonZero(sz)) {
        return false;
    }

    // Determinant of the upper 3x3. Negative means a reflection is baked in.
    const float det =
        r0.x * (r1.y * r2.z - r1.z * r2.y) -
        r0.y * (r1.x * r2.z - r1.z * r2.x) +
        r0.z * (r1.x * r2.y - r1.y * r2.x);
    if (det < 0.0f) sx = -sx;

    const Matrix3x3 basis{
        r0.x / sx, r0.y / sx, r0.z / sx,
        r1.x / sy, r1.y / sy, r1.z / sy,
        r2.x / sz, r2.y / sz, r2.z / sz};

    scaleOut = Vector3{sx, sy, sz};
    rotationOut = QuaternionFromRotationMatrix(basis);
    translationOut = Vector3{m.m[3][0], m.m[3][1], m.m[3][2]};
    return true;
}

// ------------------------------------------------------------------- view
namespace detail {

// The shared body of all four look-at forms. `forward` must already point the
// way the convention wants: toward the target for left-handed, away from it for
// right-handed.
//
// Degenerate input -- a zero direction, or an up vector parallel to it --
// returns the identity, per the library-wide policy (Inverse of a singular
// matrix, Normalize of a zero vector). Without the guard, Cross(up, f) is the
// zero vector, Normalize hands the zero back, and the function would return a
// matrix with two zero basis columns: a camera that silently collapses the
// scene to a line, discovered a long way from the LookAt call that caused it.
// Looking straight up with the conventional world up is exactly this case.
// DirectXMath asserts in debug builds and produces the garbage in release;
// returning something usable and well-defined is this library's choice.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
LookToImpl(const Vector3& eye, const Vector3& forward,
           const Vector3& up) noexcept {
    const Vector3 f = Normalize(forward);
    const Vector3 side = Cross(up, f);
    if (!detail::IsFiniteNonZero(LengthSq(side))) return Matrix4x4::Identity();
    const Vector3 r = Normalize(side);
    const Vector3 u = Cross(f, r);

    // The basis goes in as COLUMNS, because a view matrix is the inverse of the
    // camera's transform and the inverse of a rotation is its transpose.
    return Matrix4x4{
        r.x, u.x, f.x, 0.0f,
        r.y, u.y, f.y, 0.0f,
        r.z, u.z, f.z, 0.0f,
        -Dot(r, eye), -Dot(u, eye), -Dot(f, eye), 1.0f};
}

} // namespace detail

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
LookToLH(const Vector3& eye, const Vector3& direction,
         const Vector3& up) noexcept {
    return detail::LookToImpl(eye, direction, up);
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
LookToRH(const Vector3& eye, const Vector3& direction,
         const Vector3& up) noexcept {
    return detail::LookToImpl(eye, -direction, up);
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
LookAtLH(const Vector3& eye, const Vector3& target, const Vector3& up) noexcept {
    return detail::LookToImpl(eye, target - eye, up);
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
LookAtRH(const Vector3& eye, const Vector3& target, const Vector3& up) noexcept {
    return detail::LookToImpl(eye, eye - target, up);
}

// ------------------------------------------------------------- projection
// Clip z is 0 at the near plane and 1 at the far plane (Direct3D), and w carries
// the view-space depth so the perspective divide happens for free.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
PerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ) noexcept {
    float s = 0.0f, c = 0.0f;
    SinCos(fovY * 0.5f, s, c);
    const float h = c / s;              // cot(fovY/2)
    const float w = h / aspect;
    const float range = farZ / (farZ - nearZ);

    return Matrix4x4{w, 0, 0, 0,
                     0, h, 0, 0,
                     0, 0, range, 1,
                     0, 0, -range * nearZ, 0};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
PerspectiveFovRH(float fovY, float aspect, float nearZ, float farZ) noexcept {
    float s = 0.0f, c = 0.0f;
    SinCos(fovY * 0.5f, s, c);
    const float h = c / s;
    const float w = h / aspect;
    const float range = farZ / (nearZ - farZ);

    return Matrix4x4{w, 0, 0, 0,
                     0, h, 0, 0,
                     0, 0, range, -1,
                     0, 0, range * nearZ, 0};
}

// The same projection specified by the view-space size of the near plane rather
// than by a field of view.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
PerspectiveLH(float width, float height, float nearZ, float farZ) noexcept {
    const float twoNear = nearZ + nearZ;
    const float range = farZ / (farZ - nearZ);
    return Matrix4x4{twoNear / width, 0, 0, 0,
                     0, twoNear / height, 0, 0,
                     0, 0, range, 1,
                     0, 0, -range * nearZ, 0};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
PerspectiveRH(float width, float height, float nearZ, float farZ) noexcept {
    const float twoNear = nearZ + nearZ;
    const float range = farZ / (nearZ - farZ);
    return Matrix4x4{twoNear / width, 0, 0, 0,
                     0, twoNear / height, 0, 0,
                     0, 0, range, -1,
                     0, 0, range * nearZ, 0};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
OrthographicLH(float width, float height, float nearZ, float farZ) noexcept {
    const float range = 1.0f / (farZ - nearZ);
    return Matrix4x4{2.0f / width, 0, 0, 0,
                     0, 2.0f / height, 0, 0,
                     0, 0, range, 0,
                     0, 0, -range * nearZ, 1};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
OrthographicRH(float width, float height, float nearZ, float farZ) noexcept {
    const float range = 1.0f / (nearZ - farZ);
    return Matrix4x4{2.0f / width, 0, 0, 0,
                     0, 2.0f / height, 0, 0,
                     0, 0, range, 0,
                     0, 0, range * nearZ, 1};
}

// Asymmetric frustum, for split screens, shadow cascades and tiled rendering.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
OrthographicOffCenterLH(float left, float right, float bottom, float top,
                        float nearZ, float farZ) noexcept {
    const float rcpWidth = 1.0f / (right - left);
    const float rcpHeight = 1.0f / (top - bottom);
    const float range = 1.0f / (farZ - nearZ);

    return Matrix4x4{
        rcpWidth + rcpWidth, 0, 0, 0,
        0, rcpHeight + rcpHeight, 0, 0,
        0, 0, range, 0,
        -(left + right) * rcpWidth, -(top + bottom) * rcpHeight,
        -range * nearZ, 1};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
OrthographicOffCenterRH(float left, float right, float bottom, float top,
                        float nearZ, float farZ) noexcept {
    const float rcpWidth = 1.0f / (right - left);
    const float rcpHeight = 1.0f / (top - bottom);
    const float range = 1.0f / (nearZ - farZ);

    return Matrix4x4{
        rcpWidth + rcpWidth, 0, 0, 0,
        0, rcpHeight + rcpHeight, 0, 0,
        0, 0, range, 0,
        -(left + right) * rcpWidth, -(top + bottom) * rcpHeight,
        range * nearZ, 1};
}

} // namespace mathf

#endif // MATHF_TRANSFORM_HPP
