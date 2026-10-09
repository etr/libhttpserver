/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include "fuzz/quic_tls_callbacks_fuzz.hpp"
#include "./quic_event_replay.hpp"
#include "./littletest.hpp"
#include "./quic_owner_peer.hpp"
LT_BEGIN_SUITE(callback_events_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(callback_events_suite)
LT_BEGIN_AUTO_TEST(callback_events_suite, leases_remain_owned_across_reordered_crypto_and_secret_transition)
    const std::vector<std::uint8_t> bytes{0, 0, 2, 2, 0, 0, 0, 2, 2, 0, 0, 0,
        0, 1, 0, 4, 6, 1, 0, 0, 0, 1, 4, 2, 3, 0, 255, 0, 2, 0, 0, 0, 3, 0, 255, 0};
    const auto result = quic_tls_callbacks_fuzz_input(bytes);
    LT_CHECK_EQ(result.crypto_read, std::size_t{8});
    LT_CHECK(result.accepted[6] == 1 && result.released);
LT_END_AUTO_TEST(leases_remain_owned_across_reordered_crypto_and_secret_transition)
LT_BEGIN_AUTO_TEST(callback_events_suite, dispatch_send_backpressure_retirement_and_owned_copy)
    const std::vector<std::uint8_t> bytes{1, 0, 0, 12, 1, 0, 8, 4, 4, 0, 0, 16,
        5, 0, 4, 0, 1, 0, 8, 4, 4, 0, 4, 16};
    const auto result = quic_tls_callbacks_fuzz_input(bytes);
    LT_CHECK_EQ(result.accepted[1], 3U);
    LT_CHECK_EQ(result.accepted[4], 2U);
    LT_CHECK(result.released && result == quic_tls_callbacks_fuzz_input(bytes));
LT_END_AUTO_TEST(dispatch_send_backpressure_retirement_and_owned_copy)
LT_BEGIN_AUTO_TEST(callback_events_suite, sticky_dispatch_failure_and_close_with_pending_send_reject_late_events)
    const std::vector<std::uint8_t> bytes{0, 0, 0, 4, 2, 0, 0, 0, 16, 0, 0, 0,
        8, 0, 42, 0, 1, 0, 0, 4, 10, 0, 0, 0, 0, 0, 4, 4, 17, 0, 0, 0, 11, 0, 255, 0};
    const auto result = quic_tls_callbacks_fuzz_input(bytes);
    LT_CHECK_EQ(result.close_notifications, 1U);
    LT_CHECK_EQ(result.accepted[1], 0U);
    LT_CHECK(result.rejected_queue > 0 && result.released);
LT_END_AUTO_TEST(sticky_dispatch_failure_and_close_with_pending_send_reject_late_events)
LT_BEGIN_AUTO_TEST(callback_events_suite, budget_refusal_is_recoverable_and_dispatch_secret_without_cipher_is_terminal)
    const std::vector<std::uint8_t> bytes{12, 0, 0, 0, 0, 0, 0, 4, 13, 0, 0, 0,
        0, 0, 0, 4, 14, 1, 0, 0, 1, 0, 0, 4};
    const auto result = quic_tls_callbacks_fuzz_input(bytes);
    LT_CHECK_EQ(result.accepted[0], 1U);
    LT_CHECK_EQ(result.accepted[14], 0U);
    LT_CHECK_EQ(result.accepted[1], 0U);
    LT_CHECK(result.released);
LT_END_AUTO_TEST(budget_refusal_is_recoverable_and_dispatch_secret_without_cipher_is_terminal)
LT_BEGIN_AUTO_TEST(callback_events_suite, callback_input_and_event_limits_hold)
    const std::vector<std::uint8_t> bytes(8192, 0);
    const auto result = quic_tls_callbacks_fuzz_input(bytes);
    LT_CHECK(result.events == 64 && result.consumed == 256);
    LT_CHECK(quic_tls_callbacks_fuzz_input(std::span(bytes).first(3)).events == 0);
LT_END_AUTO_TEST(callback_input_and_event_limits_hold)
LT_BEGIN_AUTO_TEST(callback_events_suite, committed_callback_seeds_mutations_and_truncations)
    LT_CHECK(quic_fuzz::replay_corpus(QUIC_CALLBACK_SEED_DIR, quic_tls_callbacks_fuzz_input) > 0);
LT_END_AUTO_TEST(committed_callback_seeds_mutations_and_truncations)
LT_BEGIN_AUTO_TEST(callback_events_suite, malformed_post_handshake_crypto_closes_owner_before_or_after_pending_send)
    for (bool pending_send : {false, true}) {
        owner_test::peer peer;
        httpserver::resume_signal pause;
        unsigned started = 0, resumed = 0;
        LT_ASSERT(peer.routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
            [&](httpserver::exchange& exchange) -> httpserver::task<void> {
                ++started;
                co_await pause.wait();
                ++resumed;
                co_await exchange.writer().finish();
            }).ok());
        peer.connect();
        auto request = h3test::headers(h3test::get());
        const std::array<hd::quic_frame, 1> frames{hd::quic_stream_frame{0, 0, request, true}};
        peer.send(frames, peer.address);
        LT_ASSERT(started == 1);
        if (pending_send) LT_ASSERT(peer.prepare());
        // Unexpected TLS handshake type invokes the actual alert callback.
        const std::array malformed{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{0}};
        const std::array<hd::quic_frame, 1> crypto{hd::quic_crypto_frame{peer.copied[2], malformed}};
        auto delayed = peer.packet(crypto, peer.address);
        peer.server->on_datagram(delayed);
        if (pending_send) {
            // The owner drops incoming payload while a submitted send is pinned.
            // Complete that send before delivering the queued event again.
            LT_CHECK_EQ(peer.server->close_notifications(), 0U);
            LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::active);
            peer.server->emitted(false, peer.now);
            peer.server->on_datagram(delayed);
        }
        LT_CHECK_EQ(peer.server->close_notifications(), 1U);
        LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::closing);
        LT_CHECK(peer.server->failure().empty());
        pause.signal();
        peer.executor.run_pending();
        LT_CHECK_EQ(resumed, 0U);
        peer.server->on_datagram(delayed);
        peer.server->request_close({}, peer.now);
        LT_CHECK_EQ(peer.server->close_notifications(), 1U);
        const auto deadline = peer.server->deadline();
        LT_ASSERT(deadline);
        // The earliest deadline may be a close response, rather than retirement.
        peer.server->tick(peer.now + std::chrono::hours(1));
        LT_CHECK(!peer.server->has_keys(hd::quic_key_level::application));
        auto budget = peer.server->storage_budget();
        peer.server.reset();
        peer.executor.run_pending();
        LT_CHECK_EQ(budget.in_use(httpserver::server::resource::quic_reassembly_bytes), std::size_t{0});
    }
LT_END_AUTO_TEST(malformed_post_handshake_crypto_closes_owner_before_or_after_pending_send)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
