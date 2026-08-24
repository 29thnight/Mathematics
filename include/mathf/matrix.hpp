// mathf/matrix.hpp — the matrix types.
//
// Matrix4x4 and Matrix3x3 follow DirectXMath's convention throughout:
//
//   Storage        row-major, m[row][col]; a row is contiguous
//   Vectors        row vectors, transformed as `v * M`
//   Composition    left to right in application order --
//                  `world = scale * rotation * translation`
//   Translation    row 3 of a Matrix4x4, m[3][0..2]
//
// All four are pinned against DirectXMath by the tests rather than against
// Mathf's own output, because a matrix with any of them backwards still
// multiplies cleanly and simply renders everything in the wrong place.
//
//   Construction   Identity, element-wise
//   Access         M(row, col), and M[row, col] on C++23
//                  GetRow GetColumn Right Up Forward Translation
//   Arithmetic     * (matrix, and `v * M`) and *=
//   Operations     Transpose Determinant Inverse
//   Transform      TransformPoint (w = 1) TransformDirection (w = 0)
//   Comparison     == != (exact), NearEqual(a, b, epsilon)
//
// A matrix whose determinant is not a finite non-zero -- singular, overflowed,
// or holding a NaN -- inverts to the identity rather than to NaN. Ask
// Determinant first if that distinction matters, and see the note above
// Matrix4x4's Inverse for why it is the right question to ask: close to
// singular, whether Inverse itself reports singularity is not guaranteed to be
// the same at compile time as at run time.
#ifndef MATHF_MATRIX_HPP
#define MATHF_MATRIX_HPP

#include <mathf/matrix3x3.hpp>
#include <mathf/matrix4x4.hpp>

#endif // MATHF_MATRIX_HPP
