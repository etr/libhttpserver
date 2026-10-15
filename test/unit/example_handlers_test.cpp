/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <cstddef>
#include <string>
#include <vector>
#include <httpserver/detail/exchange_runner.hpp>
#include "../../examples/route_handlers.hpp"
#include "./body_sink_fake.hpp"
#include "./body_source_fake.hpp"
#include "./littletest.hpp"

namespace http = httpserver::http;
namespace srv = httpserver::server;
namespace {
class capture final : public httpserver::detail::exchange_sink {
 public:
    void on_respond(const http::status& status, const http::fields& fields) override {
        respond_code = status.code();
        headers = fields;
    }
    void on_admit(const httpserver::body_policy&) override {}
    void on_abort() override {}
    httpserver::websocket_upgrade_result on_upgrade(const httpserver::ws_upgrade_options&) override { return {}; }
    int respond_code = 0;
    http::fields headers;
};

struct result {
    int code;
    std::string length;
    std::string body;
    int calls;
    int completions;
};

result execute(http::protocol version, http::method_id method, bool coroutine,
               std::size_t body_size) {
    http::request_head head;
    head.request_protocol = version;
    head.request_method = http::method::known(method);
    head.route_path = coroutine ? "/echo" : "/hello";
    head.raw_target = head.route_path;
    capture decisions;
    httpserver_test::scripted_body_source input;
    httpserver_test::scripted_body_sink output;
    input.stage(std::vector<std::byte>(body_size, std::byte{'x'}));
    input.stage_end();
    httpserver::exchange exchange(head, &decisions, 0, &input, &output);
    int calls = 0;
    auto handler = coroutine ? srv::route_handler(v3_example::echo)
        : srv::make_sync_route([&calls](const http::request_head& request,
                                       std::span<const std::byte> body) {
              ++calls;
              return v3_example::hello(request, body);
          }, v3_example::body_cap);
    httpserver::manual_executor executor;
    int completions = 0;
    httpserver::spawn(executor, handler(exchange), [&](httpserver::task_result<void> done) {
        if (!done.is_exception()) ++completions;
    });
    for (int i = 0; i < 100 && completions == 0; ++i) executor.run_pending();
    output.drain(output.queued());
    const auto bytes = output.drained_bytes();
    std::string body;
    for (auto byte : bytes) body += static_cast<char>(byte);
    return {decisions.respond_code, std::string(decisions.headers.first("content-length").value_or("")),
            body, calls, completions};
}
}  // namespace

LT_BEGIN_SUITE(example_handlers_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(example_handlers_suite)

LT_BEGIN_AUTO_TEST(example_handlers_suite, shared_sync_handler_serves_all_semantic_protocols)
    for (auto version : {http::protocol::http_1_1, http::protocol::http_2, http::protocol::http_3}) {
        auto response = execute(version, http::method_id::get, false, 0);
        LT_CHECK_EQ(response.code, 200);
        LT_CHECK_EQ(response.length, "14");
        LT_CHECK_EQ(response.body, "hello from v3\n");
        LT_CHECK_EQ(response.calls, 1);
        LT_CHECK_EQ(response.completions, 1);
    }
LT_END_AUTO_TEST(shared_sync_handler_serves_all_semantic_protocols)

LT_BEGIN_AUTO_TEST(example_handlers_suite, head_retains_representation_length_without_body)
    for (auto version : {http::protocol::http_1_1, http::protocol::http_2, http::protocol::http_3}) {
        auto response = execute(version, http::method_id::head, false, 0);
        LT_CHECK_EQ(response.code, 200);
        LT_CHECK_EQ(response.length, "14");
        LT_CHECK(response.body.empty());
        LT_CHECK_EQ(response.calls, 1);
        LT_CHECK_EQ(response.completions, 1);
    }
LT_END_AUTO_TEST(head_retains_representation_length_without_body)

LT_BEGIN_AUTO_TEST(example_handlers_suite, coroutine_collects_at_cap_and_rejects_above_it)
    for (auto version : {http::protocol::http_1_1, http::protocol::http_2, http::protocol::http_3}) {
        auto accepted = execute(version, http::method_id::post, true, v3_example::body_cap);
        LT_CHECK_EQ(accepted.code, 200);
        LT_CHECK_EQ(accepted.length, "1024");
        LT_CHECK_EQ(accepted.body, std::string(1024, 'x'));
        LT_CHECK_EQ(accepted.completions, 1);
        auto refused = execute(version, http::method_id::post, true, v3_example::body_cap + 1);
        LT_CHECK_EQ(refused.code, 413);
        LT_CHECK(refused.body.empty());
        LT_CHECK_EQ(refused.completions, 1);
    }
LT_END_AUTO_TEST(coroutine_collects_at_cap_and_rejects_above_it)

LT_BEGIN_AUTO_TEST(example_handlers_suite, sync_body_cap_prevents_application_invocation)
    for (auto version : {http::protocol::http_1_1, http::protocol::http_2, http::protocol::http_3}) {
        auto refused = execute(version, http::method_id::get, false, v3_example::body_cap + 1);
        LT_CHECK_EQ(refused.code, 413);
        LT_CHECK_EQ(refused.calls, 0);
        LT_CHECK_EQ(refused.completions, 1);
    }
LT_END_AUTO_TEST(sync_body_cap_prevents_application_invocation)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
