// Shuffles, dot-product reductions, memory access, and lane readout.
//
// lane-ordering mistakes are the failure mode here: _mm_shuffle_ps takes its
// selectors reversed, and its two-source form draws from different operands per
// lane. Tests use asymmetric inputs throughout so a transposition cannot pass.

#include "support/reg_testing.hpp"

#include <mathematics/arch/consteval_ops.hpp>

#include <vector>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DXMATH 1
#else
#  define MATHEMATICS_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace math_test;
namespace ref = math::consteval_ops;

// Distinct lanes, so every permutation produces a distinguishable result.
constexpr float a_values = 1.0f, b_values = 2.0f, c_values = 3.0f, d_values = 4.0f;

} // namespace

// ------------------------------------------------------------ constexpr parity
static_assert(math::get_x(math::shuffle<3, 2, 1, 0>(
                  math::set(a_values, b_values, c_values, d_values))) == d_values);
static_assert(math::get_w(math::shuffle<3, 2, 1, 0>(
                  math::set(a_values, b_values, c_values, d_values))) == a_values);
static_assert(math::get_x(math::splat_w(math::set(a_values, b_values, c_values, d_values))) == d_values);
static_assert(math::get_x(math::dot4(math::set(1, 2, 3, 4),
                                      math::splat(1))) == 10.0f);
static_assert(math::get_x(math::dot3(math::set(1, 2, 3, 999),
                                      math::splat(1))) == 6.0f);
static_assert(math::get_x(math::dot2(math::set(1, 2, 999, 999),
                                      math::splat(1))) == 3.0f);
static_assert(math::get_y(math::set(a_values, b_values, c_values, d_values)) == b_values);
static_assert(math::get_z(math::set(a_values, b_values, c_values, d_values)) == c_values);

TEST(backend_shuffle_constexpr, compile_time_matches_runtime) {
    const vec_reg v = math::set(a_values, b_values, c_values, d_values);
    EXPECT_FLOAT_EQ(math::get_x(math::shuffle<3, 2, 1, 0>(v)), d_values);
    EXPECT_FLOAT_EQ(math::get_w(math::shuffle<3, 2, 1, 0>(v)), a_values);
    EXPECT_FLOAT_EQ(math::get_x(math::splat_w(v)), d_values);
    EXPECT_FLOAT_EQ(math::get_x(math::dot3(math::set(1, 2, 3, 999),
                                            math::splat(1))), 6.0f);
}

// ---------------------------------------------------------------------- shuffle
TEST(backend_shuffle, single_source_lanes_come_from_the_named_indices) {
    const vec_reg v = math::set(a_values, b_values, c_values, d_values);

    const vec_reg identity = math::shuffle<0, 1, 2, 3>(v);
    EXPECT_TRUE(bits_equal(identity, v));

    const vec_reg reversed = math::shuffle<3, 2, 1, 0>(v);
    EXPECT_FLOAT_EQ(math::get_x(reversed), d_values);
    EXPECT_FLOAT_EQ(math::get_y(reversed), c_values);
    EXPECT_FLOAT_EQ(math::get_z(reversed), b_values);
    EXPECT_FLOAT_EQ(math::get_w(reversed), a_values);

    const vec_reg mixed = math::shuffle<2, 0, 3, 1>(v);
    EXPECT_FLOAT_EQ(math::get_x(mixed), c_values);
    EXPECT_FLOAT_EQ(math::get_y(mixed), a_values);
    EXPECT_FLOAT_EQ(math::get_z(mixed), d_values);
    EXPECT_FLOAT_EQ(math::get_w(mixed), b_values);
}

