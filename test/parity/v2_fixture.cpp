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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// The only parity-harness TU aware of the v2/MHD engine. See the class
// comment in v2_fixture.hpp for the registry contract. All fixture
// inputs (credentials, seeds, paths) are fixed constants so committed
// transcript expectations are deterministic; volatile material (Digest
// nonces) is masked in the corpus, never randomized here.

#include "parity/v2_fixture.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "./httpserver.hpp"

#ifdef PARITY_TEST_DATA_DIR
#define PARITY_DATA_ROOT PARITY_TEST_DATA_DIR
#else
#define PARITY_DATA_ROOT "."
#endif

using httpserver::create_webserver;
using httpserver::digest_challenge;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::method_set;
using httpserver::webserver;

namespace parity {
namespace {

constexpr const char* SMOKE_BODY = "smoke-ok";
constexpr const char* BASIC_USER = "alice";
constexpr const char* BASIC_PASS = "wonderland";
constexpr const char* BASIC_REALM = "transcript";
// The digest username ("bob") lives in the transcript's curl_user
// option; only the server-side realm/password/seed are server inputs.
constexpr const char* DIGEST_REALM = "transcript";
constexpr const char* DIGEST_PASS = "builder";
constexpr const char* DIGEST_SEED = "parity-seed-096";

// Uniform anti-rot probe route present on every profile.
void register_smoke(webserver& ws) {
    ws.on_get("/__smoke", [](const http_request&) {
        return http_response::string(SMOKE_BODY);
    });
}

// Routing: exact, parameterized, method-set and 405/404 defaults.
std::unique_ptr<webserver> build_routing_basic() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0));
    ws->on_get("/hello", [](const http_request&) {
        return http_response::string("OK");
    });
    ws->on_get("/params/{id}/name/{name}", [](const http_request& req) {
        std::string id = std::string(req.get_arg("id"));
        std::string name = std::string(req.get_arg("name"));
        return http_response::string("id=" + id + ";name=" + name);
    });
    method_set get_and_head;
    get_and_head.set(httpserver::http_method::get);
    get_and_head.set(httpserver::http_method::head);
    ws->route(get_and_head, "/both", [](const http_request&) {
        return http_response::string("both-ok");
    });
    ws->on_get("/get_only", [](const http_request&) {
        return http_response::string("get-only");
    });
    register_smoke(*ws);
    return ws;
}

// Hooks: wire-observable effects only — a before_handler short-circuit
// on DELETE, an after_handler header mutation, custom 404 and 405
// pages. No hook-internal state is ever asserted.
std::unique_ptr<webserver> build_routing_hooks() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0)
        // Documented convention: the custom handlers own their status.
        .not_found_handler([](const http_request&) {
            return http_response::string("custom-not-found").with_status(404);
        })
        .method_not_allowed_handler([](const http_request&) {
            return http_response::string("custom-not-allowed").with_status(405);
        }));
    // hook_handle erases its registration on destruction; the profile
    // keeps the hooks for the server's lifetime, so detach().
    ws->add_hook(httpserver::hook_phase::before_handler,
        std::function<httpserver::hook_action(httpserver::before_handler_ctx&)>(
            [](httpserver::before_handler_ctx& ctx) {
                if (ctx.request != nullptr &&
                    ctx.request->get_path() == "/admin") {
                    return httpserver::hook_action::respond_with(
                        http_response::string("hooked403").with_status(403));
                }
                return httpserver::hook_action::pass();
            })).detach();
    ws->add_hook(httpserver::hook_phase::after_handler,
        std::function<httpserver::hook_action(httpserver::after_handler_ctx&)>(
            [](httpserver::after_handler_ctx& ctx) {
                if (ctx.response != nullptr) {
                    ctx.response->with_header("X-Hook", "after");
                }
                return httpserver::hook_action::pass();
            })).detach();
    ws->on_get("/hello", [](const http_request&) {
        return http_response::string("OK");
    });
    ws->on_get("/get_only", [](const http_request&) {
        return http_response::string("get-only");
    });
    ws->on_delete("/admin", [](const http_request&) {
        return http_response::string("admin-ok");
    });
    register_smoke(*ws);
    return ws;
}

