// mathf/vector_common.hpp — operations shared by Vector2, Vector3 and Vector4.
//
// The three types differ only in how many lanes take part. Everything that does
// not depend on that count is defined once here, constrained on a concept, so
// the three headers carry only their own storage, constructors, and the handful
// of genuinely dimension-specific operations (Cross, and the 2D scalar cross).
//
// Every operation promotes to VecReg, computes there, and comes back. The types
// themselves stay packed -- Vector3 is twelve bytes, not a padded register --
// so they drop into vertex buffers and struct members unchanged. Force-inlining
// is what keeps a chain of operations in registers between the promotion and
// the store; docs/PLAN.md Phase 2 records the measurement that has to hold for
// that to be true.
#ifndef MATHF_VECTOR_COMMON_HPP
#define MATHF_VECTOR_COMMON_HPP

#include <mathf/vec_reg.hpp>

#include <cmath>
#include <concepts>

namespace mathf {

namespace detail {

// A packed vector type: knows how many lanes it uses and converts both ways to
// the register type.
template <typename V>
concept VectorLike = requires(const V& v, VecReg r) {
    { V::kLanes } -> std::convertible_to<int>;
    { v.Reg() } -> std::same_as<VecReg>;
    { V::FromReg(r) } -> std::same_as<V>;
};

// Dot over exactly the lanes the type uses. Vector3 loads w as zero, so Dot4
// would give the same answer, but selecting by width keeps the emitted dpps
// immediate the same as DirectXMath's and makes the intent explicit.
template <int Lanes>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
DotLanes(VecReg a, VecReg b) noexcept {
    static_assert(Lanes >= 2 && Lanes <= 4, "vectors have 2, 3 or 4 lanes");
    if constexpr (Lanes == 2) return Dot2(a, b);
    else if constexpr (Lanes == 3) return Dot3(a, b);
    else return Dot4(a, b);
}

// Replaces the lanes a type does not own with one, for use as a divisor.
//
// Vector2 and Vector3 load their unused lanes as zero, so dividing one by
// another computes 0/0 there. At run time that is a NaN in a lane FromReg
// discards -- invisible. During constant evaluation it is undefined arithmetic,
// which is a hard error, and it would have made `constexpr` vector division
// impossible to write. One blend is a small price for the operation working in
// both contexts.
template <int Lanes>
MATHF_NODISCARD MATHF_INLINE constexpr VecReg
OneInUnusedLanes(VecReg v) noexcept {
    if constexpr (Lanes == 4) {
        return v;
    } else if constexpr (Lanes == 3) {
        return Select(MakeMaskReg(kLaneTrue, kLaneTrue, kLaneTrue, kLaneFalse),
                      v, Splat(1.0f));
    } else {
        return Select(MakeMaskReg(kLaneTrue, kLaneTrue, kLaneFalse, kLaneFalse),
                      v, Splat(1.0f));
    }
}

} // namespace detail

using detail::VectorLike;

// ---------------------------------------------------------------- arithmetic
//
// Lane-wise arithmetic is scalar for Vector2 and Vector3, and SIMD only for
// Vector4. That is measured, not assumed: promoting a packed three-float vector
// to a register costs a pack on the way in and three extracts on the way out,
// which is more than a single SIMD add saves over three scalar ones. A chained
// expression ran 5.25ns through the register and 3.2ns scalar; GLM, which is
// scalar for vec3, was faster than the SIMD version for exactly this reason.
//
// Reductions and geometry (Dot, Length, Normalize, Cross) still promote: there
// the work per round trip is high enough to pay for it, and Normalize measured
// faster than DirectXMath's.
namespace detail {

template <VectorLike V, typename Op>
MATHF_NODISCARD MATHF_INLINE constexpr V Componentwise(V a, V b, Op op) noexcept {
    V r;
    if constexpr (V::kLanes >= 2) { r.x = op(a.x, b.x); r.y = op(a.y, b.y); }
    if constexpr (V::kLanes >= 3) { r.z = op(a.z, b.z); }
    if constexpr (V::kLanes >= 4) { r.w = op(a.w, b.w); }
    return r;
}

} // namespace detail

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V operator+(V a, V b) noexcept {
    if constexpr (V::kLanes == 4) return V::FromReg(Add(a.Reg(), b.Reg()));
    else return detail::Componentwise(a, b, [](float p, float q) noexcept { return p + q; });
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V operator-(V a, V b) noexcept {
    if constexpr (V::kLanes == 4) return V::FromReg(Sub(a.Reg(), b.Reg()));
    else return detail::Componentwise(a, b, [](float p, float q) noexcept { return p - q; });
}

// Component-wise, following HLSL and GLM. The dot product is Dot, never `*`.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V operator*(V a, V b) noexcept {
    if constexpr (V::kLanes == 4) return V::FromReg(Mul(a.Reg(), b.Reg()));
    else return detail::Componentwise(a, b, [](float p, float q) noexcept { return p * q; });
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V operator/(V a, V b) noexcept {
    return V::FromReg(
        Div(a.Reg(), detail::OneInUnusedLanes<V::kLanes>(b.Reg())));
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V operator*(V a, float s) noexcept {
    if constexpr (V::kLanes == 4) return V::FromReg(Mul(a.Reg(), Splat(s)));
    else return detail::Componentwise(a, V{s}, [](float p, float q) noexcept { return p * q; });
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V operator*(float s, V a) noexcept {
    return a * s;
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V operator/(V a, float s) noexcept {
    return V::FromReg(Div(a.Reg(), Splat(s)));
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V operator-(V a) noexcept {
    return V::FromReg(Negate(a.Reg()));
}

template <VectorLike V>
MATHF_INLINE constexpr V& operator+=(V& a, V b) noexcept { return a = a + b; }
template <VectorLike V>
MATHF_INLINE constexpr V& operator-=(V& a, V b) noexcept { return a = a - b; }
template <VectorLike V>
MATHF_INLINE constexpr V& operator*=(V& a, V b) noexcept { return a = a * b; }
template <VectorLike V>
MATHF_INLINE constexpr V& operator*=(V& a, float s) noexcept { return a = a * s; }
template <VectorLike V>
MATHF_INLINE constexpr V& operator/=(V& a, V b) noexcept { return a = a / b; }
template <VectorLike V>
MATHF_INLINE constexpr V& operator/=(V& a, float s) noexcept { return a = a / s; }

// ---------------------------------------------------------------- comparison
// Exact equality. Approximate comparison is NearEqual, spelled out at the call
// site so nobody reaches for == and gets a tolerance they did not choose.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr bool operator==(V a, V b) noexcept {
    const MaskReg m = CmpEq(a.Reg(), b.Reg());
    return (MoveMask(m) & ((1 << V::kLanes) - 1)) == ((1 << V::kLanes) - 1);
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr bool
NearEqual(V a, V b, float epsilon = 1e-5f) noexcept {
    const VecReg diff = Abs(Sub(a.Reg(), b.Reg()));
    const MaskReg within = CmpLe(diff, Splat(epsilon));
    return (MoveMask(within) & ((1 << V::kLanes) - 1)) == ((1 << V::kLanes) - 1);
}

// ------------------------------------------------------------- lane-wise math
// Same width split as the operators above, and for the same measured reason:
// each of these is one SIMD instruction wrapped in a full register round trip,
// which does not pay for itself at two or three components.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V Abs(V a) noexcept {
    if constexpr (V::kLanes == 4) return V::FromReg(Abs(a.Reg()));
    // Sign bit cleared rather than a comparison, so -0.0f gives +0.0f and NaN
    // payloads survive -- matching what the SIMD path does.
    else return detail::Componentwise(a, a, [](float p, float) noexcept {
        return FromBits(BitsOf(p) & kAbsMask);
    });
}

// Min and Max follow minps: `a < b ? a : b`, not the equivalent-looking
// `b < a ? b : a`. The two differ only when an operand is NaN, and writing it
// the other way is how the backend's own transposed version slipped through
// (docs/PLAN.md Phase 1 rule 3).
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V Min(V a, V b) noexcept {
    if constexpr (V::kLanes == 4) return V::FromReg(Min(a.Reg(), b.Reg()));
    else return detail::Componentwise(a, b, [](float p, float q) noexcept {
        return p < q ? p : q;
    });
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V Max(V a, V b) noexcept {
    if constexpr (V::kLanes == 4) return V::FromReg(Max(a.Reg(), b.Reg()));
    else return detail::Componentwise(a, b, [](float p, float q) noexcept {
        return p > q ? p : q;
    });
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V Clamp(V v, V lo, V hi) noexcept {
    return Min(Max(v, lo), hi);
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V Saturate(V v) noexcept {
    return Clamp(v, V{}, V{1.0f});
}

// (1 - t) * a + t * b, evaluated as a + t * (b - a): one fused multiply-add, and
// exact at t == 0 and t == 1.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V Lerp(V a, V b, float t) noexcept {
    if constexpr (V::kLanes == 4) {
        const VecReg ra = a.Reg();
        return V::FromReg(MulAdd(Sub(b.Reg(), ra), Splat(t), ra));
    } else {
        return a + (b - a) * t;
    }
}

// ------------------------------------------------------------------ geometry
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr float Dot(V a, V b) noexcept {
    return GetX(detail::DotLanes<V::kLanes>(a.Reg(), b.Reg()));
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr float LengthSq(V v) noexcept {
    return Dot(v, v);
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr float Length(V v) noexcept {
    const VecReg r = v.Reg();
    return GetX(Sqrt(detail::DotLanes<V::kLanes>(r, r)));
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr float DistanceSq(V a, V b) noexcept {
    return LengthSq(b - a);
}

template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr float Distance(V a, V b) noexcept {
    return Length(b - a);
}

namespace detail {

// consteval_ops::SqrtScalar exists because std::sqrt is not constant-evaluable,
// and it pays for that with a Newton-Raphson loop in double. Calling it at run
// time is a catastrophe -- measured at 4.6x slower than the SIMD path when it
// slipped into a normalize by accident -- so the runtime side goes to std::sqrt.
MATHF_NODISCARD MATHF_INLINE constexpr float ScalarSqrt(float x) noexcept {
    MATHF_IF_CONSTEVAL { return consteval_ops::SqrtScalar(x); }
    return std::sqrt(x);
}

// Branchless, for Vector4. Every lane is computed and then selected.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V NormalizeWide(V v) noexcept {
    const VecReg r = v.Reg();
    const VecReg lengthSq = DotLanes<V::kLanes>(r, r);

    const VecReg zero = Zero();
    const VecReg infinity = Splat(consteval_ops::kInfinity);
    const MaskReg isZeroLength = CmpEq(lengthSq, zero);
    const MaskReg isFinite = CmpNe(lengthSq, infinity);

    // Divide by one where the length is zero. The quotient is discarded by the
    // select below either way, but 0/0 is undefined arithmetic and would abort
    // constant evaluation before the select ever ran -- Select is a blend, not a
    // branch, so both sides are always computed.
    const VecReg divisor = Select(isZeroLength, Splat(1.0f), Sqrt(lengthSq));
    const VecReg scaled = Div(r, divisor);

    return V::FromReg(Select(isZeroLength, zero,
                             Select(isFinite, scaled,
                                    Splat(consteval_ops::kQuietNaN))));
}

} // namespace detail

// Matches DirectXMath at the degenerate ends: a zero vector normalizes to zero
// rather than to NaN, and an infinite one to NaN. Getting that wrong is a silent
// source of NaN spreading through a scene graph, so it is worth paying for.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V Normalize(V v) noexcept {
    if constexpr (V::kLanes < 4) {
        // Scalar for the narrow types, for the same reason the lane-wise
        // operators are: at two or three components the register round trip
        // costs more than it saves. The degenerate cases become branches rather
        // than selects, and they predict essentially perfectly -- a scene's
        // vectors are not usually zero-length or infinite.
        const float lengthSq = LengthSq(v);
        if (lengthSq == 0.0f) return V{};
        if (lengthSq == consteval_ops::kInfinity) {
            return V{consteval_ops::kQuietNaN};
        }
        return v * (1.0f / detail::ScalarSqrt(lengthSq));
    } else {
        return detail::NormalizeWide(v);
    }
}

// The approximate form, with none of the guards above -- that is what makes it
// cheap. Feeding it a zero vector gives infinities.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE V NormalizeEst(V v) noexcept {
    const VecReg r = v.Reg();
    return V::FromReg(Mul(r, RSqrtEst(detail::DotLanes<V::kLanes>(r, r))));
}

// Mirrors `incident` about the surface described by `normal`, which is assumed
// to be unit length. i - 2 * dot(i, n) * n.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V
Reflect(V incident, V normal) noexcept {
    const VecReg i = incident.Reg();
    const VecReg n = normal.Reg();
    const VecReg twiceDot = Add(detail::DotLanes<V::kLanes>(i, n),
                                detail::DotLanes<V::kLanes>(i, n));
    return V::FromReg(NegMulAdd(twiceDot, n, i));
}

// Snell's law. `refractionIndex` is the ratio of the incident medium's index to
// the transmitted medium's. Total internal reflection returns the zero vector,
// which is what DirectXMath does and what callers branch on.
template <VectorLike V>
MATHF_NODISCARD MATHF_INLINE constexpr V
Refract(V incident, V normal, float refractionIndex) noexcept {
    const VecReg i = incident.Reg();
    const VecReg n = normal.Reg();
    const VecReg eta = Splat(refractionIndex);

    const VecReg dotIN = detail::DotLanes<V::kLanes>(i, n);
    // k = 1 - eta^2 * (1 - dot^2); negative means total internal reflection.
    const VecReg oneMinusDotSq = NegMulAdd(dotIN, dotIN, Splat(1.0f));
    const VecReg k = NegMulAdd(Mul(eta, eta), oneMinusDotSq, Splat(1.0f));

    const VecReg scale = MulAdd(eta, dotIN, Sqrt(k));
    const VecReg refracted = NegMulAdd(scale, n, Mul(eta, i));

    return V::FromReg(Select(CmpLe(k, Zero()), Zero(), refracted));
}

} // namespace mathf

#endif // MATHF_VECTOR_COMMON_HPP
