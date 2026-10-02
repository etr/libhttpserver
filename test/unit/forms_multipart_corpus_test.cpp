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
     License along with the library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-117 step 5: parity replay of the forms.tseq multipart corpus
// case through the REAL multipart decode and the REAL response
// framer -- no hand-synthesized fields (the TASK-114 basic corpus
// convention). The case loads the live transcript at runtime, rebuilds
// the request head and raw body from its send segments (the promoted
// parity/case_wire.hpp helper), decodes the body with the library
// decoder under the adapter's default budgets, echoes the field
// exactly the way the v2 fixture's /upload resource did (note=<v>),
// frames the response, parses the wire with the parity response-frame
// parser, and asserts every expectation through the corpus's own
// assertion engine plus the keep-alive verdict, exactly as
// transcript_runner does.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/forms/multipart.hpp>
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

// The handler side of the corpus fixture: /upload echoes
// get_arg("note") as "note=<v>" with the pinned framing (the v2
// fixture's posture, driven here by the real decode).
std::string echo_body(const forms::form_fields& fields) {
    return "note=" + std::string(fields.value("note").value_or(""));
}

// A part_sink mirroring the adapter's internal one: field parts
// collect, file parts drain (the fixture case carries none).
class field_sink final : public forms::part_sink {
 public:
    http::outcome on_part_begin(
        const forms::part_descriptor& part) override {
        draining_file_ = !part.filename.empty();
        if (!draining_file_) name_ = std::string(part.name);
        return http::outcome::okay();
    }

    http::outcome on_part_data(std::span<const std::byte> data) override {
        if (!draining_file_) {
            const char* raw = reinterpret_cast<const char*>(data.data());
            value_.append(raw, data.size());
        }
        return http::outcome::okay();
    }

    http::outcome on_part_end() override {
        entries_.emplace_back(std::move(name_), std::move(value_));
        value_.clear();
        return http::outcome::okay();
    }

    void on_part_abort(http::outcome) override { }

    forms::form_fields take_fields() {
        return forms::form_fields(std::move(entries_));
    }

 private:
    std::vector<std::pair<std::string, std::string>> entries_;
    std::string name_;
    std::string value_;
    bool draining_file_ = false;
};

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

    const parity::corpus_request request =
        parity::parse_case(*found);
    field_sink sink;
    forms::multipart_read read;
    const http::outcome decoded = forms::decode_multipart(
        as_bytes(request.body),
        request.head.head_fields.first("Content-Type"),
        forms::multipart_limits{}, sink, read);
    if (!decoded.ok()) return "decode: " + decoded.message();
    if (read.parts_completed != 1) {
        return "parts_completed " + std::to_string(read.parts_completed);
    }

    const std::string body = echo_body(sink.take_fields());
    http::fields out;
    out.append("Content-Type", "text/plain");
    out.append("Content-Length", std::to_string(body.size()));

    http1_response_framer framer({}, {});
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

LT_BEGIN_SUITE(forms_multipart_corpus_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(forms_multipart_corpus_suite)

LT_BEGIN_AUTO_TEST(forms_multipart_corpus_suite, multipart_field_replay)
    const std::string diff = replay_case("multipart_field");
    if (!diff.empty()) {
        std::cerr << "[replay multipart_field] " << diff << "\n";
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(multipart_field_replay)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
