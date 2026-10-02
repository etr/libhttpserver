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

// TASK-115 step 4: the Digest authentication policy above semantic
// request heads (RFC 7616, v2 parity). The suite is black-box: every
// Authorization value is computed by the INDEPENDENT test-side client
// (integ/digest_client.hpp -- deliberate upward include, the
// digest_client_self_test convention) against the challenge the
// policy itself issued, so a passing round proves the whole
// challenge/response contract, not a shared implementation. Pins:
//   - factory validation (control characters, negative ttl, zero
//     capacity, empty source) with out left untouched;
//   - the default policy: reject-everything and the six-field
//     challenge shape with the pinned order and quoting;
//   - the valid round, wrong password/uri/method/algorithm/realm,
//     forged nonces;
//   - a failed guess burns its nc: the same (nonce, nc) pair answers
//     replayed_nonce afterwards, a strictly larger nc authenticates;
//   - ttl=0 expiry -> stale_nonce with stale=TRUE and a fresh nonce;
//   - identical Authorization twice -> authenticated then
//     replayed_nonce WITHOUT stale;
//   - the legacy qop-absent round authenticates with the implicit
//     nc=1, and its identical replay is refused;
//   - the malformed corpus -> malformed_credentials;
//   - the HA1-source form (cleartext never seen) and unknown users;
//   - the challenge's explicit Content-Length matching the body;
//   - the nonce_unavailable 503 mapping (helpers, and the real
//     settle() path through a failing entropy seam);
//   - a concurrency smoke: 8 threads, each its own challenge and nc.

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Deliberate upward include: the independent RFC 7616 client.
#include "../integ/digest_client.hpp"
#include <httpserver/auth/digest_auth.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>

#include "./littletest.hpp"

namespace {

namespace auth = httpserver::auth;
namespace http = httpserver::http;
namespace dclient = httpserver_test;

const char* const k_user = "bob";
const char* const k_pass = "builder";
const char* const k_realm = "transcript";
const char* const k_body = "digest required";

auth::digest_auth_policy transcript_policy(
    auth::digest_algorithm algorithm
    = auth::digest_algorithm::md5) {
    auth::digest_auth_policy policy;
    auth::digest_auth_options options;
    options.algorithm = algorithm;
    options.challenge_body = k_body;
    static_cast<void>(auth::digest_auth_policy::create(
        k_realm, k_user, k_pass, options, policy));
    return policy;
}

http::request_head request_with(std::string authorization) {
    http::request_head head;
    head.raw_target = "/digest";
    head.route_path = "/digest";
    head.request_method = http::method::known(http::method_id::get);
    head.request_protocol = http::protocol::http_1_1;
    head.head_fields.append("Host", "127.0.0.1");
    if (!authorization.empty()) {
        head.head_fields.append("Authorization", std::move(authorization));
    }
    return head;
}

dclient::digest_hash hash_of(auth::digest_algorithm algorithm) {
    return algorithm == auth::digest_algorithm::sha_256
               ? dclient::digest_hash::sha256
               : dclient::digest_hash::md5;
}

// One full client round against @p policy: take the challenge the
// policy offered, answer it as the independent client would. The
// cnonce is drawn once and used in BOTH the response computation and
// the shipped header (two draws would sign one value and send another).
std::string answered_authorization(const auth::digest_auth_policy& policy,
                                   const char* user = k_user,
                                   const char* password = k_pass,
                                   const char* uri = "/digest",
                                   const char* method = "GET") {
    const auth::digest_auth_verdict offered =
        policy.check(request_with(""));
    const auto challenge = dclient::parse_www_authenticate(offered.challenge);
    if (!challenge.has_value()) return "";
    const std::string cnonce = dclient::make_cnonce();
    const std::string response = dclient::compute_response_cleartext(
        *challenge, hash_of(policy.algorithm()), method, uri, user,
        password, cnonce, "00000001");
    return dclient::build_authorization_header(
        *challenge, user, uri, cnonce, "00000001", response);
}

// answered_authorization's fixed-nonce generalization: one answer
// against an ALREADY-ISSUED challenge at an explicit nc, so several
// answers can race the same ledger slot (the failed-guess-burns-nc
// pin). The cnonce is drawn once and used in BOTH the response
// computation and the shipped header, as in answered_authorization.
std::string answered_authorization_at(
    const dclient::parsed_challenge& challenge, const char* password,
    const char* nc, const char* user = k_user, const char* uri = "/digest",
    const char* method = "GET") {
    const std::string cnonce = dclient::make_cnonce();
    const std::string response = dclient::compute_response_cleartext(
        challenge, dclient::digest_hash::md5, method, uri, user, password,
        cnonce, nc);
    return dclient::build_authorization_header(
        challenge, user, uri, cnonce, nc, response);
}

// The RFC 2617 legacy chain against an issued challenge: no qop, no
// nc, no cnonce -- response = H(HA1:nonce:HA2) over the independent
// test-side hash. build_authorization_header always emits qop, so the
// legacy header is hand-built to the parser's required five fields.
std::string legacy_authorization(const dclient::parsed_challenge& challenge,
                                 const char* user = k_user,
                                 const char* password = k_pass) {
    namespace dci = dclient::digest_client_internal;
    const std::string ha1 = dci::H_hex(
        dclient::digest_hash::md5,
        std::string(user) + ":" + challenge.realm + ":" + password);
    const std::string ha2 = dci::H_hex(dclient::digest_hash::md5,
                                       "GET:/digest");
    const std::string response = dci::H_hex(
        dclient::digest_hash::md5,
        ha1 + ":" + challenge.nonce + ":" + ha2);
    return std::string("Digest username=\"") + user + "\", realm=\""
        + challenge.realm + "\", nonce=\"" + challenge.nonce
        + "\", uri=\"/digest\", algorithm=MD5, response=\"" + response
        + "\", opaque=\"" + challenge.opaque + "\"";
}

std::string challenge_of(const auth::digest_auth_verdict& verdict) {
    return std::string(verdict.challenge_fields()
                           .first("WWW-Authenticate")
                           .value_or(""));
}

// The entropy seam's failing draw: counts its calls so the real-path
// test can also pin the one retry the mint performs before giving up.
int seam_draw_calls = 0;
http::outcome seam_failing_draw(std::span<std::byte>) {
    ++seam_draw_calls;
    return http::outcome(http::outcome_code::protocol_error,
                         "injected entropy failure (test)");
}

}  // namespace

