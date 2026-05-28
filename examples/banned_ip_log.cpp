// Demonstrates the solution to issue #332: log every banned-IP rejection.
//
// Configure the daemon with an ACCEPT default policy, block the IPs you
// want denied via ws.block_ip(), and register a connection-level
// `accept_decision` hook that logs every rejection to stderr.
//
// The hook is observation-only: throwing from it does not flip the
// decision (the daemon still rejects the connection per the policy
// callback's return), so it is safe to do I/O here.
//
// Run, then attempt to connect from one of the blocked IPs to see
// stderr show a "[BANNED] ..." line per attempt.

#include <httpserver.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace hs = httpserver;

class hello_resource : public hs::http_resource {
 public:
    hs::http_response render_get(const hs::http_request&) override {
        return hs::http_response::string("Hello, World!");
    }
};

int main() {
    hs::webserver ws{hs::create_webserver(8080)
                         .default_policy(hs::http::http_utils::ACCEPT)};

    // Block whatever client IP you want denied. The shipping example
    // uses an RFC-1918 placeholder; swap with your client's address.
    ws.block_ip("10.0.0.1");

    auto h = ws.add_hook(hs::hook_phase::accept_decision,
        std::function<void(const hs::accept_ctx&)>(
            [](const hs::accept_ctx& ctx) {
                if (!ctx.accepted) {
                    std::cerr << "[BANNED] peer=" << ctx.peer.to_string()
                              << " reason="
                              << (ctx.reason.has_value()
                                      ? std::string(*ctx.reason)
                                      : std::string("unspecified"))
                              << std::endl;
                }
            }));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/hello", resource);

    ws.start(true);   // blocking
    return 0;
}
