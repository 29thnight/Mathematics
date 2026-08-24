// Phase 0 baseline: establishes the measurement pipeline and records where
// DirectXMath sits, so every later phase has a number to be held against.
//
// Two axes are measured, because they fail differently:
//   Latency    -- a serial dependency chain; exposes per-op cost and any stall.
//   Throughput -- a batch over an array; exposes load/store and ABI overhead
//                 that a register-resident microbenchmark hides entirely.
//
// Release gate (docs/PLAN.md §4.2): Mathf must be within +-5% of DirectXMath on
// every mapped operation. Run with --benchmark_repetitions=5 for stable numbers.

#include <mathf/matrix.hpp>
#include <mathf/transform.hpp>
#include <mathf/vec_reg.hpp>
#include <mathf/vector.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <cmath>
#include <random>
#include <vector>

#if MATHF_BENCH_HAS_DXMATH
#  include <DirectXMath.h>
#endif
#if MATHF_BENCH_HAS_GLM
#  include <glm/glm.hpp>
#  include <glm/gtc/matrix_access.hpp>
#endif
#if MATHF_BENCH_HAS_VECTORMATH
// This header ends with `using namespace Vectormath::SSE;`, so it goes last and
// everything below stays explicitly qualified.
#  include <vectormath.hpp>
#endif

namespace {

// Sized to stay resident in L1. Three input streams plus one output is 64 bytes
// per element, so 512 elements is 32 KiB -- at the earlier 4096 the working set
// was 256 KiB, past L2, and every library converged on memory bandwidth with
// 11-17% run-to-run variance. Measuring the operation means keeping the data in
// cache; the bandwidth-bound case is a different question and not this one.
constexpr int kBatch = 512;
constexpr unsigned kSeed = 0x4D617468u;

// 16-byte aligned so every library can use its aligned load, and laid out as a
// flat float array rather than a vector of std::array so the three operand
// streams are contiguous.
struct alignas(16) Float4 {
    float v[4];
};

std::vector<Float4> MakeData(int n) {
    std::mt19937 rng(kSeed);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
    std::vector<Float4> out(static_cast<size_t>(n));
    for (auto& q : out) {
        for (float& f : q.v) f = dist(rng);
    }
    return out;
}

const std::vector<Float4>& Data() {
    static const std::vector<Float4> data = MakeData(kBatch * 3);
    return data;
}

} // namespace

// ============================================================== latency: MulAdd
// A serial chain: each iteration depends on the previous result, so the CPU
// cannot overlap iterations and the measurement reflects real operation latency.
//
// acc*b + c is a fixed point at acc == 1 when b + c == 1, so these constants
// hold the accumulator exactly at 1.0 over hundreds of millions of iterations.
// Any other pair drifts to infinity or to denormals, and the loop then times
// microcode assists rather than the instruction under test.
constexpr float kMulAddStableB = 0.75f;
constexpr float kMulAddStableC = 0.25f;

