/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "tls_credentials.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_HPP_
#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <httpserver/detail/tls_io_backend.hpp>
#include <httpserver/detail/tls_psk.hpp>
#include <httpserver/server/options.hpp>
namespace httpserver::detail {
// Shared canonical DNS validation for configuration and current ClientHello.
std::string canonical_tls_host(std::string host);
struct tls_host_credentials {
    std::string host;
    std::string certificate_chain_pem;
    std::string private_key_pem;
    std::string trust_roots_pem;
    std::vector<std::string> alpn;
    server::tls_profile profile = server::tls_profile::certificates;
    server::tls_client_auth_options client_auth;
    std::optional<tls_psk_config> psk;
};
// Caller supplies the expected authorization digest and a future deadline
// no later than the certificate expiry. PEM input is never retained.
struct tls_acme_challenge {
    std::string host;
    std::string certificate_pem;
    std::string private_key_pem;
    std::array<std::byte, 32> key_authorization_sha256{};
    std::chrono::system_clock::time_point expires_at;
};
struct tls_credentials_config {
    std::vector<tls_host_credentials> hosts;
    std::size_t default_host = 0;
};
struct tls_host_metadata {
    std::string host;
    std::vector<std::string> alpn;
    std::vector<unsigned char> alpn_wire;
    server::tls_profile profile;
    server::tls_client_certificate_mode client_certificate_mode = server::tls_client_certificate_mode::none;
};
class tls_credentials_snapshot;
// Acquiring once pins the whole generation before SSL_new. Keep this owner
// through session teardown and any retiring adapter child completions.
struct tls_credentials_selection {
    std::shared_ptr<const tls_credentials_snapshot> snapshot;
    std::shared_ptr<const tls_context> context;
};
class tls_credentials_snapshot final : public std::enable_shared_from_this<tls_credentials_snapshot> {
 public:
    std::uint64_t generation() const noexcept { return generation_; }
    std::size_t default_host() const noexcept { return default_host_; }
    const std::vector<tls_host_metadata>& hosts() const noexcept { return hosts_; }
    tls_credentials_selection select(std::size_t host) const;
    // Exact canonical lookup; expiry also applies to previously acquired snapshots.
    std::optional<tls_credentials_selection> select_acme(std::string_view canonical_host, std::chrono::system_clock::time_point now) const;
    tls_credentials_selection select_default() const { return select(default_host_); }

 private:
    friend class tls_credentials_registry;
    struct acme_entry {
        std::shared_ptr<const tls_context> context;
        std::chrono::system_clock::time_point expires_at;
    };
    std::map<std::string, acme_entry, std::less<>> challenges_;
    std::uint64_t generation_ = 0;
    std::size_t default_host_ = 0;
    std::vector<tls_host_metadata> hosts_;
    std::vector<std::shared_ptr<const tls_context>> contexts_;
};
// Full candidates are built off path. Only successful publications consume an
// ID, serialized with other writers. Readers use no application mutex;
// shared_ptr atomic operations need not be lock-free. Published data never mutates.
class tls_credentials_registry final {
 public:
    http::outcome replace(const tls_credentials_config& config);
    http::outcome publish_acme(const tls_acme_challenge& input);
    http::outcome remove_acme(std::string_view host);
    std::shared_ptr<const tls_credentials_snapshot> acquire() const { return std::atomic_load_explicit(&active_, std::memory_order_acquire); }

 private:
    http::outcome publish_locked(std::shared_ptr<tls_credentials_snapshot> candidate,
        std::shared_ptr<const tls_credentials_snapshot>& retired);
    std::shared_ptr<const tls_credentials_snapshot> active_;
    std::mutex publication_;
    std::uint64_t generation_ = 0;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_CREDENTIALS_HPP_
