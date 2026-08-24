// Phase 0 codegen spike: does the constexpr dual-path wrapper cost anything at
// runtime? Each computation is written three ways and compiled to assembly:
//   A  = vec_reg { __m128 }        (constexpr-capable, spike A)
//   C  = vec_reg { float[4] }      (constexpr-capable, spike C)
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

struct vec_reg { __m128 v; };

// Lane access is not portable: MSVC's __m128 is a union with m128_f32[],
// Clang/GCC's is a native vector type subscripted directly. Every compile-time
// path must go through these helpers.
INLINE constexpr float lane(const __m128& v, int i) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    return v.m128_f32[i];
#else
    return v[i];
#endif
}
INLINE constexpr void set_lane(__m128& v, int i, float x) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    v.m128_f32[i] = x;
#else
    v[i] = x;
#endif
}

INLINE constexpr vec_reg set(float x, float y, float z, float w) noexcept {
    if (std::is_constant_evaluated()) {
        vec_reg r{};
        set_lane(r.v, 0, x); set_lane(r.v, 1, y);
        set_lane(r.v, 2, z); set_lane(r.v, 3, w);
        return r;
    }
    return vec_reg{_mm_set_ps(w, z, y, x)};
}

INLINE constexpr vec_reg add(vec_reg a, vec_reg b) noexcept {
    if (std::is_constant_evaluated()) {
        vec_reg r{};
        for (int i = 0; i < 4; ++i)
            set_lane(r.v, i, lane(a.v, i) + lane(b.v, i));
        return r;
    }
    return vec_reg{_mm_add_ps(a.v, b.v)};
}

INLINE constexpr vec_reg mul(vec_reg a, vec_reg b) noexcept {
    if (std::is_constant_evaluated()) {
        vec_reg r{};
        for (int i = 0; i < 4; ++i)
            set_lane(r.v, i, lane(a.v, i) * lane(b.v, i));
        return r;
    }
    return vec_reg{_mm_mul_ps(a.v, b.v)};
}

INLINE constexpr vec_reg mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    if (std::is_constant_evaluated()) return add(mul(a, b), c);
#if defined(__AVX2__)
    return vec_reg{_mm_fmadd_ps(a.v, b.v, c.v)};
#else
    return vec_reg{_mm_add_ps(_mm_mul_ps(a.v, b.v), c.v)};
#endif
}

INLINE constexpr vec_reg dot4(vec_reg a, vec_reg b) noexcept {
    if (std::is_constant_evaluated()) {
        float s = 0;
        for (int i = 0; i < 4; ++i) s += lane(a.v, i) * lane(b.v, i);
        return set(s, s, s, s);
    }
    __m128 m = _mm_mul_ps(a.v, b.v);
    __m128 t = _mm_hadd_ps(m, m);
    return vec_reg{_mm_hadd_ps(t, t)};
}

// compile-time proof the constexpr path is live
static_assert(lane(dot4(set(1, 2, 3, 4), set(1, 1, 1, 1)).v, 0) == 10.0f);
static_assert(lane(mul_add(set(2, 0, 0, 0), set(3, 0, 0, 0),
                           set(1, 0, 0, 0)).v, 0) == 7.0f);

} // namespace sa

// ---------------------------------------------------------------- strategy C
namespace sc {

struct alignas(16) vec_reg { float f[4]; };

INLINE constexpr vec_reg set(float x, float y, float z, float w) noexcept {
    return vec_reg{{x, y, z, w}};
}

INLINE constexpr vec_reg mul_add(vec_reg a, vec_reg b, vec_reg c) noexcept {
    if (std::is_constant_evaluated()) {
        return vec_reg{{a.f[0] * b.f[0] + c.f[0], a.f[1] * b.f[1] + c.f[1],
                        a.f[2] * b.f[2] + c.f[2], a.f[3] * b.f[3] + c.f[3]}};
    }
    vec_reg r{};
#if defined(__AVX2__)
    _mm_store_ps(r.f, _mm_fmadd_ps(_mm_load_ps(a.f), _mm_load_ps(b.f),
                                   _mm_load_ps(c.f)));
#else
    _mm_store_ps(r.f, _mm_add_ps(_mm_mul_ps(_mm_load_ps(a.f), _mm_load_ps(b.f)),
                                 _mm_load_ps(c.f)));
#endif
    return r;
}

static_assert(mul_add(set(2, 0, 0, 0), set(3, 0, 0, 0), set(1, 0, 0, 0)).f[0] == 7.0f);

} // namespace sc

// --------------------------------------------------------- codegen probes
// Same math, three implementations. Compare the emitted bodies.

NOINLINE sa::vec_reg __vectorcall probe_a_muladd(sa::vec_reg a, sa::vec_reg b,
                                                sa::vec_reg c) noexcept {
    return sa::mul_add(a, b, c);
}

NOINLINE sc::vec_reg __vectorcall probe_c_muladd(sc::vec_reg a, sc::vec_reg b,
                                                sc::vec_reg c) noexcept {
    return sc::mul_add(a, b, c);
}

NOINLINE DirectX::XMVECTOR XM_CALLCONV probe_dx_muladd(DirectX::XMVECTOR a,
                                                       DirectX::XMVECTOR b,
                                                       DirectX::XMVECTOR c) noexcept {
    return DirectX::XMVectorMultiplyAdd(a, b, c);
}

NOINLINE sa::vec_reg __vectorcall probe_a_dot4(sa::vec_reg a, sa::vec_reg b) noexcept {
    return sa::dot4(a, b);
}

NOINLINE DirectX::XMVECTOR XM_CALLCONV probe_dx_dot4(DirectX::XMVECTOR a,
                                                     DirectX::XMVECTOR b) noexcept {
    return DirectX::XMVector4Dot(a, b);
}

// Chained expression: exercises whether intermediates stay in registers.
NOINLINE sa::vec_reg __vectorcall probe_a_chain(sa::vec_reg a, sa::vec_reg b,
                                               sa::vec_reg c, sa::vec_reg d) noexcept {
    return sa::add(sa::mul_add(a, b, c), sa::mul(d, d));
}

NOINLINE DirectX::XMVECTOR XM_CALLCONV probe_dx_chain(DirectX::XMVECTOR a,
                                                      DirectX::XMVECTOR b,
                                                      DirectX::XMVECTOR c,
                                                      DirectX::XMVECTOR d) noexcept {
    return DirectX::XMVectorAdd(DirectX::XMVectorMultiplyAdd(a, b, c),
                                DirectX::XMVectorMultiply(d, d));
}

int main() { return 0; }
