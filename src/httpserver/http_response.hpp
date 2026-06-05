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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_HTTP_RESPONSE_HPP_
#define SRC_HTTPSERVER_HTTP_RESPONSE_HPP_

#include <sys/types.h>          // ssize_t — for the deferred() producer

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include "httpserver/body_kind.hpp"
#include "httpserver/http_arg_value.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/iovec_entry.hpp"

namespace httpserver {

// Forward-declared so http_response carries a `detail::body*` without
// pulling the private body hierarchy (and its <microhttpd.h> dependency)
// into every consumer translation unit. The complete type is required at
// destructor / move-op definition sites only; those live in the .cpp.
namespace detail { class body; }

// Forward declarations needed for the friend grants in http_response.
// body_/kind_/status_code_ are private; detail::webserver_impl dispatch
// helpers need direct access to materialise wire responses without
// widening the public API.
class webserver;
namespace detail { class webserver_impl; }

/**
 * RFC-7616 Digest auth challenge parameters (TASK-062).
 *
 * Passed by value to `http_response::unauthorized(digest_challenge)`,
 * which produces a `body_kind::digest_challenge` response. The dispatch
 * path branches on that body kind and routes through
 * libmicrohttpd's `MHD_queue_auth_required_response3` so the
 * authoritative `WWW-Authenticate: Digest ...` challenge with
 * nonce/opaque/algorithm/qop is written into the wire response.
 *
 * Field defaults mirror the RFC 7616 §3.3 "minimal challenge"
 * recommendations:
 *   * `algorithm = MD5` — the only RFC 7616 baseline algorithm;
 *     `SHA256` and `SHA512_256` are RFC-compliant alternatives.
 *   * `qop_auth = true` — RFC 7616 mandates `qop` on every challenge
 *     except in strict RFC-2069 backward-compat mode.
 *   * `prefer_utf8 = true` — `charset=UTF-8` advisory per §3.3.
 *
 * `opaque` left empty -> the dispatch path substitutes a per-server
 * random hex string seeded once at startup.
 *
 * Empty `domain` -> RFC 7616 default (challenge applies to the entire
 * origin).
 */
struct digest_challenge {
    std::string realm;
    std::string opaque       = {};        // empty -> server-substituted
    std::string domain       = {};        // optional space-separated URIs
    http::http_utils::digest_algorithm algorithm
                             = http::http_utils::digest_algorithm::MD5;
    bool qop_auth            = true;      // qop="auth"
    bool qop_auth_int        = false;     // qop="auth-int" not in v2 scope
    bool signal_stale        = false;     // set on a stale-nonce re-challenge
    bool userhash_support    = false;     // RFC 7616 §3.4.4
    bool prefer_utf8         = true;      // charset=UTF-8
    std::string body         = {};        // "access denied" body
};

/**
 * Class representing an abstraction for an Http Response. It is used from classes using these apis to send information through http protocol.
**/
// PRD-HDR-REQ-004 exemption (DR-003a): http_response is the v2 sealed
// value type and does NOT use the PIMPL pattern. It carries a 64-byte
// SBO buffer (`body_storage_`) so the polymorphic body lives inline for
// the common cases (string/empty/file/iovec/pipe/deferred), and falls
// back to a heap pointer for outsized bodies. Move-only (DR-005);
// copying a response would have to deep-copy the body, which is
// semantically wrong for fd-owning bodies and unnecessary in practice.
//
// `final` (PRD §3.5): the v1 polymorphic subclass hierarchy
// (string_response, file_response, iovec_response, pipe_response,
// deferred_response, empty_response, basic_auth_fail_response,
// digest_auth_fail_response) was removed in TASK-013; the only way to
// build a response is now through the static factories below.
class http_response final {
 public:
     // Public type-trait shim used by the SBO unit test (TASK-009) to
     // assert the exemption from PRD-HDR-REQ-004 without poking private
     // members. The trait check is `!std::is_same_v<body_pointer_type,
     // std::unique_ptr<detail::body>>`.
     using body_pointer_type = detail::body*;

