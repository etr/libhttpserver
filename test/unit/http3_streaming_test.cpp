/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string>
#include <memory>
#include <utility>
#include <vector>
#include <array>
#include "./http3_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
LT_BEGIN_SUITE(http3_streaming_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(http3_streaming_suite)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, incremental_post_trailers_wait_for_fin_and_stream_response)
    hd::http3_request_limits limits;
    limits.body_buffer_bytes = 3;
    limits.response_buffer_bytes = 2;
    h3test::request_fixture f(limits);
    std::string received;
    unsigned calls = 0, ended = 0;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::post), "/hello",
                         [&](exchange& x) -> task<void> {
                             ++calls;
                             x.admit_body({3});
                             std::array<std::byte, 2> bytes;
                             for (;;) {
                                 auto part = co_await x.body().read_some(bytes);
                                 LT_CHECK(part.status.ok());
                                 if (part.end_of_body) break;
                                 received.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
                                 LT_CHECK_EQ(x.body().trailers().size(), 0u);
                             }
                             ++ended;
                             LT_CHECK_EQ(*x.body().trailers().first("x-end"), "yes");
                             x.start_response(http::status::from_code(200), {});
                             co_await x.writer().write(std::as_bytes(std::span(received)));
                             http::fields trailers;
                             trailers.append("x-reply", "done");
                             co_await x.writer().finish(trailers);
                         })
                  .ok());
    f.open(0);
    auto fields = h3test::get("POST");
    fields.push_back({"content-length", "6"});
    LT_ASSERT(f.feed(0, h3test::headers(fields)));
    f.executor.run_pending();
    LT_CHECK_EQ(calls, 1u);
    auto bytes = h3test::data("abcdef");
    auto trailers = h3test::headers({{"x-end", "yes"}});
    bytes.insert(bytes.end(), trailers.begin(), trailers.end());
    LT_ASSERT(f.feed(0, bytes));
    for (unsigned i = 0; i < 8; ++i) {
        f.executor.run_pending();
        f.pump(0);
    }
    LT_CHECK_EQ(received, "abcdef");
    LT_CHECK_EQ(ended, 0u);
    LT_ASSERT(f.feed(0, {}, true));
    f.executor.run_pending();
    f.drain(17);
    LT_CHECK_EQ(ended, 1u);
    auto r = h3test::decode_response(f.output[0]);
    LT_CHECK_EQ(r.body, "abcdef");
    LT_ASSERT_EQ(r.heads.size(), 2u);
    LT_CHECK_EQ(r.heads[1][0].name, "x-reply");
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(incremental_post_trailers_wait_for_fin_and_stream_response)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, receipts_never_credit_framing_across_unread_data)
    hd::http3_request_limits limits;
    limits.body_buffer_bytes = 8;
    limits.max_receipt_records = 8;
    h3test::request_fixture f(limits);
    httpserver::resume_signal first, second;
    unsigned reads = 0;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::post), "/hello",
                         [&](exchange& x) -> task<void> {
                             x.admit_body({8});
                             co_await first.wait();
                             std::array<std::byte, 2> bytes;
                             co_await x.body().read_some(bytes);
                             ++reads;
                             co_await second.wait();
                             co_await x.body().collect(16);
                             x.respond(http::status::from_code(204), {});
                         })
                  .ok());
    f.open(0);
    auto head = h3test::headers(h3test::get("POST"));
    LT_ASSERT(f.feed(0, head));
    f.executor.run_pending();
    const auto initial = f.credit(0);
    LT_CHECK_EQ(initial, 65536u + head.size());
    auto wire = h3test::data("abcd");
    auto another = h3test::data("efgh");
    wire.insert(wire.end(), another.begin(), another.end());
    auto unknown = h3test::frame(33, h3test::bytes({1, 2}));
    wire.insert(wire.end(), unknown.begin(), unknown.end());
    auto trailers = h3test::headers({{"x-end", "yes"}});
    wire.insert(wire.end(), trailers.begin(), trailers.end());
    LT_ASSERT(f.feed(0, wire, true));
    LT_CHECK_EQ(f.credit(0), initial + 2u);
    first.signal();
    f.executor.run_pending();
    LT_CHECK_EQ(reads, 1u);
    LT_CHECK_EQ(f.credit(0), initial + 4u);
    second.signal();
    f.executor.run_pending();
    f.pump(0);
    f.executor.run_pending();
    LT_CHECK_EQ(f.credit(0), 65536u + head.size() + wire.size());
    f.drain();
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(receipts_never_credit_framing_across_unread_data)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, send_credit_splits_frames_and_retention_does_not_charge_emission)
    hd::http3_request_limits limits;
    limits.response_buffer_bytes = 2;
    h3test::request_fixture f(limits, 0);
    unsigned wrote = 0;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [&](exchange& x) -> task<void> {
                             x.start_response(http::status::from_code(200), {});
                             std::string data = "abcdef";
                             co_await x.writer().write(std::as_bytes(std::span(data)));
                             ++wrote;
                             co_await x.writer().finish();
                         })
                  .ok());
    f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    f.engine->pump_output();
    LT_CHECK_EQ(f.flow.sent(), 0u);
    LT_CHECK_EQ(f.streams[0]->highest_sent(), 0u);
    LT_CHECK_EQ(wrote, 0u);
    for (unsigned credit = 1; credit < 40 && !f.fins[0]; ++credit) {
        LT_ASSERT(f.flow.apply({hd::quic_flow_kind::max_stream_data, credit, 0}));
        f.engine->pump_output();
        LT_CHECK_EQ(f.streams[0]->highest_sent(), f.output[0].size());
        f.drain(16);
    }
    auto r = h3test::decode_response(f.output[0]);
    LT_CHECK_EQ(r.body, "abcdef");
    LT_CHECK_EQ(wrote, 1u);
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(send_credit_splits_frames_and_retention_does_not_charge_emission)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, content_length_and_malformed_trailers_are_stream_errors)
    for (unsigned mode : {0u, 1u, 2u, 3u, 4u}) {
        h3test::request_fixture f;
        LT_ASSERT(f.routes
                      .route(http::method::known(http::method_id::post), "/hello",
                             [](exchange& x) -> task<void> {
                                 x.admit_body({});
                                 co_await x.body().collect(32);
                                 x.respond(http::status::from_code(204), {});
                             })
                      .ok());
        f.open(0);
        auto fields = h3test::get("POST");
        fields.push_back({"content-length", mode == 0 ? "1" : mode == 1 ? "3" : "2"});
        LT_ASSERT(f.feed(0, h3test::headers(fields)));
        f.executor.run_pending();
        auto wire = h3test::data("ab");
        if (mode >= 2) {
            auto trailers = h3test::headers({{mode == 2 ? ":path" : mode == 3 ? "content-length" : "x-end", "3"}});
            wire.insert(wire.end(), trailers.begin(), trailers.end());
        }
        LT_ASSERT(f.feed(0, wire, true));
        auto a = f.engine->take_action();
        if (mode == 4) {
            LT_CHECK(!a);
            f.executor.run_pending();
            f.drain();
            LT_CHECK_EQ(f.fins[0], 1u);
        } else {
            LT_CHECK(a && a->reset);
            if (a && a->reset) {
                LT_CHECK_EQ(a->reset->stream, 0u);
                LT_CHECK_EQ(a->reset->error, 0x10eu);
            }
        }
        LT_CHECK(!f.engine->failure());
        f.open(4);
        LT_ASSERT(f.feed(4, h3test::headers(h3test::get()), true));
        f.executor.run_pending();
        f.drain();
        LT_CHECK_EQ(f.fins[4], 1u);
    }
