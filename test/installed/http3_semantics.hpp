/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_INSTALLED_HTTP3_SEMANTICS_HPP_
#define TEST_INSTALLED_HTTP3_SEMANTICS_HPP_
#include <httpserver.hpp>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif
namespace public_h3 {
namespace h = httpserver;
inline h::task<void> text(h::exchange& x, std::string body, h::http::fields fields = {}) {
    x.start_response(h::http::status::from_code(200), fields);
    co_await x.writer().write(std::as_bytes(std::span(body)));
    co_await x.writer().finish();
}
inline void install_semantics(h::server::native_server& server, const std::string& directory) {
    const auto get = h::http::method::known(h::http::method_id::get);
    const auto post = h::http::method::known(h::http::method_id::post);
    server.route(post, "/semantic", [](h::exchange& x) -> h::task<void> {
        x.admit_body({65536}); std::array<std::byte, 7> bytes{}; std::string body;
        for (;;) { const auto part = co_await x.body().read_some(bytes); if (!part.status.ok()) co_return;
            body.append(reinterpret_cast<const char*>(part.data.data()), part.data.size()); if (part.end_of_body) break; }
        h::http::fields fields; fields.append("x-method", x.head().request_method.name());
        fields.append("x-target", x.head().raw_target); fields.append("x-route", x.head().route_path);
        for (auto value : x.head().head_fields.all("x-repeat")) fields.append("x-repeat", value);
        x.start_response(h::http::status::from_code(200), fields);
        co_await x.writer().write(std::as_bytes(std::span(body)));
        h::http::fields trailers; for (auto value : x.body().trailers().all("x-upload-end")) trailers.append("x-upload-end", value);
        co_await x.writer().finish(trailers);
    });
    server.route(get, "/items/{id}", [](h::exchange& x) { return text(x, "parameter"); });
    h::http::method_set methods; methods.set(h::http::method_id::get);
    server.route_prefix(methods, "/prefix", [](h::exchange& x) { return text(x, "prefix"); });
    auto extension = h::http::method::extension("CUSTOM");
    server.route(extension, "/extension", [](h::exchange& x) { return text(x, "extension"); });
    server.route(get, "/throw", [](h::exchange&) -> h::task<void> { throw std::runtime_error("public exception"); co_return; });
    server.route(get, "/missing-terminal", [](h::exchange&) -> h::task<void> { co_return; });
    server.route(get, "/throw-committed", [](h::exchange& x) -> h::task<void> {
        x.start_response(h::http::status::from_code(200), {}); throw std::runtime_error("committed exception"); co_return;
    });
    h::auth::basic_auth_policy basic;
    if (!h::auth::basic_auth_policy::create("public", "user", "password", basic).ok()) throw std::runtime_error("basic policy");
    server.route(get, "/auth/basic", h::auth::make_basic_guard(std::move(basic), [](h::exchange& x) { return text(x, "authenticated"); }));
    h::auth::digest_auth_policy digest;
    if (!h::auth::digest_auth_policy::create("public", "user", "password", {}, digest).ok()) throw std::runtime_error("digest policy");
    server.route(get, "/auth/digest", h::auth::make_digest_guard(std::move(digest), [](h::exchange& x) { return text(x, "authenticated"); }));
    server.route(post, "/form", [](h::exchange& x) -> h::task<void> {
        auto parsed = co_await h::forms::read_urlencoded(x, {256, 4});
        if (!parsed.ok()) { x.respond(parsed.reject_status(), parsed.reject_fields()); co_return; }
        std::string output; for (const auto& [name, value] : parsed.fields.entries()) output += name + "=" + value + "\n";
        co_await text(x, output);
    });
    server.route(post, "/multipart", [directory](h::exchange& x) -> h::task<void> {
        h::forms::temp_file_part_sink sink({directory, true, {}});
        auto parsed = co_await h::forms::read_multipart(x, {4096, 4, 2048, 512}, sink);
        if (!parsed.ok()) { x.respond(parsed.reject_status(), parsed.reject_fields()); co_return; }
        const auto fields = sink.take_fields();
        std::string output; for (const auto& [name, value] : fields.entries()) output += name + "=" + value + "\n";
        for (const auto& file : sink.completed()) output += file.name + ":" + file.filename + ":" + std::to_string(file.file_size) + "\n";
        co_await text(x, output);
    });
    auto owned = std::make_shared<h::response_definition>(); h::http::fields base; base.append("x-overlay", "base");
    const std::string payload = "immutable definition";
    std::vector<std::byte> body(std::as_bytes(std::span(payload)).begin(), std::as_bytes(std::span(payload)).end());
    if (!h::response_definition::owned_bytes(h::http::status::from_code(200), base, std::move(body), *owned).ok()) throw std::runtime_error("definition");
    server.route(get, "/definition", [owned](h::exchange& x) -> h::task<void> {
        h::response_overlay overlay; overlay.headers.append("x-overlay", "request"); overlay.trailers.append("x-end", "definition");
        co_await h::send_definition(x, *owned, std::move(overlay));
    });
    auto file = std::make_shared<h::response_definition>();
    h::response_definition::reopen_file(h::http::status::from_code(200), {}, directory + "/root.pem", *file);
    server.route(get, "/file", [file](h::exchange& x) -> h::task<void> { co_await h::send_definition(x, *file); });
    auto borrowed = std::make_shared<h::response_definition>(); auto keeper = std::make_shared<const std::string>("borrowed immutable bytes");
    h::response_definition::borrowed(h::http::status::from_code(200), {}, std::as_bytes(std::span(*keeper)), h::body_lease(keeper), *borrowed);
    server.route(get, "/borrowed", [borrowed](h::exchange& x) -> h::task<void> { co_await h::send_definition(x, *borrowed); });
    server.route(get, "/missing-file", [](h::exchange& x) -> h::task<void> {
        h::response_definition missing; h::response_definition::reopen_file(h::http::status::from_code(200), {}, "/missing-task197-response", missing);
        auto sent = co_await h::send_definition(x, missing);
        if (sent.status.code() != h::http::outcome_code::invalid_argument) throw std::runtime_error("missing file outcome");
        x.respond(h::http::status::from_code(404), {});
    });
    for (const bool pipe_source : {false, true}) {
        struct source {
            h::response_definition definition;
            std::shared_ptr<std::atomic<unsigned>> closes = std::make_shared<std::atomic<unsigned>>(0);
        };
        auto sources = std::make_shared<std::array<source, 3>>();
        for (auto& item : *sources) {
            const std::string bytes = pipe_source ? "one-shot pipe" : "one-shot file";
            std::FILE* handle = nullptr;
            if (pipe_source) {
                int descriptors[2];
#ifdef _WIN32
                if (::_pipe(descriptors, 4096, _O_BINARY) != 0) throw std::runtime_error("source pipe");
                const auto written = ::_write(descriptors[1], bytes.data(), static_cast<unsigned>(bytes.size()));
                ::_close(descriptors[1]);
                handle = ::_fdopen(descriptors[0], "rb");
#else
                if (::pipe(descriptors) != 0) throw std::runtime_error("source pipe");
                const auto written = ::write(descriptors[1], bytes.data(), bytes.size());
                ::close(descriptors[1]);
                handle = ::fdopen(descriptors[0], "rb");
#endif
                if (!handle || written != static_cast<decltype(written)>(bytes.size())) throw std::runtime_error("source pipe bytes");
            } else {
                handle = std::tmpfile();
                if (!handle || std::fwrite(bytes.data(), 1, bytes.size(), handle) != bytes.size()) throw std::runtime_error("source file bytes");
                std::rewind(handle);
            }
            auto close = [count=item.closes](std::FILE* file) { std::fclose(file); ++*count; };
            const auto result = pipe_source
                ? h::response_definition::owned_pipe(h::http::status::from_code(200), {}, handle, close, item.definition)
                : h::response_definition::owned_file(h::http::status::from_code(200), {}, handle, close, item.definition);
            if (!result.ok()) throw std::runtime_error("one-shot definition");
        }
        server.route(get, pipe_source ? "/owned-pipe-once" : "/owned-file-once", [sources](h::exchange& x) -> h::task<void> {
            const auto protocol = x.head().request_protocol;
            auto& selected = (*sources)[protocol == h::http::protocol::http_1_1 ? 0 : protocol == h::http::protocol::http_2 ? 1 : 2];
            const auto sent = co_await h::send_definition(x, selected.definition);
            if (!sent.status.ok()) {
                if (sent.status.code() != h::http::outcome_code::invalid_state) throw std::runtime_error("spent source outcome");
                h::http::fields fields; fields.append("x-source-closes", std::to_string(selected.closes->load()));
                x.respond(h::http::status::from_code(409), fields);
            }
        });
    }
    server.route(post, "/early", [](h::exchange& x) -> h::task<void> { x.respond(h::http::status::from_code(403), {}); co_return; });
}
}  // namespace public_h3
#endif
