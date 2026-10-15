/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/response_definition.hpp>

namespace httpserver::detail {

bool field_value_has_ctl(std::string_view v) noexcept {
    for (const char c : v) {
        const auto u = static_cast<unsigned char>(c);
        if ((u < 0x20 && u != '\t') || u == 0x7F) return true;
    }
    return false;
}

http::outcome check_fields_wire_safe(const http::fields& f) {
    for (const http::fields::entry e : f.entries()) {
        if (!http::detail::is_token(e.name)) {
            return http::outcome(http::outcome_code::invalid_argument, "response fields: name '" + std::string(e.name) + "' is not a token");
        }
        if (field_value_has_ctl(e.value)) {
            return http::outcome(http::outcome_code::invalid_argument, "response fields: value of '" + std::string(e.name) + "' carries a control byte");
        }
    }
    return http::outcome::okay();
}

http::outcome check_fields_framing(const http::fields& f) {
    if (f.count("content-length") > 1) {
        return http::outcome(http::outcome_code::invalid_argument, "response fields: repeated Content-Length");
    }
    if (f.count("content-length") > 0 && f.count("transfer-encoding") > 0) {
        return http::outcome(http::outcome_code::invalid_argument, "response fields: Transfer-Encoding with Content-Length");
    }
    std::uint64_t declared = 0;
    return parse_content_length(f, declared);
}

void pin_content_length(http::fields& f, std::uint64_t size) {
    if (f.count("content-length") == 0 && f.count("transfer-encoding") == 0) {
        f.append("Content-Length", std::to_string(size));
    }
}

}  // namespace httpserver::detail

namespace httpserver {

auto response_definition::owned_bytes(const http::status& s, http::fields f, std::vector<std::byte> body, response_definition& out) -> http::outcome {
    build state;
    state.status = s;
    detail::pin_content_length(f, body.size());
    state.fields = std::move(f);
    state.body.kind = source_kind::owned_bytes;
    state.body.bytes = std::move(body);
    return commit(std::move(state), out);
}

auto response_definition::reopen_file(const http::status& s, http::fields f, std::string path, response_definition& out) -> http::outcome {
    build state;
    state.status = s;
    state.fields = std::move(f);
    state.body.kind = source_kind::reopen_file;
    state.body.path = std::move(path);
    return commit(std::move(state), out);
}

auto response_definition::factory(const http::status& s, http::fields f, body_factory make, response_definition& out) -> http::outcome {
    build state;
    state.status = s;
    state.fields = std::move(f);
    state.body.kind = source_kind::factory;
    state.body.make = std::move(make);
    return commit(std::move(state), out);
}

auto response_definition::borrowed(const http::status& s, http::fields f, std::span<const std::byte> body, body_lease lease, response_definition& out) -> http::outcome {
    build state;
    state.status = s;
    detail::pin_content_length(f, body.size());
    state.fields = std::move(f);
    state.body.kind = source_kind::borrowed;
    state.body.view = body;
    state.body.lease = std::move(lease);
    return commit(std::move(state), out);
}

auto response_definition::owned_file(const http::status& s, http::fields f, std::FILE* handle, owned_close_fn close, response_definition& out) -> http::outcome {
    if (handle == nullptr || !close) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "response_definition: owned_file requires a handle and "
                             "a close operation");
    }
    build state;
    state.status = s;
    state.fields = std::move(f);
    state.body.kind = source_kind::owned_file;
    state.body.handle = std::make_shared<detail::transferred_handle>(handle, std::move(close));
    return commit(std::move(state), out);
}

auto response_definition::owned_pipe(const http::status& s, http::fields f, std::FILE* handle, owned_close_fn close, response_definition& out) -> http::outcome {
    if (handle == nullptr || !close) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "response_definition: owned_pipe requires a handle and "
                             "a close operation");
    }
    build state;
    state.status = s;
    state.fields = std::move(f);
    state.body.kind = source_kind::owned_pipe;
    state.body.handle = std::make_shared<detail::transferred_handle>(handle, std::move(close));
    return commit(std::move(state), out);
}

auto response_definition::status() const noexcept -> const http::status& {
    static const http::status empty;
    return impl_ ? impl_->status : empty;
}

auto response_definition::fields() const noexcept -> const http::fields& {
    static const http::fields empty;
    return impl_ ? impl_->fields : empty;
}

