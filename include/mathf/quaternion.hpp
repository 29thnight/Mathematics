// mathf/quaternion.hpp — unit quaternion rotation, DirectXMath conventions.
//
// Four conventions, all of them load-bearing and all of them observed from
// DirectXMath rather than assumed:
//
//   * Storage is (x, y, z, w) with the scalar LAST, so a quaternion has the same
//     memory layout as a Vector4 and the same load and store instructions.
//   * Axis-angle is (axis * sin(theta/2), cos(theta/2)).
//   * `a * b` is the rotation a FOLLOWED BY b. This is the reverse of the
//     textbook Hamilton product, which writes that composition `b * a`. It is
//     not an error: it makes quaternion composition read left to right in
//     application order, exactly like `v * M` and `S * R * T` do for the
//     matrices, so `RotationMatrix(a * b) == RotationMatrix(a) * RotationMatrix(b)`
//     with no reversal anywhere. Pick one order and every layer agrees; pick the
//     textbook one and the matrix layer has to flip.
//   * Rotation is right-handed about the axis, and a rotation matrix built from
//     a quaternion multiplies row vectors on the left.
//
// A quaternion is deliberately NOT a VectorLike. Vector2/3/4 share their
// arithmetic through that concept, and `*` there is component-wise, following
// HLSL. Component-wise multiplication of two quaternions is meaningless, and
// silently getting it instead of the Hamilton product would be the worst kind of
// bug -- one that type-checks and renders almost right. Leaving `kLanes`
// undefined is what keeps the concept from matching.
#ifndef MATHF_QUATERNION_HPP
#define MATHF_QUATERNION_HPP

#include <mathf/matrix.hpp>
#include <mathf/scalar.hpp>
#include <mathf/vector.hpp>

namespace mathf {

struct Quaternion {
    float x, y, z, w;

    // The identity rotation, not the zero quaternion. Zero is not a rotation at
    // all -- it normalizes to nothing and turns any composition into garbage --
    // so a default-constructed Quaternion being usable matters more here than
    // the symmetry with Vector4's zero.
    constexpr Quaternion() noexcept : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}

    constexpr Quaternion(float xIn, float yIn, float zIn, float wIn) noexcept
        : x(xIn), y(yIn), z(zIn), w(wIn) {}

    // ------------------------------------------------------------ conversions
    MATHF_NODISCARD MATHF_INLINE constexpr VecReg Reg() const noexcept {
        MATHF_IF_CONSTEVAL { return Set(x, y, z, w); }
        return Load(&x);
    }

    MATHF_NODISCARD MATHF_INLINE static constexpr Quaternion
    FromReg(VecReg r) noexcept {
        MATHF_IF_CONSTEVAL {
            return Quaternion{Lane(r, 0), Lane(r, 1), Lane(r, 2), Lane(r, 3)};
        }
        Quaternion out;
        Store(&out.x, r);
        return out;
    }

    // The vector part, which is the rotation axis scaled by sin(theta/2).
    MATHF_NODISCARD constexpr Vector3 Axis() const noexcept {
        return Vector3{x, y, z};
    }

    MATHF_NODISCARD constexpr float operator[](int i) const noexcept {
        return (&x)[i];
    }
    MATHF_NODISCARD constexpr float& operator[](int i) noexcept {
        return (&x)[i];
    }

    MATHF_NODISCARD static constexpr Quaternion Identity() noexcept {
        return Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
    }
};

static_assert(sizeof(Quaternion) == 16, "Quaternion must stay packed");
static_assert(std::is_standard_layout_v<Quaternion>);
static_assert(std::is_trivially_copyable_v<Quaternion>);

// The guard described in the header comment. If Quaternion ever satisfies
// VectorLike, the component-wise `operator*` template becomes a candidate for
// `q1 * q2` and the Hamilton product below is silently ambiguous or shadowed.
static_assert(!VectorLike<Quaternion>,
              "Quaternion must not satisfy VectorLike -- component-wise "
              "operator* would compete with the Hamilton product");

