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

#include <mathematics/geometry.hpp>
#include <mathematics/matrix.hpp>
#include <mathematics/ranges.hpp>
#include <mathematics/transform.hpp>
#include <mathematics/vec_reg.hpp>
#include <mathematics/vector.hpp>
#include <mathematics/views.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <random>
#include <ranges>
#include <span>
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

// =========================================================== page-pinned arenas
// Every throughput benchmark reads a shared input stream and writes an output
// buffer, and left to the allocator the low twelve bits of those two addresses
// are whatever that process, that binary and that --benchmark_filter happened
// to produce. When a store's low twelve bits match an upcoming load's, the load
// is falsely held back behind the store -- 4K aliasing -- so that accident
// lands straight on the measurement.
//
// Phase 4 hit it in the inverse benchmark: one binary measured 47, 54 and
// 74 M/s in three processes, each internally consistent. A probe with a
// controlled offset pinned the cause -- 48.6 M/s at 64 bytes of separation,
// 58.1 at 16, a flat 79 from 128 up -- and inverse moved into an arena that
// fixes the separation at half a page. docs/BASELINE.md 5(1) records it. No
// other stream benchmark was moved.
//
// The quaternion product then paid the bill. Its instruction stream was byte
// for byte identical to the recorded baseline's, and relinking the same object
// file still swung the Mathematics-to-DirectXMath ratio across twenty-nine
// points -- far enough to park it on either side of the +-5% gate. Restricting
// --benchmark_filter moved it too, because that changes which output vectors
// get allocated and therefore where they land. So every throughput family now
// owns one of these arenas, and -- the part that makes an A/B comparison mean
// anything -- Mathematics's benchmark and each baseline's read and write the
// SAME addresses rather than two independently allocated buffers.
constexpr std::size_t page_bytes = 4096;

// Where a region starts relative to the page boundary that follows the previous
// one. Page-aligning all of them instead would put every pair at a relative
// offset of zero, which is the worst case rather than a neutral one.
//
// 1024 rather than the half page the inverse arena used, because several of
// these benchmarks read two halves of ONE input region -- d[i] against
// d[i + count/2] -- and the second half starts at whatever the half-size is
// modulo a page. The quaternion stream is exactly one page, so its second half
// sits at page offset 2048 and a half-page output stagger would have landed the
// stores on precisely the same offset as those loads: the collision the arena
// exists to prevent, rebuilt by hand. At 1024 the output clears both halves of
// every stream here -- quaternion (half page offset 2048), matrix4x4 (8192, so
// zero) and the packed vector3 streams (3072) -- by at least 1024 bytes, well
// inside the flat region the probe measured.
constexpr std::size_t region_stagger[4] = {0, 1024, 2048, 3072};

// A null source reserves an output region; anything else is copied in.
struct arena_region {
    const void* source;
    std::size_t bytes;
};

template <std::size_t region_count>
class stream_arena {
    static_assert(region_count >= 1 && region_count <= 4,
                  "region_stagger covers four regions");

public:
    explicit stream_arena(const std::array<arena_region, region_count>& regions) {
        std::size_t total = 0;
        for (const arena_region& region : regions) total += region.bytes;
        // A region can spend a page rounding up and 3 KiB more on stagger.
        storage_.resize(total + 2 * (region_count + 1) * page_bytes);

        std::uintptr_t cursor =
            page_up(reinterpret_cast<std::uintptr_t>(storage_.data()));
        for (std::size_t i = 0; i < region_count; ++i) {
            auto* const base =
                reinterpret_cast<unsigned char*>(cursor + region_stagger[i]);
            bases_[i] = base;
            // Filling the bytes is also what implicitly creates the objects the
            // benchmarks then read and write through as().
            if (regions[i].source != nullptr) {
                std::memcpy(base, regions[i].source, regions[i].bytes);
            } else {
                std::memset(base, 0, regions[i].bytes);
            }
            cursor = page_up(reinterpret_cast<std::uintptr_t>(base) +
                             regions[i].bytes);
        }
    }

    // The bases point into storage_, so an arena that moved would keep working
    // only by accident. Every one of these is a function-local static built in
    // place; nothing needs to copy one.
    stream_arena(const stream_arena&) = delete;
    stream_arena& operator=(const stream_arena&) = delete;

    template <class element>
    element* as(std::size_t index) const noexcept {
        return reinterpret_cast<element*>(bases_[index]);
    }

private:
    static std::uintptr_t page_up(std::uintptr_t address) noexcept {
        return (address + page_bytes - 1) & ~std::uintptr_t{page_bytes - 1};
    }

    std::vector<unsigned char> storage_;
    std::array<unsigned char*, region_count> bases_{};
};

