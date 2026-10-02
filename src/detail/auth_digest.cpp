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

// TASK-115: the Digest authentication policy implementation (RFC
// 7616, v2 parity per PRD-V3N-REQ-002/038), in-tree only -- the
// bounded parser, the keyed nonce, the ledger, and the
// algorithm-parameterized hashes; no MHD, no TLS library. The check
// pipeline (plan section 4):
//   field lookup -> no_credentials;
//   bounded parse -> malformed_credentials (the same 401 as absent;
//     v2 never answered 400 for bad credentials);
//   nonce classification -> forged/foreign/future nonce or bad
//     response: credentials_rejected (no stale); our expired or
//     nc-exhausted nonce: stale_nonce (challenge appends stale=TRUE,
//     MHD's spelling);
//   ledger admission BEFORE password verification -> replayed_nonce
//     for a non-increasing nc (401, no stale; a failed guess burns
//     its nc);
//   algorithm/realm/uri/qop material match, HA1 resolution (fixed:
//     constant-time username; source: application callback), then the
//     qop chain or the RFC 2617 legacy chain, compared through the
//     constant-time equality over the lowered hex.
// Every non-authenticated verdict carries a challenge minted from a
// FRESH nonce (entropy failure -> nonce_unavailable, 503). The
// challenge emits the pinned six fields in the pinned order and
// quoting. The fixed-credential factory precomputes HA1 and scrubs
// the cleartext (CWE-14/312); diagnostics never echo rejected values.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/auth/digest_auth.hpp>
#include <httpserver/detail/digest_hex.hpp>
#include <httpserver/detail/digest_ledger.hpp>
#include <httpserver/detail/digest_nonce.hpp>
#include <httpserver/detail/digest_params.hpp>
#include <httpserver/detail/digest_response.hpp>
#include <httpserver/detail/entropy_sys.hpp>
#include <httpserver/detail/secure_compare.hpp>
#include <httpserver/detail/secure_zero.hpp>
#include <httpserver/exchange.hpp>

