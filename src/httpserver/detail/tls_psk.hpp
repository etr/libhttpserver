/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "tls_psk.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_TLS_PSK_HPP_
#define SRC_HTTPSERVER_DETAIL_TLS_PSK_HPP_
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
namespace httpserver::detail {
class secure_bytes final {
 public:
    // Observation runs after cleansing, before allocation release; never throws.
    using release_observer = void (*)(std::span<const std::byte>, void*) noexcept;
    secure_bytes() = default;
    explicit secure_bytes(std::span<const std::byte> bytes, release_observer observer = nullptr, void* argument = nullptr);
    ~secure_bytes();
    secure_bytes(secure_bytes&& other) noexcept;
    secure_bytes& operator=(secure_bytes&& other) noexcept;
    secure_bytes(const secure_bytes&) = delete;
    secure_bytes& operator=(const secure_bytes&) = delete;
    std::span<const std::byte> bytes() const { return {data_.get(), size_}; }
    void clear() noexcept;
 private:
    std::unique_ptr<std::byte[]> data_;
    std::size_t size_ = 0;
    release_observer observer_ = nullptr;
    void* argument_ = nullptr;
};
enum class psk_tls_version { tls12, tls13 };
enum class psk_lookup_status { accepted, rejected, provider_failure, timeout, cancelled, limit_exceeded };
struct psk_handshake_context {
    psk_tls_version version = psk_tls_version::tls12;
    std::uint64_t credential_generation = 0;
    std::string selected_host;
    std::chrono::steady_clock::time_point deadline;
    std::stop_token cancellation;
    std::size_t maximum_key_bytes = 512;
};
struct psk_lookup_result {
    psk_lookup_status status = psk_lookup_status::rejected;
    secure_bytes key;
};
using psk_lookup = std::function<psk_lookup_result(std::span<const std::byte>, const psk_handshake_context&)>;
class tls_psk_runtime;
struct tls_psk_config {
    psk_lookup lookup;
    std::shared_ptr<tls_psk_runtime> runtime;
    std::size_t maximum_identity_bytes = 256;
    std::size_t maximum_key_bytes = 512;
    std::size_t maximum_attempts = 4;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_TLS_PSK_HPP_
