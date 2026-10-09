/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_connection_identity.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_CONNECTION_IDENTITY_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_CONNECTION_IDENTITY_HPP_
#include <functional>
#include <memory>
#include <httpserver/detail/quic_amplification.hpp>
#include <httpserver/detail/quic_datagram_dispatch.hpp>
#include <httpserver/detail/quic_recovery.hpp>
#include <httpserver/server/peer_policy.hpp>
namespace httpserver::detail {
enum class quic_identity_code { ok, dropped, policy_denied, protocol, capacity, no_memory, random_failure, busy };
struct quic_identity_result {
    quic_identity_code code = quic_identity_code::ok;
    bool allow_payload = false, activated = false;
    explicit operator bool() const { return code == quic_identity_code::ok; }
};
struct quic_authenticated_path {
    quic_path_identity path;
    quic_cid destination;
    std::uint64_t number = 0;
    bool fresh = false, authenticated = false;
    // Once per authenticated UDP datagram; zero for subsequent coalesced packets.
    std::size_t received_bytes = 0;
};
enum class quic_identity_emission { sent, unsent, ambiguous };
struct quic_identity_control {
    std::uint64_t token = 0;
    quic_send_plan packet;
    quic_path_identity path;
    quic_cid destination;
    std::shared_ptr<quic_amplification_budget> budget;
    std::array<std::byte, 8> data{};
    bool response = false;
    std::size_t wire_bytes = 0;
};
// Owner-affine; keys, recovery scheduling, streams and HTTP remain outside.
// Fixed records bound cumulative CID history as well as active paths and IDs.
class quic_connection_identity final {
 public:
    using time_point = quic_recovery::time_point;
    using random_fill = std::function<bool(std::span<std::byte>)>;
    static constexpr std::size_t max_active_cids = 4, max_cid_history = 32, max_pending_retirements = 8, max_paths = 3;
    quic_connection_identity(quic_cid local, quic_cid peer, std::shared_ptr<quic_amplification_budget> admitted, quic_recovery& recovery, const server::peer_policy& policy, random_fill random,
                             quic_storage_lease storage);
    ~quic_connection_identity();
    quic_connection_identity(const quic_connection_identity&) = delete;
    quic_connection_identity& operator=(const quic_connection_identity&) = delete;
    // Promotion owns the initial registration until this explicit adoption.
    quic_identity_result attach_routes(quic_datagram_dispatch& routes, io_connection_owner::datagram_port owner, std::shared_ptr<datagram_sink> weak_forwarder,
                                       quic_datagram_dispatch::cid_registration initial);
    // Only possibly emitted CIDs authorize resets on that remote endpoint.
    quic_identity_result peer_cid_emitted(const quic_cid& cid, const datagram_endpoint& remote, quic_identity_emission outcome);
    bool matches_reset(std::span<const std::byte> datagram, const datagram_endpoint& remote) const;
    void stop();
    void detach_routes();
    void set_peer_active_limit(std::uint64_t limit);
    quic_identity_result issue_cid();
    quic_identity_result receive(const quic_authenticated_path& packet, std::span<const quic_frame> frames, time_point now);
    std::optional<quic_identity_control> prepare_control(time_point now, std::size_t protection_overhead_without_cid);
    // Owns both recovery actual-send commit and path-specific debit completion.
    quic_identity_result complete_control(std::uint64_t token, quic_identity_emission outcome, time_point now, std::uint64_t key_generation = 0);
    void information_completed(const quic_information_completion& completion);
    void expire(time_point now);
    std::optional<time_point> deadline() const;
    const quic_path_identity& application_path() const;
    std::shared_ptr<quic_amplification_budget> application_budget() const;
    quic_cid application_cid() const;
    std::size_t active_peer_cids() const;
    std::size_t path_count() const;
    std::uint64_t retirement_floor() const;

 private:
    struct state;
    std::unique_ptr<state> state_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_CONNECTION_IDENTITY_HPP_
