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
// decode_multipart, and the abort-once event adapter that turns the
// pure wire machine's events into the documented part_sink callbacks.
// The streaming read, the route adapter, and the temp-file sink are
// forms_multipart_files.cpp's neighbors (step 3).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <httpserver/detail/forms_multipart.hpp>
#include <httpserver/detail/forms_verdict.hpp>
#include <httpserver/forms/multipart.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace forms {

namespace {

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

}  // namespace forms

}  // namespace httpserver
