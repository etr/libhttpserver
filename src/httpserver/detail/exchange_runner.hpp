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

// Route execution over the exchange state machine. NOT part of the
// installed surface; consumers cannot reach it through the public
// umbrella. The engine seam itself (detail::exchange_sink) is declared
// in the public <httpserver/exchange.hpp> because the exchange's
// inline decisions invoke it; this header carries the pieces that only
// the engine (and the unit suites standing in for it) need.
#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/exchange_runner.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_EXCHANGE_RUNNER_HPP_
#define SRC_HTTPSERVER_DETAIL_EXCHANGE_RUNNER_HPP_

#include <cstdint>

#include <httpserver/exchange.hpp>

namespace httpserver {

namespace detail {

// Call-counting stand-in for the engine seam: the decision suites'
// oracle. Every committed decision is recorded with its payload, plus
// the resume signals the exchange handed out (so a test can fire or
// inspect them the way the engine would).
class recording_sink final : public exchange_sink {
 public:
    void on_admit(const body_policy& policy) override {
        ++admit_calls;
        admitted_bytes = policy.max_buffer_bytes;
    }

    void on_respond(const http::status& s, const http::fields& f) override {
        ++respond_calls;
        respond_code = s.code();
        respond_fields_size = f.size();
    }

    void on_upgrade(const ws_upgrade_options& options) override {
        ++upgrade_calls;
        upgrade_subprotocols = options.subprotocols.size();
    }

    void on_abort() override {
        ++abort_calls;
    }

    int admit_calls = 0;
    int respond_calls = 0;
    int upgrade_calls = 0;
    int abort_calls = 0;
    std::uint16_t respond_code = 0;
    std::size_t respond_fields_size = 0;
    std::uint64_t admitted_bytes = 0;
    std::size_t upgrade_subprotocols = 0;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_EXCHANGE_RUNNER_HPP_
