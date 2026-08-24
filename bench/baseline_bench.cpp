// Phase 0 baseline: establishes the measurement pipeline and records where
// DirectXMath sits, so every later phase has a number to be held against.
//
// Two axes are measured, because they fail differently:
//   Latency    -- a serial dependency chain; exposes per-op cost and any stall.
//   Throughput -- a batch over an array; exposes load/store and ABI overhead
//                 that a register-resident microbenchmark hides entirely.
//
// Release gate (docs/PLAN.md §4.2): Mathematics must be within +-5% of DirectXMath on
// every mapped operation. Run with --benchmark_repetitions=5 for stable numbers.

#include <mathematics/matrix.hpp>
#include <mathematics/transform.hpp>
#include <mathematics/vec_reg.hpp>
#include <mathematics/vector.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#if MATHEMATICS_BENCH_HAS_DXMATH
#  include <DirectXMath.h>
#endif
#if MATHEMATICS_BENCH_HAS_GLM
#  include <glm/glm.hpp>
#  include <glm/gtc/matrix_access.hpp>
#endif
#if MATHEMATICS_BENCH_HAS_VECTORMATH
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
constexpr int batch_size = 512;
constexpr unsigned random_seed = 0x4D617468u;

// 16-byte aligned so every library can use its aligned load, and laid out as a
// flat float array rather than a vector of std::array so the three operand
// streams are contiguous.
struct alignas(16) float4 {
    float v[4];
};

std::vector<float4> make_data(int n) {
    std::mt19937 rng(random_seed);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
    std::vector<float4> out(static_cast<size_t>(n));
    for (auto& q : out) {
        for (float& f : q.v) f = dist(rng);
    }
    return out;
}

const std::vector<float4>& data() {
    static const std::vector<float4> data = make_data(batch_size * 3);
    return data;
}

} // namespace

// ============================================================== latency: mul_add
// A serial chain: each iteration depends on the previous result, so the CPU
// cannot overlap iterations and the measurement reflects real operation latency.
//
// acc*b + c is a fixed point at acc == 1 when b + c == 1, so these constants
// hold the accumulator exactly at 1.0 over hundreds of millions of iterations.
// Any other pair drifts to infinity or to denormals, and the loop then times
// microcode assists rather than the instruction under test.
constexpr float mul_add_stable_b = 0.75f;
constexpr float mul_add_stable_c = 0.25f;

