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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_HTTP_RESOURCE_HPP_
#define SRC_HTTPSERVER_HTTP_RESOURCE_HPP_

#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

// render_* virtuals return http_response by value; the inline
// defaults call render(req) and forward the prvalue, which
// requires http_response to be a complete type at every override site.
// Hard-include is the simplest correct shape (the umbrella already
// reaches both headers).
#include "httpserver/http_method.hpp"
#include "httpserver/http_response.hpp"

// add_hook overloads on http_resource return hook_handle; the
// public header is part of the umbrella surface (no MHD leak).
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"

namespace httpserver { class http_request; }
namespace httpserver { class webserver; }
namespace httpserver::detail { class webserver_impl; }
namespace httpserver::detail { class resource_hook_table; }

namespace httpserver {

// render_* virtuals return
// http_response by value. The webserver dispatch path moves the value
// into mr->response (an std::optional<http_response> living on the
// per-connection modded_request) and keeps it alive until
// MHD fires request_completed. The default render() returns a
// default-constructed http_response whose status_code_ == -1 is the
// v1-compatible sentinel for "handler did not produce a response"; the
// dispatch path routes the sentinel through the internal-error handler.

/**
 * Class representing a callable http resource.
 *
 * Subclass this and override one or more `render_*` methods to handle
 * specific HTTP verbs. Overriding `render()` catches all verbs not
 * covered by a more-specific override; the default implementation
 * returns a sentinel `http_response{}` that the dispatch path
 * translates to a 405 Method Not Allowed.
 *
 * @par Thread-safety
 * A single registered instance may be invoked concurrently from
 * multiple libmicrohttpd worker threads. All state accessed by
 * `render_*` overrides must be externally synchronized.
**/
class http_resource {
 public:
     /**
      * Class destructor
     **/
     virtual ~http_resource() = default;

     /**
      * Method used to answer to a generic request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render(const http_request& req) {
         (void)req;
         // status_code_ == -1 sentinel; see class-level comment above.
         return http_response{};
     }

     /**
      * Method used to answer to a GET request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_get(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a POST request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_post(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a PUT request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_put(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a HEAD request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_head(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a DELETE request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_delete(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a TRACE request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_trace(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a OPTIONS request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_options(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a PATCH request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_patch(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a CONNECT request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_connect(const http_request& req) {
         return render(req);
     }

     /**
      * Toggle whether a specific http_method is allowed on this resource.
      * @param method enum identifying the method (no string lookup)
      * @param allow true to enable the method, false to disable it
     **/
     void set_allowing(http_method method, bool allow) noexcept {
         if (method == http_method::count_) return;  // sentinel; never settable
         if (allow) {
             methods_allowed_.set(method);
         } else {
             methods_allowed_.clear(method);
         }
     }

     /**
      * Allow every defined http_method on this resource.
     **/
     void allow_all() noexcept {
         methods_allowed_.set_all();
     }

     /**
      * Disallow every http_method on this resource.
     **/
     void disallow_all() noexcept {
         methods_allowed_.clear_all();
     }

     /**
      * Test whether `method` is allowed on this resource. Const-noexcept
      * because the answer is a single bitmask test on a trivial member;
      * no string lookup, no allocation.
      * @param method enum identifying the method to query
      * @return true if the method is currently allowed
     **/
     bool is_allowed(http_method method) const noexcept {
         return methods_allowed_.contains(method);
     }

     /**
      * Return the full allow-mask by value. The returned method_set is
      * trivially copyable (sizeof == 4) so by-value is the natural ABI.
     **/
     method_set get_allowed_methods() const noexcept {
         return methods_allowed_;
     }

     /**
      * @brief Return the cached comma-separated Allow header value for
      * the current methods_allowed_ mask.
      *
      * The 405 dispatch path reads this on every
      * method-not-allowed response; without the cache the value would
      * be rebuilt (heap-allocating) on every call via
      * detail::format_allow_header().  This getter caches the result
      * lazily and regenerates only when the mask differs from the
      * snapshot taken at cache-fill time -- so set_allowing,
      * disallow_all, and allow_all all invalidate the cache implicitly,
      * without any explicit dependency on the mutation API.
      *
      * Thread-safe: a per-resource std::shared_mutex allows concurrent
      * warm-path reads (std::shared_lock) while the cache-fill path
      * takes an exclusive std::unique_lock.  Under multi-threaded 405
      * floods the warm path is fully concurrent; only a stale-mask
      * fill serialises.  The methods_allowed_ snapshot is taken inside
      * the lock to eliminate the TOCTOU data race.
      *
      * The returned reference is valid until the next mask mutation;
      * callers must not store it beyond a single dispatch invocation.
      *
      * @return reference to the cached Allow-header string.  Empty
      *         string iff methods_allowed_ is empty (no methods
      *         allowed -- in which case there is no Allow header to
      *         emit at all).
      */
     const std::string& get_allow_header() const;

