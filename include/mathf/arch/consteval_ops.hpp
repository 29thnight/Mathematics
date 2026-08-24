// mathf/arch/consteval_ops.hpp — what every operation *means*, defined once.
//
// Each SIMD backend selects instructions; this file defines the semantics those
// instructions must reproduce. It serves three roles at once:
//   1. the compile-time path of every SSE and NEON operation,
//   2. the entire scalar fallback backend,
//   3. the oracle the tests check the SIMD paths against.
// Because all three read the same definition, a backend cannot drift from the
// specification without a test noticing.
//
// Everything here is plain scalar arithmetic on lanes. Nothing in this file may
// call an intrinsic or a non-constexpr standard function.
#ifndef MATHF_ARCH_CONSTEVAL_OPS_HPP
#define MATHF_ARCH_CONSTEVAL_OPS_HPP

#include <mathf/arch/reg.hpp>

namespace mathf::consteval_ops {

inline constexpr float kQuietNaN = FromBits(0x7FC00000u);
inline constexpr float kInfinity = FromBits(0x7F800000u);

// ----------------------------------------------------------------- construction
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
Set(float x, float y, float z, float w) noexcept {
    return MakeReg(x, y, z, w);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Splat(float s) noexcept {
    return MakeReg(s, s, s, s);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Zero() noexcept {
    return MakeReg(0.0f, 0.0f, 0.0f, 0.0f);
}

// -------------------------------------------------------------------- lane-wise
// Every unary and binary operation is expressed through these two, fully
// unrolled with literal lane indices. Constant indices matter beyond style: NEON
// lane intrinsics require them, and a runtime index leaves an indexable array in
// the frame that trips MSVC's /GS analysis.
template <typename Op>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg Unary(VecReg a, Op op) noexcept {
    return MakeReg(op(Lane(a, 0)), op(Lane(a, 1)), op(Lane(a, 2)), op(Lane(a, 3)));
}

template <typename Op>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
Binary(VecReg a, VecReg b, Op op) noexcept {
    return MakeReg(op(Lane(a, 0), Lane(b, 0)), op(Lane(a, 1), Lane(b, 1)),
                   op(Lane(a, 2), Lane(b, 2)), op(Lane(a, 3), Lane(b, 3)));
}

template <typename Op>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
Ternary(VecReg a, VecReg b, VecReg c, Op op) noexcept {
    return MakeReg(op(Lane(a, 0), Lane(b, 0), Lane(c, 0)),
                   op(Lane(a, 1), Lane(b, 1), Lane(c, 1)),
                   op(Lane(a, 2), Lane(b, 2), Lane(c, 2)),
                   op(Lane(a, 3), Lane(b, 3), Lane(c, 3)));
}

// ------------------------------------------------------------------- arithmetic
MATHF_NODISCARD MATHF_INLINE constexpr VecReg Add(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept { return x + y; });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Sub(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept { return x - y; });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Mul(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept { return x * y; });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Div(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept { return x / y; });
}

// Sign flipped by XOR rather than by 0 - x, so -0.0f negates to +0.0f and NaN
// payloads survive. This matches what the SIMD paths do.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg Negate(VecReg a) noexcept {
    return Unary(a, [](float x) noexcept { return FromBits(BitsOf(x) ^ kSignBit); });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Abs(VecReg a) noexcept {
    return Unary(a, [](float x) noexcept { return FromBits(BitsOf(x) & kAbsMask); });
}

// a * b + c. Written as separate operations, not std::fma, because std::fma is
// not constant-evaluable. The hardware FMA keeps the product at full width, so a
// SIMD result may differ from this in the last places -- tests compare MulAdd
// with a tolerance for exactly that reason.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
MulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    return Ternary(a, b, c,
                   [](float x, float y, float z) noexcept { return x * y + z; });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg
MulSub(VecReg a, VecReg b, VecReg c) noexcept {
    return Ternary(a, b, c,
                   [](float x, float y, float z) noexcept { return x * y - z; });
}

// c - a * b
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
NegMulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    return Ternary(a, b, c,
                   [](float x, float y, float z) noexcept { return z - x * y; });
}

// Written exactly as minps/maxps define themselves -- `a < b ? a : b`, not the
// equivalent-looking `b < a ? b : a`. The two agree on every ordered pair and
// differ only when an operand is NaN, since a comparison against NaN is false
// either way and the two forms then fall through to opposite operands. The
// result is that Min(NaN, x) is x and Min(x, NaN) is NaN.
//
// The transposed form passed random-input parity for exactly this reason and was
// caught only by the explicit NaN test in the scalar build.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg Min(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept { return x < y ? x : y; });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Max(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept { return x > y ? x : y; });
}