static void BM_Mathf_MulAdd_Latency(benchmark::State& state) {
    mathf::VecReg acc = mathf::Splat(1.0f);
    const mathf::VecReg b = mathf::Splat(kMulAddStableB);
    const mathf::VecReg c = mathf::Splat(kMulAddStableC);
    for (auto _ : state) {
        acc = mathf::MulAdd(acc, b, c);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (mathf::GetX(acc) < 0.5f || mathf::GetX(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(BM_Mathf_MulAdd_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_MulAdd_Latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(1.0f);
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(kMulAddStableB);
    const DirectX::XMVECTOR c = DirectX::XMVectorReplicate(kMulAddStableC);
    for (auto _ : state) {
        acc = DirectX::XMVectorMultiplyAdd(acc, b, c);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (DirectX::XMVectorGetX(acc) < 0.5f || DirectX::XMVectorGetX(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(BM_DXMath_MulAdd_Latency);
#endif

#if MATHF_BENCH_HAS_GLM
static void BM_GLM_MulAdd_Latency(benchmark::State& state) {
    glm::vec4 acc(1.0f);
    const glm::vec4 b(kMulAddStableB);
    const glm::vec4 c(kMulAddStableC);
    for (auto _ : state) {
        acc = acc * b + c;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc.x < 0.5f || acc.x > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(BM_GLM_MulAdd_Latency);
#endif

#if MATHF_BENCH_HAS_VECTORMATH
static void BM_Vectormath_MulAdd_Latency(benchmark::State& state) {
    Vectormath::SSE::Vector4 acc(1.0f);
    const Vectormath::SSE::Vector4 b(kMulAddStableB);
    const Vectormath::SSE::Vector4 c(kMulAddStableC);
    for (auto _ : state) {
        // No fused form in the API; component-wise multiply then add is the
        // idiomatic spelling.
        acc = Vectormath::SSE::mulPerElem(acc, b) + c;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    const float x = static_cast<float>(acc.getX());
    if (x < 0.5f || x > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(BM_Vectormath_MulAdd_Latency);
#endif

// =============================================================== latency: Add
// Adding 1.0 rather than 0.0: fast-math is allowed to fold x + 0 away entirely,
// which would leave the loop measuring nothing. The accumulator climbs to 2^24
// and then stops changing, because 1.0 falls below the ULP there -- bounded, and
// the instruction still issues every iteration.
static void BM_Mathf_Add_Latency(benchmark::State& state) {
    mathf::VecReg acc = mathf::Splat(1.0f);
    const mathf::VecReg b = mathf::Splat(1.0f);
    for (auto _ : state) {
        acc = mathf::Add(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!std::isfinite(mathf::GetX(acc))) {
        state.SkipWithError("accumulator left the finite range");
    }
}
BENCHMARK(BM_Mathf_Add_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Add_Latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(1.0f);
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(1.0f);
    for (auto _ : state) {
        acc = DirectX::XMVectorAdd(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!std::isfinite(DirectX::XMVectorGetX(acc))) {
        state.SkipWithError("accumulator left the finite range");
    }
}
BENCHMARK(BM_DXMath_Add_Latency);
#endif

#if MATHF_BENCH_HAS_GLM
static void BM_GLM_Add_Latency(benchmark::State& state) {
    glm::vec4 acc(1.0f);
    const glm::vec4 b(1.0f);
    for (auto _ : state) {
        acc = acc + b;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!std::isfinite(acc.x)) state.SkipWithError("accumulator left the finite range");
}
BENCHMARK(BM_GLM_Add_Latency);
#endif

#if MATHF_BENCH_HAS_VECTORMATH
static void BM_Vectormath_Add_Latency(benchmark::State& state) {
    Vectormath::SSE::Vector4 acc(1.0f);
    const Vectormath::SSE::Vector4 b(1.0f);
    for (auto _ : state) {
        acc = acc + b;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!std::isfinite(static_cast<float>(acc.getX()))) {
        state.SkipWithError("accumulator left the finite range");
    }
}
BENCHMARK(BM_Vectormath_Add_Latency);
#endif

// =============================================================== latency: Sqrt
// Repeated square roots converge to 1.0 from any positive start, so this chain
// is self-stabilising and needs no tuned constant.
static void BM_Mathf_Sqrt_Latency(benchmark::State& state) {
    mathf::VecReg acc = mathf::Splat(16.0f);
    for (auto _ : state) {
        acc = mathf::Sqrt(acc);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(BM_Mathf_Sqrt_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Sqrt_Latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(16.0f);
    for (auto _ : state) {
        acc = DirectX::XMVectorSqrt(acc);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(BM_DXMath_Sqrt_Latency);
#endif

// =============================================================== latency: Dot3
// Dot3 splats the sum of three lanes, so a self-feeding chain multiplies the
// accumulator by sum(b.xyz) each step; 1/3 in each lane holds it at 1.0. Same
// reasoning as Dot4 below.
static void BM_Mathf_Dot3_Latency(benchmark::State& state) {
    mathf::VecReg acc = mathf::Splat(1.0f);
    const mathf::VecReg b = mathf::Splat(1.0f / 3.0f);
    for (auto _ : state) {
        acc = mathf::Dot3(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (mathf::GetX(acc) < 0.5f || mathf::GetX(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(BM_Mathf_Dot3_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Dot3_Latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(1.0f);
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(1.0f / 3.0f);
    for (auto _ : state) {
        acc = DirectX::XMVector3Dot(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (DirectX::XMVectorGetX(acc) < 0.5f || DirectX::XMVectorGetX(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(BM_DXMath_Dot3_Latency);
#endif

// ================================================================ latency: Dot4
// Dot4 splats its result, so feeding it back multiplies the accumulator by
// sum(b) each iteration. Any sum(b) != 1 makes the value decay or explode until
// it hits denormals, and the loop then measures FP-assist stalls instead of the
// instruction -- an earlier version of this benchmark read 103 ns/op for exactly
// that reason. Splat(0.25) sums to 1, holding the accumulator fixed.
constexpr float kDot4StableLane = 0.25f;

static void BM_Mathf_Dot4_Latency(benchmark::State& state) {
    mathf::VecReg acc = mathf::Splat(1.0f);
    const mathf::VecReg b = mathf::Splat(kDot4StableLane);
    for (auto _ : state) {
        acc = mathf::Dot4(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    // Guards the premise above: if the accumulator drifted, the timing is void.
    if (mathf::GetX(acc) < 0.5f || mathf::GetX(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(BM_Mathf_Dot4_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Dot4_Latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(1.0f);
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(kDot4StableLane);
    for (auto _ : state) {
        acc = DirectX::XMVector4Dot(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (DirectX::XMVectorGetX(acc) < 0.5f || DirectX::XMVectorGetX(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(BM_DXMath_Dot4_Latency);
#endif

// ====================================================== latency: Dot4 as scalar
// The vector-chained Dot4 above is not comparable across all four libraries:
// Mathf and DirectXMath splat the result across the register, GLM returns a bare
// float, and Vectormath returns a FloatInVec. Chaining them as vectors would
// charge GLM for a broadcast the others get for free.
//
// This family instead measures what callers actually write -- compute a dot and
// use the scalar -- with the identical shape everywhere: broadcast a float,
// dot it, read one lane back. b sums to 1 so the accumulator holds at 1.0.
constexpr float kDotStableLane = 0.25f;

static void BM_Mathf_Dot4Scalar_Latency(benchmark::State& state) {
    float acc = 1.0f;
    const mathf::VecReg b = mathf::Splat(kDotStableLane);
    for (auto _ : state) {
        acc = mathf::GetX(mathf::Dot4(mathf::Splat(acc), b));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc < 0.5f || acc > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_Mathf_Dot4Scalar_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Dot4Scalar_Latency(benchmark::State& state) {
    float acc = 1.0f;
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(kDotStableLane);
    for (auto _ : state) {
        acc = DirectX::XMVectorGetX(
            DirectX::XMVector4Dot(DirectX::XMVectorReplicate(acc), b));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc < 0.5f || acc > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_DXMath_Dot4Scalar_Latency);
#endif

#if MATHF_BENCH_HAS_GLM
static void BM_GLM_Dot4Scalar_Latency(benchmark::State& state) {
    float acc = 1.0f;
    const glm::vec4 b(kDotStableLane);
    for (auto _ : state) {
        acc = glm::dot(glm::vec4(acc), b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc < 0.5f || acc > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_GLM_Dot4Scalar_Latency);
#endif

#if MATHF_BENCH_HAS_VECTORMATH
static void BM_Vectormath_Dot4Scalar_Latency(benchmark::State& state) {
    float acc = 1.0f;
    const Vectormath::SSE::Vector4 b(kDotStableLane);
    for (auto _ : state) {
        acc = static_cast<float>(
            Vectormath::SSE::dot(Vectormath::SSE::Vector4(acc), b));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc < 0.5f || acc > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_Vectormath_Dot4Scalar_Latency);
#endif

// ================================================ latency: Vector3 expression
// The decisive test for Phase 2's storage decision (docs/PLAN.md).
//
// Mathf's Vector3 is twelve packed bytes, so `a * b + c` promotes to a register,
// computes, and stores back on every step. That is only free if force-inlining
// lets the compiler keep the value in a register between steps -- which is the
// assumption the design rests on, and the reason for measuring rather than
// asserting it.
//
// Three variants make the answer readable:
//   Mathf Vector3   packed storage, operations promote and store
//   DXMath XMVECTOR the register held across the whole loop -- the ceiling
//   DXMath XMFLOAT3 load and store every step -- what Vector3 literally does,
//                   and what SimpleMath pays
// Matching XMVECTOR means the stores fold away. Matching only XMFLOAT3 means
// they do not, and the design should change.
static void BM_Mathf_Vector3_Chain_Latency(benchmark::State& state) {
    mathf::Vector3 acc{1.0f, 1.0f, 1.0f};
    const mathf::Vector3 b{kMulAddStableB, kMulAddStableB, kMulAddStableB};
    const mathf::Vector3 c{kMulAddStableC, kMulAddStableC, kMulAddStableC};
    for (auto _ : state) {
        acc = acc * b + c;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc.x < 0.5f || acc.x > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_Mathf_Vector3_Chain_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_XMVECTOR_Chain_Latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(1.0f);
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(kMulAddStableB);
    const DirectX::XMVECTOR c = DirectX::XMVectorReplicate(kMulAddStableC);
    for (auto _ : state) {
        acc = DirectX::XMVectorMultiplyAdd(acc, b, c);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    const float x = DirectX::XMVectorGetX(acc);
    if (x < 0.5f || x > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_DXMath_XMVECTOR_Chain_Latency);

static void BM_DXMath_XMFLOAT3_Chain_Latency(benchmark::State& state) {
    DirectX::XMFLOAT3 acc{1.0f, 1.0f, 1.0f};
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(kMulAddStableB);
    const DirectX::XMVECTOR c = DirectX::XMVectorReplicate(kMulAddStableC);
    for (auto _ : state) {
        DirectX::XMStoreFloat3(
            &acc, DirectX::XMVectorMultiplyAdd(DirectX::XMLoadFloat3(&acc), b, c));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc.x < 0.5f || acc.x > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_DXMath_XMFLOAT3_Chain_Latency);
#endif

#if MATHF_BENCH_HAS_GLM
static void BM_GLM_Vector3_Chain_Latency(benchmark::State& state) {
    glm::vec3 acc(1.0f);
    const glm::vec3 b(kMulAddStableB);
    const glm::vec3 c(kMulAddStableC);
    for (auto _ : state) {
        acc = acc * b + c;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc.x < 0.5f || acc.x > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_GLM_Vector3_Chain_Latency);
#endif

// ========================================== throughput: Vector3 normalize
// A realistic stream: read a packed twelve-byte position array, normalize, write
// it back. This is where Vector3's packing earns its keep -- there is no padding
// to skip and no conversion pass.
namespace {

constexpr int kVec3Batch = 512;

const std::vector<mathf::Vector3>& Vector3Data() {
    static const std::vector<mathf::Vector3> data = [] {
        std::mt19937 rng(kSeed);
        std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
        std::vector<mathf::Vector3> out(kVec3Batch);
        for (auto& v : out) v = mathf::Vector3{dist(rng), dist(rng), dist(rng)};
        return out;
    }();
    return data;
}

} // namespace

static void BM_Mathf_Vector3_Normalize_Throughput(benchmark::State& state) {
    const auto& in = Vector3Data();
    std::vector<mathf::Vector3> out(kVec3Batch);
    for (auto _ : state) {
        for (int i = 0; i < kVec3Batch; ++i) {
            out[static_cast<size_t>(i)] = mathf::Normalize(in[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kVec3Batch);
}
BENCHMARK(BM_Mathf_Vector3_Normalize_Throughput);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Vector3_Normalize_Throughput(benchmark::State& state) {
    const auto& in = Vector3Data();
    std::vector<DirectX::XMFLOAT3> out(kVec3Batch);
    for (auto _ : state) {
        for (int i = 0; i < kVec3Batch; ++i) {
            const auto* q =
                reinterpret_cast<const DirectX::XMFLOAT3*>(&in[static_cast<size_t>(i)].x);
            DirectX::XMStoreFloat3(
                &out[static_cast<size_t>(i)],
                DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(q)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kVec3Batch);
}
BENCHMARK(BM_DXMath_Vector3_Normalize_Throughput);
#endif

#if MATHF_BENCH_HAS_GLM
static void BM_GLM_Vector3_Normalize_Throughput(benchmark::State& state) {
    const auto& in = Vector3Data();
    std::vector<glm::vec3> out(kVec3Batch);
    for (auto _ : state) {
        for (int i = 0; i < kVec3Batch; ++i) {
            const auto& v = *reinterpret_cast<const glm::vec3*>(
                &in[static_cast<size_t>(i)].x);
            out[static_cast<size_t>(i)] = glm::normalize(v);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kVec3Batch);
}
BENCHMARK(BM_GLM_Vector3_Normalize_Throughput);
#endif

// ============================================== Matrix4x4 multiply and inverse
// The Phase 3 gate. A quarter turn about Z is the multiplier: it is orthonormal,
// so a self-feeding chain cycles with period four instead of growing without
// bound, and it is not the identity, which a compiler could fold away.
namespace {

constexpr mathf::Matrix4x4 kQuarterTurnZ{ 0, 1, 0, 0,
                                         -1, 0, 0, 0,
                                          0, 0, 1, 0,
                                          0, 0, 0, 1};

constexpr int kMatrixBatch = 256;

const std::vector<mathf::Matrix4x4>& MatrixData() {
    static const std::vector<mathf::Matrix4x4> data = [] {
        std::mt19937 rng(kSeed);
        std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
        std::vector<mathf::Matrix4x4> out(kMatrixBatch);
        for (auto& mat : out) {
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) mat.m[i][j] = dist(rng);
                mat.m[i][i] += 40.0f;   // keep them comfortably invertible
            }
        }
        return out;
    }();
    return data;
}

} // namespace

static void BM_Mathf_Matrix4x4_Multiply_Latency(benchmark::State& state) {
    mathf::Matrix4x4 acc = mathf::Matrix4x4::Identity();
    for (auto _ : state) {
        acc = acc * kQuarterTurnZ;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (std::abs(acc.m[0][0]) > 1.5f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(BM_Mathf_Matrix4x4_Multiply_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Matrix4x4_Multiply_Latency(benchmark::State& state) {
    DirectX::XMMATRIX acc = DirectX::XMMatrixIdentity();
    const DirectX::XMMATRIX b = DirectX::XMMatrixSet(
        0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    for (auto _ : state) {
        acc = DirectX::XMMatrixMultiply(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(BM_DXMath_Matrix4x4_Multiply_Latency);
#endif

static void BM_Mathf_Matrix4x4_Multiply_Throughput(benchmark::State& state) {
    const auto& d = MatrixData();
    std::vector<mathf::Matrix4x4> out(kMatrixBatch / 2);
    for (auto _ : state) {
        for (int i = 0; i < kMatrixBatch / 2; ++i) {
            out[static_cast<size_t>(i)] =
                d[static_cast<size_t>(i)] * d[static_cast<size_t>(i + kMatrixBatch / 2)];
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (kMatrixBatch / 2));
}
BENCHMARK(BM_Mathf_Matrix4x4_Multiply_Throughput);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Matrix4x4_Multiply_Throughput(benchmark::State& state) {
    const auto& d = MatrixData();
    std::vector<DirectX::XMFLOAT4X4> out(kMatrixBatch / 2);
    for (auto _ : state) {
        for (int i = 0; i < kMatrixBatch / 2; ++i) {
            const auto* qa = reinterpret_cast<const DirectX::XMFLOAT4X4*>(
                &d[static_cast<size_t>(i)].m[0][0]);
            const auto* qb = reinterpret_cast<const DirectX::XMFLOAT4X4*>(
                &d[static_cast<size_t>(i + kMatrixBatch / 2)].m[0][0]);
            DirectX::XMStoreFloat4x4(
                &out[static_cast<size_t>(i)],
                DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(qa),
                                          DirectX::XMLoadFloat4x4(qb)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (kMatrixBatch / 2));
}
BENCHMARK(BM_DXMath_Matrix4x4_Multiply_Throughput);
#endif

#if MATHF_BENCH_HAS_GLM
// GLM is column-major with column vectors, so its product is not numerically the
// same as ours for the same stored bytes. The instruction count is, which is all
// this measures.
static void BM_GLM_Matrix4x4_Multiply_Throughput(benchmark::State& state) {
    const auto& d = MatrixData();
    std::vector<glm::mat4> out(kMatrixBatch / 2);
    for (auto _ : state) {
        for (int i = 0; i < kMatrixBatch / 2; ++i) {
            const auto& a = *reinterpret_cast<const glm::mat4*>(
                &d[static_cast<size_t>(i)].m[0][0]);
            const auto& b = *reinterpret_cast<const glm::mat4*>(
                &d[static_cast<size_t>(i + kMatrixBatch / 2)].m[0][0]);
            out[static_cast<size_t>(i)] = a * b;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (kMatrixBatch / 2));
}
BENCHMARK(BM_GLM_Matrix4x4_Multiply_Throughput);
#endif

static void BM_Mathf_Matrix4x4_Inverse(benchmark::State& state) {
    const auto& d = MatrixData();
    std::vector<mathf::Matrix4x4> out(kMatrixBatch);
    for (auto _ : state) {
        for (int i = 0; i < kMatrixBatch; ++i) {
            out[static_cast<size_t>(i)] = mathf::Inverse(d[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kMatrixBatch);
}
BENCHMARK(BM_Mathf_Matrix4x4_Inverse);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Matrix4x4_Inverse(benchmark::State& state) {
    const auto& d = MatrixData();
    std::vector<DirectX::XMFLOAT4X4> out(kMatrixBatch);
    for (auto _ : state) {
        for (int i = 0; i < kMatrixBatch; ++i) {
            const auto* q = reinterpret_cast<const DirectX::XMFLOAT4X4*>(
                &d[static_cast<size_t>(i)].m[0][0]);
            DirectX::XMStoreFloat4x4(
                &out[static_cast<size_t>(i)],
                DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(q)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kMatrixBatch);
}
BENCHMARK(BM_DXMath_Matrix4x4_Inverse);
#endif

#if MATHF_BENCH_HAS_GLM
static void BM_GLM_Matrix4x4_Inverse(benchmark::State& state) {
    const auto& d = MatrixData();
    std::vector<glm::mat4> out(kMatrixBatch);
    for (auto _ : state) {
        for (int i = 0; i < kMatrixBatch; ++i) {
            const auto& a = *reinterpret_cast<const glm::mat4*>(
                &d[static_cast<size_t>(i)].m[0][0]);
            out[static_cast<size_t>(i)] = glm::inverse(a);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kMatrixBatch);
}
BENCHMARK(BM_GLM_Matrix4x4_Inverse);
#endif

// ========================================================== throughput: MulAdd
// Streams over arrays. This is where storage-type and ABI decisions show up --
// the register-resident latency benchmarks above cannot see them.

static void BM_Mathf_MulAdd_Throughput(benchmark::State& state) {
    const auto& d = Data();
    std::vector<Float4> out(kBatch);
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) {
            const mathf::VecReg a = mathf::LoadAligned(d[i].v);
            const mathf::VecReg b = mathf::LoadAligned(d[i + kBatch].v);
            const mathf::VecReg c = mathf::LoadAligned(d[i + 2 * kBatch].v);
            mathf::StoreAligned(out[static_cast<size_t>(i)].v,
                                mathf::MulAdd(a, b, c));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_Mathf_MulAdd_Throughput);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_MulAdd_Throughput(benchmark::State& state) {
    const auto& d = Data();
    std::vector<DirectX::XMFLOAT4A> out(kBatch);
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) {
            const auto* qa = reinterpret_cast<const DirectX::XMFLOAT4A*>(d[i].v);
            const auto* qb =
                reinterpret_cast<const DirectX::XMFLOAT4A*>(d[i + kBatch].v);
            const auto* qc =
                reinterpret_cast<const DirectX::XMFLOAT4A*>(d[i + 2 * kBatch].v);
            DirectX::XMStoreFloat4A(
                &out[static_cast<size_t>(i)],
                DirectX::XMVectorMultiplyAdd(DirectX::XMLoadFloat4A(qa),
                                             DirectX::XMLoadFloat4A(qb),
                                             DirectX::XMLoadFloat4A(qc)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_DXMath_MulAdd_Throughput);
#endif

#if MATHF_BENCH_HAS_GLM
static void BM_GLM_MulAdd_Throughput(benchmark::State& state) {
    const auto& d = Data();
    std::vector<glm::vec4> out(kBatch);
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) {
            const auto& a = *reinterpret_cast<const glm::vec4*>(d[i].v);
            const auto& b = *reinterpret_cast<const glm::vec4*>(d[i + kBatch].v);
            const auto& c =
                *reinterpret_cast<const glm::vec4*>(d[i + 2 * kBatch].v);
            out[static_cast<size_t>(i)] = a * b + c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_GLM_MulAdd_Throughput);
#endif

#if MATHF_BENCH_HAS_VECTORMATH
static void BM_Vectormath_MulAdd_Throughput(benchmark::State& state) {
    const auto& d = Data();
    std::vector<Vectormath::SSE::Vector4> out(kBatch);
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) {
            // Vectormath exposes no load helper for Vector4, but it is an
            // SSE-native type with a __m128 constructor, so this is the
            // idiomatic aligned load for it.
            const Vectormath::SSE::Vector4 a(_mm_load_ps(d[i].v));
            const Vectormath::SSE::Vector4 b(_mm_load_ps(d[i + kBatch].v));
            const Vectormath::SSE::Vector4 c(_mm_load_ps(d[i + 2 * kBatch].v));
            out[static_cast<size_t>(i)] = Vectormath::SSE::mulPerElem(a, b) + c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_Vectormath_MulAdd_Throughput);
#endif

// ============================================================ quaternion (Phase 4)
namespace {

constexpr int kQuatBatch = 256;

const std::vector<mathf::Quaternion>& QuatData() {
    static const std::vector<mathf::Quaternion> data = [] {
        std::mt19937 rng(kSeed + 5);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<mathf::Quaternion> out(kQuatBatch);
        for (auto& q : out) {
            // Unit quaternions, which is what every consumer of these expects.
            const mathf::Vector3 axis{dist(rng), dist(rng), dist(rng) + 1.5f};
            q = mathf::QuaternionFromAxisAngle(axis, dist(rng) * 3.0f);
        }
        return out;
    }();
    return data;
}

// A numeric fixed point for the latency chains, the same trick the MulAdd chain
// above needs: a half turn about Z is exactly (0, 0, 1, 0), and composing it
// walks a four-state cycle whose every component is exactly 0 or +/-1. Nothing
// rounds, so the accumulator cannot drift off the unit sphere over hundreds of
// millions of products -- and a drifting accumulator does not merely spoil the
// answer, it lands in denormals and times microcode assists instead of the
// instruction under test. A small angle was tried first and drifted out of range
// within one run.
const mathf::Quaternion kHalfTurnZ{0.0f, 0.0f, 1.0f, 0.0f};

} // namespace

static void BM_Mathf_Quaternion_Multiply_Latency(benchmark::State& state) {
    mathf::Quaternion acc = mathf::Quaternion::Identity();
    for (auto _ : state) {
        acc = acc * kHalfTurnZ;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!(mathf::Length(acc) > 0.99f && mathf::Length(acc) < 1.01f)) {
        state.SkipWithError("accumulator drifted off the unit sphere");
    }
}
BENCHMARK(BM_Mathf_Quaternion_Multiply_Latency);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Quaternion_Multiply_Latency(benchmark::State& state) {
    const DirectX::XMVECTOR turn = DirectX::XMVectorSet(
        kHalfTurnZ.x, kHalfTurnZ.y, kHalfTurnZ.z, kHalfTurnZ.w);
    DirectX::XMVECTOR acc = DirectX::XMQuaternionIdentity();
    for (auto _ : state) {
        acc = DirectX::XMQuaternionMultiply(acc, turn);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    const float length =
        DirectX::XMVectorGetX(DirectX::XMQuaternionLength(acc));
    if (!(length > 0.99f && length < 1.01f)) {
        state.SkipWithError("accumulator drifted off the unit sphere");
    }
}
BENCHMARK(BM_DXMath_Quaternion_Multiply_Latency);
#endif

#if MATHF_BENCH_HAS_DXMATH
// The same chain, but with DirectXMath holding its accumulator in XMFLOAT4
// storage instead of an XMVECTOR register.
//
// This is the fair comparison, and the reason the one above is not: Mathf's
// Quaternion is a packed sixteen-byte struct, so every link of the chain stores
// the result and loads it back, while an XMVECTOR accumulator never leaves a
// register. Phase 2 hit the same asymmetry with Vector3 against XMVECTOR and
// XMFLOAT3, and the answer there was to measure both rather than pick whichever
// one flattered. Throughput does not care -- the stores pipeline -- which is why
// the batch numbers match and these do not.
static void BM_DXMath_Quaternion_Multiply_Latency_Packed(benchmark::State& state) {
    const DirectX::XMVECTOR turn = DirectX::XMVectorSet(
        kHalfTurnZ.x, kHalfTurnZ.y, kHalfTurnZ.z, kHalfTurnZ.w);
    DirectX::XMFLOAT4 acc{0.0f, 0.0f, 0.0f, 1.0f};
    for (auto _ : state) {
        DirectX::XMStoreFloat4(
            &acc, DirectX::XMQuaternionMultiply(DirectX::XMLoadFloat4(&acc), turn));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(BM_DXMath_Quaternion_Multiply_Latency_Packed);
#endif

static void BM_Mathf_Quaternion_Multiply_Throughput(benchmark::State& state) {
    const auto& d = QuatData();
    std::vector<mathf::Quaternion> out(kQuatBatch / 2);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch / 2; ++i) {
            out[static_cast<size_t>(i)] =
                d[static_cast<size_t>(i)] * d[static_cast<size_t>(i + kQuatBatch / 2)];
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (kQuatBatch / 2));
}
BENCHMARK(BM_Mathf_Quaternion_Multiply_Throughput);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Quaternion_Multiply_Throughput(benchmark::State& state) {
    const auto& d = QuatData();
    std::vector<DirectX::XMFLOAT4> out(kQuatBatch / 2);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch / 2; ++i) {
            const auto* a = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i)].x);
            const auto* b = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i + kQuatBatch / 2)].x);
            DirectX::XMStoreFloat4(
                &out[static_cast<size_t>(i)],
                DirectX::XMQuaternionMultiply(DirectX::XMLoadFloat4(a),
                                              DirectX::XMLoadFloat4(b)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (kQuatBatch / 2));
}
BENCHMARK(BM_DXMath_Quaternion_Multiply_Throughput);
#endif

static void BM_Mathf_Quaternion_Slerp(benchmark::State& state) {
    const auto& d = QuatData();
    std::vector<mathf::Quaternion> out(kQuatBatch / 2);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch / 2; ++i) {
            out[static_cast<size_t>(i)] =
                mathf::Slerp(d[static_cast<size_t>(i)],
                             d[static_cast<size_t>(i + kQuatBatch / 2)], 0.37f);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (kQuatBatch / 2));
}
BENCHMARK(BM_Mathf_Quaternion_Slerp);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Quaternion_Slerp(benchmark::State& state) {
    const auto& d = QuatData();
    std::vector<DirectX::XMFLOAT4> out(kQuatBatch / 2);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch / 2; ++i) {
            const auto* a = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i)].x);
            const auto* b = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i + kQuatBatch / 2)].x);
            DirectX::XMStoreFloat4(
                &out[static_cast<size_t>(i)],
                DirectX::XMQuaternionSlerp(DirectX::XMLoadFloat4(a),
                                           DirectX::XMLoadFloat4(b), 0.37f));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (kQuatBatch / 2));
}
BENCHMARK(BM_DXMath_Quaternion_Slerp);
#endif

// Rotating a vector by a quaternion, which is what a skinning or particle
// system actually spends its time on.
static void BM_Mathf_Quaternion_RotateVector(benchmark::State& state) {
    const auto& d = QuatData();
    const auto& v = Data();
    std::vector<mathf::Vector3> out(kQuatBatch);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            const mathf::Vector3 p{v[static_cast<size_t>(i)].v[0],
                                   v[static_cast<size_t>(i)].v[1],
                                   v[static_cast<size_t>(i)].v[2]};
            out[static_cast<size_t>(i)] =
                mathf::Rotate(p, d[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_Mathf_Quaternion_RotateVector);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Quaternion_RotateVector(benchmark::State& state) {
    const auto& d = QuatData();
    const auto& v = Data();
    std::vector<DirectX::XMFLOAT3> out(kQuatBatch);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            const auto* q = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i)].x);
            const DirectX::XMVECTOR p = DirectX::XMVectorSet(
                v[static_cast<size_t>(i)].v[0], v[static_cast<size_t>(i)].v[1],
                v[static_cast<size_t>(i)].v[2], 0.0f);
            DirectX::XMStoreFloat3(
                &out[static_cast<size_t>(i)],
                DirectX::XMVector3Rotate(p, DirectX::XMLoadFloat4(q)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_DXMath_Quaternion_RotateVector);
#endif

// Quaternion to matrix -- once per object per frame in any scene graph.
static void BM_Mathf_Quaternion_ToMatrix(benchmark::State& state) {
    const auto& d = QuatData();
    std::vector<mathf::Matrix4x4> out(kQuatBatch);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            out[static_cast<size_t>(i)] =
                mathf::RotationMatrix(d[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_Mathf_Quaternion_ToMatrix);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Quaternion_ToMatrix(benchmark::State& state) {
    const auto& d = QuatData();
    std::vector<DirectX::XMFLOAT4X4> out(kQuatBatch);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            const auto* q = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i)].x);
            DirectX::XMStoreFloat4x4(
                &out[static_cast<size_t>(i)],
                DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(q)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_DXMath_Quaternion_ToMatrix);
#endif

// The full TRS build, which is the per-object cost in a scene graph.
static void BM_Mathf_Transform_Compose(benchmark::State& state) {
    const auto& d = QuatData();
    std::vector<mathf::Matrix4x4> out(kQuatBatch);
    const mathf::Vector3 scale{1.5f, 2.0f, 0.75f};
    const mathf::Vector3 translation{3.0f, -4.0f, 5.0f};
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            out[static_cast<size_t>(i)] =
                mathf::Compose(scale, d[static_cast<size_t>(i)], translation);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_Mathf_Transform_Compose);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_Transform_Compose(benchmark::State& state) {
    const auto& d = QuatData();
    std::vector<DirectX::XMFLOAT4X4> out(kQuatBatch);
    const DirectX::XMVECTOR scale = DirectX::XMVectorSet(1.5f, 2.0f, 0.75f, 0.0f);
    const DirectX::XMVECTOR translation =
        DirectX::XMVectorSet(3.0f, -4.0f, 5.0f, 0.0f);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            const auto* q = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i)].x);
            DirectX::XMStoreFloat4x4(
                &out[static_cast<size_t>(i)],
                DirectX::XMMatrixAffineTransformation(
                    scale, DirectX::XMVectorZero(), DirectX::XMLoadFloat4(q),
                    translation));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_DXMath_Transform_Compose);
#endif

// ---------------------------------------------------- scalar transcendentals
// Mathf's Sin is a minimax polynomial rather than a call into <cmath>, because
// it has to be constant-evaluable. That is a design constraint, not a
// performance claim -- these two benchmarks are what says whether the constraint
// also happened to cost anything.
namespace {

const std::vector<float>& AngleData() {
    static const std::vector<float> data = [] {
        std::mt19937 rng(kSeed + 6);
        std::uniform_real_distribution<float> dist(-20.0f, 20.0f);
        std::vector<float> out(kQuatBatch);
        for (auto& a : out) a = dist(rng);
        return out;
    }();
    return data;
}

} // namespace

static void BM_Mathf_SinCos(benchmark::State& state) {
    const auto& d = AngleData();
    std::vector<float> out(static_cast<size_t>(kQuatBatch) * 2);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            float s = 0.0f, c = 0.0f;
            mathf::SinCos(d[static_cast<size_t>(i)], s, c);
            out[static_cast<size_t>(i) * 2] = s;
            out[static_cast<size_t>(i) * 2 + 1] = c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_Mathf_SinCos);

static void BM_StdLib_SinCos(benchmark::State& state) {
    const auto& d = AngleData();
    std::vector<float> out(static_cast<size_t>(kQuatBatch) * 2);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            out[static_cast<size_t>(i) * 2] = std::sin(d[static_cast<size_t>(i)]);
            out[static_cast<size_t>(i) * 2 + 1] = std::cos(d[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_StdLib_SinCos);

#if MATHF_BENCH_HAS_DXMATH
static void BM_DXMath_SinCos(benchmark::State& state) {
    const auto& d = AngleData();
    std::vector<float> out(static_cast<size_t>(kQuatBatch) * 2);
    for (auto _ : state) {
        for (int i = 0; i < kQuatBatch; ++i) {
            float s = 0.0f, c = 0.0f;
            DirectX::XMScalarSinCos(&s, &c, d[static_cast<size_t>(i)]);
            out[static_cast<size_t>(i) * 2] = s;
            out[static_cast<size_t>(i) * 2 + 1] = c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kQuatBatch);
}
BENCHMARK(BM_DXMath_SinCos);
#endif

BENCHMARK_MAIN();