// The low two lanes come from the first operand and the high two from the
// second, matching _mm_shuffle_ps. Both operands are distinct here so a
// backend that read all four from one of them would fail.
TEST(backend_shuffle, two_source_takes_low_lanes_from_first_operand) {
    const vec_reg a = math::set(1, 2, 3, 4);
    const vec_reg b = math::set(10, 20, 30, 40);

    const vec_reg r = math::shuffle<0, 1, 0, 1>(a, b);
    EXPECT_FLOAT_EQ(math::get_x(r), 1.0f);
    EXPECT_FLOAT_EQ(math::get_y(r), 2.0f);
    EXPECT_FLOAT_EQ(math::get_z(r), 10.0f);
    EXPECT_FLOAT_EQ(math::get_w(r), 20.0f);

    const vec_reg s = math::shuffle<3, 2, 3, 2>(a, b);
    EXPECT_FLOAT_EQ(math::get_x(s), 4.0f);
    EXPECT_FLOAT_EQ(math::get_y(s), 3.0f);
    EXPECT_FLOAT_EQ(math::get_z(s), 40.0f);
    EXPECT_FLOAT_EQ(math::get_w(s), 30.0f);
}

TEST(backend_shuffle, splats_broadcast_the_named_lane) {
    const vec_reg v = math::set(a_values, b_values, c_values, d_values);
    EXPECT_TRUE(bits_equal(math::splat_x(v), math::splat(a_values)));
    EXPECT_TRUE(bits_equal(math::splat_y(v), math::splat(b_values)));
    EXPECT_TRUE(bits_equal(math::splat_z(v), math::splat(c_values)));
    EXPECT_TRUE(bits_equal(math::splat_w(v), math::splat(d_values)));
}

namespace {

// shuffle indices must be template arguments, so an exhaustive sweep has to be
// unrolled at compile time. one small function per index position, each fanning
// out to the next -- verbose, but readable and portable.
template <int x, int y, int z, int w>
void check_shuffle(vec_reg a) {
    EXPECT_TRUE(bits_equal(math::shuffle<x, y, z, w>(a),
                          ref::shuffle<x, y, z, w>(a)))
        << "shuffle<" << x << "," << y << "," << z << "," << w << ">";
}

template <int x, int y, int z>
void sweep_w(vec_reg a) {
    check_shuffle<x, y, z, 0>(a);
    check_shuffle<x, y, z, 1>(a);
    check_shuffle<x, y, z, 2>(a);
    check_shuffle<x, y, z, 3>(a);
}

template <int x, int y>
void sweep_z(vec_reg a) {
    sweep_w<x, y, 0>(a);
    sweep_w<x, y, 1>(a);
    sweep_w<x, y, 2>(a);
    sweep_w<x, y, 3>(a);
}

template <int x>
void sweep_y(vec_reg a) {
    sweep_z<x, 0>(a);
    sweep_z<x, 1>(a);
    sweep_z<x, 2>(a);
    sweep_z<x, 3>(a);
}

} // namespace

// Exhaustive over all 256 single-source permutations: cheap, and it leaves no
// index combination untested.
TEST(backend_shuffle, all_single_source_permutations_match_reference) {
    random_vectors gen(random_seed + 40);
    const sample a = gen.next();
    sweep_y<0>(a.v);
    sweep_y<1>(a.v);
    sweep_y<2>(a.v);
    sweep_y<3>(a.v);
}

TEST(backend_shuffle, two_source_permutations_match_reference) {
    random_vectors gen(random_seed + 41);
    const sample a = gen.next();
    const sample b = gen.next();

    EXPECT_TRUE(bits_equal((math::shuffle<0, 1, 2, 3>(a.v, b.v)),
                          (ref::shuffle<0, 1, 2, 3>(a.v, b.v))));
    EXPECT_TRUE(bits_equal((math::shuffle<3, 2, 1, 0>(a.v, b.v)),
                          (ref::shuffle<3, 2, 1, 0>(a.v, b.v))));
    EXPECT_TRUE(bits_equal((math::shuffle<0, 0, 3, 3>(a.v, b.v)),
                          (ref::shuffle<0, 0, 3, 3>(a.v, b.v))));
    EXPECT_TRUE(bits_equal((math::shuffle<2, 1, 1, 2>(a.v, b.v)),
                          (ref::shuffle<2, 1, 1, 2>(a.v, b.v))));
}

