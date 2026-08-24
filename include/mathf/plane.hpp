// mathf/plane.hpp — the plane ax + by + cz + d = 0.
//
// Storage and sign follow DirectXMath, observed rather than assumed:
//
//   * A plane is four floats (a, b, c, d). The first three are the normal.
//   * d is MINUS the dot of the normal with any point on the plane, so that
//     SignedDistance(p) is just dot(n, p) + d.
//   * A positive signed distance means the point is on the side the normal
//     points to. That side is called the FRONT.
//   * FromPoints uses the right-hand rule: the normal is
//     Cross(b - a, c - a), so a triangle wound counter-clockwise when seen
//     from the front faces you.
//
// The normal is NOT normalized on construction unless the factory says so.
// FromPointNormal and FromPoints both produce a unit normal; the four-float
// constructor does not, because a caller assembling a plane from a projection
// matrix row has a valid unnormalized plane and normalizing it would be wrong.
// Only a plane with a unit normal gives a true distance; Normalize makes one.
#ifndef MATHF_PLANE_HPP
#define MATHF_PLANE_HPP

#include <mathf/vector.hpp>

namespace mathf {

// Which side of a plane something is on. INTERSECTING in DirectXMath's
// vocabulary; Straddling here, because a volume that crosses the plane is not
// "intersecting the plane" in the same sense that two volumes intersect, and
// the tests read better for the distinction.
enum class PlaneSide {
    Front,       // entirely on the side the normal points to
    Back,        // entirely on the other side
    Straddling,  // crosses the plane
};

struct Plane {
    float a, b, c, d;

    // The XY plane through the origin, facing +Z. A default plane has to be a
    // real plane -- all zeros is not one, and would make every query nonsense.
    constexpr Plane() noexcept : a(0.0f), b(0.0f), c(1.0f), d(0.0f) {}

    constexpr Plane(float aIn, float bIn, float cIn, float dIn) noexcept
        : a(aIn), b(bIn), c(cIn), d(dIn) {}

    MATHF_NODISCARD constexpr Vector3 Normal() const noexcept {
        return Vector3{a, b, c};
    }

    MATHF_NODISCARD MATHF_INLINE constexpr VecReg Reg() const noexcept {
        MATHF_IF_CONSTEVAL { return Set(a, b, c, d); }
        return Load(&a);
    }

    MATHF_NODISCARD MATHF_INLINE static constexpr Plane
    FromReg(VecReg r) noexcept {
        MATHF_IF_CONSTEVAL {
            return Plane{Lane(r, 0), Lane(r, 1), Lane(r, 2), Lane(r, 3)};
        }
        Plane out;
        Store(&out.a, r);
        return out;
    }
};

static_assert(sizeof(Plane) == 16, "Plane must stay packed");
static_assert(std::is_standard_layout_v<Plane>);
static_assert(std::is_trivially_copyable_v<Plane>);

// ------------------------------------------------------------------ factories
// A degenerate normal has no plane to describe, so this returns the default XY
// plane rather than one full of NaN -- the same choice Normalize and Inverse
// make elsewhere in the library.
MATHF_NODISCARD MATHF_INLINE constexpr Plane
PlaneFromPointNormal(const Vector3& point, const Vector3& normal) noexcept {
    const float lengthSq = LengthSq(normal);
    if (!detail::IsFiniteNonZero(lengthSq)) return Plane{};

    const float inv = 1.0f / detail::ScalarSqrt(lengthSq);
    const Vector3 n{normal.x * inv, normal.y * inv, normal.z * inv};
    return Plane{n.x, n.y, n.z, -Dot(n, point)};
}

// Right-handed: Cross(b - a, c - a). Three collinear points span no plane and
// give the default, for the same reason as above.
MATHF_NODISCARD MATHF_INLINE constexpr Plane
PlaneFromPoints(const Vector3& p0, const Vector3& p1,
                const Vector3& p2) noexcept {
    return PlaneFromPointNormal(p0, Cross(p1 - p0, p2 - p0));
}

// Rescales so the normal is unit and d is a true distance. A plane built from
// a matrix row generally is not normalized, and every distance query is wrong
// by the normal's length until it is.
MATHF_NODISCARD MATHF_INLINE constexpr Plane
Normalize(const Plane& plane) noexcept {
    const float lengthSq = plane.a * plane.a + plane.b * plane.b + plane.c * plane.c;
    if (!detail::IsFiniteNonZero(lengthSq)) return Plane{};

    const float inv = 1.0f / detail::ScalarSqrt(lengthSq);
    return Plane{plane.a * inv, plane.b * inv, plane.c * inv, plane.d * inv};
}

// ------------------------------------------------------------------- queries
// Positive in front, negative behind, zero on the plane. A true distance only
// when the normal is unit -- see the header note.
MATHF_NODISCARD MATHF_INLINE constexpr float
SignedDistance(const Plane& plane, const Vector3& point) noexcept {
    return plane.a * point.x + plane.b * point.y + plane.c * point.z + plane.d;
}

// The plane equation applied to a DIRECTION, which ignores d. Useful for
// asking whether a direction points across a plane without caring where the
// plane sits -- a backface test, for instance.
MATHF_NODISCARD MATHF_INLINE constexpr float
DotNormal(const Plane& plane, const Vector3& direction) noexcept {
    return plane.a * direction.x + plane.b * direction.y + plane.c * direction.z;
}

// The epsilon is a band around the plane counted as "on it", in the same units
// as the distance -- so it means what a caller expects only for a unit normal.
MATHF_NODISCARD MATHF_INLINE constexpr PlaneSide
ClassifyPoint(const Plane& plane, const Vector3& point,
              float epsilon = 1e-5f) noexcept {
    const float distance = SignedDistance(plane, point);
    if (distance > epsilon) return PlaneSide::Front;
    if (distance < -epsilon) return PlaneSide::Back;
    return PlaneSide::Straddling;
}

MATHF_NODISCARD MATHF_INLINE constexpr Vector3
ClosestPointOnPlane(const Plane& plane, const Vector3& point) noexcept {
    return point - plane.Normal() * SignedDistance(plane, point);
}

// Mirrors a point through the plane. Reflecting a DIRECTION is Reflect() in
// vector_common.hpp -- a direction has no position, so d must not enter it,
// and using the wrong one of the two is a bug that looks almost right.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
ReflectPoint(const Plane& plane, const Vector3& point) noexcept {
    return point - plane.Normal() * (2.0f * SignedDistance(plane, point));
}

MATHF_NODISCARD MATHF_INLINE constexpr Plane
Flip(const Plane& plane) noexcept {
    return Plane{-plane.a, -plane.b, -plane.c, -plane.d};
}

// ---------------------------------------------------------------- comparison
MATHF_NODISCARD MATHF_INLINE constexpr bool
operator==(const Plane& x, const Plane& y) noexcept {
    return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d;
}

// Positive test, so a NaN fails rather than passing -- the matrix version of
// this shipped with the negated form and reported NaN as near-equal.
MATHF_NODISCARD MATHF_INLINE constexpr bool
NearEqual(const Plane& x, const Plane& y, float epsilon = 1e-5f) noexcept {
    const float da = x.a - y.a, db = x.b - y.b;
    const float dc = x.c - y.c, dd = x.d - y.d;
    return da <= epsilon && da >= -epsilon && db <= epsilon && db >= -epsilon &&
           dc <= epsilon && dc >= -epsilon && dd <= epsilon && dd >= -epsilon;
}

} // namespace mathf

#endif // MATHF_PLANE_HPP
