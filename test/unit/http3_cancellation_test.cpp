/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <string>
#include <utility>
#include <atomic>
#include "./http3_request_fixture.hpp"
#include "./http2_request_fixture.hpp"
#include "./http3_route_parity_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
LT_BEGIN_SUITE(http3_cancellation_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(http3_cancellation_suite)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, destruction_invalidates_queued_and_suspended_frames)
    for (bool run : {false, true}) {
        h3test::request_fixture f;
        httpserver::resume_signal pause;
        unsigned entered = 0, resumed = 0;
        LT_ASSERT(f.routes
                      .route(http::method::known(http::method_id::get), "/hello",
                             [&](exchange& x) -> task<void> {
                                 ++entered;
                                 co_await pause.wait();
                                 ++resumed;
                                 x.respond(http::status::from_code(200), {});
                             })
                      .ok());
        f.open(0);
        LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
        if (run) f.executor.run_pending();
        f.engine.reset();
        pause.signal();
        f.executor.run_pending();
        LT_CHECK_EQ(entered, run ? 1u : 0u);
        LT_CHECK_EQ(resumed, 0u);
        LT_CHECK_EQ(f.pool.data().budget.in_use(h3test::hs::resource::header_bytes), 0u);
        LT_CHECK_EQ(f.pool.data().budget.in_use(h3test::hs::resource::body_buffer_bytes), 0u);
    }
LT_END_AUTO_TEST(destruction_invalidates_queued_and_suspended_frames)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, reset_cancels_body_application_and_retained_output_with_sibling_usable)
    for (unsigned mode : {0u, 1u, 2u}) {
        h3test::request_fixture f;
        httpserver::resume_signal pause;
        unsigned after = 0;
        LT_ASSERT(f.routes
                      .route(http::method::known(http::method_id::post), "/hello",
                             [&](exchange& x) -> task<void> {
                                 if (mode == 0) {
                                     x.admit_body({});
                                     co_await x.body().collect(16);
                                 }
                                 if (mode == 1) co_await pause.wait();
                                 if (mode == 2) {
                                     x.start_response(http::status::from_code(200), {});
                                     co_await pause.wait();
                                 }
                                 ++after;
                                 x.respond(http::status::from_code(204), {});
                             })
                      .ok());
        auto& s = f.open(0);
        LT_ASSERT(f.feed(0, h3test::headers(h3test::get("POST"))));
        f.executor.run_pending();
        f.engine->pump_output();
        LT_ASSERT(f.flow.receive(s, hd::quic_reset_stream_frame{0, 9, f.incoming[0]}));
        auto terminal = s.take_terminal();
        LT_ASSERT(terminal);
        f.engine->terminal(0, *terminal);
        f.engine->terminal(0, *terminal);
        pause.signal();
        f.executor.run_pending();
        f.drain();
        LT_CHECK_EQ(after, 0u);
        LT_CHECK_EQ(f.output[0].size(), 0u);
        f.open(4);
        LT_ASSERT(f.feed(4, h3test::headers(h3test::get()), true));
        f.executor.run_pending();
        f.drain();
        LT_CHECK_EQ(f.fins[4], 1u);
        LT_CHECK(!f.engine->failure());
    }
LT_END_AUTO_TEST(reset_cancels_body_application_and_retained_output_with_sibling_usable)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, stop_sending_uses_emitted_size_and_clean_fin_does_not_cancel_response)
    h3test::request_fixture f;
    httpserver::resume_signal pause;
    unsigned resumed = 0;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [&](exchange& x) -> task<void> {
                             x.start_response(http::status::from_code(200), {});
                             co_await pause.wait();
                             ++resumed;
                             co_await x.writer().finish();
                         })
                  .ok());
    auto& s = f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    f.engine->pump_output();
    LT_CHECK_EQ(s.highest_sent(), 0u);
    f.engine->stop_sending(0, 123);
    auto a = f.engine->take_action();
    LT_ASSERT(a);
    LT_ASSERT(a->reset);
    LT_CHECK_EQ(a->reset->final_size, 0u);
    f.engine->stop_sending(0, 123);
    LT_CHECK(!f.engine->take_action());
    pause.signal();
    f.executor.run_pending();
    LT_CHECK_EQ(resumed, 0u);
    f.drain();
    LT_CHECK_EQ(f.output[0].size(), 0u);
LT_END_AUTO_TEST(stop_sending_uses_emitted_size_and_clean_fin_does_not_cancel_response)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, unfinished_streaming_handler_is_reset)
    h3test::request_fixture f;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [](exchange& x) -> task<void> {
                             x.start_response(http::status::from_code(200), {});
                             co_return;
                         })
                  .ok());
    f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    f.engine->pump_output();
    auto a = f.engine->take_action();
    LT_ASSERT(a);
    LT_ASSERT(a->reset);
    LT_CHECK_EQ(a->reset->error, 0x102u);
