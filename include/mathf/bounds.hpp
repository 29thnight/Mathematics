// mathf/bounds.hpp — bounding volumes: Sphere and AABB.
//
// AABB stores a CENTRE AND HALF-WIDTHS, not a minimum and a maximum. That is
// DirectXMath's BoundingBox layout, and it is the trap in this file: the two
// representations have the same shape -- two Vector3s -- so passing a min and a
// max to the constructor compiles and silently describes a different box.
// FromMinMax exists precisely so that call site can say what it means, and the
// member names (center, extents) are chosen so a designated initializer reads
// unambiguously.
//
// Centre-and-extents is not an arbitrary inheritance. It makes the overlap test
// a subtraction and a comparison per axis with no branches, and it is what a
// separating-axis test wants; min/max would need a conversion at every query.
#ifndef MATHF_BOUNDS_HPP
#define MATHF_BOUNDS_HPP

#include <mathf/vector.hpp>

namespace mathf {

// How one volume sits relative to another. Note what Contains does NOT mean:
// `a.Contains(b)` says b is entirely inside a. A huge sphere that swallows a
// small box gives Contains(box, sphere) == Intersects, because the question is
// whether the BOX contains the sphere. DirectXMath uses the same vocabulary and
// the same asymmetry, and it is the first thing people get backwards.
enum class Containment {
    Disjoint,    // no overlap at all
    Intersects,  // overlap, but the argument is not entirely inside
    Contains,    // the argument is entirely inside the receiver
};

// -------------------------------------------------------------------- Sphere
struct Sphere {
    Vector3 center;
    float radius;

    constexpr Sphere() noexcept : center{0.0f, 0.0f, 0.0f}, radius(0.0f) {}

    constexpr Sphere(const Vector3& centerIn, float radiusIn) noexcept
        : center(centerIn), radius(radiusIn) {}
};

static_assert(sizeof(Sphere) == 16, "Sphere must stay packed");
static_assert(std::is_standard_layout_v<Sphere>);
static_assert(std::is_trivially_copyable_v<Sphere>);

// ---------------------------------------------------------------------- AABB
struct AABB {
    Vector3 center;
    Vector3 extents;   // HALF-widths: the box spans center +/- extents

    // An empty box at the origin. Zero extents make a point, which is the
    // identity for Merge -- growing an empty box by a point gives that point.
    constexpr AABB() noexcept : center{0.0f, 0.0f, 0.0f},
                                extents{0.0f, 0.0f, 0.0f} {}

    constexpr AABB(const Vector3& centerIn, const Vector3& extentsIn) noexcept
        : center(centerIn), extents(extentsIn) {}

    MATHF_NODISCARD constexpr Vector3 Min() const noexcept {
        return center - extents;
    }
    MATHF_NODISCARD constexpr Vector3 Max() const noexcept {
        return center + extents;
    }

    // The eight corners, indexed by bit: bit 0 is X, bit 1 is Y, bit 2 is Z,
    // set meaning the maximum side. So 0 is the minimum corner and 7 the
    // maximum, and iterating 0..7 visits every corner exactly once.
    MATHF_NODISCARD constexpr Vector3 Corner(int index) const noexcept {
        return Vector3{
            center.x + ((index & 1) ? extents.x : -extents.x),
            center.y + ((index & 2) ? extents.y : -extents.y),
            center.z + ((index & 4) ? extents.z : -extents.z)};
    }

