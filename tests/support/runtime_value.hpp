#ifndef MATHEMATICS_TESTS_RUNTIME_VALUE_HPP
#define MATHEMATICS_TESTS_RUNTIME_VALUE_HPP

#if defined(_MSC_VER) && !defined(__clang__)
#  define MATHEMATICS_TEST_NOINLINE __declspec(noinline)
#else
#  define MATHEMATICS_TEST_NOINLINE __attribute__((noinline))
#endif

namespace math_test {

// Hands a value back through a function the compiler cannot see into.
//
// GCC evaluates an initializer at compile time whenever every input is a
// constant, even at -O0: `const auto p = orthographic_off_center_lh(-1, 7, ...)`
// never calls the compiled function, so the test checks the compiler's
// constant evaluator rather than the code a user runs, and gcov reports the
// function as never executed. One argument routed through here is enough to
// make the whole call a run-time call.
//
// Not being constexpr covers the constant evaluator; noinline covers the
// optimizer. Without it an /O2 build inlines this, propagates the constant
// anyway, and folds what the test meant to run -- MSVC then warns C4756 about
// an overflow the test is deliberately exercising, and the compiled path is
// still never taken.
template <typename value_type>
MATHEMATICS_TEST_NOINLINE value_type runtime_value(const value_type& value) {
    return value;
}

} // namespace math_test

#endif // MATHEMATICS_TESTS_RUNTIME_VALUE_HPP
