/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_PARITY_BEHAVIOR_OBSERVATION_HPP_
#define TEST_PARITY_BEHAVIOR_OBSERVATION_HPP_
#include <ostream>
#include <string>
#include <string_view>
#include <parity/response_frame.hpp>
namespace parity {
inline std::string behavior_hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const unsigned char byte : bytes) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    return hex;
}
inline void behavior_json_text(std::ostream& out, std::string_view text) {
    out << '"';
    for (const unsigned char byte : text) {
        if (byte == '"' || byte == '\\') out << '\\' << static_cast<char>(byte);
        else if (byte < 32 || byte >= 127) out << "\\u00" << behavior_hex(std::string(1, byte));
        else out << static_cast<char>(byte);
    }
    out << '"';
}
inline void behavior_json_response(std::ostream& out, const observed_response& response) {
    out << "{\"status\":" << response.status << ",\"status_line\":";
    behavior_json_text(out, response.raw_status_line);
    out << ",\"headers\":[";
    bool first = true;
    for (const auto& field : response.headers) {
        if (!first) out << ',';
        first = false;
        out << '[';
        behavior_json_text(out, field.name);
        out << ',';
        behavior_json_text(out, field.value);
        out << ']';
    }
    out << "],\"trailers\":[],\"body_hex\":";
    behavior_json_text(out, behavior_hex(response.body));
    out << ",\"framing\":";
    behavior_json_text(out, response.framing);
    out << ",\"ended\":true}";
}
}  // namespace parity
#endif  // TEST_PARITY_BEHAVIOR_OBSERVATION_HPP_
