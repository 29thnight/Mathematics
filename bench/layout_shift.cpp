// bench/layout_shift.cpp -- moves every function in the benchmark binary.
//
// docs/BASELINE.md 8 showed that shifting .text by sixteen bytes moves the
// throughput numbers of this family by several points, and docs/BASELINE.md 10
// that the hosted runners add their own spread on top. A sample drawn from one
// binary sees one layout; this file lets a sampling run draw several on purpose
// instead of by accident of whatever else changed in the commit.
//
// The padding is a function placed at the very start of .text, so every other
// function in the image moves by its size. Its body is sized to exactly the
// shift -- see hotpatch_prefix below for why that takes more than counting
// nops. The shift must be a multiple of sixteen: the next contribution is
// sixteen-byte aligned, so anything else rounds up.
//
// Getting it to the start took three attempts, and each wrong one looked right
// by some measure, so the reasons are worth keeping:
//
// - It has to be code. Zero bytes declared as data under a .text name come out
//   flagged as initialized data, the linker will not merge contributions whose
//   characteristics differ, and it emits them as a second .text section at the
//   end of the image. Nothing moves.
// - It has to be plain .text, not a grouped .text$a. The linker sorts grouped
//   sections by the suffix after '$', and the ungrouped name sorts ahead of
//   every suffix. MSVC puts code in .text$mn, so .text$a would do there; but
//   clang-cl puts code in plain .text, which then lands ahead of the padding,
//   and only the MSVC-compiled CRT behind it moves. The entry point lives in the
//   CRT, so an entry-point check passes while no benchmark has moved at all.
// - Contributions to the same section keep link order, so this file must be the
//   first object linked. bench/CMakeLists.txt lists it first for that reason.
//   Plain .text also sorts ahead of MSVC's .text$mn, so one placement serves
//   both compilers.
//
// Nothing calls the function; the /include keeps /OPT:REF from discarding it.
// Built only when the CMake cache variable of the same name is non-zero, and
// only for MSVC-style toolchains, whose pragma and intrinsic these are.

#if defined(_MSC_VER) && defined(MATHEMATICS_BENCH_TEXT_SHIFT) && MATHEMATICS_BENCH_TEXT_SHIFT > 0

#include <cstddef>
#include <intrin.h>
#include <utility>

namespace {

template <std::size_t... index>
__forceinline void emit_nops(std::index_sequence<index...>) noexcept {
    ((static_cast<void>(index), __nop()), ...);
}

// MSVC keeps every x64 function hot-patchable: when the first instruction is
// shorter than two bytes, it inserts a two-byte nop (66 90) ahead of it. It does
// not widen the first nop; it adds one. Unaccounted for, the body is two bytes
// over, the next contribution's sixteen-byte alignment rounds it up, and a
// requested shift of 16 lands at 32. The first two builds of this file did
// exactly that, one of them after a fix that assumed the widening. Read off
// the disassembly: 66 90, then every requested nop, then ret. Clang inserts
// nothing.
#if defined(__clang__)
constexpr std::size_t hotpatch_prefix = 0;
#else
constexpr std::size_t hotpatch_prefix = 2;
#endif

static_assert(MATHEMATICS_BENCH_TEXT_SHIFT % 16 == 0,
              "the shift must be a multiple of the sixteen-byte code alignment");

// prefix + nops + ret == the shift.
constexpr std::size_t nop_count = MATHEMATICS_BENCH_TEXT_SHIFT - hotpatch_prefix - 1;

} // namespace

#pragma code_seg(push, ".text")
extern "C" __declspec(noinline) void mathematics_bench_text_shift() noexcept {
    emit_nops(std::make_index_sequence<nop_count>{});
}
#pragma code_seg(pop)

#pragma comment(linker, "/include:mathematics_bench_text_shift")

#endif