     /**
      * @brief Register a per-resource hook on one of the five
      * post-route-resolution phases.
      *
      * The five permitted phases
      * are:
      *
      *   - `hook_phase::before_handler`     (short-circuit-capable)
      *   - `hook_phase::handler_exception`  (short-circuit-capable)
      *   - `hook_phase::after_handler`      (short-circuit-capable)
      *   - `hook_phase::response_sent`      (observation-only)
      *   - `hook_phase::request_completed`  (observation-only)
      *
      * The six rejected phases — `connection_opened`,
      * `accept_decision`, `connection_closed`, `request_received`,
      * `body_chunk`, and `route_resolved` — fire BEFORE the resource
      * is known, so per-route registration is structurally impossible.
      * Passing any of them (or `hook_phase::count_`) throws
      * `std::invalid_argument` naming the rejected phase. Passing an
      * empty `std::function` also throws `std::invalid_argument`.
      *
      * Per-route hooks fire AFTER the server-wide chain at the same
      * phase, and only if the server-wide chain did not
      * short-circuit. The returned `hook_handle` owns the
      * registration: destroying it (or calling `remove()`) erases the
      * entry. If the resource is destroyed before the handle, the
      * handle's destructor / `remove()` become no-ops.
      *
      * @param phase one of: before_handler, handler_exception,
      *              after_handler, response_sent, request_completed.
      * @param fn    non-empty callable matching the phase's signature.
      * @return move-only hook_handle owning the registration.
      */
     hook_handle add_hook(hook_phase phase,
         std::function<hook_action(before_handler_ctx&)> fn);
     hook_handle add_hook(hook_phase phase,
         std::function<hook_action(const handler_exception_ctx&)> fn);
     hook_handle add_hook(hook_phase phase,
         std::function<hook_action(after_handler_ctx&)> fn);
     hook_handle add_hook(hook_phase phase,
         std::function<void(const response_sent_ctx&)> fn);
     hook_handle add_hook(hook_phase phase,
         std::function<void(const request_completed_ctx&)> fn);

 protected:
     /**
      * Constructor of the class. The default state allows every defined
      * http_method, matching the v1 behaviour where `resource_init`
      * marked all nine methods true.
     **/
     http_resource() = default;

     /**
      * Copy / move special members. Note that copying an http_resource
      * shallow-copies the hook_table_ shared_ptr, meaning the copy shares
      * the same per-route hook table as the original. Hooks registered on
      * the original will also fire for the copy. If independent hook tables
      * are needed, register hooks on the copy separately after construction.
      *
      * cached_allow_mutex_ is non-copyable / non-movable
      * (std::shared_mutex has no copy or move).  The copy / move special
      * members are therefore written by hand and skip the mutex member
      * entirely -- the copy / move target gets a freshly
      * default-constructed mutex and an invalidated cache
      * (cached_allow_valid_ = false), so the next get_allow_header()
      * call on the target rebuilds the cache under its own (fresh) lock.
      * This is the correct semantic: copy / move is a structural
      * operation; cache state is local to each instance and must not be
      * shared by reference.
     **/
     http_resource(const http_resource& b) noexcept;
     http_resource(http_resource&& b) noexcept;
     http_resource& operator=(const http_resource& b) noexcept;
     http_resource& operator=(http_resource&& b) noexcept;

 private:
     friend class webserver;
     friend class detail::webserver_impl;  // dispatch helpers

     // Default-allow every valid method. method_set::set_all() is
     // constexpr, so the chained call is a constant expression and the
     // default member initialiser stays well-formed.
     method_set methods_allowed_ = method_set{}.set_all();

     // Per-resource hook bus storage (PIMPL). Lazily allocated
     // on first add_hook() call; resources that never register a hook
     // pay zero allocation cost and only sizeof(shared_ptr) of nullptr
     // storage. shared_ptr lets the hook_handle hold a weak_ptr that
     // expires cleanly when the resource is destroyed (handle.remove()
     // then becomes a no-op without dereferencing freed memory).
     //
     // `mutable` because hook firing on `const http_resource&` (from the
     // const path through dispatch) needs to read this pointer; the
     // logical const-ness of the resource is preserved (the firing path
     // only reads the table; only the public add_hook(non-const) writes).
     mutable std::shared_ptr<detail::resource_hook_table> hook_table_;

