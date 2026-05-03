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
#ifndef HTTPSERVER_COMPILATION
#error "details/body.hpp is internal; build with -DHTTPSERVER_COMPILATION."
#endif

#ifndef SRC_HTTPSERVER_DETAILS_BODY_HPP_
#define SRC_HTTPSERVER_DETAILS_BODY_HPP_

#include <sys/types.h>      // ssize_t
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <microhttpd.h>
#include <sys/uio.h>        // private header may include POSIX scatter/gather

#include "httpserver/body_kind.hpp"
#include "httpserver/iovec_entry.hpp"

namespace httpserver {

namespace detail {

// Polymorphic body that http_response stores in its small-buffer
// optimisation slot (TASK-009). materialize() walks across the C++ /
// libmicrohttpd boundary by returning a fresh MHD_Response* with NO
// headers / footers / cookies attached — those decorations are applied
// by the dispatch path (TASK-011), mirroring v1's
// http_response::decorate_response split.
//
// Lifetime contract: the body owns whatever payload it carries
// (std::string, std::vector, std::function, owned fd). After
// materialize() returns, libmicrohttpd holds borrowed pointers into the
// body's storage; the body must therefore outlive the MHD_Response
// (TASK-009/011 enforce this through http_response's own lifetime).
class body {
 public:
    virtual ~body();

    virtual body_kind kind() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual MHD_Response* materialize() = 0;

 protected:
    body() = default;
    body(const body&) = delete;
    body& operator=(const body&) = delete;
    body(body&&) = delete;
    body& operator=(body&&) = delete;
};

// ---------------------------------------------------------------------------
// empty_body — no payload. Mirrors v1 empty_response::get_raw_response.
// ---------------------------------------------------------------------------
class empty_body final : public body {
 public:
    empty_body() noexcept = default;
    explicit empty_body(int flags) noexcept : flags_(flags) {}

    body_kind kind() const noexcept override { return body_kind::empty; }
    std::size_t size() const noexcept override { return 0; }
    MHD_Response* materialize() override;

 private:
    int flags_ = 0;
};

// ---------------------------------------------------------------------------
// string_body — owns a std::string buffer; passes it to MHD as
// MHD_RESPMEM_PERSISTENT (no copy, body outlives the response).
// Mirrors v1 string_response::get_raw_response.
// ---------------------------------------------------------------------------
class string_body final : public body {
 public:
    explicit string_body(std::string content) noexcept
        : content_(std::move(content)) {}

    body_kind kind() const noexcept override { return body_kind::string; }
    std::size_t size() const noexcept override { return content_.size(); }
    MHD_Response* materialize() override;

 private:
    std::string content_;
};

// ---------------------------------------------------------------------------
// file_body — opens path on materialize(); returns nullptr if open or
// fstat fails (matches v1 file_response::get_raw_response exactly).
// size_cached_ is reserved for future use; size() currently returns it
// untouched (set on materialize) so the value reflects the on-disk size
// only after a successful materialise. This matches v1, which never
// exposed a size accessor at all.
// ---------------------------------------------------------------------------
class file_body final : public body {
 public:
    explicit file_body(std::string path) noexcept : path_(std::move(path)) {}

    body_kind kind() const noexcept override { return body_kind::file; }
    std::size_t size() const noexcept override { return size_cached_; }
    MHD_Response* materialize() override;

 private:
    std::string path_;
    std::size_t size_cached_ = 0;
};

// ---------------------------------------------------------------------------
// iovec_body — scatter/gather buffers. The CWE-190 narrowing guard on
// entries_.size() (UINT_MAX cap) is preserved from v1
// iovec_response::get_raw_response. The reinterpret_cast to MHD_IoVec*
// is justified by the layout-pinning static_asserts in body.cpp.
//
// total_size_ is computed once at construction so size() is O(1); MHD's
// MHD_IoVec doesn't expose total length and the architecture-spec
// size() contract is "logical body size in bytes".
// ---------------------------------------------------------------------------
class iovec_body final : public body {
 public:
    explicit iovec_body(std::vector<iovec_entry> entries) noexcept
        : entries_(std::move(entries)),
          total_size_(compute_total_size(entries_)) {}

    body_kind kind() const noexcept override { return body_kind::iovec; }
    std::size_t size() const noexcept override { return total_size_; }
    MHD_Response* materialize() override;

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
// pipe_body — owns a read-side fd. v2 ownership contract:
//   * constructor takes ownership of fd
//   * if materialize() succeeds, MHD owns the fd (it closes on
//     MHD_destroy_response)
//   * if materialize() is never called, ~pipe_body() must close the fd
//     to avoid a leak (v1 didn't have to handle this because its
//     pipe_response always reached the dispatch path)
// ---------------------------------------------------------------------------
class pipe_body final : public body {
 public:
    explicit pipe_body(int fd) noexcept : fd_(fd) {}
    ~pipe_body() override;

    body_kind kind() const noexcept override { return body_kind::pipe; }
    std::size_t size() const noexcept override { return 0; }  // size unknown
    MHD_Response* materialize() override;

 private:
    int fd_ = -1;
    bool materialized_ = false;
};

// ---------------------------------------------------------------------------
// deferred_body — type-erased producer callback. v1 stored a typed
// callable inside deferred_response<T>; v2 type-erases through
// std::function so the body fits the SBO budget without templating
// http_response.
//
// The trampoline is the C-callable wrapper MHD invokes; it dispatches
// to producer_. Exposed publicly (static method) so unit tests can
// drive it without a daemon.
// ---------------------------------------------------------------------------
class deferred_body final : public body {
 public:
    using producer_type =
        std::function<ssize_t(std::uint64_t, char*, std::size_t)>;

    explicit deferred_body(producer_type producer) noexcept
        : producer_(std::move(producer)) {}

    body_kind kind() const noexcept override { return body_kind::deferred; }
    std::size_t size() const noexcept override { return 0; }  // size unknown
    MHD_Response* materialize() override;

    // Public so unit tests can drive it directly; also passed by name
    // to MHD_create_response_from_callback in materialize().
    static ssize_t trampoline(void* cls, std::uint64_t pos,
                              char* buf, std::size_t max);

 private:
    producer_type producer_;
};

// ---------------------------------------------------------------------------
// SBO budget asserts. Per DR-005 every concrete body must fit in the
// 64-byte buffer http_response carries. If any of these fires on a new
// platform, TASK-010's factory must heap-allocate that subclass instead
// (and TASK-009's destructor must dispatch on body_inline_).
// ---------------------------------------------------------------------------
static_assert(sizeof(empty_body) <= 64,
              "empty_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(string_body) <= 64,
              "string_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(file_body) <= 64,
              "file_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(iovec_body) <= 64,
              "iovec_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(pipe_body) <= 64,
              "pipe_body must fit in http_response SBO (DR-005)");
static_assert(sizeof(deferred_body) <= 64,
              "deferred_body must fit in http_response SBO (DR-005)");
static_assert(alignof(deferred_body) <= 16,
              "deferred_body alignment must be <= 16 (DR-005)");

}  // namespace detail

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAILS_BODY_HPP_
