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

// Internal — never installed, never reached by consumer code.
//
// This header is gated only by HTTPSERVER_COMPILATION (no
// _HTTPSERVER_HPP_INSIDE_ clause) because it is *not* exposed via the
// umbrella <httpserver.hpp>. Including it from the umbrella would leak
// <microhttpd.h>, <sys/uio.h>, and the body subclasses into every
// consumer translation unit — exactly what M2/M5 of v2.0 are removing.
//
// Header-hygiene contract: only library .cpp files (and build-tree unit
// tests compiled with -DHTTPSERVER_COMPILATION) may include this file.
#ifndef SRC_HTTPSERVER_DETAIL_BODY_HPP_
#define SRC_HTTPSERVER_DETAIL_BODY_HPP_

#ifndef HTTPSERVER_COMPILATION
#error "detail/body.hpp is internal; build with -DHTTPSERVER_COMPILATION."
#endif

#include <microhttpd.h>
#include <sys/types.h>      // ssize_t

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>           // unique_ptr for digest_challenge_response_body params
#include <new>              // placement-new used by move_into() overrides
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "httpserver/body_kind.hpp"
#include "httpserver/http_utils.hpp"   // digest_algorithm enum
#include "httpserver/iovec_entry.hpp"

namespace httpserver {

namespace detail {

// Polymorphic body that http_response stores in its small-buffer
// optimisation slot. materialize() walks across the C++ /
// libmicrohttpd boundary by returning a fresh MHD_Response* with NO
// headers / footers / cookies attached — those decorations are applied
// by the dispatch path, mirroring v1's
// http_response::decorate_response split.
//
// Lifetime contract: the body owns whatever payload it carries
// (std::string, std::vector, std::function, owned fd). After
// materialize() returns, libmicrohttpd holds borrowed pointers into the
// body's storage; the body must therefore outlive the MHD_Response
// (enforced through http_response's own lifetime).
class response_body {
 public:
    virtual ~response_body();

    virtual body_kind kind() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual MHD_Response* materialize() = 0;

    // Placement-move into `dst`. Concrete subclasses must placement-new
    // a moved-from copy of *this into the buffer at dst (which the caller
    // guarantees to have correct alignment and at least sizeof(*this)
    // bytes). Used by http_response's move ctor / move-assign to relocate
    // an inline-stored body across SBO buffers without copying. Must be
    // noexcept so http_response's move ops can themselves be noexcept.
    virtual void move_into(void* dst) noexcept = 0;

 protected:
    response_body() = default;
    response_body(const response_body&) = delete;
    response_body& operator=(const response_body&) = delete;
    // Move ctor is intentionally NOT deleted. Concrete subclasses opt
    // back in (each declares a noexcept move ctor) so move_into() can
    // placement-move-construct into a target buffer. The base move-assign
    // stays deleted because inline relocation never assigns into an
    // existing instance — it always destroys-and-reconstructs.
    // Move ctor is provided so subclasses can offer their own noexcept move
    // ctors. However, the body base class is NOT movable in the usual sense:
    // http_response stores bodies inline via placement-new and
    // relocates them by calling move_into(dst) rather than std::move. Callers
    // MUST NOT move-construct a body into an externally-owned heap object;
    // use move_into() to relocate an inline-stored body across SBO buffers.
    response_body(response_body&&) noexcept = default;
    response_body& operator=(response_body&&) = delete;
};

// ---------------------------------------------------------------------------
// empty_response_body — no payload. Mirrors v1 empty_response::get_raw_response.
// ---------------------------------------------------------------------------
class empty_response_body final : public response_body {
 public:
    empty_response_body() noexcept = default;
    explicit empty_response_body(int flags) noexcept : flags_(flags) {}

    empty_response_body(empty_response_body&&) noexcept = default;

    body_kind kind() const noexcept override { return body_kind::empty; }
    std::size_t size() const noexcept override { return 0; }
    MHD_Response* materialize() override;

    void move_into(void* dst) noexcept override {
        ::new (dst) empty_response_body(std::move(*this));
    }