     // Lazy cache of the formatted Allow header value
     // for the 405 dispatch path.  Built on first call to
     // get_allow_header(); reused on subsequent calls as long as the
     // resource's methods_allowed_ mask is unchanged.  Mask changes
     // (set_allowing / disallow_all / allow_all) are detected implicitly
     // by comparing the live mask against the snapshot taken when the
     // cache was last filled -- the cache regenerates on next read
     // without any explicit invalidation hook.  This sidesteps the
     // maintenance trap of chasing every mutation site.
     //
     // The cost of the comparison (a single 32-bit equality test on
     // method_set::bits) is dwarfed by the avoided heap allocation
     // inside format_allow_header(), which reserves a 64-byte string and
     // appends method tokens on every 405 dispatch pre-cache.
     //
     // Thread-safety: the dispatch path can call get_allow_header()
     // concurrently from multiple MHD worker threads against the same
     // resource.  std::shared_mutex allows concurrent warm-path reads
     // (std::shared_lock) while the cache-fill (miss/stale) path takes
     // an exclusive std::unique_lock.  The lock protects ONLY the three
     // cache fields below -- the mask mutators (set_allowing /
     // allow_all / disallow_all) never take it, so it does NOT
     // synchronise methods_allowed_ writes.  Snapshotting the mask
     // inside the lock keeps each cache fill internally consistent (the
     // cached string always matches the mask it was built from); mask
     // mutation concurrent with serving falls under the library-wide
     // rule that resources are configured before the server starts.
     //
     // `mutable` for the same reason as hook_table_: the dispatch path
     // calls get_allow_header() on a `const http_resource&` (the cache
     // is logically const-observable -- the visible result is a stable
     // function of methods_allowed_).
     mutable std::shared_mutex cached_allow_mutex_;
     mutable std::string cached_allow_header_;
     mutable method_set cached_allow_mask_{};
     mutable bool cached_allow_valid_ = false;

// Internal-only accessor: only visible to translation units that define
// HTTPSERVER_COMPILATION (i.e., the library's own .cpp files). User-facing
// translation units that include <httpserver.hpp> see only the public
// add_hook() surface; this symbol is intentionally hidden from them.
#if defined(HTTPSERVER_COMPILATION)

 public:
     // Internal accessor for the dispatch path (webserver_impl). The
     // pointer is null when no hook has ever been registered on this
     // resource (the dispatch hot path treats null as "no per-route
     // hooks for any phase"). Public-but-HTTPSERVER_COMPILATION-gated
     // for the same reason webserver::make_hook_handle_ is: the symbol
     // is reachable only from within the library translation units.
     //
     // CONTRACT — synchronisation requirement:
     //   This is a **non-atomic** read of the shared_ptr's stored pointer
     //   via std::shared_ptr::get().  It is safe only when the caller
     //   holds a happens-before edge over any concurrent writer of
     //   hook_table_: e.g., after all mutating threads have been joined
     //   or after an acquire fence/lock that sequenced-after the last
     //   atomic_store in ensure_table().  Do NOT call this from a thread
     //   that races with ensure_table() without external synchronisation.
     //   (ensure_table() itself uses the non-member std::atomic_*_explicit
     //   overloads (deprecated as of C++26); once the field migrates to
     //   std::atomic<std::shared_ptr<T>>, an acquire load should
     //   replace the .get() call here too.)
     detail::resource_hook_table* hook_table_raw_() const noexcept {
         return hook_table_.get();
     }

 private:
#endif
};

// Ensure http_resource stays small enough for stack allocation in hot paths.
// Originally vptr + uint32_t method_set + padding; the per-resource hook
// table PIMPL (shared_ptr<resource_hook_table>) was added later, growing the
// cap to vptr + shared_ptr + method_set + padding.
//
// The lazy Allow-header cache adds: std::shared_mutex +
// std::string + method_set + bool.  std::shared_mutex is a sizeof~56
// storage on pthread platforms (libc++/macOS) and ~56 on libstdc++/Linux
// (similar to std::mutex); std::string SBO adds ~24-32; the method_set /
// bool fields slot into existing padding.  The new ceiling is the old
// (3*void* + 2*method_set) plus the new cache payload, padded for
// alignment.  See test/bench_sizeof_http_resource.cpp for the v1-anchored
// algebra; the value here documents the current layout shape.
static_assert(sizeof(http_resource) <=
                  sizeof(void*) * 3 + sizeof(method_set) * 2
                  + sizeof(std::shared_mutex) + sizeof(std::string)
                  + sizeof(method_set) + sizeof(bool) * 8,
              "http_resource should be approximately vptr + shared_ptr + "
              "method_set (TASK-051) + Allow-header cache "
              "(shared_mutex + string + method_set + bool) after TASK-058 step 3");

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_RESOURCE_HPP_