namespace httpserver {

namespace auth {

namespace {

namespace digest = detail::digest;

constexpr std::string_view k_authorization_field = "Authorization";
constexpr std::string_view k_forbidden_text_chars("\r\n\0", 3);
constexpr std::string_view k_forbidden_opaque_chars("\r\n\0\"\\", 5);

char lowered(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a')
                                  : c;
}

bool ascii_iequal(std::string_view left,
                  std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (lowered(left[i]) != lowered(right[i])) return false;
    }
    return true;
}

std::uint64_t now_unix() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

const digest::hash_descriptor& descriptor_of(digest_algorithm algorithm) {
    return algorithm == digest_algorithm::sha_256
               ? digest::sha256_descriptor
               : digest::md5_descriptor;
}

void append_escaped(std::string& out, std::string_view text) {
    if (text.find_first_of("\\\"") == std::string_view::npos) {
        out.append(text);
        return;
    }
    for (const char c : text) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
}

// The pinned challenge shape, in the pinned order and quoting:
// Digest realm="<escaped>", qop="auth", algorithm=<TOKEN>,
// nonce="<hex>", opaque="<hex>", charset=UTF-8[, stale=TRUE].
std::string build_challenge(std::string_view realm,
                            const digest::hash_descriptor& descriptor,
                            std::string_view nonce_hex,
                            std::string_view opaque_hex, bool stale) {
    std::string challenge;
    challenge.reserve(96 + realm.size() + nonce_hex.size()
                      + opaque_hex.size());
    challenge.append("Digest realm=\"");
    append_escaped(challenge, realm);
    challenge.append("\", qop=\"auth\", algorithm=");
    challenge.append(descriptor.token);
    challenge.append(", nonce=\"");
    challenge.append(nonce_hex);
    challenge.append("\", opaque=\"");
    challenge.append(opaque_hex);
    challenge.append("\", charset=UTF-8");
    if (stale) challenge.append(", stale=TRUE");
    return challenge;
}

// The nc the ledger sees: the presented 8-hex value, or the implicit
// 1 of a legacy no-qop request.
std::uint32_t nc_value(const std::string& nc) noexcept {
    if (nc.empty()) return 1;
    return static_cast<std::uint32_t>(
        std::strtoul(nc.c_str(), nullptr, 16));
}

// Stage 1 -- nonce classification plus ledger admission. Returns an
// engaged result when the verdict is already decided; nullopt lets
// credential verification proceed (the slot has advanced).
std::optional<digest_auth_result> gate_nonce(
    const digest::digest_params& params, const digest::nonce_key& key,
    std::chrono::seconds ttl, std::uint32_t max_nc,
    digest::digest_ledger& ledger) {
    using digest::nc_admission;
    using digest::nonce_class;
    const std::uint64_t now = now_unix();
    switch (digest::classify_nonce(params.nonce, key, now, ttl)) {
        case nonce_class::invalid:
        case nonce_class::future:
            return digest_auth_result::credentials_rejected;
        case nonce_class::expired:
            return digest_auth_result::stale_nonce;
        case nonce_class::fresh:
            break;
    }
    const std::optional<std::vector<std::byte>> stamped =
        digest::decode_hex(params.nonce.substr(0, 16));
    if (!stamped.has_value()) {
        return digest_auth_result::credentials_rejected;
    }
    const std::uint64_t expires =
        digest::nonce_detail::load_be64(stamped->data())
        + static_cast<std::uint64_t>(ttl.count());
    const nc_admission admitted = ledger.admit(
        params.nonce, now, expires, nc_value(params.nc), max_nc);
    if (admitted == nc_admission::replayed) {
        return digest_auth_result::replayed_nonce;
    }
    if (admitted == nc_admission::exhausted) {
        return digest_auth_result::stale_nonce;
    }
    return std::nullopt;
}

// Stage 2 -- the request material the policy itself fixes: the
// algorithm token, the realm, the request target, and the qop form
// (auth, or absent for the legacy chain; auth-int is refused, the
// v2 factory posture).
bool material_accepts(const digest::digest_params& params,
                      const http::request_head& head,
                      std::string_view realm,
                      const digest::hash_descriptor& descriptor) {
    if (!ascii_iequal(params.algorithm, descriptor.token)) return false;
    if (params.realm != realm) return false;
    if (params.uri != head.raw_target) return false;
    return params.qop.empty() || ascii_iequal(params.qop, "auth");
}

// Stage 3 -- HA1: the fixed form matches the username in constant
// time and uses the precomputed value; the source form asks the
// application (false or an empty HA1 classifies as unknown).
bool resolve_ha1(const digest::digest_params& params,
                 std::string_view fixed_username,
                 std::string_view fixed_ha1,
                 const digest_ha1_source* source, std::string& ha1_out) {
    if (source == nullptr) {
        if (!detail::constant_time_equal(params.username,
                                         fixed_username)) {
            return false;
        }
        ha1_out = fixed_ha1;
        return true;
    }
    return (*source)(params.username, ha1_out) && !ha1_out.empty();
}

// Stage 4 -- the response comparison: exactly 2*digest-size hex,
// lowercased, through the constant-time equality.
bool response_accepts(const digest::digest_params& params,
                      const http::request_head& head,
                      std::string_view ha1_hex,
                      const digest::hash_descriptor& descriptor) {
    if (params.response.size() != 2 * descriptor.digest_size) {
        return false;
    }
    std::string lowered_response;
    lowered_response.reserve(params.response.size());
    for (const char c : params.response) lowered_response.push_back(lowered(c));
    const std::string ha2 = digest::compute_ha2(
        descriptor, head.request_method.name(), params.uri);
    const std::string expected =
        params.qop.empty()
            ? digest::compute_response_legacy(
                  descriptor, ha1_hex, params.nonce, ha2)
            : digest::compute_response_qop(
                  descriptor, ha1_hex, params.nonce, params.nc,
                  params.cnonce, params.qop, ha2);
    return detail::constant_time_equal(lowered_response, expected);
}

}  // namespace

// The per-policy mutable state: the nonce MAC key, the opaque value,
// the replay ledger, and the tuning the factory froze.
struct digest_auth_policy::state {
    digest::nonce_key key{};
    std::string opaque_hex;
    mutable digest::digest_ledger ledger;
    std::chrono::seconds ttl{300};
    std::uint32_t max_nc = 0;
    std::string challenge_body;

