// Spike C: vec_reg stores alignas(16) float[4]; SIMD ops load/store around it.
// Question: does the optimizer elide the load/store so hot chains stay in xmm?
// This is the pragmatic fallback if A and B fail.

#include <xmmintrin.h>
#include <type_traits>

#if defined(_MSC_VER) && !defined(__clang__)
#  define INLINE __forceinline
#else
#  define INLINE inline __attribute__((always_inline))
#endif

struct alignas(16) vec_reg {
    float f[4];
};

INLINE constexpr vec_reg set(float x, float y, float z, float w) noexcept {
    return vec_reg{{x, y, z, w}};
}

INLINE constexpr vec_reg add(vec_reg a, vec_reg b) noexcept {
    if (std::is_constant_evaluated()) {
        return vec_reg{{a.f[0] + b.f[0], a.f[1] + b.f[1],
                        a.f[2] + b.f[2], a.f[3] + b.f[3]}};
    }
    vec_reg r{};
    _mm_store_ps(r.f, _mm_add_ps(_mm_load_ps(a.f), _mm_load_ps(b.f)));
    return r;
}

INLINE constexpr vec_reg mul(vec_reg a, vec_reg b) noexcept {
    if (std::is_constant_evaluated()) {
        return vec_reg{{a.f[0] * b.f[0], a.f[1] * b.f[1],
                        a.f[2] * b.f[2], a.f[3] * b.f[3]}};
    }
    vec_reg r{};
    _mm_store_ps(r.f, _mm_mul_ps(_mm_load_ps(a.f), _mm_load_ps(b.f)));
    return r;
}

INLINE constexpr float get_x(vec_reg a) noexcept { return a.f[0]; }

static_assert(get_x(add(set(1, 2, 3, 4), set(10, 20, 30, 40))) == 11.0f,
              "constexpr path failed");
static_assert(get_x(mul(set(3, 0, 0, 0), set(5, 0, 0, 0))) == 15.0f,
              "constexpr mul failed");

// Codegen probe: a chained expression. Ideal output is 2 SIMD ops, no spills.
extern "C" vec_reg spike_c_chain(vec_reg a, vec_reg b, vec_reg c) noexcept {
    return add(a, mul(b, c));
}

int main() { return 0; }
