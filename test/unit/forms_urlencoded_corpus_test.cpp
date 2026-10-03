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

// TASK-116 step 5: parity replay of the forms.tseq corpus case through
// the REAL urlencoded decode and the REAL response framer -- no
// hand-synthesized fields (the TASK-114 basic corpus convention).
// Every case loads the live transcript at runtime, rebuilds the
// request head and raw body from its send segments, decodes the body
// with the library decoder under the adapter's default budgets, echoes
// the fields exactly the way the v2 fixture's /echo_form resource did
// (a=<a>;b=<b>), frames the response, parses the wire with the parity
// response-frame parser, and asserts every expectation through the
// corpus's own assertion engine plus the keep-alive verdict, exactly
// as transcript_runner does.
//
// multipart_field is deliberately NOT replayed here: it is TASK-117's
// scope (the multipart decoder), and its case stays pinned by the v2
// transcript_runner lane alone until that task lands.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>
#include <parity/case_wire.hpp>
#include <parity/normalize.hpp>
#include <parity/response_frame.hpp>
#include <parity/transcript.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
namespace forms = httpserver::forms;

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

// TASK-117 promoted the request-side wire rebuild into
// parity/case_wire.hpp (shared with the multipart corpus replay).

// The handler side of the corpus fixture: /echo_form echoes
// get_arg("a")/get_arg("b") as "a=<a>;b=<b>" with the pinned framing
// (the v2 fixture's posture, driven here by the real decode).
std::string echo_body(const forms::form_fields& fields) {
    return "a=" + std::string(fields.value("a").value_or(""))
        + ";b=" + std::string(fields.value("b").value_or(""));
}

// Decodes with the real library, echoes through the real framer,
// parses, and checks every expectation of the case; returns the
// corpus diff ("" = pass).
std::string replay_case(const char* case_name) {
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/forms.tseq"));
    const parity::tcase* found = nullptr;
    for (const parity::tcase& c : t.cases) {
        if (c.name == case_name) found = &c;
    }
    if (found == nullptr) {
        return "case not found: " + std::string(case_name);
    }

    const parity::corpus_request request = parity::parse_case(*found);
    forms::form_fields fields;
    const http::outcome decoded = forms::decode_urlencoded(
        as_bytes(request.body), forms::urlencoded_limits{}, fields);
    if (!decoded.ok()) return "decode: " + decoded.message();

    const std::string body = echo_body(fields);
    http::fields out;
    out.append("Content-Type", "text/plain");
    out.append("Content-Length", std::to_string(body.size()));

    http1_response_framer framer;
    std::string wire;
    const http::outcome head_out = framer.start_head(
        wire, request.head, http::status::from_code(200), out);
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
        request.head, http::status::from_code(200), out);
    const http1_keepalive verdict_keepalive =
        httpserver::detail::http1_response_keepalive(
            request.head, mode.kind, mode.close_policy);

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

}  // namespace

LT_BEGIN_SUITE(forms_urlencoded_corpus_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(forms_urlencoded_corpus_suite)

LT_BEGIN_AUTO_TEST(forms_urlencoded_corpus_suite, urlencoded_echo_replay)
    const std::string diff = replay_case("urlencoded_echo");
    if (!diff.empty()) {
        std::cerr << "[replay urlencoded_echo] " << diff << "\n";
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(urlencoded_echo_replay)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
