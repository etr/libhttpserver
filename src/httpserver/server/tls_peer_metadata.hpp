/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef SRC_HTTPSERVER_SERVER_TLS_PEER_METADATA_HPP_
#define SRC_HTTPSERVER_SERVER_TLS_PEER_METADATA_HPP_
#include <cstdint>
#include <string>
namespace httpserver::server {
// Library-owned leaf identity. DNs use RFC2253; fingerprint is lowercase
// SHA-256 hex (64 characters). Validity is UTC Unix seconds. Anonymous TLS
// uses false, empty strings and -1; no provider handles escape this value.
struct tls_peer_metadata {
    bool has_client_certificate = false;
    bool client_certificate_verified = false;
    std::string subject_dn;
    std::string issuer_dn;
    std::string common_name;
    std::string fingerprint_sha256;
    std::int64_t not_before = -1;
    std::int64_t not_after = -1;
};
}  // namespace httpserver::server
#endif  // SRC_HTTPSERVER_SERVER_TLS_PEER_METADATA_HPP_
