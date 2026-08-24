// Bitwise operations, comparisons, select, and the mask predicates.
//
// These are where a backend is most likely to diverge quietly: results are bit
// patterns rather than numbers, so a wrong operand order or an inverted mask
// still produces plausible-looking floats. Everything here compares bits.

#include "support/reg_testing.hpp"

#include <mathematics/arch/consteval_ops.hpp>

#if __has_include(<DirectXMath.h>)
#  include <DirectXMath.h>
#  define MATHEMATICS_TEST_HAS_DXMATH 1
#else
#  define MATHEMATICS_TEST_HAS_DXMATH 0
#endif

namespace {

using namespace math_test;
namespace ref = math::consteval_ops;

// A mask with a known pattern: lanes 0 and 2 set.
vec_reg pattern_mask() {
    return math::make_mask_reg(math::lane_true, math::lane_false,
                              math::lane_true, math::lane_false);
}

} // namespace

// ------------------------------------------------------------ constexpr parity
static_assert(math::lane_bits(math::cmp_lt(math::set(1, 5, 3, 7),
                                           math::set(2, 2, 4, 4)), 0)
              == math::lane_true);
static_assert(math::lane_bits(math::cmp_lt(math::set(1, 5, 3, 7),
                                           math::set(2, 2, 4, 4)), 1)
              == math::lane_false);
static_assert(math::get_x(math::select(math::cmp_lt(math::splat(1),
                                                     math::splat(2)),
                                        math::splat(10),
                                        math::splat(20))) == 10.0f);
static_assert(math::get_x(math::select(math::cmp_gt(math::splat(1),
                                                     math::splat(2)),
                                        math::splat(10),
                                        math::splat(20))) == 20.0f);
static_assert(math::move_mask(math::cmp_lt(math::set(1, 5, 1, 5),
                                           math::set(2, 2, 2, 2))) == 0b0101);
static_assert(math::all_true(math::cmp_eq(math::splat(3), math::splat(3))));
static_assert(!math::any_true(math::cmp_eq(math::splat(3), math::splat(4))));

// NaN compares unequal to everything, including itself.
static_assert(math::all_true(math::cmp_ne(
    math::splat(math::consteval_ops::quiet_nan),
    math::splat(math::consteval_ops::quiet_nan))));

TEST(backend_logic_constexpr, compile_time_matches_runtime) {
    EXPECT_EQ(math::move_mask(math::cmp_lt(math::set(1, 5, 1, 5),
                                           math::set(2, 2, 2, 2))), 0b0101);
    EXPECT_FLOAT_EQ(math::get_x(math::select(
                        math::cmp_lt(math::splat(1), math::splat(2)),
                        math::splat(10), math::splat(20))), 10.0f);
    EXPECT_TRUE(math::all_true(math::cmp_eq(math::splat(3), math::splat(3))));
}

