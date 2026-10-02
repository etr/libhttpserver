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

// Streaming multipart/form-data handling with bounded admission
// (TASK-117, PRD-V3N-REQ-021/025/038, DR-V3-001). The surface is
// vocabulary only: the strict incremental wire decoder and the
// adapters live in the library's private implementation.
//
// The v3 shape is a streaming part visitor (forms::part_sink): the
// library frames the body and hands every part's begin/data/end to
// the caller, so file bytes never enter a request-wide argument map
// (v2 concatenated every upload into the flat arg map -- an
// unbounded-copy antipattern that is migration-noted away). The
// library ships one concrete sink, forms::temp_file_part_sink, that
// ports v2's disk-upload behavior: random or sanitized client
// filenames under a directory, removal of partial files, and one
// should_keep consultation per completed file at destruction.
//
// Documented hooks (this surface is the TASK-117 hook point; the
// server-wide lifecycle bus wiring, v2's request_completed on abort,
// is TASK-118):
//   - part_sink::on_part_abort fires EXACTLY ONCE for every part that
//     began but did not end, on every non-ok terminal: client
//     disconnect (cancelled / connection_closed), engine framing
//     failure, any typed limit or malformed rejection, and any
//     sink-returned failure;
//   - temp_file_part_sink's should_keep callback is consulted EXACTLY
//     ONCE per completed file when the sink is destroyed (a throwing
//     callback keeps the v2 rule: the file is removed), defaulting to
//     removal.
//
// Bounded deltas over v2 (migration-noted in the parity inventory):
// the four multipart_limits budgets (raw body bytes, begun parts, one
// part's decoded bytes, one part's header block) are typed
// limit_exceeded rejections answered with 413 BEFORE any unbounded
// storage -- v2's effective default was unbounded
// (content_size_limit = SIZE_MAX) -- and the strictness deltas
// (missing boundaries, malformed Content-Disposition, invalid
// boundary charset, header lines without a colon) are typed
// invalid_argument rejections answered with 400, where MHD silently
// produced no parts. Exactly-at-cap succeeds everywhere.

#ifndef SRC_HTTPSERVER_FORMS_MULTIPART_HPP_
#define SRC_HTTPSERVER_FORMS_MULTIPART_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace detail {

class multipart_temp_state;  // temp_file_part_sink's library-side members

}  // namespace detail

namespace forms {

// Bounded-admission budgets for one multipart body. The defaults
// mirror v2's argument budgets (DEFAULT_MAX_ARGS_BYTES /
// DEFAULT_MAX_ARGS_COUNT); v2's effective multipart default was
// UNBOUNDED (content_size_limit = SIZE_MAX), a delta the parity
// inventory records. Exactly-at-cap succeeds; a cap of zero would
// disable the bound, so create() refuses every zero cap.
struct multipart_limits {
    std::uint64_t max_total_bytes = 65536;       // raw body bytes fed
    std::uint64_t max_parts = 64;                // begun parts
    std::uint64_t max_part_bytes = 65536;        // one part's data bytes
    std::uint64_t max_part_header_bytes = 8192;  // one part's header block

    // Validates every cap (each at least 1) and lands them in @p out;
    // on rejection @p out is untouched.
    static http::outcome create(std::uint64_t max_total_bytes,
                                std::uint64_t max_parts,
                                std::uint64_t max_part_bytes,
                                std::uint64_t max_part_header_bytes,
                                multipart_limits& out);
};

// The identity of one part, as the wire machine parsed it from its
// header block. The views address library-owned storage and are valid
// only inside the on_part_begin callback (copy what is kept).
struct part_descriptor {
    std::string_view name;               // Content-Disposition name
    std::string_view filename;           // empty when a field part
    std::string_view content_type;       // part Content-Type value
    std::string_view transfer_encoding;  // part Transfer-Encoding value
};

// Application-supplied streaming consumer of one multipart body: the
// library frames the parts and hands the bytes over. Every callback
// returns an outcome; a non-ok return becomes the decode's terminal
// failure (sticky) and fires on_part_abort for the in-flight part.
// on_part_abort fires exactly once for every part that began but did
// not end. Not thread-safe: callbacks run on the reader's thread.
class part_sink {
 public:
    virtual ~part_sink() = default;
    virtual http::outcome on_part_begin(const part_descriptor&) = 0;
    virtual http::outcome on_part_data(
        std::span<const std::byte>) = 0;
    virtual http::outcome on_part_end() = 0;
    virtual void on_part_abort(http::outcome reason) = 0;
};

// A completed file part on disk (the temp-file sink's ledger entry).
struct part_file_info {
    std::string name;                   // the part's name parameter
    std::string filename;               // the client-supplied filename
    std::string file_system_file_name;  // the created path
    std::string content_type;           // the part's Content-Type
    std::uint64_t file_size = 0;        // bytes written
};

// The verdict of one bounded multipart read or decode. status is ok
// exactly when every framed part ran to its end; parts_completed
// counts them.
struct multipart_read {
    http::outcome status;
    std::uint64_t parts_completed = 0;