    state(digest::nonce_key mac_key, std::string opaque,
          std::chrono::seconds nonce_ttl, std::uint32_t use_cap,
          std::size_t capacity, std::string body)
        : key(mac_key),
          opaque_hex(std::move(opaque)),
          ledger(capacity),
          ttl(nonce_ttl),
          max_nc(use_cap),
          challenge_body(std::move(body)) {}
};

std::shared_ptr<const digest_auth_policy::state>
digest_auth_policy::make_state(const digest_auth_options& options) {
    const std::optional<digest::nonce_key> key = digest::mint_key();
    if (!key.has_value()) return nullptr;
    std::string opaque_hex = options.opaque;
    if (opaque_hex.empty()) {
        // The per-policy opaque: 16 bytes of OS entropy as 32 hex
        // (the v2 factory's random opaque analogue).
        std::byte drawn[16];
        const std::span<std::byte> draw(drawn);
        if (!detail::entropy::fill(draw).ok()
                && !detail::entropy::fill(draw).ok()) {
            return nullptr;
        }
        opaque_hex = digest::encode_hex(draw);
    }
    return std::make_shared<state>(
        *key, std::move(opaque_hex), options.nonce_ttl, options.max_nc,
        options.ledger_capacity, options.challenge_body);
}

bool digest_auth_verdict::allowed() const noexcept {
    return result == digest_auth_result::authenticated;
}

bool digest_auth_verdict::stale() const noexcept {
    return result == digest_auth_result::stale_nonce;
}

http::status digest_auth_verdict::challenge_status() const noexcept {
    const std::uint16_t code =
        result == digest_auth_result::nonce_unavailable ? 503 : 401;
    return http::status::from_code(code);
}

http::fields digest_auth_verdict::challenge_fields() const {
    http::fields fields;
    if (!challenge.empty()) {
        fields.append("WWW-Authenticate", challenge);
    }
    fields.append("Content-Length",
                  std::to_string(challenge_body.size()));
    return fields;
}

digest_auth_policy::digest_auth_policy() noexcept = default;

digest_auth_policy::digest_auth_policy(digest_auth_policy&&) noexcept = default;

digest_auth_policy& digest_auth_policy::operator=(
    digest_auth_policy&&) noexcept = default;

digest_auth_policy::~digest_auth_policy() = default;

namespace {

// Validates the shared factory inputs; diagnostics name the rule,
// never the value.
http::outcome validate_inputs(std::string_view realm,
                              const digest_auth_options& options) {
    if (realm.find_first_of(k_forbidden_text_chars)
        != std::string_view::npos) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "digest_auth_policy: realm contains a forbidden control "
            "character (CR, LF, or NUL)");
    }
    if (options.opaque.find_first_of(k_forbidden_opaque_chars)
        != std::string::npos) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "digest_auth_policy: opaque contains a forbidden "
            "character (CR, LF, NUL, quote, or backslash)");
    }
    if (options.challenge_body.find_first_of(k_forbidden_text_chars)
        != std::string::npos) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "digest_auth_policy: challenge_body contains a forbidden "
            "control character (CR, LF, or NUL)");
    }
    if (options.nonce_ttl.count() < 0) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "digest_auth_policy: nonce_ttl must not be negative");
    }
    if (options.ledger_capacity == 0) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "digest_auth_policy: ledger_capacity must be at least 1");
    }
    return http::outcome::okay();
}

}  // namespace

http::outcome digest_auth_policy::create(
    std::string realm, std::string username, std::string password,
    digest_auth_options options, digest_auth_policy& out) {
    const http::outcome validated = validate_inputs(realm, options);
    if (!validated.ok()) return validated;
    std::shared_ptr<const state> built_state = make_state(options);
    if (built_state == nullptr) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "digest_auth_policy: OS entropy source unavailable");
    }
    digest_auth_policy built;
    built.realm_ = std::move(realm);
    built.username_ = std::move(username);
    built.ha1_hex_ = digest::compute_ha1(
        descriptor_of(options.algorithm), built.username_, built.realm_,
        password);
    built.algorithm_ = options.algorithm;
    built.state_ = std::move(built_state);
    out = std::move(built);
    // The cleartext has served its purpose: scrub the factory's copy
    // before it frees (only HA1 remains).
    detail::secure_zero(password.data(), password.size());
    return http::outcome::okay();
}

http::outcome digest_auth_policy::create(
    std::string realm, digest_ha1_source source,
    digest_auth_options options, digest_auth_policy& out) {
    if (!source) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "digest_auth_policy: the HA1 source is empty");
    }
    const http::outcome validated = validate_inputs(realm, options);
    if (!validated.ok()) return validated;
    std::shared_ptr<const state> built_state = make_state(options);
    if (built_state == nullptr) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "digest_auth_policy: OS entropy source unavailable");
    }
    digest_auth_policy built;
    built.realm_ = std::move(realm);
    built.source_ = std::move(source);
    built.algorithm_ = options.algorithm;
    built.state_ = std::move(built_state);
    out = std::move(built);
    return http::outcome::okay();
}

