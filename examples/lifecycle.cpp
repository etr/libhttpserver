/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <syncstream>
#include <vector>

#include "./example_host.hpp"
#include "./example_reply.hpp"
namespace srv = httpserver::server;
namespace http = httpserver::http;
void observe(const char* text) {
    std::osyncstream(std::cout) << text << std::endl;
}
struct pending_work {
    std::mutex mutex;
    std::map<unsigned, httpserver::resume_signal> signals;
    unsigned sequence = 0;
    struct retirement {
        pending_work& owner;
        unsigned id;
        ~retirement() {
            {
                std::lock_guard lock(owner.mutex);
                owner.signals.erase(id);
            }
            observe("RETIRED");
        }
    };
};
int main(int argc, char**) {
    if (argc != 1) {
        std::cerr << "usage: lifecycle; stdin: resume, drain, wait, stop\n";
        return 2;
    }
    pending_work pending;
    srv::server_options options;
    options.concurrency().workers = 2;
    options.timeouts().suspension = std::chrono::seconds(5);
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    const auto get = http::method::known(http::method_id::get);
    for (bool cancel : {false, true}) {
        if (!server
                 .route(get, cancel ? "/cancel" : "/wait",
                        [&, cancel](httpserver::exchange& x) -> httpserver::task<void> {
                            httpserver::resume_signal signal;
                            if (!x.suspend(signal).ok()) {
                                co_return;
                            }
                            unsigned id;
                            {
                                std::lock_guard lock(pending.mutex);
                                id = ++pending.sequence;
                                pending.signals.emplace(id, signal);
                            }
                            pending_work::retirement cleanup{pending, id};
                            observe("PENDING");
                            if (cancel) {
                                auto token = x.cancellation();
                                try {
                                    co_await token.cancelled();
                                } catch (const httpserver::cancelled_exception&) {
                                    observe("CANCELLED");
                                    throw;
                                }
                            } else if (co_await signal.wait() == httpserver::resume_outcome::resumed) {
                                co_await v3_example::reply(x, "resumed\n");
                            }
                        })
                 .ok()) {
            return 1;
        }
    }
    if (!server
             .route(get, "/timeout",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        httpserver::resume_signal signal;
                        if (!x.suspend(signal).ok()) {
                            co_return;
                        }
                        if (co_await signal.wait_for(std::chrono::milliseconds(50)) ==
                            httpserver::resume_outcome::timeout) {
                            co_await v3_example::reply(x, "timeout\n", 408);
                        }
                    })
             .ok() ||
        !server
             .route(http::method::known(http::method_id::post), "/stop",
                    [&](httpserver::exchange&) -> httpserver::task<void> {
                        server.request_stop();
                        observe("STOP_REQUESTED");
                        co_return;
                    })
             .ok() ||
        !server.listen().ok()) {
        return 1;
    }
    std::cout << "PORT=" << server.get_bound_port(0) << std::endl;
    srv::drain_ticket ticket;
    for (std::string command; std::getline(std::cin, command) && command != "stop";) {
        if (command == "resume") {
            // Copy handles under the lock, then signal outside it; no exchange escapes.
            std::vector<httpserver::resume_signal> signals;
            {
                std::lock_guard lock(pending.mutex);
                for (auto& [id, signal] : pending.signals) {
                    signals.push_back(signal);
                }
            }
            observe("SIGNALED");
            for (auto& signal : signals) {
                signal.signal();
            }
        } else if (command == "drain") {
            observe(server.begin_drain(std::chrono::milliseconds(500), ticket).ok() ? "DRAINING" : "DRAIN_FAILED");
        } else if (command == "wait") {
            srv::drain_result result;
            if (!ticket.wait(result).ok()) {
                observe("WAIT_FAILED");
            } else {
                observe(result.status == srv::drain_status::completed ? "completed" : "deadline_expired");
            }
        } else {
            observe("UNKNOWN_COMMAND");
        }
    }
    server.stop();  // Retires pending handlers before destroying their registry.
}
