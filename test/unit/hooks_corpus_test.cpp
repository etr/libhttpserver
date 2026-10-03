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

// TASK-118 step 6: parity replay of all four hooks.tseq cases through
// the REAL hook bus + dispatcher + response framer (plan D6; the
// shared driver in parity/v3_dispatch_replay.hpp, the routing_corpus
// convention). The routing_hooks profile: a before_handler
// short-circuit on DELETE /admin (with the pinned ~X-Hook suppression
// of the after phase), an after_handler header mutation on every
// dispatched response, and the custom 404/405 pages through the
// construction-time factories (Allow still rides the 405).

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

// The corpus diff of one hooks.tseq case ("" = pass).
std::string check_case(const char* case_name) {
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/hooks.tseq"));
    const tcase* found = find_case(t, case_name);
    if (found == nullptr) return "case not found";

    // The routing_hooks profile: the shared routes plus the hooks. The
    // /admin registration joins the profile before the registry turns
    // read-only at dispatch.
    srv::budget_limits limits;
    limits.set(srv::resource::routes, 8);
    const srv::resource_budget budget = srv::resource_budget::root(limits);
    srv::route_registry registry = replay::build_routing_basic(budget);
    (void)registry.route(http::method::known(http::method_id::del), "/admin",
                         replay::text_route("admin-ok"));
    srv::hook_bus bus;
    replay::install_routing_hooks(bus);
    const httpserver::detail::error_page_factories pages =
        replay::custom_hook_pages();

    std::string wire;
    bool delivered = true;
    for (const replay::replay_request& request :
         replay::requests_of(*found)) {
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

LT_BEGIN_SUITE(hooks_corpus_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(hooks_corpus_suite)

LT_BEGIN_AUTO_TEST(hooks_corpus_suite, get_hello_hook_header_replay)
    const std::string diff = check_case("get_hello_hook_header");
    if (!diff.empty()) {
        std::cerr << "[replay get_hello_hook_header] " << diff << "\n";
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(get_hello_hook_header_replay)

LT_BEGIN_AUTO_TEST(hooks_corpus_suite, before_handler_403_replay)
    const std::string diff = check_case("before_handler_403");
    if (!diff.empty()) {
        std::cerr << "[replay before_handler_403] " << diff << "\n";
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(before_handler_403_replay)

LT_BEGIN_AUTO_TEST(hooks_corpus_suite, custom_404_replay)
    const std::string diff = check_case("custom_404");
    if (!diff.empty()) std::cerr << "[replay custom_404] " << diff << "\n";
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(custom_404_replay)

LT_BEGIN_AUTO_TEST(hooks_corpus_suite, custom_405_replay)
    const std::string diff = check_case("custom_405");
    if (!diff.empty()) std::cerr << "[replay custom_405] " << diff << "\n";
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(custom_405_replay)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
