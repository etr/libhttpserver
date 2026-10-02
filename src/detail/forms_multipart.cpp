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

// TASK-117: the multipart form implementation, part one
// (PRD-V3N-REQ-021/025, DR-V3-001). The strict incremental wire
// decoder lives in the private detail header; this TU owns the public
// vocabulary's members: the limits factory, the multipart_read
// rejection verdict (the shared forms_verdict helper, so form_read
// and multipart_read are one shaping, not two), the one-shot
// decode_multipart, the abort-once event adapter that turns the
// pure wire machine's events into the documented part_sink callbacks,
// the streaming read_multipart, and the bounded route adapter. The
// temp-file sink is forms_multipart_files.cpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/detail/auth_text.hpp>
#include <httpserver/detail/forms_multipart.hpp>
#include <httpserver/detail/forms_verdict.hpp>
#include <httpserver/forms/multipart.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/route_sync.hpp>

namespace httpserver {

namespace forms {

namespace {

constexpr std::string_view k_content_type_field = "Content-Type";
constexpr std::string_view k_multipart_media_type = "multipart/form-data";
// The streaming read feeds the decoder bounded chunks; the raw body is
// never buffered whole.
constexpr std::size_t kReadChunkBytes = 512;

// The v2 activation gate prefix match (request_pipeline.cpp): the
// media type case-insensitively matches up to a ';'/whitespace
// boundary, parameters after the type allowed.
bool is_multipart_content_type(
    const std::optional<std::string_view>& value) noexcept {
    if (!value.has_value()) return false;
    const std::string_view v = *value;
    if (v.size() < k_multipart_media_type.size()) return false;
    if (!detail::auth_text::ascii_iequal(
            v.substr(0, k_multipart_media_type.size()),
            k_multipart_media_type)) {
        return false;
    }
    if (v.size() == k_multipart_media_type.size()) return true;
    const char boundary = v[k_multipart_media_type.size()];
    return boundary == ';' || boundary == ' ' || boundary == '\t';
}

// Adapts the pure wire machine's events to a caller part_sink while
// tracking the in-flight part: every non-ok terminal forwards
// on_part_abort EXACTLY ONCE for the part that began but did not end
// (the TASK-117 cancellation contract; the decoder itself carries no
// cleanup policy).
class sink_event_adapter final : public detail::multipart_events {
 public:
    explicit sink_event_adapter(part_sink& sink) noexcept : sink_(sink) { }

    http::outcome on_part_begin(
        const detail::multipart_part_meta& meta) override {
        in_flight_ = true;
        return sink_.on_part_begin(
            part_descriptor{meta.name, meta.filename, meta.content_type,
                            meta.transfer_encoding});
    }

    http::outcome on_part_data(std::span<const std::byte> data) override {
        return sink_.on_part_data(data);
    }

    http::outcome on_part_end() override {
        in_flight_ = false;
        ++completed_;
        return sink_.on_part_end();
    }

    // The once-only abort of the in-flight part (a no-op when every
    // part ended, or when nothing began).
    void forward_abort(const http::outcome& reason) {
        if (!in_flight_) return;
        in_flight_ = false;
        sink_.on_part_abort(reason);
    }

    std::uint64_t completed() const noexcept { return completed_; }

 private:
    part_sink& sink_;
    std::uint64_t completed_ = 0;
    bool in_flight_ = false;
};

// The route adapter's internal sink: non-file parts decode into a
// form_fields; FILE parts are drained under the same caps and their
// bytes never buffered (the bounded value-adapter contract).
class field_collecting_sink final : public part_sink {
 public:
    http::outcome on_part_begin(const part_descriptor& part) override {
        draining_file_ = !part.filename.empty();
        if (!draining_file_) name_ = std::string(part.name);
        return http::outcome::okay();
    }

    http::outcome on_part_data(std::span<const std::byte> data) override {
        if (draining_file_) return http::outcome::okay();
        const char* raw = reinterpret_cast<const char*>(data.data());
        value_.append(raw, data.size());
        return http::outcome::okay();
    }

    http::outcome on_part_end() override {
        if (!draining_file_) {
            entries_.emplace_back(std::move(name_), std::move(value_));
            value_.clear();
        }
        return http::outcome::okay();
    }

    void on_part_abort(http::outcome) override {
        value_.clear();
        draining_file_ = false;
    }

    form_fields take_fields() {
        return form_fields(std::move(entries_));
    }