     // SBO buffer size in bytes. Must equal the alignas/array spec on
     // body_storage_ below; the static_assert on alignof(http_response)
     // in http_response.cpp catches any drift.
     static constexpr std::size_t body_buf_size = 64;

     http_response() = default;

     // Move-only (DR-005, PRD-RSP-REQ-007). Copy ops are deleted because
     // a response's body may own non-copyable resources (file fds, pipe
     // fds, std::function targets) and a deep-copy would either silently
     // duplicate ownership or be a slice. v2 propagation is always by
     // move or by shared_ptr.
     http_response(const http_response&) = delete;
     http_response& operator=(const http_response&) = delete;

     // Out-of-line because both ops touch the complete type of
     // detail::body (placement-move via move_into(), destructor, or
     // ::operator delete).
     http_response(http_response&& other) noexcept;
     http_response& operator=(http_response&& other) noexcept;

     // De-virtualised in TASK-013: the class is `final`, so polymorphic
     // destruction through a base pointer is impossible. Out-of-line
     // because the body destruct + ::operator delete pairing needs the
     // complete type detail::body.
     ~http_response();

     // Body-kind discriminator (TASK-010 AC). Mirrors the kind reported
     // by the underlying detail::body, but answered without a virtual
     // call: the kind is recorded into kind_ at factory time and
     // preserved across moves. TASK-011's dispatch path will consume
     // this for its kind-specific fast paths.
     [[nodiscard]] body_kind kind() const noexcept { return kind_; }

     // -----------------------------------------------------------------
     // Static factories (TASK-010, DR-005).
     //
     // Each factory placement-news the corresponding detail::body
     // subclass into the response's SBO buffer (or, if the body ever
     // exceeds 64 bytes, onto the heap via ::operator new(sizeof(T))
     // so the destructor's matched ::operator delete pairs cleanly).
     // Replaces the v1 polymorphic *_response subclasses.
     //
     // Status-code defaults match v1: 200 for content-bearing bodies,
     // 204 for empty(), 401 for unauthorized().
     // -----------------------------------------------------------------

     // Construct a response carrying a string body. The Content-Type
     // header defaults to "text/plain"; pass a different value (for
     // example "application/json") to override. The body string is
     // stored by move so callers retain no aliasing.
     // Throws std::invalid_argument if content_type contains CR, LF, or NUL.
     [[nodiscard]] static http_response string(
         std::string body,
         std::string content_type = "text/plain");

     // Construct a response that streams a file from disk. Does NOT
     // throw on a missing or unreadable path — failure is observable at
     // dispatch time (the materialized MHD_Response is null and the
     // dispatch path renders a 500). Mirrors v1 file_response semantics.
     [[nodiscard]] static http_response file(std::string path);

     // Construct a response from a span of scatter/gather buffers. The
     // entries array is deep-copied into the body so the span need not
     // outlive the response, but the buffers each entry's `base` points
     // at remain BORROWED — they must outlive the response (and the
     // MHD_Response that response materializes).
     [[nodiscard]] static http_response iovec(
         std::span<const iovec_entry> entries);

     // Construct a response that streams from a pipe read-end. The
     // factory takes ownership of `fd` immediately. The fd is closed
     // when the materialized MHD_Response is destroyed; if the response
     // is never materialized, the http_response's destructor closes
     // it. Callers MUST NOT close `fd` after handing it off.
     [[nodiscard]] static http_response pipe(int fd);

     // Construct an empty (no-payload) response. Defaults to 204
     // No Content, matching v1 empty_response. The optional `mhd_flags`
     // argument forwards to MHD_set_response_options on the materialized
     // MHD_Response — pass `MHD_RF_HEAD_ONLY_RESPONSE` to send a HEAD-only
     // response with headers but no body, etc.
     [[nodiscard]] static http_response empty(int mhd_flags = 0);

