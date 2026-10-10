/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <type_traits>
#include <httpserver/server/configuration.hpp>
#include <httpserver/server/diagnostics.hpp>
int main() {
    static_assert(std::is_trivially_copyable_v<httpserver::server::diagnostic_event>);
    httpserver::server::diagnostics channel([](const auto&) {});
    httpserver::server::server_options options;
    options.diagnostic_channel() = channel;
    return channel.dispatch().code == httpserver::http::outcome_code::ok ? 0 : 1;
}
