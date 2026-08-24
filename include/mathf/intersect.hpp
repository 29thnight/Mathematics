// mathf/intersect.hpp — queries between two geometric primitives.
//
// Separated from the types themselves because every one of these needs two of
// them, and putting a sphere-versus-box test in either header would make that
// header depend on the other for no reason.
//
// Three shapes of answer, and which one a function returns is part of its name:
//
//   Intersects(a, b) -> bool          do they touch at all
//   Contains(a, b)   -> Containment   is b inside a, touching a, or apart
//   Classify(a, p)   -> PlaneSide     which side of plane p does a sit on
//   Raycast(...)     -> bool + float  does the ray hit, and how far along
//
// Touching counts as intersecting throughout: a sphere resting exactly on a
// plane, or two boxes sharing a face, are reported as intersecting rather than
// disjoint. Any other choice makes the predicate discontinuous in a way that
// shows up as objects flickering apart at exact contact.
#ifndef MATHF_INTERSECT_HPP
#define MATHF_INTERSECT_HPP

#include <mathf/bounds.hpp>
#include <mathf/plane.hpp>
#include <mathf/ray.hpp>

namespace mathf {

// ---------------------------------------------------------------- containment
MATHF_NODISCARD MATHF_INLINE constexpr bool
Intersects(const Sphere& sphere, const Vector3& point) noexcept {
    return LengthSq(point - sphere.center) <= sphere.radius * sphere.radius;
}

MATHF_NODISCARD MATHF_INLINE constexpr bool
Intersects(const AABB& box, const Vector3& point) noexcept {
    const Vector3 offset = point - box.center;
    return detail::AbsScalar(offset.x) <= box.extents.x &&
           detail::AbsScalar(offset.y) <= box.extents.y &&
           detail::AbsScalar(offset.z) <= box.extents.z;
}

// Two spheres overlap when their centres are closer than the radii sum.
// Compared squared, so no square root is needed.
MATHF_NODISCARD MATHF_INLINE constexpr bool
Intersects(const Sphere& x, const Sphere& y) noexcept {
    const float reach = x.radius + y.radius;
    return LengthSq(y.center - x.center) <= reach * reach;
}

// Per-axis overlap, which is the whole test for two axis-aligned boxes.
MATHF_NODISCARD MATHF_INLINE constexpr bool
Intersects(const AABB& x, const AABB& y) noexcept {
    const Vector3 offset = y.center - x.center;
    return detail::AbsScalar(offset.x) <= x.extents.x + y.extents.x &&
           detail::AbsScalar(offset.y) <= x.extents.y + y.extents.y &&
           detail::AbsScalar(offset.z) <= x.extents.z + y.extents.z;
}

// The sphere reaches the box exactly when the box's nearest point is within
// the radius. That one identity is the entire test, and it is why ClosestPoint
// is a named function.
MATHF_NODISCARD MATHF_INLINE constexpr bool
Intersects(const AABB& box, const Sphere& sphere) noexcept {
    const Vector3 nearest = ClosestPoint(box, sphere.center);
    return LengthSq(sphere.center - nearest) <= sphere.radius * sphere.radius;
}
MATHF_NODISCARD MATHF_INLINE constexpr bool
Intersects(const Sphere& sphere, const AABB& box) noexcept {
    return Intersects(box, sphere);
}

// Contains(a, b) asks whether b is entirely inside a -- see the note on
// Containment in bounds.hpp, because the asymmetry catches people out.
MATHF_NODISCARD MATHF_INLINE constexpr Containment
Contains(const Sphere& outer, const Sphere& inner) noexcept {
    if (!Intersects(outer, inner)) return Containment::Disjoint;
    // Inside means the far side of the inner sphere still fits.
    const float distance = Length(inner.center - outer.center);
    return (distance + inner.radius <= outer.radius) ? Containment::Contains
                                                     : Containment::Intersects;
}

MATHF_NODISCARD MATHF_INLINE constexpr Containment
Contains(const AABB& outer, const AABB& inner) noexcept {
    if (!Intersects(outer, inner)) return Containment::Disjoint;

    const Vector3 offset = inner.center - outer.center;
    const bool inside =
        detail::AbsScalar(offset.x) + inner.extents.x <= outer.extents.x &&
        detail::AbsScalar(offset.y) + inner.extents.y <= outer.extents.y &&
        detail::AbsScalar(offset.z) + inner.extents.z <= outer.extents.z;
    return inside ? Containment::Contains : Containment::Intersects;
}

MATHF_NODISCARD MATHF_INLINE constexpr Containment
Contains(const AABB& box, const Sphere& sphere) noexcept {
    if (!Intersects(box, sphere)) return Containment::Disjoint;

    const Vector3 offset = sphere.center - box.center;
    const bool inside =
        detail::AbsScalar(offset.x) + sphere.radius <= box.extents.x &&
        detail::AbsScalar(offset.y) + sphere.radius <= box.extents.y &&
        detail::AbsScalar(offset.z) + sphere.radius <= box.extents.z;
    return inside ? Containment::Contains : Containment::Intersects;
}

MATHF_NODISCARD MATHF_INLINE constexpr Containment
Contains(const Sphere& sphere, const AABB& box) noexcept {
    if (!Intersects(sphere, box)) return Containment::Disjoint;

    // Every corner inside means the whole box is, since a sphere is convex.
    // The farthest corner alone decides it: it is the one at maximum extent in
    // the direction away from the centre.
    const Vector3 offset = box.center - sphere.center;
    const Vector3 farthest{
        detail::AbsScalar(offset.x) + box.extents.x,
        detail::AbsScalar(offset.y) + box.extents.y,
        detail::AbsScalar(offset.z) + box.extents.z};
    return (LengthSq(farthest) <= sphere.radius * sphere.radius)
               ? Containment::Contains
               : Containment::Intersects;
}

MATHF_NODISCARD MATHF_INLINE constexpr Containment
Contains(const Sphere& sphere, const Vector3& point) noexcept {
    return Intersects(sphere, point) ? Containment::Contains
                                     : Containment::Disjoint;
}
MATHF_NODISCARD MATHF_INLINE constexpr Containment
Contains(const AABB& box, const Vector3& point) noexcept {
    return Intersects(box, point) ? Containment::Contains
                                  : Containment::Disjoint;
}

// -------------------------------------------------------------- versus plane
// Front means the whole volume sits on the side the normal points to. These
// assume a unit normal -- see plane.hpp; with an unnormalized one the radius
// and the distance are in different units and the comparison is meaningless.
MATHF_NODISCARD MATHF_INLINE constexpr PlaneSide
Classify(const Sphere& sphere, const Plane& plane) noexcept {
    const float distance = SignedDistance(plane, sphere.center);
    if (distance > sphere.radius) return PlaneSide::Front;
    if (distance < -sphere.radius) return PlaneSide::Back;
    return PlaneSide::Straddling;
}

// The box's extent along the plane normal is the sum of its half-widths
// projected onto it, which for an axis-aligned box is the dot with the
// normal's absolute value. Comparing that against the centre distance is the
// standard separating-axis test, and it needs no corner enumeration.
MATHF_NODISCARD MATHF_INLINE constexpr PlaneSide
Classify(const AABB& box, const Plane& plane) noexcept {
    const float reach = box.extents.x * detail::AbsScalar(plane.a) +
                        box.extents.y * detail::AbsScalar(plane.b) +
                        box.extents.z * detail::AbsScalar(plane.c);
    const float distance = SignedDistance(plane, box.center);
    if (distance > reach) return PlaneSide::Front;
    if (distance < -reach) return PlaneSide::Back;
    return PlaneSide::Straddling;
}

// ------------------------------------------------------------------- raycast
// Every raycast reports the distance along the ray's direction, and a ray that
// starts inside reports zero. See ray.hpp for what that distance is in, which
// depends on whether the direction is normalized.

// Substituting the ray into the sphere equation gives a quadratic in t. The
// linear coefficient is folded in advance (b is half the usual one), which
// removes the factors of two and the 4ac from the discriminant.
MATHF_NODISCARD MATHF_INLINE constexpr bool
Raycast(const Ray& ray, const Sphere& sphere, float& distanceOut) noexcept {
    const Vector3 toCenter = ray.origin - sphere.center;
    const float a = LengthSq(ray.direction);
    if (!detail::IsFiniteNonZero(a)) return false;

    const float b = Dot(toCenter, ray.direction);
    const float c = LengthSq(toCenter) - sphere.radius * sphere.radius;

    // Pointing away with the origin outside: no root can be positive.
    if (c > 0.0f && b > 0.0f) return false;

    const float discriminant = b * b - a * c;
    if (discriminant < 0.0f) return false;

    const float root = detail::ScalarSqrt(discriminant);
    const float t = (-b - root) / a;
    // A negative near root means the origin is inside; the surface is behind,
    // so the hit is at the origin itself.
    distanceOut = t < 0.0f ? 0.0f : t;
    return true;
}

// The slab method: clip the ray against each pair of parallel faces in turn
// and keep the running entry and exit distances. They cross when the ray
// misses.
//
// Division by a zero direction component is deliberate rather than guarded.
// IEEE-754 gives +/-infinity there, and the comparisons that follow order
// infinities correctly, so a ray exactly parallel to a slab falls out right
// with no branch. The one case that needs care is an origin exactly on a face
// of a zero-width slab, where the division is 0/0 -- that NaN would make every
// comparison false and silently report a miss, so it is checked for.
MATHF_NODISCARD MATHF_INLINE constexpr bool
Raycast(const Ray& ray, const AABB& box, float& distanceOut) noexcept {
    const Vector3 minimum = box.Min();
    const Vector3 maximum = box.Max();

    float entry = 0.0f;                        // a ray starting inside hits at 0
    float exit = consteval_ops::kInfinity;

    const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float direction[3] = {ray.direction.x, ray.direction.y,
                                ray.direction.z};
    const float low[3] = {minimum.x, minimum.y, minimum.z};
    const float high[3] = {maximum.x, maximum.y, maximum.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0.0f) {
            // Parallel to this slab: either always inside it or never.
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return false;
            }
            continue;
        }
        const float inverse = 1.0f / direction[axis];
        float near = (low[axis] - origin[axis]) * inverse;
        float far = (high[axis] - origin[axis]) * inverse;
        if (near > far) {
            const float swap = near;
            near = far;
            far = swap;
        }
        if (near > entry) entry = near;
        if (far < exit) exit = far;
        if (entry > exit) return false;
    }

    distanceOut = entry;
    return true;
}