LT_END_AUTO_TEST(content_length_and_malformed_trailers_are_stream_errors)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, recovery_refusal_and_abandoned_preparation_preserve_response_and_capacity)
    hd::http3_request_limits limits;
    limits.response_buffer_bytes = 2;
    hd::quic_recovery_config config;
    config.max_information = 1;
    h3test::request_fixture f(limits, 65536, config);
    unsigned written = 0;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [&](exchange& x) -> task<void> {
                             x.start_response(http::status::from_code(200), {});
                             std::string body = "abcd";
                             co_await x.writer().write(std::as_bytes(std::span(body)));
                             ++written;
                             co_await x.writer().finish();
                         })
                  .ok());
    f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    for (unsigned i = 0; i < 4; ++i)
        f.engine->pump_output();
    f.executor.run_pending();
    LT_CHECK_EQ(written, 0u);
    LT_CHECK_EQ(f.flow.sent(), 0u);
    std::array<std::byte, 64> packet;
    auto plan = f.recovery.prepare_packet(hd::quic_pn_space::application, packet, {}, f.flow);
    LT_ASSERT(plan);
    LT_ASSERT(plan.stream);
    LT_CHECK_EQ(plan.stream->offset, 0u);
    LT_ASSERT(f.recovery.abandon_packet(plan.token));
    f.engine->pump_output();
    LT_CHECK_EQ(f.flow.sent(), 0u);
    f.drain(16);
    LT_CHECK_EQ(written, 1u);
    LT_CHECK_EQ(h3test::decode_response(f.output[0]).body, "abcd");
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(recovery_refusal_and_abandoned_preparation_preserve_response_and_capacity)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, successful_retention_releases_writer_before_any_peer_ack)
    hd::http3_request_limits limits;
    limits.response_buffer_bytes = 2;
    h3test::request_fixture f(limits);
    unsigned written = 0;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [&](exchange& x) -> task<void> {
                             x.start_response(http::status::from_code(200), {});
                             std::string body = "abcd";
                             co_await x.writer().write(std::as_bytes(std::span(body)));
                             ++written;
                             co_await x.writer().finish();
                         })
                  .ok());
    f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    LT_CHECK_EQ(written, 0u);
    f.engine->pump_output();
    f.engine->pump_output();
    f.executor.run_pending();
    LT_CHECK_EQ(written, 1u);
    LT_CHECK_EQ(f.flow.sent(), 0u);
    LT_CHECK_EQ(f.output[0].size(), 0u);
    f.drain();
    LT_CHECK_EQ(h3test::decode_response(f.output[0]).body, "abcd");
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(successful_retention_releases_writer_before_any_peer_ack)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, receipt_limit_and_tiny_admission_backpressure_one_stream)
    hd::http3_request_limits limits;
    limits.body_buffer_bytes = 8;
    limits.max_receipt_records = 2;
    h3test::request_fixture f(limits);
    httpserver::resume_signal pause;
    std::string received;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::post), "/hello",
                         [&](exchange& x) -> task<void> {
                             x.admit_body({2});
                             co_await pause.wait();
                             auto body = co_await x.body().collect(16);
                             received.assign(reinterpret_cast<const char*>(body.data.data()), body.data.size());
                             x.respond(http::status::from_code(204), {});
                         })
                  .ok());
    f.open(0);
    f.open(4);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get("POST"))));
    f.executor.run_pending();
    auto wire = h3test::data("abcd");
    auto next = h3test::data("ef");
    wire.insert(wire.end(), next.begin(), next.end());
    LT_ASSERT(f.feed(0, wire, true));
    auto credit = f.credit(0);
    f.pump(0);
    LT_CHECK_EQ(f.credit(0), credit);
    LT_ASSERT(f.feed(4, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(f.fins[4], 1u);
    pause.signal();
    for (unsigned i = 0; i < 12; ++i) {
        f.executor.run_pending();
        f.pump(0);
    }
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(received, "abcdef");
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(receipt_limit_and_tiny_admission_backpressure_one_stream)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, coalesced_post_uses_admission_backpressure_before_handler_runs)
    hd::http3_request_limits limits;
    limits.body_buffer_bytes = 8;
    h3test::request_fixture f(limits);
    httpserver::resume_signal pause;
    std::string received;
    unsigned ended = 0;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({2});
        co_await pause.wait();
        std::array<std::byte, 1> into;
        for (;;) {
            auto part = co_await x.body().read_some(into);
            LT_CHECK(part.status.ok());
            if (!part.status.ok() || part.end_of_body) break;
            received.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
            LT_CHECK(x.body().trailers().empty());
        }
        if (x.body().trailers().first("x-end") == "yes") ++ended;
        x.respond(http::status::from_code(204), {});
    }).ok());
    f.open(0);
    auto fields = h3test::get("POST");
    fields.push_back({"content-length", "6"});
    auto head = h3test::headers(fields);
    auto wire = head;
    auto body = h3test::data("abcdef");
    wire.insert(wire.end(), body.begin(), body.end());
    auto trailers = h3test::headers({{"x-end", "yes"}});
    wire.insert(wire.end(), trailers.begin(), trailers.end());
    LT_ASSERT(f.feed(0, wire, true));
    LT_CHECK_EQ(f.credit(0), 65536u + head.size() + 2u);
    f.executor.run_pending();
    f.pump(0);
    LT_CHECK(!f.engine->take_action());
    LT_CHECK_EQ(f.credit(0), 65536u + head.size() + 2u);
    f.open(4);
    LT_ASSERT(f.feed(4, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(f.fins[4], 1u);
    LT_CHECK_EQ(ended, 0u);
    pause.signal();
    for (unsigned i = 0; i < 12; ++i) {
        f.executor.run_pending();
        f.pump(0);
    }
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(received, "abcdef");
    LT_CHECK_EQ(ended, 1u);
    LT_CHECK_EQ(f.credit(0), 65536u + wire.size());
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(coalesced_post_uses_admission_backpressure_before_handler_runs)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, single_retention_slot_rotates_to_ready_sibling)
    hd::http3_request_limits limits;
    limits.response_buffer_bytes = 2;
    limits.max_pending_output_records = 1;
    h3test::request_fixture f(limits);
    unsigned written = 0;
    bool finished = false;
    LT_ASSERT(f.routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {});
        std::string chunk = "ab";
        for (unsigned i = 0; i < 32; ++i) {
            co_await x.writer().write(std::as_bytes(std::span(chunk)));
            ++written;
        }
        co_await x.writer().finish();
        finished = true;
    }).ok());
    f.open(0);
    f.open(4);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    LT_ASSERT(f.feed(4, h3test::headers(h3test::get("GET", "/missing")), true));
    f.executor.run_pending();
    f.drain(32, 4);
    LT_CHECK_EQ(f.fins[4], 1u);
    LT_CHECK(!finished);
    LT_CHECK(written < 32u);
    f.drain(32);
    LT_CHECK_EQ(h3test::decode_response(f.output[0]).body, "abababababababababababababababababababababababababababababababab");
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(single_retention_slot_rotates_to_ready_sibling)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, critical_prefix_uses_reserved_capacity_under_ordinary_saturation)
    for (unsigned saturation : {0u, 1u, 2u, 3u}) {
        hd::http3_request_limits limits;
        hd::quic_recovery_config config;
        if (saturation == 0) limits.max_pending_output_records = 1;
        if (saturation == 1) {
            config.max_information = 4;
            config.critical_information = 3;
        }
        if (saturation == 2) {
            config.max_retained_bytes = 11;
            config.critical_retained_bytes = 9;
        }
        h3test::request_fixture f(limits, 65536, config);
        f.open(0);
        if (saturation == 2) {
            LT_ASSERT(f.recovery.retain_stream({0, 0, h3test::bytes({1, 2}), false}));
        } else {
            LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
            f.executor.run_pending();
            f.engine->pump_output();
        }
        std::vector<std::uint64_t> ids;
        for (auto role : {hd::http3_role::control, hd::http3_role::qpack_encoder, hd::http3_role::qpack_decoder}) {
            auto local = f.flow.open_local(true);
            LT_ASSERT(local);
            auto stream = std::make_unique<hd::quic_stream_state>(local.id, hd::quic_endpoint_role::server, f.flow.ids(), hd::quic_stream_limits{}, f.pool.critical());
            LT_ASSERT(!f.engine->attach_local(role, *stream));
            f.streams.emplace(local.id, std::move(stream));
            ids.push_back(local.id);
        }
        auto data_budget = f.pool.data().budget;
        auto critical_budget = f.pool.critical().budget;
        auto resource = httpserver::server::resource::quic_reassembly_bytes;
        httpserver::server::reservation fill;
        if (saturation == 3) LT_ASSERT(data_budget.reserve(resource, data_budget.capacity(resource) - data_budget.in_use(resource), fill).ok());
        const auto ordinary_before = data_budget.in_use(resource), critical_before = critical_budget.in_use(resource);
        f.engine->pump_output();
        LT_CHECK_EQ(data_budget.in_use(resource), ordinary_before);
        LT_CHECK_EQ(critical_budget.in_use(resource), critical_before + 18u);
        std::array<std::byte, 64> packet;
        hd::quic_send_request request;
        request.protection_overhead = 0;
        f.now += std::chrono::seconds(1);
        auto plan = f.recovery.prepare_scheduled_packet(hd::quic_pn_space::application, packet, f.now, request, f.flow);
        LT_CHECK(plan && plan.stream);
        if (!plan || !plan.stream) continue;
        LT_CHECK_EQ(plan.stream->stream, ids[0]);
        LT_ASSERT(f.recovery.check_scheduled_emission(plan.token, f.now, plan.bytes, true, true));
        LT_ASSERT(f.recovery.abandon_packet(plan.token));
        LT_CHECK_EQ(f.flow.sent(), 0u);
        f.drain(64);
        LT_CHECK(f.output[ids[0]] == h3test::bytes({0, 4, 4, 1, 0, 7, 0}));
        LT_CHECK(f.output[ids[1]] == h3test::bytes({2}));
        LT_CHECK(f.output[ids[2]] == h3test::bytes({3}));
        for (auto id : ids) LT_CHECK_EQ(f.fins[id], 0u);
        LT_CHECK_EQ(f.flow.sent(), f.output[0].size() + 9u);
        LT_CHECK_EQ(critical_budget.in_use(resource), critical_before);
        f.engine->pump_output();
        LT_CHECK_EQ(critical_budget.in_use(resource), critical_before);
    }
