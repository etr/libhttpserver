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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// register_path / register_prefix split.
//
// Goal: prefix-vs-exact matching is now an explicit named choice, not a
// positional bool flag.
//
//   - register_path(path, ptr)   -> exact match (does NOT match a longer URL)
//   - register_prefix(path, ptr) -> prefix match (matches the path and all
//                                   children of it)
//   - register_resource is gone entirely (clean break): neither the
//     smart-pointer overloads nor the 3-arg `bool family` overloads exist.
//     Call register_path for exact match or register_prefix for prefix.
//
// This TU pins both the compile-time signature contract (the new methods
// exist with the right shape; the bool-family overload is removed) and
// the runtime matching semantics (real curl round-trips against a
// running webserver).

#include <curl/curl.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8180

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class ok_resource : public http_resource {
 public:
     http_response render_get(const http_request&) override {
         return http_response::string("OK");
     }
};

class echo_id_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) override {
         std::string body = "id=";
         body.append(req.get_arg("id"));
         return http_response::string(body);
     }
};

// Curl helper: GET url, return (response_code, body). Body is empty if curl
// fails. The webserver must already be started.
struct fetch_result {
    long response_code;  // NOLINT(runtime/int) -- libcurl CURLINFO_RESPONSE_CODE writes a long
    std::string body;
};

fetch_result fetch(const std::string& url) {
    fetch_result fr{0, {}};
    CURL* curl = curl_easy_init();
    if (!curl) return fr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fr.body);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &fr.response_code);
    curl_easy_cleanup(curl);
    return fr;
}

}  // namespace

// ---- Compile-time signature contract -----------------------------------

// (1) register_path(string, unique_ptr<http_resource>) exists, returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_path(
                      std::declval<const std::string&>(),
                      std::declval<std::unique_ptr<http_resource>>())),
                  void>,
              "register_path(const string&, unique_ptr<http_resource>) "
              "must exist and return void");

// (2) register_path(string, shared_ptr<http_resource>) exists, returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_path(
                      std::declval<const std::string&>(),
                      std::declval<std::shared_ptr<http_resource>>())),
                  void>,
              "register_path(const string&, shared_ptr<http_resource>) "
              "must exist and return void");

// (3) register_prefix(string, unique_ptr<http_resource>) exists, returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_prefix(
                      std::declval<const std::string&>(),
                      std::declval<std::unique_ptr<http_resource>>())),
                  void>,
              "register_prefix(const string&, unique_ptr<http_resource>) "
              "must exist and return void");

// (4) register_prefix(string, shared_ptr<http_resource>) exists, returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_prefix(
                      std::declval<const std::string&>(),
                      std::declval<std::shared_ptr<http_resource>>())),
                  void>,
              "register_prefix(const string&, shared_ptr<http_resource>) "
              "must exist and return void");

// (5) unregister_path / unregister_prefix exist, return void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().unregister_path(
                      std::declval<const std::string&>())),
                  void>,
              "unregister_path(const string&) must exist and return void");
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().unregister_prefix(
                      std::declval<const std::string&>())),
                  void>,
              "unregister_prefix(const string&) must exist and return void");

// (6) Negative SFINAE: register_resource is removed entirely (clean break).
//     None of its historical shapes may exist — the templated unique_ptr
//     overload, the shared_ptr overload, or the 3-arg bool-family overloads.
//     The probe covers the smart-pointer shapes (the templated/typed
//     overloads); if any survived, the call would be well-formed and
//     ::value would flip to true. Callers use register_path / register_prefix.
template <typename, typename = void>
struct has_register_resource_shared : std::false_type {};

template <typename WS>
struct has_register_resource_shared<WS, std::void_t<
    decltype(std::declval<WS&>().register_resource(
        std::declval<const std::string&>(),
        std::declval<std::shared_ptr<http_resource>>()))>> : std::true_type {};

static_assert(!has_register_resource_shared<webserver>::value,
              "register_resource(const string&, shared_ptr) must be removed");

template <typename, typename = void>
struct has_register_resource_unique : std::false_type {};

template <typename WS>
struct has_register_resource_unique<WS, std::void_t<
    decltype(std::declval<WS&>().register_resource(
        std::declval<const std::string&>(),
        std::declval<std::unique_ptr<http_resource>>()))>> : std::true_type {};

static_assert(!has_register_resource_unique<webserver>::value,
              "register_resource(const string&, unique_ptr) must be removed");

// ---- Runtime behaviour tests -------------------------------------------

LT_BEGIN_SUITE(webserver_register_path_prefix_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(webserver_register_path_prefix_suite)

// register_prefix: a longer URL (child of the registered path) must match
// the resource and serve its body.
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_prefix_matches_longer_path)
    webserver ws{create_webserver(PORT)};
    ws.register_prefix("/static", std::make_shared<ok_resource>());
    ws.start(false);

    fetch_result fr = fetch("localhost:8180/static/anything/here");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("OK"));

    ws.stop();
