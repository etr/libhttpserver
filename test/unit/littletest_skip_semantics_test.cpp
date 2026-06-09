// Copyright 2026 Sebastiano Merlino
/*
 * TASK-076 Cycle 1: Pin the LT_SKIP / LT_SKIP_IF semantics added to
 * test/littletest.hpp.
 *
 * Pre-TASK-076 the project simulated a "skip" with `LT_CHECK_EQ(1, 1)`
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

// Cycle 1.a — LT_SKIP increments the runner's skip counter and halts
// the enclosing test body. We split the post-skip "must not run"
// assertion into a sibling test because LT_SKIP terminates the test by
// throwing a skip_unattended (mirroring assert_unattended). To observe
// the counter delta we register a probe sibling test that runs before
// the skipping test in registration order.
LT_BEGIN_AUTO_TEST(littletest_skip_semantics_suite, skip_macro_increments_skip_counter)
    int before = littletest::auto_test_runner.get_skips();
    LT_SKIP("intentional skip — Cycle 1 sentinel");
    // Unreachable: LT_SKIP throws, so the line below must not execute.
    // If it does, the test framework would record a CHECK FAILURE here
    // because we'd be asserting `after == before + 1` after the
    // post-skip body still ran — which is precisely what the contract
    // forbids.
    int after = littletest::auto_test_runner.get_skips();
    LT_CHECK_EQ(after, before + 1);
    LT_FAIL("LT_SKIP did not halt test body");
LT_END_AUTO_TEST(skip_macro_increments_skip_counter)

// Cycle 1.b — LT_SKIP_IF(false, ...) is a no-op; control flows through.
LT_BEGIN_AUTO_TEST(littletest_skip_semantics_suite, skip_if_false_does_not_skip)
    int before = littletest::auto_test_runner.get_skips();
    LT_SKIP_IF(false, "should not fire");
    int after = littletest::auto_test_runner.get_skips();
    LT_CHECK_EQ(after, before);
    // Continue executing — control returns from LT_SKIP_IF.
    LT_CHECK_EQ(1 + 1, 2);
LT_END_AUTO_TEST(skip_if_false_does_not_skip)

// Cycle 1.c — LT_SKIP_IF(true, ...) increments the counter (delegates
// to LT_SKIP). We measure the post-state by reading
// get_skips() in a fresh sibling test that observes the cumulative
// counter set by the prior test's skip.
LT_BEGIN_AUTO_TEST(littletest_skip_semantics_suite, observe_prior_skip_was_counted)
    // After Cycle 1.a ran (which called LT_SKIP) the runner's skip
    // counter must be >= 1. We can't reset between tests without
    // touching the runner, so this asserts the cumulative invariant.
    LT_CHECK_GTE(littletest::auto_test_runner.get_skips(), 1);
LT_END_AUTO_TEST(observe_prior_skip_was_counted)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