    MATHF_NODISCARD static constexpr AABB
    FromMinMax(const Vector3& minimum, const Vector3& maximum) noexcept {
        return AABB{(minimum + maximum) * 0.5f, (maximum - minimum) * 0.5f};
    }
};

static_assert(sizeof(AABB) == 24, "AABB is two packed Vector3s");
static_assert(std::is_standard_layout_v<AABB>);
static_assert(std::is_trivially_copyable_v<AABB>);

// -------------------------------------------------------------------- growing
MATHF_NODISCARD MATHF_INLINE constexpr AABB
Merge(const AABB& box, const Vector3& point) noexcept {
    return AABB::FromMinMax(Min(box.Min(), point), Max(box.Max(), point));
}

MATHF_NODISCARD MATHF_INLINE constexpr AABB
Merge(const AABB& x, const AABB& y) noexcept {
    return AABB::FromMinMax(Min(x.Min(), y.Min()), Max(x.Max(), y.Max()));
}

// Grown by a uniform margin on every side. A negative margin shrinks, and can
// drive the extents negative -- every query below then treats the box as empty
// rather than as one turned inside out.
MATHF_NODISCARD MATHF_INLINE constexpr AABB
Expand(const AABB& box, float margin) noexcept {
    return AABB{box.center, box.extents + Vector3{margin, margin, margin}};
}

// The tightest box around a run of points. An empty range gives an empty box at
// the origin, which Merge then treats as the identity.
MATHF_NODISCARD MATHF_INLINE AABB
AABBFromPoints(const Vector3* points, int count) noexcept {
    if (points == nullptr || count <= 0) return AABB{};

    Vector3 minimum = points[0];
    Vector3 maximum = points[0];
    for (int i = 1; i < count; ++i) {
        minimum = Min(minimum, points[i]);
        maximum = Max(maximum, points[i]);
    }
    return AABB::FromMinMax(minimum, maximum);
}

// Grows a sphere just enough to swallow a point: the result touches both the
// old surface and the point, so its centre slides half the excess toward it.
// Growing only the radius would work too and would waste volume every time.
MATHF_NODISCARD MATHF_INLINE constexpr Sphere
Merge(const Sphere& sphere, const Vector3& point) noexcept {
    const Vector3 offset = point - sphere.center;
    const float distance = Length(offset);
    if (distance <= sphere.radius) return sphere;

    const float newRadius = (sphere.radius + distance) * 0.5f;
    const float t = (newRadius - sphere.radius) / distance;
    return Sphere{sphere.center + offset * t, newRadius};
}

// The sphere around a box -- its centre, with the half-diagonal as radius.
MATHF_NODISCARD MATHF_INLINE constexpr Sphere
BoundingSphere(const AABB& box) noexcept {
    return Sphere{box.center, Length(box.extents)};
}

// The box around a sphere.
MATHF_NODISCARD MATHF_INLINE constexpr AABB
BoundingBox(const Sphere& sphere) noexcept {
    return AABB{sphere.center,
                Vector3{sphere.radius, sphere.radius, sphere.radius}};
}

// --------------------------------------------------------------- closest point
// The point of the volume nearest the argument, which is the argument itself
// when it is already inside. This is the whole of the box-sphere test and half
// of several others, so it is named once rather than repeated.
MATHF_NODISCARD MATHF_INLINE constexpr Vector3
ClosestPoint(const AABB& box, const Vector3& point) noexcept {
    return Min(Max(point, box.Min()), box.Max());
}

MATHF_NODISCARD MATHF_INLINE constexpr Vector3
ClosestPoint(const Sphere& sphere, const Vector3& point) noexcept {
    const Vector3 offset = point - sphere.center;
    const float lengthSq = LengthSq(offset);
    if (lengthSq <= sphere.radius * sphere.radius) return point;
    // A point exactly at the centre has no nearest surface point; the centre
    // is the one answer that is not a lie about direction.
    if (!detail::IsFiniteNonZero(lengthSq)) return sphere.center;
    return sphere.center + offset * (sphere.radius / detail::ScalarSqrt(lengthSq));
}

// ------------------------------------------------------------------ comparison
MATHF_NODISCARD MATHF_INLINE constexpr bool
operator==(const Sphere& x, const Sphere& y) noexcept {
    return x.center == y.center && x.radius == y.radius;
}
MATHF_NODISCARD MATHF_INLINE constexpr bool
operator==(const AABB& x, const AABB& y) noexcept {
    return x.center == y.center && x.extents == y.extents;
}

// Positive tests, so a NaN fails rather than passing -- the matrix version of
// this shipped with the negated form and reported NaN as near-equal.
MATHF_NODISCARD MATHF_INLINE constexpr bool
NearEqual(const Sphere& x, const Sphere& y, float epsilon = 1e-5f) noexcept {
    const float dr = x.radius - y.radius;
    return NearEqual(x.center, y.center, epsilon) &&
           dr <= epsilon && dr >= -epsilon;
}
MATHF_NODISCARD MATHF_INLINE constexpr bool
NearEqual(const AABB& x, const AABB& y, float epsilon = 1e-5f) noexcept {
    return NearEqual(x.center, y.center, epsilon) &&
           NearEqual(x.extents, y.extents, epsilon);
}

} // namespace mathf

#endif // MATHF_BOUNDS_HPP