LT_BEGIN_SUITE(digest_auth_policy_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(digest_auth_policy_suite)

LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, valid_round_authenticates)
    const auth::digest_auth_policy policy = transcript_policy();
    const auth::digest_auth_verdict verdict = policy.check(
        request_with(answered_authorization(policy)));
    LT_CHECK(verdict.allowed());
    LT_CHECK(verdict.result == auth::digest_auth_result::authenticated);
    LT_CHECK(verdict.username == k_user);
LT_END_AUTO_TEST(valid_round_authenticates)

// The challenge shape: the pinned six fields, in order, with the
// pinned quoting; algorithm and charset unquoted, qop quoted, nonce
// and opaque lowercase hex; Content-Length matches the body.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, challenge_shape)
    const auth::digest_auth_policy policy = transcript_policy();
    const auth::digest_auth_verdict verdict =
        policy.check(request_with(""));
    const std::string prefix =
        "Digest realm=\"transcript\", qop=\"auth\", algorithm=MD5, "
        "nonce=\"";
    LT_CHECK(verdict.challenge.rfind(prefix, 0) == 0);
    LT_CHECK(verdict.challenge.find("\", opaque=\"", 16 + 48)
             != std::string::npos);
    LT_CHECK(verdict.challenge.size() > prefix.size() + 112 + 20);
    LT_CHECK(verdict.challenge.substr(
                 verdict.challenge.size() - std::string(", charset=UTF-8").size())
             == ", charset=UTF-8");
    LT_CHECK(!verdict.stale());
    const http::fields fields = verdict.challenge_fields();
    LT_CHECK(fields.first("Content-Length").value_or("")
             == std::to_string(std::string(k_body).size()));
    LT_CHECK(fields.size() == 2);
LT_END_AUTO_TEST(challenge_shape)

LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, default_policy_rejects_everything)
    const auth::digest_auth_policy unconfigured;
    LT_CHECK(unconfigured.realm().empty());
    LT_CHECK(unconfigured.algorithm() == auth::digest_algorithm::md5);
    LT_CHECK(unconfigured.check(request_with("")).result
             == auth::digest_auth_result::no_credentials);
    const auth::digest_auth_verdict presented =
        unconfigured.check(request_with("Digest username=\"u\""));
    LT_CHECK(!presented.allowed());
    LT_CHECK(presented.challenge
             == "Digest realm=\"\", qop=\"auth\", algorithm=MD5, "
                "nonce=\"\", opaque=\"\", charset=UTF-8");