LT_END_AUTO_TEST(register_prefix_matches_longer_path)

// register_path: a longer URL must NOT match (404).
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_path_does_not_match_longer_path)
    webserver ws{create_webserver(PORT + 1)};
    ws.register_path("/static", std::make_shared<ok_resource>());
    ws.start(false);

    fetch_result fr = fetch("localhost:8181/static/anything/here");
    LT_CHECK_EQ(fr.response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(register_path_does_not_match_longer_path)

// register_path: parameterized exact path matches and binds the {id} arg.
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_path_parameterized_matches_exact)
    webserver ws{create_webserver(PORT + 2)};
    ws.register_path("/users/{id}", std::make_shared<echo_id_resource>());
    ws.start(false);

    fetch_result fr = fetch("localhost:8182/users/42");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("id=42"));

    ws.stop();
LT_END_AUTO_TEST(register_path_parameterized_matches_exact)

// unregister_prefix removes only a prefix registration; subsequent GET 404s.
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_prefix_then_unregister_prefix_404s)
    webserver ws{create_webserver(PORT + 3)};
    ws.register_prefix("/static", std::make_shared<ok_resource>());
    ws.start(false);

    fetch_result before = fetch("localhost:8183/static/anything");
    LT_CHECK_EQ(before.response_code, 200);

    ws.unregister_prefix("/static");

    fetch_result after = fetch("localhost:8183/static/anything");
    LT_CHECK_EQ(after.response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(register_prefix_then_unregister_prefix_404s)

// unregister_path removes only an exact registration; subsequent GET 404s.
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_path_then_unregister_path_404s)
    webserver ws{create_webserver(PORT + 4)};
    ws.register_path("/foo", std::make_shared<ok_resource>());
    ws.start(false);

    fetch_result before = fetch("localhost:8184/foo");
    LT_CHECK_EQ(before.response_code, 200);

    ws.unregister_path("/foo");

    fetch_result after = fetch("localhost:8184/foo");
    LT_CHECK_EQ(after.response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(register_path_then_unregister_path_404s)

// The umbrella `unregister_resource(path)` must handle either kind; this
// test uses two different URLs (one prefix, one path) to keep the
// assertion well-defined.
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   unregister_resource_alias_handles_both_kinds)
    // Arrange: two different URLs, one registered as prefix and one as exact.
    webserver ws{create_webserver(PORT + 5)};
    ws.register_prefix("/p", std::make_shared<ok_resource>());
    ws.register_path("/x", std::make_shared<ok_resource>());
    ws.start(false);

    // Assert (before): both are reachable before any unregister call.
    LT_CHECK_EQ(fetch("localhost:8185/p/child").response_code, 200);
    LT_CHECK_EQ(fetch("localhost:8185/x").response_code, 200);

    // Act: unregister_resource removes whichever kind is registered.
    ws.unregister_resource("/p");
    ws.unregister_resource("/x");

    // Assert (after): both routes are gone.
    LT_CHECK_EQ(fetch("localhost:8185/p/child").response_code, 404);
    LT_CHECK_EQ(fetch("localhost:8185/x").response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(unregister_resource_alias_handles_both_kinds)

// Registering the SAME path as both exact and prefix is no
// longer permitted (the (method, path) cache key cannot discriminate
// the two kinds at lookup time, so the second call now throws
// std::invalid_argument). An earlier version of this test
// double-registered the same /admin path and observed both 200s; that
// state shape is unreachable on the v2 contract.
//
// What survives as a meaningful pin: `unregister_resource` is an alias
// for unregister_path AND unregister_prefix, so calling it on a path
// registered as either kind must clear that registration. Pin both
// halves with DISTINCT paths so the alias contract is still observed
// without violating the new collision guard.
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   unregister_resource_removes_both_path_and_prefix_registrations)
    webserver ws{create_webserver(PORT + 7)};
    // Use distinct paths so the collision guard does not
    // fire. /alpha is the exact registration; /beta is the prefix
    // registration. unregister_resource must clear EITHER kind.
    ws.register_path("/alpha", std::make_shared<ok_resource>());
    ws.register_prefix("/beta", std::make_shared<ok_resource>());
    ws.start(false);

    // Both must be reachable before unregister.
    LT_CHECK_EQ(fetch("localhost:8187/alpha").response_code, 200);
    LT_CHECK_EQ(fetch("localhost:8187/beta/panel").response_code, 200);

    // unregister_resource on the exact path clears the exact entry.
    ws.unregister_resource("/alpha");
    LT_CHECK_EQ(fetch("localhost:8187/alpha").response_code, 404);

    // unregister_resource on the prefix path clears the prefix entry.
    ws.unregister_resource("/beta");
    LT_CHECK_EQ(fetch("localhost:8187/beta/panel").response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(unregister_resource_removes_both_path_and_prefix_registrations)

// Paired sentinel for the collision-detection contract.
// At the v2 contract level, registering the same path as both exact
// AND prefix throws std::invalid_argument from the second call. The
// throw must leave the first registration intact (atomicity).
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_path_and_register_prefix_on_same_path_collide)
    webserver ws{create_webserver(PORT + 27)};
    ws.register_path("/gamma", std::make_shared<ok_resource>());
    bool threw = false;
    try {
        ws.register_prefix("/gamma", std::make_shared<ok_resource>());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
    ws.start(false);

    // Original exact registration must still serve the bare path.
    LT_CHECK_EQ(fetch("localhost:8207/gamma").response_code, 200);
    // And the failed prefix registration must NOT have planted a
    // prefix entry — /gamma/child must 404.
    LT_CHECK_EQ(fetch("localhost:8207/gamma/child").response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(register_path_and_register_prefix_on_same_path_collide)

// ---- normalize_path / should_skip_auth ----------------------------------
//
// apply_normalized_segment and normalize_path live in anonymous namespace in
// webserver.cpp. The only observable path through them is
// webserver_impl::should_skip_auth, which is triggered when an auth_handler is
// set and a registered route is reached.  We probe the observable effect: if
// the normalised form of the request path matches an auth_skip_paths entry the
// request is served (200); otherwise the auth_handler returns 401.
//
// Auth handler: always returns a 401 sentinel.  When should_skip_auth fires
// (i.e., the path matches an auth_skip_paths entry), the auth alias hook
// returns hook_action::pass() so the route is dispatched (200).

namespace {

// auth_handler_ptr is std::function<std::optional<http_response>(const http_request&)>.
// Return an engaged optional with a 401 response to block the request.
std::optional<httpserver::http_response> reject_auth(const httpserver::http_request&) {
    return std::optional<httpserver::http_response>(
        httpserver::http_response::string("blocked").with_status(401));
}

// fetch_code: thin wrapper returning only the HTTP status code.
long fetch_code(const std::string& url) {  // NOLINT(runtime/int)
    return fetch(url).response_code;
}

}  // namespace

// Exact match: a path listed verbatim in auth_skip_paths is served (200).
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   auth_skip_exact_path_is_served)
    webserver ws{create_webserver(PORT + 8)
                     .auth_handler(reject_auth)
                     .auth_skip_paths({"/public"})};
    ws.register_path("/public", std::make_shared<ok_resource>());
    ws.start(false);

    // /public matches the skip list -> 200.
    LT_CHECK_EQ(fetch_code("localhost:8188/public"), 200);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_exact_path_is_served)

