/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_server_admission.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_SERVER_ADMISSION_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_SERVER_ADMISSION_HPP_
#include <utility>
#include <vector>
#include <memory>
#include <httpserver/detail/quic_address_token.hpp>
#include <httpserver/detail/quic_datagram_dispatch.hpp>
namespace httpserver::detail {
struct quic_admission_limits {
    std::size_t cid_length = 8, max_pending = 64, max_retained_bytes = 1024 * 1024, max_routes = 4096;
    std::uint64_t pending_lifetime_seconds = 10, listener_id = 0;
    bool require_retry = true;
};
class quic_admission_reply final {
 public:
    const std::shared_ptr<const io_datagram>& packet() const { return packet_; }
    bool complete_send() { return budget_.complete_send(send_); }
    bool cancel_unsent() { return budget_.cancel_unsent(send_); }

 private:
    friend class quic_server_admission;
    quic_admission_reply(std::shared_ptr<const io_datagram> packet, quic_path_identity path, std::size_t received);
    std::shared_ptr<const io_datagram> packet_;
    quic_amplification_budget budget_;
    quic_amplification_budget::reservation send_;
};
struct quic_admission_facts {
    std::shared_ptr<const io_datagram> initial;
    std::shared_ptr<quic_amplification_budget> budget;
    quic_cid original_destination, destination;
};
enum class quic_admission_code { routed, dropped, reply, pending, capacity_exhausted, retired, queue_full };
// Caller serializes admission on the listener; promoted facts/accounting become
// connection-owner-affine. The sink accounts later routed datagrams once and
// creates independent unvalidated budgets for different canonical paths.
class quic_server_admission final {
    struct pending_entry;
 public:
    class pending_handle {
        friend class quic_server_admission;
        std::weak_ptr<pending_entry> entry_;
    };
    struct result {
        quic_admission_code code = quic_admission_code::dropped;
        pending_handle pending;
        std::shared_ptr<quic_admission_reply> reply;
    };
    struct promotion {
        quic_datagram_dispatch::cid_registration registration;
        quic_admission_facts facts;
    };
    explicit quic_server_admission(quic_admission_limits limits = {}, std::span<const std::byte> token_key = {});
    // Packet storage stays immutable; call once per received UDP datagram.
    // Time is caller-supplied monotonic seconds in the listener's token epoch.
    result receive(std::shared_ptr<const io_datagram> packet, std::uint64_t now);
    std::optional<quic_admission_facts> inspect(const pending_handle& handle) const;
    // Attach inspect(handle)'s facts to the sink before promotion: an inline
    // executor may deliver the retained Initial before this call returns.
    // That Initial's receive bytes are already credited in the shared budget.
    std::optional<promotion> promote(const pending_handle& handle, io_connection_owner::datagram_port owner,
        std::shared_ptr<datagram_sink> sink, std::uint64_t now);
    bool cancel(const pending_handle& handle);
    void expire(std::uint64_t now);
    std::size_t pending_count() const { return pending_.size(); }
    std::size_t retained_bytes() const { return retained_bytes_; }
    quic_datagram_dispatch& routes() { return routes_; }

 private:
    result receive_unknown(std::shared_ptr<const io_datagram> packet, const quic_path_identity& path, std::uint64_t now);
    result receive_initial(std::shared_ptr<const io_datagram> packet, const quic_path_identity& path,
        const quic_invariant_header& header, std::uint64_t now);
    result version_reply(const io_datagram& packet, const quic_invariant_header& header, const quic_path_identity& path);
    result retry_reply(const io_datagram& packet, const quic_invariant_header& header, const quic_path_identity& path, std::uint64_t now);
    result make_reply(const io_datagram& packet, const quic_path_identity& path, std::vector<std::byte> bytes);
    result retain(std::shared_ptr<const io_datagram> packet, const quic_path_identity& path,
        const quic_invariant_header& header, const std::optional<quic_token_claims>& claims, std::uint64_t now);
    const quic_admission_limits limits_;
    quic_datagram_dispatch routes_;
    quic_address_token tokens_;
    std::vector<std::shared_ptr<pending_entry>> pending_;
    std::size_t retained_bytes_ = 0;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_SERVER_ADMISSION_HPP_
