/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_network_connection.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_NETWORK_CONNECTION_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_NETWORK_CONNECTION_HPP_
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <httpserver/detail/http3_request_engine.hpp>
#include <httpserver/detail/quic_connection_identity.hpp>
#include <httpserver/detail/quic_datagram_sizing.hpp>
#include <httpserver/detail/quic_connection_lifecycle.hpp>
#include <httpserver/detail/quic_server_admission.hpp>
#include <httpserver/detail/quic_tls_session.hpp>
namespace httpserver::detail {
struct quic_network_config {
    server::timeout_options timeouts;
    std::size_t data_bytes = 393216, critical_bytes = 393216;
    std::size_t max_requests = 16, max_stream_records = 64;
    std::uint16_t port = 0;
};
struct quic_transmission { std::vector<std::byte> bytes; datagram_endpoint peer; };
// Protocol state belongs to the serialized UDP owner. Pending preparations
// remain immutable until the listener reports the actual atomic send outcome.
class quic_network_connection final : public datagram_sink, public std::enable_shared_from_this<quic_network_connection> {
 public:
    using clock_type = std::chrono::steady_clock;
    quic_network_connection(tls_credentials_selection credentials, quic_admission_facts facts, const server::route_registry& routes,
        executor& wire, std::uint64_t id, const server::peer_policy& policy, server::resource_budget budget,
        quic_network_config config, request_dispatch_context dispatch, http3_native_context native);
    ~quic_network_connection();
    std::shared_ptr<datagram_sink> route_sink();
    void adopt_routes(quic_datagram_dispatch& routes, io_connection_owner::datagram_port owner,
                      quic_datagram_dispatch::cid_registration initial);
    void on_datagram(std::shared_ptr<const io_datagram> packet) noexcept override;
    void tick(clock_type::time_point now);
    std::optional<quic_transmission> prepare(clock_type::time_point now);
    void emitted(quic_identity_emission outcome, clock_type::time_point now);
    void emitted(bool success, clock_type::time_point now);
    std::optional<clock_type::time_point> deadline() const;
    void request_close(const quic_close_frame& frame, clock_type::time_point now);
    bool retired() const;
    bool connected() const { return connected_; }
    bool terminal() const { return !active(); }
    bool pending() const { return pending_ || pending_control_ || close_pending_; }
    void drain(clock_type::time_point deadline);