 private:
    std::vector<std::pair<std::string, std::string>> entries_;
    std::string name_;
    std::string value_;
    bool draining_file_ = false;
};

task<void> run_multipart_route(exchange& x,
                               const multipart_limits& caps,
                               const multipart_route_handler& call) {
    if (!is_multipart_content_type(
            x.head().head_fields.first(k_content_type_field))) {
        // v2 parity: no form processing at all, so the drained body
        // cannot reject; the handler sees no fields.
        if (!x.admit_body(body_policy{caps.max_total_bytes}).ok()) {
            co_return;
        }
        const body_collect drained =
            co_await x.body().collect(caps.max_total_bytes);
        if (!drained.status.ok()) {
            // Over-cap on the drain: the handler never runs (the
            // engine owns the undrained remainder).
            if (drained.status.code()
                    == http::outcome_code::limit_exceeded) {
                static_cast<void>(x.respond(http::status::from_code(413),
                                            http::fields()));
            }
            co_return;
        }
        co_await server::detail::commit_sync_value(
            x, call(x.head(), form_fields{}));
        co_return;
    }
    // The multipart branch: fields decode, file parts drain, and the
    // typed rejections carry the length-framed empty body.
    field_collecting_sink fields_sink;
    const multipart_read read =
        co_await read_multipart(x, caps, fields_sink);
    if (!read.ok()) {
        // A typed rejection commits the framed empty body; every other
        // failure (transport, cancellation, sink) ends without a local
        // commit and the runner synthesizes.
        if (read.status.code() == http::outcome_code::limit_exceeded
                || read.status.code()
                       == http::outcome_code::invalid_argument) {
            static_cast<void>(x.respond(read.reject_status(),
                                        read.reject_fields()));
        }
        co_return;
    }
    co_await server::detail::commit_sync_value(
        x, call(x.head(), fields_sink.take_fields()));
}


}  // namespace

http::outcome multipart_limits::create(std::uint64_t max_total_bytes,
                                       std::uint64_t max_parts,
                                       std::uint64_t max_part_bytes,
                                       std::uint64_t max_part_header_bytes,
                                       multipart_limits& out) {
    if (max_total_bytes < 1 || max_parts < 1 || max_part_bytes < 1
            || max_part_header_bytes < 1) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "multipart_limits: every cap must be at least 1");
    }
    out = multipart_limits{max_total_bytes, max_parts, max_part_bytes,
                           max_part_header_bytes};
    return http::outcome::okay();
}

bool multipart_read::ok() const noexcept {
    return status.ok();
}

http::status multipart_read::reject_status() const noexcept {
    return detail::forms_verdict::reject_status(status);
}

http::fields multipart_read::reject_fields() const {
    return detail::forms_verdict::reject_fields();
}

http::outcome decode_multipart(std::span<const std::byte> body,
                               std::optional<std::string_view> content_type,
                               const multipart_limits& limits,
                               part_sink& sink, multipart_read& out) {
    std::string boundary;
    const http::outcome bounded =
        detail::extract_boundary(content_type, boundary);
    if (!bounded.ok()) return bounded;

    sink_event_adapter adapter(sink);
    detail::multipart_decoder decoder(boundary, limits.max_total_bytes,
                                      limits.max_parts,
                                      limits.max_part_bytes,
                                      limits.max_part_header_bytes,
                                      adapter);
    const http::outcome fed = decoder.feed(body);
    const http::outcome done = fed.ok() ? decoder.finish() : fed;
    if (!done.ok()) {
        adapter.forward_abort(done);
        return done;
    }
    out = multipart_read{http::outcome::okay(), adapter.completed()};
    return http::outcome::okay();
}

task<multipart_read> read_multipart(exchange& x,
                                    const multipart_limits& limits,
                                    part_sink& sink) {
    // The read owns its admission, so the byte cap rides the engine
    // side too; a caller that admitted already gets invalid_state.
    const http::outcome admitted =
        x.admit_body(body_policy{limits.max_total_bytes});
    if (!admitted.ok()) co_return multipart_read{admitted, 0};

    std::string boundary;
    const http::outcome bounded = detail::extract_boundary(
        x.head().head_fields.first(k_content_type_field), boundary);
    if (!bounded.ok()) co_return multipart_read{bounded, 0};

    sink_event_adapter adapter(sink);
    detail::multipart_decoder decoder(boundary, limits.max_total_bytes,
                                      limits.max_parts,
                                      limits.max_part_bytes,
                                      limits.max_part_header_bytes,
                                      adapter);
    std::byte chunk[kReadChunkBytes];
    for (;;) {
        const body_read read = co_await x.body().read_some(
            std::span<std::byte>(chunk, kReadChunkBytes));
        if (!read.status.ok()) {
            // Disconnect or cancellation: the begun part aborts once.
            adapter.forward_abort(read.status);
            co_return multipart_read{read.status, adapter.completed()};
        }
        if (read.end_of_body) break;
        const http::outcome fed = decoder.feed(read.data);
        if (!fed.ok()) {
            adapter.forward_abort(fed);
            co_return multipart_read{fed, adapter.completed()};
        }
    }
    const http::outcome done = decoder.finish();
    if (!done.ok()) {
        adapter.forward_abort(done);
        co_return multipart_read{done, adapter.completed()};
    }
    co_return multipart_read{http::outcome::okay(), adapter.completed()};
}

// The adapter's per-request sequence, named so the factory and the
// sequence each read under the complexity gate.

server::route_handler make_multipart_route(multipart_limits limits,
                                            multipart_route_handler handler) {
    if (limits.max_total_bytes < 1 || limits.max_parts < 1
            || limits.max_part_bytes < 1 || limits.max_part_header_bytes < 1
            || !handler) {
        throw std::invalid_argument(
            "make_multipart_route: every cap must be at least 1 and "
            "the handler must not be empty");
    }
    return [call = std::move(handler), caps = limits](
               exchange& x) -> task<void> {
        co_return co_await run_multipart_route(x, caps, call);
    };
}

}  // namespace forms

}  // namespace httpserver