auto response_definition::validate(const build& state) -> http::outcome {
    if (!state.status.valid()) {
        return http::outcome(http::outcome_code::invalid_argument, "response_definition: status code is invalid");
    }
    const http::outcome wire = detail::check_fields_wire_safe(state.fields);
    if (!wire.ok()) return wire;
    const http::outcome framing = detail::check_fields_framing(state.fields);
    if (!framing.ok()) return framing;
    if (state.body.kind == source_kind::reopen_file && state.body.path.empty()) {
        return http::outcome(http::outcome_code::invalid_argument, "response_definition: reopen_file requires a path");
    }
    if (state.body.kind == source_kind::factory && !state.body.make) {
        return http::outcome(http::outcome_code::invalid_argument, "response_definition: factory requires a callable");
    }
    if (state.body.kind == source_kind::borrowed && !state.body.lease.valid()) {
        return http::outcome(http::outcome_code::invalid_argument, "response_definition: borrowed requires a lease");
    }
    return http::outcome::okay();
}

auto response_definition::commit(build&& state, response_definition& out) -> http::outcome {
    const http::outcome checked = validate(state);
    if (!checked.ok()) return checked;
    impl frozen;
    frozen.status = std::move(state.status);
    frozen.fields = std::move(state.fields);
    frozen.body = std::make_shared<const detail::response_body_source>(std::move(state.body));
    out.impl_ = std::make_shared<const impl>(std::move(frozen));
    return http::outcome::okay();
}

}  // namespace httpserver

namespace httpserver::detail {

http::fields merge_send_fields(const response_definition& def, const response_overlay& overlay) {
    http::fields merged = def.fields();
    for (const http::fields::entry e : overlay.headers.entries()) {
        merged.append(e.name, e.value);
    }
    return merged;
}

http::outcome check_trailer_names(const http::fields& trailers) {
    for (const std::string_view name : {"Transfer-Encoding", "Content-Length", "Host"}) {
        if (trailers.count(name) > 0) {
            return http::outcome(http::outcome_code::invalid_argument, "response fields: '" + std::string(name) + "' cannot be a trailer");
        }
    }
    return http::outcome::okay();
}

http::outcome check_overlay(const response_overlay& overlay) {
    const http::outcome headers = check_fields_wire_safe(overlay.headers);
    if (!headers.ok()) return headers;
    const http::outcome trailers = check_fields_wire_safe(overlay.trailers);
    if (!trailers.ok()) return trailers;
    return check_trailer_names(overlay.trailers);
}

http::outcome prepare_send(const response_definition& def, const response_overlay& overlay, http::fields& merged, send_cursor& cursor) {
    if (!def.valid()) {
        return http::outcome(http::outcome_code::invalid_argument, "send_definition: definition is empty");
    }
    const http::outcome overlay_ok = check_overlay(overlay);
    if (!overlay_ok.ok()) return overlay_ok;
    merged = merge_send_fields(def, overlay);
    const http::outcome framing = check_fields_framing(merged);
    if (!framing.ok()) return framing;
    const std::shared_ptr<const response_body_source>& body = def.impl_->body;
    const http::outcome prepared = cursor.prepare(body, merged);
    if (!prepared.ok()) return prepared;
    if (body->kind == response_source_kind::reopen_file || body->kind == response_source_kind::owned_file) {
        // The pinned size is per send: the file is re-observed (or the
        // transferred handle probed) every time, never baked into the
        // definition.
        pin_content_length(merged, cursor.file_size());
    }
    return http::outcome::okay();
}

task<http::outcome> stream_body(exchange& x, send_cursor& cursor, std::size_t& body_bytes) {
    for (;;) {
        const body_chunk chunk = cursor.pull();
        if (!chunk.status.ok()) co_return chunk.status;
        if (!chunk.data.empty()) {
            const body_write written = co_await x.writer().write(chunk.data);
            if (!written.status.ok()) co_return written.status;
            body_bytes += chunk.data.size();
        }
        if (chunk.end) co_return http::outcome::okay();
    }
}

}  // namespace httpserver::detail

namespace httpserver {

task<send_report> send_definition(exchange& x, const response_definition& def, response_overlay overlay) {
    send_report report;
    if (x.disconnected()) {
        report.status = http::outcome(http::outcome_code::connection_closed, "send_definition: exchange is disconnected");
        co_return report;
    }
    http::fields merged;
    detail::send_cursor cursor;
    report.status = detail::prepare_send(def, overlay, merged, cursor);
    if (!report.status.ok()) co_return report;
    report.status = x.start_response(def.status(), merged);
    if (!report.status.ok()) co_return report;
    report.status = co_await detail::stream_body(x, cursor, report.body_bytes);
    if (!report.status.ok()) co_return report;
    const body_finish finished = co_await x.writer().finish(overlay.trailers);
    report.status = finished.status;
    co_return report;
}

}  // namespace httpserver