 private:
    int flags_ = 0;
};

// ---------------------------------------------------------------------------
// string_response_body — owns a std::string buffer; passes it to MHD as
// MHD_RESPMEM_PERSISTENT (no copy, body outlives the response).
// Mirrors v1 string_response::get_raw_response.
// ---------------------------------------------------------------------------
class string_response_body final : public response_body {
 public:
    explicit string_response_body(std::string data) noexcept
        : data_(std::move(data)) {}

    string_response_body(string_response_body&&) noexcept = default;

    body_kind kind() const noexcept override { return body_kind::string; }
    std::size_t size() const noexcept override { return data_.size(); }
    MHD_Response* materialize() override;

    void move_into(void* dst) noexcept override {
        ::new (dst) string_response_body(std::move(*this));
    }

    /// Returns the body bytes. Primarily for tests.
    [[nodiscard]] const std::string& get_data() const noexcept {
        return data_;
    }

 private:
    std::string data_;
};

// ---------------------------------------------------------------------------
// file_response_body — opens the file and runs fstat at construction so that:
//   * size() is accurate immediately (no need to call materialize() first),
//     because the on-disk size is known from fstat, not from seek/tell.
//   * materialize() avoids the TOCTOU race on file size: st_size from the
//     already-obtained fstat is used directly; the fd position is never
//     changed before being handed to MHD_create_response_from_fd (CWE-367).
//   * No repeated open/fstat syscalls on a hypothetical second materialize().
//
// Caller path contract (CWE-23):
//   path_ is assumed to be a validated, canonicalized path. O_NOFOLLOW
//   blocks the final component being a symlink, but intermediate components
//   are still followed. Callers supplying user-derived paths MUST canonicalize
//   them (e.g. realpath()) before constructing file_response_body.
//
// Ownership / lifecycle:
//   * If open or fstat fails at construction, fd_ == -1 and size_ == 0;
//     materialize() will return nullptr.
//   * If materialize() succeeds, MHD owns the fd (MHD_destroy_response closes
//     it). materialized_ is set to suppress ~file_response_body's close.
//   * If materialize() is never called, ~file_response_body closes fd_.
// ---------------------------------------------------------------------------
class file_response_body final : public response_body {
 public:
    explicit file_response_body(std::string path) noexcept;
    ~file_response_body() override;

    // Hand-written move: transfers fd_ ownership and marks the source as
    // already-materialized so its destructor skips the close path
    // (otherwise the moved-from body would close the fd we just gave to
    // the destination).
    file_response_body(file_response_body&& o) noexcept;

    body_kind kind() const noexcept override { return body_kind::file; }
    std::size_t size() const noexcept override { return size_; }
    MHD_Response* materialize() override;

    void move_into(void* dst) noexcept override {
        ::new (dst) file_response_body(std::move(*this));
    }

 private:
    std::string path_;
    std::size_t size_ = 0;
    int fd_ = -1;
    bool materialized_ = false;
};

// ---------------------------------------------------------------------------
// iovec_response_body — scatter/gather buffers. The CWE-190 narrowing guard on
// entries_.size() (UINT_MAX cap) is preserved from v1
// iovec_response::get_raw_response. The reinterpret_cast to MHD_IoVec*
// is justified by the layout-pinning static_asserts in body.cpp.
//
// total_size_ is computed once at construction so size() is O(1); MHD's
// MHD_IoVec doesn't expose total length and the architecture-spec
// size() contract is "logical body size in bytes".
//
// LIFETIME CONTRACT (CWE-416):
//   iovec_response_body owns the entries_ vector (the container), but the iov_base
//   pointers inside each iovec_entry are BORROWED — they point into
//   caller-owned buffers. After materialize() returns, libmicrohttpd holds
//   those borrowed pointers until MHD_destroy_response() is called.
//
//   CALLERS MUST guarantee that all iov_base buffers (and the iovec_response_body
//   itself) outlive the MHD_Response* returned by materialize(). The
//   factory path enforces this by tying the iovec_response_body's
//   lifetime to http_response, and http_response must outlive the MHD
//   connection. Do NOT free the underlying buffer data before the
//   MHD_Response is destroyed.
//
// ALLOCATION NOTE:
//   std::vector unconditionally heap-allocates its backing store, so every
//   iovec_response_body construction performs one heap allocation. The SBO
//   static_assert only verifies that the vector control block (3 words)
//   fits in the 64-byte inline slot; the iovec_entry array itself lives on
//   the heap. This is intentional: iovec payloads are by definition
//   scatter lists of borrowed pointers, so a further small-vector
//   optimisation would only save one allocation while adding complexity.
//   The heap fallback is an accepted trade-off for iovec_response_body.
// ---------------------------------------------------------------------------
class iovec_response_body final : public response_body {
 public:
    explicit iovec_response_body(std::vector<iovec_entry> entries) noexcept
        : entries_(std::move(entries)),
          total_size_(compute_total_size(entries_)) {}