     // Construct a response that streams from a producer callback.
     // libmicrohttpd invokes `producer(pos, buf, max)` whenever it
     // needs more bytes; the producer should return the number of
     // bytes written, MHD_CONTENT_READER_END_OF_STREAM, or
     // MHD_CONTENT_READER_END_WITH_ERROR. The producer is stored by
     // move; large captures may force std::function to heap-allocate
     // internally (independent of http_response's own SBO).
     [[nodiscard]] static http_response deferred(
         std::function<ssize_t(std::uint64_t, char*, std::size_t)> producer);

     /// Construct a 401 Unauthorized response with a WWW-Authenticate
     /// header of the form `<scheme> realm="<realm>"`. Replaces v1's
     /// basic_auth_fail_response and digest_auth_fail_response.
     ///
     /// Use this overload for `"Basic"` and for RFC-2069-compatible
     /// static `Digest realm="..."` challenges.  For full RFC 7616 §3.3
     /// Digest challenges with `nonce`, `opaque`, `algorithm`, and `qop`
     /// parameters — required by strict RFC-7616 parsers — use the
     /// `unauthorized(digest_challenge)` overload below.  The dispatch
     /// path detects a `body_kind::digest_challenge` body and routes
     /// through libmicrohttpd's `MHD_queue_auth_required_response3`,
     /// driving the per-connection nonce state machine.  See RFC 7616
     /// §3.3 (challenge format), §3.4 (Authorization validation), and
     /// §5.10 (security considerations).
     ///
     /// @see digest_challenge
     [[nodiscard]] static http_response unauthorized(
         std::string_view scheme,
         std::string_view realm,
         std::string body = {});

#ifdef HAVE_DAUTH
     /// Construct an RFC 7616 §3.3 Digest 401 Unauthorized response
     /// (TASK-062).
     ///
     /// The returned response carries a `body_kind::digest_challenge`
     /// body that the webserver dispatch path recognises: instead of
     /// `MHD_queue_response`, the dispatch site invokes
     /// `MHD_queue_auth_required_response3` (libmicrohttpd >= 0x00097701)
     /// with the parameters carried on the body.  libmicrohttpd then
     /// writes the authoritative `WWW-Authenticate: Digest ...`
     /// challenge with `nonce`, `opaque`, `algorithm`, `qop`, and
     /// (optionally) `charset=UTF-8` / `userhash=true`.
     ///
     /// The nonce is generated and replay-tracked by libmicrohttpd
     /// itself, HMAC-keyed by `create_webserver().digest_auth_random`
     /// and replay-windowed by `create_webserver().nonce_nc_size`.
     /// libhttpserver does not implement its own nonce store, in line
     /// with the "MHD's MD5/SHA-256 helpers remain the underlying
     /// primitive" architectural constraint.
     ///
     /// The `opaque` field is optional — when left empty, the dispatch
     /// path substitutes a per-webserver-instance random hex string
     /// generated once at startup.
     ///
     /// Header-injection guards: `realm`, `opaque`, `domain`, and `body`
     /// are rejected with `std::invalid_argument` if they contain CR,
     /// LF, or NUL (CWE-113).
     ///
     /// Only declared when libmicrohttpd Digest support is compiled in
     /// (`HAVE_DAUTH`); the build-time gate matches the gate on
     /// `http_request::check_digest_auth(...)`.
     ///
     /// @see http_response::unauthorized(std::string_view,std::string_view,std::string)
     [[nodiscard]] static http_response unauthorized(
         digest_challenge challenge);
#endif  // HAVE_DAUTH

     // -----------------------------------------------------------------
     // Read accessors (TASK-011, PRD-RSP-REQ-002 / PRD-RSP-REQ-003).
     //
     // Lifetime contract for the string_view-returning accessors:
     //
     // The returned view points into storage owned by *this. The view is
     // valid until ANY of the following happen:
     //   1. *this is destroyed.
     //   2. *this is moved-from (move ctor / move-assign target).
     //   3. The corresponding map is mutated for the SAME key
     //      (with_header(key, ...) replacing an existing value
     //      invalidates a view obtained from a prior get_header(key)).
     //
     // std::map's node-stability guarantee means that adding or removing
     // OTHER keys does NOT invalidate views of unrelated keys; only
     // same-key re-assignment, erase, or whole-response destruction
     // does. Multi-value headers are not modelled in v2.0 — header_map
     // is single-valued per key.
     //
     // Callers MUST NOT keep the view past the next non-const operation
     // on the response, and MUST NOT keep it past the response's
     // destruction. If a longer lifetime is required, copy into a
     // std::string.
     //
     // No noexcept on the single-key accessors: std::map::find can in
     // principle propagate a comparator exception. The map-returning
     // accessors and the trivial scalar accessors (get_status, kind) are
     // noexcept (they only return a reference / scalar member).
     // -----------------------------------------------------------------

