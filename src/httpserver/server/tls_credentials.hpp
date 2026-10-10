/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef SRC_HTTPSERVER_SERVER_TLS_CREDENTIALS_HPP_
#define SRC_HTTPSERVER_SERVER_TLS_CREDENTIALS_HPP_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <memory>
#include <string>
#include <vector>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/options.hpp>

namespace httpserver::detail { class tls_credentials_access; }
namespace httpserver::server {
// Move-only secure storage. Overwrite/destruction cleanse bytes; observation is
// read-only and lasts until the next move, clear or destruction.
class tls_psk_key final {
 public:
    tls_psk_key() noexcept;
    explicit tls_psk_key(std::span<const std::byte> bytes);
    ~tls_psk_key();
    tls_psk_key(tls_psk_key&&) noexcept;
    tls_psk_key& operator=(tls_psk_key&&) noexcept;
    tls_psk_key(const tls_psk_key&) = delete;
    tls_psk_key& operator=(const tls_psk_key&) = delete;
    std::span<const std::byte> bytes() const noexcept;
    void clear() noexcept;
 private:
    friend class httpserver::detail::tls_credentials_access;
    class impl;
    std::unique_ptr<impl> impl_;
};
enum class tls_psk_version { tls12, tls13 };
enum class tls_psk_status { accepted, rejected, provider_failure, timeout, cancelled, limit_exceeded };
struct tls_psk_context {
    tls_psk_version version = tls_psk_version::tls12;
    std::uint64_t credential_generation = 0;
    std::string selected_host;
    std::chrono::steady_clock::time_point deadline;
    std::stop_token cancellation;
    std::size_t maximum_key_bytes = 512;
};
struct tls_psk_result {
    tls_psk_status status = tls_psk_status::rejected;
    tls_psk_key key;
};
// Identity/context are borrowed for this invocation; retained values need copies.
// TLS1.3 identity is opaque bytes. TLS1.2 uses its textual, NUL-terminated wire
// identity. Own captured state through retirement; synchronize mutable state.
using tls_psk_lookup = std::function<tls_psk_result(std::span<const std::byte>, const tls_psk_context&)>;
struct tls_psk_runtime_options {
    std::size_t lookup_workers = 2;
    std::size_t lookup_queue = 16;
};
// One fixed lookup capacity may span stores/generations. Provider steps use the
// native server's separate bounded runtime. A timed-out ignored callback keeps
// its slot until return; its late result cannot authenticate. Stop/destruction
// never join arbitrary callbacks. Explicit retirement rejects callback self-wait.
class tls_psk_runtime final {
 public:
    static http::outcome create(tls_psk_runtime_options options, std::shared_ptr<tls_psk_runtime>& output);
    ~tls_psk_runtime();
    void request_stop() noexcept;
    http::outcome wait_retired(std::chrono::steady_clock::time_point deadline);
    tls_psk_runtime(const tls_psk_runtime&) = delete;
    tls_psk_runtime& operator=(const tls_psk_runtime&) = delete;
 private:
    friend class httpserver::detail::tls_credentials_access;
    tls_psk_runtime();
    class impl;
    std::shared_ptr<impl> impl_;
};
struct tls_psk_config {
    tls_psk_lookup lookup;
    std::shared_ptr<tls_psk_runtime> runtime;
    std::size_t maximum_identity_bytes = 256;
    std::size_t maximum_key_bytes = 512;
    std::size_t maximum_attempts = 4;
};
// Exact canonical DNS names; unknown/absent SNI selects default_host.
// Ordinary ALPN permits http/1.1 or empty fallback. Initial auth only.
// Fields append to preserve existing four-member aggregate initializers.
struct tls_host_credentials {
    std::string host;
    std::string certificate_chain_pem;
    std::string private_key_pem;
    std::vector<std::string> alpn{"http/1.1"};
    std::string trust_roots_pem;
    tls_profile profile = tls_profile::certificates;
    tls_client_auth_options client_auth;
    std::optional<tls_psk_config> psk;
};
struct tls_credentials_config {
    std::vector<tls_host_credentials> hosts;
    std::size_t default_host = 0;
};
struct tls_acme_challenge {
    std::string host;
    std::string certificate_pem;
    std::string private_key_pem;
    std::array<std::byte, 32> key_authorization_sha256{};
    std::chrono::system_clock::time_point expires_at;
};
// Copies share one store, never process-global state. replace builds and
// validates a complete candidate synchronously, then publishes atomically.
// Failure preserves the active generation. PEM input is not retained; caller
// buffers may die on return. Listeners retain the implementation; accepted
// sessions pin one complete generation through handshake and I/O retirement.
// TLS-off has identical declarations and returns not_supported from replace.
class tls_credentials final {
 public:
    tls_credentials();
    tls_credentials(const tls_credentials&) = default;
    tls_credentials& operator=(const tls_credentials&) = default;
    http::outcome replace(const tls_credentials_config& config);
    // Material validation is independent of listener port. Selection requires
    // exact canonical SNI, sole acme-tls/1, TCP and authentic local port443.
    http::outcome publish_acme(const tls_acme_challenge& challenge);
    http::outcome remove_acme(std::string_view host);

 private:
    friend class httpserver::detail::tls_credentials_access;
    class impl;
    std::shared_ptr<impl> impl_;
};
}  // namespace httpserver::server
#endif  // SRC_HTTPSERVER_SERVER_TLS_CREDENTIALS_HPP_