// ------------------------------------------------------------------- bitwise
TEST(backend_logic, bitwise_matches_reference) {
    random_vectors gen(random_seed + 20);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        EXPECT_TRUE(bits_equal(math::bit_and(a.v, b.v), ref::bit_and(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::bit_or(a.v, b.v), ref::bit_or(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::bit_xor(a.v, b.v), ref::bit_xor(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::bit_and_not(a.v, b.v), ref::bit_and_not(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::bit_not(a.v), ref::bit_not(a.v))) << n;
    }
}

// bit_and_not's operand order is the one thing about it worth remembering, and the
// two SIMD backends spell it with opposite argument orders internally.
TEST(backend_logic, and_not_inverts_its_first_operand) {
    const vec_reg ones = math::splat(math::from_bits(math::lane_true));
    const vec_reg zeros = math::splat(math::from_bits(math::lane_false));

    // ~0 & 1 == 1
    EXPECT_EQ(math::lane_bits(math::bit_and_not(zeros, ones), 0), math::lane_true);
    // ~1 & 0 == 0, and ~1 & 1 == 0
    EXPECT_EQ(math::lane_bits(math::bit_and_not(ones, zeros), 0), math::lane_false);
    EXPECT_EQ(math::lane_bits(math::bit_and_not(ones, ones), 0), math::lane_false);
}

// ------------------------------------------------------------------ comparison
TEST(backend_logic, comparisons_match_reference) {
    random_vectors gen(random_seed + 21);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        EXPECT_TRUE(bits_equal(math::cmp_eq(a.v, b.v), ref::cmp_eq(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_ne(a.v, b.v), ref::cmp_ne(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_lt(a.v, b.v), ref::cmp_lt(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_le(a.v, b.v), ref::cmp_le(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_gt(a.v, b.v), ref::cmp_gt(a.v, b.v))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_ge(a.v, b.v), ref::cmp_ge(a.v, b.v))) << n;
    }
}

// Random floats almost never compare equal, so the interesting cases have to be
// constructed: equal values, and lanes that straddle the comparison.
TEST(backend_logic, comparisons_match_reference_when_operands_coincide) {
    random_vectors gen(random_seed + 22);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        // Half the lanes identical to a, half strictly greater.
        const vec_reg b = math::set(a.f[0], a.f[1] + 1.0f, a.f[2], a.f[3] + 1.0f);

        EXPECT_TRUE(bits_equal(math::cmp_eq(a.v, b), ref::cmp_eq(a.v, b))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_le(a.v, b), ref::cmp_le(a.v, b))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_ge(a.v, b), ref::cmp_ge(a.v, b))) << n;
        EXPECT_EQ(math::move_mask(math::cmp_eq(a.v, b)), 0b0101) << n;
    }
}

TEST(backend_logic, comparisons_on_edge_values) {
    const auto& values = edge_values();
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < values.size(); ++j) {
            const vec_reg a = math::splat(opaque(values[i]));
            const vec_reg b = math::splat(opaque(values[j]));
            const std::string where =
                "values[" + std::to_string(i) + "], values[" + std::to_string(j) + "]";
            EXPECT_TRUE(bits_equal(math::cmp_eq(a, b), ref::cmp_eq(a, b))) << where;
            EXPECT_TRUE(bits_equal(math::cmp_lt(a, b), ref::cmp_lt(a, b))) << where;
            EXPECT_TRUE(bits_equal(math::cmp_ge(a, b), ref::cmp_ge(a, b))) << where;
        }
    }
}

TEST(backend_logic, na_n_compares_unordered_against_everything) {
    const vec_reg nan = math::splat(opaque(quiet_nan()));
    const vec_reg one = math::splat(opaque(1.0f));

    // Every ordered comparison is false, and only not-equal is true.
    EXPECT_FALSE(math::any_true(math::cmp_eq(nan, one)));
    EXPECT_FALSE(math::any_true(math::cmp_lt(nan, one)));
    EXPECT_FALSE(math::any_true(math::cmp_le(nan, one)));
    EXPECT_FALSE(math::any_true(math::cmp_gt(nan, one)));
    EXPECT_FALSE(math::any_true(math::cmp_ge(nan, one)));
    EXPECT_TRUE(math::all_true(math::cmp_ne(nan, one)));
    EXPECT_TRUE(math::all_true(math::cmp_ne(nan, nan)));
}

// Signed zeros compare equal but have different bits, which is exactly the case
// a value-based comparison would wave through.
TEST(backend_logic, signed_zeros_compare_equal) {
    const vec_reg pos = math::splat(opaque(0.0f));
    const vec_reg neg = math::splat(opaque(-0.0f));
    EXPECT_TRUE(math::all_true(math::cmp_eq(pos, neg)));
    EXPECT_NE(math::lane_bits(pos, 0), math::lane_bits(neg, 0));
}

// ---------------------------------------------------------------------- select
TEST(backend_logic, select_matches_reference) {
    random_vectors gen(random_seed + 23);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        const sample c = gen.next();
        const math::mask_reg mask = math::cmp_lt(a.v, b.v);

        EXPECT_TRUE(bits_equal(math::select(mask, b.v, c.v),
                              ref::select(mask, b.v, c.v))) << n;
        EXPECT_TRUE(bits_equal(math::select(pattern_mask(), b.v, c.v),
                              ref::select(pattern_mask(), b.v, c.v))) << n;
    }
}

TEST(backend_logic, select_takes_set_lanes_from_if_true) {
    const vec_reg t = math::set(1, 2, 3, 4);
    const vec_reg f = math::set(10, 20, 30, 40);
    const vec_reg r = math::select(pattern_mask(), t, f);

    EXPECT_FLOAT_EQ(math::get_x(r), 1.0f);    // mask lane 0 set   -> if_true
    EXPECT_FLOAT_EQ(math::get_y(r), 20.0f);   // mask lane 1 clear -> if_false
    EXPECT_FLOAT_EQ(math::get_z(r), 3.0f);
    EXPECT_FLOAT_EQ(math::get_w(r), 40.0f);
}