template <class... region_specs>
stream_arena<sizeof...(region_specs)> make_arena(region_specs... specs) {
    return stream_arena<sizeof...(region_specs)>(
        std::array<arena_region, sizeof...(region_specs)>{specs...});
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
//
// Keep the value runtime-visible. With a constexpr 0.25f, clang proves that
// GLM's and Vectormath's scalar expressions are exactly `acc = acc` and removes
// the dot product, producing an impossible sub-nanosecond result. A volatile
// read outside the timed loop preserves the stable runtime value without
// exposing the identity to the optimizer.
const volatile float dot_stable_lane = 0.25f;

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

// Both Mathematics paths and both baselines stream through these two regions,
// so the four are measured at one fixed input-to-output offset.
stream_arena<2>& normalize_arena() {
    static auto instance = make_arena(
        arena_region{vector3_data().data(),
                     vector3_batch_size * sizeof(math::vector3)},
        arena_region{nullptr, vector3_batch_size * sizeof(math::vector3)});
    return instance;
}

#if MATHEMATICS_BENCH_HAS_DXMATH
static_assert(sizeof(DirectX::XMFLOAT3) == sizeof(math::vector3),
              "the baselines share the family's input and output regions");
#endif
#if MATHEMATICS_BENCH_HAS_GLM
static_assert(sizeof(glm::vec3) == sizeof(math::vector3),
              "the baselines share the family's input and output regions");
#endif

} // namespace

static void bm_mathematics_vector3_normalize_throughput(benchmark::State& state) {
    const auto& arena = normalize_arena();
    const auto* in = arena.as<const math::vector3>(0);
    auto* out = arena.as<math::vector3>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < vector3_batch_size; ++i) {
            out[i] = math::normalize(in[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * vector3_batch_size);
}
BENCHMARK(bm_mathematics_vector3_normalize_throughput);

static void bm_mathematics_vector3_normalize_unchecked_throughput(
    benchmark::State& state) {
    const auto& arena = normalize_arena();
    const auto* in = arena.as<const math::vector3>(0);
    auto* out = arena.as<math::vector3>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < vector3_batch_size; ++i) {
            out[i] = math::normalize_unchecked(in[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * vector3_batch_size);
}
BENCHMARK(bm_mathematics_vector3_normalize_unchecked_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_vector3_normalize_throughput(benchmark::State& state) {
    const auto& arena = normalize_arena();
    const auto* in = arena.as<const DirectX::XMFLOAT3>(0);
    auto* out = arena.as<DirectX::XMFLOAT3>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < vector3_batch_size; ++i) {
            DirectX::XMStoreFloat3(
                &out[i], DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&in[i])));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * vector3_batch_size);
}
BENCHMARK(bm_dx_math_vector3_normalize_throughput);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_vector3_normalize_throughput(benchmark::State& state) {
    const auto& arena = normalize_arena();
    const auto* in = arena.as<const glm::vec3>(0);
    auto* out = arena.as<glm::vec3>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < vector3_batch_size; ++i) {
            out[i] = glm::normalize(in[i]);
        }
        benchmark::DoNotOptimize(out);
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

// Each matrix family gets an arena; the note on stream_arena above says why an
// independently allocated output vector is not a neutral choice. Inverse was
// the first benchmark here to need one, and is the reason that note exists.
constexpr std::size_t matrix_stream_bytes =
    matrix_batch_size * sizeof(math::matrix4x4);

stream_arena<2>& matrix_multiply_arena() {
    static auto instance = make_arena(
        arena_region{matrix_data().data(), matrix_stream_bytes},
        arena_region{nullptr, matrix_stream_bytes / 2});
    return instance;
}

stream_arena<2>& matrix_inverse_arena() {
    static auto instance = make_arena(
        arena_region{matrix_data().data(), matrix_stream_bytes},
        arena_region{nullptr, matrix_stream_bytes});
    return instance;
}

stream_arena<2>& matrix_transpose_arena() {
    static auto instance = make_arena(
        arena_region{matrix_data().data(), matrix_stream_bytes},
        arena_region{nullptr, matrix_stream_bytes});
    return instance;
}

stream_arena<2>& decompose_arena() {
    static auto instance = make_arena(
        arena_region{matrix_data().data(), matrix_stream_bytes},
        arena_region{nullptr,
                     matrix_batch_size * sizeof(math::decomposed_transform)});
    return instance;
}

#if MATHEMATICS_BENCH_HAS_DXMATH
static_assert(sizeof(DirectX::XMFLOAT4X4) == sizeof(math::matrix4x4),
              "the baselines share the family's input and output regions");
#endif
#if MATHEMATICS_BENCH_HAS_GLM
static_assert(sizeof(glm::mat4) == sizeof(math::matrix4x4),
              "the baselines share the family's input and output regions");
#endif

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
    const auto& arena = matrix_multiply_arena();
    const auto* d = arena.as<const math::matrix4x4>(0);
    auto* out = arena.as<math::matrix4x4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size / 2; ++i) {
            out[i] = d[i] * d[i + matrix_batch_size / 2];
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (matrix_batch_size / 2));
}
BENCHMARK(bm_mathematics_matrix4x4_multiply_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_matrix4x4_multiply_throughput(benchmark::State& state) {
    const auto& arena = matrix_multiply_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT4X4>(0);
    auto* out = arena.as<DirectX::XMFLOAT4X4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size / 2; ++i) {
            DirectX::XMStoreFloat4x4(
                &out[i],
                DirectX::XMMatrixMultiply(
                    DirectX::XMLoadFloat4x4(&d[i]),
                    DirectX::XMLoadFloat4x4(&d[i + matrix_batch_size / 2])));
        }
        benchmark::DoNotOptimize(out);
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
    const auto& arena = matrix_multiply_arena();
    const auto* d = arena.as<const glm::mat4>(0);
    auto* out = arena.as<glm::mat4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size / 2; ++i) {
            out[i] = d[i] * d[i + matrix_batch_size / 2];
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (matrix_batch_size / 2));
}
BENCHMARK(bm_glm_matrix4x4_multiply_throughput);
#endif