// ------------------------------------------------------------------ construction
// Right-handed about the axis: with a unit +Z axis this sends +X toward +Y.
// The axis is normalized here, so a caller passing a scaled direction gets the
// rotation they meant rather than a quaternion of the wrong magnitude.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
QuaternionFromAxisAngle(const Vector3& axis, float radians) noexcept {
    const float lengthSq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    if (lengthSq == 0.0f) return Quaternion::Identity();

    const float invLength = 1.0f / detail::ScalarSqrt(lengthSq);
    float s = 0.0f, c = 0.0f;
    SinCos(radians * 0.5f, s, c);
    const float k = s * invLength;
    return Quaternion{axis.x * k, axis.y * k, axis.z * k, c};
}

// Half-angle sine and cosine per axis, combined in the order DirectXMath's
// XMQuaternionRotationRollPitchYaw uses. That order is roll (Z), then pitch (X),
// then yaw (Y) in application order -- verified against DirectXMath rather than
// derived, because every source states Euler order differently and three of the
// six orders produce plausible-looking results for small angles.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
QuaternionFromPitchYawRoll(float pitch, float yaw, float roll) noexcept {
    float sp = 0.0f, cp = 0.0f, sy = 0.0f, cy = 0.0f, sr = 0.0f, cr = 0.0f;
    SinCos(pitch * 0.5f, sp, cp);
    SinCos(yaw * 0.5f, sy, cy);
    SinCos(roll * 0.5f, sr, cr);

    return Quaternion{
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        sr * cp * cy - cr * sp * sy,
        cr * cp * cy + sr * sp * sy};
}

MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
QuaternionFromEuler(const Vector3& pitchYawRoll) noexcept {
    return QuaternionFromPitchYawRoll(pitchYawRoll.x, pitchYawRoll.y,
                                      pitchYawRoll.z);
}

// ------------------------------------------------------------------- products
// The Hamilton product, with the operands swapped relative to the textbook
// formula so that the result is "a, then b" (see the header comment).
//
// Written once, in register operations, rather than as a scalar formula. Those
// operations are already constexpr dual-path -- scalar during constant
// evaluation, intrinsics at run time -- so this is one implementation that
// serves both, with no second version to drift out of step.
//
// The shape is DirectXMath's: each of b's components broadcast against a
// permutation of a, with an alternating sign per row. The signs go in as a
// multiply by +/-1 rather than an XOR of the sign bit, because that is what lets
// the sign and the accumulation fuse into a single multiply-add; the XOR form
// would be one operation cheaper on paper and one instruction longer in fact.
//
// Written as four scalar rows it measured 443 M/s against DirectXMath's 596 --
// outside the +-5% gate. Four lanes is exactly the width where promoting to a
// register pays (docs/PLAN.md Phase 2), unlike the three-component vectors.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
Multiply(const Quaternion& a, const Quaternion& b) noexcept {
    const VecReg ra = a.Reg();
    const VecReg rb = b.Reg();

    //   x =  bw*ax + bx*aw + by*az - bz*ay
    //   y =  bw*ay - bx*az + by*aw + bz*ax
    //   z =  bw*az + bx*ay - by*ax + bz*aw
    //   w =  bw*aw - bx*ax - by*ay - bz*az
    //
    // Read down the columns: the second column is a reversed (w,z,y,x) with
    // signs (+,-,+,-), the third is (z,w,x,y) with (+,+,-,-), the fourth is
    // (y,x,w,z) with (-,+,+,-).
    const VecReg aWZYX = Shuffle<3, 2, 1, 0>(ra);
    const VecReg aZWXY = Shuffle<2, 3, 0, 1>(ra);
    const VecReg aYXWZ = Shuffle<1, 0, 3, 2>(ra);

    const VecReg signWZYX = Set(1.0f, -1.0f, 1.0f, -1.0f);
    const VecReg signZWXY = Set(1.0f, 1.0f, -1.0f, -1.0f);
    const VecReg signYXWZ = Set(-1.0f, 1.0f, 1.0f, -1.0f);

    VecReg acc = Mul(SplatW(rb), ra);
    acc = MulAdd(Mul(SplatX(rb), aWZYX), signWZYX, acc);
    acc = MulAdd(Mul(SplatY(rb), aZWXY), signZWXY, acc);
    acc = MulAdd(Mul(SplatZ(rb), aYXWZ), signYXWZ, acc);
    return Quaternion::FromReg(acc);
}

MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
operator*(const Quaternion& a, const Quaternion& b) noexcept {
    return Multiply(a, b);
}

