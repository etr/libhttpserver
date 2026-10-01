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

// TASK-108: the native_server pimpl. Owns, in destruction order: the
// stop flags, the poll-driven transport engine (its close completes
// every pending operation), the worker pool (drains those
// completions), the listener engines (their connection records are all
// erased by then), the route registry, and the budget scope it
// reserves against. See server/server.hpp for the lifecycle contract.

#include <httpserver/server/server.hpp>

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/listener_engine.hpp>
#include <httpserver/detail/worker_pool.hpp>

namespace httpserver {

namespace server {

namespace engine = httpserver::detail;

class native_server::impl {
 public:
    explicit impl(server_options options)
        : options_(std::move(options)),
          budget_(resource_budget::root(options_.budgets())),
          pool_(options_.concurrency().workers) {
        registry_state_ = route_registry::create(budget_, registry_);
    }

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    http::outcome route(const http::method& method, std::string_view pattern,
                        route_handler handler) {
        if (running_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: route() after listen() is not allowed");
        }
        return registry_.route(method, pattern, std::move(handler));
    }

    http::outcome listen() {
        if (stop_requested_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: this server was stopped and cannot listen");
        }
        if (running_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: listen() ran twice");
        }
        // REQ-016: the validate() gate runs before any bind.
        const http::outcome valid = options_.validate();
        if (!valid.ok()) return valid;
        if (!registry_state_.ok()) return registry_state_;

        engine::connection_engine_config config =
            engine::connection_engine_config::from_budget_limits(options_.budgets());
        config.timeouts = options_.timeouts();

        for (std::size_t i = 0; i < options_.listener_count(); ++i) {
            listeners_.push_back(std::make_shared<engine::listener_engine>(
                backend_, pool_, registry_, budget_, config));
            const http::outcome bound =
                listeners_.back()->listen(options_.listener(i), i);
            if (!bound.ok()) {
                // Terminal for this object: stop what bound and fail
                // typed. The listeners stay owned until the destructor
                // drains, so no engine outlives its listener.
                request_stop();
                return bound;
            }
        }
        running_.store(true, std::memory_order_release);
        return http::outcome::okay();
    }

    void request_stop() noexcept {
        stop_requested_.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        for (const std::shared_ptr<engine::listener_engine>& listener :
             listeners_) {
            listener->request_stop();
        }
        static_cast<void>(backend_.wake());
    }

    void stop() {
        request_stop();
        // Terminal-fails every pending operation; the completions queue
        // onto the pool, and the drain joins the engines' unwinding.
        static_cast<void>(backend_.close());
        pool_.drain_and_join();
    }

    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    std::uint16_t get_bound_port(std::size_t listener_index) const noexcept {
        if (listener_index >= listeners_.size()) return 0;
        return listeners_[listener_index]->bound_port();
    }

 private:
    // Destruction order (reverse declaration): the flags, the transport
    // engine (close), the pool (drain), the listeners, the registry,
    // the budget, the options. The engine tasks that the close enqueues
    // run during the pool's implicit drain; every listener record is
    // erased before the listeners die.
    server_options options_;
    resource_budget budget_;
    route_registry registry_;
    http::outcome registry_state_;
    std::vector<std::shared_ptr<engine::listener_engine>> listeners_;
    engine::worker_pool pool_;
    engine::io_poll_backend backend_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
};

native_server::native_server(server_options options)
    : impl_(std::make_unique<impl>(std::move(options))) {
}

native_server::~native_server() {
    impl_->stop();
}

http::outcome native_server::route(const http::method& method,
                                   std::string_view pattern,
                                   route_handler handler) {
    return impl_->route(method, pattern, std::move(handler));
}

http::outcome native_server::listen() { return impl_->listen(); }

void native_server::request_stop() noexcept { impl_->request_stop(); }

void native_server::stop() { impl_->stop(); }

bool native_server::is_running() const noexcept {
    return impl_->is_running();
}

std::uint16_t native_server::get_bound_port(
    std::size_t listener_index) const noexcept {
    return impl_->get_bound_port(listener_index);
}

}  // namespace server

}  // namespace httpserver
