/*
    This file is part of libhttpserver
    Copyright (C) 2011-2019 Sebastiano Merlino

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
    USA
*/

// TASK-057: secure-by-default redaction of credential material in
// http_request::operator<<. Default-built test requests must NOT leak
// the Basic-auth password, Authorization / Proxy-Authorization header
// values, or any cookie value into the diagnostic dump. The opt-in
// create_webserver::expose_credentials_in_logs(true) flag (propagated
// through create_test_request::expose_credentials_in_logs() for unit
// scope) restores the v1 verbose form for development.

#include <sstream>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/create_test_request.hpp"
#include "./littletest.hpp"

using httpserver::create_test_request;
using httpserver::http_request;

LT_BEGIN_SUITE(http_request_operator_stream_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_request_operator_stream_suite)

// TASK-057 — Acceptance: default-built http_request must redact
// credentials when streamed via operator<<.
LT_BEGIN_AUTO_TEST(http_request_operator_stream_suite, operator_stream_redacts_credentials)
    auto req = create_test_request()
        .user("admin")
        .pass("hunter2")
        .header("Authorization", "Basic YWRtaW46aHVudGVyMg==")
        .header("Proxy-Authorization", "Digest username=\"admin\", response=\"deadbeef\"")
        .cookie("session", "session-token-cafef00d")
        .cookie("csrf", "csrf-token-abad1dea")
        .build();

    std::ostringstream oss;
    oss << req;
    const std::string out = oss.str();

    // Username remains visible (REMOTE_USER access-log convention).
    LT_CHECK(out.find("user:\"admin\"") != std::string::npos);

    // Basic-auth password value must be redacted.
    LT_CHECK(out.find("pass:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("hunter2") == std::string::npos);

    // Authorization-class header values must be redacted; the base64
    // and digest payloads must NOT appear.
    LT_CHECK(out.find("Authorization:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("Proxy-Authorization:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("YWRtaW46aHVudGVyMg==") == std::string::npos);
    LT_CHECK(out.find("deadbeef") == std::string::npos);

    // Every cookie value must be redacted; cookie keys remain visible.
    LT_CHECK(out.find("session:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("csrf:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("session-token-cafef00d") == std::string::npos);
    LT_CHECK(out.find("csrf-token-abad1dea") == std::string::npos);
LT_END_AUTO_TEST(operator_stream_redacts_credentials)

// TASK-057 — Acceptance: opt-in flag (development-only) restores the
// v1 verbose form bit-for-bit. The redaction token MUST NOT appear in
// the output when the opt-in is set.
LT_BEGIN_AUTO_TEST(http_request_operator_stream_suite, operator_stream_exposes_credentials_when_opted_in)
    auto req = create_test_request()
        .user("admin")
        .pass("hunter2")
        .header("Authorization", "Basic YWRtaW46aHVudGVyMg==")
        .header("Proxy-Authorization", "Digest username=\"admin\", response=\"deadbeef\"")
        .cookie("session", "session-token-cafef00d")
        .cookie("csrf", "csrf-token-abad1dea")
        .expose_credentials_in_logs()
        .build();

    std::ostringstream oss;
    oss << req;
    const std::string out = oss.str();

    // Verbose v1 form: every credential surface is streamed plaintext.
    LT_CHECK(out.find("pass:\"hunter2\"") != std::string::npos);
    LT_CHECK(out.find("Authorization:\"Basic YWRtaW46aHVudGVyMg==\"") != std::string::npos);
    LT_CHECK(out.find("Proxy-Authorization:\"Digest username=\"admin\", response=\"deadbeef\"\"") != std::string::npos);
    LT_CHECK(out.find("session:\"session-token-cafef00d\"") != std::string::npos);
    LT_CHECK(out.find("csrf:\"csrf-token-abad1dea\"") != std::string::npos);

    // The redaction token MUST NOT appear in the dump when the opt-in
    // flag is set (lets a developer inspect verbatim wire payloads).
    LT_CHECK(out.find("<redacted>") == std::string::npos);
LT_END_AUTO_TEST(operator_stream_exposes_credentials_when_opted_in)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
