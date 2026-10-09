/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_SUPPORT_HTTP3_NETWORK_OWNER_HPP_
#define TEST_SUPPORT_HTTP3_NETWORK_OWNER_HPP_
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "test/support/quic_interop_files.hpp"
#include <httpserver/detail/http3_request_engine.hpp>
#include <httpserver/detail/quic_connection_identity.hpp>
#include <httpserver/detail/quic_connection_lifecycle.hpp>
#include <httpserver/detail/quic_server_admission.hpp>
#include <httpserver/detail/quic_tls_session.hpp>
namespace h3net {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
using clock_type = std::chrono::steady_clock;
struct statistics {
    unsigned authenticated = 0, duplicates = 0, drops = 0, emitted = 0;
};
struct transmission {
    std::vector<std::byte> bytes;
    hd::datagram_endpoint peer;
};
// Test-only, bounded owner; all methods run in the fixture's serialization domain.
class connection final : public hd::datagram_sink {
 public:
    connection(hd::tls_credentials_selection credentials, hd::quic_admission_facts facts, const hs::route_registry& routes, httpserver::manual_executor& executor, std::uint64_t id,
               const hs::peer_policy& policy, const std::string& file_root = {}, hd::quic_key_observer observer = nullptr, void* observer_argument = nullptr);
    ~connection();
    void adopt_routes(hd::quic_datagram_dispatch& routes, hd::io_connection_owner::datagram_port owner, hd::quic_datagram_dispatch::cid_registration initial, std::weak_ptr<connection> self);
    void on_datagram(std::shared_ptr<const hd::io_datagram> packet) noexcept override;
    void tick(clock_type::time_point now);
    std::optional<transmission> prepare(clock_type::time_point now);
    void emitted(bool success, clock_type::time_point now);
    void emitted(hd::quic_identity_emission outcome, clock_type::time_point now);
    std::optional<clock_type::time_point> deadline() const;
    hs::resource_budget storage_budget() const { return root_; }
    hs::resource_budget application_storage_budget() const { return pool_.data().budget; }
    const statistics& stats() const { return stats_; }
    const std::string& failure() const { return failure_; }
    bool connected() const { return connected_; }
    void request_close(const hd::quic_close_frame& frame, clock_type::time_point now);
    hd::quic_lifecycle_state state() const { return lifecycle_.state(); }
    unsigned close_notifications() const { return close_notifications_; }
    bool has_keys(hd::quic_key_level level) const { return keys_.keys(level, hd::quic_key_direction::read) || keys_.keys(level, hd::quic_key_direction::write); }
    std::uint64_t current_stream() const { return current_stream_; }

 private:
    bool receive_packet(std::span<const std::byte> packet, const hd::quic_path_identity& path, std::size_t credit, bool& accounted, clock_type::time_point now);
    void apply(const hd::quic_frame& frame, hd::quic_pn_space space, clock_type::time_point now);
    std::optional<transmission> prepare_datagram(clock_type::time_point now, const hd::quic_cid& destination);
    std::optional<transmission> prepare_probe(clock_type::time_point now, const hd::quic_cid& destination);
    void drive_tls();
    bool active() const { return lifecycle_.state() == hd::quic_lifecycle_state::active; }
    void terminal_changed(clock_type::time_point now);
    void retire_terminal(clock_type::time_point now);
    void cache_close();
    std::optional<transmission> prepare_close(clock_type::time_point now);
    void admit_h3();
    hd::quic_stream_state& stream(std::uint64_t id);
    void drain_completions();
    void retain_output();
    hd::quic_recovery_environment environment() const;
    hd::quic_connection_lifecycle lifecycle_{std::chrono::milliseconds(15000), {}, clock_type::now()};
    unsigned close_notifications_ = 0;
    bool receiving_ = false, close_blocked_ = false;
    bool terminal_stopped_ = false, terminal_retired_ = false, close_pending_ = false;
    std::optional<transmission> cached_close_;
    hd::quic_admission_facts facts_;
    hs::resource_budget root_;
    hd::quic_storage_pool pool_;
    hd::quic_key_state keys_;
    hd::quic_recovery recovery_;
    std::unique_ptr<hd::quic_connection_identity> identity_;
    std::unique_ptr<hd::quic_tls_session> tls_;
    std::unique_ptr<hd::quic_flow_control> flow_;
    std::map<std::uint64_t, std::unique_ptr<hd::quic_stream_state>> streams_;
    std::unique_ptr<hd::http3_request_engine> engine_;
    std::unique_ptr<hqnet::files> files_;
    const hs::route_registry& routes_;
    httpserver::manual_executor& executor_;
    std::uint64_t id_, current_stream_ = 0;
    std::vector<std::byte> client_cid_, server_cid_, parameter_bytes_;
    hd::quic_transport_parameters local_;
    std::array<std::optional<std::uint64_t>, 3> largest_;
    std::array<std::uint64_t, 3> copied_{};
    struct crypto_prefix {
        std::uint64_t through = 0;
        bool acknowledged = false;
    };
    std::array<std::map<std::uint64_t, crypto_prefix>, 3> crypto_acknowledged_;
    std::array<std::uint64_t, 3> retired_{};
    std::array<unsigned, 3> probes_{};
    std::optional<hd::quic_send_plan> pending_;
    std::array<hd::quic_send_plan, 3> pending_plans_;
    std::array<hd::quic_datagram_emission, 3> pending_packets_;
    std::array<hd::quic_pn_space, 3> pending_spaces_;
    std::size_t pending_count_ = 0;
    bool pending_batch_ = false;
    std::optional<hd::quic_identity_control> pending_control_;
    std::shared_ptr<hd::quic_amplification_budget> pending_budget_;
    std::optional<hd::quic_amplification_budget::reservation> debit_;
    std::size_t pending_bytes_ = 0;
    bool connected_ = false, initial_delivered_ = false, initial_discarded_ = false;
    clock_type::time_point expires_ = clock_type::now() + std::chrono::seconds(15);
    statistics stats_;
    std::string failure_;
};
}  // namespace h3net
#endif  // TEST_SUPPORT_HTTP3_NETWORK_OWNER_HPP_