// Path-traversal normalization: "/a/b/../c" normalises to "/a/c".
// If "/a/c" is in auth_skip_paths the request is served (200).
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   auth_skip_dot_dot_normalizes_to_canonical_path)
    webserver ws{create_webserver(PORT + 9)
                     .auth_handler(reject_auth)
                     .auth_skip_paths({"/a/c"})};
    ws.register_path("/a/c", std::make_shared<ok_resource>());
    ws.start(false);

    // /a/b/../c -- the ".." pops "b"; normalises to /a/c -> skip list hit -> 200.
    LT_CHECK_EQ(fetch_code("localhost:8189/a/b/../c"), 200);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_dot_dot_normalizes_to_canonical_path)

// Single-dot segments are elided: "/a/./b" normalises to "/a/b".
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   auth_skip_single_dot_segment_is_elided)
    webserver ws{create_webserver(PORT + 10)
                     .auth_handler(reject_auth)
                     .auth_skip_paths({"/a/b"})};
    ws.register_path("/a/b", std::make_shared<ok_resource>());
    ws.start(false);

    // /a/./b -> elide "." -> /a/b -> skip list hit -> 200.
    LT_CHECK_EQ(fetch_code("localhost:8190/a/./b"), 200);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_single_dot_segment_is_elided)

// Path NOT in auth_skip_paths is blocked by the auth handler (401).
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   auth_not_in_skip_list_is_blocked)
    webserver ws{create_webserver(PORT + 11)
                     .auth_handler(reject_auth)
                     .auth_skip_paths({"/allowed"})};
    ws.register_path("/blocked", std::make_shared<ok_resource>());
    ws.start(false);

    // /blocked is not in the skip list -> auth_handler fires -> 401.
    LT_CHECK_EQ(fetch_code("localhost:8191/blocked"), 401);

    ws.stop();
LT_END_AUTO_TEST(auth_not_in_skip_list_is_blocked)

