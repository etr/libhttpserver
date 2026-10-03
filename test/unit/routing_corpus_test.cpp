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

// TASK-118 step 6: parity replay of all six routing.tseq cases through
// the REAL registry + dispatcher + response framer (plan D6, the
// TASK-114 basic corpus convention; the shared replay driver lives in
// parity/v3_dispatch_replay.hpp). The cases load the live transcript
// at runtime, dispatch through the real pipeline, frame with the real
// framer, parse the wire with the parity response-frame parser, and
// assert every expectation through the corpus's own assertion engine
// plus the keep-alive verdict, exactly as transcript_runner does. The
// pipelined case replays as two sequential dispatches framed into one
// wire stream.

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/server/hooks.hpp>
#include <httpserver/server/routes.hpp>
#include <parity/case_wire.hpp>
#include <parity/normalize.hpp>
#include <parity/response_frame.hpp>
#include <parity/transcript.hpp>
#include <parity/v3_dispatch_replay.hpp>

#include "./littletest.hpp"

namespace {

namespace replay = parity::v3_replay;
namespace srv = httpserver::server;
namespace http = httpserver::http;
using parity::observed_response;
using parity::response_frame_parser;
using parity::tcase;

const tcase* find_case(const parity::transcript& t, const char* name) {
    for (const tcase& c : t.cases) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

// The corpus diff of one case ("" = pass): dispatch, frame, parse,
// assert every expectation, keep-alive verdict included.
std::string check_case(const char* case_name) {
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/routing.tseq"));
    const tcase* found = find_case(t, case_name);
    if (found == nullptr) return "case not found";

    srv::budget_limits limits;
    limits.set(srv::resource::routes, 8);
    const srv::resource_budget budget = srv::resource_budget::root(limits);
    const srv::route_registry registry = replay::build_routing_basic(budget);
    srv::hook_bus bus;
    const httpserver::detail::error_page_factories pages;

    std::string wire;
    bool head_only = false;
    bool delivered = true;
    for (const replay::replay_request& request :
         replay::requests_of(*found)) {
        if (request.head.request_method
                == http::method::known(http::method_id::head)) {
            head_only = true;
        }
        bool ok = false;
        const replay::replay_result one =
            replay::dispatch_once(registry, bus, pages, request, &ok);
        if (!ok) delivered = false;
        wire.append(one.wire);
        if (!one.keep_alive) return "keep-alive verdict closed";
    }
    if (!delivered) return "dispatcher failed";
    if (wire.empty()) return "no wire bytes";

    response_frame_parser parser;
    parser.set_head_only(head_only);
    std::vector<observed_response> responses = parser.feed(wire);
    for (observed_response& r : parser.finish()) {
        responses.push_back(std::move(r));
    }
    if (parser.failed()) return "parser: " + parser.error();

    std::vector<parity::normalized_exchange> exchanges;
    for (const observed_response& r : responses) {
        exchanges.push_back(parity::normalize(r));
    }

    std::size_t index = 0;
    bool first = true;
    for (const parity::expectation& e : found->expects) {
        const bool advances = e.kind == parity::expect_kind::status
            || e.kind == parity::expect_kind::status_line;
        if (advances && !first) ++index;
        if (advances) first = false;
        if (e.kind == parity::expect_kind::connection) {
            if (e.value == "close") return "connection close unexpected";
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

LT_BEGIN_SUITE(routing_corpus_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(routing_corpus_suite)

LT_BEGIN_AUTO_TEST(routing_corpus_suite, get_hello_replay)
    const std::string diff = check_case("get_hello");
    if (!diff.empty()) std::cerr << "[replay get_hello] " << diff << "\n";
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(get_hello_replay)

LT_BEGIN_AUTO_TEST(routing_corpus_suite, parameterized_path_replay)
    const std::string diff = check_case("parameterized_path");
    if (!diff.empty()) {
        std::cerr << "[replay parameterized_path] " << diff << "\n";
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(parameterized_path_replay)

LT_BEGIN_AUTO_TEST(routing_corpus_suite, head_both_methods_replay)
    const std::string diff = check_case("head_both_methods");
    if (!diff.empty()) {
        std::cerr << "[replay head_both_methods] " << diff << "\n";
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(head_both_methods_replay)

LT_BEGIN_AUTO_TEST(routing_corpus_suite, method_not_allowed_replay)
    const std::string diff = check_case("method_not_allowed");
    if (!diff.empty()) {
        std::cerr << "[replay method_not_allowed] " << diff << "\n";
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(method_not_allowed_replay)

LT_BEGIN_AUTO_TEST(routing_corpus_suite, not_found_replay)
    const std::string diff = check_case("not_found");
    if (!diff.empty()) std::cerr << "[replay not_found] " << diff << "\n";
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(not_found_replay)

LT_BEGIN_AUTO_TEST(routing_corpus_suite, pipelined_keepalive_replay)
    const std::string diff = check_case("pipelined_keepalive");
    if (!diff.empty()) {
        std::cerr << "[replay pipelined_keepalive] " << diff << "\n";
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(pipelined_keepalive_replay)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
