// mathf/geometry.hpp — the geometric primitives and the queries between them.
//
// Conventions, all matched against DirectXMath and DirectXCollision:
//
//   Plane      (a, b, c, d), d = -dot(normal, point); positive distance is
//              the FRONT, the side the normal points to
//   AABB       centre and HALF-WIDTHS, not min and max -- FromMinMax exists
//              because the two are the same shape and confusing them is silent
//   Ray        a half-line; distances run along the direction and a hit
//              behind the origin is not a hit
//   Contains   asymmetric: Contains(a, b) asks whether b is inside a
//   Touching   counts as intersecting, everywhere
#ifndef MATHF_GEOMETRY_HPP
#define MATHF_GEOMETRY_HPP

#include <mathf/bounds.hpp>
#include <mathf/intersect.hpp>
#include <mathf/plane.hpp>
#include <mathf/ray.hpp>

#endif // MATHF_GEOMETRY_HPP
