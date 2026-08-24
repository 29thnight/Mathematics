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

#include <mathf/vec_reg.hpp>

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

namespace {

constexpr int kBatch = 4096;
constexpr unsigned kSeed = 0x4D617468u;

std::vector<std::array<float, 4>> MakeData(int n) {
    std::mt19937 rng(kSeed);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
    std::vector<std::array<float, 4>> out(static_cast<size_t>(n));
    for (auto& v : out) v = {dist(rng), dist(rng), dist(rng), dist(rng)};
    return out;
}

const std::vector<std::array<float, 4>>& Data() {
    static const std::vector<std::array<float, 4>> data = MakeData(kBatch * 3);
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

// ========================================================== throughput: MulAdd
// Streams over arrays. This is where storage-type and ABI decisions show up --
// the register-resident latency benchmarks above cannot see them.

static void BM_Mathf_MulAdd_Throughput(benchmark::State& state) {
    const auto& d = Data();
    std::vector<std::array<float, 4>> out(kBatch);
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) {
            const mathf::VecReg a = mathf::Set(d[i][0], d[i][1], d[i][2], d[i][3]);
            const mathf::VecReg b = mathf::Set(d[i + kBatch][0], d[i + kBatch][1],
                                               d[i + kBatch][2], d[i + kBatch][3]);
            const mathf::VecReg c =
                mathf::Set(d[i + 2 * kBatch][0], d[i + 2 * kBatch][1],
                           d[i + 2 * kBatch][2], d[i + 2 * kBatch][3]);
            const mathf::VecReg r = mathf::MulAdd(a, b, c);
            out[static_cast<size_t>(i)] = {mathf::Lane(r, 0), mathf::Lane(r, 1),
                                           mathf::Lane(r, 2), mathf::Lane(r, 3)};
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
    std::vector<DirectX::XMFLOAT4> out(kBatch);
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) {
            const DirectX::XMVECTOR a =
                DirectX::XMVectorSet(d[i][0], d[i][1], d[i][2], d[i][3]);
            const DirectX::XMVECTOR b =
                DirectX::XMVectorSet(d[i + kBatch][0], d[i + kBatch][1],
                                     d[i + kBatch][2], d[i + kBatch][3]);
            const DirectX::XMVECTOR c =
                DirectX::XMVectorSet(d[i + 2 * kBatch][0], d[i + 2 * kBatch][1],
                                     d[i + 2 * kBatch][2], d[i + 2 * kBatch][3]);
            DirectX::XMStoreFloat4(&out[static_cast<size_t>(i)],
                                   DirectX::XMVectorMultiplyAdd(a, b, c));
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
            const glm::vec4 a(d[i][0], d[i][1], d[i][2], d[i][3]);
            const glm::vec4 b(d[i + kBatch][0], d[i + kBatch][1],
                              d[i + kBatch][2], d[i + kBatch][3]);
            const glm::vec4 c(d[i + 2 * kBatch][0], d[i + 2 * kBatch][1],
                              d[i + 2 * kBatch][2], d[i + 2 * kBatch][3]);
            out[static_cast<size_t>(i)] = a * b + c;
        }
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_GLM_MulAdd_Throughput);
#endif

BENCHMARK_MAIN();
