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

// TASK-024: register_path / register_prefix split.
//
// Goal: prefix-vs-exact matching is now an explicit named choice, not a
// positional bool flag.
//
//   - register_path(path, ptr)   -> exact match (does NOT match a longer URL)
//   - register_prefix(path, ptr) -> prefix match (matches the path and all
//                                   children of it)
//   - register_resource(path, ptr) is kept as a [[deprecated]] alias for
//     register_path so TASK-023-era call sites still compile.
//   - The 3-arg `bool family` overloads of register_resource are gone.
//
// This TU pins both the compile-time signature contract (the new methods
// exist with the right shape; the bool-family overload is removed) and
// the runtime matching semantics (real curl round-trips against a
// running webserver).

#include <curl/curl.h>

#include <memory>
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

// (6) Negative SFINAE: the 3-arg bool-family overload of register_resource
//     must be gone. (Acceptance criterion #1 of TASK-024 pinned at compile
//     time.) The probe expression is a call with a trailing `bool` arg; if
//     such an overload existed, the call would be well-formed and ::value
//     would flip to true.
template <typename, typename = void>
struct has_bool_family_register : std::false_type {};

template <typename WS>
struct has_bool_family_register<WS, std::void_t<
    decltype(std::declval<WS&>().register_resource(
        std::declval<const std::string&>(),
        std::declval<std::shared_ptr<http_resource>>(),
        std::declval<bool>()))>> : std::true_type {};

static_assert(!has_bool_family_register<webserver>::value,
              "the bool-family register_resource overload must be removed");

template <typename, typename = void>
struct has_bool_family_register_unique : std::false_type {};

template <typename WS>
struct has_bool_family_register_unique<WS, std::void_t<
    decltype(std::declval<WS&>().register_resource(
        std::declval<const std::string&>(),
        std::declval<std::unique_ptr<http_resource>>(),
        std::declval<bool>()))>> : std::true_type {};

static_assert(!has_bool_family_register_unique<webserver>::value,
              "the bool-family register_resource unique_ptr overload must be removed");

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

// The deprecated register_resource(path, ptr) forwarder must still compile
// and behave like register_path (exact match, no longer-URL match).
// Suppress the deprecation warning locally so the test binary still
// builds with -Werror.
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   register_resource_deprecated_forwarder_behaves_like_register_path)
    webserver ws{create_webserver(PORT + 6)};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    ws.register_resource("/d", std::make_shared<ok_resource>());
#pragma GCC diagnostic pop
    ws.start(false);

    LT_CHECK_EQ(fetch("localhost:8186/d").response_code, 200);
    // Exact-match behaviour: a longer URL must 404.
    LT_CHECK_EQ(fetch("localhost:8186/d/extra").response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(register_resource_deprecated_forwarder_behaves_like_register_path)

// unregister_resource on a path registered as BOTH prefix and exact must
// remove BOTH entries in a single call. After the call, neither the exact
// URL nor any child URL should be served. This pins the atomicity contract:
// the exact-match entry and the prefix entry are gone together so there is
// no window in which one is present and the other is not.
LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   unregister_resource_removes_both_path_and_prefix_registrations)
    webserver ws{create_webserver(PORT + 7)};
    // Register the same path as both an exact handler and a prefix handler.
    ws.register_path("/admin", std::make_shared<ok_resource>());
    ws.register_prefix("/admin", std::make_shared<ok_resource>());
    ws.start(false);

    // Both must be reachable before unregister.
    LT_CHECK_EQ(fetch("localhost:8187/admin").response_code, 200);
    LT_CHECK_EQ(fetch("localhost:8187/admin/panel").response_code, 200);

    // A single unregister_resource call must atomically remove both.
    ws.unregister_resource("/admin");

    // Exact URL must be gone.
    LT_CHECK_EQ(fetch("localhost:8187/admin").response_code, 404);
    // Child URL served by the prefix entry must also be gone.
    LT_CHECK_EQ(fetch("localhost:8187/admin/panel").response_code, 404);

    ws.stop();
LT_END_AUTO_TEST(unregister_resource_removes_both_path_and_prefix_registrations)

// ---- normalize_path / should_skip_auth (finding test-quality-reviewer-iter1-2) ---
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

// auth_handler_ptr is std::function<std::shared_ptr<http_response>(const http_request&)>.
// Return a non-null 401 response to block the request.
std::shared_ptr<httpserver::http_response> reject_auth(const httpserver::http_request&) {
    return std::make_shared<httpserver::http_response>(
        std::move(httpserver::http_response::string("blocked").with_status(401)));
}

// fetch_code: thin wrapper returning only the HTTP status code.
long fetch_code(const std::string& url) {
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

// ---- unique_ptr ownership-transfer runtime tests (finding test-quality-reviewer-iter1-1) ---
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

// ---- Error-path tests for register_prefix (findings 9 / 32) -------------
//
// register_resource (deprecated alias) is tested for null / duplicate in
// webserver_register_smartptr_test.cpp. The tests below pin the same
// invariants on the new register_prefix API surface so this TU is
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

// ---- Cross-kind selectivity test (finding test-quality-reviewer-iter1-3) ---
//
// unregister_path must remove only the exact registration on a path;
// a prefix registration at the same path must survive and continue serving
// child URLs. Likewise (by symmetry) unregister_prefix must leave the
// exact registration intact. This pins the isolation invariant between
// the two registration kinds.

LT_BEGIN_AUTO_TEST(webserver_register_path_prefix_suite,
                   unregister_path_leaves_prefix_registration_intact)
    webserver ws{create_webserver(PORT + 17)};
    // Arrange: same path registered as both exact and prefix.
    ws.register_path("/q", std::make_shared<ok_resource>());
    ws.register_prefix("/q", std::make_shared<ok_resource>());
    ws.start(false);

    // Assert (before): exact URL and child URL both served.
    LT_CHECK_EQ(fetch("localhost:8197/q").response_code, 200);
    LT_CHECK_EQ(fetch("localhost:8197/q/child").response_code, 200);

    // Act: remove only the exact registration.
    ws.unregister_path("/q");

    // Assert (after): child URL via prefix registration still served.
    LT_CHECK_EQ(fetch("localhost:8197/q/child").response_code, 200);

    ws.stop();
LT_END_AUTO_TEST(unregister_path_leaves_prefix_registration_intact)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
