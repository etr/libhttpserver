/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http3_request_state.hpp>
#include <utility>

namespace httpserver::detail {

auto http3_request_engine::state::observe(server::diagnostic_kind kind, std::uint64_t stream_id, http::outcome_code code, server::diagnostic_reason reason, server::resource resource_kind) -> void {
    server::diagnostic_event event{kind, reason, code};
    event.protocol = http::protocol::http_3;
    event.connection_id = connection_id;
    event.stream_id = stream_id;
    event.resource_kind = resource_kind;
    diagnostics_access::record(dispatch_context.diagnostics, event);
}

auto http3_request_engine::state::terminal(record& r, bool complete, http::outcome_code code) -> void {
    if (!r.accepted || r.diagnostic_terminal) return;
    r.diagnostic_terminal = true;
    observe(complete ? server::diagnostic_kind::stream_completed : server::diagnostic_kind::stream_cancelled, r.transport->id(), code,
            complete ? server::diagnostic_reason::none : server::diagnostic_reason::local_stop);
}

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
http3_request_engine::state::state(quic_storage_lease d, quic_storage_lease c, quic_flow_control& f, quic_recovery& q, const server::route_registry& r, executor& e, http3_request_limits l,
                                   std::uint64_t id, net::peer_address p, request_dispatch_context context, http3_native_context n)
    : data(std::move(d)),
      critical(std::move(c)),
      flow(f),
      recovery(q),
      routes(r),
      dispatch_context(std::move(context)),
      limits(l),
      connection(data, critical, l.framing, n.workers != nullptr),
      handlers(std::make_shared<http3_request_helpers::route_executor>(e)),
      connection_id(id),
      peer(p),
      owner(e),
      native(std::move(n)),
      scope(std::make_shared<drain_scope>(dispatch_context.diagnostics)) {
    scope->enter();
}
// NOLINTEND

http3_request_engine::state::pump_guard::~pump_guard() {
    --owner.pumps;
    owner.reap();
}

}  // namespace httpserver::detail
