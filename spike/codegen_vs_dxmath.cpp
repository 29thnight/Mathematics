// Phase 0 codegen spike: does the constexpr dual-path wrapper cost anything at
// runtime? Each computation is written three ways and compiled to assembly:
//   A  = VecReg { __m128 }        (constexpr-capable, spike A)
//   C  = VecReg { float[4] }      (constexpr-capable, spike C)
//   DX = DirectXMath              (the performance baseline)
// The A/C bodies must match DX instruction-for-instruction, modulo scheduling.
//
// Inspect with:  cl /FA  ->  codegen_vs_dxmath.asm

#include <DirectXMath.h>
#include <xmmintrin.h>
#include <type_traits>

#define NOINLINE __declspec(noinline)
#define INLINE   __forceinline

// ---------------------------------------------------------------- strategy A
namespace sa {

struct VecReg { __m128 v; };

// Lane access is NOT portable: MSVC's __m128 is a union with m128_f32[],
// Clang/GCC's is a native vector type subscripted directly. Every compile-time
// path must go through these helpers.
INLINE constexpr float Lane(const __m128& v, int i) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    return v.m128_f32[i];
#else
    return v[i];
#endif
}
INLINE constexpr void SetLane(__m128& v, int i, float x) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    v.m128_f32[i] = x;
#else
    v[i] = x;
#endif
}

INLINE constexpr VecReg Set(float x, float y, float z, float w) noexcept {
    if (std::is_constant_evaluated()) {
        VecReg r{};
        SetLane(r.v, 0, x); SetLane(r.v, 1, y);
        SetLane(r.v, 2, z); SetLane(r.v, 3, w);
        return r;
    }
    return VecReg{_mm_set_ps(w, z, y, x)};
}

INLINE constexpr VecReg Add(VecReg a, VecReg b) noexcept {
    if (std::is_constant_evaluated()) {
        VecReg r{};
        for (int i = 0; i < 4; ++i)
            SetLane(r.v, i, Lane(a.v, i) + Lane(b.v, i));
        return r;
    }
    return VecReg{_mm_add_ps(a.v, b.v)};
}

INLINE constexpr VecReg Mul(VecReg a, VecReg b) noexcept {
    if (std::is_constant_evaluated()) {
        VecReg r{};
        for (int i = 0; i < 4; ++i)
            SetLane(r.v, i, Lane(a.v, i) * Lane(b.v, i));
        return r;
    }
    return VecReg{_mm_mul_ps(a.v, b.v)};
}

INLINE constexpr VecReg MulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    if (std::is_constant_evaluated()) return Add(Mul(a, b), c);
#if defined(__AVX2__)
    return VecReg{_mm_fmadd_ps(a.v, b.v, c.v)};
#else
    return VecReg{_mm_add_ps(_mm_mul_ps(a.v, b.v), c.v)};
#endif
}

INLINE constexpr VecReg Dot4(VecReg a, VecReg b) noexcept {
    if (std::is_constant_evaluated()) {
        float s = 0;
        for (int i = 0; i < 4; ++i) s += Lane(a.v, i) * Lane(b.v, i);
        return Set(s, s, s, s);
    }
    __m128 m = _mm_mul_ps(a.v, b.v);
    __m128 t = _mm_hadd_ps(m, m);
    return VecReg{_mm_hadd_ps(t, t)};
}

// compile-time proof the constexpr path is live
static_assert(Lane(Dot4(Set(1, 2, 3, 4), Set(1, 1, 1, 1)).v, 0) == 10.0f);
static_assert(Lane(MulAdd(Set(2, 0, 0, 0), Set(3, 0, 0, 0),
                          Set(1, 0, 0, 0)).v, 0) == 7.0f);

} // namespace sa

// ---------------------------------------------------------------- strategy C
namespace sc {

struct alignas(16) VecReg { float f[4]; };

INLINE constexpr VecReg Set(float x, float y, float z, float w) noexcept {
    return VecReg{{x, y, z, w}};
}

INLINE constexpr VecReg MulAdd(VecReg a, VecReg b, VecReg c) noexcept {
    if (std::is_constant_evaluated()) {
        return VecReg{{a.f[0] * b.f[0] + c.f[0], a.f[1] * b.f[1] + c.f[1],
                       a.f[2] * b.f[2] + c.f[2], a.f[3] * b.f[3] + c.f[3]}};
    }
    VecReg r{};
#if defined(__AVX2__)
    _mm_store_ps(r.f, _mm_fmadd_ps(_mm_load_ps(a.f), _mm_load_ps(b.f),
                                   _mm_load_ps(c.f)));
#else
    _mm_store_ps(r.f, _mm_add_ps(_mm_mul_ps(_mm_load_ps(a.f), _mm_load_ps(b.f)),
                                 _mm_load_ps(c.f)));
#endif
    return r;
}

static_assert(MulAdd(Set(2, 0, 0, 0), Set(3, 0, 0, 0), Set(1, 0, 0, 0)).f[0] == 7.0f);

} // namespace sc

// --------------------------------------------------------- codegen probes
// Same math, three implementations. Compare the emitted bodies.

NOINLINE sa::VecReg __vectorcall probe_a_muladd(sa::VecReg a, sa::VecReg b,
                                                sa::VecReg c) noexcept {
    return sa::MulAdd(a, b, c);
}

NOINLINE sc::VecReg __vectorcall probe_c_muladd(sc::VecReg a, sc::VecReg b,
                                                sc::VecReg c) noexcept {
    return sc::MulAdd(a, b, c);
}

NOINLINE DirectX::XMVECTOR XM_CALLCONV probe_dx_muladd(DirectX::XMVECTOR a,
                                                       DirectX::XMVECTOR b,
                                                       DirectX::XMVECTOR c) noexcept {
    return DirectX::XMVectorMultiplyAdd(a, b, c);
}

NOINLINE sa::VecReg __vectorcall probe_a_dot4(sa::VecReg a, sa::VecReg b) noexcept {
    return sa::Dot4(a, b);
}

NOINLINE DirectX::XMVECTOR XM_CALLCONV probe_dx_dot4(DirectX::XMVECTOR a,
                                                     DirectX::XMVECTOR b) noexcept {
    return DirectX::XMVector4Dot(a, b);
}

// Chained expression: exercises whether intermediates stay in registers.
NOINLINE sa::VecReg __vectorcall probe_a_chain(sa::VecReg a, sa::VecReg b,
                                               sa::VecReg c, sa::VecReg d) noexcept {
    return sa::Add(sa::MulAdd(a, b, c), sa::Mul(d, d));
}

NOINLINE DirectX::XMVECTOR XM_CALLCONV probe_dx_chain(DirectX::XMVECTOR a,
                                                      DirectX::XMVECTOR b,
                                                      DirectX::XMVECTOR c,
                                                      DirectX::XMVECTOR d) noexcept {
    return DirectX::XMVectorAdd(DirectX::XMVectorMultiplyAdd(a, b, c),
                                DirectX::XMVectorMultiply(d, d));
}

int main() { return 0; }