// ------------------------------------------------------------ roots, reciprocals
// std::sqrt is not constant-evaluable before C++26, so this is Newton-Raphson in
// double precision. The seed halves the exponent in the bit pattern, which lands
// within a few percent and lets the iteration converge in a handful of steps
// regardless of magnitude. Compile-time cost is irrelevant; correctness is not.
MATHF_NODISCARD MATHF_INLINE constexpr float SqrtScalar(float x) noexcept {
    if (x != x) return x;                       // NaN propagates unchanged
    if (x < 0.0f) return kQuietNaN;             // domain error
    if (x == 0.0f) return x;                    // preserves signed zero
    if (x == kInfinity) return x;

    const float seed = FromBits((BitsOf(x) >> 1) + 0x1FC00000u);
    double g = static_cast<double>(seed);
    const double v = static_cast<double>(x);
    for (int i = 0; i < 16; ++i) {
        const double next = 0.5 * (g + v / g);
        if (next == g) break;
        g = next;
    }
    return static_cast<float>(g);
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Sqrt(VecReg a) noexcept {
    return Unary(a, [](float x) noexcept { return SqrtScalar(x); });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg RSqrt(VecReg a) noexcept {
    return Unary(a, [](float x) noexcept { return 1.0f / SqrtScalar(x); });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Recip(VecReg a) noexcept {
    return Unary(a, [](float x) noexcept { return 1.0f / x; });
}

// --------------------------------------------------------------------- bitwise
MATHF_NODISCARD MATHF_INLINE constexpr VecReg And(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return FromBits(BitsOf(x) & BitsOf(y));
    });
}

// ~a & b, matching _mm_andnot_ps: the first operand is the one inverted.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg AndNot(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return FromBits(~BitsOf(x) & BitsOf(y));
    });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Or(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return FromBits(BitsOf(x) | BitsOf(y));
    });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Xor(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return FromBits(BitsOf(x) ^ BitsOf(y));
    });
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Not(VecReg a) noexcept {
    return Unary(a, [](float x) noexcept { return FromBits(~BitsOf(x)); });
}

// ------------------------------------------------------------------ comparison
// Results are masks: every lane is all-ones or all-zeros (see reg.hpp).
namespace detail {

MATHF_NODISCARD MATHF_INLINE constexpr float MaskOf(bool b) noexcept {
    return FromBits(b ? kLaneTrue : kLaneFalse);
}

} // namespace detail

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg CmpEq(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return detail::MaskOf(x == y);
    });
}

// Ordered-false / unordered-true, matching cmpneq_ps: NaN compares not-equal to
// everything including itself.
MATHF_NODISCARD MATHF_INLINE constexpr MaskReg CmpNe(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return detail::MaskOf(!(x == y));
    });
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg CmpLt(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return detail::MaskOf(x < y);
    });
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg CmpLe(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return detail::MaskOf(x <= y);
    });
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg CmpGt(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return detail::MaskOf(x > y);
    });
}

MATHF_NODISCARD MATHF_INLINE constexpr MaskReg CmpGe(VecReg a, VecReg b) noexcept {
    return Binary(a, b, [](float x, float y) noexcept {
        return detail::MaskOf(x >= y);
    });
}

