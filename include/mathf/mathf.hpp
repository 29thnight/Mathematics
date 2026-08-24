// mathf/mathf.hpp — everything, for code that does not care about include cost.
//
// Prefer the individual headers in a translation unit that only needs part of
// the library: each one is self-contained and they are deliberately small.
// This exists so a consumer can write one include and get on with it.
//
// Conventions, all of them DirectXMath's and all of them verified against it
// rather than assumed:
//
//   Storage        row-major matrices, m[row][col], a row contiguous
//   Vectors        row vectors, transformed as `v * M`
//   Composition    left to right in application order, for matrices AND
//                  quaternions -- `a * b` is a followed by b
//   Translation    row 3 of a Matrix4x4
//   Quaternions    (x, y, z, w), scalar last; axis-angle is
//                  (axis * sin(t/2), cos(t/2))
//   Handedness     never a default -- LookAtLH/RH, PerspectiveFovLH/RH, ...
//   Depth          clip z from 0 at the near plane to 1 at the far plane
//   Angles         radians; Degrees() and Radians() convert
//
// Everything is constexpr-usable, which is the line DirectXMath cannot cross.
#ifndef MATHF_MATHF_HPP
#define MATHF_MATHF_HPP

#include <mathf/config.hpp>
#include <mathf/geometry.hpp>
#include <mathf/matrix.hpp>
#include <mathf/quaternion.hpp>
#include <mathf/scalar.hpp>
#include <mathf/transform.hpp>
#include <mathf/vec_reg.hpp>
#include <mathf/vector.hpp>

#endif // MATHF_MATHF_HPP
