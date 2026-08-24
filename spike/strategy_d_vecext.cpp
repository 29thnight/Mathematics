// Spike D: vec_reg uses a compiler vector extension type as storage.
// Clang/GCC only. These types are constexpr-friendly AND map 1:1 to xmm.
// Question: is this the best Clang/GCC path (with A/B/C used on MSVC)?

#include <type_traits>

#if defined(__clang__) || defined(__GNUC__)

using f32x4 = float __attribute__((vector_size(16)));

struct vec_reg {
    f32x4 v;
};

constexpr vec_reg set(float x, float y, float z, float w) noexcept {
    return vec_reg{f32x4{x, y, z, w}};
}
constexpr vec_reg add(vec_reg a, vec_reg b) noexcept { return vec_reg{a.v + b.v}; }
constexpr vec_reg mul(vec_reg a, vec_reg b) noexcept { return vec_reg{a.v * b.v}; }
constexpr float get_x(vec_reg a) noexcept { return a.v[0]; }

static_assert(get_x(add(set(1, 2, 3, 4), set(10, 20, 30, 40))) == 11.0f,
              "constexpr path failed");
static_assert(get_x(mul(set(3, 0, 0, 0), set(5, 0, 0, 0))) == 15.0f,
              "constexpr mul failed");

extern "C" vec_reg spike_d_chain(vec_reg a, vec_reg b, vec_reg c) noexcept {
    return add(a, mul(b, c));
}

#else
#  pragma message("Spike D skipped: vector extensions unavailable on this compiler")
#endif

int main() { return 0; }