// Bitwise blend, not a branch: lanes where the mask is set come from ifTrue.
// A mask lane that is neither all-ones nor all-zeros blends bit by bit, which
// mirrors the hardware rather than pretending it cannot happen.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
Select(MaskReg mask, VecReg ifTrue, VecReg ifFalse) noexcept {
    return Ternary(mask, ifTrue, ifFalse,
                   [](float m, float t, float f) noexcept {
                       const std::uint32_t mb = BitsOf(m);
                       return FromBits((BitsOf(t) & mb) | (BitsOf(f) & ~mb));
                   });
}

// ------------------------------------------------------------------- predicates
// One bit per lane, taken from each lane's sign bit -- the same definition as
// movmskps, so a mask lane of all-ones reads as set.
MATHF_NODISCARD MATHF_INLINE constexpr int MoveMask(VecReg a) noexcept {
    return static_cast<int>(((LaneBits(a, 0) >> 31) << 0) |
                            ((LaneBits(a, 1) >> 31) << 1) |
                            ((LaneBits(a, 2) >> 31) << 2) |
                            ((LaneBits(a, 3) >> 31) << 3));
}

MATHF_NODISCARD MATHF_INLINE constexpr bool AllTrue(MaskReg m) noexcept {
    return MoveMask(m) == 0xF;
}

MATHF_NODISCARD MATHF_INLINE constexpr bool AnyTrue(MaskReg m) noexcept {
    return MoveMask(m) != 0;
}

// ------------------------------------------------------------------- reductions
// Dot products splat the scalar result across all four lanes, matching
// DirectXMath, so the result feeds straight back into vector arithmetic without
// a broadcast step.
MATHF_NODISCARD MATHF_INLINE constexpr VecReg Dot2(VecReg a, VecReg b) noexcept {
    return Splat(Lane(a, 0) * Lane(b, 0) + Lane(a, 1) * Lane(b, 1));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Dot3(VecReg a, VecReg b) noexcept {
    return Splat(Lane(a, 0) * Lane(b, 0) + Lane(a, 1) * Lane(b, 1)
               + Lane(a, 2) * Lane(b, 2));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg Dot4(VecReg a, VecReg b) noexcept {
    return Splat(Lane(a, 0) * Lane(b, 0) + Lane(a, 1) * Lane(b, 1)
               + Lane(a, 2) * Lane(b, 2) + Lane(a, 3) * Lane(b, 3));
}

// ---------------------------------------------------------------------- shuffle
// Lane indices are template parameters because they must be compile-time
// constants: NEON's lane intrinsics and SSE's shuffle immediate both require it.
template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg Shuffle(VecReg a) noexcept {
    static_assert(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 &&
                      W >= 0 && W < 4,
                  "shuffle lane indices must be in [0, 4)");
    return MakeReg(Lane(a, X), Lane(a, Y), Lane(a, Z), Lane(a, W));
}

// Two-source shuffle following _mm_shuffle_ps: the low two lanes come from a,
// the high two from b.
template <int X, int Y, int Z, int W>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg Shuffle(VecReg a, VecReg b) noexcept {
    static_assert(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 &&
                      W >= 0 && W < 4,
                  "shuffle lane indices must be in [0, 4)");
    return MakeReg(Lane(a, X), Lane(a, Y), Lane(b, Z), Lane(b, W));
}

MATHF_NODISCARD MATHF_INLINE constexpr VecReg SplatX(VecReg a) noexcept {
    return Shuffle<0, 0, 0, 0>(a);
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg SplatY(VecReg a) noexcept {
    return Shuffle<1, 1, 1, 1>(a);
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg SplatZ(VecReg a) noexcept {
    return Shuffle<2, 2, 2, 2>(a);
}
MATHF_NODISCARD MATHF_INLINE constexpr VecReg SplatW(VecReg a) noexcept {
    return Shuffle<3, 3, 3, 3>(a);
}

} // namespace mathf::consteval_ops

#endif // MATHF_ARCH_CONSTEVAL_OPS_HPP