LT_END_AUTO_TEST(default_policy_rejects_everything)

LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, factory_validates_inputs)
    auth::digest_auth_policy policy = transcript_policy();
    const auto try_create = [&](std::string realm,
                                auth::digest_auth_options options) {
        auth::digest_auth_policy built;
        return auth::digest_auth_policy::create(
            std::move(realm), k_user, k_pass, std::move(options), built);
    };
    auth::digest_auth_options options;
    const std::string bad_control[] = {
        std::string("trans\rcript"), std::string("trans\ncript"),
        std::string("trans\0cript", 11)};
    for (const std::string& bad : bad_control) {
        LT_CHECK(!try_create(bad, options).ok());
    }
    options.opaque = "has\"quote";
    LT_CHECK(!try_create(k_realm, options).ok());
    options.opaque = "back\\slash";
    LT_CHECK(!try_create(k_realm, options).ok());
    options.opaque.clear();
    options.challenge_body = "bad\rbody";
    LT_CHECK(!try_create(k_realm, options).ok());
    options.challenge_body.clear();
    options.nonce_ttl = std::chrono::seconds(-1);
    LT_CHECK(!try_create(k_realm, options).ok());
    options.nonce_ttl = std::chrono::seconds(300);
    options.ledger_capacity = 0;
    LT_CHECK(!try_create(k_realm, options).ok());

    // A failed create leaves the previous policy untouched.
    LT_CHECK(policy.realm() == k_realm);
    LT_CHECK(policy.check(request_with(answered_authorization(policy)))
                 .allowed());

    // The empty HA1 source is a configuration error.
    auth::digest_auth_policy sourced;
    LT_CHECK(!auth::digest_auth_policy::create(
                 k_realm, auth::digest_ha1_source(), options, sourced)
                 .ok());
    // Diagnostics never echo the rejected values.
    const http::outcome refused =
        try_create(std::string("trans\rcript"), options);
    LT_CHECK(refused.code() == http::outcome_code::invalid_argument);
    LT_CHECK(refused.message().find("trans") == std::string::npos);
LT_END_AUTO_TEST(factory_validates_inputs)

LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, wrong_material_is_rejected)
    const auth::digest_auth_policy policy = transcript_policy();
    const auth::digest_auth_verdict wrong_password = policy.check(
        request_with(answered_authorization(policy, k_user, "wrong")));
    LT_CHECK(wrong_password.result
             == auth::digest_auth_result::credentials_rejected);
    const auth::digest_auth_verdict wrong_user = policy.check(
        request_with(answered_authorization(policy, "alice", k_pass)));
    LT_CHECK(wrong_user.result
             == auth::digest_auth_result::credentials_rejected);
    const auth::digest_auth_verdict wrong_uri = policy.check(
        request_with(answered_authorization(
            policy, k_user, k_pass, "/other")));
    LT_CHECK(wrong_uri.result
             == auth::digest_auth_result::credentials_rejected);
    const auth::digest_auth_verdict wrong_method = policy.check(
        request_with(answered_authorization(
            policy, k_user, k_pass, "/digest", "POST")));
    LT_CHECK(wrong_method.result
             == auth::digest_auth_result::credentials_rejected);

    // A SHA-256 client against the MD5 policy: algorithm mismatch.
    const auth::digest_auth_verdict wrong_algorithm = policy.check(
        request_with([&] {
            const auth::digest_auth_policy sha_policy =
                transcript_policy(auth::digest_algorithm::sha_256);
            return answered_authorization(sha_policy);
        }()));
    LT_CHECK(wrong_algorithm.result
             == auth::digest_auth_result::credentials_rejected);

    // A foreign nonce cannot authenticate (tagged key).
    const auth::digest_auth_verdict forged = policy.check(request_with(
        "Digest username=\"bob\", realm=\"transcript\", "
        "nonce=\"1111111111111111111111111111111111111111111111111111"
        "111111111111111111111111\", uri=\"/digest\", "
        "response=\"0123456789abcdef0123456789abcdef\""));
    LT_CHECK(forged.result
             == auth::digest_auth_result::credentials_rejected);
    LT_CHECK(!forged.stale());
    LT_CHECK(forged.challenge_status().code() == 401);
