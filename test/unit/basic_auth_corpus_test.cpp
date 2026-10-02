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
     License along with this library; if not, see the file LICENSE in
     the distribution; if not, write to the Free Software Foundation,
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

// TASK-114 step 6: parity replay of the whole auth_basic.tseq corpus
// case through the REAL Basic policy and the REAL response framer --
// no hand-synthesized challenge fields (the framer suite's
// auth_basic_no_credentials replay synthesized them; this suite
// replaces that posture for M9). Every case loads the live transcript
// at runtime, classifies the request head with the policy (fixed
// alice/wonderland credentials, realm "transcript", exactly the v2
// fixture's posture), frames the resulting status/fields/body, parses
// the wire with the parity response-frame parser, and asserts every
// expectation through the corpus's own assertion engine
// (check_expectation under the Nth-status cursor rule) plus the
// keep-alive verdict, exactly as transcript_runner does.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/auth/basic_auth.hpp>
#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>
#include <parity/normalize.hpp>
#include <parity/response_frame.hpp>
#include <parity/transcript.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
namespace auth = httpserver::auth;

using httpserver::detail::http1_keepalive;
using httpserver::detail::http1_response_framer;
using httpserver::detail::http1_response_mode;
using parity::observed_response;
using parity::response_frame_parser;

constexpr std::size_t unlimited = static_cast<std::size_t>(-1);

std::span<const std::byte> as_bytes(const std::string& s) {
    const auto* raw = reinterpret_cast<const std::byte*>(s.data());
    return std::span<const std::byte>(raw, s.size());
}

// The v2 parity fixture's fixed credentials and realm.
auth::basic_auth_policy transcript_policy() {
    auth::basic_auth_policy policy;
    const http::outcome created = auth::basic_auth_policy::create(
        "transcript", "alice", "wonderland", policy);
    if (!created.ok()) return policy;
    return policy;
}

// The request head of one corpus case, read from its send segments:
// the Authorization field value when present (the only auth-relevant
// field), plus the method/target of the request. Segments carry their
// CRLF; each field line ends at it.
http::request_head case_request(const parity::tcase& c) {
    http::request_head head;
    head.raw_target = "/secret";
    head.route_path = "/secret";
    head.request_method = http::method::known(http::method_id::get);
    head.request_protocol = http::protocol::http_1_1;
    head.head_fields.append("Host", "127.0.0.1");
    for (const parity::send_segment& segment : c.sends) {
        std::string_view line(segment.bytes);
        constexpr std::string_view k_field = "Authorization: ";
        if (line.rfind(k_field, 0) != 0) continue;
        line.remove_prefix(k_field.size());
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.remove_suffix(1);
        }
        head.head_fields.append("Authorization", line);
    }
    return head;
}

// The handler side of the corpus fixture: authenticated requests are
// served the secret body with the pinned framing.
void secret_response(http::fields& fields, std::string& body) {
    fields.append("Content-Type", "text/plain");
    fields.append("Content-Length", "9");
    body = "secret-ok";
}

// Classifies with the real policy, frames, parses, and checks every
// expectation of the case; returns the corpus diff ("" = pass).
std::string replay_case(const char* case_name) {
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/auth_basic.tseq"));
    const parity::tcase* found = nullptr;
    for (const parity::tcase& c : t.cases) {
        if (c.name == case_name) found = &c;
    }
    if (found == nullptr) {
        return "case not found: " + std::string(case_name);
    }

    const auth::basic_auth_policy policy = transcript_policy();
    const auth::basic_auth_verdict verdict = policy.check(case_request(*found));

    std::uint16_t status = 200;
    http::fields fields;
    std::string body;
    if (verdict.allowed()) {
        secret_response(fields, body);
    } else {
        status = verdict.challenge_status().code();
        fields = verdict.challenge_fields();
    }

    http1_response_framer framer({}, {});
    std::string wire;
    const http::request_head& request = case_request(*found);
    const http::outcome head_out = framer.start_head(
        wire, request, http::status::from_code(status), fields);
    if (!head_out.ok()) return "start_head: " + head_out.message();
    if (!body.empty()) {
        const http::outcome pushed =
            framer.push_body(wire, as_bytes(body), unlimited);
        if (!pushed.ok()) return "push_body: " + pushed.message();
    }
    const http::outcome ended = framer.finish_body(wire, http::fields());
    if (!ended.ok()) return "finish_body: " + ended.message();

    response_frame_parser parser;
    std::vector<observed_response> responses = parser.feed(wire);
    for (observed_response& r : parser.finish()) {
        responses.push_back(std::move(r));
    }
    if (parser.failed()) return "parser: " + parser.error();
    if (responses.size() != 1) {
        return "expected one parsed response, got "
            + std::to_string(responses.size());
    }
    const std::vector<parity::normalized_exchange> exchanges{
        parity::normalize(responses.front())};

    const http1_response_mode mode = http1_response_mode::compute(
        request, http::status::from_code(status), fields);
    const http1_keepalive verdict_keepalive =
        httpserver::detail::http1_response_keepalive(
            request, mode.kind, mode.close_policy);

    std::size_t index = 0;
    bool first = true;
    for (const parity::expectation& e : found->expects) {
        const bool advances = e.kind == parity::expect_kind::status
            || e.kind == parity::expect_kind::status_line;
        if (advances && !first) ++index;
        if (advances) first = false;
        if (e.kind == parity::expect_kind::connection) {
            const bool want_keep = e.value != "close";
            const bool got_keep =
                verdict_keepalive == http1_keepalive::keep_alive;
            if (want_keep != got_keep) {
                return "expect-line=" + std::to_string(e.line)
                    + ": connection " + e.value + " vs keepalive verdict";
            }
            continue;
        }
        if (e.kind == parity::expect_kind::closer) continue;
        if (index >= exchanges.size()) return "cursor past observed";
        const parity::match_result m = parity::check_expectation(
            e, exchanges[index], PARITY_TRANSCRIPT_DIR);
        if (!m.ok) return m.diff;
    }
    return "";
}

void expect_replay(const std::string& diff, const char* what) {
    if (!diff.empty()) {
        std::cerr << "[replay " << what << "] " << diff << "\n";
    }
}

}  // namespace

LT_BEGIN_SUITE(basic_auth_corpus_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(basic_auth_corpus_suite)

LT_BEGIN_AUTO_TEST(basic_auth_corpus_suite, no_credentials_replay)
    const std::string diff = replay_case("no_credentials");
    expect_replay(diff, "no_credentials");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(no_credentials_replay)

LT_BEGIN_AUTO_TEST(basic_auth_corpus_suite, valid_credentials_replay)
    const std::string diff = replay_case("valid_credentials");
    expect_replay(diff, "valid_credentials");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(valid_credentials_replay)

LT_BEGIN_AUTO_TEST(basic_auth_corpus_suite, invalid_credentials_replay)
    const std::string diff = replay_case("invalid_credentials");
    expect_replay(diff, "invalid_credentials");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(invalid_credentials_replay)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
