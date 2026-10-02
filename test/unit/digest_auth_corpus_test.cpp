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
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/

// TASK-115 step 5: parity replay of the auth_digest.tseq corpus
// through the REAL Digest policy and the REAL response framer. The
// pinned challenge_structure case replays exactly as the basic corpus
// does (classify the case's request head, frame the verdict's
// status/fields/body, check every expectation through the corpus's
// own engine, masks covering the volatile nonce/opaque). The
// roundtrip_digest case is curl-mediated in the corpus because a raw
// transcript cannot sign against a server-chosen nonce; here it
// becomes a two-step in-test handshake: the transcript's request is
// answered with the policy's challenge, the INDEPENDENT test client
// (integ/digest_client.hpp) computes the RFC 7616 response against
// that challenge, the answered request is re-classified by the same
// policy, and the authenticated side is served the pinned secret --
// so a pass proves the whole challenge/response contract over the
// wire, not a shared implementation.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Deliberate upward include: the independent RFC 7616 client.
#include "../integ/digest_client.hpp"
#include <httpserver/auth/digest_auth.hpp>
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
namespace dclient = httpserver_test;

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

// The v2 parity fixture's Digest posture: realm "transcript",
// bob/builder, MD5, the "digest required" challenge body.
auth::digest_auth_policy transcript_policy() {
    auth::digest_auth_policy policy;
    auth::digest_auth_options options;
    options.challenge_body = "digest required";
    static_cast<void>(auth::digest_auth_policy::create(
        "transcript", "bob", "builder", options, policy));
    return policy;
}

// The request head of one corpus case, read from its send segments
// (the method/target come from the fixture's route, the Authorization
// field when a segment carries one).
http::request_head case_request(const parity::tcase& c) {
    http::request_head head;
    head.raw_target = "/digest";
    head.route_path = "/digest";
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
// served the secret body with the pinned framing (the roundtrip
// case's "digest-ok" success body).
void secret_response(http::fields& fields, std::string& body) {
    fields.append("Content-Type", "text/plain");
    fields.append("Content-Length", "9");
    body = "digest-ok";
}

// Frames one status/fields/body through the REAL framer and returns
// the parsed normalized exchange (""-prefixed error string on a
// framing or parse failure).
std::string framed_exchange(const http::request_head& request,
                            std::uint16_t status, const http::fields& fields,
                            const std::string& body,
                            parity::normalized_exchange& out) {
    http1_response_framer framer({}, {});
    std::string wire;
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
    out = parity::normalize(responses.front());
    return "";
}

// Runs the corpus's assertion engine over one case's expectations
// against the observed exchange (the Nth-status cursor rule and the
// keep-alive verdict, exactly as transcript_runner does).
std::string check_expects(const parity::tcase& c,
                          const parity::normalized_exchange& exchange,
                          const http::request_head& request,
                          const http::fields& fields, std::uint16_t status) {
    const http1_response_mode mode = http1_response_mode::compute(
        request, http::status::from_code(status), fields);
    const http1_keepalive keepalive =
        httpserver::detail::http1_response_keepalive(
            request, mode.kind, mode.close_policy);

    const std::vector<parity::normalized_exchange> exchanges{exchange};
    std::size_t index = 0;
    bool first = true;
    for (const parity::expectation& e : c.expects) {
        const bool advances = e.kind == parity::expect_kind::status
            || e.kind == parity::expect_kind::status_line;
        if (advances && !first) ++index;
        if (advances) first = false;
        if (e.kind == parity::expect_kind::connection) {
            const bool want_keep = e.value != "close";
            const bool got_keep = keepalive == http1_keepalive::keep_alive;
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

// Locates one named case of an ALREADY-PARSED transcript. The caller
// owns @p t and must keep it alive past every use of the result: the
// tcase's strings live in the transcript, so a pointer into a
// transcript destroyed at the helper's return would dangle (the
// TASK-114 lifetime shape -- parse in the replay function, look up
// against the local).
const parity::tcase* find_case(const parity::transcript& t,
                               const char* name) {
    for (const parity::tcase& c : t.cases) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

// challenge_structure: the 401 over the wire, byte-for-byte around
// the nonce/opaque masks.
std::string replay_challenge_structure() {
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/auth_digest.tseq"));
    const parity::tcase* c = find_case(t, "challenge_structure");
    if (c == nullptr) return "case not found: challenge_structure";

    const auth::digest_auth_policy policy = transcript_policy();
    const http::request_head request = case_request(*c);
    const auth::digest_auth_verdict verdict = policy.check(request);
    if (verdict.allowed()) return "unauthenticated request authenticated";

    parity::normalized_exchange exchange;
    const std::string framed =
        framed_exchange(request, verdict.challenge_status().code(),
                        verdict.challenge_fields(), verdict.challenge_body,
                        exchange);
    if (!framed.empty()) return framed;
    return check_expects(*c, exchange, request, verdict.challenge_fields(),
                         verdict.challenge_status().code());
}

// roundtrip_digest: the corpus's curl-mediated case as a two-step
// handshake -- challenge, independent-client response, re-check, the
// authenticated side served the pinned secret.
std::string replay_roundtrip_digest() {
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/auth_digest.tseq"));
    const parity::tcase* c = find_case(t, "roundtrip_digest");
    if (c == nullptr) return "case not found: roundtrip_digest";

    const auth::digest_auth_policy policy = transcript_policy();
    const http::request_head request = case_request(*c);

    const auth::digest_auth_verdict offered = policy.check(request);
    const auto challenge =
        dclient::parse_www_authenticate(offered.challenge);
    if (!challenge.has_value()) {
        return "challenge did not parse: " + offered.challenge;
    }
    const std::string cnonce = dclient::make_cnonce();
    const std::string response = dclient::compute_response_cleartext(
        *challenge, dclient::digest_hash::md5, "GET", "/digest", "bob",
        "builder", cnonce, "00000001");

    http::request_head answered = request;
    answered.head_fields.append(
        "Authorization",
        dclient::build_authorization_header(
            *challenge, "bob", "/digest", cnonce, "00000001", response));
    const auth::digest_auth_verdict verdict = policy.check(answered);
    if (!verdict.allowed()) return "answered request was not authenticated";

    http::fields fields;
    std::string body;
    secret_response(fields, body);
    parity::normalized_exchange exchange;
    const std::string framed =
        framed_exchange(request, 200, fields, body, exchange);
    if (!framed.empty()) return framed;
    return check_expects(*c, exchange, request, fields, 200);
}

void expect_replay(const std::string& diff, const char* what) {
    if (!diff.empty()) {
        std::cerr << "[replay " << what << "] " << diff << "\n";
    }
}

}  // namespace

LT_BEGIN_SUITE(digest_auth_corpus_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(digest_auth_corpus_suite)

LT_BEGIN_AUTO_TEST(digest_auth_corpus_suite, challenge_structure_replay)
    const std::string diff = replay_challenge_structure();
    expect_replay(diff, "challenge_structure");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(challenge_structure_replay)

LT_BEGIN_AUTO_TEST(digest_auth_corpus_suite, roundtrip_digest_replay)
    const std::string diff = replay_roundtrip_digest();
    expect_replay(diff, "roundtrip_digest");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(roundtrip_digest_replay)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
