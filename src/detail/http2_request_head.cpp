/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/http2_request_head.hpp>
namespace httpserver::detail {
bool http2_regular_field(std::string_view name, std::string_view value) { return multiplexed_head::regular_field(name, value); }
bool http2_content_length(const http::fields& fields, std::optional<std::uint64_t>& length) { return multiplexed_head::content_length(fields, length); }
bool http2_trailer_field(std::string_view name, std::string_view value) { return multiplexed_head::trailer_field(name, value); }
bool http2_convert_request(std::span<const hpack_field> fields, http::request_head& head, http2_connect_metadata& connect) {
    return multiplexed_head::convert(fields, head, http::protocol::http_2, &connect);
}
bool http2_convert_request(std::span<const hpack_field> fields, http::request_head& head) {
    http2_connect_metadata connect;
    return http2_convert_request(fields, head, connect);
}
}  // namespace httpserver::detail
