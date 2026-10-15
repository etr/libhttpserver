/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/lifecycle_sink.hpp>
#include <memory>
#include <utility>

namespace httpserver::detail {

lifecycle_sink::lifecycle_sink(exchange_sink& inner, const server::hook_bus& bus, const http::request_head& head, const bool* engine_refusal,
                               std::shared_ptr<const server::tls_peer_metadata> tls) noexcept
    : inner_(inner), bus_(bus), head_(head), engine_refusal_(engine_refusal), tls_(std::move(tls)) {}

auto lifecycle_sink::begin_completion() noexcept -> bool {
    if (completed_) return false;
    completed_ = true;
    return true;
}

auto lifecycle_sink::on_upgrade(const ws_upgrade_options& options) -> websocket_upgrade_result {
    // No response head is queued on the upgrade path: neither
    // after_handler nor response_sent fires here (the ws milestone
    // owns the session lifecycle).
    return inner_.on_upgrade(options);
}

auto lifecycle_sink::on_abort() -> void {
    aborted_ = true;
    inner_.on_abort();
}

auto lifecycle_sink::commit(const http::status& s, const http::fields& f, bool streaming) -> void {
    // Mutable copies exist only when after_handler can run.
    if (next_ != provenance::pre_handler && bus_.any_hooks(server::hook_phase::after_handler)) {
        http::status final_status = s;
        http::fields final_fields = f;
        fire_after(final_status, final_fields);
        forward(final_status, final_fields, streaming);
    } else {
        forward(s, f, streaming);
    }
}

auto lifecycle_sink::forward(const http::status& s, const http::fields& f, bool streaming) -> void {
    if (streaming)
        inner_.on_start_response(s, f);
    else
        inner_.on_respond(s, f);
    if (engine_refusal_ != nullptr && *engine_refusal_) {
        refused_ = true;
        return;
    }
    responded_ = true;
    status_ = s.code();
    fire_sent();
}

auto lifecycle_sink::fire_after(http::status& s, http::fields& f) const -> void {
    server::after_handler_ctx ctx{head_, s, std::move(f), tls_};
    // The action is ignored by contract: after_handler mutates,
    // full replacement is not offered (the v2 delta).
    (void)bus_.fire<server::hook_phase::after_handler>(ctx);
    s = ctx.status;
    f = std::move(ctx.fields);
}

auto lifecycle_sink::fire_sent() const -> void {
    if (!bus_.any_hooks(server::hook_phase::response_sent)) return;
    server::response_sent_ctx ctx{head_, status_, tls_};
    (void)bus_.fire<server::hook_phase::response_sent>(ctx);
}

}  // namespace httpserver::detail