LT_END_AUTO_TEST(wrong_material_is_rejected)

// A failed guess burns its nc: ledger admission advances the slot
// BEFORE password verification (plan section 6.1; digest_ledger.hpp's
// anti-replay rationale), so the same (nonce, nc=00000001) pair can
// never authenticate afterwards and only a strictly larger nc
// succeeds.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, failed_guess_burns_nc)
    const auth::digest_auth_policy policy = transcript_policy();
    const auth::digest_auth_verdict offered =
        policy.check(request_with(""));
    const auto challenge = dclient::parse_www_authenticate(offered.challenge);
    LT_CHECK(challenge.has_value());

    const auth::digest_auth_verdict wrong = policy.check(request_with(
        answered_authorization_at(*challenge, "wrong", "00000001")));
    LT_CHECK(wrong.result
             == auth::digest_auth_result::credentials_rejected);

    // The SAME challenge at the SAME nc with the CORRECT password:
    // the slot burned above answers replay, never authenticated.
    const auth::digest_auth_verdict retry = policy.check(request_with(
        answered_authorization_at(*challenge, k_pass, "00000001")));
    LT_CHECK(retry.result == auth::digest_auth_result::replayed_nonce);
    LT_CHECK(!retry.stale());

    // Only a strictly larger nc can succeed against the burned slot.
    const auth::digest_auth_verdict next = policy.check(request_with(
        answered_authorization_at(*challenge, k_pass, "00000002")));
    LT_CHECK(next.allowed());
    LT_CHECK(next.username == k_user);
LT_END_AUTO_TEST(failed_guess_burns_nc)

// Identical Authorization presented twice: the first authenticates,
// the second is a replay WITHOUT the stale hint.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, replayed_nonce_has_no_stale)
    const auth::digest_auth_policy policy = transcript_policy();
    const std::string authorization =
        answered_authorization(policy);
    const auth::digest_auth_verdict first =
        policy.check(request_with(authorization));
    LT_CHECK(first.allowed());
    const auth::digest_auth_verdict second =
        policy.check(request_with(authorization));
    LT_CHECK(second.result == auth::digest_auth_result::replayed_nonce);
    LT_CHECK(!second.stale());
    LT_CHECK(second.challenge_status().code() == 401);
    LT_CHECK(second.challenge.rfind("Digest realm=\"transcript\"", 0) == 0);
    LT_CHECK(second.challenge.find("stale") == std::string::npos);
LT_END_AUTO_TEST(replayed_nonce_has_no_stale)

// The legacy qop-absent round: check() authenticates the RFC 2617
// chain (response = H(HA1:nonce:HA2), no qop/nc/cnonce) with the
// implicit nc=1, and the identical header replayed is refused as
// replayed_nonce (plan section 6.1's 'qop-absent implicit nc 1 then
// replay').
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, legacy_qop_absent_round_and_replay)
    const auth::digest_auth_policy policy = transcript_policy();
    const auth::digest_auth_verdict offered =
        policy.check(request_with(""));
    const auto challenge = dclient::parse_www_authenticate(offered.challenge);
    LT_CHECK(challenge.has_value());
    const std::string authorization = legacy_authorization(*challenge);

    const auth::digest_auth_verdict verdict =
        policy.check(request_with(authorization));
    LT_CHECK(verdict.allowed());
    LT_CHECK(verdict.result == auth::digest_auth_result::authenticated);
    LT_CHECK(verdict.username == k_user);

    // The identical qop-less header again: the implicit nc=1 slot was
    // burned by the round above.
    const auth::digest_auth_verdict replay =
        policy.check(request_with(authorization));
    LT_CHECK(replay.result == auth::digest_auth_result::replayed_nonce);
    LT_CHECK(!replay.stale());
LT_END_AUTO_TEST(legacy_qop_absent_round_and_replay)