    iovec_response_body(iovec_response_body&&) noexcept = default;

    body_kind kind() const noexcept override { return body_kind::iovec; }
    std::size_t size() const noexcept override { return total_size_; }
    MHD_Response* materialize() override;

    void move_into(void* dst) noexcept override {
        ::new (dst) iovec_response_body(std::move(*this));
    }

 private:
    static std::size_t compute_total_size(
            const std::vector<iovec_entry>& entries) noexcept {
        std::size_t total = 0;
        for (const auto& e : entries) total += e.len;
        return total;
    }

    std::vector<iovec_entry> entries_;
    std::size_t total_size_;
};

// ---------------------------------------------------------------------------
// pipe_response_body — owns a read-side fd. v2 ownership contract:
//   * constructor takes ownership of fd
//   * if materialize() succeeds, MHD owns the fd (it closes on
//     MHD_destroy_response)
//   * if materialize() is never called, ~pipe_response_body() must close the fd
//     to avoid a leak (v1 didn't have to handle this because its
//     pipe_response always reached the dispatch path)
// ---------------------------------------------------------------------------
class pipe_response_body final : public response_body {
 public:
    explicit pipe_response_body(int fd) noexcept : fd_(fd) {}
    ~pipe_response_body() override;

    // Hand-written move: transfers fd_ and suppresses the source's close
    // path (mirrors file_response_body for the same reason).
    pipe_response_body(pipe_response_body&& o) noexcept;

    body_kind kind() const noexcept override { return body_kind::pipe; }
    std::size_t size() const noexcept override { return 0; }  // size unknown
    MHD_Response* materialize() override;

    void move_into(void* dst) noexcept override {
        ::new (dst) pipe_response_body(std::move(*this));
    }

 private:
    int fd_ = -1;
    // suppresses ~pipe_response_body's close — MHD owns fd_ after a successful
    // materialize() (mirrors the analogous field in file_response_body).
    bool materialized_ = false;
};

// ---------------------------------------------------------------------------
// deferred_response_body — type-erased producer callback. v1 stored a typed
// callable inside deferred_response<T>; v2 type-erases through
// std::function so the body fits the SBO budget without templating
// http_response.
//
// The trampoline is the C-callable wrapper MHD invokes; it dispatches
// to producer_. Exposed publicly (static method) so unit tests can
// drive it without a daemon.
//
// NULL GUARD (CWE-476):
//   trampoline() checks for null cls and empty producer_ before invoking
//   the callable. MHD's callback mechanism does not catch C++ exceptions;
//   a null-invoke would call std::terminate() in MHD's IO thread.
//   If cls is null or producer_ is empty, trampoline returns
//   MHD_CONTENT_READER_END_WITH_ERROR to signal an error to MHD.
//
// ALLOCATION NOTE:
//   std::function internally uses small-buffer optimisation (SBO), but
//   the SBO threshold is implementation-defined (typically 16-32 bytes on
//   libstdc++ / libc++). Lambdas that capture more than one pointer (e.g.
//   a user object reference plus a shared_ptr sentinel) will silently
//   heap-allocate inside std::function. The static_assert on
//   sizeof(deferred_response_body) only verifies that std::function's control
//   block fits in the 64-byte SBO buffer, NOT that the callable itself
//   is stored inline. Callers should minimise captures to stay within
//   std::function's internal SSO threshold if zero-allocation is required.
// ---------------------------------------------------------------------------
class deferred_response_body final : public response_body {
 public:
    using producer_type =
        std::function<ssize_t(std::uint64_t, char*, std::size_t)>;

