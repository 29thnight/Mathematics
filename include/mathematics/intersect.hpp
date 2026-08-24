// mathematics/intersect.hpp — queries between two geometric primitives.
//
// Separated from the types themselves because every one of these needs two of
// them, and putting a sphere-versus-box test in either header would make that
// header depend on the other for no reason.
//
// Three shapes of answer, and which one a function returns is part of its name:
//
//   Intersects(a, b) -> bool          do they touch at all
//   Contains(a, b)   -> containment   is b inside a, touching a, or apart
//   Classify(a, p)   -> plane_side     which side of plane p does a sit on
//   Raycast(...)     -> bool + float  does the ray hit, and how far along
//
// Touching counts as intersecting throughout: a sphere resting exactly on a
// plane, or two boxes sharing a face, are reported as intersecting rather than
// disjoint. Any other choice makes the predicate discontinuous in a way that
// shows up as objects flickering apart at exact contact.
#ifndef MATHEMATICS_INTERSECT_HPP
#define MATHEMATICS_INTERSECT_HPP

#include <mathematics/bounds.hpp>
#include <mathematics/plane.hpp>
#include <mathematics/ray.hpp>

#include <optional>

namespace math {

// ---------------------------------------------------------------- containment
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const sphere& input_sphere, const vector3& point) noexcept {
    return length_sq(point - input_sphere.center) <=
           input_sphere.radius * input_sphere.radius;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const aabb& box, const vector3& point) noexcept {
    if (box.is_empty()) return false;
    const vector3 offset = point - box.center;
    return detail::abs_scalar(offset.x) <= box.extents.x &&
           detail::abs_scalar(offset.y) <= box.extents.y &&
           detail::abs_scalar(offset.z) <= box.extents.z;
}

// Two spheres overlap when their centres are closer than the radii sum.
// Compared squared, so no square root is needed.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const sphere& x, const sphere& y) noexcept {
    const float reach = x.radius + y.radius;
    return length_sq(y.center - x.center) <= reach * reach;
}

// Per-axis overlap, which is the whole test for two axis-aligned boxes.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const aabb& x, const aabb& y) noexcept {
    if (x.is_empty() || y.is_empty()) return false;
    const vector3 offset = y.center - x.center;
    return detail::abs_scalar(offset.x) <= x.extents.x + y.extents.x &&
           detail::abs_scalar(offset.y) <= x.extents.y + y.extents.y &&
           detail::abs_scalar(offset.z) <= x.extents.z + y.extents.z;
}

// The sphere reaches the box exactly when the box's nearest point is within
// the radius. That one identity is the entire test, and it is why closest_point
// is a named function.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const aabb& box, const sphere& input_sphere) noexcept {
    if (box.is_empty()) return false;
    const vector3 nearest = closest_point(box, input_sphere.center);
    return length_sq(input_sphere.center - nearest) <=
           input_sphere.radius * input_sphere.radius;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
intersects(const sphere& input_sphere, const aabb& box) noexcept {
    return intersects(box, input_sphere);
}