MATHF_INLINE constexpr Quaternion& operator*=(Quaternion& a,
                                              const Quaternion& b) noexcept {
    return a = Multiply(a, b);
}

// Scalar scaling and addition exist for interpolation, not as general
// arithmetic: the result of adding two rotations is not a rotation until it is
// normalized, which is exactly what Nlerp does with them.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
operator*(const Quaternion& q, float s) noexcept {
    return Quaternion{q.x * s, q.y * s, q.z * s, q.w * s};
}
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
operator*(float s, const Quaternion& q) noexcept {
    return q * s;
}
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
operator+(const Quaternion& a, const Quaternion& b) noexcept {
    return Quaternion{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
operator-(const Quaternion& q) noexcept {
    return Quaternion{-q.x, -q.y, -q.z, -q.w};
}

// ---------------------------------------------------------------- magnitude
MATHF_NODISCARD MATHF_INLINE constexpr float
Dot(const Quaternion& a, const Quaternion& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

MATHF_NODISCARD MATHF_INLINE constexpr float
LengthSq(const Quaternion& q) noexcept {
    return Dot(q, q);
}

MATHF_NODISCARD MATHF_INLINE constexpr float Length(const Quaternion& q) noexcept {
    return detail::ScalarSqrt(LengthSq(q));
}

// A zero quaternion normalizes to the identity rather than to NaN, matching the
// choice Vector3::Normalize makes for the zero vector and for the same reason:
// a NaN here propagates into every transform downstream and is diagnosed a long
// way from where it started.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
Normalize(const Quaternion& q) noexcept {
    const float lengthSq = LengthSq(q);
    if (!detail::IsFiniteNonZero(lengthSq)) return Quaternion::Identity();
    return q * (1.0f / detail::ScalarSqrt(lengthSq));
}

// ------------------------------------------------------------ inverse family
// Negating the vector part. For a unit quaternion this is the inverse, and it is
// what almost every call site actually wants.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
Conjugate(const Quaternion& q) noexcept {
    return Quaternion{-q.x, -q.y, -q.z, q.w};
}

// The true inverse, conjugate over the squared norm, correct for a quaternion
// that has drifted off the unit sphere. Conjugate is the one to reach for when
// the input is known normalized -- this one pays for a division to be right
// about the case where it is not.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
Inverse(const Quaternion& q) noexcept {
    const float lengthSq = LengthSq(q);
    if (!detail::IsFiniteNonZero(lengthSq)) return Quaternion::Identity();
    return Conjugate(q) * (1.0f / lengthSq);
}

// -------------------------------------------------------------------- rotate
// v' = v + 2 * cross(qv, cross(qv, v) + w * v), the form that avoids
// materialising the two quaternion products. Same identity DirectXMath uses.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
Rotate(const Vector3& v, const Quaternion& q) noexcept {
    const Vector3 qv{q.x, q.y, q.z};
    const Vector3 t = Cross(qv, v) + v * q.w;
    return v + Cross(qv, t) * 2.0f;
}

// The inverse rotation, without building the inverse quaternion first.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
InverseRotate(const Vector3& v, const Quaternion& q) noexcept {
    return Rotate(v, Conjugate(q));
}

// -------------------------------------------------------------- interpolation
// Normalized linear interpolation: cheap, and the right default for animation
// blending where the endpoints are close together. The path is not constant
// speed -- that is what Slerp is for -- but the result is always a unit
// quaternion and the cost is a handful of operations.
//
// Takes the short arc: if the endpoints point into opposite hemispheres they
// describe the same rotations but interpolating between them the long way spins
// nearly all the way round.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
Nlerp(const Quaternion& a, const Quaternion& b, float t) noexcept {
    const float d = Dot(a, b);
    const Quaternion target = d < 0.0f ? -b : b;
    return Normalize(a * (1.0f - t) + target * t);
}

// Constant angular velocity along the shortest arc.
//
// Falls back to Nlerp when the endpoints are nearly parallel: the sine of the
// half-angle is the divisor, and near zero it takes the answer with it. The
// threshold is where the two agree to well within float precision, so the seam
// is invisible.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
Slerp(const Quaternion& a, const Quaternion& b, float t) noexcept {
    float d = Dot(a, b);
    const Quaternion target = d < 0.0f ? -b : b;
    if (d < 0.0f) d = -d;

    if (d > 0.9995f) return Nlerp(a, target, t);

    const float theta = ACos(d);
    const float sinTheta = Sin(theta);
    const float wa = Sin((1.0f - t) * theta) / sinTheta;
    const float wb = Sin(t * theta) / sinTheta;
    return a * wa + target * wb;
}

// ------------------------------------------------------------ decomposition
// The axis is undefined for the identity, so it is reported as +X with a zero
// angle rather than as a division by zero.
MATHF_INLINE constexpr void
ToAxisAngle(const Quaternion& q, Vector3& axisOut, float& radiansOut) noexcept {
    const Quaternion n = Normalize(q);
    // The vector part's length is |sin(theta/2)| and w is cos(theta/2), so the
    // angle comes from an arc tangent of the two rather than an arc cosine of w
    // alone. Near zero and near pi that is the better conditioned of the two.
    const float vLength = detail::ScalarSqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (vLength == 0.0f) {
        axisOut = Vector3{1.0f, 0.0f, 0.0f};
        radiansOut = 0.0f;
        return;
    }
    const float inv = 1.0f / vLength;
    axisOut = Vector3{n.x * inv, n.y * inv, n.z * inv};
    radiansOut = 2.0f * ATan2(vLength, n.w);
}

// Back to pitch (X), yaw (Y), roll (Z), inverting QuaternionFromPitchYawRoll.
//
// At the poles -- pitch at +/-90 degrees -- yaw and roll describe the same
// motion and cannot be told apart. That is gimbal lock, and it is a property of
// Euler angles, not a bug to fix: the convention here is to put the whole
// rotation into yaw and leave roll at zero, which is what makes the round trip
// through a quaternion still land on the same rotation.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
ToEuler(const Quaternion& q) noexcept {
    const Quaternion n = Normalize(q);
    const float xx = n.x * n.x, yy = n.y * n.y, zz = n.z * n.z;
    const float xy = n.x * n.y, xz = n.x * n.z, yz = n.y * n.z;
    const float wx = n.w * n.x, wy = n.w * n.y, wz = n.w * n.z;

    // Derived from M = Rz(roll) * Rx(pitch) * Ry(yaw), the composition
    // QuaternionFromPitchYawRoll builds, read in row-vector form:
    //
    //   M[2][1] = -sin(pitch)                    pitch alone
    //   M[2][0] =  cos(pitch) sin(yaw)           yaw, once pitch is known
    //   M[2][2] =  cos(pitch) cos(yaw)
    //   M[0][1] =  sin(roll)  cos(pitch)         roll likewise
    //   M[1][1] =  cos(roll)  cos(pitch)
    //
    // Substituting the quaternion form of each entry gives the three lines
    // below. Every one of the three signs here was wrong on the first attempt
    // and the round-trip test is what caught it -- a term-by-term derivation is
    // the only way to get this right, because a sign error in yaw or roll still
    // produces a plausible rotation for small angles.
    const float sinPitch = 2.0f * (wx - yz);

    if (sinPitch >= 0.99999f || sinPitch <= -0.99999f) {
        // Gimbal lock. cos(pitch) is zero, so yaw and roll turn about the same
        // axis and only their combination is observable -- no decomposition can
        // recover both. Putting the whole amount in yaw and leaving roll at zero
        // preserves the one thing that can be preserved: feeding the result back
        // through QuaternionFromPitchYawRoll lands on the same rotation.
        return Vector3{sinPitch > 0.0f ? kHalfPi : -kHalfPi,
                       ATan2(2.0f * (wy - xz), 1.0f - 2.0f * (yy + zz)),
                       0.0f};
    }

    return Vector3{
        ASin(sinPitch),
        ATan2(2.0f * (wy + xz), 1.0f - 2.0f * (xx + yy)),
        ATan2(2.0f * (wz + xy), 1.0f - 2.0f * (xx + zz))};
}

// ----------------------------------------------------------- matrix bridging
// Row-vector rotation matrix, matching the rest of the library: the basis
// vectors are the ROWS, so `v * M` and `Rotate(v, q)` agree.
MATHF_NODISCARD MATHF_INLINE constexpr Matrix4x4
RotationMatrix(const Quaternion& q) noexcept {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    return Matrix4x4{
        1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f,
        2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f,
        2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f,
        0.0f,                    0.0f,                    0.0f,                    1.0f};
}

MATHF_NODISCARD MATHF_INLINE constexpr Matrix3x3
RotationMatrix3x3(const Quaternion& q) noexcept {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    return Matrix3x3{
        1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),
        2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),
        2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy)};
}