// Multiple leading ".." that would escape the root: normalize_path must
// clamp the stack at empty (not underflow) so the resulting path is "/".
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   auth_skip_excess_dot_dot_clamps_to_root)
    webserver ws{create_webserver(PORT + 12)
                     .auth_handler(reject_auth)
                     .auth_skip_paths({"/secure"})};
    ws.register_path("/secure", std::make_shared<ok_resource>());
    ws.start(false);

    // "/../../secure" -> two ".." with empty stack -> clamps to root ->
    // then push "secure" -> normalises to "/secure" -> skip list hit -> 200.
    LT_CHECK_EQ(fetch_code("localhost:8192/../../secure"), 200);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_excess_dot_dot_clamps_to_root)

// ---- unique_ptr ownership-transfer runtime tests -------------------------
//
// The static_asserts above verify that the unique_ptr overloads exist and
// return void. These runtime tests verify that ownership is actually
// transferred — i.e., the resource is correctly served through the
// webserver after the unique_ptr is moved in. Mirrors the
// unique_ptr_overload_compiles_and_serves test in
// webserver_register_smartptr_test.cpp for the new register_path /
// register_prefix API surface.

LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_path_unique_ptr_transfers_ownership_and_serves)
    webserver ws{create_webserver(PORT + 13)};
    // Arrange: move a unique_ptr into register_path.
    ws.register_path("/up", std::make_unique<ok_resource>());
    ws.start(false);

    // Assert: the resource is served at its exact path.
    LT_CHECK_EQ(fetch("localhost:8193/up").response_code, 200);
    LT_CHECK_EQ(fetch("localhost:8193/up").body, std::string("OK"));
    // Exact-match only: a child URL must 404.
    LT_CHECK_EQ(fetch("localhost:8193/up/child").response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(register_path_unique_ptr_transfers_ownership_and_serves)

LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_prefix_unique_ptr_transfers_ownership_and_serves)
    webserver ws{create_webserver(PORT + 14)};
    // Arrange: move a unique_ptr into register_prefix.
    ws.register_prefix("/pfx", std::make_unique<ok_resource>());
    ws.start(false);

    // Assert: the resource is served at the registered path and children.
    LT_CHECK_EQ(fetch("localhost:8194/pfx").response_code, 200);
    LT_CHECK_EQ(fetch("localhost:8194/pfx/child/deep").response_code, 200);
    LT_CHECK_EQ(fetch("localhost:8194/pfx/child/deep").body, std::string("OK"));

    ws.stop();
LT_END_AUTO_TEST(register_prefix_unique_ptr_transfers_ownership_and_serves)

// ---- Error-path tests for register_prefix --------------------------------
//
// register_path is tested for null / duplicate in
// webserver_register_smartptr_test.cpp. The tests below pin the same
// invariants on the register_prefix API surface so this TU is
// self-contained.

LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_prefix_null_shared_ptr_throws)
    webserver ws{create_webserver(PORT + 15)};
    // Passing a null shared_ptr must throw std::invalid_argument.
    LT_CHECK_THROW(ws.register_prefix("/null", std::shared_ptr<http_resource>{}));
LT_END_AUTO_TEST(register_prefix_null_shared_ptr_throws)

LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_prefix_duplicate_throws)
    webserver ws{create_webserver(PORT + 16)};
    ws.register_prefix("/dup", std::make_shared<ok_resource>());
    // A second register_prefix on the same path must throw.
    LT_CHECK_THROW(ws.register_prefix("/dup", std::make_shared<ok_resource>()));
LT_END_AUTO_TEST(register_prefix_duplicate_throws)

// ---- Cross-kind selectivity test ------------------------------------------
//
// Same-path exact+prefix coexistence is now forbidden (the
// collision guard throws on the second registration). An earlier
// version of this test set up that forbidden state; the
// updated test pins the isolation invariant on DISTINCT paths
// instead — unregister_path on /q must not touch the prefix
// registration at /q-prefix.

LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   unregister_path_leaves_prefix_registration_intact)
    webserver ws{create_webserver(PORT + 17)};
    // Arrange: distinct exact and prefix paths (the collision guard
    // forbids the same path being both kinds).
    ws.register_path("/q", std::make_shared<ok_resource>());
    ws.register_prefix("/qprefix", std::make_shared<ok_resource>());
    ws.start(false);

    // Assert (before): exact URL and prefix-child URL both served.
    LT_CHECK_EQ(fetch("localhost:8197/q").response_code, 200);
    LT_CHECK_EQ(fetch("localhost:8197/qprefix/child").response_code, 200);

    // Act: remove only the exact registration.
    ws.unregister_path("/q");

    // Assert (after): the unrelated prefix registration still serves
    // child URLs — unregister_path did not touch it.
    LT_CHECK_EQ(fetch("localhost:8197/qprefix/child").response_code, 200);

    ws.stop();
LT_END_AUTO_TEST(unregister_path_leaves_prefix_registration_intact)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