    bool ok() const noexcept;

    // The status the two form rejections answer with:
    // limit_exceeded -> 413, invalid_argument (malformed multipart or
    // an unsafe filename) -> 400. Any other failure (transport,
    // cancellation, a sink/filesystem error) carries no committable
    // status: the result is invalid and reject_status() reports 0.
    http::status reject_status() const noexcept;

    // The response fields for a rejection: an explicit
    // Content-Length: 0 (the v2 parity framing).
    http::fields reject_fields() const;
};

// Decodes one complete raw multipart body under @p limits, taking the
// boundary from @p content_type. On success @p out carries the
// verdict; on a typed rejection @p out is untouched and the sink saw
// on_part_abort exactly once for its in-flight part (a boundary
// problem before any part began aborts nothing).
http::outcome decode_multipart(std::span<const std::byte> body,
                                std::optional<std::string_view> content_type,
                                const multipart_limits& limits,
                                part_sink& sink, multipart_read& out);

// Streaming bounded read of the exchange's multipart body: admits it
// under limits.max_total_bytes, feeds the incremental decoder through
// ~512-byte reads (the raw body is never buffered whole), drives @p
// sink, and fires on_part_abort exactly once per begun part on every
// non-ok end (disconnect, cancellation, rejection, sink failure). The
// sink must outlive the task. A caller that already admitted the body
// gets invalid_state; a Content-Type without a usable boundary is a
// typed invalid_argument verdict.
task<multipart_read> read_multipart(exchange& x,
                                    const multipart_limits& limits,
                                    part_sink& sink);

// Application-supplied handler of one decoded multipart form (the
// value adapter's callback, the sync-route shape). form_fields is the
// urlencoded surface's type: non-file parts land in it with the same
// contract (arrival-order repeats, first-value lookup).
using multipart_route_handler = concurrency::unique_function<server::sync_response(const http::request_head&, const form_fields&)>;

// Wraps a value-returning handler with the whole bounded multipart
// sequence: admit under the byte cap, stream the parts through the
// adapter's internal sink (non-file parts decode into form_fields;
// FILE parts are drained under the same caps and never buffered),
// answer a typed rejection with 413/400 and a length-framed empty
// body without invoking the handler, apply the v2 content-type gate
// (a non-multipart type still drains under the cap and reaches the
// handler with EMPTY fields -- v2 ran no form processing there
// either), invoke, and commit the returned value (auto
// Content-Length when the handler framed nothing -- the sync-route
// rules). Throws std::invalid_argument at registration time for a
// zero cap or an empty handler. Applications that need the file bytes
// register a coroutine route and drive read_multipart with their own
// part_sink (temp_file_part_sink ports v2's disk-upload behavior).
server::route_handler make_multipart_route(multipart_limits limits,
                                            multipart_route_handler handler);

// The completed-file consultation callback (v2's
// file_cleanup_callback shape): return true to keep the file.
using part_keep_callback = concurrency::unique_function<bool(
    const std::string& name, const std::string& filename,
    const part_file_info& file)>;

// temp_file_part_sink's options.
struct temp_file_options {
    // Destination directory for file parts (v2's file_upload_dir).
    std::string directory;

    // v2's generate_random_filename_on_upload: random on-disk names
    // (the v3 entropy seam) instead of sanitized client filenames.
    bool random_names = true;

    // Consulted exactly once per COMPLETED file at sink destruction:
    // true keeps the file, false (and a throwing callback) removes it
    // -- the direct port of v2's file_cleanup_callback, whose default
    // removed every upload.
    part_keep_callback should_keep;
};

// v2's disk-upload behavior as one part_sink: writes file parts under
// the configured directory (random names, or the sanitized basename
// of the client filename -- a traversal-shaped name is a typed
// invalid_argument rejection), records their metadata, removes the
// partial file on abort and idempotently at destruction, and consults
// should_keep once per completed file at destruction (default
// remove). Field parts decode into a form_fields available through
// take_fields(). Members live in the library.
class temp_file_part_sink final : public part_sink {
 public:
    // The v2 defaults: /tmp with random names and nothing kept.
    temp_file_part_sink();

    explicit temp_file_part_sink(temp_file_options options);

    ~temp_file_part_sink() override;

    temp_file_part_sink(const temp_file_part_sink&) = delete;
    temp_file_part_sink& operator=(const temp_file_part_sink&) = delete;

    http::outcome on_part_begin(const part_descriptor& part) override;
    http::outcome on_part_data(std::span<const std::byte> data) override;
    http::outcome on_part_end() override;
    void on_part_abort(http::outcome reason) override;

    // Every completed (ended) file part, in arrival order.
    const std::vector<part_file_info>& completed() const noexcept;

    // The decoded field parts (a form_fields over non-file parts).
    form_fields take_fields();

 private:
    std::unique_ptr<detail::multipart_temp_state> state_;
};

}  // namespace forms

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_FORMS_MULTIPART_HPP_