// Shepperd's method: pick the largest of the four possible divisors and use its
// branch. The naive `w = sqrt(1 + trace) / 2` loses all precision when the trace
// approaches -1 -- a 180 degree rotation -- and turns into a square root of a
// small negative number a little past it. Choosing the largest component first
// keeps the divisor away from zero in every case.
MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
QuaternionFromRotationMatrix(const Matrix4x4& m) noexcept {
    const float m00 = m.m[0][0], m11 = m.m[1][1], m22 = m.m[2][2];
    const float trace = m00 + m11 + m22;

    if (trace > 0.0f) {
        const float s = detail::ScalarSqrt(trace + 1.0f) * 2.0f;   // 4w
        return Quaternion{(m.m[1][2] - m.m[2][1]) / s,
                          (m.m[2][0] - m.m[0][2]) / s,
                          (m.m[0][1] - m.m[1][0]) / s,
                          0.25f * s};
    }
    if (m00 > m11 && m00 > m22) {
        const float s = detail::ScalarSqrt(1.0f + m00 - m11 - m22) * 2.0f;  // 4x
        return Quaternion{0.25f * s,
                          (m.m[0][1] + m.m[1][0]) / s,
                          (m.m[2][0] + m.m[0][2]) / s,
                          (m.m[1][2] - m.m[2][1]) / s};
    }
    if (m11 > m22) {
        const float s = detail::ScalarSqrt(1.0f + m11 - m00 - m22) * 2.0f;  // 4y
        return Quaternion{(m.m[0][1] + m.m[1][0]) / s,
                          0.25f * s,
                          (m.m[1][2] + m.m[2][1]) / s,
                          (m.m[2][0] - m.m[0][2]) / s};
    }
    const float s = detail::ScalarSqrt(1.0f + m22 - m00 - m11) * 2.0f;      // 4z
    return Quaternion{(m.m[2][0] + m.m[0][2]) / s,
                      (m.m[1][2] + m.m[2][1]) / s,
                      0.25f * s,
                      (m.m[0][1] - m.m[1][0]) / s};
}

