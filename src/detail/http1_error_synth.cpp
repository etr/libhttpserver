/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http1_error_synth.hpp>
#include <string>

namespace httpserver::detail {

auto http1_error_synth::serialize(http::protocol version, std::uint16_t code, const http1_response_framer::clock_source& clock) -> std::string {
    // The synthetic request head carries Connection: close so the
    // keep-alive verdict closes and the framer emits the header.
    http::request_head request;
    request.request_protocol = version;
    request.request_method = http::method::known(http::method_id::get);
    request.raw_target = "/";
    request.route_path = "/";
    request.head_fields.append("Connection", "close");
    http::fields fields;
    fields.append("Content-Length", "0");
    http1_response_framer framer(clock);
    std::string wire;
    const http::outcome head = framer.start_head(wire, request, http::status::from_code(code), fields);
    if (!head.ok()) {
        wire.clear();
        return wire;
    }
    static_cast<void>(framer.finish_body(wire, http::fields()));
    return wire;
}

}  // namespace httpserver::detail