LT_END_AUTO_TEST(critical_prefix_uses_reserved_capacity_under_ordinary_saturation)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, continue_is_sent_only_after_admission_and_zero_data_does_not_end_body)
    for (bool admit : {false, true}) {
        h3test::request_fixture f;
        unsigned ended = 0;
        LT_ASSERT(f.routes
                      .route(http::method::known(http::method_id::post), "/hello",
                             [&](exchange& x) -> task<void> {
                                 if (admit) {
                                     x.admit_body({});
                                     auto body = co_await x.body().collect(4);
                                     LT_CHECK(body.status.ok());
                                     ++ended;
                                 }
                                 x.respond(http::status::from_code(admit ? 204 : 403), {});
                                 co_return;
                             })
                      .ok());
        f.open(0);
        auto head = h3test::get("POST");
        head.push_back({"expect", "100-continue"});
        LT_ASSERT(f.feed(0, h3test::headers(head)));
        f.executor.run_pending();
        f.drain();
        auto interim = h3test::decode_response(f.output[0]);
        LT_ASSERT_EQ(interim.heads.size(), 1u);
        LT_CHECK_EQ(interim.heads[0][0].value, admit ? "100" : "403");
        LT_CHECK_EQ(f.fins[0], admit ? 0u : 1u);
        if (admit) {
            LT_ASSERT(f.feed(0, h3test::data("")));
            f.executor.run_pending();
            LT_CHECK_EQ(ended, 0u);
            LT_ASSERT(f.feed(0, {}, true));
            f.executor.run_pending();
            f.drain();
            LT_CHECK_EQ(ended, 1u);
            LT_CHECK_EQ(f.fins[0], 1u);
        }
    }