MATHF_NODISCARD MATHF_INLINE constexpr Quaternion
QuaternionFromRotationMatrix(const Matrix3x3& m) noexcept {
    return QuaternionFromRotationMatrix(Matrix4x4{
        m.m[0][0], m.m[0][1], m.m[0][2], 0.0f,
        m.m[1][0], m.m[1][1], m.m[1][2], 0.0f,
        m.m[2][0], m.m[2][1], m.m[2][2], 0.0f,
        0.0f,      0.0f,      0.0f,      1.0f});
}

// ------------------------------------------------------------------ comparison
// Exact, component-wise. Note that q and -q are the same rotation but compare
// unequal here -- deliberately, since this compares quaternions, not the
// rotations they stand for. Use Dot to ask the other question: |Dot(a,b)| near
// one means the two describe the same orientation.
MATHF_NODISCARD MATHF_INLINE constexpr bool
operator==(const Quaternion& a, const Quaternion& b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

MATHF_NODISCARD MATHF_INLINE constexpr bool
NearEqual(const Quaternion& a, const Quaternion& b,
          float epsilon = 1e-5f) noexcept {
    // Positive test, so a NaN fails rather than passing -- the matrix version of
    // this shipped with the negated form and reported NaN as near-equal.
    const float dx = a.x - b.x, dy = a.y - b.y;
    const float dz = a.z - b.z, dw = a.w - b.w;
    return dx <= epsilon && dx >= -epsilon && dy <= epsilon && dy >= -epsilon &&
           dz <= epsilon && dz >= -epsilon && dw <= epsilon && dw >= -epsilon;
}

// The comparison that usually matters: do these describe the same orientation,
// regardless of which of the two antipodal representations each one uses.
MATHF_NODISCARD MATHF_INLINE constexpr bool
SameRotation(const Quaternion& a, const Quaternion& b,
             float epsilon = 1e-5f) noexcept {
    return NearEqual(a, b, epsilon) || NearEqual(a, -b, epsilon);
}

} // namespace mathf

#endif // MATHF_QUATERNION_HPP
