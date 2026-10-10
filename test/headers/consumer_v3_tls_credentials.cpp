/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <utility>
#include <httpserver/server/tls_credentials.hpp>
#include <httpserver/features.hpp>

int main() {
    httpserver::server::tls_credentials store;
    auto shared = store;
    auto moved = std::move(shared);
    const httpserver::server::tls_credentials_config empty;
    const auto expected = httpserver::query_features().tcp_tls
        ? httpserver::http::outcome_code::invalid_argument : httpserver::http::outcome_code::not_supported;
    return store.replace(empty).code() != expected || moved.replace(empty).code() != expected
        || shared.replace(empty).code() != expected;
}