LT_END_AUTO_TEST(continue_is_sent_only_after_admission_and_zero_data_does_not_end_body)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, response_trailer_limit_is_reported_to_writer_before_copying)
    hd::http3_request_limits limits;
    limits.framing.headers.max_fields = 4;
    h3test::request_fixture f(limits);
    bool accepted = true;
    LT_ASSERT(f.routes
                  .route(http::method::known(http::method_id::get), "/hello",
                         [&](exchange& x) -> task<void> {
                             x.start_response(http::status::from_code(200), {});
                             http::fields trailers;
                             for (unsigned i = 0; i < 5; ++i)
                                 trailers.append("x-repeat", "value");
                             accepted = (co_await x.writer().finish(trailers)).status.ok();
                         })
                  .ok());
    f.open(0);
    LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
    f.executor.run_pending();
    LT_CHECK(!accepted);
    auto action = f.engine->take_action();
    LT_ASSERT(action);
    LT_ASSERT(action->reset);
LT_END_AUTO_TEST(response_trailer_limit_is_reported_to_writer_before_copying)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, blocked_response_does_not_block_sibling_or_required_control_prefix)
    h3test::request_fixture f({}, 0);
    for (auto id : {0u, 4u}) {
        f.open(id);
        LT_ASSERT(f.feed(id, h3test::headers(h3test::get()), true));
    }
    auto local = f.flow.open_local(true);
    LT_ASSERT(local);
    auto stream = std::make_unique<hd::quic_stream_state>(local.id, hd::quic_endpoint_role::server, f.flow.ids(), hd::quic_stream_limits{}, f.pool.critical());
    LT_CHECK(!f.engine->attach_local(hd::http3_role::control, *stream));
    f.streams.emplace(local.id, std::move(stream));
    LT_ASSERT(f.flow.apply({hd::quic_flow_kind::max_stream_data, 64, 4}));
    LT_ASSERT(f.flow.apply({hd::quic_flow_kind::max_stream_data, 64, local.id}));
    f.executor.run_pending();
    f.drain();
    LT_CHECK_EQ(f.fins[0], 0u);
    LT_CHECK_EQ(f.fins[4], 1u);
    LT_CHECK(f.output[local.id] == h3test::bytes({0, 4, 4, 1, 0, 7, 0}));
    LT_CHECK_EQ(f.fins[local.id], 0u);
    LT_ASSERT(f.flow.apply({hd::quic_flow_kind::max_stream_data, 64, 0}));
    f.drain();
    LT_CHECK_EQ(f.fins[0], 1u);