// Basic auth: central auth_handler validates fixed credentials and
// answers 401 with the documented Basic challenge shape.
std::unique_ptr<webserver> build_auth_basic() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0)
        .basic_auth(true)
        .auth_handler([](const http_request& req)
                          -> std::optional<http_response> {
            if (req.get_path() == "/__smoke") return std::nullopt;
            if (req.get_user() == BASIC_USER && req.get_pass() == BASIC_PASS) {
                return std::nullopt;
            }
            return http_response::unauthorized("Basic", BASIC_REALM);
        }));
    ws->on_get("/secret", [](const http_request&) {
        return http_response::string("secret-ok");
    });
    register_smoke(*ws);
    return ws;
}

// Digest auth: resource-level RFC 7616 challenge/round-trip against
// fixed credentials. Nonces come from MHD's nonce store and are masked
// in the corpus, not pinned.
class digest_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) override {
        if (req.get_digested_user() == "") {
            digest_challenge challenge;
            challenge.realm = DIGEST_REALM;
            challenge.response_body = "digest required";
            return http_response::unauthorized(challenge);
        }
        auto result = req.check_digest_auth(
            DIGEST_REALM, DIGEST_PASS, 300, 0,
            httpserver::http::http_utils::digest_algorithm::MD5);
        if (result == httpserver::http::http_utils::digest_auth_result::OK) {
            return http_response::string("digest-ok");
        }
        return http_response::string("digest denied").with_status(401);
    }
};

std::unique_ptr<webserver> build_auth_digest() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0)
        .digest_auth(true)
        .digest_auth_random(DIGEST_SEED)
        .nonce_nc_size(4));
    ws->register_path("/digest", std::make_shared<digest_resource>());
    register_smoke(*ws);
    return ws;
}

// Forms: urlencoded and multipart POST fields echoed from get_arg.
std::unique_ptr<webserver> build_forms() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0));
    ws->on_post("/echo_form", [](const http_request& req) {
        std::string a = std::string(req.get_arg("a"));
        std::string b = std::string(req.get_arg("b"));
        return http_response::string("a=" + a + ";b=" + b);
    });
    ws->on_post("/upload", [](const http_request& req) {
        std::string note = std::string(req.get_arg("note"));
        return http_response::string("note=" + note);
    });
    register_smoke(*ws);
    return ws;
}

// File responses: an existing file and a missing file (documented to
// surface as a dispatch-time 500, not a construction error).
std::unique_ptr<webserver> build_file_resp() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0));
    ws->on_get("/file", [](const http_request&) {
        return http_response::file(PARITY_DATA_ROOT "/test_content");
    });
    ws->on_get("/empty_file", [](const http_request&) {
        return http_response::file(PARITY_DATA_ROOT "/test_content_empty");
    });
    ws->on_get("/missing", [](const http_request&) {
        return http_response::file(PARITY_DATA_ROOT "/no_such_file_096");
    });
    ws->on_get("/pipe", [](const http_request&) {
        int endpoints[2];
        if (::pipe(endpoints) != 0) throw std::runtime_error("parity pipe");
        const auto written = ::write(endpoints[1], "abcXYZ", 6);
        ::close(endpoints[1]);
        if (written != 6) {
            ::close(endpoints[0]);
            throw std::runtime_error("write pipe");
        }
        return http_response::pipe(endpoints[0]);
    });
    ws->on_get("/iovec", [](const http_request&) {
        const httpserver::iovec_entry parts[] = {{"abc", 3}, {"XYZ", 3}};
        return http_response::iovec(parts);
    });
    ws->on_get("/deferred", [](const http_request&) {
        return http_response::deferred([](std::uint64_t pos, char* dest,
                                         std::size_t cap) -> ssize_t {
            if (pos >= 6) return -1;
            const auto count = std::min(cap, std::size_t(6 - pos));
            std::memcpy(dest, "abcXYZ" + pos, count);
            return static_cast<ssize_t>(count);
        });
    });
    register_smoke(*ws);
    return ws;
}

// IP controls: default-REJECT policy with the loopback explicitly
// allow-listed. (Deny-then-refuse wire shape is policy-callback
// timing dependent; see the parity inventory for what is pinned.)
std::unique_ptr<webserver> build_ip_controls() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0)
        .default_policy(httpserver::http::http_utils::REJECT));
    ws->allow_ip("127.0.0.1");
    ws->on_get("/hello", [](const http_request&) {
        return http_response::string("OK");
    });
    register_smoke(*ws);
    return ws;
}

