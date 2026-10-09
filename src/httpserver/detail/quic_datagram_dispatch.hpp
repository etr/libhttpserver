/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_datagram_dispatch.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_DATAGRAM_DISPATCH_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_DATAGRAM_DISPATCH_HPP_
#include <utility>
#include <array>
#include <vector>
#include <cstddef>
#include <deque>
#include <unordered_set>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/quic_invariant_header.hpp>
namespace httpserver {
namespace detail {
enum class datagram_dispatch_code { routed, malformed, unknown, retired, queue_full };
struct datagram_dispatch_result {
    datagram_dispatch_code code;
    // Unrouted packets stay owned by the caller. No connection is allocated.
    std::shared_ptr<const io_datagram> packet;
};
class quic_datagram_dispatch final {
    struct route;
 public:
    class cid_registration {
     private:
        friend class quic_datagram_dispatch;
        cid_registration(std::string key, const std::shared_ptr<route>& route)
            : key_(std::move(key)), route_(route) { }
        std::string key_;
        std::weak_ptr<route> route_;
    };
    class reset_registration {
     private:
        friend class quic_datagram_dispatch;
        explicit reset_registration(const std::shared_ptr<route>& target) : route_(target) {}
        std::weak_ptr<route> route_;
    };
    std::optional<reset_registration> register_reset(const std::array<std::byte, 16>& token, const datagram_endpoint& remote,
        io_connection_owner::datagram_port owner, std::shared_ptr<datagram_sink> sink);
    bool remove(const reset_registration& registration);
    explicit quic_datagram_dispatch(std::optional<std::size_t> short_cid_length = std::nullopt,
                                    std::size_t max_cids = 4096);
    ~quic_datagram_dispatch();
    // Shared listeners require nonempty CIDs; a configured short-header CID
    // length also applies to registered routes.
    std::optional<cid_registration> register_cid(const quic_cid& cid,
        io_connection_owner::datagram_port owner, std::shared_ptr<datagram_sink> sink);
    bool remove(const cid_registration& registration);
    datagram_dispatch_result dispatch(std::shared_ptr<const io_datagram> packet) const;

 private:
    struct route {
        route(io_connection_owner::datagram_port target, std::shared_ptr<datagram_sink> sink)
            : owner(std::move(target)), delivery(std::make_shared<datagram_delivery>(std::move(sink))) { }
        io_connection_owner::datagram_port owner;
        std::shared_ptr<datagram_delivery> delivery;
    };
    static std::string key(const quic_cid& cid);
    std::shared_ptr<route> lookup_cid(const quic_cid& cid, datagram_dispatch_code& missing) const;
    std::shared_ptr<route> lookup_reset(const io_datagram& packet) const;
    const std::optional<std::size_t> short_cid_length_;
    const std::size_t max_cids_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<route>> routes_;
    // Bounded retirement history is separate from active admission. CIDs older
    // than this history return unknown, without exhausting a long-lived listener.
    std::unordered_set<std::string> retired_;
    std::deque<std::string> retired_order_;
    struct reset_route {
        std::array<std::byte, 16> token;
        datagram_endpoint remote;
        std::shared_ptr<route> target;
    };
    std::vector<reset_route> resets_;
};
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_DATAGRAM_DISPATCH_HPP_
