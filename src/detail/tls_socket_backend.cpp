/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/tls_socket_backend.hpp>

#include <memory>
#include <stdexcept>
#include <utility>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/tls_credentials.hpp>

namespace httpserver::detail {
tls_socket_backend::tls_socket_backend(io_socket_backend& raw, executor& ex, std::uint64_t connection,
                                       std::shared_ptr<const tls_context> context, tls_handshake_context handshake)
    : raw_(raw), connection_(connection), tls_(raw, ex, connection, std::move(context), true, handshake, tls_read_cancel_policy::preserve_session) {}

tls_socket_backend::tls_socket_backend(io_socket_backend& raw, executor& ex, std::uint64_t connection,
                                       tls_credentials_selection selection, tls_handshake_context handshake)
    : raw_(raw), connection_(connection), tls_(raw, ex, connection, std::move(selection), true, handshake, tls_read_cancel_policy::preserve_session) {}

tls_socket_backend::~tls_socket_backend() { close(); }

io_backend& tls_socket_backend::operation_backend(const op_state& op) {
    auto kind = op.kind();
    if (kind == io_op_kind::cancel) {
        const auto& target = std::get<cancel_payload>(op.payload()).target;
        if (target) kind = target->kind();
    }
    if (kind == io_op_kind::timer || kind == io_op_kind::wake) return raw_;
    return tls_;
}

void tls_socket_backend::submit(op_state& op) {
    const auto* target = op.kind() == io_op_kind::cancel
        ? std::get<cancel_payload>(op.payload()).target.get() : nullptr;
    const bool foreign = op.connection() != connection_ || (target && target->connection() != connection_);
    if (foreign || released_.load(std::memory_order_acquire)) {
        if (op.claim_terminal()) {
            op.owner()->enqueue(op.shared_from_this(), {foreign
                ? http::outcome_code::invalid_argument : http::outcome_code::connection_closed});
        }
        return;
    }
    operation_backend(op).submit(op);
}

http::outcome_code tls_socket_backend::request_cancel(op_state& op) {
    if (op.connection() != connection_) return http::outcome_code::invalid_argument;
    return operation_backend(op).request_cancel(op);
}

http::outcome tls_socket_backend::ready() const { return raw_.ready(); }
void tls_socket_backend::activate_external() { raw_.activate_external(); }
void tls_socket_backend::adopt_connection(std::uint64_t, pollsys::native_socket_t) {
    throw std::logic_error("TLS socket view requires an already adopted connection");
}
void tls_socket_backend::adopt_datagram(std::uint64_t, pollsys::native_socket_t) {
    throw std::logic_error("TLS socket view does not own datagrams");
}
void tls_socket_backend::adopt_listener(std::uint64_t, pollsys::native_socket_t) {
    throw std::logic_error("TLS socket view does not own listeners");
}
pollsys::native_socket_t tls_socket_backend::native_handle(std::uint64_t id) const {
    return id == connection_ ? raw_.native_handle(id) : pollsys::k_invalid_socket;
}
void tls_socket_backend::release_connection(std::uint64_t id) {
    if (id == connection_) close();
}
std::size_t tls_socket_backend::wake() { return raw_.wake(); }
std::size_t tls_socket_backend::close() {
    if (released_.exchange(true, std::memory_order_acq_rel)) return 0;
    tls_.close();
    raw_.release_connection(connection_);
    return 1;
}
server::interest_snapshot tls_socket_backend::interests() const { return raw_.interests(); }
http::outcome tls_socket_backend::dispatch(std::span<const server::readiness_event> events,
                                          std::chrono::steady_clock::time_point now) {
    return raw_.dispatch(events, now);
}
std::shared_ptr<const server::tls_peer_metadata> tls_socket_backend::peer_metadata() const { return tls_.peer_metadata(); }
tls_negotiated_protocol tls_socket_backend::negotiated_protocol() const { return tls_.negotiated_protocol(); }
}  // namespace httpserver::detail