// ttl=0: the nonce minted for the first check is expired by the time
// the answered request arrives (wall clock advanced a second), and
// the re-challenge carries stale=TRUE with a fresh nonce.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, expired_nonce_is_stale)
    auth::digest_auth_options options;
    options.nonce_ttl = std::chrono::seconds(0);
    auth::digest_auth_policy policy;
    LT_CHECK(auth::digest_auth_policy::create(
                 k_realm, k_user, k_pass, options, policy)
                 .ok());
    const auth::digest_auth_verdict offered =
        policy.check(request_with(""));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const std::string authorization = [&] {
        const auto challenge = dclient::parse_www_authenticate(
            offered.challenge);
        if (!challenge.has_value()) return std::string();
        const std::string response = dclient::compute_response_cleartext(
            *challenge, dclient::digest_hash::md5, "GET", "/digest", k_user,
            k_pass, dclient::make_cnonce(), "00000001");
        return dclient::build_authorization_header(
            *challenge, k_user, "/digest", dclient::make_cnonce(),
            "00000001", response);
    }();
    const auth::digest_auth_verdict stale =
        policy.check(request_with(authorization));
    LT_CHECK(stale.result == auth::digest_auth_result::stale_nonce);
    LT_CHECK(stale.stale());
    LT_CHECK(stale.challenge.find(", stale=TRUE")
             != std::string::npos);
    // The stale challenge offers a NEW nonce.
    LT_CHECK(stale.challenge != offered.challenge);
    LT_CHECK(stale.challenge_status().code() == 401);
LT_END_AUTO_TEST(expired_nonce_is_stale)

LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, malformed_corpus_classifies)
    const auth::digest_auth_policy policy = transcript_policy();
    const std::string cases[] = {
        std::string("Basic dXNlcg=="),
        std::string("Digest"),
        std::string("Digest username=\"bob\""),
        std::string("Digest username=\"bob\", realm=\"transcript\""),
        std::string("Digest username=\"bob\", realm=\"transcript\","
                    " nonce=\"n\", uri=\"/digest\""),
        std::string("Digest username=\"bob\", realm=\"transcript\","
                    " nonce=\"n\", uri=\"/digest\", response=\"\""),
    };
    for (const std::string& bad : cases) {
        const auth::digest_auth_verdict verdict =
            policy.check(request_with(bad));
        LT_CHECK(verdict.result
                 == auth::digest_auth_result::malformed_credentials);
        LT_CHECK(verdict.challenge_status().code() == 401);
    }

    // Well-formed but policy-refused material classifies as
    // credentials_rejected (never 400): an auth-int qop and a
    // response of the wrong hex width for the algorithm.
    const std::string refused[] = {
        std::string("Digest username=\"bob\", realm=\"transcript\","
                    " nonce=\"n\", uri=\"/digest\", qop=auth-int, "
                    "nc=00000001, cnonce=\"c\", "
                    "response=\"0123456789abcdef0123456789abcdef\""),
        std::string("Digest username=\"bob\", realm=\"transcript\","
                    " nonce=\"n\", uri=\"/digest\", qop=auth, "
                    "nc=00000001, cnonce=\"c\", response=\"short\""),
        std::string("Digest username=\"bob\", realm=\"elsewhere\","
                    " nonce=\"n\", uri=\"/digest\", qop=auth, "
                    "nc=00000001, cnonce=\"c\", "
                    "response=\"0123456789abcdef0123456789abcdef\""),
    };
    for (const std::string& bad : refused) {
        LT_CHECK(policy.check(request_with(bad)).result
                 == auth::digest_auth_result::credentials_rejected);
    }
LT_END_AUTO_TEST(malformed_corpus_classifies)

// The HA1-source form authenticates without the cleartext password
// ever reaching the policy; an unknown username rejects.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, ha1_source_form_decides)
    auth::digest_auth_options options;
    auth::digest_auth_policy policy;
    int calls = 0;
    const http::outcome created = auth::digest_auth_policy::create(
        k_realm,
        [&](const std::string& user, std::string& ha1) {
            ++calls;
            if (user != k_user) return false;
            ha1 = dclient::digest_client_internal::H_hex(
                dclient::digest_hash::md5,
                std::string(k_user) + ":" + k_realm + ":" + k_pass);
            return true;
        },
        options, policy);
    LT_CHECK(created.ok());
    const std::string authorization = answered_authorization(policy);
    const auth::digest_auth_verdict verdict =
        policy.check(request_with(authorization));
    LT_CHECK(verdict.allowed());
    LT_CHECK_EQ(calls, 1);

    // An unknown username never resolves a HA1.
    const auth::digest_auth_verdict unknown = policy.check(
        request_with(answered_authorization(policy, "eve", k_pass)));
    LT_CHECK(unknown.result
             == auth::digest_auth_result::credentials_rejected);
LT_END_AUTO_TEST(ha1_source_form_decides)