     /// Returns the value of header `key`, or an empty view if absent.
     /// Does NOT insert on miss (PRD-RSP-REQ-003).
     /// View lifetime: see lifetime contract above.
     [[nodiscard]] std::string_view get_header(std::string_view key) const;

     /// Returns the value of footer `key`, or an empty view if absent.
     /// Does NOT insert on miss. View lifetime: see lifetime contract.
     [[nodiscard]] std::string_view get_footer(std::string_view key) const;

     /// Returns the value of cookie `key`, or an empty view if absent.
     /// Does NOT insert on miss. View lifetime: see lifetime contract.
     [[nodiscard]] std::string_view get_cookie(std::string_view key) const;

     /**
      * Method used to get all response headers.
      * @return a map<string,string> containing all headers.
     **/
     [[nodiscard]] const http::header_map& get_headers() const noexcept {
         return headers_;
     }

     /**
      * Method used to get all response footers.
      * @return a map<string,string> containing all footers.
     **/
     [[nodiscard]] const http::header_map& get_footers() const noexcept {
         return footers_;
     }

     /**
      * Method used to get all response cookies.
      * @return a map<string,string> containing all cookies.
     **/
     [[nodiscard]] const http::header_map& get_cookies() const noexcept {
         return cookies_;
     }

     /**
      * Method used to get the response status code.
      * @return The response code
     **/
     [[nodiscard]] int get_status() const noexcept {
         return status_code_;
     }

     // ------------------------------------------------------------------
     // Fluent setters (TASK-012, PRD-RSP-REQ-004).
     //
     // Each setter is overloaded on the value-category of *this so that
     // both lvalue and rvalue (factory) chains keep the response live
     // and zero-copy:
     //
     //   * The `&` overload returns http_response& so that
     //         r.with_header(k, v).with_status(s);
     //     compiles and returns *this when `r` is an lvalue.
     //   * The `&&` overload returns http_response&& so that
     //         http_response::string("hi").with_header(...).with_status(...)
     //     keeps the temporary as an rvalue end-to-end; the chain calls
     //     successive `&&` overloads on the same SBO-inline body without
     //     any intermediate move-construction or heap relocation.
     //
     // String parameters are taken by value: the body internally moves
     // them into the underlying header/footer/cookie maps via
     // insert_or_assign, so callers can either copy or move into the
     // setter without an extra allocation.
     //
     // Backward compatibility (constraint): pre-TASK-012 callers wrote
     //         r.with_header(k, v);
     // in statement form, discarding the (then `void`) return. Switching
     // the return type to a non-`[[nodiscard]]` reference is strictly
     // source-compatible — the reference is silently ignored.
     //
     // Cookie API decision (action item #4 of TASK-012): the v2.0 cookie
     // surface is the v1 (name, value) string-pair shape. `with_cookie`
     // overwrites any prior entry for `name` (the cookie map is keyed
     // case-insensitively). The value is rendered verbatim into the
     // `Set-Cookie` header by decorate_response, so callers who need
     // attributes (Path, Secure, HttpOnly, SameSite, ...) pre-format the
     // value, e.g. with_cookie("sid", "abc; Path=/; Secure; HttpOnly").
     // A structured cookie type with first-class attribute fields is
     // intentionally deferred to a follow-up task; it can be added as a
     // non-breaking overload alongside this string-pair API.
     //
     // Security warning: the value is rendered verbatim. A semicolon in the
     // value will inject synthetic cookie attributes into the same Set-Cookie
     // header (attribute injection). Callers MUST NOT pass attacker-controlled
     // data as the value without first ensuring it does not contain semicolons
     // or other Set-Cookie syntax characters. The pre-formatted attribute style
     // above is intended for trusted, developer-supplied strings only.
     //
     // Note on with_status: status replaces the stored code outright,
     // including any flag bits set by shoutCAST() (which ORs
     // MHD_ICY_FLAG into status_code_). Callers wanting both write
     // with_status(...) first and shoutCAST() second.
     // ------------------------------------------------------------------
     http_response& with_header(std::string key, std::string value) &;
     http_response&& with_header(std::string key, std::string value) &&;

