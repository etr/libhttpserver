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

#include <algorithm>
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

#include "httpserver/detail/response_body.hpp"   // complete type for body_->~response_body()
#include "httpserver/detail/http_field_validation.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/iovec_entry.hpp"

namespace httpserver {

// -----------------------------------------------------------------------
// Layout / trait acceptance asserts. Duplicated in
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
// noexcept: destroy_body relies on body subclass dtors being
// noexcept, adopt_body_from relies on the noexcept move_into() virtual
// (statically asserted per-subclass in detail/response_body.hpp).
//
// Members are private; they live as out-of-line member functions so
// they have access without an extra friend declaration.
// -----------------------------------------------------------------------
void http_response::destroy_body() noexcept {
    if (body_ == nullptr) return;
    body_->~response_body();
    if (!body_inline_) {
        // Heap path: ::operator delete pairs with the
        // ::operator new(sizeof(T)) the factory uses.
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
        body_ = reinterpret_cast<detail::response_body*>(body_storage_);
        body_inline_ = true;
        other.body_->~response_body();
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
    other.kind_ = body_kind::empty;
}

// -----------------------------------------------------------------------
// Destructor.
//
// Non-virtual: the class is `final`, so polymorphic
// destruction through a base pointer is impossible. Out-of-line because
// destroy_body() needs the complete type detail::response_body.
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
// detail/response_body.hpp).
// -----------------------------------------------------------------------
http_response::http_response(http_response&& other) noexcept
    : status_code_(other.status_code_),
      headers_(std::move(other.headers_)),
      footers_(std::move(other.footers_)),
      cookies_(std::move(other.cookies_)),
      cookies_mirror_valid_(other.cookies_mirror_valid_),
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
    cookies_mirror_valid_ = other.cookies_mirror_valid_;
    structured_cookies_ = std::move(other.structured_cookies_);
    kind_ = other.kind_;
    adopt_body_from(other);
    return *this;
}

void http_response::shoutCAST() {
    status_code_ |= http::http_utils::shoutcast_response;
}

// -----------------------------------------------------------------------
// Fluent with_* setters.
//
// Validation helpers are in the anonymous namespace above; the & / &&
// overload pairs delegate to do_set_*() private helpers.
// -----------------------------------------------------------------------

// Validates any HTTP field name/value pair for forbidden control characters
// (CR, LF, NUL — CWE-113). Used for headers, footers, and cookies.
// kForbiddenFieldChars lives in detail/http_field_validation.hpp, shared
// with the unauthorized() factories in http_response_factories.cpp.
namespace {
void validate_http_field(std::string_view setter_name,
                         std::string_view key,
                         std::string_view value) {
    if (key.find_first_of(detail::kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            std::string(setter_name) +
            ": key contains forbidden control character (CR, LF, or NUL)");
    }
    if (value.find_first_of(detail::kForbiddenFieldChars) != std::string_view::npos) {
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
    // Legacy v1 string-blob entry point. Build and validate the
    // structured cookie FIRST — with_name / with_value throw
    // std::invalid_argument for forbidden characters (CR, LF, NUL,
    // ';', '=' in names), so structured_cookies_ (the authoritative,
    // wire-rendered store) is only mutated once the input is known good.
    //
    // Note: validate_http_field is intentionally NOT called here. The
    // structured cookie::with_name / with_value setters enforce the
    // stricter RFC 6265 rules (rejecting ';' in both name and value,
    // among others) and give callers the correct error message.
    // Calling validate_http_field first would give an incorrect
    // earlier error citing 'forbidden control character' rather than
    // the real reason.
    //
    // v1 overwrite semantics: in v1,
    // calling with_cookie("sid", "old") then with_cookie("sid", "new")
    // silently overwrote — one Set-Cookie header on the wire. So the
    // structured store overwrites too: an existing structured cookie
    // with the same name is REPLACED in place (keeping its position);
    // only a genuinely new name is appended. Name equivalence uses
    // http::header_comparator — the same case-insensitive ordering used
    // by the deprecated `cookies_` mirror. The structured path
    // (do_set_cookie_struct) intentionally keeps append semantics.
    cookie new_cookie;
    new_cookie.with_name(key).with_value(value);  // throws before any mutation
    // The deprecated `cookies_` name->value mirror is now rebuilt lazily
    // from structured_cookies_ on demand (get_cookies / get_cookie), so
    // this hot setter no longer pays a red-black-tree insert whose result
    // is never read on the wire path -- it just invalidates the mirror.
    cookies_mirror_valid_ = false;
    const http::header_comparator name_less;
    auto it = std::find_if(
        structured_cookies_.begin(), structured_cookies_.end(),
        [&](const cookie& existing) {
            return !name_less(existing.name(), new_cookie.name())
                && !name_less(new_cookie.name(), existing.name());
        });
    if (it != structured_cookies_.end()) {
        *it = std::move(new_cookie);
    } else {
        structured_cookies_.push_back(std::move(new_cookie));
    }
}

void http_response::do_set_cookie_struct(cookie c) {
    // Structured entry point. structured_cookies_ is authoritative; the
    // deprecated `cookies_` name->value mirror is rebuilt lazily from it
    // (get_cookie / get_cookies), so this appends and invalidates the
    // mirror rather than eagerly inserting into a map the wire path never
    // reads.
    //
    // NOTE: cookies created by cookie::parse_cookie_header() bypass
    // all name/value validators and store raw wire bytes directly.
    // Callers MUST NOT pass parsed request cookies to this path
    // without first re-constructing them through with_name()/
    // with_value().  The render-time guard in
    // cookie::to_set_cookie_header() will throw if a forbidden byte
    // reaches the wire, providing a last line of defense.
    cookies_mirror_valid_ = false;
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

// Structured with_cookie(cookie) overloads.
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
// Const single-key accessors.
//
// All three share the same shape: heterogeneous lookup into the
// corresponding header_map (transparent header_comparator), returning an
// empty std::string_view on miss. NEVER inserts; the
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

void http_response::ensure_cookie_mirror_() const noexcept {
    // Rebuild the deprecated name->value mirror from the authoritative
    // structured_cookies_ list, on the first read after a mutation. Kept
    // noexcept (get_cookies() is declared noexcept): on allocation failure
    // we surface an empty mirror rather than propagate. Not synchronised
    // -- an http_response is owned by a single request thread, like the
    // rest of this class. Duplicate names in structured_cookies_ collapse
    // last-wins here, matching the old eager insert_or_assign path.
    if (cookies_mirror_valid_) {
        return;
    }
    try {
        cookies_.clear();
        for (const auto& c : structured_cookies_) {
            cookies_.insert_or_assign(c.name(), c.value());
        }
    } catch (...) {
        cookies_.clear();
    }
    cookies_mirror_valid_ = true;
}

std::string_view http_response::get_cookie(std::string_view key) const {
    ensure_cookie_mirror_();
    return header_map_find_view(cookies_, key);
}

std::ostream &operator<< (std::ostream& os, const http_response& r) {
    os << "Response [response_code:" << r.status_code_ << "]" << std::endl;

    r.ensure_cookie_mirror_();
    http::dump_header_map(os, "Headers", r.headers_);
    http::dump_header_map(os, "Footers", r.footers_);
    http::dump_header_map(os, "Cookies", r.cookies_);

    return os;
}

}  // namespace httpserver