 private:
    bool credit_datagram(const io_datagram&, const quic_path_identity&);
    void signal_progress();
    void queue_datagram(std::shared_ptr<const io_datagram>, clock_type::time_point);
    void receive_datagram(const io_datagram&, const quic_path_identity&, bool&, clock_type::time_point);
    void inspect_reset(const io_datagram&, clock_type::time_point);
    bool retire_application();
    bool close_key_available(unsigned) const;
    std::optional<quic_datagram_packet> close_packet(unsigned, std::span<std::byte>, const quic_cid&);
    bool consume_close(std::span<const quic_frame>, clock_type::time_point);
    bool accept_identity(std::span<const quic_frame>, const quic_path_identity&, const quic_cid&, std::uint64_t, std::size_t, bool&, clock_type::time_point);
    bool process_frames(std::span<const quic_frame>, quic_pn_space, std::uint64_t, bool, const quic_path_identity&, const quic_cid&, std::size_t, bool&, clock_type::time_point);
    void apply_frames(std::span<const quic_frame>, quic_pn_space, clock_type::time_point);
    void commit_received(quic_pn_space, std::uint64_t, bool, clock_type::time_point);
    bool receive_packet(std::span<const std::byte>, const quic_path_identity&, std::size_t, bool&, clock_type::time_point);
    void apply(const quic_frame&, quic_pn_space, clock_type::time_point);
    std::optional<quic_transmission> prepare_datagram(clock_type::time_point, const quic_cid&);
    std::optional<quic_transmission> prepare_probe(clock_type::time_point, const quic_cid&);
    void complete_close(quic_identity_emission, clock_type::time_point);
    void complete_control(quic_identity_emission, clock_type::time_point);
    void record_plan_sent(const quic_send_plan&, quic_pn_space, clock_type::time_point);
    quic_identity_result commit_prepared(quic_identity_emission, clock_type::time_point);
    void abandon_prepared();
    void apply_ack(const quic_ack_frame&, quic_pn_space, clock_type::time_point);
    void apply_reset(const quic_reset_stream_frame&);
    void retire_crypto_prefix(const quic_information_completion&);
    void retain_crypto_output();
    void receive_queued_datagrams();
    struct datagram_preparation {
        std::array<std::array<std::byte, 1200>, 3> plain{};
        std::array<quic_datagram_packet, 3> packets{};
        std::array<quic_send_plan, 3> plans{};
        std::array<quic_pn_space, 3> spaces{};
        std::size_t count = 0;
    };
    quic_packet_write packet_header(unsigned, const quic_cid&) const;
    void prepare_packets(datagram_preparation&, clock_type::time_point, const quic_cid&);
    quic_transmission protect_datagram(datagram_preparation&);
    void drive_tls();
    bool active() const { return lifecycle_.state() == quic_lifecycle_state::active; }
    void check_drain(clock_type::time_point);
    void terminal_changed(clock_type::time_point);
    void retire_terminal(clock_type::time_point);
    void cache_close();
    std::optional<quic_transmission> prepare_close(clock_type::time_point);
    void admit_h3();
    quic_stream_state& stream(std::uint64_t);
    void drain_completions();
    void retain_output();
    quic_recovery_environment environment() const;
    quic_network_config config_;
    quic_connection_lifecycle lifecycle_;
    bool receiving_ = false, close_blocked_ = false;
    bool terminal_stopped_ = false, terminal_retired_ = false, close_pending_ = false;
    std::optional<quic_transmission> cached_close_;
    quic_admission_facts facts_;
    server::resource_budget root_;
    quic_storage_pool pool_;
    quic_key_state keys_;
    quic_recovery recovery_;
    std::unique_ptr<quic_connection_identity> identity_;
    std::unique_ptr<quic_tls_session> tls_;
    std::unique_ptr<quic_flow_control> flow_;
    std::map<std::uint64_t, std::unique_ptr<quic_stream_state>> streams_;
    std::unique_ptr<http3_request_engine> engine_;
    const server::route_registry& routes_;
    executor& executor_;
    request_dispatch_context dispatch_;
    http3_native_context native_;
    std::uint64_t id_, current_stream_ = 0;
    std::vector<std::byte> client_cid_, server_cid_, parameter_bytes_;
    quic_transport_parameters local_;
    std::array<std::optional<std::uint64_t>, 3> largest_;
    std::array<std::uint64_t, 3> copied_{};
    struct crypto_prefix { std::uint64_t through = 0; bool acknowledged = false; };
    std::array<std::map<std::uint64_t, crypto_prefix>, 3> crypto_acknowledged_;
    std::array<std::uint64_t, 3> retired_{};
    std::array<unsigned, 3> probes_{};
    std::optional<quic_send_plan> pending_;
    std::array<quic_send_plan, 3> pending_plans_;
    std::array<quic_datagram_emission, 3> pending_packets_;
    std::array<quic_pn_space, 3> pending_spaces_;
    std::size_t pending_count_ = 0;
    bool pending_batch_ = false;
    std::optional<quic_identity_control> pending_control_;
    std::shared_ptr<quic_amplification_budget> pending_budget_;
    std::optional<quic_amplification_budget::reservation> debit_;
    std::size_t pending_bytes_ = 0;
    bool connected_ = false, initial_delivered_ = false, initial_discarded_ = false, failed_ = false;
    struct queued_packet { std::shared_ptr<const io_datagram> packet; server::reservation storage; };
    std::deque<queued_packet> received_;
    std::size_t received_bytes_ = 0;
    clock_type::time_point handshake_deadline_;
    std::optional<clock_type::time_point> drain_requested_;
    server::drain_ticket drain_ticket_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_NETWORK_CONNECTION_HPP_
