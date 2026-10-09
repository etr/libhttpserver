/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_PARITY_BEHAVIOR_REPLAY_HPP_
#define TEST_PARITY_BEHAVIOR_REPLAY_HPP_
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <parity/case_wire.hpp>

namespace parity {

// Bounded to the committed length-framed corpus. Native HTTP/1 replay still
// writes the original segments; only multiplexed translation reassembles them.
inline std::vector<corpus_request> behavior_requests(const tcase& test) {
    constexpr std::size_t max_bytes = 1024 * 1024;
    constexpr std::size_t max_requests = 16;
    std::string wire;
    for (const auto& segment : test.sends) {
        if (segment.bytes.size() > max_bytes - wire.size()) throw std::runtime_error("corpus byte bound");
        wire += segment.bytes;
    }
    std::vector<corpus_request> requests;
    std::size_t at = 0;
    while (at < wire.size()) {
        if (requests.size() == max_requests) throw std::runtime_error("corpus request bound");
        const auto end = wire.find("\r\n\r\n", at);
        if (end == std::string::npos) throw std::runtime_error("truncated corpus request head");
        const auto line_end = wire.find("\r\n", at);
        const auto method_end = wire.find(' ', at);
        const auto target_end = wire.find(' ', method_end + 1);
        if (method_end >= line_end || target_end >= line_end || method_end == at || target_end == method_end + 1)
            throw std::runtime_error("malformed corpus request line");
        tcase single;
        single.sends.push_back({wire.substr(at, end + 4 - at), 1});
        auto request = parse_case(single);
        if (!httpserver::http::method::parse(wire.substr(at, method_end - at)) ||
                !httpserver::http::parse(wire.substr(target_end + 1, line_end - target_end - 1)))
            throw std::runtime_error("unknown corpus method or protocol");
        if (request.head.head_fields.count("Content-Length") > 1 || request.head.head_fields.first("Transfer-Encoding"))
            throw std::runtime_error("unsupported corpus framing");
        std::size_t length = 0;
        if (const auto field = request.head.head_fields.first("Content-Length")) {
            const auto parsed = std::from_chars(field->data(), field->data() + field->size(), length);
            if (parsed.ec != std::errc() || parsed.ptr != field->data() + field->size())
                throw std::runtime_error("malformed corpus length");
        }
        at = end + 4;
        if (length > wire.size() - at) throw std::runtime_error("truncated corpus body");
        request.body = wire.substr(at, length);
        at += length;
        request.head.route_path = request.head.raw_target.substr(0, request.head.raw_target.find('?'));
        requests.push_back(std::move(request));
    }
    if (requests.empty()) throw std::runtime_error("empty corpus request sequence");
    return requests;
}

inline std::string field_lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Connection-specific fields are prohibited by RFC 9113/9114. Other field
// values, duplicates and their relative order remain unchanged.
inline std::vector<std::pair<std::string, std::string>> multiplexed_fields(const corpus_request& request) {
    const auto& head = request.head;
    std::vector<std::pair<std::string, std::string>> result{
        {":method", std::string(head.request_method.name())}, {":scheme", "https"},
        {":path", head.raw_target}, {":authority", std::string(head.head_fields.first("Host").value_or("localhost"))}};
    std::vector<std::string> forbidden{"host", "connection", "keep-alive", "proxy-connection", "transfer-encoding", "upgrade"};
    for (const auto& value : head.head_fields.all("Connection")) {
        std::size_t at = 0;
        while (at < value.size()) {
            const auto comma = value.find(',', at);
            auto token = value.substr(at, comma == std::string::npos ? comma : comma - at);
            const auto begin = token.find_first_not_of(" \t");
            if (begin != std::string::npos)
                forbidden.push_back(field_lower(token.substr(begin, token.find_last_not_of(" \t") - begin + 1)));
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
    }
    for (const auto field : head.head_fields.entries()) {
        const auto name = field_lower(field.name);
        if (std::find(forbidden.begin(), forbidden.end(), name) == forbidden.end())
            result.emplace_back(name, field.value);
    }
    return result;
}

}  // namespace parity
#endif  // TEST_PARITY_BEHAVIOR_REPLAY_HPP_
