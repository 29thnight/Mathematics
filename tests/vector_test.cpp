// Vector2/3/4 storage, construction, operators, and lane-wise math.
// Geometry and DirectXMath parity live in vector_geometry_test.cpp.

#include "support/reg_testing.hpp"

#include <mathf/vector.hpp>

#include <type_traits>
#include <vector>

namespace {

using namespace mathf_test;
using mathf::Vector2;
using mathf::Vector3;
using mathf::Vector4;

} // namespace

// ---------------------------------------------------------------------- layout
// The packing is a promise to callers: a Vector3 array has to be a position
// stream a GPU can read with no gaps, and the types have to sit in structs
// without imposing alignment. A silent change here breaks binary interop rather
// than any test's arithmetic, so it is asserted directly.
static_assert(sizeof(Vector2) == 8);
static_assert(sizeof(Vector3) == 12);
static_assert(sizeof(Vector4) == 16);
static_assert(alignof(Vector3) == alignof(float));

TEST(VectorLayout, ArraysAreContiguousWithNoPadding) {
    const std::vector<Vector3> stream = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    const float* raw = &stream[0].x;
    for (int i = 0; i < 9; ++i) {
        EXPECT_FLOAT_EQ(raw[i], static_cast<float>(i + 1)) << "element " << i;
    }
}

TEST(VectorLayout, IndexingReachesTheNamedMembers) {
    Vector4 v{1, 2, 3, 4};
    EXPECT_FLOAT_EQ(v[0], v.x);
    EXPECT_FLOAT_EQ(v[3], v.w);
    v[2] = 30.0f;
    EXPECT_FLOAT_EQ(v.z, 30.0f);
}

// ---------------------------------------------------------------- construction
static_assert(Vector3{}.x == 0.0f && Vector3{}.y == 0.0f && Vector3{}.z == 0.0f);
static_assert(Vector4{}.w == 0.0f);
static_assert(Vector3{2.0f}.y == 2.0f);
static_assert(Vector3::UnitY().y == 1.0f && Vector3::UnitY().x == 0.0f);
static_assert(Vector4::One().w == 1.0f);

TEST(VectorConstruction, DefaultIsZero) {
    // Deliberately different from DirectXMath and GLM, which leave the lanes
    // indeterminate. Cheap, and it removes a class of uninitialized-read bugs.
    EXPECT_TRUE(Vector3{} == Vector3::Zero());
    EXPECT_TRUE(Vector4{} == Vector4::Zero());
    EXPECT_TRUE(Vector2{} == Vector2::Zero());
}

TEST(VectorConstruction, SplatFillsEveryComponent) {
    const Vector4 v{3.5f};
    EXPECT_FLOAT_EQ(v.x, 3.5f);
    EXPECT_FLOAT_EQ(v.y, 3.5f);
    EXPECT_FLOAT_EQ(v.z, 3.5f);
    EXPECT_FLOAT_EQ(v.w, 3.5f);
}

// ------------------------------------------------------------ register round trip
// Everything else assumes Reg() and FromReg() preserve the components exactly,
// so that is checked on its own -- a failure here would otherwise surface as an
// arithmetic bug somewhere unrelated.
static_assert(Vector4::FromReg(Vector4{1, 2, 3, 4}.Reg()).z == 3.0f);
static_assert(Vector3::FromReg(Vector3{1, 2, 3}.Reg()).z == 3.0f);
static_assert(Vector2::FromReg(Vector2{1, 2}.Reg()).y == 2.0f);

TEST(VectorRegister, RoundTripsExactly) {
    RandomVectors gen(kSeed + 60);
    for (int n = 0; n < kSamples; ++n) {
        const Sample s = gen.Next();
        const Vector4 v4{s.f[0], s.f[1], s.f[2], s.f[3]};
        const Vector4 r4 = Vector4::FromReg(v4.Reg());
        EXPECT_EQ(r4.x, v4.x) << n;
        EXPECT_EQ(r4.y, v4.y) << n;
        EXPECT_EQ(r4.z, v4.z) << n;
        EXPECT_EQ(r4.w, v4.w) << n;

        const Vector3 v3{s.f[0], s.f[1], s.f[2]};
        const Vector3 r3 = Vector3::FromReg(v3.Reg());
        EXPECT_EQ(r3.x, v3.x) << n;
        EXPECT_EQ(r3.y, v3.y) << n;
        EXPECT_EQ(r3.z, v3.z) << n;
    }
}

