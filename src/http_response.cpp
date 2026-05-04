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
    body_ = nullptr;
    body_inline_ = false;
}

void http_response::adopt_body_from(http_response& o) noexcept {
    if (o.body_ == nullptr) {
        return;  // destination's body_/body_inline_ already cleared
    }
    if (o.body_inline_) {
        // Placement-move into our buffer, then destroy the source's
        // inline body so the source's destructor is a no-op.
        o.body_->move_into(body_storage_);
        body_ = reinterpret_cast<detail::body*>(body_storage_);
        body_inline_ = true;
        o.body_->~body();
    } else {
        // Heap path: pointer transfer — no allocation, no copy.
        body_ = o.body_;
        body_inline_ = false;
    }
    o.body_ = nullptr;
    o.body_inline_ = false;
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
http_response::http_response(http_response&& o) noexcept
    : status_code_(o.status_code_),
      headers_(std::move(o.headers_)),
      footers_(std::move(o.footers_)),
      cookies_(std::move(o.cookies_)),
      kind_(o.kind_) {
    adopt_body_from(o);
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
http_response& http_response::operator=(http_response&& o) noexcept {
    if (this == &o) {
        return *this;
    }
    destroy_body();
    status_code_ = o.status_code_;
    headers_ = std::move(o.headers_);
    footers_ = std::move(o.footers_);
    cookies_ = std::move(o.cookies_);
    kind_ = o.kind_;
    adopt_body_from(o);
    return *this;
}

void http_response::shoutCAST() {
    status_code_ |= http::http_utils::shoutcast_response;
}

// -----------------------------------------------------------------------
// Fluent with_* setters (TASK-012, PRD-RSP-REQ-004).
//
// Each setter has two ref-qualified overloads that delegate to a private
// do_set_*() helper containing the validation + mutation logic. The
// overloads differ only in their return statement: `& overload` returns
// *this by lvalue reference; `&& overload` returns std::move(*this).
// Centralising the mutation in a single helper means validation and
// insert_or_assign are in exactly one place per setter, not duplicated
// across every overload pair.
//
// Validation (security, TASK-012 review-pass):
//   * with_header / with_footer: reject key or value containing CR,
//     LF, or NUL — these characters can split an HTTP response and
//     inject additional headers (CWE-113).
//   * with_cookie: same CRLF/NUL rejection on name and value.
//   * with_status: code must be in [100, 599] per RFC 9110 §15.
//
// insert_or_assign — rather than `m[k] = v` — is used so the by-value
// `std::string` parameters can be moved into the map slot directly.
// -----------------------------------------------------------------------

// Shared forbidden-character set for header/footer/cookie field names
// and values. The string_view spans all three bytes including the
// embedded NUL.
namespace {
constexpr std::string_view kForbiddenFieldChars("\r\n\0", 3);

void validate_header_field(std::string_view context,
                           std::string_view key,
                           std::string_view value) {
    if (key.find_first_of(kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            std::string(context) +
            ": key contains forbidden control character (CR, LF, or NUL)");
    }
    if (value.find_first_of(kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            std::string(context) +
            ": value contains forbidden control character (CR, LF, or NUL)");
    }
}
}  // namespace

void http_response::do_set_header(std::string key, std::string value) {
    validate_header_field("with_header", key, value);
    headers_.insert_or_assign(std::move(key), std::move(value));
}

void http_response::do_set_footer(std::string key, std::string value) {
    validate_header_field("with_footer", key, value);
    footers_.insert_or_assign(std::move(key), std::move(value));
}

void http_response::do_set_cookie(std::string key, std::string value) {
    validate_header_field("with_cookie", key, value);
    cookies_.insert_or_assign(std::move(key), std::move(value));
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
    return std::string_view(it->second);
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

namespace {
inline http::header_view_map to_view_map(const http::header_map& hdr_map) {
    http::header_view_map view_map;
    for (const auto& item : hdr_map) {
        view_map[std::string_view(item.first)] = std::string_view(item.second);
    }
    return view_map;
}
}

std::ostream &operator<< (std::ostream& os, const http_response& r) {
    os << "Response [response_code:" << r.status_code_ << "]" << std::endl;

    http::dump_header_map(os, "Headers", to_view_map(r.headers_));
    http::dump_header_map(os, "Footers", to_view_map(r.footers_));
    http::dump_header_map(os, "Cookies", to_view_map(r.cookies_));

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

http_response http_response::pipe(int fd, std::size_t size_hint) {
    (void)size_hint;  // reserved for future use
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
    // kForbiddenFieldChars is the same constant used by validate_header_field
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

    // Security: escape double-quote characters inside realm per RFC 7235
    // §2.1 quoted-string rules. An unescaped " terminates the quoted-string
    // early, producing syntactically invalid header values that some parsers
    // misinterpret (CWE-116).
    std::string escaped_realm;
    escaped_realm.reserve(realm.size());
    for (char c : realm) {
        if (c == '"') {
            escaped_realm.push_back('\\');
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

}  // namespace httpserver