LT_END_AUTO_TEST(unfinished_streaming_handler_is_reset)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, receive_abandonment_preserves_response_when_peer_honors_stop_sending)
    h3test::request_fixture f;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::post), "/hello",
                         [](exchange& x) -> task<void> {
                             x.respond(http::status::from_code(403), {});
                             co_return;
                         })
                  .ok());
    auto& stream = f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get("POST"))));
    f.executor.run_pending();
    auto action = f.engine->take_action();
    LT_ASSERT(action);
    LT_ASSERT(action->stop_sending);
    LT_CHECK(!action->reset);
    LT_ASSERT(f.flow.receive(stream, hd::quic_reset_stream_frame{0, 0x10c, f.incoming[0]}));
    auto terminal = stream.take_terminal();
    LT_ASSERT(terminal);
    f.engine->terminal(0, *terminal);
    f.drain();
    LT_ASSERT_EQ(f.fins[0], 1u);
    LT_CHECK_EQ(h3test::decode_response(f.output[0]).heads[0][0].value, "403");
LT_END_AUTO_TEST(receive_abandonment_preserves_response_when_peer_honors_stop_sending)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, inline_handler_can_destroy_engine_or_cancel_itself)
    for (bool destroy : {false, true}) {
        h3test::request_fixture f;
        httpserver::inline_executor executor;
        unsigned before = 0, after = 0;
        f.engine = std::make_unique<hd::http3_request_engine>(f.pool.data(), f.pool.critical(), f.flow, f.recovery, f.routes, executor);
        LT_ASSERT(f.routes
                      .route(http::method::known(http::method_id::get), "/hello",
                             [&](exchange&) -> task<void> {
                                 ++before;
                                 if (destroy)
                                     f.engine.reset();
                                 else
                                     f.engine->disconnect({http::outcome_code::cancelled, "inline cancellation"});
                                 ++after;
                                 co_return;
                             })
                      .ok());
        f.open(0);
        auto wire = h3test::headers(h3test::get());
        LT_ASSERT(f.flow.receive(*f.streams[0], {0, 0, wire, true}));
        auto* engine = f.engine.get();
        engine->pump_receive(0);
        LT_CHECK_EQ(before, 1u);
        LT_CHECK_EQ(after, 1u);
    }
LT_END_AUTO_TEST(inline_handler_can_destroy_engine_or_cancel_itself)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, common_handler_cancellation_is_observed_over_http1_http2_and_h3)
    for (unsigned wait : {0u, 1u, 2u}) {
        for (unsigned version : {1u, 2u, 3u}) {
            hd::http3_request_limits limits;
            limits.response_buffer_bytes = 2;
            h3test::request_fixture f(limits);
            httpserver::stop_token cancellation;
            httpserver::resume_signal signal;
            std::atomic<bool> entered{false}, finished{false};
            LT_ASSERT(f.routes
                          .route(http::method::known(http::method_id::post), "/hello",
                                 [&](exchange& x) -> task<void> {
                                     cancellation = x.cancellation();
                                     if (wait == 0) x.admit_body({4});
                                     if (wait == 1) x.suspend(signal);
                                     if (wait == 2) x.start_response(http::status::from_code(200), {});
                                     entered = true;
                                     if (wait == 0) co_await x.body().collect(32);
                                     if (wait == 1) co_await signal.wait();
                                     if (wait == 2) {
                                         const std::string data(8 * 1024 * 1024, 'x');
                                         auto result = co_await x.writer().write(std::as_bytes(std::span(data)));
                                         finished = result.status.ok();
                                     }
                                 })
                          .ok());
            if (version == 1) {
                h3parity::http1_fixture h1(f.root, f.routes);
                h1.send("POST /hello HTTP/1.1\r\nHost: example.test\r\nContent-Length: 4\r\n\r\n");
                LT_ASSERT(h3parity::until([&] { return entered.load(); }));
                LT_CHECK(!finished.load());
                h1.cancel();
                LT_ASSERT(h3parity::until([&] { return cancellation.stop_requested(); }));
            } else if (version == 2) {
                hd::http2_request_limits h2limits;
                h2limits.response_buffer_bytes = 2;
                hd::http2_request_engine h2(f.root, f.routes, f.executor, h2limits);
                hd::hpack_encoder encoder(f.root);
                auto wire = h2test::preface();
                h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(4, 0)));
                auto fields = h2test::get();
                fields[0].value = "POST";
                h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
                LT_ASSERT(h2test::feed(h2, wire));
                f.executor.run_pending();
                LT_CHECK(entered.load());
                LT_CHECK(!finished.load());
                LT_ASSERT(h2test::feed(h2, h2test::frame(3, 0, 1, {0, 0, 0, 8})));
                f.executor.run_pending();
                LT_CHECK(cancellation.stop_requested());
            } else {
                auto& stream = f.open(0);
                LT_ASSERT(f.feed(0, h3test::headers(h3test::get("POST"))));
                f.executor.run_pending();
                LT_CHECK(entered.load());
                LT_CHECK(!finished.load());
                LT_ASSERT(f.flow.receive(stream, hd::quic_reset_stream_frame{0, 8, f.incoming[0]}));
                auto terminal = stream.take_terminal();
                LT_ASSERT(terminal);
                f.engine->terminal(0, *terminal);
                f.executor.run_pending();
                LT_CHECK(cancellation.stop_requested());
            }
        }
    }