// Contains(a, b) asks whether b is entirely inside a -- see the note on
// containment in bounds.hpp, because the asymmetry catches people out.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const sphere& outer, const sphere& inner) noexcept {
    if (!intersects(outer, inner)) return containment::disjoint;
    // Inside means the far side of the inner sphere still fits.
    const float distance = length(inner.center - outer.center);
    return (distance + inner.radius <= outer.radius) ? containment::contains
                                                     : containment::intersects;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const aabb& outer, const aabb& inner) noexcept {
    if (!intersects(outer, inner)) return containment::disjoint;

    const vector3 offset = inner.center - outer.center;
    const bool inside =
        detail::abs_scalar(offset.x) + inner.extents.x <= outer.extents.x &&
        detail::abs_scalar(offset.y) + inner.extents.y <= outer.extents.y &&
        detail::abs_scalar(offset.z) + inner.extents.z <= outer.extents.z;
    return inside ? containment::contains : containment::intersects;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const aabb& box, const sphere& input_sphere) noexcept {
    if (!intersects(box, input_sphere)) return containment::disjoint;

    const vector3 offset = input_sphere.center - box.center;
    const bool inside =
        detail::abs_scalar(offset.x) + input_sphere.radius <= box.extents.x &&
        detail::abs_scalar(offset.y) + input_sphere.radius <= box.extents.y &&
        detail::abs_scalar(offset.z) + input_sphere.radius <= box.extents.z;
    return inside ? containment::contains : containment::intersects;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const sphere& input_sphere, const aabb& box) noexcept {
    if (!intersects(input_sphere, box)) return containment::disjoint;

    // Every corner inside means the whole box is, since a sphere is convex.
    // The farthest corner alone decides it: it is the one at maximum extent in
    // the direction away from the centre.
    const vector3 offset = box.center - input_sphere.center;
    const vector3 farthest{
        detail::abs_scalar(offset.x) + box.extents.x,
        detail::abs_scalar(offset.y) + box.extents.y,
        detail::abs_scalar(offset.z) + box.extents.z};
    return (length_sq(farthest) <= input_sphere.radius * input_sphere.radius)
               ? containment::contains
               : containment::intersects;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const sphere& input_sphere, const vector3& point) noexcept {
    return intersects(input_sphere, point) ? containment::contains
                                           : containment::disjoint;
}
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr containment
contains(const aabb& box, const vector3& point) noexcept {
    return intersects(box, point) ? containment::contains
                                  : containment::disjoint;
}

// -------------------------------------------------------------- versus plane
// Front means the whole volume sits on the side the normal points to. These
// assume a unit normal -- see plane.hpp; with an unnormalized one the radius
// and the distance are in different units and the comparison is meaningless.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane_side
classify(const sphere& input_sphere, const plane& input_plane) noexcept {
    const float distance = signed_distance(input_plane, input_sphere.center);
    if (distance > input_sphere.radius) return plane_side::front;
    if (distance < -input_sphere.radius) return plane_side::back;
    return plane_side::straddling;
}

// The box's extent along the plane normal is the sum of its half-widths
// projected onto it, which for an axis-aligned box is the dot with the
// normal's absolute value. Comparing that against the centre distance is the
// standard separating-axis test, and it needs no corner enumeration.
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr plane_side
classify(const aabb& box, const plane& input_plane) noexcept {
    // An empty volume has no side. Straddling is conservative for callers that
    // use this result for culling: it cannot incorrectly discard anything.
    if (box.is_empty()) return plane_side::straddling;
    const float reach = box.extents.x * detail::abs_scalar(input_plane.a) +
                        box.extents.y * detail::abs_scalar(input_plane.b) +
                        box.extents.z * detail::abs_scalar(input_plane.c);
    const float distance = signed_distance(input_plane, box.center);
    if (distance > reach) return plane_side::front;
    if (distance < -reach) return plane_side::back;
    return plane_side::straddling;
}

// ------------------------------------------------------------------- raycast
// Every raycast reports the distance along the ray's direction, and a ray that
// starts inside reports zero. See ray.hpp for what that distance is in, which
// depends on whether the direction is normalized.