// SHOUTcast: ICY status line on the wire, unreachable through curl.
std::unique_ptr<webserver> build_shoutcast() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0));
    ws->on_get("/stream", [](const http_request&) {
        http_response r = http_response::string("OK");
        r.shoutCAST();
        return r;
    });
    register_smoke(*ws);
    return ws;
}

// WebSocket: cleartext upgrade handshake with an echo endpoint.
class echo_ws_handler : public httpserver::websocket_handler {
 public:
    void on_message(httpserver::websocket_session& session,
                    std::string_view msg) override {
        session.send_text(std::string(msg));
    }
};

std::unique_ptr<webserver> build_websocket() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0));
    ws->register_ws_resource("/ws", std::make_shared<echo_ws_handler>());
    register_smoke(*ws);
    return ws;
}

// TLS: the repo's self-signed test certificate. Only reachable through
// curl-mediated cases in the transcript corpus (recorded limitation).
std::unique_ptr<webserver> build_tls() {
    auto ws = std::make_unique<webserver>(create_webserver().port(0)
        .use_ssl(true)
        .https_mem_key(PARITY_DATA_ROOT "/key.pem")
        .https_mem_cert(PARITY_DATA_ROOT "/cert.pem"));
    ws->on_get("/hello", [](const http_request&) {
        return http_response::string("OK");
    });
    register_smoke(*ws);
    return ws;
}

}  // namespace

v2_fixture::v2_fixture() = default;
v2_fixture::~v2_fixture() = default;

const std::vector<std::string>& v2_fixture::profile_names() {
    static const std::vector<std::string> names = {
        "routing_basic", "routing_hooks", "auth_basic", "auth_digest",
        "forms", "file_resp", "ip_controls", "shoutcast", "websocket",
        "tls",
    };
    return names;
}

bool v2_fixture::has_profile(const std::string& name) const {
    for (const std::string& n : profile_names()) {
        if (n == name) return true;
    }
    return false;
}

bool v2_fixture::profile_available(const std::string& name) const {
    auto f = webserver::features();
    if (name == "auth_digest") return f.digest_auth;
    if (name == "auth_basic") return f.basic_auth;
    if (name == "tls") return f.tls;
    if (name == "websocket") return f.websocket;
    return has_profile(name);
}

uint16_t v2_fixture::start(const std::string& name) {
    stop();
    std::unique_ptr<webserver> server;
    if (name == "routing_basic") {
        server = build_routing_basic();
    } else if (name == "routing_hooks") {
        server = build_routing_hooks();
    } else if (name == "auth_basic") {
        server = build_auth_basic();
    } else if (name == "auth_digest") {
        if (!webserver::features().digest_auth) {
            throw std::runtime_error("auth_digest unavailable: HAVE_DAUTH off");
        }
        server = build_auth_digest();
    } else if (name == "forms") {
        server = build_forms();
    } else if (name == "file_resp") {
        server = build_file_resp();
    } else if (name == "ip_controls") {
        server = build_ip_controls();
    } else if (name == "shoutcast") {
        server = build_shoutcast();
    } else if (name == "websocket") {
        if (!webserver::features().websocket) {
            throw std::runtime_error("websocket unavailable: HAVE_WEBSOCKET off");
        }
        server = build_websocket();
    } else if (name == "tls") {
        if (!webserver::features().tls) {
            throw std::runtime_error("tls unavailable: HAVE_GNUTLS off");
        }
        server = build_tls();
    } else {
        throw std::runtime_error("unknown profile: " + name);
    }
    // Non-blocking start: throws on daemon failure; returns false by
    // design (true is reserved for a blocking start that drained).
    server->start(false);
    if (!server->is_running()) {
        throw std::runtime_error("failed to start profile: " + name);
    }
    uint16_t port = server->get_bound_port();
    ws_ = std::move(server);
    return port;
}

void v2_fixture::stop() {
    if (ws_ != nullptr) {
        ws_->stop();
        ws_.reset();
    }
}

}  // namespace parity