    explicit deferred_response_body(producer_type producer) noexcept
        : producer_(std::move(producer)) {
        // Precondition: caller must not pass a null/empty callable.
        // An empty producer_ would cause trampoline() to return
        // MHD_CONTENT_READER_END_WITH_ERROR on every MHD read callback,
        // which is unlikely to be the caller's intent.
        assert(producer_ != nullptr &&
               "deferred_response_body: producer must not be empty");
    }

    deferred_response_body(deferred_response_body&&) noexcept = default;

    body_kind kind() const noexcept override { return body_kind::deferred; }
    std::size_t size() const noexcept override { return 0; }  // size unknown
    MHD_Response* materialize() override;

    void move_into(void* dst) noexcept override {
        ::new (dst) deferred_response_body(std::move(*this));
    }

    // Public so unit tests can drive it directly; also passed by name
    // to MHD_create_response_from_callback in materialize().
    static ssize_t trampoline(void* cls, std::uint64_t pos,
                              char* buf, std::size_t max);

 private:
    producer_type producer_;
};

// ---------------------------------------------------------------------------
// digest_challenge_response_body — RFC-7616 Digest auth challenge marker.
//
// Stores the parameters needed to drive
// MHD_queue_auth_required_response3 at dispatch time. materialize()
// returns the "access denied" body-only MHD_Response; the WWW-Authenticate
// header itself is NOT attached at the body layer -- the dispatch path
// branches on body_kind::digest_challenge and calls
// MHD_queue_auth_required_response3 instead of MHD_queue_response, so
// libmicrohttpd writes the authoritative challenge (with nonce, opaque,
// algorithm, qop, charset, userhash bits) directly into the connection.
//
// Parameters live on the heap inside a single unique_ptr to keep the
// body's inline footprint under the 64-byte SBO budget. The
// trade-off — one extra small allocation at factory time — is accepted
// because the alternative (promoting digest_challenge_response_body to a
// heap-fallback body via the existing oversized path) would also
// allocate, and the inline-with-unique_ptr shape keeps the SBO budget
// assertion clean.
// ---------------------------------------------------------------------------
class digest_challenge_response_body final : public response_body {
 public:
    struct params {
        std::string realm;
        std::string opaque;       // empty -> server-substituted at dispatch
        std::string domain;
        std::string body_text;    // "access denied" body string
        ::httpserver::http::http_utils::digest_algorithm algorithm;
        bool qop_auth;
        bool qop_auth_int;
        bool signal_stale;
        bool userhash_support;
        bool prefer_utf8;
    };

    // Not noexcept: std::make_unique heap-allocates and can throw
    // std::bad_alloc. Only the move constructor and move_into() (used by
    // http_response's noexcept move path) need to be noexcept.
    explicit digest_challenge_response_body(params p)
        : params_(std::make_unique<params>(std::move(p))) {}

    digest_challenge_response_body(digest_challenge_response_body&&) noexcept = default;

    body_kind kind() const noexcept override {
        return body_kind::digest_challenge;
    }
    std::size_t size() const noexcept override {
        return params_ ? params_->body_text.size() : 0;
    }
    MHD_Response* materialize() override;

    void move_into(void* dst) noexcept override {
        ::new (dst) digest_challenge_response_body(std::move(*this));
    }

    // Read-only accessor for params. Used by the dispatch path
    // (webserver_impl::materialize_and_queue_response branches on
    // kind() == digest_challenge then reaches in here for the realm,
    // opaque, algorithm, qop, etc.) and by unit tests via the SBO
    // friend hook.
    [[nodiscard]] const params& get_params() const noexcept {
        // Postcondition: params_ is always populated after construction;
        // a moved-from body becomes destructible but no caller reaches
        // get_params on a moved-from body (the http_response that owned
        // it has had its body slot reset).
        return *params_;
    }

