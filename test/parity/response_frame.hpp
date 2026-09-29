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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-096: incremental response-frame parser for the transcript runner.
//
// Converts a raw response byte stream (fed in arbitrary segments, as a
// real TCP connection delivers it) into observed_response values: raw
// status line, status code, ordered headers (plus chunk trailers), body
// bytes, and how the body was framed on the wire.
//
// Framing understood:
//   * Content-Length              -> framing "content-length"
//   * Transfer-Encoding: chunked  -> framing "chunked" (trailers captured)
//   * neither                     -> framing "none", body runs until EOF
//                                    (reported by finish())
//   * 1xx / 204 / 304             -> framing "none", no body by RFC 7230
//
// Errors (malformed status line, invalid chunk size, declared body above
// max_response_bytes, truncation at EOF) are recorded in error() and
// stall the parser -- never a hang.
//
// Pure: fed from memory, no sockets, no libhttpserver.

#ifndef TEST_PARITY_RESPONSE_FRAME_HPP_
#define TEST_PARITY_RESPONSE_FRAME_HPP_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace parity {

struct observed_header {
    std::string name;
    std::string value;
};

struct observed_response {
    std::string raw_status_line;
    int status = 0;
    std::vector<observed_header> headers;
    std::string body;
    // "content-length" | "chunked" | "none"
    std::string framing;
};

class response_frame_parser {
 public:
    // Ingest bytes; returns every response completed by this feed (a
    // single pipelined segment may complete several).
    std::vector<observed_response> feed(std::string_view bytes);

    // Signal EOF: emits a pending until-close response, if any. A
    // response truncated mid-frame records an error instead.
    std::vector<observed_response> finish();

    // Non-empty once a parse error occurred; the parser ignores further
    // feeds after an error.
    const std::string& error() const;

    // True once a parse error occurred (error() carries the detail).
    bool failed() const { return error_; }

    // True while an until-close response is being accumulated: the
    // runner reads until EOF before finish() can emit it.
    bool pending_until_close() const { return mode_ == mode::until_close; }

    // HEAD request context (RFC 7231 §4.3.2): the response completes
    // after the header block even when it declares a body length, and
    // no body bytes are expected on the wire.
    void set_head_only(bool head_only) { head_only_ = head_only; }

    // Memory guard: responses declaring more body than this are an
    // error, not an allocation.
    static constexpr std::size_t max_response_bytes = 4u * 1024u * 1024u;

 private:
    enum class mode { head, headers, content_length, chunked, chunk_trailers, until_close };

    void consume();
    bool parse_head_line();
    bool parse_header_line();
    void decide_framing();
    bool parse_content_length();
    bool parse_chunk();
    bool parse_trailer_line();
    void finish_response();
    void fail(const std::string& message);
    void check_truncation();

    std::string buf_;
    mode mode_ = mode::head;
    std::string raw_status_line_;
    int status_ = 0;
    std::vector<observed_header> headers_;
    std::string body_;
    std::string framing_;
    std::vector<observed_response> completed_;
    std::size_t content_remaining_ = 0;
    std::size_t chunk_remaining_ = 0;
    bool head_only_ = false;
    bool error_ = false;
    std::string error_text_;
};

}  // namespace parity

#endif  // TEST_PARITY_RESPONSE_FRAME_HPP_
