// Spike D: VecReg uses a compiler vector extension type as storage.
// Clang/GCC only. These types are constexpr-friendly AND map 1:1 to xmm.
// Question: is this the best Clang/GCC path (with A/B/C used on MSVC)?

#include <type_traits>

#if defined(__clang__) || defined(__GNUC__)

using f32x4 = float __attribute__((vector_size(16)));

struct VecReg {
    f32x4 v;
};

constexpr VecReg Set(float x, float y, float z, float w) noexcept {
    return VecReg{f32x4{x, y, z, w}};
}
constexpr VecReg Add(VecReg a, VecReg b) noexcept { return VecReg{a.v + b.v}; }
constexpr VecReg Mul(VecReg a, VecReg b) noexcept { return VecReg{a.v * b.v}; }
constexpr float GetX(VecReg a) noexcept { return a.v[0]; }

static_assert(GetX(Add(Set(1, 2, 3, 4), Set(10, 20, 30, 40))) == 11.0f,
              "constexpr path failed");
static_assert(GetX(Mul(Set(3, 0, 0, 0), Set(5, 0, 0, 0))) == 15.0f,
              "constexpr mul failed");

extern "C" VecReg spike_d_chain(VecReg a, VecReg b, VecReg c) noexcept {
    return Add(a, Mul(b, c));
}

#else
#  pragma message("Spike D skipped: vector extensions unavailable on this compiler")
#endif

int main() { return 0; }
