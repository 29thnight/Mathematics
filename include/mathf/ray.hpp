// mathf/ray.hpp — a half-line: an origin and a direction.
//
// A ray starts at its origin and goes one way only. Every intersection query
// in intersect.hpp returns a distance measured ALONG THE DIRECTION, so:
//
//   * A hit behind the origin is not a hit. A ray fired away from a sphere
//     reports no intersection, not a negative distance.
//   * A ray whose origin is already inside a volume hits at distance zero,
//     because it is touching that volume at its origin.
//
// That last rule is the one place in this library that deliberately does NOT
// match DirectXMath, for the unusual reason that DirectXMath does not match
// itself. Fired along +Z from inside a unit sphere at (0,0,0.5), its
// BoundingSphere reports 0.5 -- the distance to where the ray LEAVES. The same
// query against a BoundingBox spanning [-1,1] reports -1.5 -- the distance
// BACK to where the ray would have entered. One is a positive exit, the other
// a negative entry, and no caller can write one branch that handles both. So
// the two conventions cannot both be matched, and zero is the answer that is
// true for every primitive, never points behind the origin, and needs no
// special case at the call site. Ask Intersects(volume, ray.origin) to tell
// whether a reported zero means "started inside".
//   * The distance is in units of the direction's length. Keep the direction
//     normalized and it is a real distance; leave it unnormalized and it is a
//     parameter t such that Origin + Direction * t is the hit point. Both are
//     useful, so neither is forced, but mixing them up silently scales every
//     distance the caller reads.
#ifndef MATHF_RAY_HPP
#define MATHF_RAY_HPP

#include <mathf/vector.hpp>

namespace mathf {

struct Ray {
    Vector3 origin;
    Vector3 direction;

    // Along +Z from the origin -- a real ray, so a default-constructed one can
    // be queried without producing nonsense the way a zero direction would.
    constexpr Ray() noexcept : origin{0.0f, 0.0f, 0.0f},
                               direction{0.0f, 0.0f, 1.0f} {}

    constexpr Ray(const Vector3& originIn, const Vector3& directionIn) noexcept
        : origin(originIn), direction(directionIn) {}

    MATHF_NODISCARD constexpr Vector3 PointAt(float t) const noexcept {
        return origin + direction * t;
    }
};

static_assert(sizeof(Ray) == 24, "Ray is two packed Vector3s");
static_assert(std::is_standard_layout_v<Ray>);
static_assert(std::is_trivially_copyable_v<Ray>);

// A zero direction has no ray to describe, so this leaves the default +Z
// rather than dividing by zero -- consistent with Normalize elsewhere.
MATHF_NODISCARD MATHF_INLINE constexpr Ray
NormalizeDirection(const Ray& ray) noexcept {
    const float lengthSq = LengthSq(ray.direction);
    if (!detail::IsFiniteNonZero(lengthSq)) {
        return Ray{ray.origin, Vector3{0.0f, 0.0f, 1.0f}};
    }
    return Ray{ray.origin, ray.direction * (1.0f / detail::ScalarSqrt(lengthSq))};
}

MATHF_NODISCARD MATHF_INLINE constexpr bool
operator==(const Ray& x, const Ray& y) noexcept {
    return x.origin == y.origin && x.direction == y.direction;
}

MATHF_NODISCARD MATHF_INLINE constexpr bool
NearEqual(const Ray& x, const Ray& y, float epsilon = 1e-5f) noexcept {
    return NearEqual(x.origin, y.origin, epsilon) &&
           NearEqual(x.direction, y.direction, epsilon);
}

} // namespace mathf

#endif // MATHF_RAY_HPP
