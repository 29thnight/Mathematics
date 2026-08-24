// Spike A: vec_reg holds a raw __m128 member.
// Question: can we read/write __m128 lanes during constant evaluation?
// Expected risk: MSVC's __m128 is __declspec(intrin_type) union -> likely fails.

#include <xmmintrin.h>
#include <type_traits>

#if defined(_MSC_VER) && !defined(__clang__)
#  define LANE(v, i) ((v).m128_f32[i])
#  define INLINE __forceinline
#else
#  define LANE(v, i) ((v)[i])
#  define INLINE [[gnu::always_inline]] inline
#endif

struct vec_reg {
    __m128 v;
};

constexpr vec_reg set(float x, float y, float z, float w) noexcept {
    if (std::is_constant_evaluated()) {
        vec_reg r{};
        LANE(r.v, 0) = x; LANE(r.v, 1) = y; LANE(r.v, 2) = z; LANE(r.v, 3) = w;
        return r;
    }
    return vec_reg{_mm_set_ps(w, z, y, x)};
}

constexpr vec_reg add(vec_reg a, vec_reg b) noexcept {
    if (std::is_constant_evaluated()) {
        vec_reg r{};
        for (int i = 0; i < 4; ++i) LANE(r.v, i) = LANE(a.v, i) + LANE(b.v, i);
        return r;
    }
    return vec_reg{_mm_add_ps(a.v, b.v)};
}

constexpr float get_x(vec_reg a) noexcept {
    if (std::is_constant_evaluated()) return LANE(a.v, 0);
    return _mm_cvtss_f32(a.v);
}

// Forces compile-time evaluation. If this compiles, strategy A works.
static_assert(get_x(add(set(1, 2, 3, 4), set(10, 20, 30, 40))) == 11.0f,
              "constexpr path failed");

extern "C" float spike_a_runtime(float x) noexcept {
    return get_x(add(set(x, 2, 3, 4), set(10, 20, 30, 40)));
}

int main() { return 0; }