// ------------------------------------------------------------------- reductions
TEST(backend_shuffle, dot_products_match_reference) {
    random_vectors gen(random_seed + 42);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        // dpps and a left-to-right scalar sum associate differently, so this is
        // a tolerance comparison by nature -- and the tolerance has to scale with
        // the terms rather than the result, since the two can nearly cancel.
        EXPECT_TRUE(near_equal(math::dot2(a.v, b.v), ref::dot2(a.v, b.v),
                              0.0f, dot_tolerance(a.f, b.f, 2))) << n;
        EXPECT_TRUE(near_equal(math::dot3(a.v, b.v), ref::dot3(a.v, b.v),
                              0.0f, dot_tolerance(a.f, b.f, 3))) << n;
        EXPECT_TRUE(near_equal(math::dot4(a.v, b.v), ref::dot4(a.v, b.v),
                              0.0f, dot_tolerance(a.f, b.f, 4))) << n;
    }
}

TEST(backend_shuffle, dot_products_ignore_lanes_outside_their_width) {
    // A huge value in the unused lanes would swamp the result if it leaked in.
    const vec_reg a = math::set(1, 2, 1e20f, 1e20f);
    const vec_reg b = math::set(3, 4, 1e20f, 1e20f);
    EXPECT_FLOAT_EQ(math::get_x(math::dot2(a, b)), 11.0f);

    const vec_reg c = math::set(1, 2, 3, 1e20f);
    const vec_reg d = math::set(4, 5, 6, 1e20f);
    EXPECT_FLOAT_EQ(math::get_x(math::dot3(c, d)), 32.0f);
}

TEST(backend_shuffle, dot_products_broadcast_to_every_lane) {
    const vec_reg a = math::set(1, 2, 3, 4);
    const vec_reg b = math::set(5, 6, 7, 8);
    for (const vec_reg r : {math::dot2(a, b), math::dot3(a, b), math::dot4(a, b)}) {
        EXPECT_FLOAT_EQ(math::get_x(r), math::get_y(r));
        EXPECT_FLOAT_EQ(math::get_x(r), math::get_z(r));
        EXPECT_FLOAT_EQ(math::get_x(r), math::get_w(r));
    }
}

// ---------------------------------------------------------------- memory access
TEST(backend_shuffle, load_and_store_round_trip_exactly) {
    random_vectors gen(random_seed + 43);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        alignas(16) std::array<float, 4> buffer{};

        math::store_aligned(buffer.data(), a.v);
        EXPECT_TRUE(bits_equal(math::load_aligned(buffer.data()), a.v)) << n;

        math::store(buffer.data(), a.v);
        EXPECT_TRUE(bits_equal(math::load(buffer.data()), a.v)) << n;
    }
}

// Every other test in the suite assumes set and lane preserve bits exactly, so
// this is checked on its own -- a failure here would otherwise surface as an
// arithmetic bug somewhere unrelated.
TEST(backend_shuffle, set_and_lane_round_trip_exactly) {
    random_vectors gen(random_seed + 44);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const auto got = to_array(a.v);
        for (int i = 0; i < 4; ++i) {
            EXPECT_EQ(got[i], a.f[i]) << "lane " << i << " sample " << n;
        }
    }
}

TEST(backend_shuffle, load_preserves_lane_order) {
    alignas(16) const std::array<float, 4> buffer = {a_values, b_values, c_values, d_values};
    const vec_reg v = math::load_aligned(buffer.data());
    EXPECT_FLOAT_EQ(math::get_x(v), a_values);
    EXPECT_FLOAT_EQ(math::get_y(v), b_values);
    EXPECT_FLOAT_EQ(math::get_z(v), c_values);
    EXPECT_FLOAT_EQ(math::get_w(v), d_values);
}

// The unaligned form must work at every byte offset; using an aligned load on an
// unaligned address faults on x86 rather than degrading.
TEST(backend_shuffle, unaligned_load_works_at_every_offset) {
    std::vector<float> storage(16);
    for (std::size_t i = 0; i < storage.size(); ++i) {
        storage[i] = static_cast<float>(i);
    }
    for (std::size_t offset = 0; offset < 4; ++offset) {
        const vec_reg v = math::load(storage.data() + offset);
        EXPECT_FLOAT_EQ(math::get_x(v), static_cast<float>(offset));
        EXPECT_FLOAT_EQ(math::get_w(v), static_cast<float>(offset + 3));
    }
}

