/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#ifndef TEST_PARITY_V3_DISPATCH_REPLAY_HPP_
#define TEST_PARITY_V3_DISPATCH_REPLAY_HPP_

// TASK-118 step 6 (plan D6): the shared dispatcher-level replay driver
// of the routing and hooks corpus suites -- the TASK-117 shared-driver
// lesson, so the two suites are not copies. One corpus case's request
// is dispatched through the REAL registry + hook bus + dispatcher
// (dispatch_request) on a manual executor; the committed response is
// framed by the REAL http1_response_framer and parsed back by the
// parity response-frame parser, exactly the replay level the corpus
// pins. The v2 fixture's routing_basic / routing_hooks profiles are
// rebuilt here as v3 registrations; socket-level truth lives in the
// native_http1_e2e suite.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/detail/http1_response_mode.hpp>
#include <httpserver/detail/lifecycle_sink.hpp>
#include <httpserver/detail/request_lifecycle.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/server/hooks.hpp>
#include <httpserver/server/routes.hpp>
#include <httpserver/server/route_sync.hpp>

#include "./case_wire.hpp"
#include "./transcript.hpp"

namespace parity {

namespace v3_replay {

namespace srv = httpserver::server;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;

// The framing engine-sink stand-in: the committed head and the body
// bytes run through the REAL framer, so the produced wire bytes are
// what the HTTP/1 engine emits (the TASK-114/116/117 convention).
class framing_sink final : public httpserver::detail::exchange_sink {
 public:
    explicit framing_sink(const http::request_head& head) : head_(head) { }

    void on_admit(const httpserver::body_policy&) override { }

    void on_respond(const http::status& s, const http::fields& f) override {
        static_cast<void>(framer_.start_head(wire, head_, s, f));
        committed_status_ = s;
        committed_fields_ = f;
    }

    void on_upgrade(const httpserver::ws_upgrade_options&) override { }

    void on_abort() override { }

    // The writer-side framing seams (the body sink forwards here).
    http::outcome frame_body(std::span<const std::byte> from) {
        constexpr std::size_t unlimited = static_cast<std::size_t>(-1);
        return framer_.push_body(wire, from, unlimited);
    }

    http::outcome frame_end(const http::fields& trailers) {
        return framer_.finish_body(wire, trailers);
    }

    std::string wire;

    // The committed head inputs (the keep-alive verdict recomputes the
    // engine's decision from them).
    const http::status& committed_status() const noexcept {
        return committed_status_;
    }

    const http::fields& committed_fields() const noexcept {
        return committed_fields_;
    }

 private:
    const http::request_head& head_;
    httpserver::detail::http1_response_framer framer_;
    http::status committed_status_;
    http::fields committed_fields_;
};

// The body sink between the exchange writer and the framer.
class framing_body_sink final : public httpserver::detail::body_sink {
 public:
    explicit framing_body_sink(framing_sink& inner) : inner_(inner) { }

    httpserver::detail::body_push_result push(
            std::span<const std::byte> from) override {
        const http::outcome pushed = inner_.frame_body(from);
        httpserver::detail::body_push_result out;
        if (pushed.ok()) {
            out.kind = httpserver::detail::body_push::accepted;
            out.copied = from.size();
        } else {
            out.kind = httpserver::detail::body_push::failed;
        }
        return out;
    }

    httpserver::detail::body_push_result push_end(
            const http::fields& trailers) override {
        const http::outcome ended = inner_.frame_end(trailers);
        httpserver::detail::body_push_result out;
        out.kind = ended.ok() ? httpserver::detail::body_push::accepted
                              : httpserver::detail::body_push::failed;
        return out;
    }

    const http::outcome& failure() const noexcept override {
        return ok_failure_;
    }

    void park(httpserver::detail::body_write_wait&) override { }
    void unpark(httpserver::detail::body_write_wait&) override { }

