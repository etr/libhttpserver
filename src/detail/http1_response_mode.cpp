/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http1_response_mode.hpp>
#include <string>
#include <utility>

namespace httpserver::detail {

auto http1_response_mode::compute(const http::request_head& request, const http::status& s, const http::fields& fields) -> http1_response_mode {
    http1_response_mode mode;
    if (!s.valid()) {
        // Engine-internal misuse (an invalid status must never reach
        // a response commit); respond_then_close lets the engine
        // emit a synthesized error before the reset it now needs.
        return reject(mode, http::outcome_code::invalid_argument, "status code is outside 100..599", http1_close_policy::respond_then_close);
    }
    if (s.informational() || s.code() == 204) {
        // RFC 9112 section 6.3 rule 1: no body, and handler framing
        // fields are stripped at emission (the framer's rule).
        return mode;
    }
    if (s.code() == 304) {
        // Metadata only: a Content-Length rides as the would-be GET
        // body size; Transfer-Encoding is stripped at emission.
        mode.kind = http1_response_body_kind::metadata_only;
        return mode;
    }
    if (request.request_method.id() == http::method_id::head) {
        // HEAD: fields verbatim, no framing validation. Migration
        // note: v3 emits the handler's fields exactly and adds no
        // framing, so even an ambiguous CL+TE pair passes through
        // (there is no body to frame).
        mode.kind = http1_response_body_kind::head_no_body;
        return mode;
    }
    compute_body_framing(mode, request, fields);
    return mode;
}

auto http1_response_mode::compute_body_framing(http1_response_mode& mode, const http::request_head& request, const http::fields& fields) -> void {
    const std::span<const std::string> te = fields.all("transfer-encoding");
    const std::span<const std::string> cl = fields.all("content-length");
    // Both framings on a body response is ambiguous no matter what
    // the values say (the request-side smuggling rule, mirrored).
    if (!te.empty() && !cl.empty()) {
        reject(mode, http::outcome_code::protocol_error, "Transfer-Encoding together with Content-Length", http1_close_policy::close_now);
        return;
    }
    if (!te.empty()) {
        compute_from_te(mode, te);
    } else if (!cl.empty()) {
        compute_from_cl(mode, cl);
    } else if (request.request_protocol == http::protocol::http_1_1) {
        // Engine-selected chunked: the framer appends
        // "Transfer-Encoding: chunked".
        mode.kind = http1_response_body_kind::chunked;
    } else {
        mode.kind = http1_response_body_kind::close_delimited;
    }
}

auto http1_response_mode::reject(http1_response_mode& mode, http::outcome_code code, std::string what, http1_close_policy policy) -> http1_response_mode& {
    mode.failure = http::outcome(code, "http1_response_mode: " + std::move(what));
    mode.close_policy = policy;
    return mode;
}

auto http1_response_mode::compute_from_te(http1_response_mode& mode, const std::span<const std::string>& values) -> void {
    if (!is_single_chunked_te(values)) {
        reject(mode, http::outcome_code::protocol_error, "Transfer-Encoding is not exactly one final chunked", http1_close_policy::close_now);
        return;
    }
    mode.kind = http1_response_body_kind::chunked;
}

auto http1_response_mode::is_single_chunked_te(const std::span<const std::string>& values) -> bool {
    if (values.size() != 1) return false;
    std::string_view rest = values.front();
    bool chunked_seen = false;
    for (;;) {
        const std::size_t comma = rest.find(',');
        const std::string_view element = detail_head::trim_ows(rest.substr(0, comma));
        const bool is_chunked = !element.empty() && detail_head::ascii_iequals(element, "chunked");
        if (!is_chunked || chunked_seen) return false;
        chunked_seen = true;
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    return chunked_seen;
}

auto http1_response_mode::compute_from_cl(http1_response_mode& mode, const std::span<const std::string>& values) -> void {
    if (values.size() > 1) {
        // Migration note (same as the request side): even identical
        // duplicates are refused — framing-ambiguity resistance.
        reject(mode, http::outcome_code::protocol_error, "duplicate Content-Length", http1_close_policy::close_now);
        return;
    }
    std::uint64_t parsed = 0;
    if (!parse_length(values.front(), parsed)) {
        reject(mode, http::outcome_code::protocol_error, "Content-Length is not a valid digit count", http1_close_policy::close_now);
        return;
    }
    mode.kind = http1_response_body_kind::length;
    mode.content_length = parsed;
}

auto http1_response_mode::parse_length(std::string_view value, std::uint64_t& out) -> bool {
    if (value.empty()) return false;
    for (const char c : value) {
        if (c < '0' || c > '9') return false;
    }
    if (value.size() >= 2 && value.front() == '0') return false;
    return std::from_chars(value.data(), value.data() + value.size(), out, 10).ec == std::errc();
}

bool has_connection_token(const std::span<const std::string>& values, std::string_view token) {
    for (const std::string& value : values) {
        std::string_view rest = value;
        for (;;) {
            const std::size_t comma = rest.find(',');
            const std::string_view element = detail_head::trim_ows(rest.substr(0, comma));
            if (detail_head::ascii_iequals(element, token)) return true;
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
    return false;
}

http1_keepalive http1_response_keepalive(const http::request_head& request, http1_response_body_kind kind, http1_close_policy hint) {
    if (hint != http1_close_policy::none || kind == http1_response_body_kind::close_delimited) {
        return http1_keepalive::close;
    }
    const std::span<const std::string> connection = request.head_fields.all("connection");
    if (has_connection_token(connection, "close")) {
        return http1_keepalive::close;
    }
    if (request.request_protocol == http::protocol::http_1_0) {
        return has_connection_token(connection, "keep-alive") ? http1_keepalive::keep_alive : http1_keepalive::close;
    }
    return http1_keepalive::keep_alive;
}

}  // namespace httpserver::detail
