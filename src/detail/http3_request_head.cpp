/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http3_request_head.hpp>
#include <httpserver/detail/multiplexed_request_head.hpp>
namespace httpserver::detail {
bool http3_regular_field(std::string_view name, std::string_view value) {
    return multiplexed_head::regular_field(name, value);
}
bool http3_content_length(const http::fields& fields, std::optional<std::uint64_t>& length) {
    return multiplexed_head::content_length(fields, length);
}
bool http3_trailer_field(std::string_view name, std::string_view value) {
    return multiplexed_head::trailer_field(name, value);
}
bool http3_convert_request(std::span<const qpack_field> fields, http::request_head& head) {
    return multiplexed_head::convert(fields, head, http::protocol::http_3, nullptr);
}
bool http3_convert_trailers(std::span<const qpack_field> fields, http::fields& out) {
    for (const auto& field : fields) {
        if (!http3_trailer_field(field.name, field.value)) return false;
        out.append(field.name, field.value);
    }
    return true;
}
}  // namespace httpserver::detail