 private:
    std::unique_ptr<params> params_;
};

// ---------------------------------------------------------------------------
// SBO budget asserts. Every concrete body must fit in the 64-byte
// buffer http_response carries. If any of these fires on a new
// platform, http_response's factory must heap-allocate that subclass
// instead (and its destructor must dispatch on body_inline_).
// ---------------------------------------------------------------------------
static_assert(sizeof(empty_response_body) <= 64,
              "empty_response_body must fit in http_response SBO (DR-005)");
static_assert(alignof(empty_response_body) <= 16,
              "empty_response_body alignment must be <= alignas(16) SBO buffer (DR-005)");
static_assert(sizeof(string_response_body) <= 64,
              "string_response_body must fit in http_response SBO (DR-005)");
static_assert(alignof(string_response_body) <= 16,
              "string_response_body alignment must be <= alignas(16) SBO buffer (DR-005)");
static_assert(sizeof(file_response_body) <= 64,
              "file_response_body must fit in http_response SBO (DR-005)");
static_assert(alignof(file_response_body) <= 16,
              "file_response_body alignment must be <= alignas(16) SBO buffer (DR-005)");
static_assert(sizeof(iovec_response_body) <= 64,
              "iovec_response_body must fit in http_response SBO (DR-005)");
static_assert(alignof(iovec_response_body) <= 16,
              "iovec_response_body alignment must be <= alignas(16) SBO buffer (DR-005)");
static_assert(sizeof(pipe_response_body) <= 64,
              "pipe_response_body must fit in http_response SBO (DR-005)");
static_assert(alignof(pipe_response_body) <= 16,
              "pipe_response_body alignment must be <= alignas(16) SBO buffer (DR-005)");
static_assert(sizeof(deferred_response_body) <= 64,
              "deferred_response_body must fit in http_response SBO (DR-005)");
static_assert(alignof(deferred_response_body) <= 16,
              "deferred_response_body alignment must be <= 16 (DR-005)");
static_assert(sizeof(digest_challenge_response_body) <= 64,
              "digest_challenge_response_body must fit in http_response SBO (DR-005)");
static_assert(alignof(digest_challenge_response_body) <= 16,
              "digest_challenge_response_body alignment must be <= 16 (DR-005)");

// Per-subclass nothrow-move contract. http_response::move_into(...) is
// noexcept, and that depends on every concrete body's move
// constructor being noexcept. If a future change to one of the members
// breaks this (e.g. swapping std::function for a wrapper that allocates
// on move), the assert fires here so the regression is caught at the
// subclass site, not buried in http_response.cpp.
static_assert(std::is_nothrow_move_constructible_v<empty_response_body>,
              "empty_response_body move ctor must be noexcept (TASK-009 / DR-005)");
static_assert(std::is_nothrow_move_constructible_v<string_response_body>,
              "string_response_body move ctor must be noexcept (TASK-009 / DR-005)");
static_assert(std::is_nothrow_move_constructible_v<file_response_body>,
              "file_response_body move ctor must be noexcept (TASK-009 / DR-005)");
static_assert(std::is_nothrow_move_constructible_v<iovec_response_body>,
              "iovec_response_body move ctor must be noexcept (TASK-009 / DR-005)");
static_assert(std::is_nothrow_move_constructible_v<pipe_response_body>,
              "pipe_response_body move ctor must be noexcept (TASK-009 / DR-005)");
static_assert(std::is_nothrow_move_constructible_v<deferred_response_body>,
              "deferred_response_body move ctor must be noexcept (TASK-009 / DR-005)");
static_assert(std::is_nothrow_move_constructible_v<digest_challenge_response_body>,
              "digest_challenge_response_body move ctor must be noexcept (DR-005)");

}  // namespace detail

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_BODY_HPP_