// A Vector3 occupies twelve bytes, so its register cannot come from a sixteen
// byte load. This pins the w lane down: Length and Normalize would silently
// absorb whatever was there otherwise.
TEST(VectorRegister, Vector3LeavesTheFourthLaneZero) {
    EXPECT_FLOAT_EQ(mathf::GetW(Vector3{1, 2, 3}.Reg()), 0.0f);
    EXPECT_FLOAT_EQ(mathf::GetZ(Vector2{1, 2}.Reg()), 0.0f);
    EXPECT_FLOAT_EQ(mathf::GetW(Vector2{1, 2}.Reg()), 0.0f);
}

// ------------------------------------------------------------------- arithmetic
static_assert((Vector3{1, 2, 3} + Vector3{10, 20, 30}).y == 22.0f);
static_assert((Vector3{10, 20, 30} - Vector3{1, 2, 3}).z == 27.0f);
static_assert((Vector3{2, 3, 4} * Vector3{3, 3, 3}).x == 6.0f);
static_assert((Vector3{10, 20, 30} / Vector3{2, 2, 2}).y == 10.0f);
static_assert((Vector3{1, 2, 3} * 2.0f).z == 6.0f);
static_assert((2.0f * Vector3{1, 2, 3}).z == 6.0f);
static_assert((Vector3{10, 20, 30} / 2.0f).x == 5.0f);
static_assert((-Vector3{1, -2, 3}).y == 2.0f);

// Division has to survive constant evaluation at every width. Vector2 and
// Vector3 load their unused lanes as zero, so dividing by another vector would
// compute 0/0 in lanes the type does not own -- fine at run time, where the
// result is discarded, but undefined arithmetic that aborts a constexpr. The
// divisor's unused lanes are filled with one to prevent it, and these pin that.
static_assert((Vector2{10, 20} / Vector2{2, 4}).y == 5.0f);
static_assert((Vector3{10, 20, 30} / Vector3{2, 4, 5}).z == 6.0f);
static_assert((Vector4{10, 20, 30, 40} / Vector4{2, 4, 5, 8}).w == 5.0f);
static_assert((Vector2{10, 20} / 2.0f).y == 10.0f);
static_assert((Vector3{10, 20, 30} / 2.0f).z == 15.0f);
static_assert((Vector4{10, 20, 30, 40} / 2.0f).w == 20.0f);

// Note for anyone tempted to give operator/(V, float) the same unused-lane
// guard: it does not need one. Dividing by a zero scalar is rejected during
// constant evaluation for EVERY width, Vector4 included, because [expr.mul]/4
// makes division by zero undefined regardless of the operand type -- the lanes
// the vector actually owns fail first. The guard on operator/(V, V) is needed
// because there the divisor's used lanes can be nonzero while its unused lanes
// are zero, which is a case only the narrow types can reach.

TEST(VectorArithmetic, ComponentWiseAgainstHandComputedValues) {
    const Vector4 a{1, 2, 3, 4};
    const Vector4 b{10, 20, 30, 40};

    EXPECT_TRUE(a + b == Vector4(11, 22, 33, 44));
    EXPECT_TRUE(b - a == Vector4(9, 18, 27, 36));
    EXPECT_TRUE(a * b == Vector4(10, 40, 90, 160));
    EXPECT_TRUE(b / a == Vector4(10, 10, 10, 10));
    EXPECT_TRUE(a * 2.0f == Vector4(2, 4, 6, 8));
    EXPECT_TRUE(-a == Vector4(-1, -2, -3, -4));
}

// `*` is component-wise, following HLSL and GLM. Asserted explicitly because the
// alternative reading -- a dot product -- would still compile and return
// something plausible if the convention ever drifted.
TEST(VectorArithmetic, MultiplyIsComponentWiseNotDotProduct) {
    const Vector3 a{1, 2, 3};
    const Vector3 b{4, 5, 6};
    EXPECT_TRUE(a * b == Vector3(4, 10, 18));
    EXPECT_FLOAT_EQ(mathf::Dot(a, b), 32.0f);
}

TEST(VectorArithmetic, CompoundAssignmentMatchesTheBinaryForm) {
    Vector3 v{1, 2, 3};
    v += Vector3{1, 1, 1};
    EXPECT_TRUE(v == Vector3(2, 3, 4));
    v -= Vector3{1, 1, 1};
    EXPECT_TRUE(v == Vector3(1, 2, 3));
    v *= 2.0f;
    EXPECT_TRUE(v == Vector3(2, 4, 6));
    v /= 2.0f;
    EXPECT_TRUE(v == Vector3(1, 2, 3));
}

