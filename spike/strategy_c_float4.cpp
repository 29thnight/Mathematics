// Spike C: VecReg stores alignas(16) float[4]; SIMD ops load/store around it.
// Question: does the optimizer elide the load/store so hot chains stay in xmm?
// This is the pragmatic fallback if A and B fail.

#include <xmmintrin.h>
#include <type_traits>

#if defined(_MSC_VER) && !defined(__clang__)
#  define INLINE __forceinline
#else
#  define INLINE inline __attribute__((always_inline))
#endif

struct alignas(16) VecReg {
    float f[4];
};

INLINE constexpr VecReg Set(float x, float y, float z, float w) noexcept {
    return VecReg{{x, y, z, w}};
}

INLINE constexpr VecReg Add(VecReg a, VecReg b) noexcept {
    if (std::is_constant_evaluated()) {
        return VecReg{{a.f[0] + b.f[0], a.f[1] + b.f[1],
                       a.f[2] + b.f[2], a.f[3] + b.f[3]}};
    }
    VecReg r{};
    _mm_store_ps(r.f, _mm_add_ps(_mm_load_ps(a.f), _mm_load_ps(b.f)));
    return r;
}

INLINE constexpr VecReg Mul(VecReg a, VecReg b) noexcept {
    if (std::is_constant_evaluated()) {
        return VecReg{{a.f[0] * b.f[0], a.f[1] * b.f[1],
                       a.f[2] * b.f[2], a.f[3] * b.f[3]}};
    }
    VecReg r{};
    _mm_store_ps(r.f, _mm_mul_ps(_mm_load_ps(a.f), _mm_load_ps(b.f)));
    return r;
}

INLINE constexpr float GetX(VecReg a) noexcept { return a.f[0]; }

static_assert(GetX(Add(Set(1, 2, 3, 4), Set(10, 20, 30, 40))) == 11.0f,
              "constexpr path failed");
static_assert(GetX(Mul(Set(3, 0, 0, 0), Set(5, 0, 0, 0))) == 15.0f,
              "constexpr mul failed");

// Codegen probe: a chained expression. Ideal output is 2 SIMD ops, no spills.
extern "C" VecReg spike_c_chain(VecReg a, VecReg b, VecReg c) noexcept {
    return Add(a, Mul(b, c));
}

int main() { return 0; }
