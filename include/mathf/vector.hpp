// mathf/vector.hpp — the vector types.
//
// Vector2, Vector3 and Vector4 are packed, standard-layout types with the full
// operator set. They are what most code should use: `v.x` reaches a real member,
// a Vector3 array is a twelve-bytes-per-element position stream, and they sit in
// structs with no alignment demands.
//
// Operations promote to VecReg, compute there, and store back, all force-inlined
// so a chain of them stays in registers. Code that crosses non-inlined
// boundaries in a hot loop should hold VecReg directly instead
// (mathf/vec_reg.hpp).
//
//   Arithmetic   + - * / unary- and the compound forms; * and / also by scalar
//   Comparison   == != (exact), NearEqual(a, b, epsilon)
//   Lane-wise    Abs Min Max Clamp Saturate Lerp
//   Geometry     Dot Length LengthSq Distance DistanceSq
//                Normalize NormalizeEst Reflect Refract
//   Vector3      Cross
//   Vector2      Cross (scalar) Perpendicular
//
// Component-wise multiply is `*`, following HLSL and GLM; the dot product is
// always Dot, never an operator (docs/PLAN.md §2.7).
#ifndef MATHF_VECTOR_HPP
#define MATHF_VECTOR_HPP

#include <mathf/vector2.hpp>
#include <mathf/vector3.hpp>
#include <mathf/vector4.hpp>

#endif // MATHF_VECTOR_HPP