 private:
    inline static const http::outcome ok_failure_ = http::outcome::okay();
    framing_sink& inner_;
};

// One replayed request: the parsed head plus its raw body.
struct replay_request {
    http::request_head head;
    std::string body;
};

// The outcome of one replayed dispatch: the wire bytes and the
// keep-alive verdict of the committed response.
struct replay_result {
    std::string wire;
    bool keep_alive = false;
};

// A value-shaped route committing @p body with the pinned framing
// (Content-Type then Content-Length, the corpus pin order).
inline srv::route_handler text_route(const std::string& body) {
    return srv::route_handler([out = body](exchange& x) -> task<void> {
        srv::sync_response value;
        value.status = http::status::from_code(200);
        value.fields.append("Content-Type", "text/plain");
        value.fields.append("Content-Length", std::to_string(out.size()));
        const std::byte* raw = reinterpret_cast<const std::byte*>(out.data());
        value.body.assign(raw, raw + out.size());
        co_await srv::detail::commit_sync_value(x, std::move(value));
    });
}

// The parameterized route echoing its captures as name=value pairs
// (the v2 fixture's /params/{id}/name/{name} shape).
inline srv::route_handler params_route() {
    return srv::route_handler([](exchange& x) -> task<void> {
        std::string body;
        for (const srv::route_captures& arg : x.path_args()) {
            if (!body.empty()) body.append(";");
            body.append(arg.name).append("=").append(arg.value);
        }
        srv::sync_response value;
        value.status = http::status::from_code(200);
        value.fields.append("Content-Type", "text/plain");
        value.fields.append("Content-Length", std::to_string(body.size()));
        const std::byte* raw =
            reinterpret_cast<const std::byte*>(body.data());
        value.body.assign(raw, raw + body.size());
        co_await srv::detail::commit_sync_value(x, std::move(value));
    });
}

// routing_basic: exact, parameterized, method-set routes and the
// 404/405 defaults (routing.tseq).
inline srv::route_registry build_routing_basic(
        const srv::resource_budget& budget) {
    srv::route_registry registry;
    static_cast<void>(srv::route_registry::create(budget, registry));
    const http::method get = http::method::known(http::method_id::get);
    http::method_set get_head;
    get_head.set(http::method_id::get);
    get_head.set(http::method_id::head);
    static_cast<void>(registry.route(get, "/hello", text_route("OK")));
    static_cast<void>(
        registry.route(get, "/params/{id}/name/{name}", params_route()));
    static_cast<void>(
        registry.route(get_head, "/both", text_route("both-ok")));
    static_cast<void>(registry.route(get, "/get_only", text_route("get-only")));
    return registry;
}

// routing_hooks (hooks.tseq): the before_handler short-circuit on
// DELETE /admin and the after_handler header mutation, both on the
// bus; the custom 404/405 pages arrive as the factories (the v2
// aliases' equivalents -- v2 owns the status, Allow still rides).
inline void install_routing_hooks(srv::hook_bus& bus) {
    (void)bus.add<srv::hook_phase::before_handler>(
        [](srv::before_handler_ctx& ctx) -> srv::hook_action {
            if (ctx.request.route_path != "/admin") {
                return srv::hook_action::pass();
            }
            srv::hook_response page;
            page.status = http::status::from_code(403);
            page.fields.append("Content-Type", "text/plain");
            const char* body = "hooked403";
            const std::byte* raw =
                reinterpret_cast<const std::byte*>(body);
            page.body.assign(raw, raw + 9);
            return srv::hook_action::respond_with(std::move(page));
        }).detach();
    (void)bus.add<srv::hook_phase::after_handler>(
        [](srv::after_handler_ctx& ctx) -> srv::hook_action {
            ctx.fields.append("X-Hook", "after");
            return srv::hook_action::pass();
        }).detach();
}

inline httpserver::detail::error_page_factories custom_hook_pages() {
    httpserver::detail::error_page_factories pages;
    pages.not_found =
        std::make_shared<const srv::server_options::response_factory>(
            [](const http::request_head&) -> srv::hook_response {
                srv::hook_response page;
                page.status = http::status::from_code(404);
                page.fields.append("Content-Type", "text/plain");
                const char* body = "custom-not-found";
                const std::byte* raw =
                    reinterpret_cast<const std::byte*>(body);
                page.body.assign(raw, raw + 16);
                return page;
            });
    pages.method_not_allowed =
        std::make_shared<const srv::server_options::response_factory>(
            [](const http::request_head&) -> srv::hook_response {
                srv::hook_response page;
                page.status = http::status::from_code(405);
                page.fields.append("Content-Type", "text/plain");
                const char* body = "custom-not-allowed";
                const std::byte* raw =
                    reinterpret_cast<const std::byte*>(body);
                page.body.assign(raw, raw + 18);
                return page;
            });
    return pages;
}

// Dispatches one request through the real pipeline and frames the
// committed response; delivered=false on a dispatcher failure.
inline replay_result dispatch_once(const srv::route_registry& registry,
                                   const srv::hook_bus& bus,
                                   const httpserver::detail::
                                       error_page_factories& pages,
                                   const replay_request& request,
                                   bool* delivered) {
    replay_result out;
    framing_sink sink(request.head);
    framing_body_sink body(sink);
    httpserver::detail::lifecycle_sink interceptor(sink, bus, request.head);
    exchange x(request.head, &interceptor, 0, nullptr, &body);
    manual_executor ex;
    bool ok = false;
    spawn(ex,
          httpserver::detail::dispatch_request(
              registry, bus, pages, interceptor, x),
          [&ok](task_result<void> r) { ok = !r.is_exception(); });
    while (ex.run_pending() > 0) {
    }
    if (delivered != nullptr) *delivered = ok;
    out.wire = sink.wire;
    const httpserver::detail::http1_response_mode mode =
        httpserver::detail::http1_response_mode::compute(
            request.head, sink.committed_status(),
            sink.committed_fields());
    out.keep_alive =
        httpserver::detail::http1_response_keepalive(
            request.head, mode.kind, mode.close_policy)
        == httpserver::detail::http1_keepalive::keep_alive;
    return out;
}

// Rebuilds the case's request list from its send segments: the
// blank-line segment ends one request head (a pipelined case carries
// two); leftover segments after a body-less head belong to the next
// request.
inline std::vector<replay_request> requests_of(const tcase& c) {
    std::vector<replay_request> out;
    tcase pending;
    const auto flush = [&out](tcase& built) {
        const corpus_request parsed = parse_case(built);
        replay_request request;
        request.head = std::move(parsed.head);
        request.body = std::move(parsed.body);
        out.push_back(std::move(request));
    };
    for (const send_segment& seg : c.sends) {
        pending.sends.push_back(seg);
        if (seg.bytes != "\r\n") continue;
        flush(pending);
        pending = tcase{};
    }
    if (!pending.sends.empty()) flush(pending);
    return out;
}

}  // namespace v3_replay

}  // namespace parity

#endif  // TEST_PARITY_V3_DISPATCH_REPLAY_HPP_
