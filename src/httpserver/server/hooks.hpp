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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-118: the v3 request lifecycle hook vocabulary and bus (plan D4,
// DR-V3-001 semantic-exchange-only contexts). NOT part of the v2
// umbrella <httpserver.hpp>: like the rest of the v3 server area this
// is an additive surface.
//
// Seven request-scoped phases, in firing order, keeping the v2 names
// where the native engine has the point:
//
//   request_received   head parsed and route_path derived, before any
//                      lookup; short-circuit capable (the response is
//                      committed and the handler never runs, the body
//                      never admitted).
//   route_resolved     after the resolve, on a hit AND a miss alike;
//                      observation only.
//   before_handler     after a resolving hit, before the handler
//                      invoke; also the consultation point when the
//                      method mismatches (the hook may supply the 405;
//                      the engine still appends Allow). Short-circuit
//                      capable.
//   handler_exception  a throw escaped the handler task; the chain may
//                      supply the response, else the engine
//                      synthesizes the 500.
//   after_handler      at response-head commit; mutates status and
//                      fields in place. Full replacement is NOT
//                      offered (documented v2 delta: v2 could swap the
//                      whole response object).
//   response_sent      after the engine accepted the committed head;
//                      observation only.
//   request_completed  exactly once when the exchange settles, on
//                      success, short-circuit, synthesis, exception,
//                      and disconnect alike (succeeded=false plus the
//                      typed end reason).
//
// Not ported from v2 (migration notes in
// specs/architecture/v3/v2-parity-inventory.md): connection_opened and
// connection_closed (accept-time peer policy is a later task),
// accept_decision, and body_chunk (the v3 body model is pull-based:
// pre-body control is request_received, per-chunk visibility is the
// handler's read loop). Per-resource add_hook is not ported either --
// per-route composition is a wrapping route_handler.
//
// Bus contracts carried over from v2: registration order within a
// phase; snapshot-copy firing (a hook may add or remove hooks
// mid-fire without disturbing the running pass); the first
// respond_with short-circuits the remaining hooks of the phase; a
// zero-cost-when-unused gate per phase; add/remove are runtime-safe
// before and after listen(). A hook that throws propagates out of
// fire() to the engine's dispatcher, which contains the throw,
// treats it as pass(), and routes it through the handler_exception
// chain.

#ifndef SRC_HTTPSERVER_SERVER_HOOKS_HPP_
#define SRC_HTTPSERVER_SERVER_HOOKS_HPP_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>

namespace httpserver {

namespace server {


// The seven request-scoped phases plus the count_ sentinel, which must
// remain last and is not a valid phase value.
enum class hook_phase : std::uint8_t {
    request_received,
    route_resolved,
    before_handler,
    handler_exception,
    after_handler,
    response_sent,
    request_completed,
    count_,  // sentinel; must remain last
};

static_assert(static_cast<std::size_t>(hook_phase::count_) == std::size_t{7},
              "seven request-scoped phases");

// The response a short-circuiting hook supplies: terminal status,
// response fields, and the complete body bytes. An invalid status
// makes the engine use its own synthesis for that point instead.
struct hook_response {
    http::status status;  // invalid -> the engine's default synthesis
    http::fields fields;
    std::vector<std::byte> body;
};

// The outcome value every hook returns. pass() continues the chain (or
// the engine's own logic once the chain is exhausted); respond_with
// short-circuits the remaining hooks of the phase and, on a
// short-circuit-capable phase, replaces the engine's next step with
// the carried response. On observation-only phases (route_resolved,
// response_sent, request_completed) and on after_handler (mutation
// only, replacement not offered) the action of a pass hook is the
// documented contract; a respond_with there is ignored.
class hook_action {
 public:
    hook_action() noexcept = default;
    hook_action(const hook_action&) = delete;
    hook_action& operator=(const hook_action&) = delete;
    hook_action(hook_action&&) noexcept = default;
    hook_action& operator=(hook_action&&) noexcept = default;
    ~hook_action() = default;

    [[nodiscard]] static hook_action pass() noexcept {
        return hook_action{};
    }

    [[nodiscard]] static hook_action respond_with(hook_response r) noexcept {
        hook_action a;
        a.response_.emplace(std::move(r));
        return a;
    }

    [[nodiscard]] bool is_pass() const noexcept {
        return !response_.has_value();
    }

    // Consumes the wrapped response. Precondition: !is_pass().
    [[nodiscard]] hook_response&& take_response() && noexcept {
        assert(response_.has_value()
               && "take_response() called on a pass-action");
        return std::move(*response_);
    }

 private:
    std::optional<hook_response> response_{};
};

// The resolved route a hook observes: the registered pattern text, the
// methods of the resolved tier (the Allow inputs), and the family.
struct route_descriptor {
    std::string_view pattern;  // views the registry's stored pattern
    http::method_set methods;
    bool is_prefix = false;
};

// Per-phase contexts. Every context references the semantic request
// head; no engine or backend types appear (DR-V3-001).
struct request_received_ctx {
    const http::request_head& request;
};

struct route_resolved_ctx {
    const http::request_head& request;
    bool matched = false;            // fires on a hit AND a miss alike
    route_descriptor route;          // empty pattern on a miss
};

struct before_handler_ctx {
    const http::request_head& request;
    route_descriptor route;          // the method-mismatch tier on a 405
};

struct handler_exception_ctx {
    const http::request_head& request;
    std::exception_ptr error;        // the escaped exception
};

// after_handler mutates the about-to-be-committed response in place:
// status and fields (append or replace field values). Full response
// replacement is not offered (documented v2 delta).
struct after_handler_ctx {
    const http::request_head& request;
    http::status status;
    http::fields fields;
};

struct response_sent_ctx {
    const http::request_head& request;
    std::uint16_t status = 0;        // the committed head's status
};

struct request_completed_ctx {
    const http::request_head& request;
    bool succeeded = true;
    http::outcome end;               // ok on success; typed end reason
};

namespace detail {

class hook_bus_impl;

// TU-defined bridges over the incomplete pimpl: the header stays
// vocabulary-only, the storage lives in the library.
std::uint64_t hook_bus_add(
        const std::shared_ptr<hook_bus_impl>& owner, std::uint8_t phase,
        concurrency::unique_function<hook_action(void*)> call);
void hook_bus_remove(const std::shared_ptr<hook_bus_impl>& owner,
                     std::uint8_t phase, std::uint64_t key) noexcept;
bool hook_bus_any(const std::shared_ptr<hook_bus_impl>& owner,
                  std::uint8_t phase) noexcept;
hook_action hook_bus_fire(const std::shared_ptr<hook_bus_impl>& owner,
                          std::uint8_t phase, void* ctx);

// The phase -> context mapping (compile-time, so add() and fire()
// cannot disagree about a phase's context type).
template <hook_phase P> struct phase_ctx;
template <> struct phase_ctx<hook_phase::request_received> {
    using type = request_received_ctx;
};
template <> struct phase_ctx<hook_phase::route_resolved> {
    using type = route_resolved_ctx;
};
template <> struct phase_ctx<hook_phase::before_handler> {
    using type = before_handler_ctx;
};
template <> struct phase_ctx<hook_phase::handler_exception> {
    using type = handler_exception_ctx;
};
template <> struct phase_ctx<hook_phase::after_handler> {
    using type = after_handler_ctx;
};
template <> struct phase_ctx<hook_phase::response_sent> {
    using type = response_sent_ctx;
};
template <> struct phase_ctx<hook_phase::request_completed> {
    using type = request_completed_ctx;
};
template <hook_phase P>
using phase_ctx_t = typename phase_ctx<P>::type;

// The context type of a hook callable: its sole `Ctx&` parameter.
// Lambdas (const and mutable, noexcept either way) and function
// pointers match.
template <typename F, typename = void> struct hook_arg;
template <typename F>
struct hook_arg<F, std::void_t<decltype(&F::operator())>> : hook_arg<decltype(&F::operator())> {};
template <typename R, typename C, typename A>
struct hook_arg<R (C::*)(A&), void> { using type = A; };
template <typename R, typename C, typename A>
struct hook_arg<R (C::*)(A&) const, void> { using type = A; };
template <typename R, typename C, typename A>
struct hook_arg<R (C::*)(A&) noexcept, void> { using type = A; };
template <typename R, typename C, typename A>
struct hook_arg<R (C::*)(A&) const noexcept, void> { using type = A; };
template <typename R, typename A>
struct hook_arg<R (*)(A&), void> { using type = A; };
template <typename R, typename A>
struct hook_arg<R (*)(A&) noexcept, void> { using type = A; };

}  // namespace detail

// RAII registration ownership: the destructor erases the registration
// unless detach() disarmed it. Safe to outlive the bus (a silent
// no-op). Move-only; a moved-from handle is disarmed.
class hook_handle {
 public:
    hook_handle() noexcept = default;

    hook_handle(const hook_handle&) = delete;
    hook_handle& operator=(const hook_handle&) = delete;
    hook_handle(hook_handle&&) noexcept = default;
    hook_handle& operator=(hook_handle&&) noexcept = default;
    ~hook_handle();

    // Erases the registration. Idempotent; a no-op when disarmed or
    // when the bus is gone.
    void remove() noexcept;

    // Disarms the destructor, making the registration permanent.
    void detach() noexcept;

    // False after remove(), detach(), or a move-out.
    bool armed() const noexcept { return key_ != 0; }

 private:
    friend class hook_bus;

    hook_handle(std::uint64_t key, std::uint8_t phase,
                std::weak_ptr<detail::hook_bus_impl> owner) noexcept;

    std::uint64_t key_ = 0;  // 0 = disarmed
    std::uint8_t phase_ = 0;
    std::weak_ptr<detail::hook_bus_impl> owner_;
};

// The server-wide hook bus. add() registers one hook for one phase and
// hands back the owning handle; fire() is the engine-facing seam (the
// dispatcher invokes it; consumer code never does). Thread-safe:
// add/remove/any_hooks/fire may race freely. Zero-cost-when-unused:
// fire() on a phase with no hooks is one atomic load.
class hook_bus {
 public:
    hook_bus();
    ~hook_bus();

    hook_bus(const hook_bus&) = delete;
    hook_bus& operator=(const hook_bus&) = delete;
    hook_bus(hook_bus&&) noexcept;
    hook_bus& operator=(hook_bus&&) noexcept;

    // Registers @p hook for phase P. The callable's sole `Ctx&`
    // parameter must be the phase's context type (statically
    // enforced). Registration order within the phase is the firing
    // order.
    template <hook_phase P, typename F>
    hook_handle add(F&& hook) {
        using ctx = typename detail::hook_arg<std::decay_t<F>>::type;
        static_assert(
            std::is_same_v<ctx, detail::phase_ctx_t<P>>,
            "hook context type does not match the registered phase");
        const std::uint64_t key = detail::hook_bus_add(
            impl_, static_cast<std::uint8_t>(P),
            concurrency::unique_function<hook_action(void*)>(
                [call = std::forward<F>(hook)](void* erased) -> hook_action {
                    return call(*static_cast<ctx*>(erased));
                }));
        return hook_handle(key, static_cast<std::uint8_t>(P), impl_);
    }

    // True once the phase holds at least one live registration.
    bool any_hooks(hook_phase p) const noexcept {
        return detail::hook_bus_any(impl_, static_cast<std::uint8_t>(p));
    }

    // The engine-facing firing seam: invokes the phase's hooks in
    // registration order over a snapshot; the first respond_with wins
    // and later hooks of the phase do not run. A hook that throws
    // propagates (the dispatcher contains it).
    template <hook_phase P>
    hook_action fire(typename detail::phase_ctx_t<P>& ctx) const {
        return detail::hook_bus_fire(impl_, static_cast<std::uint8_t>(P),
                                     &ctx);
    }

 private:
    std::shared_ptr<detail::hook_bus_impl> impl_;
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_HOOKS_HPP_
