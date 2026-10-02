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

// TASK-114 step 5: the Basic authentication policy above semantic
// request heads (RFC 7617, v2 parity per PRD-V3N-REQ-038). The suite
// pins:
//   - the challenge shape: absent, malformed, wrong-scheme and
//     rejected credentials all answer the SAME pre-built challenge
//     (401, WWW-Authenticate: Basic realm="transcript", explicit
//     Content-Length: 0);
//   - parsing: case-insensitive scheme, SP/HTAB separators, strict
//     token68/base64, first-colon split, colon-less token keeping an
//     empty password, empty user;
//   - matching: fixed credentials through the constant-time
//     comparison, equal-length wrong-user and wrong-password
//     classifying identically with byte-identical challenges;
//   - the validator form: decoded pair delivered, empty rejected;
//   - factory validation: CR/LF/NUL realms refused with out
//     untouched, backslash/quote escaping, empty realm legal;
//   - credential redaction: no challenge or diagnostic string the
//     API produces ever contains the fixed credentials or the
//     client's token;
//   - the guard adapter: unauthenticated requests answer 401 without
//     invoking the wrapped handler; authenticated ones flow through.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/auth/basic_auth.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/base64.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/server/routes.hpp>

#include "./littletest.hpp"

using httpserver::auth::basic_auth_policy;
using httpserver::auth::basic_auth_result;
using httpserver::auth::basic_auth_verdict;
using httpserver::detail::base64_encode;
using httpserver::exchange;
using httpserver::manual_executor;
using httpserver::spawn;
using httpserver::task;
using httpserver::task_result;
namespace http = httpserver::http;
namespace detail = httpserver::detail;
namespace srv = httpserver::server;

namespace {

const char* const k_user = "alice";
const char* const k_pass = "wonderland";
const char* const k_realm = "transcript";
const char* const k_valid_token = "YWxpY2U6d29uZGVybGFuZA==";
const char* const k_invalid_token = "aW52YWxpZDppbnZhbGlk";

std::string token_for(const std::string& user, const std::string& pass) {
    const std::string plain = user + ":" + pass;
    const auto* raw = reinterpret_cast<const std::byte*>(plain.data());
    return base64_encode(std::span<const std::byte>(raw, plain.size()));
}

basic_auth_policy transcript_policy() {
    basic_auth_policy policy;
    const http::outcome created = basic_auth_policy::create(
        k_realm, k_user, k_pass, policy);
    if (!created.ok()) return policy;
    return policy;
}

http::request_head request_with(std::string authorization) {
    http::request_head head;
    head.raw_target = "/secret";
    head.route_path = "/secret";
    head.request_method = http::method::known(http::method_id::get);
    head.request_protocol = http::protocol::http_1_1;
    head.head_fields.append("Host", "127.0.0.1");
    if (!authorization.empty()) {
        head.head_fields.append("Authorization",
                                std::move(authorization));
    }
    return head;
}

std::string challenge_of(const basic_auth_verdict& verdict) {
    return std::string(verdict.challenge_fields()
                           .first("WWW-Authenticate")
                           .value_or(""));
}

// The strings the API produced on one verdict path (classification,
// challenge, and no diagnostics beyond the factory outcomes asserted
// separately): none may carry credentials.
bool mentions_secret(const std::string& produced) {
    return produced.find(k_user) != std::string::npos
        || produced.find(k_pass) != std::string::npos
        || produced.find(k_valid_token) != std::string::npos
        || produced.find(k_invalid_token) != std::string::npos;
}

srv::resource_budget budget_with_routes(std::size_t capacity) {
    srv::budget_limits limits;
    limits.set(srv::resource::routes, capacity);
    return srv::resource_budget::root(limits);
}

class capturing_sink final : public detail::exchange_sink {
 public:
    void on_admit(const httpserver::body_policy&) override { }
    void on_respond(const http::status& s, const http::fields& f) override {
        ++respond_calls;
        code = s.code();
        responded = f;
    }
    void on_upgrade(const httpserver::ws_upgrade_options&) override { }
    void on_abort() override { ++abort_calls; }

    int respond_calls = 0;
    int abort_calls = 0;
    std::uint16_t code = 0;
    http::fields responded;
};

void drain(manual_executor& ex) {
    while (ex.run_pending() > 0) {
    }
}

}  // namespace