// Each type must use exactly its own lanes. A Vector2 that quietly computed over
// four would still pass every test above.
TEST(VectorArithmetic, OperationsUseOnlyTheirOwnLanes) {
    EXPECT_FLOAT_EQ(mathf::Dot(Vector2{3, 4}, Vector2{3, 4}), 25.0f);
    EXPECT_FLOAT_EQ(mathf::Dot(Vector3{1, 2, 3}, Vector3{1, 1, 1}), 6.0f);
    EXPECT_FLOAT_EQ(mathf::Dot(Vector4{1, 2, 3, 4}, Vector4{1, 1, 1, 1}), 10.0f);

    EXPECT_FLOAT_EQ(mathf::Length(Vector2{3, 4}), 5.0f);
    EXPECT_FLOAT_EQ(mathf::Length(Vector3{0, 3, 4}), 5.0f);
    EXPECT_FLOAT_EQ(mathf::Length(Vector4{0, 0, 3, 4}), 5.0f);
}

// ------------------------------------------------------------------ comparison
TEST(VectorComparison, EqualityIsExactAndPerComponent) {
    EXPECT_TRUE(Vector3(1, 2, 3) == Vector3(1, 2, 3));
    EXPECT_FALSE(Vector3(1, 2, 3) == Vector3(1, 2, 3.0001f));
    EXPECT_TRUE(Vector3(1, 2, 3) != Vector3(1, 2, 3.0001f));
}

// Vector2 and Vector3 leave the unused register lanes zero, but equality must
// not depend on that -- it compares only the lanes the type owns.
TEST(VectorComparison, EqualityIgnoresUnusedLanes) {
    EXPECT_TRUE(Vector2(1, 2) == Vector2(1, 2));
    EXPECT_TRUE(Vector3(1, 2, 3) == Vector3(1, 2, 3));
    EXPECT_FALSE(Vector3(1, 2, 3) == Vector3(1, 2, 4));
}

TEST(VectorComparison, NearEqualAcceptsSmallDifferences) {
    EXPECT_TRUE(mathf::NearEqual(Vector3(1, 2, 3), Vector3(1, 2, 3.000001f)));
    EXPECT_FALSE(mathf::NearEqual(Vector3(1, 2, 3), Vector3(1, 2, 3.1f)));
    EXPECT_TRUE(mathf::NearEqual(Vector3(1, 2, 3), Vector3(1, 2, 3.1f), 0.2f));
}

// ---------------------------------------------------------------- lane-wise math
static_assert(mathf::Abs(Vector3{-1, 2, -3}).x == 1.0f);
static_assert(mathf::Min(Vector3{1, 5, 3}, Vector3{4, 2, 6}).y == 2.0f);
static_assert(mathf::Max(Vector3{1, 5, 3}, Vector3{4, 2, 6}).x == 4.0f);
static_assert(mathf::Lerp(Vector3{0, 0, 0}, Vector3{10, 20, 30}, 0.5f).y == 10.0f);

TEST(VectorLaneMath, AbsMinMaxClampSaturate) {
    EXPECT_TRUE(mathf::Abs(Vector3(-1, 2, -3)) == Vector3(1, 2, 3));
    EXPECT_TRUE(mathf::Min(Vector3(1, 5, 3), Vector3(4, 2, 6)) == Vector3(1, 2, 3));
    EXPECT_TRUE(mathf::Max(Vector3(1, 5, 3), Vector3(4, 2, 6)) == Vector3(4, 5, 6));
    EXPECT_TRUE(mathf::Clamp(Vector3(-1, 5, 0.5f), Vector3::Zero(),
                             Vector3::One()) == Vector3(0, 1, 0.5f));
    EXPECT_TRUE(mathf::Saturate(Vector3(-1, 5, 0.5f)) == Vector3(0, 1, 0.5f));
}

// Lerp is written as a + t * (b - a) so that the endpoints come back exactly.
// The algebraically equal (1-t)*a + t*b does not guarantee that.
TEST(VectorLaneMath, LerpIsExactAtItsEndpoints) {
    const Vector3 a{1.1f, 2.2f, 3.3f};
    const Vector3 b{9.9f, 8.8f, 7.7f};
    EXPECT_TRUE(mathf::Lerp(a, b, 0.0f) == a);
    EXPECT_TRUE(mathf::Lerp(a, b, 1.0f) == b);
    EXPECT_TRUE(mathf::NearEqual(mathf::Lerp(a, b, 0.5f),
                                 Vector3(5.5f, 5.5f, 5.5f)));
}

TEST(VectorLaneMath, LerpExtrapolatesOutsideTheUnitInterval) {
    const Vector3 a{0, 0, 0};
    const Vector3 b{10, 10, 10};
    EXPECT_TRUE(mathf::NearEqual(mathf::Lerp(a, b, 2.0f), Vector3(20, 20, 20)));
    EXPECT_TRUE(mathf::NearEqual(mathf::Lerp(a, b, -1.0f),
                                 Vector3(-10, -10, -10)));
}