TEST(backend_shuffle, store_writes_exactly_four_floats) {
    std::array<float, 6> buffer{};
    constexpr float guard_value = -999.0f;
    buffer.fill(guard_value);

    math::store(buffer.data() + 1, math::set(a_values, b_values, c_values, d_values));

    EXPECT_FLOAT_EQ(buffer[0], guard_value) << "wrote before the destination";
    EXPECT_FLOAT_EQ(buffer[1], a_values);
    EXPECT_FLOAT_EQ(buffer[4], d_values);
    EXPECT_FLOAT_EQ(buffer[5], guard_value) << "wrote past the destination";
}

// ------------------------------------------------------------------ lane readout
TEST(backend_shuffle, getters_read_their_own_lane) {
    const vec_reg v = math::set(a_values, b_values, c_values, d_values);
    EXPECT_FLOAT_EQ(math::get_x(v), a_values);
    EXPECT_FLOAT_EQ(math::get_y(v), b_values);
    EXPECT_FLOAT_EQ(math::get_z(v), c_values);
    EXPECT_FLOAT_EQ(math::get_w(v), d_values);

    EXPECT_FLOAT_EQ(math::lane(v, 0), a_values);
    EXPECT_FLOAT_EQ(math::lane(v, 1), b_values);
    EXPECT_FLOAT_EQ(math::lane(v, 2), c_values);
    EXPECT_FLOAT_EQ(math::lane(v, 3), d_values);
}

// ---------------------------------------------------------- DirectXMath parity
#if MATHEMATICS_TEST_HAS_DXMATH
namespace {

DirectX::XMVECTOR to_xm(vec_reg v) {
    return DirectX::XMVectorSet(math::lane(v, 0), math::lane(v, 1),
                                math::lane(v, 2), math::lane(v, 3));
}

vec_reg from_xm(DirectX::FXMVECTOR v) {
    DirectX::XMFLOAT4 out{};
    DirectX::XMStoreFloat4(&out, v);
    return math::set(out.x, out.y, out.z, out.w);
}

} // namespace

TEST(backend_shuffle_dx_parity, dot_products_match_direct_x_math) {
    random_vectors gen(random_seed + 50);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        const DirectX::XMVECTOR xa = to_xm(a.v);
        const DirectX::XMVECTOR xb = to_xm(b.v);

        EXPECT_TRUE(near_equal(math::dot2(a.v, b.v),
                              from_xm(DirectX::XMVector2Dot(xa, xb)),
                              0.0f, dot_tolerance(a.f, b.f, 2))) << n;
        EXPECT_TRUE(near_equal(math::dot3(a.v, b.v),
                              from_xm(DirectX::XMVector3Dot(xa, xb)),
                              0.0f, dot_tolerance(a.f, b.f, 3))) << n;
        EXPECT_TRUE(near_equal(math::dot4(a.v, b.v),
                              from_xm(DirectX::XMVector4Dot(xa, xb)),
                              0.0f, dot_tolerance(a.f, b.f, 4))) << n;
    }
}

TEST(backend_shuffle_dx_parity, lane_getters_match_direct_x_math) {
    random_vectors gen(random_seed + 51);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const DirectX::XMVECTOR xa = to_xm(a.v);
        EXPECT_FLOAT_EQ(math::get_x(a.v), DirectX::XMVectorGetX(xa)) << n;
        EXPECT_FLOAT_EQ(math::get_y(a.v), DirectX::XMVectorGetY(xa)) << n;
        EXPECT_FLOAT_EQ(math::get_z(a.v), DirectX::XMVectorGetZ(xa)) << n;
        EXPECT_FLOAT_EQ(math::get_w(a.v), DirectX::XMVectorGetW(xa)) << n;
    }
}
#endif // MATHEMATICS_TEST_HAS_DXMATH