LT_BEGIN_SUITE(basic_auth_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(basic_auth_suite)

// Challenge shape: absent, malformed, wrong-scheme and rejected
// credentials all produce the byte-identical documented challenge.
LT_BEGIN_AUTO_TEST(basic_auth_suite, every_rejection_answers_same_challenge)
    const basic_auth_policy policy = transcript_policy();
    const std::string shapes[] = {
        std::string(""),                      // no credentials
        std::string("Basic !!!not-base64!!!"),  // malformed token
        std::string("Basic"),                 // missing separator+token
        std::string("Basic "),                // separator without token
        std::string("Basic =="),              // empty token (padding only)
        std::string("Basic YWxpY2U"),         // length not a multiple of 4
        std::string("Basic Zh=="),            // non-canonical base64 tail
        std::string("Bearer dG9rZW4="),       // wrong scheme
    };
    for (const std::string& shape : shapes) {
        const basic_auth_verdict verdict =
            policy.check(request_with(shape));
        LT_CHECK(!verdict.allowed());
        LT_CHECK(verdict.result == basic_auth_result::no_credentials
                 || verdict.result == basic_auth_result::malformed_credentials
                 || verdict.result == basic_auth_result::credentials_rejected);
        LT_CHECK(verdict.challenge_status().code() == 401);
        LT_CHECK(challenge_of(verdict)
                 == "Basic realm=\"transcript\"");
        const http::fields fields = verdict.challenge_fields();
        LT_CHECK(fields.first("Content-Length").value_or("") == "0");
        LT_CHECK(fields.size() == 2);
    }
    // The absent case classifies distinctly.
    const basic_auth_verdict absent = policy.check(request_with(""));
    LT_CHECK(absent.result == basic_auth_result::no_credentials);
    // Every parse failure classifies as malformed (never 400-posture).
    const basic_auth_verdict malformed =
        policy.check(request_with("Basic !!!not-base64!!!"));
    LT_CHECK(malformed.result
             == basic_auth_result::malformed_credentials);
    const basic_auth_verdict wrong_scheme =
        policy.check(request_with("Bearer dG9rZW4="));
    LT_CHECK(wrong_scheme.result
             == basic_auth_result::malformed_credentials);
    LT_END_AUTO_TEST(every_rejection_answers_same_challenge)

LT_BEGIN_AUTO_TEST(basic_auth_suite, valid_credentials_authenticate)
    const basic_auth_policy policy = transcript_policy();
    const basic_auth_verdict verdict =
        policy.check(request_with(std::string("Basic ") + k_valid_token));
    LT_CHECK(verdict.allowed());
    LT_CHECK(verdict.result == basic_auth_result::authenticated);
    LT_CHECK(verdict.user == "alice");
    LT_CHECK(verdict.password == "wonderland");
LT_END_AUTO_TEST(valid_credentials_authenticate)

LT_BEGIN_AUTO_TEST(basic_auth_suite, separators_and_scheme_case)
    const basic_auth_policy policy = transcript_policy();
    const char* spellings[] = {
        "Basic", "basic", "BASIC", "BaSiC",
    };
    for (const char* scheme : spellings) {
        const std::string header =
            std::string(scheme) + " " + k_valid_token;
        LT_CHECK(policy.check(request_with(header)).allowed());
        const std::string tabbed =
            std::string(scheme) + "\t " + k_valid_token;
        LT_CHECK(policy.check(request_with(tabbed)).allowed());
        const std::string spaced =
            std::string(scheme) + "  " + k_valid_token + " ";
        LT_CHECK(policy.check(request_with(spaced)).allowed());
    }
LT_END_AUTO_TEST(separators_and_scheme_case)

LT_BEGIN_AUTO_TEST(basic_auth_suite, first_colon_splits_password)
    const basic_auth_policy policy = transcript_policy();
    // A colon-less token keeps the octets as the user, password empty
    // (v2/MHD parity); it never authenticates against a fixed pair.
    const basic_auth_verdict colonless =
        policy.check(request_with("Basic dXNlcg=="));
    LT_CHECK(!colonless.allowed());
    LT_CHECK(colonless.user == "user");
    LT_CHECK(colonless.password.empty());
    // Empty user before the colon decodes to an empty user.
    const basic_auth_verdict empty_user =
        policy.check(request_with("Basic Omdw"));
    LT_CHECK(!empty_user.allowed());
    LT_CHECK(empty_user.user.empty());
    LT_CHECK(empty_user.password == "gp");
    // Only the FIRST colon splits; later colons stay in the password.
    const basic_auth_verdict many =
        policy.check(request_with("Basic " + token_for("a", "b:c")));
    LT_CHECK(many.user == "a");
    LT_CHECK(many.password == "b:c");
    // Valid base64 of non-UTF8 octets still parses and classifies.
    const basic_auth_verdict binary =
        policy.check(request_with("Basic ////"));
    LT_CHECK(!binary.allowed());
    LT_CHECK(binary.user == "\xff\xff\xff");
LT_END_AUTO_TEST(first_colon_splits_password)

LT_BEGIN_AUTO_TEST(basic_auth_suite, fixed_match_is_constant_time_shaped)
    const basic_auth_policy policy = transcript_policy();
    // Equal-length wrong user vs wrong password: identical
    // classification and identical challenge bytes (no decision-
    // dependent output beyond the shared enum).
    const basic_auth_verdict wrong_user = policy.check(
        request_with("Basic " + token_for("carol", k_pass)));
    const basic_auth_verdict wrong_pass = policy.check(
        request_with("Basic " + token_for(k_user, "banderlnad")));
    LT_CHECK(wrong_user.result == basic_auth_result::credentials_rejected);
    LT_CHECK(wrong_pass.result == basic_auth_result::credentials_rejected);
    LT_CHECK(wrong_user.challenge == wrong_pass.challenge);
    LT_CHECK(challenge_of(wrong_user) == challenge_of(wrong_pass));
    // The right pair in the wrong order stays rejected.
    const basic_auth_verdict swapped = policy.check(
        request_with("Basic " + token_for(k_pass, k_user)));
    LT_CHECK(!swapped.allowed());
LT_END_AUTO_TEST(fixed_match_is_constant_time_shaped)

LT_BEGIN_AUTO_TEST(basic_auth_suite, validator_form_decides)
    int calls = 0;
    std::string seen_user;
    std::string seen_pass;
    basic_auth_policy policy;
    const http::outcome created = basic_auth_policy::create(
        k_realm,
        [&](const std::string& user, const std::string& pass) {
            ++calls;
            seen_user = user;
            seen_pass = pass;
            return user == "bob" && pass == "secret";
        },
        policy);
    LT_CHECK(created.ok());
    LT_CHECK(policy.check(
        request_with("Basic " + token_for("bob", "secret"))).allowed());
    LT_CHECK(calls == 1);
    LT_CHECK(seen_user == "bob");
    LT_CHECK(seen_pass == "secret");
    const basic_auth_verdict rejected = policy.check(
        request_with("Basic " + token_for("bob", "wrong")));
    LT_CHECK(rejected.result == basic_auth_result::credentials_rejected);
    LT_CHECK(rejected.challenge_status().code() == 401);
    // The validator never runs when the header is absent or malformed.
    const int calls_before = calls;
    static_cast<void>(policy.check(request_with("")));
    static_cast<void>(policy.check(request_with("Basic !!")));
    LT_CHECK(calls == calls_before);
    // An empty validator is a configuration error.
    basic_auth_policy empty_validator;
    const http::outcome refused = basic_auth_policy::create(
        k_realm, httpserver::auth::basic_auth_validator(),
        empty_validator);
    LT_CHECK(!refused.ok());
    LT_CHECK(refused.code() == http::outcome_code::invalid_argument);
    LT_CHECK(empty_validator.check(request_with("")).result
             == basic_auth_result::no_credentials);
LT_END_AUTO_TEST(validator_form_decides)

LT_BEGIN_AUTO_TEST(basic_auth_suite, factory_validates_and_escapes_realm)
    basic_auth_policy policy;
    const http::outcome ok = basic_auth_policy::create(
        k_realm, k_user, k_pass, policy);
    LT_CHECK(ok.ok());
    LT_CHECK(policy.realm() == k_realm);

    // CR, LF and NUL in the realm are refused (CWE-113); out is
    // untouched (still the previous, valid policy).
    const std::string bad_realms[] = {
        std::string("trans\rcript"), std::string("trans\ncript"),
        std::string("trans\0cript", 11)};
    for (const std::string& bad : bad_realms) {
        const http::outcome refused =
            basic_auth_policy::create(bad, k_user, k_pass, policy);
        LT_CHECK(!refused.ok());
        LT_CHECK(refused.code() == http::outcome_code::invalid_argument);
        LT_CHECK(!refused.message().empty());
        LT_CHECK(policy.realm() == k_realm);
        LT_CHECK(mentions_secret(refused.message()) == false);
    }

    // Backslash and double-quote escape per RFC 7235 quoted-string
    // (v2 unauthorized parity); the empty realm is legal.
    basic_auth_policy escaped;
    LT_CHECK(basic_auth_policy::create(
        "say \"hi\"\\ok", k_user, k_pass, escaped).ok());
    LT_CHECK(challenge_of(escaped.check(request_with("")))
             == "Basic realm=\"say \\\"hi\\\"\\\\ok\"");
    basic_auth_policy empty_realm;
    LT_CHECK(basic_auth_policy::create("", k_user, k_pass, empty_realm).ok());
    LT_CHECK(challenge_of(empty_realm.check(request_with("")))
             == "Basic realm=\"\"");
LT_END_AUTO_TEST(factory_validates_and_escapes_realm)

LT_BEGIN_AUTO_TEST(basic_auth_suite, produced_strings_redact_credentials)
    const basic_auth_policy policy = transcript_policy();
    const std::string paths[] = {
        std::string(""), std::string("Basic !!!"),
        std::string("Bearer x"), std::string("Basic YWxpY2U"),
        std::string("Basic dXNlcg=="),
        std::string("Basic ") + k_valid_token,
        std::string("Basic ") + k_invalid_token,
    };
    for (const std::string& header : paths) {
        const basic_auth_verdict verdict =
            policy.check(request_with(header));
        LT_CHECK(mentions_secret(challenge_of(verdict)) == false);
        LT_CHECK(mentions_secret(verdict.challenge) == false);
    }
    LT_CHECK(mentions_secret(
        challenge_of(policy.check(request_with("")))) == false);
LT_END_AUTO_TEST(produced_strings_redact_credentials)

LT_BEGIN_AUTO_TEST(basic_auth_suite, guard_wraps_route_handler)
    srv::route_registry registry;
    LT_CHECK(srv::route_registry::create(budget_with_routes(4), registry).ok());
    int handler_calls = 0;
    basic_auth_policy policy = transcript_policy();
    LT_CHECK(registry.route(
        http::method::known(http::method_id::get), "/secret",
        httpserver::auth::make_basic_guard(
            std::move(policy),
            [&handler_calls](exchange& x) -> task<void> {
                ++handler_calls;
                http::fields f;
                f.append("Content-Type", "text/plain");
                f.append("Content-Length", "9");
                static_cast<void>(
                    x.respond(http::status::from_code(200), f));
                co_return;
            })).ok());

    // Unauthenticated: 401 with the exact challenge framing; the
    // wrapped handler never runs.
    {
        capturing_sink sink;
        exchange unauthenticated(request_with(""), &sink);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, unauthenticated),
              [&](task_result<void> r) {
                  ++deliveries;
                  LT_CHECK(!r.is_exception());
              });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(handler_calls, 0);
        LT_CHECK_EQ(sink.respond_calls, 1);
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(401));
        LT_CHECK(sink.responded.first("WWW-Authenticate").value_or("")
                 == "Basic realm=\"transcript\"");
        LT_CHECK(sink.responded.first("Content-Length").value_or("") == "0");
        LT_CHECK_EQ(sink.abort_calls, 0);
    }

    // Authenticated: the wrapped handler answers.
    {
        capturing_sink sink;
        exchange authenticated(
            request_with(std::string("Basic ") + k_valid_token), &sink);
        manual_executor ex;
        int deliveries = 0;
        spawn(ex, detail::run_route(registry, authenticated),
              [&](task_result<void> r) {
                  ++deliveries;
                  LT_CHECK(!r.is_exception());
              });
        drain(ex);
        LT_CHECK_EQ(deliveries, 1);
        LT_CHECK_EQ(handler_calls, 1);
        LT_CHECK_EQ(sink.respond_calls, 1);
        LT_CHECK_EQ(sink.code, static_cast<std::uint16_t>(200));
        LT_CHECK(sink.responded.first("Content-Length").value_or("") == "9");
    }
LT_END_AUTO_TEST(guard_wraps_route_handler)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
