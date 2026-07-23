// Copyright 2026 Sebastiano Merlino
/*
 * Pin the LT_SKIP / LT_SKIP_IF semantics added to
 * test/littletest.hpp.
 *
 * Previously the project simulated a "skip" with `LT_CHECK_EQ(1, 1)`
 * inside a try/catch: a build that silently lost TLS support would
 * report PASS rather than SKIP. The replacement primitive must:
 *
 *   1. Increment a runner-side skip counter on each invocation.
 *   2. Halt the enclosing test body (matches assert_unattended semantics
 *      so subsequent assertions can't fire spurious failures after a
 *      skip).
 *   3. Be a no-op when LT_SKIP_IF's predicate is false.
 *
 * This TU is a runtime sentinel; the suite-level exit-code 77 semantics
 * are pinned by scripts/test_check_littletest_skip_exit_code.sh.
 */
#include "./littletest.hpp"

LT_BEGIN_SUITE(littletest_skip_semantics_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(littletest_skip_semantics_suite)

// LT_SKIP increments the runner's skip counter and halts
// the enclosing test body. LT_SKIP throws skip_unattended so no code
// after it can execute; the counter-delta assertion is therefore
// delegated to `skip_counter_nonzero_after_skip_runs` below, which
// reads the cumulative skip total in a fresh test body after this one
// has run.
LT_BEGIN_AUTO_TEST(littletest_skip_semantics_suite, skip_macro_increments_skip_counter)
    LT_SKIP("intentional skip — Cycle 1 sentinel");
LT_END_AUTO_TEST(skip_macro_increments_skip_counter)

// LT_SKIP_IF(false, ...) is a no-op; control flows through.
LT_BEGIN_AUTO_TEST(littletest_skip_semantics_suite, skip_if_false_does_not_skip)
    int before = littletest::auto_test_runner.get_skips();
    LT_SKIP_IF(false, "should not fire");
    int after = littletest::auto_test_runner.get_skips();
    LT_CHECK_EQ(after, before);
LT_END_AUTO_TEST(skip_if_false_does_not_skip)

// LT_SKIP_IF(true, ...) increments the counter (delegates
// to LT_SKIP) and halts the enclosing test body, same as LT_SKIP.
LT_BEGIN_AUTO_TEST(littletest_skip_semantics_suite, skip_if_true_delegates_to_skip)
    LT_SKIP_IF(true, "intentional — Cycle 1.c direct exercise");
LT_END_AUTO_TEST(skip_if_true_delegates_to_skip)

// We measure the post-state of the above by
// reading get_skips() in a fresh sibling test that observes the
// cumulative counter set by the prior tests' skips.
//
// Ordering guarantee: LT_BEGIN_AUTO_TEST registers each test into
// littletest::auto_test_vector via push_back() in source-declaration
// order (static initialization order within a translation unit is
// well-defined), and AUTORUN_TESTS() iterates that vector in order —
// so this test's dependency on skip_macro_increments_skip_counter and
// skip_if_true_delegates_to_skip having already run is a stable,
// intentional invariant, not accidental ordering.
LT_BEGIN_AUTO_TEST(littletest_skip_semantics_suite, skip_counter_nonzero_after_skip_runs)
    // After the LT_SKIP and LT_SKIP_IF(true, ...) tests
    // ran, the runner's skip counter must be >= 2. We can't reset
    // between tests without touching the runner, so this asserts the
    // cumulative invariant.
    LT_CHECK_GTE(littletest::auto_test_runner.get_skips(), 2);
LT_END_AUTO_TEST(skip_counter_nonzero_after_skip_runs)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
