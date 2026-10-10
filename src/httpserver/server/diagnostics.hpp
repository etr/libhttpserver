/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_HTTPSERVER_SERVER_DIAGNOSTICS_HPP_
#define SRC_HTTPSERVER_SERVER_DIAGNOSTICS_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/server/budgets.hpp>

namespace httpserver::detail {
struct diagnostics_access;
}
namespace httpserver::server {
enum class diagnostic_kind : std::uint8_t {
    connection_accepted,
    connection_rejected,
    connection_closed,
    stream_admitted,
    stream_completed,
    stream_cancelled,
    protocol_failure,
    tls_completed,
    tls_failed,
    resource_refused,
    flow_stalled,
    drain_started,
    drain_completed,
    drain_expired
};
inline constexpr std::size_t diagnostic_kind_count = 14;
enum class diagnostic_reason : std::uint8_t { none, peer_policy, capacity, malformed_input, peer_reset, local_stop, transport, handshake, deadline, flow_control };
// Scalar-only records deliberately cannot carry request or TLS identities.
struct diagnostic_event {
    diagnostic_kind kind = diagnostic_kind::connection_closed;
    diagnostic_reason reason = diagnostic_reason::none;
    http::outcome_code outcome = http::outcome_code::ok;
    std::optional<http::protocol> protocol = std::nullopt;  // absent for protocol-independent scopes
    resource resource_kind = resource::connections;  // relevant to resource_refused
    std::uint64_t connection_id = 0;
    std::uint64_t stream_id = 0;
    std::uint64_t listener_id = 0;
    std::uint64_t quantity = 0;
};
struct diagnostic_counters {
    std::array<std::uint64_t, diagnostic_kind_count> events{};
    std::uint64_t active_connections = 0;
    std::uint64_t active_streams = 0;
    // Charged retained storage and metadata, not just unsent payload bytes.
    std::uint64_t charged_response_queue_bytes = 0;
    std::uint64_t dropped_events = 0;
    std::uint64_t callback_failures = 0;
};
struct diagnostic_dispatch_result {
    http::outcome_code code = http::outcome_code::ok;
    std::size_t delivered = 0;
};
// Copyable shared channel. Default construction disables diagnostics. Callback
// captures must remain valid until the last dispatch; destruction never dispatches.
class diagnostics {
 public:
    using callback = concurrency::unique_function<void(const diagnostic_event&)>;
    static constexpr std::size_t queue_capacity = 128;
    diagnostics() noexcept = default;
    explicit diagnostics(callback callback);
    // Each field is atomic individually; concurrent snapshots are approximate.
    diagnostic_counters counters() const noexcept;
    // Runs callbacks on the caller's thread, outside engine/queue locks. Concurrent
    // or recursive dispatch returns invalid_state. Exceptions are counted/contained.
    // New events cannot extend this call beyond the queue size observed at entry.
    diagnostic_dispatch_result dispatch(std::size_t max_events = queue_capacity) const noexcept;

 private:
    friend struct httpserver::detail::diagnostics_access;
    struct state;
    std::shared_ptr<state> state_;
};
}  // namespace httpserver::server
#endif  // SRC_HTTPSERVER_SERVER_DIAGNOSTICS_HPP_