std::string digest_auth_policy::minted_challenge(bool stale) const {
    if (state_ == nullptr) {
        // The unconfigured policy names the empty realm; it can mint
        // no nonce and nothing it is shown can authenticate.
        return build_challenge(realm_, descriptor_of(algorithm_), "",
                               "", stale);
    }
    const std::optional<std::string> nonce = digest::mint_nonce(
        state_->key, now_unix(), &detail::entropy::fill);
    if (!nonce.has_value()) return std::string();
    return build_challenge(realm_, descriptor_of(algorithm_), *nonce,
                           state_->opaque_hex, stale);
}

void digest_auth_policy::settle(digest_auth_verdict& verdict,
                                digest_auth_result result) const {
    verdict.result = result;
    verdict.challenge = minted_challenge(
        result == digest_auth_result::stale_nonce);
    // A configured policy that could not mint a challenge nonce maps
    // to the 503 taxonomy entry, never a 401 with an empty field.
    if (verdict.challenge.empty() && state_ != nullptr) {
        verdict.result = digest_auth_result::nonce_unavailable;
    }
}

digest_auth_verdict digest_auth_policy::check(
    const http::request_head& head) const {
    digest_auth_verdict verdict;
    if (state_ != nullptr) verdict.challenge_body = state_->challenge_body;
    const std::optional<std::string_view> credentials =
        head.head_fields.first(k_authorization_field);
    if (!credentials.has_value()) {
        settle(verdict, digest_auth_result::no_credentials);
        return verdict;
    }
    return verify(*credentials, head, std::move(verdict));
}

digest_auth_verdict digest_auth_policy::verify(
    std::string_view authorization, const http::request_head& head,
    digest_auth_verdict verdict) const {
    digest::digest_params params;
    if (digest::parse_digest_credentials(authorization, params)
        != digest::params_status::ok) {
        settle(verdict, digest_auth_result::malformed_credentials);
        return verdict;
    }
    verdict.username = params.username;

    const digest::hash_descriptor& descriptor =
        descriptor_of(algorithm_);
    // The unconfigured policy has no nonce key: a well-formed
    // credential is refused outright.
    const std::optional<digest_auth_result> gated =
        state_ == nullptr
            ? std::optional<digest_auth_result>(
                  digest_auth_result::credentials_rejected)
            : gate_nonce(params, state_->key, state_->ttl,
                         state_->max_nc, state_->ledger);
    if (gated.has_value()) {
        settle(verdict, *gated);
        return verdict;
    }
    if (!material_accepts(params, head, realm_, descriptor)) {
        settle(verdict, digest_auth_result::credentials_rejected);
        return verdict;
    }
    std::string ha1;
    const digest_ha1_source* source = source_ ? &source_ : nullptr;
    if (!resolve_ha1(params, username_, ha1_hex_, source, ha1)) {
        settle(verdict, digest_auth_result::credentials_rejected);
        return verdict;
    }
    if (response_accepts(params, head, ha1, descriptor)) {
        verdict.result = digest_auth_result::authenticated;
        return verdict;
    }
    settle(verdict, digest_auth_result::credentials_rejected);
    return verdict;
}

const std::string& digest_auth_policy::realm() const noexcept {
    return realm_;
}

digest_algorithm digest_auth_policy::algorithm() const noexcept {
    return algorithm_;
}

server::route_handler make_digest_guard(digest_auth_policy policy,
                                        server::route_handler handler) {
    return [guard = std::move(policy), next = std::move(handler)](
               exchange& x) -> task<void> {
        const digest_auth_verdict verdict = guard.check(x.head());
        if (verdict.allowed()) {
            co_await next(x);
            co_return;
        }
        const http::status status = verdict.challenge_status();
        const http::fields fields = verdict.challenge_fields();
        if (verdict.challenge_body.empty()) {
            static_cast<void>(x.respond(status, fields));
            co_return;
        }
        // The explicit Content-Length in the challenge fields drives
        // the pinned length-framed shape; the body is written once.
        static_cast<void>(x.start_response(status, fields));
        const std::byte* raw = reinterpret_cast<const std::byte*>(
            verdict.challenge_body.data());
        co_await x.writer().write(
            std::span<const std::byte>(raw, verdict.challenge_body.size()));
        co_await x.writer().finish();
    };
}

}  // namespace auth

}  // namespace httpserver