LT_END_AUTO_TEST(common_handler_cancellation_is_observed_over_http1_http2_and_h3)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, reset_releases_partial_response_storage_and_semantic_reservations)
    h3test::request_fixture f({}, 1);
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [](exchange& x) -> task<void> {
                             http::fields fields;
                             fields.append("x-large", std::string(500, 'a'));
                             x.respond(http::status::from_code(200), fields);
                             co_return;
                         })
                  .ok());
    auto& stream = f.open(0);
    const auto before = f.pool.data().budget.in_use(h3test::hs::resource::quic_reassembly_bytes);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    f.engine->pump_output();
    // STOP_SENDING cancels a staged/retained response even after clean request FIN.
    f.engine->stop_sending(0, 7);
    f.executor.run_pending();
    LT_CHECK_EQ(f.pool.data().budget.in_use(h3test::hs::resource::header_bytes), 0u);
    LT_CHECK_EQ(f.pool.data().budget.in_use(h3test::hs::resource::quic_reassembly_bytes), before + stream.retained_storage());
    LT_CHECK_EQ(stream.highest_sent(), 0u);
LT_END_AUTO_TEST(reset_releases_partial_response_storage_and_semantic_reservations)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, transport_reset_interrupts_an_outstanding_borrowed_data_event)
    hd::http3_request_limits limits;
    limits.body_buffer_bytes = 1;
    h3test::request_fixture f(limits);
    httpserver::resume_signal pause;
    httpserver::stop_token cancellation;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::post), "/hello",
                         [&](exchange& x) -> task<void> {
                             cancellation = x.cancellation();
                             x.admit_body({1});
                             co_await pause.wait();
                             co_await x.body().collect(16);
                         })
                  .ok());
    auto& stream = f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get("POST"))));
    f.executor.run_pending();
    LT_ASSERT(f.feed(0, h3test::data("abcdef")));
    LT_ASSERT(f.flow.receive(stream, hd::quic_reset_stream_frame{0, 8, f.incoming[0]}));
    f.pump(0);
    LT_CHECK(cancellation.stop_requested());
    LT_CHECK(!f.engine->failure());
LT_END_AUTO_TEST(transport_reset_interrupts_an_outstanding_borrowed_data_event)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, automatic_critical_terminal_keeps_connection_error_scope)
    h3test::request_fixture f;
    auto& stream = f.open(2);
    LT_ASSERT(f.feed(2, h3test::bytes({0, 4, 0})));
    LT_ASSERT(f.flow.receive(stream, hd::quic_reset_stream_frame{2, 9, f.incoming[2]}));
    f.pump(2);
    LT_ASSERT(f.engine->failure());
    LT_CHECK_EQ(f.engine->failure()->wire_code, 0x104u);
LT_END_AUTO_TEST(automatic_critical_terminal_keeps_connection_error_scope)
LT_BEGIN_AUTO_TEST(http3_cancellation_suite, local_critical_closure_retains_the_framing_cores_error)
    for (auto kind : {hd::quic_terminal_kind::eof, hd::quic_terminal_kind::reset}) {
        h3test::request_fixture f;
        auto local = f.flow.open_local(true);
        LT_ASSERT(local);
        auto stream = std::make_unique<hd::quic_stream_state>(local.id, hd::quic_endpoint_role::server, f.flow.ids(), hd::quic_stream_limits{}, f.pool.critical());
        LT_CHECK(!f.engine->attach_local(hd::http3_role::control, *stream));
        f.streams.emplace(local.id, std::move(stream));
        f.engine->terminal(local.id, {kind, 9});
        LT_ASSERT(f.engine->failure());
        LT_CHECK_EQ(f.engine->failure()->wire_code, 0x104u);
    }
LT_END_AUTO_TEST(local_critical_closure_retains_the_framing_cores_error)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
