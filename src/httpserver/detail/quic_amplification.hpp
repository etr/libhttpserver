/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_amplification.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_AMPLIFICATION_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_AMPLIFICATION_HPP_
#include <array>
#include <memory>
#include <optional>
#include <httpserver/detail/io_datagram.hpp>
namespace httpserver::detail {
struct quic_path_identity {
    datagram_endpoint peer;
    std::optional<datagram_endpoint> local;
    std::optional<std::uint32_t> interface_index;
    std::uint64_t socket_id = 0;
    bool operator==(const quic_path_identity&) const = default;
};
std::optional<quic_path_identity> quic_datagram_path(const io_datagram& packet);
// Serialized by the listener before promotion, then by the connection owner.
// A new path requires a new budget. Never copy validation or credit across paths.
class quic_amplification_budget final {
    struct state;
 public:
    class reservation {
        friend class quic_amplification_budget;
        std::weak_ptr<state> owner_;
        std::uint64_t generation_ = 0;
    };
    explicit quic_amplification_budget(quic_path_identity path);
    quic_amplification_budget(const quic_amplification_budget&) = delete;
    quic_amplification_budget& operator=(const quic_amplification_budget&) = delete;
    void receive_datagram(std::size_t bytes);
    std::optional<reservation> reserve_send(std::size_t bytes);
    bool complete_send(const reservation& send);
    // Only call when the transport proves no bytes were sent. Ambiguous
    // completions use complete_send and retain the entire debit.
    bool cancel_unsent(const reservation& send);
    void mark_address_validated();
    bool validated() const;
    const quic_path_identity& path() const;
    std::uint64_t received() const;
    std::uint64_t debited() const;

 private:
    bool consume(const reservation& send, bool refund);
    std::shared_ptr<state> state_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_AMPLIFICATION_HPP_