// Substituting the ray into the sphere equation gives a quadratic in t. The
// linear coefficient is folded in advance (b is half the usual one), which
// removes the factors of two and the 4ac from the discriminant.
MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast returns true")
MATHEMATICS_INLINE constexpr bool
raycast(const ray& input_ray, const sphere& input_sphere,
        float& distance_out) noexcept {
    const vector3 to_center = input_ray.origin - input_sphere.center;
    const float a = length_sq(input_ray.direction);
    if (!detail::is_finite_non_zero(a)) return false;

    const float b = dot(to_center, input_ray.direction);
    const float c = length_sq(to_center) -
                    input_sphere.radius * input_sphere.radius;

    // Pointing away with the origin outside: no root can be positive.
    if (c > 0.0f && b > 0.0f) return false;

    const float discriminant = b * b - a * c;
    if (discriminant < 0.0f) return false;

    const float root = detail::scalar_sqrt(discriminant);
    const float t = (-b - root) / a;
    // A negative near root means the origin is inside; the surface is behind,
    // so the hit is at the origin itself.
    distance_out = t < 0.0f ? 0.0f : t;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<float>
raycast(const ray& input_ray, const sphere& input_sphere) noexcept {
    float distance = 0.0f;
    if (!raycast(input_ray, input_sphere, distance)) return std::nullopt;
    return distance;
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
MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast returns true")
MATHEMATICS_INLINE constexpr bool
raycast(const ray& input_ray, const aabb& box, float& distance_out) noexcept {
    if (box.is_empty() ||
        !detail::is_finite_non_zero(length_sq(input_ray.direction))) {
        return false;
    }
    const vector3 minimum = box.min();
    const vector3 maximum = box.max();

    float entry = 0.0f;                        // a ray starting inside hits at 0
    float exit = consteval_ops::infinity;

    const float origin[3] = {input_ray.origin.x, input_ray.origin.y,
                             input_ray.origin.z};
    const float direction[3] = {input_ray.direction.x, input_ray.direction.y,
                                input_ray.direction.z};
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

    distance_out = entry;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<float>
raycast(const ray& input_ray, const aabb& box) noexcept {
    float distance = 0.0f;
    if (!raycast(input_ray, box, distance)) return std::nullopt;
    return distance;
}

// A plane is two-sided here: a ray hits it from either face. It misses only
// when it runs parallel, and a ray lying exactly in the plane counts as a miss
// rather than an infinity of hits -- there is no single distance to report.
MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast returns true")
MATHEMATICS_INLINE constexpr bool
raycast(const ray& input_ray, const plane& input_plane,
        float& distance_out) noexcept {
    const float slope = dot_normal(input_plane, input_ray.direction);
    if (slope == 0.0f) return false;

    const float t = -signed_distance(input_plane, input_ray.origin) / slope;
    if (t < 0.0f) return false;   // the plane is behind the origin

    distance_out = t;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<float>
raycast(const ray& input_ray, const plane& input_plane) noexcept {
    float distance = 0.0f;
    if (!raycast(input_ray, input_plane, distance)) return std::nullopt;
    return distance;
}

// Moeller-Trumbore, which finds the barycentric coordinates and the distance
// without ever building the triangle's plane. Single-sided is a rendering
// choice, not a geometric one, so this hits from both faces and leaves the
// winding test to the caller who knows their convention.
MATHEMATICS_NODISCARD_MSG("distance_out is valid only when raycast_triangle returns true")
MATHEMATICS_INLINE constexpr bool
raycast_triangle(const ray& input_ray, const vector3& v0, const vector3& v1,
                const vector3& v2, float& distance_out) noexcept {
    const vector3 edge1 = v1 - v0;
    const vector3 edge2 = v2 - v0;
    const vector3 pvec = cross(input_ray.direction, edge2);
    const float determinant = dot(edge1, pvec);

    // Zero determinant means the ray is parallel to the triangle's plane.
    if (!detail::is_finite_non_zero(determinant)) return false;

    const float inverse = 1.0f / determinant;
    const vector3 tvec = input_ray.origin - v0;
    const float u = dot(tvec, pvec) * inverse;
    if (u < 0.0f || u > 1.0f) return false;

    const vector3 qvec = cross(tvec, edge1);
    const float v = dot(input_ray.direction, qvec) * inverse;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = dot(edge2, qvec) * inverse;
    if (t < 0.0f) return false;

    distance_out = t;
    return true;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::optional<float>
raycast_triangle(const ray& input_ray, const vector3& v0, const vector3& v1,
                 const vector3& v2) noexcept {
    float distance = 0.0f;
    if (!raycast_triangle(input_ray, v0, v1, v2, distance)) return std::nullopt;
    return distance;
}

} // namespace math

#endif // MATHEMATICS_INTERSECT_HPP
