// Spike B: vec_reg is a union of float[4] and __m128.
// Question: can constexpr write f[] while runtime uses v?
// Expected risk: reading an inactive union member is UB in constant evaluation.

#include <xmmintrin.h>
#include <type_traits>

union vec_reg {
    float f[4];
    __m128 v;
};

constexpr vec_reg set(float x, float y, float z, float w) noexcept {
    if (std::is_constant_evaluated()) {
        vec_reg r{};                 // activates f[]
        r.f[0] = x; r.f[1] = y; r.f[2] = z; r.f[3] = w;
        return r;
    }
    vec_reg r{};
    r.v = _mm_set_ps(w, z, y, x);   // activates v
    return r;
}

constexpr vec_reg add(vec_reg a, vec_reg b) noexcept {
    if (std::is_constant_evaluated()) {
        vec_reg r{};
        for (int i = 0; i < 4; ++i) r.f[i] = a.f[i] + b.f[i];
        return r;
    }
    vec_reg r{};
    r.v = _mm_add_ps(a.v, b.v);
    return r;
}

constexpr float get_x(vec_reg a) noexcept {
    if (std::is_constant_evaluated()) return a.f[0];
    return _mm_cvtss_f32(a.v);
}

static_assert(get_x(add(set(1, 2, 3, 4), set(10, 20, 30, 40))) == 11.0f,
              "constexpr path failed");

extern "C" float spike_b_runtime(float x) noexcept {
    return get_x(add(set(x, 2, 3, 4), set(10, 20, 30, 40)));
}

int main() { return 0; }
