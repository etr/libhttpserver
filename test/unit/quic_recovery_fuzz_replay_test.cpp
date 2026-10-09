/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include "fuzz/quic_recovery_fuzz.hpp"
#include "./quic_event_replay.hpp"
#include "./littletest.hpp"
LT_BEGIN_SUITE(recovery_events_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(recovery_events_suite)
LT_BEGIN_AUTO_TEST(recovery_events_suite, reordered_crypto_is_owned_and_consumed_only_after_gap_fills)
    const std::vector<std::uint8_t> bytes{1, 0, 2, 2, 15, 0, 0, 8, 1, 0, 0, 2, 15, 0, 0, 8};
    const auto result = quic_recovery_fuzz_input(bytes);
    LT_CHECK_EQ(result.crypto_read, std::size_t{4});
    LT_CHECK(result.released && result == quic_recovery_fuzz_input(bytes));
LT_END_AUTO_TEST(reordered_crypto_is_owned_and_consumed_only_after_gap_fills)
LT_BEGIN_AUTO_TEST(recovery_events_suite, acknowledgments_reorder_and_loss_timer_requeues_retained_information)
    const std::vector<std::uint8_t> bytes{2, 0, 0, 8, 3, 0, 64, 0, 4, 0, 100, 0,
        2, 0, 8, 8, 3, 0, 64, 0, 4, 0, 100, 0, 6, 0, 1, 0, 7, 0, 2, 0, 6, 0, 0, 0};
    const auto result = quic_recovery_fuzz_input(bytes);
    // A late ACK counts the lost packet, whose flight bytes were already removed.
    LT_CHECK_EQ(result.acknowledged_packets, std::size_t{2});
    LT_CHECK_EQ(result.acknowledged, std::size_t{100});
    LT_CHECK(result.lost >= 100 && result.accepted[7] > 0);
LT_END_AUTO_TEST(acknowledgments_reorder_and_loss_timer_requeues_retained_information)
LT_BEGIN_AUTO_TEST(recovery_events_suite, validated_path_activation_waits_for_pending_send_and_close_rejects_queued_work)
    const std::vector<std::uint8_t> bytes{10, 1, 10, 0, 11, 0, 0, 0, 12, 1, 0, 0,
        2, 2, 0, 4, 3, 2, 64, 0, 13, 1, 11, 0, 5, 0, 0, 0, 13, 1, 12, 0,
        14, 0, 0, 0, 2, 2, 0, 4, 7, 0, 255, 0};
    const auto result = quic_recovery_fuzz_input(bytes);
    LT_CHECK_EQ(result.paths_activated, 1U);
    LT_CHECK_EQ(result.close_notifications, 1U);
    LT_CHECK(result.rejected_queue > 0 && result.released);
LT_END_AUTO_TEST(validated_path_activation_waits_for_pending_send_and_close_rejects_queued_work)
LT_BEGIN_AUTO_TEST(recovery_events_suite, input_event_and_trace_limits_hold)
    const std::vector<std::uint8_t> bytes(8192, 0);
    const auto result = quic_recovery_fuzz_input(bytes);
    LT_CHECK_EQ(result.events, std::size_t{64});
    LT_CHECK_EQ(result.consumed, std::size_t{256});
    LT_CHECK(quic_recovery_fuzz_input(std::span(bytes).first(3)).events == 0);
LT_END_AUTO_TEST(input_event_and_trace_limits_hold)
LT_BEGIN_AUTO_TEST(recovery_events_suite, committed_recovery_seeds_mutations_and_truncations)
    LT_CHECK(quic_fuzz::replay_corpus(QUIC_RECOVERY_SEED_DIR, quic_recovery_fuzz_input) > 0);
LT_END_AUTO_TEST(committed_recovery_seeds_mutations_and_truncations)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