LT_END_AUTO_TEST(blocked_response_does_not_block_sibling_or_required_control_prefix)
LT_BEGIN_AUTO_TEST(http3_streaming_suite, peer_field_section_uses_full_62_bit_setting_without_narrowing)
    for (auto cap : {std::uint64_t{41}, std::uint64_t{1} << 40}) {
        h3test::request_fixture f;
        f.open(2);
        std::vector<std::byte> settings;
        h3test::integer(settings, 6);
        h3test::integer(settings, cap);
        auto control = h3test::bytes({0});
        auto frame = h3test::frame(4, settings);
        control.insert(control.end(), frame.begin(), frame.end());
        LT_ASSERT(f.feed(2, control));
        f.open(0);
        LT_ASSERT(f.feed(0, h3test::headers(h3test::get()), true));
        f.executor.run_pending();
        f.drain();
        if (cap == 41) {
            auto a = f.engine->take_action();
            LT_ASSERT(a);
            LT_ASSERT(a->reset);
            LT_CHECK_EQ(f.fins[0], 0u);
        } else {
            LT_CHECK_EQ(f.fins[0], 1u);
            LT_CHECK_EQ(h3test::decode_response(f.output[0]).heads[0][0].value, "404");
        }
    }
LT_END_AUTO_TEST(peer_field_section_uses_full_62_bit_setting_without_narrowing)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