// ------------------------------------------------------------------ predicates
TEST(backend_logic, move_mask_matches_reference) {
    random_vectors gen(random_seed + 24);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        const math::mask_reg mask = math::cmp_lt(a.v, b.v);
        EXPECT_EQ(math::move_mask(mask), ref::move_mask(mask)) << n;
        EXPECT_EQ(math::all_true(mask), ref::all_true(mask)) << n;
        EXPECT_EQ(math::any_true(mask), ref::any_true(mask)) << n;
    }
}

TEST(backend_logic, move_mask_reads_lanes_low_to_high) {
    // lane 0 is bit 0. Getting this backwards is a classic transposition bug and
    // would still pass a symmetric test.
    EXPECT_EQ(math::move_mask(math::make_mask_reg(math::lane_true, 0, 0, 0)), 0b0001);
    EXPECT_EQ(math::move_mask(math::make_mask_reg(0, math::lane_true, 0, 0)), 0b0010);
    EXPECT_EQ(math::move_mask(math::make_mask_reg(0, 0, math::lane_true, 0)), 0b0100);
    EXPECT_EQ(math::move_mask(math::make_mask_reg(0, 0, 0, math::lane_true)), 0b1000);
    EXPECT_EQ(math::move_mask(pattern_mask()), 0b0101);
}

// move_mask reads the sign bit, so a negative float registers even though it is
// not a mask. That is the movmskps definition and callers rely on it.
TEST(backend_logic, move_mask_reads_sign_bit_of_ordinary_floats) {
    EXPECT_EQ(math::move_mask(math::set(-1.0f, 1.0f, -1.0f, 1.0f)), 0b0101);
    EXPECT_EQ(math::move_mask(math::splat(-0.0f)), 0b1111);
    EXPECT_EQ(math::move_mask(math::splat(0.0f)), 0b0000);
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

TEST(backend_logic_dx_parity, comparisons_and_bitwise_match_direct_x_math) {
    random_vectors gen(random_seed + 30);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        const DirectX::XMVECTOR xa = to_xm(a.v);
        const DirectX::XMVECTOR xb = to_xm(b.v);

        EXPECT_TRUE(bits_equal(math::cmp_eq(a.v, b.v),
                              from_xm(DirectX::XMVectorEqual(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_lt(a.v, b.v),
                              from_xm(DirectX::XMVectorLess(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_le(a.v, b.v),
                              from_xm(DirectX::XMVectorLessOrEqual(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_gt(a.v, b.v),
                              from_xm(DirectX::XMVectorGreater(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::cmp_ge(a.v, b.v),
                              from_xm(DirectX::XMVectorGreaterOrEqual(xa, xb)))) << n;

        EXPECT_TRUE(bits_equal(math::bit_and(a.v, b.v),
                              from_xm(DirectX::XMVectorAndInt(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::bit_or(a.v, b.v),
                              from_xm(DirectX::XMVectorOrInt(xa, xb)))) << n;
        EXPECT_TRUE(bits_equal(math::bit_xor(a.v, b.v),
                              from_xm(DirectX::XMVectorXorInt(xa, xb)))) << n;
        // DirectXMath offers NOR rather than NOT; NOR(a, 0) is ~a.
        EXPECT_TRUE(bits_equal(math::bit_not(a.v),
                              from_xm(DirectX::XMVectorNorInt(
                                  xa, DirectX::XMVectorZero())))) << n;
        // XMVectorAndCInt(V1, V2) is V1 & ~V2 -- the inverted operand is the
        // second, where ours is the first, so the arguments swap.
        EXPECT_TRUE(bits_equal(math::bit_and_not(a.v, b.v),
                              from_xm(DirectX::XMVectorAndCInt(xb, xa)))) << n;
    }
}

TEST(backend_logic_dx_parity, select_matches_direct_x_math) {
    random_vectors gen(random_seed + 31);
    for (int n = 0; n < sample_count; ++n) {
        const sample a = gen.next();
        const sample b = gen.next();
        const sample c = gen.next();
        const math::mask_reg mask = math::cmp_lt(a.v, b.v);

        // XMVectorSelect(V1, V2, Control) takes V1 where the control bit is
        // clear and V2 where it is set, so if_false comes first.
        EXPECT_TRUE(bits_equal(
            math::select(mask, b.v, c.v),
            from_xm(DirectX::XMVectorSelect(to_xm(c.v), to_xm(b.v), to_xm(mask)))))
            << n;
    }
}
#endif // MATHEMATICS_TEST_HAS_DXMATH
