// mathf/vec_reg.hpp — the register-level API.
//
// VecReg wraps one 128-bit SIMD register and is ALWAYS passed by value. It is
// not a storage type: Phase 2's Float2/3/4 own memory layout, and values promote
// to VecReg to compute. docs/SPIKE-RESULTS.md §3 measures why that separation is
// mandatory rather than stylistic -- a four-float struct is decomposed across
// four registers by __vectorcall and rebuilt at every call boundary.
//
// Every operation works at compile time and at run time, selecting between a
// scalar definition and the target's instructions. The Phase 0 spike verified
// the wrapper is free: runtime output is instruction-identical to DirectXMath.
//
//   Construction   Set Splat Zero
//   Memory         Load LoadAligned Store StoreAligned
//   Arithmetic     Add Sub Mul Div Negate Abs MulAdd MulSub NegMulAdd Min Max
//   Roots          Sqrt RSqrt RSqrtEst Recip RecipEst
//   Bitwise        And AndNot Or Xor Not
//   Comparison     CmpEq CmpNe CmpLt CmpLe CmpGt CmpGe Select
//   Predicates     MoveMask AllTrue AnyTrue
//   Reductions     Dot2 Dot3 Dot4
//   Shuffle        Shuffle<X,Y,Z,W> SplatX SplatY SplatZ SplatW
//   Lanes          Lane SetLane GetX GetY GetZ GetW
//
// Comparisons return masks whose lanes are all-ones or all-zeros, the same
// convention DirectXMath uses; Select consumes them. Because an all-ones lane
// reads as a NaN, mask values must only be combined bitwise, never used in
// arithmetic.
//
// Two behaviours are target-specific and deliberately not normalised:
//   - Min/Max with a NaN operand (see the note in arch/simd_neon.hpp)
//   - RSqrtEst/RecipEst precision, which is approximate on SSE and NEON and
//     exact on the scalar fallback
#ifndef MATHF_VEC_REG_HPP
#define MATHF_VEC_REG_HPP

#include <mathf/arch/simd_select.hpp>

#endif // MATHF_VEC_REG_HPP