static void bm_mathematics_mul_add_latency(benchmark::State& state) {
    math::vec_reg acc = math::splat(1.0f);
    const math::vec_reg b = math::splat(mul_add_stable_b);
    const math::vec_reg c = math::splat(mul_add_stable_c);
    for (auto _ : state) {
        acc = math::mul_add(acc, b, c);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (math::get_x(acc) < 0.5f || math::get_x(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(bm_mathematics_mul_add_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_mul_add_latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(1.0f);
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(mul_add_stable_b);
    const DirectX::XMVECTOR c = DirectX::XMVectorReplicate(mul_add_stable_c);
    for (auto _ : state) {
        acc = DirectX::XMVectorMultiplyAdd(acc, b, c);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (DirectX::XMVectorGetX(acc) < 0.5f || DirectX::XMVectorGetX(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(bm_dx_math_mul_add_latency);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_mul_add_latency(benchmark::State& state) {
    glm::vec4 acc(1.0f);
    const glm::vec4 b(mul_add_stable_b);
    const glm::vec4 c(mul_add_stable_c);
    for (auto _ : state) {
        acc = acc * b + c;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc.x < 0.5f || acc.x > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(bm_glm_mul_add_latency);
#endif

#if MATHEMATICS_BENCH_HAS_VECTORMATH
static void bm_vectormath_mul_add_latency(benchmark::State& state) {
    Vectormath::SSE::Vector4 acc(1.0f);
    const Vectormath::SSE::Vector4 b(mul_add_stable_b);
    const Vectormath::SSE::Vector4 c(mul_add_stable_c);
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
BENCHMARK(bm_vectormath_mul_add_latency);
#endif

// =============================================================== latency: add
// Adding 1.0 rather than 0.0: fast-math is allowed to fold x + 0 away entirely,
// which would leave the loop measuring nothing. The accumulator climbs to 2^24
// and then stops changing, because 1.0 falls below the ULP there -- bounded, and
// the instruction still issues every iteration.
static void bm_mathematics_add_latency(benchmark::State& state) {
    math::vec_reg acc = math::splat(1.0f);
    const math::vec_reg b = math::splat(1.0f);
    for (auto _ : state) {
        acc = math::add(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!std::isfinite(math::get_x(acc))) {
        state.SkipWithError("accumulator left the finite range");
    }
}
BENCHMARK(bm_mathematics_add_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_add_latency(benchmark::State& state) {
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
BENCHMARK(bm_dx_math_add_latency);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_add_latency(benchmark::State& state) {
    glm::vec4 acc(1.0f);
    const glm::vec4 b(1.0f);
    for (auto _ : state) {
        acc = acc + b;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!std::isfinite(acc.x)) state.SkipWithError("accumulator left the finite range");
}
BENCHMARK(bm_glm_add_latency);
#endif

#if MATHEMATICS_BENCH_HAS_VECTORMATH
static void bm_vectormath_add_latency(benchmark::State& state) {
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
BENCHMARK(bm_vectormath_add_latency);
#endif

// =============================================================== latency: sqrt
// Repeated square roots converge to 1.0 from any positive start, so this chain
// is self-stabilising and needs no tuned constant.
static void bm_mathematics_sqrt_latency(benchmark::State& state) {
    math::vec_reg acc = math::splat(16.0f);
    for (auto _ : state) {
        acc = math::sqrt(acc);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(bm_mathematics_sqrt_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_sqrt_latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(16.0f);
    for (auto _ : state) {
        acc = DirectX::XMVectorSqrt(acc);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(bm_dx_math_sqrt_latency);
#endif

// =============================================================== latency: dot3
// dot3 splats the sum of three lanes, so a self-feeding chain multiplies the
// accumulator by sum(b.xyz) each step; 1/3 in each lane holds it at 1.0. Same
// reasoning as dot4 below.
static void bm_mathematics_dot3_latency(benchmark::State& state) {
    math::vec_reg acc = math::splat(1.0f);
    const math::vec_reg b = math::splat(1.0f / 3.0f);
    for (auto _ : state) {
        acc = math::dot3(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (math::get_x(acc) < 0.5f || math::get_x(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(bm_mathematics_dot3_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_dot3_latency(benchmark::State& state) {
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
BENCHMARK(bm_dx_math_dot3_latency);
#endif

// ================================================================ latency: dot4
// dot4 splats its result, so feeding it back multiplies the accumulator by
// sum(b) each iteration. Any sum(b) != 1 makes the value decay or explode until
// it hits denormals, and the loop then measures FP-assist stalls instead of the
// instruction -- an earlier version of this benchmark read 103 ns/op for exactly
// that reason. splat(0.25) sums to 1, holding the accumulator fixed.
constexpr float dot4_stable_lane = 0.25f;

static void bm_mathematics_dot4_latency(benchmark::State& state) {
    math::vec_reg acc = math::splat(1.0f);
    const math::vec_reg b = math::splat(dot4_stable_lane);
    for (auto _ : state) {
        acc = math::dot4(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    // Guards the premise above: if the accumulator drifted, the timing is void.
    if (math::get_x(acc) < 0.5f || math::get_x(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(bm_mathematics_dot4_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_dot4_latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(1.0f);
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(dot4_stable_lane);
    for (auto _ : state) {
        acc = DirectX::XMVector4Dot(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (DirectX::XMVectorGetX(acc) < 0.5f || DirectX::XMVectorGetX(acc) > 2.0f) {
        state.SkipWithError("accumulator drifted; latency measurement invalid");
    }
}
BENCHMARK(bm_dx_math_dot4_latency);
#endif

// ====================================================== latency: dot4 as scalar
// The vector-chained dot4 above is not comparable across all four libraries:
// Mathematics and DirectXMath splat the result across the register, GLM returns a bare
// float, and Vectormath returns a FloatInVec. Chaining them as vectors would
// charge GLM for a broadcast the others get for free.
//
// This family instead measures what callers actually write -- compute a dot and
// use the scalar -- with the identical shape everywhere: broadcast a float,
// dot it, read one lane back. b sums to 1 so the accumulator holds at 1.0.
constexpr float dot_stable_lane = 0.25f;

static void bm_mathematics_dot4_scalar_latency(benchmark::State& state) {
    float acc = 1.0f;
    const math::vec_reg b = math::splat(dot_stable_lane);
    for (auto _ : state) {
        acc = math::get_x(math::dot4(math::splat(acc), b));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc < 0.5f || acc > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_mathematics_dot4_scalar_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_dot4_scalar_latency(benchmark::State& state) {
    float acc = 1.0f;
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(dot_stable_lane);
    for (auto _ : state) {
        acc = DirectX::XMVectorGetX(
            DirectX::XMVector4Dot(DirectX::XMVectorReplicate(acc), b));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc < 0.5f || acc > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_dx_math_dot4_scalar_latency);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_dot4_scalar_latency(benchmark::State& state) {
    float acc = 1.0f;
    const glm::vec4 b(dot_stable_lane);
    for (auto _ : state) {
        acc = glm::dot(glm::vec4(acc), b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc < 0.5f || acc > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_glm_dot4_scalar_latency);
#endif

#if MATHEMATICS_BENCH_HAS_VECTORMATH
static void bm_vectormath_dot4_scalar_latency(benchmark::State& state) {
    float acc = 1.0f;
    const Vectormath::SSE::Vector4 b(dot_stable_lane);
    for (auto _ : state) {
        acc = static_cast<float>(
            Vectormath::SSE::dot(Vectormath::SSE::Vector4(acc), b));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc < 0.5f || acc > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_vectormath_dot4_scalar_latency);
#endif

// ================================================ latency: vector3 expression
// The decisive test for Phase 2's storage decision (docs/PLAN.md).
//
// Mathematics's vector3 is twelve packed bytes, so `a * b + c` promotes to a register,
// computes, and stores back on every step. That is only free if force-inlining
// lets the compiler keep the value in a register between steps -- which is the
// assumption the design rests on, and the reason for measuring rather than
// asserting it.
//
// Three variants make the answer readable:
//   Mathematics vector3   packed storage, operations promote and store
//   DXMath XMVECTOR the register held across the whole loop -- the ceiling
//   DXMath XMFLOAT3 load and store every step -- what vector3 literally does,
//                   and what SimpleMath pays
// Matching XMVECTOR means the stores fold away. Matching only XMFLOAT3 means
// they do not, and the design should change.
static void bm_mathematics_vector3_chain_latency(benchmark::State& state) {
    math::vector3 acc{1.0f, 1.0f, 1.0f};
    const math::vector3 b{mul_add_stable_b, mul_add_stable_b, mul_add_stable_b};
    const math::vector3 c{mul_add_stable_c, mul_add_stable_c, mul_add_stable_c};
    for (auto _ : state) {
        acc = acc * b + c;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc.x < 0.5f || acc.x > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_mathematics_vector3_chain_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_xmvector_chain_latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorReplicate(1.0f);
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(mul_add_stable_b);
    const DirectX::XMVECTOR c = DirectX::XMVectorReplicate(mul_add_stable_c);
    for (auto _ : state) {
        acc = DirectX::XMVectorMultiplyAdd(acc, b, c);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    const float x = DirectX::XMVectorGetX(acc);
    if (x < 0.5f || x > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_dx_math_xmvector_chain_latency);

static void bm_dx_math_xmfloat3_chain_latency(benchmark::State& state) {
    DirectX::XMFLOAT3 acc{1.0f, 1.0f, 1.0f};
    const DirectX::XMVECTOR b = DirectX::XMVectorReplicate(mul_add_stable_b);
    const DirectX::XMVECTOR c = DirectX::XMVectorReplicate(mul_add_stable_c);
    for (auto _ : state) {
        DirectX::XMStoreFloat3(
            &acc, DirectX::XMVectorMultiplyAdd(DirectX::XMLoadFloat3(&acc), b, c));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc.x < 0.5f || acc.x > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_dx_math_xmfloat3_chain_latency);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_vector3_chain_latency(benchmark::State& state) {
    glm::vec3 acc(1.0f);
    const glm::vec3 b(mul_add_stable_b);
    const glm::vec3 c(mul_add_stable_c);
    for (auto _ : state) {
        acc = acc * b + c;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (acc.x < 0.5f || acc.x > 2.0f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_glm_vector3_chain_latency);
#endif

// ========================================== throughput: vector3 normalize
// A realistic stream: read a packed twelve-byte position array, normalize, write
// it back. This is where vector3's packing earns its keep -- there is no padding
// to skip and no conversion pass.
namespace {

constexpr int vector3_batch_size = 512;

const std::vector<math::vector3>& vector3_data() {
    static const std::vector<math::vector3> data = [] {
        std::mt19937 rng(random_seed);
        std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
        std::vector<math::vector3> out(vector3_batch_size);
        for (auto& v : out) v = math::vector3{dist(rng), dist(rng), dist(rng)};
        return out;
    }();
    return data;
}

} // namespace

static void bm_mathematics_vector3_normalize_throughput(benchmark::State& state) {
    const auto& in = vector3_data();
    std::vector<math::vector3> out(vector3_batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < vector3_batch_size; ++i) {
            out[static_cast<size_t>(i)] = math::normalize(in[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * vector3_batch_size);
}
BENCHMARK(bm_mathematics_vector3_normalize_throughput);

static void bm_mathematics_vector3_normalize_unchecked_throughput(
    benchmark::State& state) {
    const auto& in = vector3_data();
    std::vector<math::vector3> out(vector3_batch_size);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < vector3_batch_size; ++i) {
            out[static_cast<size_t>(i)] =
                math::normalize_unchecked(in[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * vector3_batch_size);
}
BENCHMARK(bm_mathematics_vector3_normalize_unchecked_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_vector3_normalize_throughput(benchmark::State& state) {
    const auto& in = vector3_data();
    std::vector<DirectX::XMFLOAT3> out(vector3_batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < vector3_batch_size; ++i) {
            const auto* q =
                reinterpret_cast<const DirectX::XMFLOAT3*>(&in[static_cast<size_t>(i)].x);
            DirectX::XMStoreFloat3(
                &out[static_cast<size_t>(i)],
                DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(q)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * vector3_batch_size);
}
BENCHMARK(bm_dx_math_vector3_normalize_throughput);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_vector3_normalize_throughput(benchmark::State& state) {
    const auto& in = vector3_data();
    std::vector<glm::vec3> out(vector3_batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < vector3_batch_size; ++i) {
            const auto& v = *reinterpret_cast<const glm::vec3*>(
                &in[static_cast<size_t>(i)].x);
            out[static_cast<size_t>(i)] = glm::normalize(v);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * vector3_batch_size);
}
BENCHMARK(bm_glm_vector3_normalize_throughput);
#endif

// ============================================== matrix4x4 multiply and inverse
// The Phase 3 gate. A quarter turn about Z is the multiplier: it is orthonormal,
// so a self-feeding chain cycles with period four instead of growing without
// bound, and it is not the identity, which a compiler could fold away.
namespace {

constexpr math::matrix4x4 quarter_turn_z{ 0, 1, 0, 0,
                                         -1, 0, 0, 0,
                                          0, 0, 1, 0,
                                          0, 0, 0, 1};

constexpr int matrix_batch_size = 256;


const std::vector<math::matrix4x4>& matrix_data() {
    static const std::vector<math::matrix4x4> data = [] {
        std::mt19937 rng(random_seed);
        std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
        std::vector<math::matrix4x4> out(matrix_batch_size);
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

// The inverse benchmarks read their input and write their output through this
// arena, which pins the two arrays' RELATIVE page offset instead of leaving it
// to the allocator.
//
// It exists because the inverse numbers would not reproduce: the same binary
// measured 47, 54 and 74 M/s in different processes, each internally
// consistent. The cause is 4K aliasing -- when a store's low twelve address
// bits match an upcoming load's, the load is falsely held back behind the
// store -- and whether the input and output vectors collided that way was
// decided by ASLR, differently every process. A probe with a controlled
// offset pinned it: 48.6 M/s at a relative offset of 64 bytes (each store
// aliasing the very next iteration's load), 58 at 16, a flat 79 from 128 up.
// Phase 3 documented the irreproducibility as an open confusion; this was it.
// Half a page of separation parks every library's run in the flat region, so
// the comparison measures the arithmetic and not the allocator's mood.
struct inverse_arena {
    std::vector<unsigned char> storage;
    unsigned char* in;
    unsigned char* out;

    inverse_arena()
        : storage(2 * matrix_batch_size * sizeof(math::matrix4x4) + 3 * 4096) {
        const auto base = reinterpret_cast<std::uintptr_t>(storage.data());
        const std::uintptr_t page = (base + 4095u) & ~std::uintptr_t{4095u};
        in = reinterpret_cast<unsigned char*>(page);
        const std::uintptr_t after_in =
            (page + matrix_batch_size * sizeof(math::matrix4x4) + 4095u) &
            ~std::uintptr_t{4095u};
        out = reinterpret_cast<unsigned char*>(after_in + 2048u);

        std::memcpy(in, matrix_data().data(),
                    matrix_batch_size * sizeof(math::matrix4x4));
    }
};

inverse_arena& get_inverse_arena() {
    static inverse_arena arena_instance;
    return arena_instance;
}


} // namespace

static void bm_mathematics_matrix4x4_multiply_latency(benchmark::State& state) {
    math::matrix4x4 acc = math::matrix4x4::identity();
    for (auto _ : state) {
        acc = acc * quarter_turn_z;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (std::abs(acc.m[0][0]) > 1.5f) state.SkipWithError("accumulator drifted");
}
BENCHMARK(bm_mathematics_matrix4x4_multiply_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_matrix4x4_multiply_latency(benchmark::State& state) {
    DirectX::XMMATRIX acc = DirectX::XMMatrixIdentity();
    const DirectX::XMMATRIX b = DirectX::XMMatrixSet(
        0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    for (auto _ : state) {
        acc = DirectX::XMMatrixMultiply(acc, b);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(bm_dx_math_matrix4x4_multiply_latency);
#endif

static void bm_mathematics_matrix4x4_multiply_throughput(benchmark::State& state) {
    const auto& d = matrix_data();
    std::vector<math::matrix4x4> out(matrix_batch_size / 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size / 2; ++i) {
            out[static_cast<size_t>(i)] =
                d[static_cast<size_t>(i)] * d[static_cast<size_t>(i + matrix_batch_size / 2)];
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (matrix_batch_size / 2));
}
BENCHMARK(bm_mathematics_matrix4x4_multiply_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_matrix4x4_multiply_throughput(benchmark::State& state) {
    const auto& d = matrix_data();
    std::vector<DirectX::XMFLOAT4X4> out(matrix_batch_size / 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size / 2; ++i) {
            const auto* qa = reinterpret_cast<const DirectX::XMFLOAT4X4*>(
                &d[static_cast<size_t>(i)].m[0][0]);
            const auto* qb = reinterpret_cast<const DirectX::XMFLOAT4X4*>(
                &d[static_cast<size_t>(i + matrix_batch_size / 2)].m[0][0]);
            DirectX::XMStoreFloat4x4(
                &out[static_cast<size_t>(i)],
                DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(qa),
                                          DirectX::XMLoadFloat4x4(qb)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (matrix_batch_size / 2));
}
BENCHMARK(bm_dx_math_matrix4x4_multiply_throughput);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
// GLM is column-major with column vectors, so its product is not numerically the
// same as ours for the same stored bytes. The instruction count is, which is all
// this measures.
static void bm_glm_matrix4x4_multiply_throughput(benchmark::State& state) {
    const auto& d = matrix_data();
    std::vector<glm::mat4> out(matrix_batch_size / 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size / 2; ++i) {
            const auto& a = *reinterpret_cast<const glm::mat4*>(
                &d[static_cast<size_t>(i)].m[0][0]);
            const auto& b = *reinterpret_cast<const glm::mat4*>(
                &d[static_cast<size_t>(i + matrix_batch_size / 2)].m[0][0]);
            out[static_cast<size_t>(i)] = a * b;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (matrix_batch_size / 2));
}
BENCHMARK(bm_glm_matrix4x4_multiply_throughput);
#endif

static void bm_mathematics_matrix4x4_inverse(benchmark::State& state) {
    auto& arena_instance = get_inverse_arena();
    const auto* in = reinterpret_cast<const math::matrix4x4*>(arena_instance.in);
    auto* out = reinterpret_cast<math::matrix4x4*>(arena_instance.out);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            out[i] = math::inverse(in[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_mathematics_matrix4x4_inverse);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_matrix4x4_inverse(benchmark::State& state) {
    auto& arena_instance = get_inverse_arena();
    const auto* in = reinterpret_cast<const DirectX::XMFLOAT4X4*>(arena_instance.in);
    auto* out = reinterpret_cast<DirectX::XMFLOAT4X4*>(arena_instance.out);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            DirectX::XMStoreFloat4x4(
                &out[i],
                DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&in[i])));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_dx_math_matrix4x4_inverse);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_matrix4x4_inverse(benchmark::State& state) {
    auto& arena_instance = get_inverse_arena();
    const auto* in = reinterpret_cast<const glm::mat4*>(arena_instance.in);
    auto* out = reinterpret_cast<glm::mat4*>(arena_instance.out);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            out[i] = glm::inverse(in[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_glm_matrix4x4_inverse);
#endif

// ========================================================== throughput: mul_add
// Streams over arrays. This is where storage-type and ABI decisions show up --
// the register-resident latency benchmarks above cannot see them.

static void bm_mathematics_mul_add_throughput(benchmark::State& state) {
    const auto& d = data();
    std::vector<float4> out(batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < batch_size; ++i) {
            const math::vec_reg a = math::load_aligned(d[i].v);
            const math::vec_reg b = math::load_aligned(d[i + batch_size].v);
            const math::vec_reg c = math::load_aligned(d[i + 2 * batch_size].v);
            math::store_aligned(out[static_cast<size_t>(i)].v,
                                math::mul_add(a, b, c));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(bm_mathematics_mul_add_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_mul_add_throughput(benchmark::State& state) {
    const auto& d = data();
    std::vector<DirectX::XMFLOAT4A> out(batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < batch_size; ++i) {
            const auto* qa = reinterpret_cast<const DirectX::XMFLOAT4A*>(d[i].v);
            const auto* qb =
                reinterpret_cast<const DirectX::XMFLOAT4A*>(d[i + batch_size].v);
            const auto* qc =
                reinterpret_cast<const DirectX::XMFLOAT4A*>(d[i + 2 * batch_size].v);
            DirectX::XMStoreFloat4A(
                &out[static_cast<size_t>(i)],
                DirectX::XMVectorMultiplyAdd(DirectX::XMLoadFloat4A(qa),
                                             DirectX::XMLoadFloat4A(qb),
                                             DirectX::XMLoadFloat4A(qc)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(bm_dx_math_mul_add_throughput);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_mul_add_throughput(benchmark::State& state) {
    const auto& d = data();
    std::vector<glm::vec4> out(batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < batch_size; ++i) {
            const auto& a = *reinterpret_cast<const glm::vec4*>(d[i].v);
            const auto& b = *reinterpret_cast<const glm::vec4*>(d[i + batch_size].v);
            const auto& c =
                *reinterpret_cast<const glm::vec4*>(d[i + 2 * batch_size].v);
            out[static_cast<size_t>(i)] = a * b + c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(bm_glm_mul_add_throughput);
#endif

#if MATHEMATICS_BENCH_HAS_VECTORMATH
static void bm_vectormath_mul_add_throughput(benchmark::State& state) {
    const auto& d = data();
    std::vector<Vectormath::SSE::Vector4> out(batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < batch_size; ++i) {
            // Vectormath exposes no load helper for vector4, but it is an
            // SSE-native type with a __m128 constructor, so this is the
            // idiomatic aligned load for it.
            const Vectormath::SSE::Vector4 a(_mm_load_ps(d[i].v));
            const Vectormath::SSE::Vector4 b(_mm_load_ps(d[i + batch_size].v));
            const Vectormath::SSE::Vector4 c(_mm_load_ps(d[i + 2 * batch_size].v));
            out[static_cast<size_t>(i)] = Vectormath::SSE::mulPerElem(a, b) + c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(bm_vectormath_mul_add_throughput);
#endif

// ============================================================ quaternion (Phase 4)
namespace {

constexpr int quaternion_batch_size = 256;

const std::vector<math::quaternion>& quaternion_data() {
    static const std::vector<math::quaternion> data = [] {
        std::mt19937 rng(random_seed + 5);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<math::quaternion> out(quaternion_batch_size);
        for (auto& q : out) {
            // Unit quaternions, which is what every consumer of these expects.
            const math::vector3 axis{dist(rng), dist(rng), dist(rng) + 1.5f};
            q = math::quaternion_from_axis_angle(axis, dist(rng) * 3.0f);
        }
        return out;
    }();
    return data;
}

// A numeric fixed point for the latency chains, the same trick the mul_add chain
// above needs: a half turn about Z is exactly (0, 0, 1, 0), and composing it
// walks a four-state cycle whose every component is exactly 0 or +/-1. Nothing
// rounds, so the accumulator cannot drift off the unit sphere over hundreds of
// millions of products -- and a drifting accumulator does not merely spoil the
// answer, it lands in denormals and times microcode assists instead of the
// instruction under test. A small angle was tried first and drifted out of range
// within one run.
const math::quaternion half_turn_z{0.0f, 0.0f, 1.0f, 0.0f};

} // namespace

static void bm_mathematics_quaternion_multiply_latency(benchmark::State& state) {
    math::quaternion acc = math::quaternion::identity();
    math::quaternion turn = half_turn_z;
    benchmark::DoNotOptimize(turn);
    for (auto _ : state) {
        acc = acc * turn;
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!(math::length(acc) > 0.99f && math::length(acc) < 1.01f)) {
        state.SkipWithError("accumulator drifted off the unit sphere");
    }
}
BENCHMARK(bm_mathematics_quaternion_multiply_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_multiply_latency(benchmark::State& state) {
    DirectX::XMVECTOR turn = DirectX::XMVectorSet(
        half_turn_z.x, half_turn_z.y, half_turn_z.z, half_turn_z.w);
    benchmark::DoNotOptimize(turn);
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
BENCHMARK(bm_dx_math_quaternion_multiply_latency);
#endif

#if MATHEMATICS_BENCH_HAS_DXMATH
// The same chain, but with DirectXMath holding its accumulator in XMFLOAT4
// storage instead of an XMVECTOR register.
//
// This is the fair comparison, and the reason the one above is not: Mathematics's
// quaternion is a packed sixteen-byte struct, so every link of the chain stores
// the result and loads it back, while an XMVECTOR accumulator never leaves a
// register. Phase 2 hit the same asymmetry with vector3 against XMVECTOR and
// XMFLOAT3, and the answer there was to measure both rather than pick whichever
// one flattered. Throughput does not care -- the stores pipeline -- which is why
// the batch numbers match and these do not.
static void bm_dx_math_quaternion_multiply_latency_packed(benchmark::State& state) {
    DirectX::XMVECTOR turn = DirectX::XMVectorSet(
        half_turn_z.x, half_turn_z.y, half_turn_z.z, half_turn_z.w);
    benchmark::DoNotOptimize(turn);
    DirectX::XMFLOAT4 acc{0.0f, 0.0f, 0.0f, 1.0f};
    for (auto _ : state) {
        DirectX::XMStoreFloat4(
            &acc, DirectX::XMQuaternionMultiply(DirectX::XMLoadFloat4(&acc), turn));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(bm_dx_math_quaternion_multiply_latency_packed);
#endif

static void bm_mathematics_quaternion_multiply_throughput(benchmark::State& state) {
    const auto& d = quaternion_data();
    std::vector<math::quaternion> out(quaternion_batch_size / 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size / 2; ++i) {
            out[static_cast<size_t>(i)] =
                d[static_cast<size_t>(i)] * d[static_cast<size_t>(i + quaternion_batch_size / 2)];
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (quaternion_batch_size / 2));
}
BENCHMARK(bm_mathematics_quaternion_multiply_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_multiply_throughput(benchmark::State& state) {
    const auto& d = quaternion_data();
    std::vector<DirectX::XMFLOAT4> out(quaternion_batch_size / 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size / 2; ++i) {
            const auto* a = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i)].x);
            const auto* b = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i + quaternion_batch_size / 2)].x);
            DirectX::XMStoreFloat4(
                &out[static_cast<size_t>(i)],
                DirectX::XMQuaternionMultiply(DirectX::XMLoadFloat4(a),
                                              DirectX::XMLoadFloat4(b)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (quaternion_batch_size / 2));
}
BENCHMARK(bm_dx_math_quaternion_multiply_throughput);
#endif

// Each result is escaped individually, and the interpolation parameter is made
// opaque per outer iteration. Both are load-bearing.
//
// Everything the inner loop reads is otherwise loop-invariant, so a compiler
// may hoist the whole batch out of the timing loop. Clang did exactly that to
// the DirectXMath version, which reported 0.245 ns for 128 slerps -- 523 G/s,
// three orders of magnitude past plausible, and the kind of number that reads
// as a win if nobody checks it. The usual `DoNotOptimize(out.data())` after the
// loop was not enough: it escapes the pointer, not what was written through it,
// so the stores stayed dead. Making `t` opaque was not enough either.
// DoNotOptimize on each element is, and it costs both sides the same.
static void bm_mathematics_quaternion_slerp(benchmark::State& state) {
    const auto& d = quaternion_data();
    std::vector<math::quaternion> out(quaternion_batch_size / 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        float t = 0.37f;
        benchmark::DoNotOptimize(t);
        for (int i = 0; i < quaternion_batch_size / 2; ++i) {
            out[static_cast<size_t>(i)] =
                math::slerp(d[static_cast<size_t>(i)],
                             d[static_cast<size_t>(i + quaternion_batch_size / 2)], t);
            benchmark::DoNotOptimize(out[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (quaternion_batch_size / 2));
}
BENCHMARK(bm_mathematics_quaternion_slerp);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_slerp(benchmark::State& state) {
    const auto& d = quaternion_data();
    std::vector<DirectX::XMFLOAT4> out(quaternion_batch_size / 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        float t = 0.37f;
        benchmark::DoNotOptimize(t);
        for (int i = 0; i < quaternion_batch_size / 2; ++i) {
            const auto* a = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i)].x);
            const auto* b = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i + quaternion_batch_size / 2)].x);
            DirectX::XMStoreFloat4(
                &out[static_cast<size_t>(i)],
                DirectX::XMQuaternionSlerp(DirectX::XMLoadFloat4(a),
                                           DirectX::XMLoadFloat4(b), t));
            benchmark::DoNotOptimize(out[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (quaternion_batch_size / 2));
}
BENCHMARK(bm_dx_math_quaternion_slerp);
#endif

// Rotating a vector by a quaternion, which is what a skinning or particle
// system actually spends its time on.
static void bm_mathematics_quaternion_rotate_vector(benchmark::State& state) {
    const auto& d = quaternion_data();
    const auto& v = data();
    std::vector<math::vector3> out(quaternion_batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            const math::vector3 p{v[static_cast<size_t>(i)].v[0],
                                   v[static_cast<size_t>(i)].v[1],
                                   v[static_cast<size_t>(i)].v[2]};
            out[static_cast<size_t>(i)] =
                math::rotate(p, d[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_mathematics_quaternion_rotate_vector);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_rotate_vector(benchmark::State& state) {
    const auto& d = quaternion_data();
    const auto& v = data();
    std::vector<DirectX::XMFLOAT3> out(quaternion_batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
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
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_dx_math_quaternion_rotate_vector);
#endif

// quaternion to matrix -- once per object per frame in any scene graph.
static void bm_mathematics_quaternion_to_matrix(benchmark::State& state) {
    const auto& d = quaternion_data();
    std::vector<math::matrix4x4> out(quaternion_batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            out[static_cast<size_t>(i)] =
                math::rotation_matrix(d[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_mathematics_quaternion_to_matrix);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_to_matrix(benchmark::State& state) {
    const auto& d = quaternion_data();
    std::vector<DirectX::XMFLOAT4X4> out(quaternion_batch_size);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            const auto* q = reinterpret_cast<const DirectX::XMFLOAT4*>(
                &d[static_cast<size_t>(i)].x);
            DirectX::XMStoreFloat4x4(
                &out[static_cast<size_t>(i)],
                DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(q)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_dx_math_quaternion_to_matrix);
#endif

// The full TRS build, which is the per-object cost in a scene graph.
static void bm_mathematics_transform_compose(benchmark::State& state) {
    const auto& d = quaternion_data();
    std::vector<math::matrix4x4> out(quaternion_batch_size);
    const math::vector3 scale{1.5f, 2.0f, 0.75f};
    const math::vector3 translation{3.0f, -4.0f, 5.0f};
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            out[static_cast<size_t>(i)] =
                math::compose(scale, d[static_cast<size_t>(i)], translation);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_mathematics_transform_compose);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_transform_compose(benchmark::State& state) {
    const auto& d = quaternion_data();
    std::vector<DirectX::XMFLOAT4X4> out(quaternion_batch_size);
    const DirectX::XMVECTOR scale = DirectX::XMVectorSet(1.5f, 2.0f, 0.75f, 0.0f);
    const DirectX::XMVECTOR translation =
        DirectX::XMVectorSet(3.0f, -4.0f, 5.0f, 0.0f);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
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
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_dx_math_transform_compose);
#endif

// ---------------------------------------------------- scalar transcendentals
// Mathematics's sin is a minimax polynomial rather than a call into <cmath>, because
// it has to be constant-evaluable. That is a design constraint, not a
// performance claim -- these two benchmarks are what says whether the constraint
// also happened to cost anything.
namespace {

const std::vector<float>& angle_data() {
    static const std::vector<float> data = [] {
        std::mt19937 rng(random_seed + 6);
        std::uniform_real_distribution<float> dist(-20.0f, 20.0f);
        std::vector<float> out(quaternion_batch_size);
        for (auto& a : out) a = dist(rng);
        return out;
    }();
    return data;
}

} // namespace

static void bm_mathematics_sin_cos(benchmark::State& state) {
    const auto& d = angle_data();
    std::vector<float> out(static_cast<size_t>(quaternion_batch_size) * 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            float s = 0.0f, c = 0.0f;
            math::sin_cos(d[static_cast<size_t>(i)], s, c);
            out[static_cast<size_t>(i) * 2] = s;
            out[static_cast<size_t>(i) * 2 + 1] = c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_mathematics_sin_cos);

static void bm_std_lib_sin_cos(benchmark::State& state) {
    const auto& d = angle_data();
    std::vector<float> out(static_cast<size_t>(quaternion_batch_size) * 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            out[static_cast<size_t>(i) * 2] = std::sin(d[static_cast<size_t>(i)]);
            out[static_cast<size_t>(i) * 2 + 1] = std::cos(d[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_std_lib_sin_cos);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_sin_cos(benchmark::State& state) {
    const auto& d = angle_data();
    std::vector<float> out(static_cast<size_t>(quaternion_batch_size) * 2);
    for (auto _ : state) {
        // See the note on anchor(): without this the whole batch hoists out.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            float s = 0.0f, c = 0.0f;
            DirectX::XMScalarSinCos(&s, &c, d[static_cast<size_t>(i)]);
            out[static_cast<size_t>(i) * 2] = s;
            out[static_cast<size_t>(i) * 2 + 1] = c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_dx_math_sin_cos);
#endif

// ================================ audit gap: the gated operations with no bench
// docs/PLAN.md 4.2 names cross, transpose and a batch vertex transform among
// the operations that must stay within +-5% of DirectXMath. The Phase 5 audit
// found all three had never been measured -- a gate you cannot evaluate is not
// a gate. These close that.
namespace {
constexpr int stream_count = 512;

const std::vector<math::vector3>& stream_data() {
    static const std::vector<math::vector3> data = [] {
        std::mt19937 rng(random_seed + 7);
        std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
        std::vector<math::vector3> out(stream_count);
        for (auto& v : out) v = math::vector3{dist(rng), dist(rng), dist(rng)};
        return out;
    }();
    return data;
}
} // namespace

// cross with a unit axis has a four-state fixed cycle: X -> -Y -> -X -> Y.
// The old benchmark normalized after every cross, so it measured two APIs and
// compared Mathematics's packed vector3 round-trip with a register-resident XMVECTOR.
// This chain isolates cross and stays exact without normalization.
static void bm_mathematics_cross_latency(benchmark::State& state) {
    math::vector3 acc{1.0f, 0.0f, 0.0f};
    math::vector3 axis{0.0f, 0.0f, 1.0f};
    benchmark::DoNotOptimize(axis);
    for (auto _ : state) {
        acc = math::cross(acc, axis);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
    if (!(math::length(acc) > 0.5f && math::length(acc) < 2.0f)) {
        state.SkipWithError("accumulator drifted");
    }
}
BENCHMARK(bm_mathematics_cross_latency);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_cross_latency(benchmark::State& state) {
    DirectX::XMVECTOR acc = DirectX::XMVectorSet(1, 0, 0, 0);
    DirectX::XMVECTOR axis = DirectX::XMVectorSet(0, 0, 1, 0);
    benchmark::DoNotOptimize(axis);
    for (auto _ : state) {
        acc = DirectX::XMVector3Cross(acc, axis);
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(bm_dx_math_cross_latency);

// Fair storage-level comparison for Mathematics's packed vector3 API. The resident
// XMVECTOR result above remains useful as the register ceiling, but it does not
// pay the materialization cost that every vector3 return necessarily pays.
static void bm_dx_math_cross_latency_packed(benchmark::State& state) {
    DirectX::XMFLOAT3 acc{1.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 axis{0.0f, 0.0f, 1.0f};
    benchmark::DoNotOptimize(axis);
    for (auto _ : state) {
        DirectX::XMStoreFloat3(
            &acc, DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&acc),
                                          DirectX::XMLoadFloat3(&axis)));
        benchmark::DoNotOptimize(acc);
    }
    benchmark::ClobberMemory();
}
BENCHMARK(bm_dx_math_cross_latency_packed);
#endif

static void bm_mathematics_cross_throughput(benchmark::State& state) {
    const auto& d = stream_data();
    std::vector<math::vector3> out(stream_count / 2);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count / 2; ++i) {
            out[static_cast<size_t>(i)] =
                math::cross(d[static_cast<size_t>(i)],
                             d[static_cast<size_t>(i + stream_count / 2)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (stream_count / 2));
}
BENCHMARK(bm_mathematics_cross_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_cross_throughput(benchmark::State& state) {
    const auto& d = stream_data();
    std::vector<DirectX::XMFLOAT3> out(stream_count / 2);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count / 2; ++i) {
            const auto* a = reinterpret_cast<const DirectX::XMFLOAT3*>(
                &d[static_cast<size_t>(i)].x);
            const auto* b = reinterpret_cast<const DirectX::XMFLOAT3*>(
                &d[static_cast<size_t>(i + stream_count / 2)].x);
            DirectX::XMStoreFloat3(
                &out[static_cast<size_t>(i)],
                DirectX::XMVector3Cross(DirectX::XMLoadFloat3(a),
                                        DirectX::XMLoadFloat3(b)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (stream_count / 2));
}
BENCHMARK(bm_dx_math_cross_throughput);
#endif

static void bm_mathematics_matrix4x4_transpose(benchmark::State& state) {
    const auto& d = matrix_data();
    std::vector<math::matrix4x4> out(matrix_batch_size);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            out[static_cast<size_t>(i)] = transpose(d[static_cast<size_t>(i)]);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_mathematics_matrix4x4_transpose);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_matrix4x4_transpose(benchmark::State& state) {
    const auto& d = matrix_data();
    std::vector<DirectX::XMFLOAT4X4> out(matrix_batch_size);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            const auto* q = reinterpret_cast<const DirectX::XMFLOAT4X4*>(
                &d[static_cast<size_t>(i)].m[0][0]);
            DirectX::XMStoreFloat4x4(
                &out[static_cast<size_t>(i)],
                DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(q)));
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_dx_math_matrix4x4_transpose);
#endif

// The batch vertex transform, which is what a skinning or particle pass
// actually does. DirectXMath has a dedicated streaming entry point for this;
// Mathematics has no equivalent by design -- the loop IS the entry point -- so this
// measures whether that design costs anything.
static void bm_mathematics_transform_point_stream(benchmark::State& state) {
    const auto& d = stream_data();
    const math::matrix4x4 world = math::compose(
        math::vector3{1.5f, 1.5f, 1.5f},
        math::quaternion_from_axis_angle(math::vector3{0, 1, 0}, 0.7f),
        math::vector3{3, -4, 5});
    std::vector<math::vector3> out(stream_count);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count; ++i) {
            out[static_cast<size_t>(i)] =
                math::transform_point(d[static_cast<size_t>(i)], world);
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_mathematics_transform_point_stream);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_transform_coord_stream(benchmark::State& state) {
    const auto& d = stream_data();
    const math::matrix4x4 world = math::compose(
        math::vector3{1.5f, 1.5f, 1.5f},
        math::quaternion_from_axis_angle(math::vector3{0, 1, 0}, 0.7f),
        math::vector3{3, -4, 5});
    const DirectX::XMMATRIX xm = DirectX::XMLoadFloat4x4(
        reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world.m[0][0]));
    std::vector<DirectX::XMFLOAT3> out(stream_count);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        DirectX::XMVector3TransformCoordStream(
            out.data(), sizeof(DirectX::XMFLOAT3),
            reinterpret_cast<const DirectX::XMFLOAT3*>(d.data()),
            sizeof(math::vector3), stream_count, xm);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_dx_math_transform_coord_stream);
#endif

BENCHMARK_MAIN();
