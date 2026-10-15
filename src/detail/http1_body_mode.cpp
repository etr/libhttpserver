/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http1_body_mode.hpp>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace httpserver::detail {

auto http1_body_mode::compute(const http::request_head& head) -> http1_body_mode {
    http1_body_mode mode;
    const std::span<const std::string> te = head.head_fields.all("transfer-encoding");
    const std::span<const std::string> cl = head.head_fields.all("content-length");
    // R1: both framings present is a request-smuggling probe, no
    // matter what the values say.
    if (!te.empty() && !cl.empty()) {
        reject(mode, http::outcome_code::protocol_error, "Transfer-Encoding together with Content-Length", http1_close_policy::close_now);
        return mode;
    }
    if (!te.empty()) {
        compute_transfer_encoding(mode, head.request_protocol, te);
    } else {
        compute_content_length(mode, cl);
    }
    return mode;
}

auto http1_body_mode::reject(http1_body_mode& mode, http::outcome_code code, std::string what, http1_close_policy policy) -> void {
    mode.kind = http1_body_kind::rejected;
    mode.failure = http::outcome(code, "http1_body_mode: " + std::move(what));
    mode.close_policy = policy;
}

auto http1_body_mode::compute_transfer_encoding(http1_body_mode& mode, http::protocol version, const std::span<const std::string>& values) -> void {
    // R2a: chunked exists only from HTTP/1.1 on.
    if (version == http::protocol::http_1_0) {
        reject(mode, http::outcome_code::protocol_error, "Transfer-Encoding on HTTP/1.0", http1_close_policy::close_now);
        return;
    }
    std::vector<std::string_view> elements;
    if (!split_elements(mode, values, elements)) return;
    classify_chunked(mode, elements);
}

auto http1_body_mode::split_elements(http1_body_mode& mode, const std::span<const std::string>& values, std::vector<std::string_view>& out) -> bool {
    for (const std::string& value : values) {
        std::string_view rest = value;
        for (;;) {
            const std::size_t comma = rest.find(',');
            const std::string_view element = detail_head::trim_ows(rest.substr(0, comma));
            if (element.empty() || !http::detail::is_token(element)) {
                reject(mode, http::outcome_code::protocol_error,
                       "Transfer-Encoding element is empty or not a"
                       " token",
                       http1_close_policy::close_now);
                return false;
            }
            out.push_back(element);
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
    return true;
}

auto http1_body_mode::classify_chunked(http1_body_mode& mode, const std::vector<std::string_view>& elements) -> void {
    bool chunked_seen = false;
    bool coding_before_chunked = false;
    for (const std::string_view element : elements) {
        if (ascii_iequals(element, "chunked")) {
            if (chunked_seen) {
                reject(mode, http::outcome_code::protocol_error, "chunked appears more than once", http1_close_policy::close_now);
                return;
            }
            chunked_seen = true;
            continue;
        }
        if (chunked_seen) {
            reject(mode, http::outcome_code::protocol_error, "chunked is not the final transfer coding", http1_close_policy::close_now);
            return;
        }
        coding_before_chunked = true;
    }
    if (!chunked_seen) {
        reject(mode, http::outcome_code::protocol_error, "Transfer-Encoding without chunked", http1_close_policy::close_now);
        return;
    }
    if (coding_before_chunked) {
        reject(mode, http::outcome_code::not_supported, "transfer coding before chunked is not supported", http1_close_policy::respond_then_close);
        return;
    }
    mode.kind = http1_body_kind::chunked;
}

auto http1_body_mode::compute_content_length(http1_body_mode& mode, const std::span<const std::string>& values) -> void {
    // R4: no framing headers at all — no body.
    if (values.empty()) return;
    if (values.size() > 1) {
        // Migration note: RFC 9110 section 8.6 MAYs that
        // recipients combine duplicate Content-Length values; v3
        // declines (even identical duplicates are a classic
        // request-smuggling probe).
        reject(mode, http::outcome_code::protocol_error, "duplicate Content-Length", http1_close_policy::close_now);
        return;
    }
    std::uint64_t parsed = 0;
    if (!parse_length(values.front(), parsed)) {
        // Migration note: a leading-zero spelling such as "007" is
        // RFC-legal (1*DIGIT) but rejected for framing-ambiguity
        // resistance.
        reject(mode, http::outcome_code::protocol_error, "Content-Length is not a valid digit count", http1_close_policy::close_now);
        return;
    }
    mode.kind = http1_body_kind::length;
    mode.content_length = parsed;
}

auto http1_body_mode::parse_length(std::string_view value, std::uint64_t& out) -> bool {
    if (value.empty()) return false;
    for (const char c : value) {
        if (c < '0' || c > '9') return false;
    }
    if (value.size() >= 2 && value.front() == '0') return false;
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t parsed = 0;
    for (const char c : value) {
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (parsed > (kMax - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    out = parsed;
    return true;
}

}  // namespace httpserver::detail