// The SHA-256 policy round-trips with the same contract.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, sha256_round_authenticates)
    const auth::digest_auth_policy policy =
        transcript_policy(auth::digest_algorithm::sha_256);
    LT_CHECK(policy.algorithm() == auth::digest_algorithm::sha_256);
    const auth::digest_auth_verdict offered =
        policy.check(request_with(""));
    LT_CHECK(offered.challenge.find("algorithm=SHA-256")
             != std::string::npos);
    const auth::digest_auth_verdict verdict = policy.check(
        request_with(answered_authorization(policy)));
    LT_CHECK(verdict.allowed());
LT_END_AUTO_TEST(sha256_round_authenticates)

// nonce_unavailable maps to 503 and carries no challenge (the
// entropy seam itself was pinned in digest_nonce_ledger).
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, nonce_unavailable_is_503)
    auth::digest_auth_verdict verdict;
    verdict.result = auth::digest_auth_result::nonce_unavailable;
    LT_CHECK(!verdict.allowed());
    LT_CHECK(verdict.challenge_status().code() == 503);
    const http::fields fields = verdict.challenge_fields();
    LT_CHECK(!fields.first("WWW-Authenticate").has_value());
    LT_CHECK(fields.first("Content-Length").value_or("") == "0");
LT_END_AUTO_TEST(nonce_unavailable_is_503)

// The real entropy-failure path: a configured policy whose challenge
// mint draws through a permanently failing seam settles to
// nonce_unavailable -- 503, no WWW-Authenticate field -- never a 401
// with an empty challenge. Driven through the REAL settle() (the
// digest_auth_test_access bridge, the webserver_test_access pattern),
// not a fabricated verdict; the draw count also pins the mint's one
// retry.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, entropy_failure_maps_to_503)
    const auth::digest_auth_policy policy = transcript_policy();
    auth::digest_auth_verdict verdict;
    verdict.challenge_body = k_body;  // as check() would carry it
    seam_draw_calls = 0;
    auth::digest_auth_test_access::settle(
        policy, verdict, auth::digest_auth_result::no_credentials,
        &seam_failing_draw);

    LT_CHECK(verdict.result
             == auth::digest_auth_result::nonce_unavailable);
    LT_CHECK(!verdict.allowed());
    LT_CHECK(verdict.challenge.empty());
    LT_CHECK(verdict.challenge_status().code() == 503);
    const http::fields fields = verdict.challenge_fields();
    LT_CHECK(!fields.first("WWW-Authenticate").has_value());
    LT_CHECK(fields.first("Content-Length").value_or("")
             == std::to_string(std::string(k_body).size()));
    LT_CHECK_EQ(seam_draw_calls, 2);  // the one retry, then give up
LT_END_AUTO_TEST(entropy_failure_maps_to_503)

// The guard adapter: unauthenticated requests are answered with the
// verdict's status/fields (the body written with the pinned
// framing); authenticated requests reach the wrapped handler. The
// exchange-level behavior is exercised end-to-end in the e2e suite;
// here the verdict helpers carry the contract.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, verdict_helpers_shape)
    const auth::digest_auth_policy policy = transcript_policy();
    const auth::digest_auth_verdict verdict =
        policy.check(request_with(""));
    LT_CHECK(!verdict.allowed());
    LT_CHECK(!verdict.stale());
    LT_CHECK(verdict.challenge_status().code() == 401);
    LT_CHECK(verdict.challenge_body == k_body);
    LT_CHECK(verdict.username.empty());
    // The challenge nonce differs across checks (fresh per check).
    const auth::digest_auth_verdict again =
        policy.check(request_with(""));
    LT_CHECK(again.challenge != verdict.challenge);
LT_END_AUTO_TEST(verdict_helpers_shape)

// Concurrency smoke: eight threads, each taking its own challenge
// and answering it; all authenticate, none observes another's nonce.
LT_BEGIN_AUTO_TEST(digest_auth_policy_suite, concurrent_rounds_authenticate)
    const auth::digest_auth_policy policy = transcript_policy();
    std::vector<std::thread> workers;
    std::vector<int> authenticated(8, 0);
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&, i] {
            const std::string authorization =
                answered_authorization(policy);
            authenticated[i] = policy.check(request_with(authorization))
                                   .allowed();
        });
    }
    for (std::thread& worker : workers) worker.join();
    for (int ok : authenticated) LT_CHECK_EQ(ok, 1);
LT_END_AUTO_TEST(concurrent_rounds_authenticate)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
