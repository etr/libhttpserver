/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-108 step 6: the bare error response serializer
// (http1_error_synth). The engine answers without routing when the head
// never parsed or the framing was rejected; the synth pins that wire
// form: version-mirrored status line, "Connection: close",
// "Content-Length: 0", empty body. Parsed through the parity response
// frame parser like every other wire observation. Header-only surface;
// the LDADD stays empty.

#include <string>
#include <vector>

#include <httpserver/detail/http1_error_synth.hpp>
#include <parity/response_frame.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;

using httpserver::detail::http1_error_synth;
using parity::observed_response;
using parity::response_frame_parser;

// Serializes one error and parses it back.
observed_response wire_for(http::protocol version, std::uint16_t code) {
    const std::string bytes = http1_error_synth::serialize(version, code, {});
    response_frame_parser parser;
    const std::vector<observed_response> done = parser.feed(bytes);
    if (done.size() != 1) {
        return observed_response{};
    }
    return done.front();
}

bool has_connection_close(const observed_response& response) {
    for (const parity::observed_header& header : response.headers) {
        if (header.name == "Connection"
                && header.value.find("close") != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

LT_BEGIN_SUITE(http1_error_synth_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http1_error_synth_suite)

// The malformed-head answer: 400, empty length-framed body, close.
LT_BEGIN_AUTO_TEST(http1_error_synth_suite, bad_request_400_http_1_1)
    const observed_response response =
        wire_for(http::protocol::http_1_1, 400);
    LT_CHECK_EQ(response.status, 400);
    LT_CHECK(response.raw_status_line.find("HTTP/1.1") == 0);
    LT_CHECK_EQ(response.body, std::string(""));
    LT_CHECK_EQ(response.framing, std::string("content-length"));
    LT_CHECK(has_connection_close(response));
LT_END_AUTO_TEST(bad_request_400_http_1_1)

// The head-budget answer: 431.
LT_BEGIN_AUTO_TEST(http1_error_synth_suite, headers_too_large_431)
    const observed_response response =
        wire_for(http::protocol::http_1_1, 431);
    LT_CHECK_EQ(response.status, 431);
    LT_CHECK_EQ(response.body, std::string(""));
    LT_CHECK(has_connection_close(response));
LT_END_AUTO_TEST(headers_too_large_431)

// The unsupported-coding answer: 501.
LT_BEGIN_AUTO_TEST(http1_error_synth_suite, not_implemented_501)
    const observed_response response =
        wire_for(http::protocol::http_1_1, 501);
    LT_CHECK_EQ(response.status, 501);
    LT_CHECK(has_connection_close(response));
LT_END_AUTO_TEST(not_implemented_501)

// The overload answer: 503.
LT_BEGIN_AUTO_TEST(http1_error_synth_suite, unavailable_503)
    const observed_response response =
        wire_for(http::protocol::http_1_1, 503);
    LT_CHECK_EQ(response.status, 503);
    LT_CHECK(has_connection_close(response));
LT_END_AUTO_TEST(unavailable_503)

// On HTTP/1.0 the status line mirrors the request version; the empty
// body keeps its explicit zero length (valid 1.0 framing) and the
// close is signaled through the Connection field.
LT_BEGIN_AUTO_TEST(http1_error_synth_suite, http_1_0_mirrors_version)
    const observed_response response =
        wire_for(http::protocol::http_1_0, 400);
    LT_CHECK_EQ(response.status, 400);
    LT_CHECK(response.raw_status_line.find("HTTP/1.0") == 0);
    LT_CHECK_EQ(response.framing, std::string("content-length"));
    LT_CHECK(has_connection_close(response));
LT_END_AUTO_TEST(http_1_0_mirrors_version)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