// A plane is two-sided here: a ray hits it from either face. It misses only
// when it runs parallel, and a ray lying exactly in the plane counts as a miss
// rather than an infinity of hits -- there is no single distance to report.
MATHF_NODISCARD MATHF_INLINE constexpr bool
Raycast(const Ray& ray, const Plane& plane, float& distanceOut) noexcept {
    const float slope = DotNormal(plane, ray.direction);
    if (slope == 0.0f) return false;

    const float t = -SignedDistance(plane, ray.origin) / slope;
    if (t < 0.0f) return false;   // the plane is behind the origin

    distanceOut = t;
    return true;
}

// Moeller-Trumbore, which finds the barycentric coordinates and the distance
// without ever building the triangle's plane. Single-sided is a rendering
// choice, not a geometric one, so this hits from both faces and leaves the
// winding test to the caller who knows their convention.
MATHF_NODISCARD MATHF_INLINE constexpr bool
RaycastTriangle(const Ray& ray, const Vector3& v0, const Vector3& v1,
                const Vector3& v2, float& distanceOut) noexcept {
    const Vector3 edge1 = v1 - v0;
    const Vector3 edge2 = v2 - v0;
    const Vector3 pvec = Cross(ray.direction, edge2);
    const float determinant = Dot(edge1, pvec);

    // Zero determinant means the ray is parallel to the triangle's plane.
    if (!detail::IsFiniteNonZero(determinant)) return false;

    const float inverse = 1.0f / determinant;
    const Vector3 tvec = ray.origin - v0;
    const float u = Dot(tvec, pvec) * inverse;
    if (u < 0.0f || u > 1.0f) return false;

    const Vector3 qvec = Cross(tvec, edge1);
    const float v = Dot(ray.direction, qvec) * inverse;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = Dot(edge2, qvec) * inverse;
    if (t < 0.0f) return false;

    distanceOut = t;
    return true;
}

} // namespace mathf

#endif // MATHF_INTERSECT_HPP
