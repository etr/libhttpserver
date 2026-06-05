/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#include "httpserver/http_response.hpp"

#include <sys/types.h>          // ssize_t (for the deferred() producer)

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "httpserver/detail/body.hpp"   // complete type for body_->~body()
#include "httpserver/http_utils.hpp"
#include "httpserver/iovec_entry.hpp"

namespace httpserver {

// -----------------------------------------------------------------------
// Layout / trait acceptance asserts (TASK-009 AC). Duplicated in
// test/unit/http_response_sbo_test.cpp; placing them in the .cpp catches
// drift on every library build, even before tests are linked.
// -----------------------------------------------------------------------
static_assert(std::is_nothrow_move_constructible_v<http_response>,
              "TASK-009 AC: move ctor must be noexcept");
static_assert(std::is_nothrow_move_assignable_v<http_response>,
              "TASK-009 AC: move assign must be noexcept");
static_assert(!std::is_copy_constructible_v<http_response>,
              "TASK-009 AC: move-only");
static_assert(!std::is_copy_assignable_v<http_response>,
              "TASK-009 AC: move-only");
static_assert(http_response::body_buf_size == 64,
              "DR-005: SBO buffer is 64 bytes");
static_assert(alignof(http_response) >= 16,
              "alignas(16) std::byte body_storage_[64] requires class "
              "alignment >= 16");

// -----------------------------------------------------------------------
// Body lifecycle helpers.
//
// destroy_body() and adopt_body_from() factor out the SBO destruct /
// adopt logic that the destructor, move ctor, and move-assign all need.
// Keeping each branch in exactly one place makes the inline-vs-heap
// discriminator impossible to get out of sync. Both helpers are
// noexcept (DR-005): destroy_body relies on body subclass dtors being
// noexcept, adopt_body_from relies on the noexcept move_into() virtual
// (statically asserted per-subclass in detail/body.hpp).
//
// Members are private; they live as out-of-line member functions so
// they have access without an extra friend declaration.
// -----------------------------------------------------------------------
void http_response::destroy_body() noexcept {
    if (body_ == nullptr) return;
    body_->~body();
    if (!body_inline_) {
        // Heap path: ::operator delete pairs with the
        // ::operator new(sizeof(T)) the factory uses (TASK-010).
        ::operator delete(body_);
    }
    // Invariant: leave in the empty/no-body state regardless of which
    // branch ran (inline dtor only, or heap dtor + operator delete).
    body_ = nullptr;
    body_inline_ = false;
}

void http_response::adopt_body_from(http_response& other) noexcept {
    if (other.body_ == nullptr) {
        return;  // source has no body; nothing to adopt
    }
    if (other.body_inline_) {
        // Placement-move into our buffer, then destroy the source's
        // inline body so the source's destructor is a no-op.
        other.body_->move_into(body_storage_);
        body_ = reinterpret_cast<detail::body*>(body_storage_);
        body_inline_ = true;
        other.body_->~body();
    } else {
        // Heap path: pointer transfer — no allocation, no copy.
        body_ = other.body_;
        body_inline_ = false;
    }
    other.body_ = nullptr;
    other.body_inline_ = false;
    // Reset the moved-from source's kind_ to empty so any code that reads
    // kind() on a moved-from http_response sees a consistent state
    // (body_ == nullptr always corresponds to body_kind::empty).
    // Findings: performance-reviewer-iter1-34 / security-reviewer-iter1-40.
    other.kind_ = body_kind::empty;
}

// -----------------------------------------------------------------------
// Destructor.
//
// De-virtualised in TASK-013: the class is `final`, so polymorphic
// destruction through a base pointer is impossible. Out-of-line because
// destroy_body() needs the complete type detail::body.
// -----------------------------------------------------------------------
http_response::~http_response() {
    destroy_body();
}

// -----------------------------------------------------------------------
// Move constructor.
//
// noexcept because every member's move is noexcept (header_map is a
// std::map, std::map move is noexcept; std::byte[64] is trivially
// movable; per-subclass body move ctors are noexcept by static_assert in
// detail/body.hpp).
// -----------------------------------------------------------------------
http_response::http_response(http_response&& other) noexcept
    : status_code_(other.status_code_),
      headers_(std::move(other.headers_)),
      footers_(std::move(other.footers_)),
      cookies_(std::move(other.cookies_)),
      structured_cookies_(std::move(other.structured_cookies_)),
      kind_(other.kind_) {
    adopt_body_from(other);
}

// -----------------------------------------------------------------------
// Move-assignment.
//
// Linearises the inline×heap inline×heap "four cases" into:
//   step 1 — destroy our existing body
//   step 2 — adopt source's body
//
// Self-assignment is guarded explicitly because step 1 would otherwise
// destroy the body we are about to read from.
// -----------------------------------------------------------------------
http_response& http_response::operator=(http_response&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy_body();
    status_code_ = other.status_code_;
    headers_ = std::move(other.headers_);
    footers_ = std::move(other.footers_);
    cookies_ = std::move(other.cookies_);
    structured_cookies_ = std::move(other.structured_cookies_);
    kind_ = other.kind_;
    adopt_body_from(other);
    return *this;
}

void http_response::shoutCAST() {
    status_code_ |= http::http_utils::shoutcast_response;
}

// -----------------------------------------------------------------------
// Fluent with_* setters (TASK-012, PRD-RSP-REQ-004).
//
// Validation helpers are in the anonymous namespace above; the & / &&
// overload pairs delegate to do_set_*() private helpers.
// -----------------------------------------------------------------------

// Shared forbidden-character set for header/footer/cookie field names
// and values. The string_view spans all three bytes including the
// embedded NUL.
namespace {
constexpr std::string_view kForbiddenFieldChars("\r\n\0", 3);

// Validates any HTTP field name/value pair for forbidden control characters
// (CR, LF, NUL — CWE-113). Used for headers, footers, and cookies.
void validate_http_field(std::string_view setter_name,
                         std::string_view key,
                         std::string_view value) {
    if (key.find_first_of(kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            std::string(setter_name) +
            ": key contains forbidden control character (CR, LF, or NUL)");
    }
    if (value.find_first_of(kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            std::string(setter_name) +
            ": value contains forbidden control character (CR, LF, or NUL)");
    }
}
}  // namespace

void http_response::do_set_header(std::string key, std::string value) {
    validate_http_field("with_header", key, value);
    headers_.insert_or_assign(std::move(key), std::move(value));
}

void http_response::do_set_footer(std::string key, std::string value) {
    validate_http_field("with_footer", key, value);
    footers_.insert_or_assign(std::move(key), std::move(value));
}

void http_response::do_set_cookie(std::string key, std::string value) {
    // Legacy v1 string-blob entry point. Validates with the v1
    // CR/LF/NUL guard (same as before TASK-064), mirrors into the
    // legacy `cookies_` map for the deprecated `get_cookie`/
    // `get_cookies` accessors, AND appends a structured cookie so the
    // dispatch path has a single render source. Pre-TASK-064 callers
    // that put a `;` in the value silently injected attributes;
    // post-TASK-064 the structured renderer is the only render source
    // and it bans `;` in cookie values -- so we keep the v1
    // validate_http_field for the legacy shim's input check and
    // simply rely on the structured renderer to enforce the stricter
    // RFC 6265 rules on emit. (Action: a legacy caller passing `;` in
    // the value will see the validation throw at to_set_cookie_header
    // time -- that is the v2 promise the task asks for.)
    validate_http_field("with_cookie", key, value);
    cookies_.insert_or_assign(key, value);
    cookie c;
    c.with_name(std::move(key)).with_value(std::move(value));
    structured_cookies_.push_back(std::move(c));
}

void http_response::do_set_cookie_struct(cookie c) {
    // Structured entry point. The cookie value was validated at its
    // own setter sites; mirror name/value into the legacy `cookies_`
    // map so the deprecated `get_cookie`/`get_cookies` accessors keep
    // returning sane data.
    cookies_.insert_or_assign(c.name(), c.value());
    structured_cookies_.push_back(std::move(c));
}

void http_response::do_set_status(int code) {
    if (code < 100 || code > 599) {
        throw std::invalid_argument(
            "with_status: HTTP status code out of range [100, 599]");
    }
    status_code_ = code;
}

http_response& http_response::with_header(std::string key,
                                          std::string value) & {
    do_set_header(std::move(key), std::move(value));
    return *this;
}

http_response&& http_response::with_header(std::string key,
                                           std::string value) && {
    do_set_header(std::move(key), std::move(value));
    return std::move(*this);
}

http_response& http_response::with_footer(std::string key,
                                          std::string value) & {
    do_set_footer(std::move(key), std::move(value));
    return *this;
}

http_response&& http_response::with_footer(std::string key,
                                           std::string value) && {
    do_set_footer(std::move(key), std::move(value));
    return std::move(*this);
}

http_response& http_response::with_cookie(std::string key,
                                          std::string value) & {
    do_set_cookie(std::move(key), std::move(value));
    return *this;
}

http_response&& http_response::with_cookie(std::string key,
                                           std::string value) && {
    do_set_cookie(std::move(key), std::move(value));
    return std::move(*this);
}

// TASK-064: structured with_cookie(cookie) overloads.
http_response& http_response::with_cookie(cookie c) & {
    do_set_cookie_struct(std::move(c));
    return *this;
}

http_response&& http_response::with_cookie(cookie c) && {
    do_set_cookie_struct(std::move(c));
    return std::move(*this);
}

http_response& http_response::with_status(int code) & {
    do_set_status(code);
    return *this;
}

http_response&& http_response::with_status(int code) && {
    do_set_status(code);
    return std::move(*this);
}

// -----------------------------------------------------------------------
// Const single-key accessors (TASK-011).
//
// All three share the same shape: heterogeneous lookup into the
// corresponding header_map (transparent header_comparator), returning an
// empty std::string_view on miss. NEVER inserts (PRD-RSP-REQ-003); the
// previous v1 accessors used `headers_[key]`, which silently inserted
// an empty entry on miss and consequently could not be const.
//
// View lifetime is documented in the class-level contract block in
// http_response.hpp.
// -----------------------------------------------------------------------
namespace {
inline std::string_view header_map_find_view(const http::header_map& m,
                                             std::string_view key) {
    auto it = m.find(key);
    if (it == m.end()) return {};
    return it->second;
}
}  // namespace

std::string_view http_response::get_header(std::string_view key) const {
    return header_map_find_view(headers_, key);
}

std::string_view http_response::get_footer(std::string_view key) const {
    return header_map_find_view(footers_, key);
}

std::string_view http_response::get_cookie(std::string_view key) const {
    return header_map_find_view(cookies_, key);
}

std::ostream &operator<< (std::ostream& os, const http_response& r) {
    os << "Response [response_code:" << r.status_code_ << "]" << std::endl;

    http::dump_header_map(os, "Headers", r.headers_);
    http::dump_header_map(os, "Footers", r.footers_);
    http::dump_header_map(os, "Cookies", r.cookies_);

    return os;
}

// -----------------------------------------------------------------------
// emplace_body — single placement-new entry point shared by all
// factories (TASK-010). Centralising the SBO-vs-heap decision here means
// the matched ::operator new(sizeof(T)) / ::operator delete pairing the
// destructor relies on (TASK-009 OQ-4) lives in exactly one place; a
// stray plain `new T(...)` in any factory would mismatch the
// destructor's ::operator delete and trip ASan immediately.
//
// Defined out-of-line in this TU because every factory in this file
// instantiates it (so no separate-TU instantiation is needed) and the
// template body needs the complete type detail::body. Per-T size+align
// guards duplicate the SBO budget asserts in detail/body.hpp so an
// over-sized future body subclass fails to compile at the factory site
// rather than silently triggering the heap fallback.
// -----------------------------------------------------------------------
template <typename T, typename... Args>
void http_response::emplace_body(body_kind k, Args&&... args) {
    static_assert(std::is_base_of_v<detail::body, T>,
                  "emplace_body: T must derive from detail::body");
    assert(body_ == nullptr &&
           "emplace_body: body slot already populated");
    if constexpr (sizeof(T) <= body_buf_size && alignof(T) <= 16) {
        // SBO inline path.
        body_ = ::new (body_storage_) T(std::forward<Args>(args)...);
        body_inline_ = true;
    } else {
        // Heap fallback. ::operator new(sizeof(T)) is paired exactly
        // with the destructor's ::operator delete(body_); a plain
        // `new T(...)` here would mismatch.
        void* mem = ::operator new(sizeof(T));
        try {
            body_ = ::new (mem) T(std::forward<Args>(args)...);
        } catch (...) {
            ::operator delete(mem);
            throw;
        }
        body_inline_ = false;
    }
    kind_ = k;
}

// -----------------------------------------------------------------------
// Static factories (TASK-010). Each factory:
//   1. constructs a default http_response (status_code_ = -1, no body),
//   2. sets the status code and any per-kind headers,
//   3. emplaces the appropriate detail::body subclass via emplace_body.
//
// The status-code defaults match v1: 200 for content-bearing bodies,
// 204 for empty(), 401 for unauthorized().
// -----------------------------------------------------------------------

http_response http_response::empty(int mhd_flags) {
    http_response r;
    r.status_code_ = http::http_utils::http_no_content;  // 204
    r.emplace_body<detail::empty_body>(body_kind::empty, mhd_flags);
    return r;
}

http_response http_response::string(std::string body,
                                    std::string content_type) {
    http_response r;
    r.status_code_ = http::http_utils::http_ok;          // 200
    r.with_header(http::http_utils::http_header_content_type,
                  std::move(content_type));
    r.emplace_body<detail::string_body>(body_kind::string,
                                        std::move(body));
    return r;
}

http_response http_response::file(std::string path) {
    http_response r;
    r.status_code_ = http::http_utils::http_ok;
    // Match v1 file_response default Content-Type. Callers can override
    // with .with_header("Content-Type", "...") in the chain.
    r.with_header(http::http_utils::http_header_content_type,
                  http::http_utils::application_octet_stream);
    r.emplace_body<detail::file_body>(body_kind::file, std::move(path));
    return r;
}

http_response http_response::iovec(std::span<const iovec_entry> entries) {
    // Deep-copy into the body's owned vector so the caller's span need
    // not outlive the response. The buffers each entry's `base` points
    // at remain BORROWED — see detail::iovec_body's lifetime contract.
    std::vector<iovec_entry> v(entries.begin(), entries.end());
    http_response r;
    r.status_code_ = http::http_utils::http_ok;
    r.emplace_body<detail::iovec_body>(body_kind::iovec, std::move(v));
    return r;
}

http_response http_response::pipe(int fd) {
    http_response r;
    r.status_code_ = http::http_utils::http_ok;
    r.emplace_body<detail::pipe_body>(body_kind::pipe, fd);
    return r;
}

http_response http_response::deferred(
        std::function<ssize_t(std::uint64_t, char*, std::size_t)> producer) {
    http_response r;
    r.status_code_ = http::http_utils::http_ok;
    r.emplace_body<detail::deferred_body>(body_kind::deferred,
                                          std::move(producer));
    return r;
}

http_response http_response::unauthorized(std::string_view scheme,
                                          std::string_view realm,
                                          std::string body) {
    // Security: reject scheme or realm values containing CR, LF, or NUL.
    // Any of these characters can be used to inject additional HTTP headers
    // into the WWW-Authenticate response header (CWE-113). This is always a
    // caller error — callers must never pass untrusted user input as scheme
    // or realm without first validating it. Throw std::invalid_argument so
    // the error is visible and cannot be silently swallowed.
    // kForbiddenFieldChars is the same constant used by validate_http_field
    // above — reused here to avoid a duplicate definition.
    if (scheme.find_first_of(kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            "http_response::unauthorized: scheme contains forbidden control "
            "character (CR, LF, or NUL)");
    }
    if (realm.find_first_of(kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            "http_response::unauthorized: realm contains forbidden control "
            "character (CR, LF, or NUL)");
    }

    // Security: escape backslash and double-quote characters inside realm
    // per RFC 7235 §2.1 quoted-string rules.  RFC 7235 allows both to be
    // escaped via the quoted-pair rule `\X`; an unescaped `"` terminates
    // the quoted-string early (CWE-116) and an unescaped `\` is
    // misinterpreted as starting an escape sequence by strict parsers.
    // Backslash must be escaped first to avoid double-escaping the
    // backslashes we insert for double-quote escaping.
    std::string escaped_realm;
    escaped_realm.reserve(realm.size());
    for (char c : realm) {
        if (c == '\\') {
            escaped_realm.push_back('\\');  // escape backslash first
        } else if (c == '"') {
            escaped_realm.push_back('\\');  // escape double-quote
        }
        escaped_realm.push_back(c);
    }

    http_response r;
    r.status_code_ = http::http_utils::http_unauthorized;        // 401
    // Build `<scheme> realm="<escaped_realm>"`. AC #3 requires byte-for-byte
    // `Basic realm="myrealm"` for the canonical case (which has no quotes).
    std::string challenge;
    challenge.reserve(scheme.size() + escaped_realm.size() + 10);
    challenge.append(scheme.data(), scheme.size());
    challenge.append(" realm=\"", 8);
    challenge.append(escaped_realm);
    challenge.push_back('"');
    r.with_header(http::http_utils::http_header_www_authenticate,
                  challenge);
    // The body slot literally holds a string_body (possibly empty), so
    // kind() reports body_kind::string. Switching to body_kind::empty
    // for the empty-body case would fork the construction path and
    // break the invariant that kind() reflects the placed-new body.
    r.emplace_body<detail::string_body>(body_kind::string,
                                        std::move(body));
    return r;
}

#ifdef HAVE_DAUTH
// TASK-062: RFC 7616 §3.3-compliant Digest challenge factory.
//
// Validates the user-supplied fields for header-injection control
// characters (CR/LF/NUL) and packs the parameters into a
// detail::digest_challenge_body. No `WWW-Authenticate` header is added
// at the response-value layer; the dispatch path (TASK-062 branch in
// materialize_and_queue_response) calls
// MHD_queue_auth_required_response3 to attach the authoritative
// challenge with nonce/opaque/algorithm/qop/charset/userhash bits.
//
// Empty opaque is preserved: the dispatch path substitutes
// webserver_impl::digest_opaque_ at queue time, so the factory remains
// side-effect-free (no webserver reference required).
http_response http_response::unauthorized(digest_challenge challenge) {
    // Same forbidden-character set as validate_http_field above.
    auto reject_ctrl_chars = [](std::string_view field,
                                std::string_view value) {
        if (value.find_first_of(kForbiddenFieldChars) !=
                std::string_view::npos) {
            throw std::invalid_argument(
                std::string("http_response::unauthorized(digest_challenge): ") +
                std::string(field) +
                " contains forbidden control character (CR, LF, or NUL)");
        }
    };
    reject_ctrl_chars("realm",  challenge.realm);
    reject_ctrl_chars("opaque", challenge.opaque);
    reject_ctrl_chars("domain", challenge.domain);
    reject_ctrl_chars("body",   challenge.body);

    detail::digest_challenge_body::params p{
        /*realm=*/        std::move(challenge.realm),
        /*opaque=*/       std::move(challenge.opaque),
        /*domain=*/       std::move(challenge.domain),
        /*body_text=*/    std::move(challenge.body),
        /*algorithm=*/    challenge.algorithm,
        /*qop_auth=*/     challenge.qop_auth,
        /*qop_auth_int=*/ challenge.qop_auth_int,
        /*signal_stale=*/ challenge.signal_stale,
        /*userhash_support=*/ challenge.userhash_support,
        /*prefer_utf8=*/  challenge.prefer_utf8,
    };

    http_response r;
    r.status_code_ = http::http_utils::http_unauthorized;        // 401
    r.emplace_body<detail::digest_challenge_body>(
        body_kind::digest_challenge, std::move(p));
    return r;
}
#endif  // HAVE_DAUTH

}  // namespace httpserver
