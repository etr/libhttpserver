/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_HTTP3_ROUTE_PARITY_FIXTURE_HPP_
#define TEST_UNIT_HTTP3_ROUTE_PARITY_FIXTURE_HPP_
#include <array>
#include <optional>
#include <vector>
#include <utility>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/worker_pool.hpp>
#include <parity/response_frame.hpp>
#include "./io_loopback.hpp"
namespace h3parity {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
template <typename Predicate>
bool until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}
// The same native HTTP/1 owner/poll/worker composition used by connection_engine.
// No semantic sink or body mock stands in for a protocol in parity scenarios.
class http1_fixture {
 public:
    http1_fixture(hs::resource_budget budget, const hs::route_registry& routes) : pool_(2) {
        pair_ = io_loopback::pair::make();
        if (!pair_.ok()) throw std::runtime_error("HTTP/1 loopback failed");
        hd::pollsys::set_nonblocking(pair_.peer(), true);
        backend_.adopt_connection(77, pair_.detach_local());
        auto config = hd::connection_engine_config::from_budget_limits(hs::budget_limits{});
        config.outbox.max_queue_bytes = 256;
        engine_ = std::make_shared<hd::connection_engine>(backend_, pool_, routes, hooks_, budget, scope_, config, 77, [this] { stopped_ = true; });
        engine_->start();
    }
    ~http1_fixture() {
        cancel();
        until([this] { return stopped_.load(); });
    }
    void send(std::string_view wire) { io_loopback::write_all(pair_.peer(), wire.data(), wire.size()); }
    void cancel() { engine_->shutdown(); }
    parity::observed_response response() {
        parity::response_frame_parser parser;
        std::optional<parity::observed_response> result;
        if (!until([&] {
                std::array<std::byte, 1024> bytes{};
                auto read = hd::pollsys::read_some(pair_.peer(), bytes.data(), bytes.size());
                auto responses = read.status == hd::pollsys::sys_status::ok
                                     ? parser.feed({reinterpret_cast<const char*>(bytes.data()), read.transferred})
                                     : (read.status == hd::pollsys::sys_status::closed_reset ? parser.finish() : std::vector<parity::observed_response>{});
                if (!responses.empty()) result = std::move(responses.front());
                return result.has_value();
            }))
            throw std::runtime_error("HTTP/1 response deadline");
        return std::move(*result);
    }

 private:
    hs::hook_bus hooks_;
    hd::drain_scope scope_;
    io_loopback::pair pair_;
    hd::worker_pool pool_;
    hd::io_poll_backend backend_;
    std::atomic<bool> stopped_{false};
    std::shared_ptr<hd::connection_engine> engine_;
};
}  // namespace h3parity
#endif  // TEST_UNIT_HTTP3_ROUTE_PARITY_FIXTURE_HPP_