static void bm_mathematics_matrix4x4_inverse(benchmark::State& state) {
    const auto& arena = matrix_inverse_arena();
    const auto* in = arena.as<const math::matrix4x4>(0);
    auto* out = arena.as<math::matrix4x4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
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

// Same arithmetic as inverse(), but failure is represented in the type rather
// than by an identity sentinel. This measures the optional construction and
// success check on the overwhelmingly common invertible path.
static void bm_cxx20_matrix4x4_try_inverse_optional(benchmark::State& state) {
    const auto& arena = matrix_inverse_arena();
    const auto* in = arena.as<const math::matrix4x4>(0);
    auto* out = arena.as<math::matrix4x4>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            const auto value = math::try_inverse(in[i]);
            if (value) out[i] = *value;
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_cxx20_matrix4x4_try_inverse_optional);

// The two forms are charged the same escape budget. This one used to call
// DoNotOptimize on its success flag once per ITEM while the optional form below
// escaped nothing per item, which taxed the out-parameter API for a barrier the
// other never paid. Summing the flag keeps every call's result observable and
// moves the escape to once per batch, where the optional form already has it.
static void bm_cxx20_decompose_out_parameters(benchmark::State& state) {
    const auto& arena = decompose_arena();
    const auto* in = arena.as<const math::matrix4x4>(0);
    auto* out = arena.as<math::decomposed_transform>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        unsigned succeeded = 0;
        for (int i = 0; i < matrix_batch_size; ++i) {
            auto& value = out[i];
            succeeded += static_cast<unsigned>(math::decompose(
                in[i], value.scale, value.rotation, value.translation));
        }
        benchmark::DoNotOptimize(succeeded);
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_cxx20_decompose_out_parameters);

static void bm_cxx20_decompose_optional(benchmark::State& state) {
    const auto& arena = decompose_arena();
    const auto* in = arena.as<const math::matrix4x4>(0);
    auto* out = arena.as<math::decomposed_transform>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            const auto value = math::decompose(in[i]);
            if (value) out[i] = *value;
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_cxx20_decompose_optional);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_matrix4x4_inverse(benchmark::State& state) {
    const auto& arena = matrix_inverse_arena();
    const auto* in = arena.as<const DirectX::XMFLOAT4X4>(0);
    auto* out = arena.as<DirectX::XMFLOAT4X4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
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
    const auto& arena = matrix_inverse_arena();
    const auto* in = arena.as<const glm::mat4>(0);
    auto* out = arena.as<glm::mat4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
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
namespace {

// Region 0 holds all three operand streams exactly as data() lays them out, so
// their relative offsets are unchanged and only the output moves.
stream_arena<2>& mul_add_arena() {
    static auto instance = make_arena(
        arena_region{data().data(), 3 * batch_size * sizeof(float4)},
        arena_region{nullptr, batch_size * sizeof(float4)});
    return instance;
}

#if MATHEMATICS_BENCH_HAS_DXMATH
static_assert(sizeof(DirectX::XMFLOAT4A) == sizeof(float4),
              "the baselines share the family's input and output regions");
#endif
#if MATHEMATICS_BENCH_HAS_GLM
static_assert(sizeof(glm::vec4) == sizeof(float4),
              "the baselines share the family's input and output regions");
#endif
#if MATHEMATICS_BENCH_HAS_VECTORMATH
static_assert(sizeof(Vectormath::SSE::Vector4) == sizeof(float4),
              "the baselines share the family's input and output regions");
#endif

} // namespace

static void bm_mathematics_mul_add_throughput(benchmark::State& state) {
    const auto& arena = mul_add_arena();
    const auto* d = arena.as<const float4>(0);
    auto* out = arena.as<float4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < batch_size; ++i) {
            const math::vec_reg a = math::load_aligned(d[i].v);
            const math::vec_reg b = math::load_aligned(d[i + batch_size].v);
            const math::vec_reg c = math::load_aligned(d[i + 2 * batch_size].v);
            math::store_aligned(out[i].v, math::mul_add(a, b, c));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(bm_mathematics_mul_add_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_mul_add_throughput(benchmark::State& state) {
    const auto& arena = mul_add_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT4A>(0);
    auto* out = arena.as<DirectX::XMFLOAT4A>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < batch_size; ++i) {
            DirectX::XMStoreFloat4A(
                &out[i],
                DirectX::XMVectorMultiplyAdd(
                    DirectX::XMLoadFloat4A(&d[i]),
                    DirectX::XMLoadFloat4A(&d[i + batch_size]),
                    DirectX::XMLoadFloat4A(&d[i + 2 * batch_size])));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(bm_dx_math_mul_add_throughput);
#endif

#if MATHEMATICS_BENCH_HAS_GLM
static void bm_glm_mul_add_throughput(benchmark::State& state) {
    const auto& arena = mul_add_arena();
    const auto* d = arena.as<const glm::vec4>(0);
    auto* out = arena.as<glm::vec4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < batch_size; ++i) {
            out[i] = d[i] * d[i + batch_size] + d[i + 2 * batch_size];
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(bm_glm_mul_add_throughput);
#endif

#if MATHEMATICS_BENCH_HAS_VECTORMATH
static void bm_vectormath_mul_add_throughput(benchmark::State& state) {
    const auto& arena = mul_add_arena();
    const auto* d = arena.as<const float4>(0);
    auto* out = arena.as<Vectormath::SSE::Vector4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < batch_size; ++i) {
            // Vectormath exposes no load helper for vector4, but it is an
            // SSE-native type with a __m128 constructor, so this is the
            // idiomatic aligned load for it.
            const Vectormath::SSE::Vector4 a(_mm_load_ps(d[i].v));
            const Vectormath::SSE::Vector4 b(_mm_load_ps(d[i + batch_size].v));
            const Vectormath::SSE::Vector4 c(_mm_load_ps(d[i + 2 * batch_size].v));
            out[i] = Vectormath::SSE::mulPerElem(a, b) + c;
        }
        benchmark::DoNotOptimize(out);
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

// One arena per quaternion family. Every one of these pairs Mathematics against
// DirectXMath, and the pair only means something if both sides stream through
// the same addresses -- see the note on stream_arena.
constexpr std::size_t quaternion_stream_bytes =
    quaternion_batch_size * sizeof(math::quaternion);

stream_arena<2>& quaternion_multiply_arena() {
    static auto instance = make_arena(
        arena_region{quaternion_data().data(), quaternion_stream_bytes},
        arena_region{nullptr, quaternion_stream_bytes / 2});
    return instance;
}

stream_arena<2>& quaternion_slerp_arena() {
    static auto instance = make_arena(
        arena_region{quaternion_data().data(), quaternion_stream_bytes},
        arena_region{nullptr, quaternion_stream_bytes / 2});
    return instance;
}

// Two input streams here: the rotations and the points they are applied to.
stream_arena<3>& quaternion_rotate_arena() {
    static auto instance = make_arena(
        arena_region{quaternion_data().data(), quaternion_stream_bytes},
        arena_region{data().data(), quaternion_batch_size * sizeof(float4)},
        arena_region{nullptr, quaternion_batch_size * sizeof(math::vector3)});
    return instance;
}

stream_arena<2>& quaternion_to_matrix_arena() {
    static auto instance = make_arena(
        arena_region{quaternion_data().data(), quaternion_stream_bytes},
        arena_region{nullptr,
                     quaternion_batch_size * sizeof(math::matrix4x4)});
    return instance;
}

stream_arena<2>& transform_compose_arena() {
    static auto instance = make_arena(
        arena_region{quaternion_data().data(), quaternion_stream_bytes},
        arena_region{nullptr,
                     quaternion_batch_size * sizeof(math::matrix4x4)});
    return instance;
}

#if MATHEMATICS_BENCH_HAS_DXMATH
static_assert(sizeof(DirectX::XMFLOAT4) == sizeof(math::quaternion),
              "the baselines share the family's input and output regions");
#endif

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
    const auto& arena = quaternion_multiply_arena();
    const auto* d = arena.as<const math::quaternion>(0);
    auto* out = arena.as<math::quaternion>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size / 2; ++i) {
            out[i] = d[i] * d[i + quaternion_batch_size / 2];
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (quaternion_batch_size / 2));
}
BENCHMARK(bm_mathematics_quaternion_multiply_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_multiply_throughput(benchmark::State& state) {
    const auto& arena = quaternion_multiply_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT4>(0);
    auto* out = arena.as<DirectX::XMFLOAT4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size / 2; ++i) {
            DirectX::XMStoreFloat4(
                &out[i],
                DirectX::XMQuaternionMultiply(
                    DirectX::XMLoadFloat4(&d[i]),
                    DirectX::XMLoadFloat4(&d[i + quaternion_batch_size / 2])));
        }
        benchmark::DoNotOptimize(out);
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
// as a win if nobody checks it. The usual `DoNotOptimize(out)` after the
// loop was not enough: it escapes the pointer, not what was written through it,
// so the stores stayed dead. Making `t` opaque was not enough either.
// DoNotOptimize on each element is, and it costs both sides the same.
static void bm_mathematics_quaternion_slerp(benchmark::State& state) {
    const auto& arena = quaternion_slerp_arena();
    const auto* d = arena.as<const math::quaternion>(0);
    auto* out = arena.as<math::quaternion>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        float t = 0.37f;
        benchmark::DoNotOptimize(t);
        for (int i = 0; i < quaternion_batch_size / 2; ++i) {
            out[i] = math::slerp(d[i], d[i + quaternion_batch_size / 2], t);
            benchmark::DoNotOptimize(out[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (quaternion_batch_size / 2));
}
BENCHMARK(bm_mathematics_quaternion_slerp);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_slerp(benchmark::State& state) {
    const auto& arena = quaternion_slerp_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT4>(0);
    auto* out = arena.as<DirectX::XMFLOAT4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        float t = 0.37f;
        benchmark::DoNotOptimize(t);
        for (int i = 0; i < quaternion_batch_size / 2; ++i) {
            DirectX::XMStoreFloat4(
                &out[i],
                DirectX::XMQuaternionSlerp(
                    DirectX::XMLoadFloat4(&d[i]),
                    DirectX::XMLoadFloat4(&d[i + quaternion_batch_size / 2]), t));
            benchmark::DoNotOptimize(out[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (quaternion_batch_size / 2));
}
BENCHMARK(bm_dx_math_quaternion_slerp);
#endif

// Rotating a vector by a quaternion, which is what a skinning or particle
// system actually spends its time on.
static void bm_mathematics_quaternion_rotate_vector(benchmark::State& state) {
    const auto& arena = quaternion_rotate_arena();
    const auto* d = arena.as<const math::quaternion>(0);
    const auto* v = arena.as<const float4>(1);
    auto* out = arena.as<math::vector3>(2);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            const math::vector3 p{v[i].v[0], v[i].v[1], v[i].v[2]};
            out[i] = math::rotate(p, d[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_mathematics_quaternion_rotate_vector);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_rotate_vector(benchmark::State& state) {
    const auto& arena = quaternion_rotate_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT4>(0);
    const auto* v = arena.as<const float4>(1);
    auto* out = arena.as<DirectX::XMFLOAT3>(2);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            const DirectX::XMVECTOR p =
                DirectX::XMVectorSet(v[i].v[0], v[i].v[1], v[i].v[2], 0.0f);
            DirectX::XMStoreFloat3(
                &out[i], DirectX::XMVector3Rotate(p, DirectX::XMLoadFloat4(&d[i])));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_dx_math_quaternion_rotate_vector);
#endif

// quaternion to matrix -- once per object per frame in any scene graph.
static void bm_mathematics_quaternion_to_matrix(benchmark::State& state) {
    const auto& arena = quaternion_to_matrix_arena();
    const auto* d = arena.as<const math::quaternion>(0);
    auto* out = arena.as<math::matrix4x4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            out[i] = math::rotation_matrix(d[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_mathematics_quaternion_to_matrix);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_quaternion_to_matrix(benchmark::State& state) {
    const auto& arena = quaternion_to_matrix_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT4>(0);
    auto* out = arena.as<DirectX::XMFLOAT4X4>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            DirectX::XMStoreFloat4x4(
                &out[i], DirectX::XMMatrixRotationQuaternion(
                             DirectX::XMLoadFloat4(&d[i])));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_dx_math_quaternion_to_matrix);
#endif

// The full TRS build, which is the per-object cost in a scene graph.
static void bm_mathematics_transform_compose(benchmark::State& state) {
    const auto& arena = transform_compose_arena();
    const auto* d = arena.as<const math::quaternion>(0);
    auto* out = arena.as<math::matrix4x4>(1);
    const math::vector3 scale{1.5f, 2.0f, 0.75f};
    const math::vector3 translation{3.0f, -4.0f, 5.0f};
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            out[i] = math::compose(scale, d[i], translation);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_mathematics_transform_compose);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_transform_compose(benchmark::State& state) {
    const auto& arena = transform_compose_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT4>(0);
    auto* out = arena.as<DirectX::XMFLOAT4X4>(1);
    const DirectX::XMVECTOR scale = DirectX::XMVectorSet(1.5f, 2.0f, 0.75f, 0.0f);
    const DirectX::XMVECTOR translation =
        DirectX::XMVectorSet(3.0f, -4.0f, 5.0f, 0.0f);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            DirectX::XMStoreFloat4x4(
                &out[i],
                DirectX::XMMatrixAffineTransformation(
                    scale, DirectX::XMVectorZero(),
                    DirectX::XMLoadFloat4(&d[i]), translation));
        }
        benchmark::DoNotOptimize(out);
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

// Sine and cosine interleaved into one output stream, so the region is two
// floats per angle.
stream_arena<2>& sin_cos_arena() {
    static auto instance = make_arena(
        arena_region{angle_data().data(), quaternion_batch_size * sizeof(float)},
        arena_region{nullptr, 2 * quaternion_batch_size * sizeof(float)});
    return instance;
}

} // namespace

static void bm_mathematics_sin_cos(benchmark::State& state) {
    const auto& arena = sin_cos_arena();
    const auto* d = arena.as<const float>(0);
    auto* out = arena.as<float>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            float s = 0.0f, c = 0.0f;
            math::sin_cos(d[i], s, c);
            out[i * 2] = s;
            out[i * 2 + 1] = c;
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_mathematics_sin_cos);

static void bm_std_lib_sin_cos(benchmark::State& state) {
    const auto& arena = sin_cos_arena();
    const auto* d = arena.as<const float>(0);
    auto* out = arena.as<float>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            out[i * 2] = std::sin(d[i]);
            out[i * 2 + 1] = std::cos(d[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * quaternion_batch_size);
}
BENCHMARK(bm_std_lib_sin_cos);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_sin_cos(benchmark::State& state) {
    const auto& arena = sin_cos_arena();
    const auto* d = arena.as<const float>(0);
    auto* out = arena.as<float>(1);
    for (auto _ : state) {
        // The input is loop-invariant; without this barrier the compiler is
        // free to hoist the whole batch out of the timing loop.
        benchmark::ClobberMemory();
        for (int i = 0; i < quaternion_batch_size; ++i) {
            float s = 0.0f, c = 0.0f;
            DirectX::XMScalarSinCos(&s, &c, d[i]);
            out[i * 2] = s;
            out[i * 2 + 1] = c;
        }
        benchmark::DoNotOptimize(out);
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

const std::vector<math::ray>& ray_data() {
    static const std::vector<math::ray> rays = [] {
        std::vector<math::ray> out(stream_count);
        const auto& points = stream_data();
        for (int i = 0; i < stream_count; ++i) {
            const math::vector3 origin =
                points[static_cast<size_t>(i)] * 2.0f + math::vector3{0, 0, -30};
            out[static_cast<size_t>(i)] =
                math::ray{origin, math::normalize(-origin)};
        }
        return out;
    }();
    return rays;
}

constexpr std::size_t point_stream_bytes = stream_count * sizeof(math::vector3);

// The five aabb benchmarks all read region 0, so the pointer, span and range
// overloads are compared over one address rather than three allocations.
stream_arena<2>& aabb_arena() {
    static auto instance = make_arena(
        arena_region{stream_data().data(), point_stream_bytes},
        arena_region{nullptr, (stream_count / 2) * sizeof(math::aabb)});
    return instance;
}

stream_arena<2>& raycast_arena() {
    static auto instance = make_arena(
        arena_region{ray_data().data(), stream_count * sizeof(math::ray)},
        arena_region{nullptr, stream_count * sizeof(float)});
    return instance;
}

stream_arena<2>& cross_arena() {
    static auto instance = make_arena(
        arena_region{stream_data().data(), point_stream_bytes},
        arena_region{nullptr, (stream_count / 2) * sizeof(math::vector3)});
    return instance;
}

stream_arena<2>& transform_point_arena() {
    static auto instance = make_arena(
        arena_region{stream_data().data(), point_stream_bytes},
        arena_region{nullptr, point_stream_bytes});
    return instance;
}
} // namespace

// ========================================= C++20 API cost and safety tradeoffs
// Pointer/count and span deliberately keep independent loops so a standard
// library change cannot silently alter the established engine hot path.
static void bm_cxx20_aabb_from_points_pointer(benchmark::State& state) {
    const auto* points = aabb_arena().as<const math::vector3>(0);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        // The count is made opaque deliberately. Passing the constant straight
        // through handed this overload -- and only this one -- a compile-time
        // trip count, so what it was being compared against was a differently
        // compiled loop rather than a different API. The span and range
        // overloads carry their length in an object the barrier forces a
        // reload of; this gives the pointer form the same footing.
        int count = stream_count;
        benchmark::DoNotOptimize(count);
        math::aabb box = math::aabb_from_points(points, count);
        benchmark::DoNotOptimize(box);
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_cxx20_aabb_from_points_pointer);

static void bm_cxx20_aabb_from_points_span(benchmark::State& state) {
    const std::span<const math::vector3> view{
        aabb_arena().as<const math::vector3>(0),
        static_cast<std::size_t>(stream_count)};
    for (auto _ : state) {
        benchmark::ClobberMemory();
        math::aabb box = math::aabb_from_points(view);
        benchmark::DoNotOptimize(box);
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_cxx20_aabb_from_points_span);

static void bm_cxx20_aabb_from_points_range(benchmark::State& state) {
    auto view = std::span<const math::vector3>{
                    aabb_arena().as<const math::vector3>(0),
                    static_cast<std::size_t>(stream_count)} |
                std::views::transform([](const math::vector3& point) {
                    return point;
                });
    for (auto _ : state) {
        benchmark::ClobberMemory();
        math::aabb box = math::aabb_from_points(view);
        benchmark::DoNotOptimize(box);
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_cxx20_aabb_from_points_range);

static math::aabb legacy_aabb_from_min_max(const math::vector3& minimum,
                                           const math::vector3& maximum) noexcept {
    return math::aabb{(minimum + maximum) * 0.5f,
                      (maximum - minimum) * 0.5f};
}

static void bm_cxx20_aabb_legacy_arithmetic(benchmark::State& state) {
    const auto& arena = aabb_arena();
    const auto* points = arena.as<const math::vector3>(0);
    auto* out = arena.as<math::aabb>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count / 2; ++i) {
            const auto& a = points[i];
            const auto& b = points[i + stream_count / 2];
            out[i] = legacy_aabb_from_min_max(math::min(a, b), math::max(a, b));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (stream_count / 2));
}
BENCHMARK(bm_cxx20_aabb_legacy_arithmetic);

static void bm_cxx20_aabb_midpoint(benchmark::State& state) {
    const auto& arena = aabb_arena();
    const auto* points = arena.as<const math::vector3>(0);
    auto* out = arena.as<math::aabb>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count / 2; ++i) {
            const auto& a = points[i];
            const auto& b = points[i + stream_count / 2];
            out[i] = math::aabb::from_min_max(math::min(a, b), math::max(a, b));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (stream_count / 2));
}
BENCHMARK(bm_cxx20_aabb_midpoint);

static void bm_cxx20_raycast_out_parameter(benchmark::State& state) {
    const auto& arena = raycast_arena();
    const auto* rays = arena.as<const math::ray>(0);
    auto* out = arena.as<float>(1);
    const math::sphere target{math::vector3{}, 5.0f};
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count; ++i) {
            float distance = -1.0f;
            const bool hit = math::raycast(rays[i], target, distance);
            out[i] = hit ? distance : -1.0f;
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_cxx20_raycast_out_parameter);

static void bm_cxx20_raycast_optional(benchmark::State& state) {
    const auto& arena = raycast_arena();
    const auto* rays = arena.as<const math::ray>(0);
    auto* out = arena.as<float>(1);
    const math::sphere target{math::vector3{}, 5.0f};
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count; ++i) {
            out[i] = math::raycast(rays[i], target).value_or(-1.0f);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_cxx20_raycast_optional);

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
    const auto& arena = cross_arena();
    const auto* d = arena.as<const math::vector3>(0);
    auto* out = arena.as<math::vector3>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count / 2; ++i) {
            out[i] = math::cross(d[i], d[i + stream_count / 2]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (stream_count / 2));
}
BENCHMARK(bm_mathematics_cross_throughput);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_cross_throughput(benchmark::State& state) {
    const auto& arena = cross_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT3>(0);
    auto* out = arena.as<DirectX::XMFLOAT3>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count / 2; ++i) {
            DirectX::XMStoreFloat3(
                &out[i],
                DirectX::XMVector3Cross(
                    DirectX::XMLoadFloat3(&d[i]),
                    DirectX::XMLoadFloat3(&d[i + stream_count / 2])));
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * (stream_count / 2));
}
BENCHMARK(bm_dx_math_cross_throughput);
#endif

static void bm_mathematics_matrix4x4_transpose(benchmark::State& state) {
    const auto& arena = matrix_transpose_arena();
    const auto* d = arena.as<const math::matrix4x4>(0);
    auto* out = arena.as<math::matrix4x4>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            out[i] = transpose(d[i]);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * matrix_batch_size);
}
BENCHMARK(bm_mathematics_matrix4x4_transpose);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_matrix4x4_transpose(benchmark::State& state) {
    const auto& arena = matrix_transpose_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT4X4>(0);
    auto* out = arena.as<DirectX::XMFLOAT4X4>(1);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < matrix_batch_size; ++i) {
            DirectX::XMStoreFloat4x4(
                &out[i],
                DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&d[i])));
        }
        benchmark::DoNotOptimize(out);
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
    const auto& arena = transform_point_arena();
    const auto* d = arena.as<const math::vector3>(0);
    auto* out = arena.as<math::vector3>(1);
    const math::matrix4x4 world = math::compose(
        math::vector3{1.5f, 1.5f, 1.5f},
        math::quaternion_from_axis_angle(math::vector3{0, 1, 0}, 0.7f),
        math::vector3{3, -4, 5});
    for (auto _ : state) {
        benchmark::ClobberMemory();
        for (int i = 0; i < stream_count; ++i) {
            out[i] = math::transform_point(d[i], world);
        }
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_mathematics_transform_point_stream);

#if MATHEMATICS_BENCH_HAS_DXMATH
static void bm_dx_math_transform_coord_stream(benchmark::State& state) {
    const auto& arena = transform_point_arena();
    const auto* d = arena.as<const DirectX::XMFLOAT3>(0);
    auto* out = arena.as<DirectX::XMFLOAT3>(1);
    const math::matrix4x4 world = math::compose(
        math::vector3{1.5f, 1.5f, 1.5f},
        math::quaternion_from_axis_angle(math::vector3{0, 1, 0}, 0.7f),
        math::vector3{3, -4, 5});
    const DirectX::XMMATRIX xm = DirectX::XMLoadFloat4x4(
        reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world.m[0][0]));
    for (auto _ : state) {
        benchmark::ClobberMemory();
        DirectX::XMVector3TransformCoordStream(
            out, sizeof(DirectX::XMFLOAT3), d, sizeof(math::vector3),
            stream_count, xm);
        benchmark::DoNotOptimize(out);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * stream_count);
}
BENCHMARK(bm_dx_math_transform_coord_stream);
#endif

// =========================================== fixed-extent range terminal cost
// Tiny-range timings complement spike/codegen_library.cpp. DoNotOptimize keeps
// each input and result observable without adding work inside the operation.
//
// READ THE ASSEMBLY, NOT THESE NUMBERS. The loops here are one to three
// instructions long, and at that size instruction-fetch alignment decides the
// result. Shifting .text by sixteen bytes and relinking -- same object file,
// same instruction bytes -- moves a 2x penalty from direct_transform onto the
// view, fixed and pipeline variants and back off again:
//
//   .text shift | direct | view  | fixed | pipeline
//   +0          | 0.481  | 0.241 | 0.238 | 0.245 ns
//   +16         | 0.264  | 0.266 | 0.249 | 0.248 ns
//   +32         | 0.266  | 0.499 | 0.504 | 0.494 ns
//
// The four compile to the SAME loop body -- one vmovaps, one vfmadd132ps, one
// vmovaps -- so a gap between them is the linker talking, never the abstraction.
// That is the finding this family exists to report, and it survives relinking;
// the individual timings do not.
//
// The barrier at the top of each iteration makes the no-hoisting requirement
// explicit instead of leaving it to DoNotOptimize, whose implementations happen
// to carry a memory clobber of their own. It is free: adding it left all
// nineteen of these functions byte-identical under both clang-cl and MSVC.
// The for_each benchmarks do not carry it and do not need it either -- they
// mutate `value` and escape it inside the loop, so each iteration already
// depends on the last.
static void bm_fixed_ranges_components_direct_sum(benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        const float result = value.x + value.y + value.z + value.w;
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_components_direct_sum);

static void bm_fixed_ranges_components_view_sum(benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        float result = 0.0f;
        for (const float component : math::components(value)) result += component;
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_components_view_sum);

static void bm_fixed_ranges_components_fold(benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        const float result = math::ranges::fold_fixed(
            math::components(value), 0.0f, std::plus<>{});
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_components_fold);

static void bm_fixed_ranges_components_pipeline_fold(benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        const float result =
            math::components(value) |
            math::views::transform_fixed(
                [](float component) { return component; }) |
            math::ranges::fold_fixed(0.0f, std::plus<>{});
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_components_pipeline_fold);

static void bm_fixed_ranges_components_direct_square_sum(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        const float result = value.x * value.x + value.y * value.y +
                             value.z * value.z + value.w * value.w;
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_components_direct_square_sum);

static void bm_fixed_ranges_components_pipeline_square_sum(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        const float result =
            math::components(value) |
            math::views::transform_fixed(
                [](float component) { return component * component; }) |
            math::ranges::fold_fixed(0.0f, std::plus<>{});
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_components_pipeline_square_sum);

static void bm_fixed_ranges_components_lazy_square_range_for(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        float result = 0.0f;
        for (const float component :
             math::components(value) |
                 math::views::transform_fixed([](float element) {
                     return element * element;
                 })) {
            result += component;
        }
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_components_lazy_square_range_for);

static void bm_fixed_ranges_components_lazy_range_for(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        float result = 0.0f;
        for (const float component :
             math::components(value) |
                 math::views::transform_fixed(
                     [](float element) { return element; })) {
            result += component;
        }
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_components_lazy_range_for);

static void bm_fixed_ranges_components_direct_for_each(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    for (auto _ : state) {
        value.x = value.x * 0.5f + 0.5f;
        value.y = value.y * 0.5f + 0.5f;
        value.z = value.z * 0.5f + 0.5f;
        value.w = value.w * 0.5f + 0.5f;
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK(bm_fixed_ranges_components_direct_for_each);

static void bm_fixed_ranges_components_view_for_each(benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    for (auto _ : state) {
        for (float& component : math::components(value)) {
            component = component * 0.5f + 0.5f;
        }
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK(bm_fixed_ranges_components_view_for_each);

static void bm_fixed_ranges_components_fixed_for_each(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    for (auto _ : state) {
        static_cast<void>(math::ranges::for_each_fixed(
            math::components(value), [](float& component) {
                component = component * 0.5f + 0.5f;
            }));
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK(bm_fixed_ranges_components_fixed_for_each);

static void bm_fixed_ranges_components_pipeline_for_each(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    for (auto _ : state) {
        static_cast<void>(
            math::components(value) |
            math::ranges::for_each_fixed([](float& component) {
                component = component * 0.5f + 0.5f;
            }));
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK(bm_fixed_ranges_components_pipeline_for_each);

static void bm_fixed_ranges_components_direct_transform(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    std::array<float, 4> output{};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        output[0] = value.x * 2.0f + 1.0f;
        output[1] = value.y * 2.0f + 1.0f;
        output[2] = value.z * 2.0f + 1.0f;
        output[3] = value.w * 2.0f + 1.0f;
        benchmark::DoNotOptimize(output);
    }
}
BENCHMARK(bm_fixed_ranges_components_direct_transform);

static void bm_fixed_ranges_components_view_transform(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    std::array<float, 4> output{};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        auto destination = output.begin();
        for (const float component : math::components(value)) {
            *destination++ = component * 2.0f + 1.0f;
        }
        benchmark::DoNotOptimize(output);
    }
}
BENCHMARK(bm_fixed_ranges_components_view_transform);

static void bm_fixed_ranges_components_fixed_transform(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    std::array<float, 4> output{};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        static_cast<void>(math::ranges::transform_fixed(
            math::components(value), output.begin(),
            [](float component) { return component * 2.0f + 1.0f; }));
        benchmark::DoNotOptimize(output);
    }
}
BENCHMARK(bm_fixed_ranges_components_fixed_transform);

static void bm_fixed_ranges_components_pipeline_transform(
    benchmark::State& state) {
    math::vector4 value{1.25f, -2.5f, 3.75f, 4.5f};
    std::array<float, 4> output{};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        static_cast<void>(
            math::components(value) |
            math::ranges::transform_fixed_to(
                output.begin(), [](float component) {
                    return component * 2.0f + 1.0f;
                }));
        benchmark::DoNotOptimize(output);
    }
}
BENCHMARK(bm_fixed_ranges_components_pipeline_transform);

static void bm_fixed_ranges_rows_direct_sum(benchmark::State& state) {
    math::matrix4x4 value{1, 2, 3, 4,
                          5, 6, 7, 8,
                          9, 10, 11, 12,
                          13, 14, 15, 16};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        float result = 0.0f;
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                result += value.m[row][column];
            }
        }
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_rows_direct_sum);

static void bm_fixed_ranges_rows_view_sum(benchmark::State& state) {
    math::matrix4x4 value{1, 2, 3, 4,
                          5, 6, 7, 8,
                          9, 10, 11, 12,
                          13, 14, 15, 16};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        float result = 0.0f;
        for (const auto row : math::rows(value)) {
            for (const float element : row) result += element;
        }
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_rows_view_sum);

static void bm_fixed_ranges_rows_fold(benchmark::State& state) {
    math::matrix4x4 value{1, 2, 3, 4,
                          5, 6, 7, 8,
                          9, 10, 11, 12,
                          13, 14, 15, 16};
    benchmark::DoNotOptimize(value);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        const float result = math::ranges::fold_fixed(
            math::rows(value), 0.0f,
            [](float accumulated, std::span<const float, 4> row) {
                return math::ranges::fold_fixed(
                    row, accumulated, std::plus<>{});
            });
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(bm_fixed_ranges_rows_fold);

BENCHMARK_MAIN();
