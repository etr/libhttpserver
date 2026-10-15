/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/server/route_sync.hpp>
#include <utility>

namespace httpserver::server::detail {

void pin_value_framing(http::fields& f, std::size_t body_size) {
    if (f.count("content-length") == 0 && f.count("transfer-encoding") == 0) {
        f.append("Content-Length", std::to_string(body_size));
    }
}

task<void> commit_sync_value(exchange& x, sync_response value) {
    if (!value.status.valid()) co_return;
    pin_value_framing(value.fields, value.body.size());
    if (value.body.empty() || x.head().request_method == http::method::known(http::method_id::head)) {
        static_cast<void>(x.respond(value.status, value.fields));
        co_return;
    }
    // A value knows its whole body: commit the head, then push the
    // bytes and the end through the writer.
    if (!x.start_response(value.status, value.fields).ok()) co_return;
    const body_write written = co_await x.writer().write(std::span<const std::byte>(value.body.data(), value.body.size()));
    if (!written.status.ok()) co_return;
    co_await x.writer().finish();
}

}  // namespace httpserver::server::detail

namespace httpserver::server {

route_handler make_sync_route(sync_route_handler handler, std::uint64_t body_cap) {
    return [call = std::move(handler), cap = body_cap](exchange& x) -> task<void> {
        // The admission carries the cap to the engine (the engine-side
        // staging bound is the bounded-admission task); a typed failure
        // here is a disconnect, so the route ends quietly.
        if (!x.admit_body(body_policy{cap}).ok()) co_return;
        const body_collect collected = co_await x.body().collect(cap);
        if (!collected.status.ok()) {
            // Over-cap: the handler never runs; the undrained remainder
            // is the engine's to settle. Every other failure (disconnect,
            // framing) ends without a local commit; the runner owns the
            // synthesis for the ones that need one.
            if (collected.status.code() == http::outcome_code::limit_exceeded) {
                static_cast<void>(x.respond(http::status::from_code(413), http::fields()));
            }
            co_return;
        }
        sync_response value = call(x.head(), std::span<const std::byte>(collected.data.data(), collected.data.size()));
        co_await detail::commit_sync_value(x, std::move(value));
    };
}

}  // namespace httpserver::server