     http_response& with_footer(std::string key, std::string value) &;
     http_response&& with_footer(std::string key, std::string value) &&;

     http_response& with_cookie(std::string key, std::string value) &;
     http_response&& with_cookie(std::string key, std::string value) &&;

     http_response& with_status(int code) &;
     http_response&& with_status(int code) &&;

     void shoutCAST();

 private:
     int status_code_ = -1;

     http::header_map headers_;
     http::header_map footers_;
     http::header_map cookies_;

     // SBO state for the polymorphic body. body_ is either nullptr (no
     // body), a pointer into body_storage_ (inline), or a heap pointer
     // allocated via ::operator new(sizeof(T)) + placement-new (heap
     // fallback). body_inline_ discriminates the two non-null cases so
     // the destructor knows whether to invoke ::operator delete.
     // kind_ will let dispatch sites (TASK-010/011) fast-path on body
     // kind without a virtual call.
     body_kind kind_ = body_kind::empty;
     alignas(16) std::byte body_storage_[body_buf_size]{};
     detail::body* body_ = nullptr;
     bool body_inline_ = false;

     // SBO lifecycle helpers shared by destructor / move ctor /
     // move-assign. Both noexcept (DR-005). See http_response.cpp for
     // the inline-vs-heap discriminator details.
     void destroy_body() noexcept;
     void adopt_body_from(http_response& other) noexcept;

     // Shared mutation helpers for the fluent setters (TASK-012
     // review-pass). Each helper validates its inputs, then performs the
     // map mutation or scalar assignment.  Centralising the logic here
     // means the & and && overloads only differ in their return
     // statement; the mutation + validation is in exactly one place.
     void do_set_header(std::string key, std::string value);
     void do_set_footer(std::string key, std::string value);
     void do_set_cookie(std::string key, std::string value);
     void do_set_status(int code);

     // Placement-new a concrete detail::body subclass into the SBO
     // buffer (or, if T does not fit, onto the heap via the matched
     // ::operator new(sizeof(T))/::operator delete pairing the
     // destructor relies on). Defined out-of-line in http_response.cpp
     // because it requires the complete type detail::body — it is only
     // instantiated from the factory bodies in that TU.
     //
     // Pre-condition: the response's body slot is empty
     // (default-constructed). Factories construct on a fresh
     // http_response, so this always holds; an assertion guards it.
     template <typename T, typename... Args>
     void emplace_body(body_kind k, Args&&... args);

     // Friend declarations belong in private: — friendship is unaffected
     // by access specifiers, but placing them here (rather than in a
     // misleading protected: section) signals clearly that these names can
     // bypass encapsulation; http_response is final so there are no
     // subclasses to inherit any protected access anyway.
     friend std::ostream &operator<< (std::ostream &os, const http_response &r);

     // The TASK-009 SBO unit test exercises the four-case move
     // cross-product directly through the SBO state above. Only the test
     // TU is friended; production callers go through the (forthcoming
     // TASK-010) factory functions. The friend is restricted by name and
     // does not widen the public API.
     friend struct http_response_sbo_test_access;

     // body_ is private; detail::webserver_impl dispatch helpers need
     // direct access to materialise wire responses and fire the
     // response_sent hook (body_->size()).
     friend class detail::webserver_impl;
};

std::ostream &operator<<(std::ostream &os, const http_response &r);

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_RESPONSE_HPP_
